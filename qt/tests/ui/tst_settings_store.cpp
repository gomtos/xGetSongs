#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTemporaryDir>

#include <thread>
#include <vector>

#include "fake_app_api.h"
#include "settings_store.h"

// The settings part of shell/tests/desktop.test.ts (normalizeSettings and the store the screen is given), under the same
// names, and the tests of the settings file the Tauri shell kept (sidecar-host/src/settings.rs, the cases of the Compose app's
// JsonSettingsStoreTest.kt), under their names in camel case. The Rust shell read and wrote the file on the screen's behalf;
// here SettingsStore does both, synchronously, so the TypeScript tests about the order of asynchronous writes are about the
// order of synchronous saves. Every file lives in a QTemporaryDir.

using namespace Qt::StringLiterals;
using xgs::ui::normalizeSettings;
using xgs::ui::SettingsStore;
using xgs::ui::UserSettings;

namespace {

UserSettings settings(std::optional<QString> outputDir, bool overwrite, bool includeRank, bool searchLyricsOnline) {
    return UserSettings{std::move(outputDir), overwrite, includeRank, searchLyricsOnline};
}

const UserSettings korean = settings(u"D:\\음악\\내 재생목록"_s, true, false, false);

/// A settings file in a fresh temporary folder, with a store on it.
struct Rig {
    QTemporaryDir dir;
    QString file;

    Rig() : file(dir.filePath(u"settings.json"_s)) {}

    SettingsStore store() const { return SettingsStore(file); }

    void write(const QByteArray& bytes) const {
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(bytes);
    }

    QString text() const {
        QFile in(file);
        if (!in.open(QIODevice::ReadOnly)) return QString();
        return QString::fromUtf8(in.readAll());
    }

    static QStringList filesIn(const QString& directory) {
        QStringList names = QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        names.sort();
        return names;
    }
};

/// The bytes of a JSON text written with ' for " (the tests' JSON is written this way to keep raw strings out of a file
/// moc reads).
QByteArray json(const char* text) {
    QByteArray bytes(text);
    bytes.replace('\'', '"');
    return bytes;
}

QJsonObject object(std::initializer_list<std::pair<QString, QJsonValue>> members) {
    QJsonObject result;
    for (const auto& [key, value] : members) result.insert(key, value);
    return result;
}

QJsonObject toJson(const UserSettings& value) {
    return object({{u"outputDir"_s, value.outputDir ? QJsonValue(*value.outputDir) : QJsonValue(QJsonValue::Null)},
                   {u"overwrite"_s, value.overwrite},
                   {u"includeRank"_s, value.includeRank},
                   {u"searchLyricsOnline"_s, value.searchLyricsOnline}});
}

}  // namespace

class SettingsStoreTest : public QObject {
    Q_OBJECT

private slots:
    // ---- the settings the Rust side handed over (desktop.test.ts) ----

    void settingsInTheShapeOfTheFileAreTakenAsTheyAre() {
        QCOMPARE(normalizeSettings(toJson(korean)), korean);
        QCOMPARE(normalizeSettings(object({{u"outputDir"_s, QJsonValue::Null},
                                           {u"overwrite"_s, false},
                                           {u"includeRank"_s, true},
                                           {u"searchLyricsOnline"_s, true}})),
                 UserSettings());
    }

    void settingsThatAreNotWhatWasExpectedAreTheDefaults() {
        const QList<QJsonValue> raws = {
            QJsonValue(QJsonValue::Undefined),
            QJsonValue(QJsonValue::Null),
            QJsonValue(u"x"_s),
            QJsonValue(5),
            QJsonArray(),
            QJsonObject(),
            object({{u"outputDir"_s, 5}}),
            object({{u"outputDir"_s, QJsonValue::Null}, {u"overwrite"_s, u"yes"_s}, {u"includeRank"_s, true}, {u"searchLyricsOnline"_s, true}}),
        };
        for (qsizetype i = 0; i < raws.size(); ++i) {
            QVERIFY2(normalizeSettings(raws[i]) == UserSettings(), qPrintable(u"case %1"_s.arg(i)));
        }
    }

    // ---- the store the screen is given (desktop.test.ts) ----

    void theStoreHandsOutWhatItWasStartedWith() {
        Rig rig;
        QVERIFY(rig.store().save(korean));
        SettingsStore store = rig.store();

        QCOMPARE(store.load(), korean);
        QCOMPARE(store.load(), korean);
    }

