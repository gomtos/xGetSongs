#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <latch>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include <windows.h>

#include "fs/file_ops.h"
#include "output/local_folder_sink.h"
#include "paths/paths.h"

using namespace Qt::StringLiterals;
using xgs::output::LocalFolderSink;
using xgs::output::PutResult;
using xgs::output::PutStatus;

namespace {

QString windowsPath(const QString& path) {
    QString out = path;
    return out.replace(u'/', u'\\');
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray("<unreadable>");
    return file.readAll();
}

// One put of the overlapping-put tests: its result, or the text of what it threw.
struct Attempt {
    std::optional<PutResult> result;
    QString thrown;
};

// Removes, when it goes out of scope (also after a failed assertion), files and then directories of any length, in the order
// given: QTemporaryDir cannot remove a directory of more than 260 units.
struct LongPathCleanup {
    QStringList files;
    QStringList directories;   // the deepest first
    ~LongPathCleanup() {
        for (const QString& file : files) xgs::fs::removeQuietly(file);
        for (const QString& directory : directories) {
            const QString extended = u"\\\\?\\"_s + directory;
            RemoveDirectoryW(reinterpret_cast<const wchar_t*>(extended.utf16()));
        }
    }
};

}  // namespace

// Port of LocalFolderSinkTest.kt. Kotlin's FileAlreadyExistsException is PutStatus::AlreadyExists, its
// IllegalArgumentException is std::invalid_argument.
class LocalFolderSinkTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_temp;
    QString m_root, m_target;
    int m_sources = 0;

    // Files.createTempFile(root, "src", ".mp3") holding content
    QString source(const QString& content) {
        const QString file = *xgs::paths::child(m_root, u"src%1.mp3"_s.arg(++m_sources));
        if (!xgs::fs::writeUtf8(file, content).isOk()) throw std::runtime_error("the source was not written");
        return file;
    }

    // Replacing a file is not atomic on Windows, so puts of one name that overlap must not fail each other.
    std::vector<Attempt> putTogether(LocalFolderSink& sink, int count, bool overwrite) {
        std::vector<QString> sources;
        for (int i = 1; i <= count; ++i) sources.push_back(source(u"data-%1"_s.arg(i)));
        std::vector<Attempt> attempts(static_cast<size_t>(count));
        std::latch go(count);
        {
            std::vector<std::jthread> threads;
            for (int i = 0; i < count; ++i) {
                threads.emplace_back([&, i] {
                    go.arrive_and_wait();
                    try {
                        attempts[size_t(i)].result = sink.put(u"same.mp3"_s, sources[size_t(i)], overwrite);
                    } catch (const std::exception& e) {
                        attempts[size_t(i)].thrown = QString::fromUtf8(e.what());
                    }
                });
            }
        }
        return attempts;
    }

