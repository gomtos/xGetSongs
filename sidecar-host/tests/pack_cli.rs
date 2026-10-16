//! `xgs-pack`, the tool that appends a sidecar image to the shell's exe, run as a person runs it.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use xgs_sidecar::payload;

struct Scratch(PathBuf);

impl Scratch {
    fn new(name: &str) -> Scratch {
        let dir = std::env::temp_dir().join(format!("xgs-pack-cli-test-{}-{name}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        Scratch(dir)
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

fn pack(args: &[&Path]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_xgs-pack")).args(args).output().expect("xgs-pack starts")
}

fn text(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).into_owned()
}

fn image_in(dir: &Path) -> PathBuf {
    let image = dir.join("image");
    fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
    fs::create_dir_all(image.join("lib")).unwrap();
    fs::write(image.join("runtime").join("bin").join("java.exe"), b"fake java").unwrap();
    fs::write(image.join("lib").join("server.jar"), b"fake jar").unwrap();
    image
}

#[test]
fn an_image_is_appended_to_a_copy_of_the_exe_and_the_summary_names_what_went_in() {
    let scratch = Scratch::new("ok");
    let image = image_in(&scratch.0);
    let exe = scratch.0.join("shell.exe");
    fs::write(&exe, b"MZ the shell").unwrap();
    let output = scratch.0.join("out").join("xGetSongs.exe");

    let result = pack(&[&exe, &image, &output]);

    assert!(result.status.success(), "{}", text(&result.stderr));
    let footer = payload::read_footer(&mut fs::File::open(&output).unwrap()).unwrap().expect("the output carries a runtime");
    assert_eq!(footer.offset, b"MZ the shell".len() as u64);
    let summary = text(&result.stdout);
    assert!(summary.contains("2 files"), "{summary}");
    assert!(summary.contains(&footer.key_name()), "{summary}");
    assert_eq!(fs::read(&exe).unwrap(), b"MZ the shell", "the exe it was given is left alone");
}

#[test]
fn the_wrong_number_of_arguments_is_a_usage_message_and_exit_code_2() {
    let result = pack(&[Path::new("only-one")]);

    assert_eq!(result.status.code(), Some(2));
    assert!(text(&result.stderr).contains("usage"), "{}", text(&result.stderr));
}

#[test]
fn a_folder_that_is_not_a_sidecar_image_is_refused_and_nothing_is_written() {
    let scratch = Scratch::new("not-an-image");
    let exe = scratch.0.join("shell.exe");
    fs::write(&exe, b"MZ the shell").unwrap();
    let wrong = scratch.0.join("wrong");
    fs::create_dir_all(wrong.join("lib")).unwrap();
    let output = scratch.0.join("xGetSongs.exe");

    let result = pack(&[&exe, &wrong, &output]);

    assert_eq!(result.status.code(), Some(1));
    assert!(text(&result.stderr).contains("java.exe"), "{}", text(&result.stderr));
    assert!(!output.exists());
}

#[test]
fn an_exe_that_already_carries_a_runtime_is_refused() {
    let scratch = Scratch::new("twice");
    let image = image_in(&scratch.0);
    let exe = scratch.0.join("shell.exe");
    fs::write(&exe, b"MZ the shell").unwrap();
    let once = scratch.0.join("once.exe");
    assert!(pack(&[&exe, &image, &once]).status.success());

    let result = pack(&[&once, &image, &scratch.0.join("twice.exe")]);

    assert_eq!(result.status.code(), Some(1));
    assert!(text(&result.stderr).contains("already carries"), "{}", text(&result.stderr));
}

#[test]
fn packing_an_exe_over_itself_is_refused_and_leaves_it_alone() {
    let scratch = Scratch::new("itself");
    let image = image_in(&scratch.0);
    let exe = scratch.0.join("shell.exe");
    fs::write(&exe, b"MZ the shell").unwrap();

    let result = pack(&[&exe, &image, &exe]);

    assert_eq!(result.status.code(), Some(1));
    assert!(text(&result.stderr).contains("same file"), "{}", text(&result.stderr));
    assert_eq!(fs::read(&exe).unwrap(), b"MZ the shell");
}