    void everySaveIsWrittenInTheOrderItWasMadeEvenWhenTheFirstWriteIsSlow() {
        // A save returns once the file is written, so a later save can never be overtaken by an earlier one.
        Rig rig;
        SettingsStore store = rig.store();

        QVERIFY(store.save(settings(u"first"_s, false, true, true)));
        QVERIFY(store.save(settings(u"second"_s, false, true, true)));
        QVERIFY(store.save(settings(u"third"_s, false, true, true)));

        QCOMPARE(store.load().outputDir, std::optional<QString>(u"third"_s));
        QCOMPARE(Rig::filesIn(rig.dir.path()), QStringList{u"settings.json"_s});
    }

    void aWriteThatFailsDoesNotStopTheNextOnesAndNeverThrows() {
        Rig rig;
        QVERIFY(QDir().mkdir(rig.file));  // a folder in place of the file: the write fails
        SettingsStore store = rig.store();

        QCOMPARE(store.save(settings(u"bad"_s, false, true, true)), false);
        QVERIFY(QDir().rmdir(rig.file));
        QCOMPARE(store.save(settings(u"good"_s, false, true, true)), true);

        QCOMPARE(store.load().outputDir, std::optional<QString>(u"good"_s));
    }

    void aWriteThatThrowsAtOnceIsTreatedLikeOneThatFails() {
        // The write that fails before anything is written: the folder of the file cannot even be made.
        Rig rig;
        const QString blocker = rig.dir.filePath(u"not-a-folder"_s);
        QFile out(blocker);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.close();
        SettingsStore broken(blocker + u"/settings.json"_s);
        SettingsStore good = rig.store();

        QCOMPARE(broken.save(settings(u"bad"_s, false, true, true)), false);
        QCOMPARE(good.save(settings(u"good"_s, false, true, true)), true);

        QCOMPARE(good.load().outputDir, std::optional<QString>(u"good"_s));
        QCOMPARE(broken.load(), UserSettings());
    }

    void whenSavedHasNothingToWaitForWithoutASave() {
        // Nothing is written until something is saved: loading a store whose file is missing creates nothing.
        Rig rig;

        QCOMPARE(rig.store().load(), UserSettings());

        QCOMPARE(Rig::filesIn(rig.dir.path()), QStringList());
    }

    void theDesktopLoadsTheSettingsFileBeforeItIsHandedOver() {
        Rig rig;
        rig.write(u"{\"outputDir\":\"D:\\\\음악\\\\내 재생목록\",\"overwrite\":true,\"includeRank\":false,\"searchLyricsOnline\":false}"_s.toUtf8());

        QCOMPARE(rig.store().load(), korean);
    }

    void aSettingsFileThatCannotBeLoadedMeansTheDefaultsAndNoFailure() {
        Rig rig;
        rig.write("{ not json");

        QCOMPARE(rig.store().load(), UserSettings());
    }

    void savedSettingsGoToTheRustSideUnderTheNameOfTheCommand() {
        // The file the Rust side wrote for save_settings is the one this store writes: the four keys of the settings.
        Rig rig;

        QVERIFY(rig.store().save(korean));

        const QString text = rig.text();
        QVERIFY2(text.contains(u"\"outputDir\": \"D:\\\\음악\\\\내 재생목록\""_s), qPrintable(text));
        QVERIFY2(text.contains(u"\"overwrite\": true"_s), qPrintable(text));
        QVERIFY2(text.contains(u"\"includeRank\": false"_s), qPrintable(text));
        QVERIFY2(text.contains(u"\"searchLyricsOnline\": false"_s), qPrintable(text));
    }

    // ---- the settings file (sidecar-host/src/settings.rs): load ----

    void aMissingFileGivesTheDefaults() {
        Rig rig;

        QCOMPARE(rig.store().load(), UserSettings());
    }

    void theDefaultsAreWhatTheSpecSays() {
        QCOMPARE(UserSettings(), settings(std::nullopt, false, true, true));
        QVERIFY2(UserSettings().searchLyricsOnline, "the lyrics search is on until the user turns it off");
    }

    void savedSettingsComeBackUnchangedIncludingAKoreanPath() {
        Rig rig;
        const UserSettings saved = settings(u"D:\\음악\\내 재생목록"_s, true, false, false);

        rig.store().save(saved);

        QCOMPARE(rig.store().load(), saved);
        QVERIFY2(SettingsStore(rig.file).load() == saved, "a second store reads the same file");
        QVERIFY2(rig.text().contains(u"내 재생목록"_s), "the file is UTF-8 text");
    }

