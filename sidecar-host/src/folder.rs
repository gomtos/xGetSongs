//! Folders for the screen. Ported from `app/src/desktopMain/kotlin/com/xgetsongs/app/ExplorerOpener.kt` and `FolderPicker.kt`: the
//! tests of this file are the cases of `ExplorerOpenerTest.kt`, one for one and under the same name in snake case.

use std::path::{Path, PathBuf};
use std::process::Command;

/// Whether the platform would take [path] as a path at all: no control characters, none of `<>"|?*`, and a `:` only as the
/// one of a drive (`C:`). Kotlin's `Path.of` threw an `InvalidPathException` for the others, which meant "no folder to show".
fn is_path_the_platform_takes(path: &str) -> bool {
    path.char_indices().all(|(index, character)| match character {
        '<' | '>' | '"' | '|' | '?' | '*' => false,
        ':' => index == 1,
        other => !other.is_control(),
    })
}

/// The folder to show for [path]: the first existing directory on the way up from it (the path itself when it is a directory, the
/// parent of a regular file, then the parents, up to the root), or None for a blank path, one the platform rejects, or one whose
/// whole chain is missing (a drive that does not exist). A relative path is read from the working folder, which is where the
/// server would put the files too. Never panics.
pub fn existing_folder_for(path: &str) -> Option<PathBuf> {
    if path.trim().is_empty() || !is_path_the_platform_takes(path) {
        return None;
    }
    let mut candidate: Option<&Path> = None;
    let absolute = std::path::absolute(path).ok()?;
    let mut current = absolute.as_path();
    loop {
        if current.is_dir() {
            candidate = Some(current);
            break;
        }
        match current.parent() {
            Some(parent) => current = parent,
            None => break,
        }
    }
    candidate.map(Path::to_path_buf)
}

/// [path] itself when it is an existing directory, else None: where the folder chooser starts (Kotlin: `File(initial).takeIf
/// { it.isDirectory }`).
pub fn existing_directory(path: &str) -> Option<PathBuf> {
    if path.trim().is_empty() {
        return None;
    }
    // Absolute: the system's dialog cannot start in a relative path (Kotlin's `File` resolved it against the working folder).
    let directory = std::path::absolute(path).ok()?;
    directory.is_dir().then_some(directory)
}

/// The command line argument that makes Explorer open [folder] and nothing else. Always in quotes: Explorer splits its command line at
/// commas, and `Command::arg` quotes a path only when it holds a space or a tab (`D:\Music\Hits,2020` would open the Documents folder).
fn explorer_argument(folder: &Path) -> String {
    format!("\"{}\"", folder.display())
}

fn explorer_command(folder: &Path) -> Command {
    let mut command = Command::new("explorer.exe");
    #[cfg(windows)]
    std::os::windows::process::CommandExt::raw_arg(&mut command, explorer_argument(folder));
    #[cfg(not(windows))]
    command.arg(folder);
    command
}

