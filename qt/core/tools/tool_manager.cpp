#include "tools/tool_manager.h"

#include <QStringList>

#include <climits>
#include <mutex>
#include <optional>
#include <span>
#include <utility>

#include "fs/file_ops.h"
#include "paths/paths.h"
#include "text/kt_text.h"

using namespace Qt::StringLiterals;

namespace xgs::tools {

namespace {

constexpr qsizetype MinYtDlpBytes = 1'000'000;
constexpr int DenoMinimum[] = {2, 3, 0};
constexpr int NodeMinimum[] = {22, 0, 0};

// The first non-blank stdout line of a run, trimmed (Kotlin's firstLine). None when the program cannot be started (Kotlin
// catches the IOException), exits with a code other than 0 or prints no such line.
struct FirstLine {
    bool cancelled = false;
    std::optional<QString> line;
};

FirstLine firstLine(process::ProcessRunner& runner, const QString& path, const QString& arg, std::stop_token stop) {
    std::mutex mutex;
    QStringList lines;
    const process::RunResult result = runner.run(
        {path, arg},
        [&](const QString& line) {
            const std::lock_guard lock(mutex);
            lines.append(line);
        },
        [](const QString&) {}, stop);
    if (result.status == process::RunStatus::Cancelled) return {true, std::nullopt};
    // (Failed: started but lost, like a start failure: Kotlin would have caught an IOException)
    if (result.status == process::RunStatus::StartFailed || result.status == process::RunStatus::Failed || result.exitCode != 0)
        return {};
    const std::lock_guard lock(mutex);
    for (const QString& line : lines)
        if (!kt::isBlank(line)) return {false, kt::trim(line)};
    return {};
}

QString removePrefix(const QString& text, QStringView prefix) {
    return kt::startsWith(text, prefix) ? text.sliced(prefix.size()) : text;
}

QString removeSuffix(const QString& text, QStringView suffix) {
    return kt::endsWith(text, suffix) ? text.first(text.size() - suffix.size()) : text;
}

QString substringBefore(const QString& text, QChar delimiter) {
    const qsizetype at = text.indexOf(delimiter);
    return at < 0 ? text : text.first(at);
}

// `part.takeWhile(Char::isDigit).toIntOrNull() ?: 0`: the leading digits (Unicode Nd, each worth Character.digit), 0 when there
// are none or their value does not fit an Int.
int leadingNumber(QStringView part) {
    qint64 value = 0;
    qsizetype digits = 0;
    for (const QChar c : part) {
        if (!kt::isDigit(c)) break;
        value = value * 10 + kt::digitValue(c);
        if (value > INT_MAX) return 0;
        ++digits;
    }
    return digits == 0 ? 0 : int(value);
}

bool isAtLeast(const QString& version, std::span<const int> minimum) {
    QList<int> parts;
    for (const QString& part : version.split(u'.')) parts.append(leadingNumber(part));
    for (qsizetype i = 0; i < qsizetype(minimum.size()); ++i) {
        const int actual = i < parts.size() ? parts.at(i) : 0;
        if (actual != minimum[size_t(i)]) return actual > minimum[size_t(i)];
    }
    return true;
}

}  // namespace

ToolManager::ToolManager(const ToolPathProvider& tools, process::ProcessRunner& runner, QString binDir, Fetch fetch)
    : m_tools(tools), m_runner(runner), m_binDir(std::move(binDir)), m_fetch(std::move(fetch)) {}

ToolsStatus ToolManager::status(std::stop_token stop) {
    const ToolPaths paths = m_tools.current();
    const auto info = [&](const std::optional<QString>& path, const QString& arg, auto parse) -> ToolInfo {
        if (!path) return ToolInfo{};
        const std::optional<QString> line = firstLine(m_runner, *path, arg, stop).line;
        const std::optional<QString> version = line ? std::optional<QString>(parse(*line)) : std::nullopt;
        return ToolInfo{version.has_value(), version, *path};
    };
    ToolsStatus status;
    status.ytDlp = info(paths.ytDlp, u"--version"_s, [](const QString& line) { return line; });
    status.ffmpeg = info(paths.ffmpeg, u"-version"_s,
                         [](const QString& line) { return substringBefore(removePrefix(line, u"ffmpeg version "), u' '); });
    if (paths.jsRuntime) {
        const QString& path = *paths.jsRuntime;
        const QString name = removeSuffix(kt::lowercase(paths::fileName(path).value_or(QString())), u".exe");
        const bool deno = name == u"deno";
        const std::optional<QString> line = firstLine(m_runner, path, u"--version"_s, stop).line;
        std::optional<QString> version;
        if (line) {
            if (deno) {
                const QStringList words = line->split(u' ');
                if (words.size() > 1) version = words.at(1);
            } else {
                version = removePrefix(*line, u"v");
            }
        }
        const bool supported = version && isAtLeast(*version, deno ? std::span<const int>(DenoMinimum) : std::span<const int>(NodeMinimum));
        status.jsRuntime = ToolInfo{supported, version, path};
    }
    return status;
}

Outcome<QString> ToolManager::installYtDlp(std::stop_token stop) {
    const Outcome<QByteArray> fetched = m_fetch(YtDlpUrl.toString(), stop);
    if (fetched.status == OutcomeStatus::Cancelled) return Outcome<QString>::cancelled();
    if (fetched.status == OutcomeStatus::Failed) return Outcome<QString>::failed(u"yt-dlp 다운로드에 실패했습니다: "_s + fetched.error);
    const QByteArray& bytes = *fetched.value;
    if (bytes.size() < MinYtDlpBytes) return Outcome<QString>::failed(u"내려받은 파일이 너무 작습니다. 다시 시도하세요."_s);

    // Stage next to the target, verify that it runs, and only then replace yt-dlp.exe, so a bad download never shadows (or
    // overwrites) a working binary.
    const QString staged = paths::child(m_binDir, u"yt-dlp.new.exe"_s).value_or(QString());
    const QString target = paths::child(m_binDir, u"yt-dlp.exe"_s).value_or(QString());
    // Best effort, on every way out: a stale yt-dlp.new.exe is harmless, but none is left.
    struct RemoveStaged {
        const QString& path;
        ~RemoveStaged() { fs::removeQuietly(path); }
    } removeStaged{staged};
    const auto installFailed = [](const QString& error) { return Outcome<QString>::failed(u"yt-dlp를 설치하지 못했습니다: "_s + error); };

    // Kotlin's createDirectories fails where a file is in the way; then the write fails too, with Windows' text.
    fs::makeDirectories(m_binDir);
    const Outcome<std::monostate> written = fs::writeBytes(staged, bytes);
    if (!written.isOk()) return installFailed(written.error);
    const FirstLine version = firstLine(m_runner, staged, u"--version"_s, stop);
    if (version.cancelled) return Outcome<QString>::cancelled();
    if (!version.line) return Outcome<QString>::failed(u"내려받은 yt-dlp를 실행할 수 없습니다."_s);
    if (stop.stop_requested()) return Outcome<QString>::cancelled();
    // Files.move with REPLACE_EXISTING replaces an empty directory (its documentation), not one with something inside.
    if (fs::isDirectory(target)) fs::removeEmptyDirectory(target);
    const fs::MoveResult moved = fs::moveFile(staged, target, true);
    if (moved.status != fs::MoveStatus::Moved) return installFailed(moved.error);
    return Outcome<QString>::ok(u"yt-dlp "_s + *version.line + u" 을(를) 설치했습니다."_s);
}

Outcome<QString> ToolManager::updateYtDlp(std::stop_token stop) {
    const std::optional<QString> ytDlp = m_tools.current().ytDlp;
    if (!ytDlp) return Outcome<QString>::failed(u"yt-dlp가 설치되어 있지 않습니다."_s);
    std::mutex mutex;
    QStringList lines;
    const process::LineHandler collect = [&](const QString& line) {
        const std::lock_guard lock(mutex);
        lines.append(line);
    };
    const process::RunResult result = m_runner.run({*ytDlp, u"--ignore-config"_s, u"-U"_s}, collect, collect, stop);
    if (result.status == process::RunStatus::Cancelled) return Outcome<QString>::cancelled();
    if (result.status == process::RunStatus::StartFailed || result.status == process::RunStatus::Failed)
        return Outcome<QString>::failed(u"yt-dlp를 실행할 수 없습니다: "_s + result.error);
    std::optional<QString> last;
    {
        const std::lock_guard lock(mutex);
        for (const QString& line : lines)
            if (!kt::isBlank(line)) last = line;
    }
    if (result.exitCode != 0) return Outcome<QString>::failed(last.value_or(u"yt-dlp 업데이트에 실패했습니다."_s));
    return Outcome<QString>::ok(last.value_or(u"yt-dlp를 업데이트했습니다."_s));
}

}  // namespace xgs::tools