    void defaultValuesAreWrittenOutToo() {
        Rig rig;

        rig.store().save(UserSettings());

        const QString text = rig.text();
        for (const QString& key : {u"outputDir"_s, u"overwrite"_s, u"includeRank"_s, u"searchLyricsOnline"_s}) {
            QVERIFY2(text.contains(u'"' + key + u'"'), qPrintable(key + u" in "_s + text));
        }
        QVERIFY2(!text.contains(u"concurrency"_s), qPrintable(u"the engine decides the concurrency, so it is not stored: "_s + text));
        QVERIFY2(text.contains(u"\"searchLyricsOnline\": true"_s), qPrintable(text));
    }

    void anOldFileWithoutTheLyricsKeyLoadsWithTheSearchOn() {
        Rig rig;
        rig.write(json("{'outputDir':'D:/x','overwrite':true,'includeRank':false,'concurrency':3}"));

        QCOMPARE(rig.store().load(), settings(u"D:/x"_s, true, false, true));
        QVERIFY(rig.store().load().searchLyricsOnline);
    }

    void aStoredSearchLyricsOnlineValueIsRead() {
        Rig rig;
        rig.write(json("{'searchLyricsOnline':false}"));
        QVERIFY(!rig.store().load().searchLyricsOnline);

        rig.write(json("{'overwrite':true,'searchLyricsOnline':true,'concurrency':4}"));
        QCOMPARE(rig.store().load(), settings(std::nullopt, true, true, true));
    }

    void aTurnedOffLyricsSearchIsWrittenAndComesBack() {
        Rig rig;

        rig.store().save(settings(std::nullopt, false, true, false));

        QVERIFY(rig.text().contains(u"\"searchLyricsOnline\": false"_s));
        QVERIFY(!SettingsStore(rig.file).load().searchLyricsOnline);
    }

    void filesThatCannotBeUsedGiveTheDefaultsAndNeverPanic() {
        Rig rig;
        const QList<std::pair<const char*, QByteArray>> cases = {
            {"corrupt JSON", "{"},
            {"empty file", ""},
            {"blank file", "  \n "},
            {"wrong shape", "[]"},
            {"a bare value", "42"},
            {"the null literal", "null"},
            {"text that is not JSON", "hello"},
            {"a field of the wrong type", json("{'overwrite':'yes'}")},
            {"a lyrics option of the wrong type", json("{'overwrite':true,'searchLyricsOnline':'no'}")},
            {"a number where the folder should be", json("{'outputDir':5}")},
        };
        for (const auto& [name, content] : cases) {
            rig.write(content);

            QVERIFY2(rig.store().load() == UserSettings(), name);
        }
    }

    void validJsonWithBytesThatAreNotUtf8GivesTheDefaultsInsteadOfAGarbledFolder() {
        // The syntax is fine and only the encoding is wrong: what an ANSI/CP949 editor writes for a Korean folder name (0xC0 0xBD
        // 0xBE 0xC7) is not UTF-8, and must not turn into U+FFFD characters in the output folder.
        Rig rig;
        QByteArray body = json("{'outputDir':'D:\\\\");
        body.append("\xC0\xBD\xBE\xC7");
        body.append(json("','overwrite':true,'concurrency':4}"));
        rig.write(body);

        QCOMPARE(rig.store().load(), UserSettings());
    }

    void aUtf16FileGivesTheDefaults() {
        // What Windows PowerShell's Out-File writes by default: a byte order mark, then two bytes per character.
        Rig rig;
        QByteArray body("\xFF\xFE", 2);
        for (QChar unit : u"{\"overwrite\":true}"_s) {
            body.append(char(unit.unicode() & 0xFF));
            body.append(char(unit.unicode() >> 8));
        }
        rig.write(body);

        QCOMPARE(rig.store().load(), UserSettings());
    }

    void aUtf8ByteOrderMarkInFrontOfValidJsonIsIgnored() {
        Rig rig;
        QByteArray body("\xEF\xBB\xBF");
        body.append(u"{\"outputDir\":\"D:\\\\음악\",\"overwrite\":true,\"includeRank\":false,\"concurrency\":3}"_s.toUtf8());
        rig.write(body);

        QCOMPARE(rig.store().load(), settings(u"D:\\음악"_s, true, false, true));
    }

    void aFolderInPlaceOfTheFileGivesTheDefaults() {
        Rig rig;
        QVERIFY(QDir().mkdir(rig.file));

        QCOMPARE(rig.store().load(), UserSettings());
    }

