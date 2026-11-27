#pragma once

#include <QObject>
#include <QString>

#include <functional>

// One copy of the app at a time, with the names tauri-plugin-single-instance (MIT/Apache-2.0, 2.5.3, its Windows
// platform_impl) uses, so that the Qt build and the Tauri build of xGetSongs see each other (design spec §3: the two never run
// at the same time, so they never write the same settings or log file together):
//  - a named mutex `<id>-sim`, made owned; ERROR_ALREADY_EXISTS means another copy runs;
//  - the first copy keeps a hidden message window of class `<id>-sic` named `<id>-siw` (found with FindWindowW, so it is a
//    top-level window, never shown: unlike the plugin's it does not get WS_VISIBLE, which only its own WM_PAINT needed);
//  - a second copy allows the first to come to the front (AllowSetForegroundWindow), sends it WM_COPYDATA with dwData 1542 and
//    `cwd|arg0|arg1…` in UTF-8 with a NUL (SendMessageTimeoutW, 10 s), and ends.
// The id of the product is the Tauri app's identifier; the tests give a made-up one.
namespace xgs::app {

class SingleInstance final : public QObject {
    Q_OBJECT
public:
    /// `com.xgetsongs.shell`, the identifier in shell/src-tauri/tauri.conf.json (the plugin's `semver` feature is off there, so
    /// no version is added to it).
    static QString productId();

    /// Takes the mutex of id, or tells the copy that holds it. Made on the GUI thread (the message window belongs to it).
    explicit SingleInstance(QString id, QObject* parent = nullptr);
    ~SingleInstance() override;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    bool isFirst() const { return m_first; }

    /// What the first copy does when another one starts: called on the GUI thread, after the message has been answered. An
    /// empty function does nothing.
    void onSecondLaunch(std::function<void()> callback);

    /// For the message window only: a second copy has said that it started. Runs the callback a moment later (queued), so the
    /// second copy's SendMessageTimeoutW is answered first.
    void secondLaunchReported();

private:
    void tellTheFirst();

    QString m_id;
    bool m_first = false;
    void* m_mutex = nullptr;
    void* m_window = nullptr;
    bool m_classRegistered = false;
    std::function<void()> m_callback;
};

}  // namespace xgs::app
