#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <optional>

#include "model/models.h"
#include "outcome.h"

// Everything the screen logic needs from the engine: the C++ form of the TypeScript `XgsApi` (shell/src/state/types.ts). The Tauri
// shell talked to a server, so it had resolve and job IDs and a list of ranks to retry; an engine in the same process needs none of
// them: the holder sends the items to download themselves.
//
// Every function is called on the GUI thread and every answer arrives on the GUI thread, after the call has returned (never from
// inside it). A function may throw (std::bad_alloc, std::system_error when a thread cannot be started, an exception of the code
// behind it); the holder shows that as a failure of the call.
namespace xgs::ui {

/// What startJob needs: the items to download with their final ranks (for a retry only the failed ones; for a single video its
/// one item with the rank the user chose) and the options of the job, together with what the lookup said about the list.
struct JobRequest {
    QList<ResolvedItem> items;
    bool isPlaylist = true;
    /// The title of the playlist: the album of its files and the name of its folder.
    std::optional<QString> playlistTitle;
    /// outputDir, overwrite, singleRank, includeRank, searchLyricsOnline and albumName (the typed album name, empty when the
    /// field was not touched or is blank).
    JobOptions options;
    bool operator==(const JobRequest&) const = default;
};

/// The answers about one job. started comes first and once: empty when the job runs, else the message why it could not start
/// (nothing else follows then). Then event for each event of the job, in order, and ended once when no event follows: empty
/// after the JobDone (or when the events stopped without one: the screen calls that a lost connection), else the message of a
/// failure that ended them.
struct JobCallbacks {
    std::function<void(std::optional<QString> error)> started;
    std::function<void(const JobEvent& event)> event;
    std::function<void(std::optional<QString> error)> ended;
};

class AppApi {
public:
    virtual ~AppApi() = default;

    virtual void tools(std::function<void(Outcome<ToolsStatus>)> done) = 0;
    /// Ok with the text to show, or Failed with why.
    virtual void installYtDlp(std::function<void(Outcome<QString>)> done) = 0;
    virtual void updateYtDlp(std::function<void(Outcome<QString>)> done) = 0;
    virtual void resolve(const QString& input, std::function<void(Outcome<ResolveResult>)> done) = 0;
    virtual void startJob(JobRequest request, JobCallbacks callbacks) = 0;
    /// Cancels the job started last. The job still ends with its JobDone.
    virtual void cancelJob() = 0;
    /// Ends everything for good: whatever runs is cancelled and waited for, and no answer is delivered any more, not even one
    /// that was already on its way. Later calls do nothing.
    virtual void stop() = 0;
};

}  // namespace xgs::ui
