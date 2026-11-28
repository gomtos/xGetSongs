#include "diag/job_log.h"

#include <type_traits>
#include <variant>

#include "log/log_redaction.h"
#include "text/kt_text.h"

namespace xgs::diag::JobLog {

namespace {

QString boolText(bool value) {
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString statusName(JobStatus status) {
    switch (status) {
        case JobStatus::Completed: return QStringLiteral("COMPLETED");
        case JobStatus::Cancelled: return QStringLiteral("CANCELLED");
        case JobStatus::Failed: return QStringLiteral("FAILED");
    }
    return QStringLiteral("FAILED");
}

QString outcomeName(LyricsOutcome outcome) {
    switch (outcome) {
        case LyricsOutcome::Description: return QStringLiteral("DESCRIPTION");
        case LyricsOutcome::Online: return QStringLiteral("ONLINE");
        case LyricsOutcome::NotFound: return QStringLiteral("NOT_FOUND");
        case LyricsOutcome::SearchOff: return QStringLiteral("SEARCH_OFF");
    }
    return QString();
}

// Char.isISOControl(): U+0000..001F and U+007F..009F.
bool isIsoControl(QChar c) {
    const char16_t u = c.unicode();
    return u <= 0x1F || (u >= 0x7F && u <= 0x9F);
}

// One line of at most MAX_REASON_LENGTH characters (and a `…` when it was cut): line breaks would split a log record.
QString oneLine(const QString& text) {
    QString collapsed;
    collapsed.reserve(text.size());
    bool afterSpace = false;
    for (const QChar c : text) {
        const bool space = isIsoControl(c) || kt::isWhitespace(c);
        if (space) {
            if (afterSpace) continue;   // runs of blanks become one
            collapsed += u' ';
        } else {
            collapsed += c;
        }
        afterSpace = space;
    }
    const QString flat = kt::trim(collapsed);
    if (flat.size() <= MAX_REASON_LENGTH) return flat;
    // Never end on the first half of a surrogate pair.
    const qsizetype end = kt::isHighSurrogate(flat[MAX_REASON_LENGTH - 1]) ? MAX_REASON_LENGTH - 1 : MAX_REASON_LENGTH;
    return flat.left(end) + QStringLiteral("…");
}

// Redacted before it is cut, so no part of a path survives the cut; and before the line breaks go, which end a path.
QString reason(const QString& text, int rank, const QHash<int, QString>& fileNames) {
    QStringList known;
    const auto name = fileNames.constFind(rank);
    if (name != fileNames.constEnd()) known << *name;
    return oneLine(redact(text, known));
}

}  // namespace

QString shortId(const QString& jobId) {
    return jobId.left(8);
}

QString started(const QString& jobId, InputKind kind, int itemCount, const JobOptions& options, int concurrency) {
    const QString kindText = kind == InputKind::Playlist ? QStringLiteral("재생목록") : QStringLiteral("영상");
    const bool albumTyped = options.albumName && !kt::isBlank(*options.albumName);
    return QStringLiteral("작업 시작: id=") + jobId + QStringLiteral(", 종류=") + kindText + QStringLiteral(", 항목=") +
           QString::number(itemCount) + QStringLiteral("개, overwrite=") + boolText(options.overwrite) +
           QStringLiteral(", includeRank=") + boolText(options.includeRank) + QStringLiteral(", concurrency=") +
           QString::number(concurrency) + QStringLiteral(", searchLyricsOnline=") + boolText(options.searchLyricsOnline) +
           QStringLiteral(", outputDir=") + (options.outputDir ? *options.outputDir : QStringLiteral("없음")) +
           (albumTyped ? QStringLiteral(", albumName=지정") : QString());
}

QString eventsDisconnected(const QString& jobId) {
    return QStringLiteral("이벤트 연결이 끊어짐 (작업 ") + shortId(jobId) + QStringLiteral(")");
}

QString redact(const QString& text, const QStringList& knownNames) {
    return redaction::redact(text, knownNames);
}

std::optional<JobLogLine> describe(const JobEvent& event, const QHash<int, QString>& fileNames) {
    return std::visit(
        [&](const auto& e) -> std::optional<JobLogLine> {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, ItemStarted>) {
                return JobLogLine{Level::Debug, QStringLiteral("항목 시작: 순위 ") + QString::number(e.rank) + QStringLiteral(", 영상 ") + e.videoId};
            } else if constexpr (std::is_same_v<T, Progress>) {
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, ItemDone>) {
                // The rank, and the name of the lyrics outcome when the event has one (`ONLINE`, not the lyrics and not the file name).
                QString text = QStringLiteral("항목 완료: 순위 ") + QString::number(e.rank);
                if (e.lyrics) text += QStringLiteral(", 가사 ") + outcomeName(*e.lyrics);
                return JobLogLine{Level::Info, text};
            } else if constexpr (std::is_same_v<T, ItemSkipped>) {
                return JobLogLine{Level::Info, QStringLiteral("항목 건너뜀: 순위 ") + QString::number(e.rank) + QStringLiteral(", 사유: ") +
                                                   reason(e.reason, e.rank, fileNames)};
            } else if constexpr (std::is_same_v<T, ItemFailed>) {
                return JobLogLine{Level::Warn, QStringLiteral("항목 실패: 순위 ") + QString::number(e.rank) + QStringLiteral(", 사유: ") +
                                                   reason(e.message, e.rank, fileNames)};
            } else {
                return JobLogLine{Level::Info, QStringLiteral("작업 종료: 상태=") + statusName(e.status) + QStringLiteral(", 성공 ") +
                                                   QString::number(e.summary.succeeded) + QStringLiteral(", 건너뜀 ") +
                                                   QString::number(e.summary.skipped) + QStringLiteral(", 실패 ") +
                                                   QString::number(e.summary.failed)};
            }
        },
        event);
}

}  // namespace xgs::diag::JobLog