    void unknownKeysAreIgnoredAndTheKnownOnesStillRead() {
        Rig rig;
        rig.write(json("{'outputDir':'D:/x','future':{'a':[1,2]},'overwrite':true,'theme':'dark','includeRank':false,'concurrency':3,'searchLyricsOnline':false}"));

        QCOMPARE(rig.store().load(), settings(u"D:/x"_s, true, false, false));
    }

    void missingKeysTakeTheirDefaults() {
        Rig rig;
        rig.write(json("{'overwrite':true}"));

        QCOMPARE(rig.store().load(), settings(std::nullopt, true, true, true));
    }

    void aConcurrencyFromAnOlderFileIsIgnoredAndLeftOutOfTheNextSave() {
        Rig rig;
        for (const char* stored : {"3", "99999999999", "\"many\"", "null"}) {
            rig.write(QByteArray(json("{'overwrite':true,'concurrency':")) + stored + "}");

            QVERIFY2(rig.store().load() == settings(std::nullopt, true, true, true), stored);
        }

        rig.store().save(rig.store().load());

        // Rust left the key out of the next save; the design spec §3 keeps keys the app does not know (a deliberate difference,
        // see savingKeepsKeysTheAppDoesNotKnow). The value is not read either way.
        QVERIFY(rig.text().contains(u"\"concurrency\""_s));
        QCOMPARE(rig.store().load(), settings(std::nullopt, true, true, true));
    }

    void aBlankStoredFolderIsKeptAsItIsForTheCallerToReplace() {
        Rig rig;
        rig.write(json("{'outputDir':'   '}"));
        QCOMPARE(rig.store().load().outputDir, std::optional<QString>(u"   "_s));

        rig.write(json("{'outputDir':''}"));
        QCOMPARE(rig.store().load().outputDir, std::optional<QString>(u""_s));

        rig.write(json("{'outputDir':null}"));
        QCOMPARE(rig.store().load().outputDir, std::optional<QString>());
    }

    // ---- the settings file (sidecar-host/src/settings.rs): save ----

    void saveCreatesTheParentDirectories() {
        Rig rig;
        SettingsStore nested(rig.dir.filePath(u"a/b/settings.json"_s));
        const UserSettings saved = settings(u"D:/Songs"_s, true, true, true);

        nested.save(saved);

        QCOMPARE(nested.load(), saved);
        QCOMPARE(Rig::filesIn(rig.dir.filePath(u"a/b"_s)), QStringList{u"settings.json"_s});
    }

    void saveLeavesNoTemporaryFileBehind() {
        Rig rig;

        rig.store().save(settings(u"D:/Songs"_s, false, true, true));
        rig.store().save(settings(u"D:/Other"_s, false, true, true));

        QCOMPARE(Rig::filesIn(rig.dir.path()), QStringList{u"settings.json"_s});
    }

    void saveReplacesAnExistingFile() {
        Rig rig;
        rig.write(json("{'outputDir':'D:/old','overwrite':true,'includeRank':false,'concurrency':4,'searchLyricsOnline':false,'future':1}"));
        const UserSettings saved = settings(u"D:/new"_s, false, true, true);

        rig.store().save(saved);

        QCOMPARE(rig.store().load(), saved);
        QCOMPARE(Rig::filesIn(rig.dir.path()), QStringList{u"settings.json"_s});
        // A deliberate difference from sidecar-host/src/settings.rs (whose test asserted that `future` is gone): the design spec
        // §3 says keys the app does not know are not deleted.
        QVERIFY2(rig.text().contains(u"\"future\": 1"_s), qPrintable(rig.text()));
    }

    // ---- not in the reference: what the design spec adds to saving ----

    void savingKeepsKeysTheAppDoesNotKnow() {
        Rig rig;
        rig.write(json("{'outputDir':'D:/old','theme':'dark','future':{'a':[1,2]},'concurrency':3}"));

        QVERIFY(rig.store().save(settings(u"D:/new"_s, true, false, true)));

        QCOMPARE(rig.store().load(), settings(u"D:/new"_s, true, false, true));
        const QString text = rig.text();
        QVERIFY2(text.contains(u"\"theme\": \"dark\""_s), qPrintable(text));
        QVERIFY2(text.contains(u"\"future\""_s) && text.contains(u"\"a\""_s), qPrintable(text));
        QVERIFY2(text.contains(u"\"concurrency\": 3"_s), qPrintable(text));
        QVERIFY2(!text.contains(u"D:/old"_s), qPrintable(text));

        // A broken file is replaced by the four keys.
        rig.write("{ broken");
        QVERIFY(rig.store().save(settings(u"D:/x"_s, false, true, true)));
        QCOMPARE(rig.store().load(), settings(u"D:/x"_s, false, true, true));
        QVERIFY2(!rig.text().contains(u"broken"_s), qPrintable(rig.text()));
        QCOMPARE(Rig::filesIn(rig.dir.path()), QStringList{u"settings.json"_s});
    }

