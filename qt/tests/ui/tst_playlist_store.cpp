#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <thread>
#include <vector>

#include "playlist_store.h"

// The playlist file (design spec §5.3): the format, the empty start, the broken file that is set aside as playlists.json.bad
// before anything is written over it, and the write that fails without touching the old file. Every file lives in a
// QTemporaryDir. All the names and IDs are made up.

using namespace Qt::StringLiterals;
using xgs::ui::Playlist;
using xgs::ui::PlaylistStore;

namespace {

const QList<Playlist> sample = {{u"더미 목록 하나"_s, u"PLdummyplaylistid0000000000000000001"_s},
                                {u"더미 목록 둘"_s, u"PLdummyplaylistid0000000000000000002"_s},
                                {u"Dummy three"_s, u"PLdummyplaylistid0000000000000000003"_s}};

/// The bytes of a JSON text written with ' for ".
QByteArray json(const char* text) {
    QByteArray bytes(text);
    bytes.replace('\'', '"');
    return bytes;
}

struct Rig {
    QTemporaryDir dir;
    QString file;

    Rig() : file(dir.filePath(u"playlists.json"_s)) {}

    PlaylistStore store() const { return PlaylistStore(file); }

    void write(const QByteArray& bytes, const QString& path = QString()) const {
        QFile out(path.isEmpty() ? file : path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(bytes);
    }

    QByteArray read(const QString& path = QString()) const {
        QFile in(path.isEmpty() ? file : path);
        if (!in.open(QIODevice::ReadOnly)) return QByteArray("<missing>");
        return in.readAll();
    }

    QStringList files() const {
        QStringList names = QDir(dir.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        names.sort();
        return names;
    }
};

}  // namespace

class PlaylistStoreTest : public QObject {
    Q_OBJECT

private slots:
    void aMissingFileIsAnEmptyList() {
        Rig rig;
        QVERIFY(rig.store().load().isEmpty());
        QVERIFY(rig.files().isEmpty());   // loading writes nothing
    }

    void whatWasSavedIsLoadedInTheSameOrder() {
        Rig rig;
        QVERIFY(rig.store().save(sample));
        QCOMPARE(rig.store().load(), sample);
        QCOMPARE(rig.files(), QStringList{u"playlists.json"_s});   // no temporary file stays
    }

    void theFileHasTheVersionAndTheNameAndPidOfEveryEntry() {
        Rig rig;
        QVERIFY(rig.store().save({sample.first()}));
        const QByteArray bytes = rig.read();
        QVERIFY2(bytes.contains("\"version\": 1"), bytes.constData());
        QVERIFY2(bytes.contains("\"playlists\""), bytes.constData());
        QVERIFY2(bytes.contains(u"더미 목록 하나"_s.toUtf8()), "UTF-8, not escaped");
        QVERIFY2(bytes.contains("\"pid\": \"PLdummyplaylistid0000000000000000001\""), bytes.constData());
        QVERIFY2(!bytes.startsWith("\xEF\xBB\xBF"), "no byte order mark");
    }

    void aSaveCreatesTheFoldersAboveTheFile() {
        Rig rig;
        const QString nested = rig.dir.filePath(u"a/b/playlists.json"_s);
        QVERIFY(PlaylistStore(nested).save(sample));
        QCOMPARE(PlaylistStore(nested).load(), sample);
    }

    void anEmptyListIsSavedAndLoadedAsEmpty() {
        Rig rig;
        QVERIFY(rig.store().save(sample));
        QVERIFY(rig.store().save({}));
        QVERIFY(rig.store().load().isEmpty());
    }

    void aFileWithAByteOrderMarkIsRead() {
        Rig rig;
        rig.write(QByteArray("\xEF\xBB\xBF") + json("{'version':1,'playlists':[{'name':'a','pid':'PLx'}]}"));
        QCOMPARE(rig.store().load(), (QList<Playlist>{{u"a"_s, u"PLx"_s}}));
    }

    // ---- Review Focus 3: the user's list is not lost ----

    void aBrokenFileIsKeptAsBad() {
        const QList<QByteArray> broken = {
            QByteArray("not json at all"),
            QByteArray(""),
            json("[1,2,3]"),
            json("{'version':1}"),
            json("{'version':1,'playlists':'x'}"),
            json("{'version':2,'playlists':[]}"),
            QByteArray("\xFF\xFE\x00 garbage"),
        };
        for (qsizetype i = 0; i < broken.size(); ++i) {
            Rig rig;
            rig.write(broken[i]);
            PlaylistStore store = rig.store();

            QVERIFY2(store.load().isEmpty(), qPrintable(u"case %1"_s.arg(i)));
            QCOMPARE(rig.read(), broken[i]);                 // loading does not touch it
            QVERIFY2(store.save(sample), qPrintable(u"case %1"_s.arg(i)));

            QCOMPARE(rig.read(rig.file + u".bad"_s), broken[i]);   // the broken file, byte for byte
            QCOMPARE(store.load(), sample);
            QCOMPARE(rig.files(), (QStringList{u"playlists.json"_s, u"playlists.json.bad"_s}));
        }
    }

