#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <windows.h>
#include <wrl/client.h>

#include <WebView2.h>

// A WebView2 in this process that never appears on screen.
// The parent HWND is a top-level window that is never shown (no WS_VISIBLE, off screen,
// 800x600), and the controller is made visible inside it so page timers keep running.
// InPrivate profile; popups and downloads are refused. Every COM call happens on the GUI
// thread (Qt has initialised it as STA); construct, use and destroy it on that thread only.
class HiddenWebView : public QObject
{
    Q_OBJECT
public:
    explicit HiddenWebView(const QString &userDataFolder, QObject *parent = nullptr);
    ~HiddenWebView() override;

    void open(const QUrl &url);           // GUI thread only; queued until ready()
    void evaluate(const QString &script); // result arrives as evaluated()

signals:
    void ready();                         // environment and controller exist
    void failed(const QString &error);    // e.g. no runtime: never a crash, this signal
    void navigated(bool success);
    void evaluated(const QString &json);  // JSON string from ExecuteScript (empty on error)

private:
    void start();
    void onEnvironment(HRESULT result, ICoreWebView2Environment *env);
    void onController(HRESULT result, ICoreWebView2Controller *controller);
    void fail(const QString &error);

    QString m_userDataFolder;
    HWND m_host = nullptr;
    bool m_failed = false;
    QUrl m_pendingUrl;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_env;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webview;
};
