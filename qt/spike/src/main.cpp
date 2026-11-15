#include "report.h"

#include <QApplication>
#include <QEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <windows.h>

namespace {

// Milliseconds since this process was created (GetProcessTimes creation time).
qint64 msSinceProcessStart()
{
    FILETIME created, exited, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    GetSystemTimeAsFileTime(&now);
    auto toU64 = [](const FILETIME &f) {
        return (static_cast<quint64>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    return static_cast<qint64>((toU64(now) - toU64(created)) / 10000); // 100 ns ticks
}

// Measures: first Show of the window, then the first Paint of it (or a child), then the
// first 0 ms timer after that (event loop idle). Then writes "start" and quits.
class StartProbe : public QObject
{
public:
    explicit StartProbe(QWidget *window) : m_window(window) {}

    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (m_done || !obj->isWidgetType())
            return false;
        auto *w = static_cast<QWidget *>(obj);
        if (w->window() != m_window)
            return false;
        if (ev->type() == QEvent::Show && w == m_window)
            m_sawShow = true;
        else if (ev->type() == QEvent::Paint && m_sawShow && m_shownMs < 0) {
            m_shownMs = msSinceProcessStart();
            QTimer::singleShot(0, this, [this] {
                const qint64 idleMs = msSinceProcessStart();
                m_done = true;
                Report::put("start", QJsonObject{
                    {"processStartToShownMs", static_cast<int>(m_shownMs)},
                    {"processStartToIdleMs", static_cast<int>(idleMs)},
                });
                QApplication::quit();
            });
        }
        return false;
    }

private:
    QWidget *m_window;
    bool m_sawShow = false;
    bool m_done = false;
    qint64 m_shownMs = -1;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Command line contract: xgs-qt-spike.exe [--report <file>] [one switch]
    // Later tasks add switches by extending this list (and the dispatch below).
    const QStringList args = app.arguments().mid(1);
    QString mode; // empty = plain window
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a == "--report" && i + 1 < args.size())
            Report::setPath(args[++i]);
        else if (a == "--exit-after-show")
            mode = a;
    }

    QMainWindow window;
    window.setWindowTitle("xgs-qt-spike");

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(new QLineEdit);
    layout->addWidget(new QPlainTextEdit);
    window.setCentralWidget(central);
    window.resize(640, 480);

    if (mode == "--exit-after-show")
        app.installEventFilter(new StartProbe(&window));

    window.show();

    return app.exec();
}