    void saveIntoALocationThatCannotBeWrittenDoesNotPanic() {
        Rig rig;
        const QString blocker = rig.dir.filePath(u"not-a-folder"_s);
        QFile out(blocker);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("\x01", 1);
        out.close();
        SettingsStore broken(blocker + u"/settings.json"_s);

        broken.save(settings(u"D:/Songs"_s, false, true, true));

        QCOMPARE(broken.load(), UserSettings());
        QVERIFY2(Rig::filesIn(rig.dir.path()) == QStringList{u"not-a-folder"_s}, "nothing else was created");
    }

    void anUnexpectedRuntimeFailureNeverEscapesFromLoadOrSave() {
        // The JVM test used a path whose every operation threw; here the path is one the platform refuses at every step.
        SettingsStore broken(u"bad"_s + QChar(0) + u"name\\settings.json"_s);

        QCOMPARE(broken.save(settings(u"D:/Songs"_s, true, true, true)), false);

        QCOMPARE(broken.load(), UserSettings());
        SettingsStore root(u"C:\\"_s);
        QCOMPARE(root.save(UserSettings()), false);  // a file system root has no name to build a file's from
        QCOMPARE(root.load(), UserSettings());
    }

    void saveOverADirectoryThatIsNotEmptyDoesNotPanicAndLeavesNoTemporaryFile() {
        Rig rig;
        QVERIFY(QDir().mkdir(rig.file));
        QFile keep(rig.file + u"/keep.txt"_s);
        QVERIFY(keep.open(QIODevice::WriteOnly));
        keep.write("\x01", 1);
        keep.close();

        rig.store().save(settings(u"D:/Songs"_s, false, true, true));

        QCOMPARE(Rig::filesIn(rig.dir.path()), QStringList{u"settings.json"_s});
        QCOMPARE(Rig::filesIn(rig.file), QStringList{u"keep.txt"_s});
    }

    void savesFromSeveralThreadsLeaveOneValidFileAndNoTemporaryFile() {
        Rig rig;
        {
            std::vector<std::jthread> threads;
            for (int index = 0; index < 8; ++index) {
                threads.emplace_back([&rig, index] {
                    SettingsStore(rig.file).save(settings(u"D:/Songs"_s + QString::number(index), index % 2 == 0, true, true));
                });
            }
        }

        const UserSettings loaded = rig.store().load();
        QVERIFY2(loaded.outputDir && loaded.outputDir->startsWith(u"D:/Songs"_s), qPrintable(loaded.outputDir.value_or(u"<null>"_s)));
        QCOMPARE(Rig::filesIn(rig.dir.path()), QStringList{u"settings.json"_s});
    }

    void quotedBooleansAreReadLikeKotlinxDoes() {
        // kotlinx.serialization read `"overwrite":"true"` as true even without the lenient mode (a hand-edited file); a file like
        // that must not fall back to the defaults, whose first change would overwrite the stored folder.
        Rig rig;
        rig.write(json("{'outputDir':'D:/x','overwrite':'true','includeRank':'false','searchLyricsOnline':'false'}"));

        QCOMPARE(rig.store().load(), settings(u"D:/x"_s, true, false, false));
    }

    // ---- not in the reference: the default folder ----

    void theDefaultOutputFolderIsMusicXGetSongsInTheHomeFolder() {
        QCOMPARE(SettingsStore::standardOutputDir(), QDir::toNativeSeparators(QDir::homePath()) + u"\\Music\\xGetSongs"_s);
        QCOMPARE(SettingsStore(u"x.json"_s).defaultOutputDir(), SettingsStore::standardOutputDir());
        QCOMPARE(SettingsStore(u"x.json"_s, u"D:\\Elsewhere"_s).defaultOutputDir(), u"D:\\Elsewhere"_s);
    }
};

QTEST_GUILESS_MAIN(SettingsStoreTest)
#include "tst_settings_store.moc"