/// Shows [existing_folder_for] of [path] in the system file manager; does nothing when there is no such folder. All of it runs on
/// a thread of its own, so the caller never waits for the file manager (or for a network share that does not answer). Opening the
/// folder is only a convenience, so every failure is swallowed.
pub fn open_in_explorer(path: &str) {
    let path = path.to_string();
    let _ = std::thread::Builder::new().name("open-in-explorer".into()).spawn(move || {
        if let Some(folder) = existing_folder_for(&path) {
            let _ = explorer_command(&folder).spawn();
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    /// A fresh folder in the temp folder that is deleted again when the test is over.
    struct Scratch(PathBuf);

    impl Scratch {
        fn new(name: &str) -> Scratch {
            let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
            let dir = std::env::temp_dir().join(format!("xgs-folder-test-{}-{name}-{nanos}", std::process::id()));
            fs::create_dir_all(&dir).unwrap();
            Scratch(dir)
        }
    }

    impl Drop for Scratch {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn text(path: &Path) -> &str {
        path.to_str().unwrap()
    }

    #[test]
    fn an_existing_directory_returns_itself() {
        let scratch = Scratch::new("existing");
        let folder = scratch.0.join("Music");
        fs::create_dir_all(&folder).unwrap();

        assert_eq!(existing_folder_for(text(&folder)), Some(folder.clone()));
        assert_eq!(existing_folder_for(text(&scratch.0)), Some(scratch.0.clone()));
    }

    #[test]
    fn a_missing_child_of_an_existing_directory_returns_the_parent() {
        let scratch = Scratch::new("child");
        let folder = scratch.0.join("Music");
        fs::create_dir_all(&folder).unwrap();

        assert_eq!(existing_folder_for(text(&folder.join("My Playlist"))), Some(folder));
    }

    #[test]
    fn several_missing_levels_return_the_nearest_existing_ancestor() {
        let scratch = Scratch::new("levels");
        let folder = scratch.0.join("Music").join("Rock");
        fs::create_dir_all(&folder).unwrap();

        assert_eq!(existing_folder_for(text(&folder.join("a").join("b").join("c"))), Some(folder));
        assert_eq!(existing_folder_for(text(&scratch.0.join("x").join("y"))), Some(scratch.0.clone()));
    }

    #[test]
    fn a_regular_file_returns_its_parent_directory() {
        let scratch = Scratch::new("file");
        let folder = scratch.0.join("Music");
        fs::create_dir_all(&folder).unwrap();
        let file = folder.join("001 A - T.mp3");
        fs::write(&file, "data").unwrap();

        assert_eq!(existing_folder_for(text(&file)), Some(folder));
    }

    #[test]
    fn a_path_below_a_regular_file_returns_the_directory_around_the_file() {
        let scratch = Scratch::new("below");
        let file = scratch.0.join("a-file");
        fs::write(&file, "data").unwrap();

        assert_eq!(existing_folder_for(text(&file.join("child"))), Some(scratch.0.clone()));
    }

    #[test]
    fn a_blank_path_returns_none() {
        assert_eq!(existing_folder_for(""), None);
        assert_eq!(existing_folder_for("   "), None);
        assert_eq!(existing_folder_for("\t"), None);
    }

    #[test]
    fn a_path_the_platform_rejects_returns_none_and_does_not_panic() {
        let scratch = Scratch::new("rejected");

        assert_eq!(existing_folder_for(&format!("{}\\bad\u{0}name", text(&scratch.0))), None);
        assert_eq!(existing_folder_for("\u{0}"), None);
        assert_eq!(existing_folder_for(&format!("{}\\what?", text(&scratch.0))), None, "a question mark cannot be in a name");
        assert_eq!(existing_folder_for(&format!("{}\\a<b", text(&scratch.0))), None);
    }

    #[test]
    fn a_path_with_no_existing_ancestor_returns_none() {
        // Only Windows has roots that can be missing (a drive letter nobody uses).
        if !cfg!(windows) {
            return;
        }
        let missing = ('A'..='Z').rev().find(|letter| !Path::new(&format!("{letter}:\\")).exists()).expect("a free drive letter");

        assert_eq!(existing_folder_for(&format!("{missing}:\\Music\\My Playlist")), None);
        assert_eq!(existing_folder_for(&format!("{missing}:\\")), None);
    }

    #[test]
    fn a_relative_path_is_looked_up_from_the_working_folder() {
        let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let missing = format!("xgs-missing-folder-{nanos}");

        let found = existing_folder_for(&missing);

        assert_eq!(found, Some(std::env::current_dir().unwrap()), "the nearest existing ancestor of a relative path is the working folder");
    }

    #[test]
    fn opening_without_a_folder_to_open_does_nothing_and_does_not_panic() {
        open_in_explorer("");
        open_in_explorer("   ");
        open_in_explorer("\u{0}");
    }

    // ---- not in the Kotlin file: where the folder chooser starts ----

    #[test]
    fn the_chooser_starts_in_the_typed_folder_only_when_it_is_one() {
        let scratch = Scratch::new("chooser");
        let file = scratch.0.join("a-file");
        fs::write(&file, "data").unwrap();

        assert_eq!(existing_directory(text(&scratch.0)), Some(scratch.0.clone()));
        assert_eq!(existing_directory(text(&scratch.0.join("missing"))), None);
        assert_eq!(existing_directory(text(&file)), None, "a file is not a folder to start in");
        assert_eq!(existing_directory(""), None);
        assert_eq!(existing_directory("   "), None);
    }

    #[test]
    fn a_drive_letter_path_with_its_colon_is_a_path() {
        assert!(is_path_the_platform_takes("C:\\Music"));
        assert!(is_path_the_platform_takes("D:/음악/내 재생목록"));
        assert!(is_path_the_platform_takes("relative\\folder"));
        assert!(!is_path_the_platform_takes("C:\\a:b"), "a colon anywhere else is not");
    }

    // ---- not in the Kotlin file: what the review of stage 4 found ----

    #[test]
    fn the_folder_is_handed_to_explorer_in_quotes_so_a_comma_does_not_split_it() {
        // `explorer.exe C:\Music\Hits,2020` opens the Documents folder: Explorer splits its command line at commas. A path without a
        // space is not quoted by `Command::arg`, so the quotes are put on here.
        assert_eq!(explorer_argument(Path::new(r"C:\Music\Hits,2020")), r#""C:\Music\Hits,2020""#);
        assert_eq!(explorer_argument(Path::new(r"C:\Music\Hits 2020")), r#""C:\Music\Hits 2020""#);
        assert_eq!(explorer_argument(Path::new(r"C:\음악\내 재생목록")), r#""C:\음악\내 재생목록""#);
    }

    #[test]
    fn a_relative_folder_is_given_to_the_chooser_as_an_absolute_path() {
        // The dialog cannot start in a relative path; Kotlin's `File` resolved it against the working folder.
        let found = existing_directory(".").expect("the working folder is a folder");

        assert!(found.is_absolute(), "{found:?}");
        assert_eq!(found, std::path::absolute(".").unwrap());
    }
}
