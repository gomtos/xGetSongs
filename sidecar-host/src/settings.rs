//! The options the app restores on the next start, kept in `settings.json` in the app data folder. Ported from
//! `app/src/commonMain/kotlin/com/xgetsongs/app/settings/UserSettings.kt` and `app/src/desktopMain/kotlin/com/xgetsongs/app/JsonSettingsStore.kt`:
//! the file is the one the Compose app wrote and reads, and the tests of this file are the cases of `JsonSettingsStoreTest.kt`, one for
//! one and under the same name in snake case.
//!
//! This is the only writer of the file: the sidecar never touches it.

use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

/// The options the app restores on the next start. The typed playlist or video address and the rank of a single video are not part
/// of it. `output_dir` is None (or blank) when the user never chose a folder: the app then uses its default. The lyrics search is on
/// for a settings file written before the option existed. A `concurrency` in an older settings file is ignored: the server decides how
/// many items are downloaded at once.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UserSettings {
    pub output_dir: Option<String>,
    pub overwrite: bool,
    pub include_rank: bool,
    pub search_lyrics_online: bool,
}

impl Default for UserSettings {
    fn default() -> UserSettings {
        UserSettings { output_dir: None, overwrite: false, include_rank: true, search_lyrics_online: true }
    }
}

/// Two saves at once (the options change while the window closes) must not write the same temporary file together.
static WRITE_LOCK: Mutex<()> = Mutex::new(());

/// Keeps the options in a small UTF-8 JSON file. Neither function panics or fails, whatever goes wrong: a file that is missing,
/// unreadable, not valid UTF-8 or not what was expected means the defaults, and a profile that cannot be written to means the options
/// are not kept (the app works the same without remembering them).
#[derive(Clone)]
pub struct SettingsFile {
    path: PathBuf,
}

impl SettingsFile {
    pub fn new(path: impl Into<PathBuf>) -> SettingsFile {
        SettingsFile { path: path.into() }
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn load(&self) -> UserSettings {
        read(&self.path).unwrap_or_default()
    }

    /// Writes next to the target and moves it over, so a crash never leaves half a file behind.
    pub fn save(&self, settings: &UserSettings) {
        let _guard = WRITE_LOCK.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        let mut temp: Option<PathBuf> = None;
        let _ = write(&self.path, settings, &mut temp);
        if let Some(temp) = temp {
            let _ = std::fs::remove_file(temp); // a leftover temp file is overwritten by the next save
        }
    }
}

fn read(path: &Path) -> Option<UserSettings> {
    let bytes = std::fs::read(path).ok()?;
    // Strict: bytes that are not UTF-8 (a file saved in the ANSI code page) must give the defaults, not a folder name full of U+FFFD.
    // A byte order mark, which Notepad and PowerShell like to add, is not part of the JSON.
    let text = String::from_utf8(bytes).ok()?;
    let value: Value = serde_json::from_str(text.strip_prefix('\u{feff}').unwrap_or(&text)).ok()?;
    let object = value.as_object()?;
    let defaults = UserSettings::default();
    // A key that is missing takes its default; a key of the wrong type makes the whole file unusable. Unknown keys are ignored. A boolean
    // may be quoted (`"true"`), as kotlinx.serialization reads it.
    let flag = |key: &str, default: bool| match object.get(key) {
        None => Some(default),
        Some(Value::Bool(value)) => Some(*value),
        Some(Value::String(text)) if text == "true" => Some(true),
        Some(Value::String(text)) if text == "false" => Some(false),
        Some(_) => None,
    };
    let output_dir = match object.get("outputDir") {
        None | Some(Value::Null) => None,
        Some(Value::String(text)) => Some(text.clone()),
        Some(_) => return None,
    };
    Some(UserSettings {
        output_dir,
        overwrite: flag("overwrite", defaults.overwrite)?,
        include_rank: flag("includeRank", defaults.include_rank)?,
        search_lyrics_online: flag("searchLyricsOnline", defaults.search_lyrics_online)?,
    })
}

fn write(path: &Path, settings: &UserSettings, temp: &mut Option<PathBuf>) -> std::io::Result<()> {
    let name = path
        .file_name()
        .ok_or_else(|| std::io::Error::new(std::io::ErrorKind::InvalidInput, "a file system root has no name"))?;
    let mut temp_name = name.to_os_string();
    temp_name.push(".tmp");
    let temp_path = path.with_file_name(temp_name);
    if let Some(parent) = std::path::absolute(path)?.parent() {
        std::fs::create_dir_all(parent)?;
    }
    *temp = Some(temp_path.clone());
    std::fs::write(&temp_path, serde_json::to_vec_pretty(settings)?)?;
    std::fs::rename(&temp_path, path)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    /// A fresh folder in the temp folder that is deleted again when the test is over, with the settings file of the test in it.
    struct Rig {
        dir: PathBuf,
        file: PathBuf,
        store: SettingsFile,
    }

    impl Rig {
        fn new(name: &str) -> Rig {
            let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
            let dir = std::env::temp_dir().join(format!("xgs-settings-test-{}-{name}-{nanos}", std::process::id()));
            fs::create_dir_all(&dir).unwrap();
            let file = dir.join("settings.json");
            Rig { store: SettingsFile::new(&file), dir, file }
        }

        fn write(&self, text: &str) {
            fs::write(&self.file, text.as_bytes()).unwrap();
        }

        fn files_in(&self, directory: &Path) -> Vec<String> {
            let mut names: Vec<String> =
                fs::read_dir(directory).unwrap().map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned()).collect();
            names.sort();
            names
        }

        fn text(&self) -> String {
            fs::read_to_string(&self.file).unwrap()
        }
    }

