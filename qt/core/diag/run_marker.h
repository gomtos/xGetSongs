#pragma once

#include <QByteArray>
#include <QString>
#include <QTimeZone>

#include <functional>
#include <optional>
#include <variant>

#include "diag/instant.h"

// `last-run.txt`, the marker that tells the next run whether this one ended properly. Port of RunMarker.kt (and Processes
// from Diagnostics.kt). The file is UTF-8 `key=value` lines, the format the Kotlin sidecar writes in the same folder.
namespace xgs::diag {

/// What this process and the others on the machine look like: the marker of the last run is judged with it.
class Processes {
public:
    virtual ~Processes() = default;
    virtual qint64 ownPid() const = 0;
    virtual Instant ownStart() const = 0;
    /// The start time of the live process with number pid, or nullopt when there is none (or it will not say).
    virtual std::optional<Instant> startOf(qint64 pid) const = 0;
};

/// The real process table (Win32: GetProcessTimes of a process that is still active).
class SystemProcesses final : public Processes {
public:
    qint64 ownPid() const override;
    Instant ownStart() const override;
    std::optional<Instant> startOf(qint64 pid) const override;
};

enum class RunState { Running, Exited };

/// What the marker file of a run says that matters to the next run: the state, and for a running one the pid and the start
/// time of that process (an exited marker may lack both).
struct RunMarker {
    RunState state = RunState::Exited;
    std::optional<qint64> pid;
    std::optional<Instant> started;
    bool operator==(const RunMarker&) const = default;
};

/// Reads the text of a marker file (UTF-8 `key=value` lines: unknown keys, blank lines, lines without `=`, a byte order mark
/// and Windows line ends are all fine). nullopt when it is not a marker: no `state`, an unknown one, or a running one without
/// a usable `pid` and `started`. A start time outside 2000-01-01..3000-01-01 counts as unusable.
std::optional<RunMarker> parseRunMarker(const QString& text);

/// The marker text of a run that has just started.
QString renderRunning(qint64 pid, const Instant& started);

/// The marker text of a run that is ending: userRequested is `reason=user`, anything else `reason=other`.
QString renderExited(qint64 pid, const Instant& started, bool userRequested, const Instant& time);

/// What the marker the previous run left says about that run.
struct PreviousRunNone {
    bool operator==(const PreviousRunNone&) const = default;
};
/// The process of the previous run is still alive: this is a second run of the app.
struct PreviousRunStillRunning {
    qint64 pid = 0;
    bool operator==(const PreviousRunStillRunning&) const = default;
};
/// The marker still says running, but the process is gone (or its number belongs to a newer process).
struct PreviousRunUnclean {
    qint64 pid = 0;
    Instant started;
    bool operator==(const PreviousRunUnclean&) const = default;
};
using PreviousRun = std::variant<PreviousRunNone, PreviousRunStillRunning, PreviousRunUnclean>;

/// A running marker counts as another live run only when startOf (the start time of the live process with that number, or
/// nullopt) gives exactly the recorded start time: Windows reuses process numbers.
PreviousRun judgePreviousRun(const std::optional<RunMarker>& previous,
                             const std::function<std::optional<Instant>(qint64)>& startOf);

/// A line for the log: warn says whether it is a warning.
struct PreviousRunNote {
    bool warn = false;
    QString text;
    bool operator==(const PreviousRunNote&) const = default;
};

/// The words for run (the start time in zone, the machine's by default), or nullopt when there is nothing to say.
std::optional<PreviousRunNote> describePreviousRun(const PreviousRun& run,
                                                   const QTimeZone& zone = QTimeZone::systemTimeZone());

/// The marker file of this run: markRunning when the app starts, markExited from the exit record. Neither throws, whatever goes
/// wrong. The text goes to a temp file next to the marker that is moved over it (replace), so a kill never leaves half a file
/// behind; when that fails (another process can hold the marker open) the marker is overwritten in place (overwrite) rather than
/// left saying `running` after a normal exit. A writing strategy gives false (or throws) when it failed; an empty one is the
/// real one.
class RunMarkerFile {
public:
    using Writer = std::function<bool(const QString& file, const QByteArray& bytes)>;

    RunMarkerFile(QString file, qint64 pid, Instant started, Writer replace = {}, Writer overwrite = {});

    /// The marker of the previous run, or nullopt when there is none or it cannot be read as one.
    std::optional<RunMarker> read() const;
    void markRunning() noexcept;
    void markExited(bool userRequested) noexcept;
    void markExited(bool userRequested, const Instant& time) noexcept;

private:
    void write(const QString& text) noexcept;

    QString m_file;
    qint64 m_pid;
    Instant m_started;
    Writer m_replace;
    Writer m_overwrite;
};

}  // namespace xgs::diag