    void aFileWithOneBadEntryKeepsTheGoodOnesAndIsKeptAsBad() {
        Rig rig;
        const QByteArray text = json(
            "{'version':1,'playlists':[{'name':'a','pid':'PLa'},{'name':'','pid':'PLb'},{'name':'c'},5,{'name':'d','pid':'PLd'}]}");
        rig.write(text);
        QCOMPARE(rig.store().load(), (QList<Playlist>{{u"a"_s, u"PLa"_s}, {u"d"_s, u"PLd"_s}}));

        QVERIFY(rig.store().save({{u"a"_s, u"PLa"_s}}));
        QCOMPARE(rig.read(rig.file + u".bad"_s), text);
    }

    void aGoodFileIsNotSetAside() {
        Rig rig;
        QVERIFY(rig.store().save(sample));
        QVERIFY(rig.store().save({sample.first()}));
        QVERIFY(rig.store().save(sample));
        QCOMPARE(rig.files(), QStringList{u"playlists.json"_s});
    }

    void anOlderBadFileIsReplacedByTheNewerOne() {
        Rig rig;
        rig.write(QByteArray("first broken"));
        QVERIFY(rig.store().save(sample));
        rig.write(QByteArray("second broken"));
        QVERIFY(rig.store().save(sample));
        QCOMPARE(rig.read(rig.file + u".bad"_s), QByteArray("second broken"));
    }

    void aFailedWriteLeavesTheOldFile() {
        Rig rig;
        QVERIFY(rig.store().save(sample));
        const QByteArray before = rig.read();
        // A folder where the temporary file goes: the write cannot succeed.
        QVERIFY(QDir().mkdir(rig.file + u".tmp"_s));

        QVERIFY(!rig.store().save({sample.first()}));

        QCOMPARE(rig.read(), before);
        QCOMPARE(rig.store().load(), sample);
    }

    void aFailedWriteAfterABrokenFileStillHasTheBrokenFileUnderItsOwnName() {
        Rig rig;
        rig.write(QByteArray("broken"));
        QVERIFY(QDir().mkdir(rig.file + u".tmp"_s));

        QVERIFY(!rig.store().save(sample));

        // It was set aside before the write, and that is where the user finds it.
        QCOMPARE(rig.read(rig.file + u".bad"_s), QByteArray("broken"));
    }

    void aFolderInPlaceOfTheFileIsNotOverwritten() {
        Rig rig;
        QVERIFY(QDir().mkdir(rig.file));
        PlaylistStore store = rig.store();
        QVERIFY(store.load().isEmpty());
        // Whatever happens, the folder and what is in it are not destroyed.
        store.save(sample);
        QVERIFY(QFileInfo(rig.file).isDir() || QFileInfo(rig.file + u".bad"_s).isDir());
    }

    void aPathThatIsNotAPathIsNotSaved() {
        QVERIFY(!PlaylistStore(QString()).save(sample));
        QVERIFY(!PlaylistStore(u"C:\\"_s).save(sample));
        QVERIFY(!PlaylistStore(u"a\0b"_s).save(sample));
        QVERIFY(PlaylistStore(QString()).load().isEmpty());
    }

    void savesFromManyThreadsEndWithOneOfThemWhole() {
        Rig rig;
        std::vector<std::jthread> threads;
        for (int i = 0; i < 6; ++i) {
            threads.emplace_back([&rig, i] {
                const QList<Playlist> mine = {{u"더미 %1"_s.arg(i), u"PLthread%1"_s.arg(i)}};
                for (int n = 0; n < 5; ++n) PlaylistStore(rig.file).save(mine);
            });
        }
        threads.clear();
        const QList<Playlist> result = rig.store().load();
        QCOMPARE(result.size(), 1);
        QCOMPARE(result.first().pid, u"PLthread%1"_s.arg(result.first().name.last(1)));
        QCOMPARE(rig.files(), QStringList{u"playlists.json"_s});
    }
};

QTEST_APPLESS_MAIN(PlaylistStoreTest)
#include "tst_playlist_store.moc"