private slots:
    void init() {
        m_temp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temp->isValid());
        m_root = *xgs::paths::fromText(windowsPath(m_temp->path()));
        m_target = *xgs::paths::child(m_root, u"music/out"_s);
    }
    void cleanup() { m_temp.reset(); }

    void createsTheTargetDirectoryAndMovesTheFile() {
        LocalFolderSink sink(m_target);
        const QString source = this->source(u"data"_s);

        QCOMPARE(sink.put(u"001 A - B.mp3"_s, source, false).status, PutStatus::Stored);

        QCOMPARE(readBytes(*xgs::paths::child(m_target, u"001 A - B.mp3"_s)), QByteArray("data"));
        QVERIFY(!xgs::fs::exists(source));
    }

    void existsReflectsTheFolderContent() {
        LocalFolderSink sink(m_target);
        QVERIFY(!sink.exists(u"001 A - B.mp3"_s));

        QCOMPARE(sink.put(u"001 A - B.mp3"_s, source(u"x"_s), false).status, PutStatus::Stored);

        QVERIFY(sink.exists(u"001 A - B.mp3"_s));
    }

    void existingFilesAreKeptUnlessOverwriteIsOn() {
        LocalFolderSink sink(m_target);
        QCOMPARE(sink.put(u"a.mp3"_s, source(u"old"_s), false).status, PutStatus::Stored);

        QCOMPARE(sink.put(u"a.mp3"_s, source(u"new"_s), false).status, PutStatus::AlreadyExists);
        QCOMPARE(readBytes(*xgs::paths::child(m_target, u"a.mp3"_s)), QByteArray("old"));

        QCOMPARE(sink.put(u"a.mp3"_s, source(u"new"_s), true).status, PutStatus::Stored);
        QCOMPARE(readBytes(*xgs::paths::child(m_target, u"a.mp3"_s)), QByteArray("new"));
    }

    void overlappingPutsOfOneNameWithOverwriteAllSucceed() {
        LocalFolderSink sink(m_target);

        for (int round = 0; round < 20; ++round) {
            const std::vector<Attempt> attempts = putTogether(sink, 8, true);

            QStringList failures;
            for (const Attempt& attempt : attempts) {
                if (!attempt.thrown.isEmpty()) failures.append(attempt.thrown);
                else if (attempt.result->status != PutStatus::Stored) failures.append(u"status %1: %2"_s.arg(int(attempt.result->status)).arg(attempt.result->error));
            }
            QCOMPARE(failures, QStringList());
            QVERIFY(readBytes(*xgs::paths::child(m_target, u"same.mp3"_s)).startsWith("data-"));
        }
    }

    void overlappingPutsOfOneNameWithoutOverwriteLetExactlyOneWin() {
        LocalFolderSink sink(m_target);

        for (int round = 0; round < 20; ++round) {
            xgs::fs::removeQuietly(*xgs::paths::child(m_target, u"same.mp3"_s));
            const std::vector<Attempt> attempts = putTogether(sink, 8, false);

            int successes = 0;
            QStringList others;
            for (const Attempt& attempt : attempts) {
                if (attempt.thrown.isEmpty() && attempt.result->status == PutStatus::Stored) ++successes;
                else if (!attempt.thrown.isEmpty() || attempt.result->status != PutStatus::AlreadyExists)
                    others.append(attempt.thrown.isEmpty() ? attempt.result->error : attempt.thrown);
            }
            QCOMPARE(successes, 1);
            QVERIFY2(others.isEmpty(), qPrintable(others.join(u"; "_s)));
        }
    }

    void fileNamesWithPathSegmentsAreRejected() {
        LocalFolderSink sink(m_target);

        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, sink.put(u"../evil.mp3"_s, source(u"x"_s), false));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, sink.put(u"sub/evil.mp3"_s, source(u"x"_s), false));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, sink.exists(u".."_s));
    }

    // Added: the output folder and the target are longer than MAX_PATH (260 units), whatever the length of %TEMP%.
    void putWorksWhenTheTargetPathIsLongerThan260() {
        // Two folders of 100 units and one that brings the folder past 270 units, so the target is long even when %TEMP% is
        // short, and still is when it is long.
        const QString first = *xgs::paths::child(m_root, QString(100, u'a'));
        const QString second = *xgs::paths::child(first, QString(100, u'b'));
        const QString directory = *xgs::paths::child(second, QString(std::max<qsizetype>(10, 270 - second.size()), u'c'));
        const QString name = QString(40, u'n') + u".m4a"_s;
        const QString target = *xgs::paths::child(directory, name);
        const QString back = *xgs::paths::child(m_root, u"back.m4a"_s);
        LongPathCleanup cleanup{{target}, {directory, second, first}};
        QVERIFY(directory.size() > 260);
        QVERIFY(target.size() > 260);

        LocalFolderSink sink(directory);
        QCOMPARE(sink.directory(), directory);
        QVERIFY(!sink.exists(name));

        const PutResult stored = sink.put(name, source(u"data"_s), false);
        QCOMPARE(stored.status, PutStatus::Stored);
        QVERIFY(sink.exists(name));
        QVERIFY(xgs::fs::isRegularFile(target));
        QCOMPARE(sink.put(name, source(u"new"_s), false).status, PutStatus::AlreadyExists);
        QCOMPARE(sink.put(name, source(u"new"_s), true).status, PutStatus::Stored);

        // the content arrived: move it back to a short path and read it there
        QCOMPARE(xgs::fs::moveFile(target, back, false).status, xgs::fs::MoveStatus::Moved);
        QCOMPARE(readBytes(back), QByteArray("new"));
        QVERIFY(!sink.exists(name));
    }
};

QTEST_GUILESS_MAIN(LocalFolderSinkTest)
#include "tst_local_folder_sink.moc"
