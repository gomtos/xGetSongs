#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

#include "diag/log_sink.h"
#include "model/models.h"

// The words the job log is written in (port of server/.../JobLog.kt), kept apart from the logging itself. A line holds ranks,
// video ids, statuses, counts, options, the lyrics outcome of a finished item (the name of the enum, such as `ONLINE`) and the
// reason text of a skipped or failed item, redacted, and nothing that names a song: no title, no file name, no path, no lyrics.
namespace xgs::diag {

struct JobLogLine {
    Level level = Level::Info;
    QString text;
    bool operator==(const JobLogLine&) const = default;
};

namespace JobLog {

/// The logger name of the job lines (Kotlin: com.xgetsongs.server.Jobs).
inline const QString LOGGER_NAME = QStringLiteral("xgs.jobs");

/// Longest reason text kept in a line; a yt-dlp error can run to several screens.
inline constexpr int MAX_REASON_LENGTH = 500;

inline const QString CANCEL_REQUESTED = QStringLiteral("취소 요청을 받음");

/// The part of a job's ID that tags its lines: enough to tell the jobs of one run apart.
QString shortId(const QString& jobId);

/// concurrency is the number of parallel downloads chosen. The album name the user typed is free text like the titles, so the
/// line only says that there was one.
QString started(const QString& jobId, InputKind kind, int itemCount, const JobOptions& options, int concurrency);

/// The line for the event stream of a job that ended without the job being done.
QString eventsDisconnected(const QString& jobId);

/// text without Windows paths and without the knownNames (the file name an item was started with, say): see LogRedaction.
QString redact(const QString& text, const QStringList& knownNames);

/// The line for event, or nullopt for the events that are not logged (progress comes many times a second). fileNames holds the
/// file name each rank was started with: the reason of a skipped or failed item is stripped of it.
std::optional<JobLogLine> describe(const JobEvent& event, const QHash<int, QString>& fileNames = {});

}  // namespace JobLog

}  // namespace xgs::diag