    impl Drop for Rig {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.dir);
        }
    }

    fn settings(output_dir: Option<&str>, overwrite: bool, include_rank: bool, search_lyrics_online: bool) -> UserSettings {
        UserSettings { output_dir: output_dir.map(str::to_string), overwrite, include_rank, search_lyrics_online }
    }

    // ---- load ----

    #[test]
    fn a_missing_file_gives_the_defaults() {
        let rig = Rig::new("missing");

        assert_eq!(rig.store.load(), UserSettings::default());
    }

    #[test]
    fn the_defaults_are_what_the_spec_says() {
        assert_eq!(UserSettings::default(), settings(None, false, true, true));
        assert!(UserSettings::default().search_lyrics_online, "the lyrics search is on until the user turns it off");
    }

    #[test]
    fn saved_settings_come_back_unchanged_including_a_korean_path() {
        let rig = Rig::new("roundtrip");
        let saved = settings(Some("D:\\음악\\내 재생목록"), true, false, false);

        rig.store.save(&saved);

        assert_eq!(rig.store.load(), saved);
        assert_eq!(SettingsFile::new(&rig.file).load(), saved, "a second store reads the same file");
        assert!(rig.text().contains("내 재생목록"), "the file is UTF-8 text");
    }

    #[test]
    fn default_values_are_written_out_too() {
        let rig = Rig::new("defaults-written");

        rig.store.save(&UserSettings::default());

        let text = rig.text();
        for key in ["outputDir", "overwrite", "includeRank", "searchLyricsOnline"] {
            assert!(text.contains(&format!("\"{key}\"")), "{key} in {text}");
        }
        assert!(!text.contains("concurrency"), "the server decides the concurrency, so it is not stored: {text}");
        assert!(text.contains("\"searchLyricsOnline\": true"), "{text}");
    }

    #[test]
    fn an_old_file_without_the_lyrics_key_loads_with_the_search_on() {
        let rig = Rig::new("old");
        rig.write(r#"{"outputDir":"D:/x","overwrite":true,"includeRank":false,"concurrency":3}"#);

        assert_eq!(rig.store.load(), settings(Some("D:/x"), true, false, true));
        assert!(rig.store.load().search_lyrics_online);
    }

    #[test]
    fn a_stored_search_lyrics_online_value_is_read() {
        let rig = Rig::new("lyrics-read");
        rig.write(r#"{"searchLyricsOnline":false}"#);
        assert!(!rig.store.load().search_lyrics_online);

        rig.write(r#"{"overwrite":true,"searchLyricsOnline":true,"concurrency":4}"#);
        assert_eq!(rig.store.load(), UserSettings { overwrite: true, ..UserSettings::default() });
    }

    #[test]
    fn a_turned_off_lyrics_search_is_written_and_comes_back() {
        let rig = Rig::new("lyrics-off");

        rig.store.save(&UserSettings { search_lyrics_online: false, ..UserSettings::default() });

        assert!(rig.text().contains("\"searchLyricsOnline\": false"));
        assert!(!SettingsFile::new(&rig.file).load().search_lyrics_online);
    }

    #[test]
    fn files_that_cannot_be_used_give_the_defaults_and_never_panic() {
        let rig = Rig::new("unusable");
        let cases = [
            ("corrupt JSON", "{"),
            ("empty file", ""),
            ("blank file", "  \n "),
            ("wrong shape", "[]"),
            ("a bare value", "42"),
            ("the null literal", "null"),
            ("text that is not JSON", "hello"),
            ("a field of the wrong type", r#"{"overwrite":"yes"}"#),
            ("a lyrics option of the wrong type", r#"{"overwrite":true,"searchLyricsOnline":"no"}"#),
            ("a number where the folder should be", r#"{"outputDir":5}"#),
        ];
        for (name, content) in cases {
            rig.write(content);

            assert_eq!(rig.store.load(), UserSettings::default(), "{name}");
        }
    }

    #[test]
    fn valid_json_with_bytes_that_are_not_utf8_gives_the_defaults_instead_of_a_garbled_folder() {
        // The syntax is fine and only the encoding is wrong: what an ANSI/CP949 editor writes for a Korean folder name (0xC0 0xBD
        // 0xBE 0xC7) is not UTF-8, and must not turn into U+FFFD characters in the output folder.
        let rig = Rig::new("not-utf8");
        let mut body = br#"{"outputDir":"D:\\"#.to_vec();
        body.extend_from_slice(&[0xC0, 0xBD, 0xBE, 0xC7]);
        body.extend_from_slice(br#"","overwrite":true,"concurrency":4}"#);
        fs::write(&rig.file, body).unwrap();

        assert_eq!(rig.store.load(), UserSettings::default());
    }

    #[test]
    fn a_utf16_file_gives_the_defaults() {
        // What Windows PowerShell's Out-File writes by default: a byte order mark, then two bytes per character.
        let rig = Rig::new("utf16");
        let mut body = vec![0xFF, 0xFE];
        for unit in r#"{"overwrite":true}"#.encode_utf16() {
            body.extend_from_slice(&unit.to_le_bytes());
        }
        fs::write(&rig.file, body).unwrap();

        assert_eq!(rig.store.load(), UserSettings::default());
    }

    #[test]
    fn a_utf8_byte_order_mark_in_front_of_valid_json_is_ignored() {
        let rig = Rig::new("bom");
        let json = r#"{"outputDir":"D:\\음악","overwrite":true,"includeRank":false,"concurrency":3}"#;
        let mut body = vec![0xEF, 0xBB, 0xBF];
        body.extend_from_slice(json.as_bytes());
        fs::write(&rig.file, body).unwrap();

        assert_eq!(rig.store.load(), settings(Some("D:\\음악"), true, false, true));
    }

    #[test]
    fn a_folder_in_place_of_the_file_gives_the_defaults() {
        let rig = Rig::new("folder");
        fs::create_dir(&rig.file).unwrap();

        assert_eq!(rig.store.load(), UserSettings::default());
    }

    #[test]
    fn unknown_keys_are_ignored_and_the_known_ones_still_read() {
        let rig = Rig::new("unknown");
        rig.write(
            r#"{"outputDir":"D:/x","future":{"a":[1,2]},"overwrite":true,"theme":"dark","includeRank":false,"concurrency":3,"searchLyricsOnline":false}"#,
        );

        assert_eq!(rig.store.load(), settings(Some("D:/x"), true, false, false));
    }

    #[test]
    fn missing_keys_take_their_defaults() {
        let rig = Rig::new("missing-keys");
        rig.write(r#"{"overwrite":true}"#);

        assert_eq!(rig.store.load(), UserSettings { overwrite: true, ..UserSettings::default() });
    }

    #[test]
    fn a_concurrency_from_an_older_file_is_ignored_and_left_out_of_the_next_save() {
        let rig = Rig::new("concurrency");
        for stored in ["3", "99999999999", "\"many\"", "null"] {
            rig.write(&format!(r#"{{"overwrite":true,"concurrency":{stored}}}"#));

            assert_eq!(rig.store.load(), UserSettings { overwrite: true, ..UserSettings::default() }, "stored {stored}");
        }

        rig.store.save(&rig.store.load());

        assert!(!rig.text().contains("concurrency"));
    }

    #[test]
    fn a_blank_stored_folder_is_kept_as_it_is_for_the_caller_to_replace() {
        let rig = Rig::new("blank-folder");
        rig.write(r#"{"outputDir":"   "}"#);
        assert_eq!(rig.store.load().output_dir.as_deref(), Some("   "));

        rig.write(r#"{"outputDir":""}"#);
        assert_eq!(rig.store.load().output_dir.as_deref(), Some(""));

        rig.write(r#"{"outputDir":null}"#);
        assert_eq!(rig.store.load().output_dir, None);
    }

    // ---- save ----

    #[test]
    fn save_creates_the_parent_directories() {
        let rig = Rig::new("parents");
        let nested = SettingsFile::new(rig.dir.join("a").join("b").join("settings.json"));
        let saved = settings(Some("D:/Songs"), true, true, true);

        nested.save(&saved);

        assert_eq!(nested.load(), saved);
        assert_eq!(rig.files_in(&rig.dir.join("a").join("b")), ["settings.json"]);
    }

    #[test]
    fn save_leaves_no_temporary_file_behind() {
        let rig = Rig::new("no-temp");

        rig.store.save(&settings(Some("D:/Songs"), false, true, true));
        rig.store.save(&settings(Some("D:/Other"), false, true, true));

        assert_eq!(rig.files_in(&rig.dir), ["settings.json"]);
    }

    #[test]
    fn save_replaces_an_existing_file() {
        let rig = Rig::new("replace");
        rig.write(r#"{"outputDir":"D:/old","overwrite":true,"includeRank":false,"concurrency":4,"searchLyricsOnline":false,"future":1}"#);
        let saved = settings(Some("D:/new"), false, true, true);

        rig.store.save(&saved);

        assert_eq!(rig.store.load(), saved);
        assert_eq!(rig.files_in(&rig.dir), ["settings.json"]);
        assert!(!rig.text().contains("future"), "the old content is gone");
    }

    #[test]
    fn save_into_a_location_that_cannot_be_written_does_not_panic() {
        let rig = Rig::new("blocked");
        let blocker = rig.dir.join("not-a-folder");
        fs::write(&blocker, [1u8]).unwrap();
        let broken = SettingsFile::new(blocker.join("settings.json"));

        broken.save(&settings(Some("D:/Songs"), false, true, true));

        assert_eq!(broken.load(), UserSettings::default());
        assert_eq!(rig.files_in(&rig.dir), ["not-a-folder"], "nothing else was created");
    }

    #[test]
    fn an_unexpected_runtime_failure_never_escapes_from_load_or_save() {
        // The JVM test used a path whose every operation threw; here the path is one the platform refuses at every step.
        let broken = SettingsFile::new("bad\u{0}name\\settings.json");

        broken.save(&settings(Some("D:/Songs"), true, true, true));

        assert_eq!(broken.load(), UserSettings::default());
        let root = SettingsFile::new(if cfg!(windows) { "C:\\" } else { "/" });
        root.save(&UserSettings::default()); // a file system root has no name to build a temp file's from
        assert_eq!(root.load(), UserSettings::default());
    }

    #[test]
    fn save_over_a_directory_that_is_not_empty_does_not_panic_and_leaves_no_temporary_file() {
        let rig = Rig::new("over-dir");
        fs::create_dir(&rig.file).unwrap();
        fs::write(rig.file.join("keep.txt"), [1u8]).unwrap();

        rig.store.save(&settings(Some("D:/Songs"), false, true, true));

        assert_eq!(rig.files_in(&rig.dir), ["settings.json"]);
        assert_eq!(rig.files_in(&rig.file), ["keep.txt"]);
    }

    // ---- not in the Kotlin file ----

    #[test]
    fn saves_from_several_threads_leave_one_valid_file_and_no_temporary_file() {
        let rig = Rig::new("threads");
        std::thread::scope(|scope| {
            for index in 0..8 {
                let store = &rig.store;
                scope.spawn(move || store.save(&settings(Some(&format!("D:/Songs{index}")), index % 2 == 0, true, true)));
            }
        });

        let loaded = rig.store.load();
        assert!(loaded.output_dir.as_deref().is_some_and(|dir| dir.starts_with("D:/Songs")), "{loaded:?}");
        assert_eq!(rig.files_in(&rig.dir), ["settings.json"]);
    }

    #[test]
    fn quoted_booleans_are_read_like_kotlinx_does() {
        // kotlinx.serialization reads `"overwrite":"true"` as true even without the lenient mode (a hand-edited file); a file like that
        // must not fall back to the defaults, whose first change would overwrite the stored folder.
        let rig = Rig::new("quoted-booleans");
        rig.write(r#"{"outputDir":"D:/x","overwrite":"true","includeRank":"false","searchLyricsOnline":"false"}"#);

        assert_eq!(rig.store.load(), settings(Some("D:/x"), true, false, false));
    }

    #[test]
    fn the_settings_the_page_sends_are_read_in_the_shape_of_the_file() {
        let sent: UserSettings =
            serde_json::from_str(r#"{"outputDir":"D:\\음악","overwrite":true,"includeRank":false,"searchLyricsOnline":true}"#).unwrap();

        assert_eq!(sent, settings(Some("D:\\음악"), true, false, true));
        assert_eq!(serde_json::to_value(&sent).unwrap()["outputDir"], "D:\\음악", "and sent back in the same shape");
        assert!(serde_json::from_str::<UserSettings>(r#"{"outputDir":null,"overwrite":1,"includeRank":true,"searchLyricsOnline":true}"#).is_err());
    }
}
