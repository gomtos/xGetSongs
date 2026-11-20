#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include <thread>

#include <windows.h>

#include "fs/file_ops.h"

using namespace Qt::StringLiterals;
using xgs::fs::MoveStatus;

namespace {

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray("<unreadable>");
    return file.readAll();
}

// An empty directory of any length (an absolute path with `\`), through Win32's extended path form.
bool removeLongDirectory(const QString& path) {
    const QString extended = u"\\\\?\\"_s + path;
    return RemoveDirectoryW(reinterpret_cast<const wchar_t*>(extended.utf16())) != 0;
}

// QTemporaryDir gives `/`; the engine's paths use `\`.
QString windowsPath(const QString& path) {
    QString out = path;
    return out.replace(u'/', u'\\');
}

}  // namespace

class FileOpsTest : public QObject {
    Q_OBJECT

private slots:
    void moveReplacesAnExistingTargetWhenAsked() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString from = windowsPath(dir.filePath(u"from.m4a"_s));
        const QString to = windowsPath(dir.filePath(u"to.m4a"_s));
        writeFile(from, "new");
        writeFile(to, "old");
        const xgs::fs::MoveResult result = xgs::fs::moveFile(from, to, true);
        QCOMPARE(result.status, MoveStatus::Moved);
        QVERIFY(!QFile::exists(from));
        QCOMPARE(readFile(to), QByteArray("new"));
    }

    void moveReportsAnExistingTargetWhenNotAskedToReplace() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString from = windowsPath(dir.filePath(u"from.m4a"_s));
        const QString to = windowsPath(dir.filePath(u"to.m4a"_s));
        writeFile(from, "new");
        writeFile(to, "old");
        const xgs::fs::MoveResult result = xgs::fs::moveFile(from, to, false);
        QCOMPARE(result.status, MoveStatus::TargetExists);
        QCOMPARE(readFile(from), QByteArray("new"));
        QCOMPARE(readFile(to), QByteArray("old"));
    }

    void moveFailsWithAMessageWhenTheSourceIsMissing() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString to = windowsPath(dir.filePath(u"to.m4a"_s));
        QElapsedTimer timer;
        timer.start();
        const xgs::fs::MoveResult result = xgs::fs::moveFile(windowsPath(dir.filePath(u"missing.m4a"_s)), to, true);
        const qint64 elapsed = timer.elapsed();
        QCOMPARE(result.status, MoveStatus::Failed);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(result.code, quint32(ERROR_FILE_NOT_FOUND));
        QVERIFY(!QFile::exists(to));
        // a missing source is not retried: it fails at once, not after the retry budget (about 1.5 s)
        QVERIFY2(elapsed < 1000, qPrintable(u"a missing source took %1 ms"_s.arg(elapsed)));
    }

    // A difference from Kotlin: a target that another program holds open for a moment (as a virus scanner may hold a file
    // just written) is replaced once it lets go, within the retry budget.
    void moveRetriesWhileTheTargetIsHeldForAMoment() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString from = windowsPath(dir.filePath(u"from.m4a"_s));
        const QString to = windowsPath(dir.filePath(u"to.m4a"_s));
        writeFile(from, "new");
        writeFile(to, "old");
        // opened without FILE_SHARE_DELETE: replacing the file fails while this handle is open
        const HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(to.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        std::jthread release([held] {
            Sleep(300);
            CloseHandle(held);
        });
        const xgs::fs::MoveResult result = xgs::fs::moveFile(from, to, true);
        release.join();
        QVERIFY2(result.status == MoveStatus::Moved, qPrintable(u"code %1: %2"_s.arg(result.code).arg(result.error)));
        QVERIFY(!QFile::exists(from));
        QCOMPARE(readFile(to), QByteArray("new"));
    }

    // A target held for longer than the retry budget: Failed with Windows' text and code, and the source stays.
    void moveFailsWithTheCodeWhenTheTargetStaysHeld() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString from = windowsPath(dir.filePath(u"from.m4a"_s));
        const QString to = windowsPath(dir.filePath(u"to.m4a"_s));
        writeFile(from, "new");
        writeFile(to, "old");
        const HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(to.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        QElapsedTimer timer;
        timer.start();
        const xgs::fs::MoveResult result = xgs::fs::moveFile(from, to, true);
        const qint64 elapsed = timer.elapsed();
        CloseHandle(held);
        QCOMPARE(result.status, MoveStatus::Failed);
        QVERIFY(!result.error.isEmpty());
        QVERIFY2(result.code == ERROR_ACCESS_DENIED || result.code == ERROR_SHARING_VIOLATION,
                 qPrintable(u"code %1: %2"_s.arg(result.code).arg(result.error)));
        QCOMPARE(readFile(from), QByteArray("new"));
        QCOMPARE(readFile(to), QByteArray("old"));
        // it was retried for about the budget (1.52 s of pauses), not much longer
        QVERIFY2(elapsed >= 1000 && elapsed < 10000, qPrintable(u"gave up after %1 ms"_s.arg(elapsed)));
    }

    void writeUtf8WritesNoByteOrderMark() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = windowsPath(dir.filePath(u"meta.ffmeta"_s));
        writeFile(path, "a much longer old content");
        const xgs::Outcome<std::monostate> outcome = xgs::fs::writeUtf8(path, u"한글\n"_s);
        QVERIFY(outcome.isOk());
        // (the plan wrote EC 95 9C; U+D55C in UTF-8 is ED 95 9C, and Kotlin's Files.writeString writes ED 95 9C EA B8 80 0A)
        QCOMPARE(readFile(path), QByteArray::fromHex("ED959CEAB8800A"));
    }

    // Step 3 of the Q2b Task 1 report: Kotlin's Files.writeString(path, text, UTF_8) throws (UnmappableCharacterException,
    // "Input length = 1") for an unpaired surrogate, high or low, before it opens the file: no file is created and an existing
    // file keeps its content.
    void writeUtf8HandlesALoneSurrogateLikeKotlin() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString high = u"a"_s + QChar(char16_t(0xD83D)) + u"b"_s;
        const QString low = u"a"_s + QChar(char16_t(0xDE00)) + u"b"_s;
        const QString reversed = QString(QChar(char16_t(0xDE00))) + QChar(char16_t(0xD83D));
        int n = 0;
        for (const QString& text : {high, low, reversed}) {
            const QString fresh = windowsPath(dir.filePath(u"fresh%1.txt"_s.arg(n)));
            const xgs::Outcome<std::monostate> created = xgs::fs::writeUtf8(fresh, text);
            QCOMPARE(created.status, xgs::OutcomeStatus::Failed);
            QCOMPARE(created.error, u"Input length = 1"_s);
            QVERIFY(!QFile::exists(fresh));

            const QString existing = windowsPath(dir.filePath(u"existing%1.txt"_s.arg(n)));
            writeFile(existing, "OLD");
            const xgs::Outcome<std::monostate> replaced = xgs::fs::writeUtf8(existing, text);
            QCOMPARE(replaced.status, xgs::OutcomeStatus::Failed);
            QCOMPARE(readFile(existing), QByteArray("OLD"));
            ++n;
        }
        // a surrogate pair is a character like any other
        const QString paired = windowsPath(dir.filePath(u"paired.txt"_s));
        QVERIFY(xgs::fs::writeUtf8(paired, u"ok😀"_s).isOk());
        QCOMPARE(readFile(paired), QByteArray::fromHex("6F6BF09F9880"));
    }

    void pathsLongerThan260Work() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // a directory under 248 units (QTemporaryDir must be able to remove it), and files of 300 units and more in it
        const QString base = windowsPath(dir.path());
        const QString folder = base + u'\\' + QString(239 - base.size(), u'd');
        QCOMPARE(folder.size(), 240);
        QVERIFY(xgs::fs::makeDirectories(folder));
        const QString from = folder + u'\\' + QString(60, u'f') + u".m4a"_s;
        const QString to = folder + u'\\' + QString(70, u't') + u".m4a"_s;
        QVERIFY(from.size() >= 300);
        QVERIFY(to.size() >= 300);

        QVERIFY(xgs::fs::writeBytes(from, "audio").isOk());
        QVERIFY(xgs::fs::isRegularFile(from));
        QCOMPARE(xgs::fs::moveFile(from, to, false).status, MoveStatus::Moved);
        QVERIFY(!xgs::fs::isRegularFile(from));
        QVERIFY(xgs::fs::isRegularFile(to));
        QVERIFY(xgs::fs::writeUtf8(from, u"한글"_s).isOk());
        QCOMPARE(xgs::fs::moveFile(from, to, false).status, MoveStatus::TargetExists);
        QCOMPARE(xgs::fs::moveFile(from, to, true).status, MoveStatus::Moved);
        QVERIFY(!xgs::fs::isRegularFile(from));
        xgs::fs::removeQuietly(to);
        QVERIFY(!xgs::fs::isRegularFile(to));
        QVERIFY(!xgs::fs::isRegularFile(folder));   // a directory is not a regular file

        // a directory of more than 260 units is created too (and removed here: QTemporaryDir may not reach it)
        const QString deep = folder + u'\\' + QString(30, u'e') + u'\\' + QString(30, u'g');
        QVERIFY(deep.size() > 260);
        QVERIFY(xgs::fs::makeDirectories(deep));
        QVERIFY(xgs::fs::makeDirectories(deep));   // already there
        const QString inDeep = deep + u"\\x.txt"_s;
        QVERIFY(xgs::fs::writeBytes(inDeep, "x").isOk());
        QVERIFY(xgs::fs::isRegularFile(inDeep));
        xgs::fs::removeQuietly(inDeep);
        QVERIFY(!xgs::fs::isRegularFile(inDeep));
        QVERIFY(removeLongDirectory(deep));
        QVERIFY(removeLongDirectory(folder + u'\\' + QString(30, u'e')));
    }

    void removeQuietlyIgnoresAMissingFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString missing = windowsPath(dir.filePath(u"missing.m4a"_s));
        xgs::fs::removeQuietly(missing);
        QVERIFY(!QFile::exists(missing));
        const QString present = windowsPath(dir.filePath(u"present.m4a"_s));
        writeFile(present, "x");
        xgs::fs::removeQuietly(present);
        QVERIFY(!QFile::exists(present));
    }

    void existsSeesFilesAndDirectories() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString base = windowsPath(dir.path());
        QVERIFY(xgs::fs::exists(base));   // a directory exists
        const QString file = base + u"\\a.m4a"_s;
        QVERIFY(!xgs::fs::exists(file));
        writeFile(file, "x");
        QVERIFY(xgs::fs::exists(file));
        // a path of more than 260 units (the folder stays under 248 units, so QTemporaryDir can remove it)
        if (base.size() > 200) QSKIP("the temporary folder's path is too long for this test");
        const QString folder = base + u'\\' + QString(239 - base.size(), u'd');
        QVERIFY(xgs::fs::makeDirectories(folder));
        const QString longFile = folder + u'\\' + QString(60, u'f') + u".m4a"_s;
        QVERIFY(longFile.size() > 260);
        QVERIFY(!xgs::fs::exists(longFile));
        QVERIFY(xgs::fs::writeBytes(longFile, "x").isOk());
        QVERIFY(xgs::fs::exists(longFile));
        xgs::fs::removeQuietly(longFile);
        QVERIFY(!xgs::fs::exists(longFile));
    }
};

QTEST_GUILESS_MAIN(FileOpsTest)
#include "tst_file_ops.moc"
