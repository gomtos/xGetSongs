#include "labels.h"

#include <algorithm>
#include <cmath>

#include "filename/filename_formatter.h"
#include "text/kt_text.h"

using namespace Qt::StringLiterals;

namespace xgs::ui {

namespace {

/// A finished row says where its lyrics came from or why it has none. Plain `완료` when the engine did not report an outcome.
QString doneLabel(const std::optional<LyricsOutcome>& lyrics) {
    if (!lyrics) return u"완료"_s;
    switch (*lyrics) {
    case LyricsOutcome::Description: return u"완료 · 가사 ✓ 설명란"_s;
    case LyricsOutcome::Online: return u"완료 · 가사 ✓ 인터넷"_s;
    case LyricsOutcome::NotFound: return u"완료 · 가사 없음"_s;
    case LyricsOutcome::SearchOff: return u"완료 · 가사 없음 (검색 끔)"_s;
    }
    return u"완료"_s;
}

/// String(value).padStart(2, "0") for 0..59.
QString two(qint64 value) {
    return QString::number(value).rightJustified(2, u'0');
}

bool isAsciiDigit(QChar c) {
    return c.unicode() >= u'0' && c.unicode() <= u'9';
}

}  // namespace

QString updateYtDlpLabel() {
    return u"yt-dlp 업데이트"_s;
}

QString formatFailureHelp() {
    return u"재생목록의 모든 곡이 “m4a 오디오 형식이 없습니다.”로 실패하면 영상 문제가 아니라 yt-dlp가 낡았거나 "_s
           u"JavaScript 런타임(Node.js, Deno)에 문제가 있는 것일 수 있습니다. 앱 위쪽의 "_s +
           updateYtDlpLabel() +
           u" 버튼으로 최신 버전으로 바꾸고 Node.js 22+ 또는 Deno 2.3+가 설치돼 있는지 확인한 뒤 다시 받으세요."_s;
}

QString statusLabel(const ItemStatus& status) {
    switch (status.kind) {
    case ItemStatus::Kind::Ready: return u"준비됨"_s;
    case ItemStatus::Kind::Waiting: return u"대기 중"_s;
    case ItemStatus::Kind::Downloading:
        // Math.trunc; the engine never sends a percent that is not finite
        return status.percent ? u"다운로드 "_s + QString::number(qint64(std::trunc(*status.percent))) + u'%' : u"다운로드 중…"_s;
    case ItemStatus::Kind::Finishing: return u"마무리 중…"_s;
    case ItemStatus::Kind::Done: return doneLabel(status.lyrics);
    case ItemStatus::Kind::Skipped: return u"건너뜀: "_s + status.text;
    case ItemStatus::Kind::Failed: return u"실패: "_s + status.text;
    }
    return QString();
}

std::optional<QString> summaryText(const UiState& state) {
    if (!state.summary) return std::nullopt;
    const JobSummary& summary = *state.summary;
    const QString took = state.elapsedMs ? u" · 소요 "_s + elapsedLabel(*state.elapsedMs) : QString();
    const QString counts = u"성공 %1 · 건너뜀 %2 · 실패 %3"_s.arg(summary.succeeded).arg(summary.skipped).arg(summary.failed) + took;
    if (!state.jobStatus) return std::nullopt;
    switch (*state.jobStatus) {
    case JobStatus::Completed: return u"완료 — "_s + counts;
    case JobStatus::Cancelled: return u"취소됨 — "_s + counts;
    case JobStatus::Failed: return u"중단됨 — "_s + counts;
    }
    return std::nullopt;
}

QString elapsedLabel(qint64 elapsedMs) {
    const qint64 total = std::max<qint64>(elapsedMs / 1000, 0);  // Math.trunc: integer division rounds toward zero as well
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const qint64 seconds = total % 60;
    if (hours > 0) return u"%1시간 %2분 %3초"_s.arg(hours).arg(two(minutes), two(seconds));
    if (minutes > 0) return u"%1분 %2초"_s.arg(minutes).arg(two(seconds));
    return u"%1초"_s.arg(seconds);
}

std::optional<QString> destinationPath(const UiState& state) {
    if (!state.resolved) return std::nullopt;
    if (kt::trim(state.outputDir).isEmpty()) return std::nullopt;
    const std::optional<QString> folder =
        filename::destinationFolder(state.resolved->kind == InputKind::Playlist, state.resolved->playlistTitle, state.albumName);
    if (!folder) return state.outputDir;
    return kt::trimEndChars(state.outputDir, u"\\/") + u'\\' + *folder;
}

QString albumNameText(const UiState& state) {
    if (state.albumName) return *state.albumName;
    return state.resolved && state.resolved->kind == InputKind::Playlist ? state.resolved->playlistTitle.value_or(QString()) : QString();
}

std::optional<QString> destinationLabel(const UiState& state) {
    const std::optional<QString> path = destinationPath(state);
    if (!path) return std::nullopt;
    return u"저장 위치: "_s + *path;
}

QString rankLabel(int rank) {
    return QString::number(rank).rightJustified(3, u'0');
}

QString rankInputText(const QString& raw) {
    QString digits;
    for (QChar c : raw) {
        if (isAsciiDigit(c)) digits.append(c);
    }
    return digits.left(3);
}

std::optional<int> rankFromInput(const QString& text) {
    if (text.isEmpty()) return std::nullopt;
    qint64 value = 0;
    for (QChar c : text) {
        if (!isAsciiDigit(c)) return std::nullopt;
        // Number.parseInt of a long run of digits is huge, and huge is clamped to the largest rank: stop growing well above it.
        value = std::min<qint64>(value * 10 + (c.unicode() - u'0'), 1'000'000);
    }
    return int(std::clamp<qint64>(value, filename::MinRank, filename::MaxRank));
}

}  // namespace xgs::ui
