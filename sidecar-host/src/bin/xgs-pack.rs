//! Appends a sidecar image to a copy of the shell's exe, so that one file is all there is to hand out:
//!
//!     xgs-pack <shell.exe> <sidecar-image-folder> <output.exe>
//!
//! The image is what `gradlew :server:sidecarImage` makes (`runtime\bin\java.exe` and `lib`); see `xgs_sidecar::payload` for what the
//! output is and how the app unpacks it.

use std::fs;
use std::path::Path;
use std::process::ExitCode;
use xgs_sidecar::payload;

fn megabytes(bytes: u64) -> f64 {
    bytes as f64 / (1024.0 * 1024.0)
}

fn run(exe: &Path, image: &Path, output: &Path) -> Result<String, String> {
    let java = image.join("runtime").join("bin").join("java.exe");
    if !java.is_file() || !image.join("lib").is_dir() {
        return Err(format!("{} is not a sidecar image: {} or its lib folder is missing", image.display(), java.display()));
    }
    // Writing the output empties it before the exe is read: the same file would be lost.
    if fs::canonicalize(exe).ok().is_some_and(|exe| Some(exe) == fs::canonicalize(output).ok()) {
        return Err(format!("{} is the same file as the output", exe.display()));
    }
    if let Some(parent) = output.parent().filter(|parent| !parent.as_os_str().is_empty()) {
        fs::create_dir_all(parent).map_err(|error| format!("cannot create {}: {error}", parent.display()))?;
    }
    let packed = payload::pack(exe, image, output).map_err(|error| {
        let _ = fs::remove_file(output); // no half-written exe to be mistaken for a real one
        format!("cannot pack {}: {error}", image.display())
    })?;
    Ok(format!(
        "{}: {} files, {:.1} MB of image stored as {:.1} MB behind the exe, key {}, {:.1} MB in all",
        output.display(),
        packed.files,
        megabytes(packed.original_bytes),
        megabytes(packed.stored_bytes),
        packed.footer.key_name(),
        megabytes(fs::metadata(output).map(|meta| meta.len()).unwrap_or(0)),
    ))
}

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let [exe, image, output] = arguments.as_slice() else {
        eprintln!("usage: xgs-pack <shell.exe> <sidecar-image-folder> <output.exe>");
        return ExitCode::from(2);
    };
    match run(Path::new(exe), Path::new(image), Path::new(output)) {
        Ok(summary) => {
            println!("{summary}");
            ExitCode::SUCCESS
        }
        Err(message) => {
            eprintln!("xgs-pack: {message}");
            ExitCode::FAILURE
        }
    }
}
