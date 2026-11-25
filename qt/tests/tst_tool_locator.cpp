#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include <optional>

#include "paths/paths.h"
#include "tools/tool_locator.h"

using namespace Qt::StringLiterals;
using xgs::ToolPaths;
using xgs::tools::ToolLocator;

// Port of ToolLocatorTest.kt: real files under a temporary directory.
class ToolLocatorTest : public QObject {
    Q_OBJECT

    // Made in initTestCase, not with the object: QTEST_APPLESS_MAIN builds the object before Qt Test reads the arguments, and
    // `-functions` ends the process without destroying it, which left a %TEMP%\qt_temp-* folder behind on every such call.
    std::optional<QTemporaryDir> m_temp;
    QString m_root, m_appBin, m_pathDir;

    static QString windowsPath(const QString& path) {
        QString out = path;
        return out.replace(u'/', u'\\');
    }
    static QString dirIn(const QString& parent, const QString& name) {
        const QString dir = *xgs::paths::child(parent, name);
        return QDir().mkpath(dir) ? dir : QString();
    }
    // Writes "x" to <dir>\<name> and returns the path.
    static QString touch(const QString& dir, const QString& name) {
        const QString path = *xgs::paths::child(dir, name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write("x") != 1) return QString();
        return path;
    }
    static QString at(const QString& dir, const QString& name) { return *xgs::paths::child(dir, name); }

    ToolLocator locator(std::optional<QString> ffmpegOverride = std::nullopt) const {
        return ToolLocator(m_appBin, m_pathDir + u";Z:\\does\\not\\exist"_s, ffmpegOverride);
    }

private slots:
    void initTestCase() { m_temp.emplace(); }
    void cleanupTestCase() { m_temp.reset(); }
    void init() {
        QVERIFY(m_temp && m_temp->isValid());
        m_root = *xgs::paths::fromText(windowsPath(m_temp->path()));
        m_appBin = dirIn(m_root, u"app-bin"_s);
        m_pathDir = dirIn(m_root, u"path-dir"_s);
        QVERIFY(!m_appBin.isEmpty());
        QVERIFY(!m_pathDir.isEmpty());
    }
    void cleanup() {
        // Each test starts from an empty tree (Kotlin makes a fresh temp directory per test instance).
        QDir(m_root).removeRecursively();
    }

    void findsToolsOnPath() {
        QVERIFY(!touch(m_pathDir, u"yt-dlp.exe"_s).isEmpty());
        QVERIFY(!touch(m_pathDir, u"ffmpeg.exe"_s).isEmpty());
        QVERIFY(!touch(m_pathDir, u"node.exe"_s).isEmpty());

        const ToolPaths paths = locator().current();

        QCOMPARE(paths.ytDlp, std::optional<QString>(at(m_pathDir, u"yt-dlp.exe"_s)));
        QCOMPARE(paths.ffmpeg, std::optional<QString>(at(m_pathDir, u"ffmpeg.exe"_s)));
        QCOMPARE(paths.jsRuntime, std::optional<QString>(at(m_pathDir, u"node.exe"_s)));
    }

    void appBinDirectoryWinsOverPath() {
        QVERIFY(!touch(m_pathDir, u"yt-dlp.exe"_s).isEmpty());
        QVERIFY(!touch(m_appBin, u"yt-dlp.exe"_s).isEmpty());

        QCOMPARE(locator().current().ytDlp, std::optional<QString>(at(m_appBin, u"yt-dlp.exe"_s)));
    }

    void denoIsPreferredOverNode() {
        QVERIFY(!touch(m_pathDir, u"node.exe"_s).isEmpty());
        QVERIFY(!touch(m_pathDir, u"deno.exe"_s).isEmpty());

        QCOMPARE(locator().current().jsRuntime, std::optional<QString>(at(m_pathDir, u"deno.exe"_s)));
    }

    void missingToolsAreNull() {
        const ToolPaths paths = locator().current();

        QVERIFY(!paths.ytDlp.has_value());
        QVERIFY(!paths.ffmpeg.has_value());
        QVERIFY(!paths.jsRuntime.has_value());
    }

    void ffmpegOverrideIsUsedWhenItExists() {
        QVERIFY(!touch(m_pathDir, u"ffmpeg.exe"_s).isEmpty());
        const QString custom = touch(m_root, u"my-ffmpeg.exe"_s);
        QVERIFY(!custom.isEmpty());

        QCOMPARE(locator(custom).current().ffmpeg, std::optional<QString>(custom));
        QCOMPARE(locator(at(m_root, u"gone.exe"_s)).current().ffmpeg, std::optional<QString>(at(m_pathDir, u"ffmpeg.exe"_s)));
    }

    void toolInstalledLaterIsPickedUpWithoutRestart() {
        const ToolLocator located = locator();
        QVERIFY(!located.current().ytDlp.has_value());

        QVERIFY(!touch(m_appBin, u"yt-dlp.exe"_s).isEmpty());

        QCOMPARE(located.current().ytDlp, std::optional<QString>(at(m_appBin, u"yt-dlp.exe"_s)));
    }
};

QTEST_APPLESS_MAIN(ToolLocatorTest)
#include "tst_tool_locator.moc"
