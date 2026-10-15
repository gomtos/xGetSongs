//! The sidecar's runtime inside the one exe the app is delivered as.
//!
//! `xgs-pack` appends two things to a copy of the shell's exe: the files of the sidecar image (an archive of our own, read by
//! nothing but this module) and a footer at the very end that says where the archive is. Windows ignores what follows the
//! sections of an exe, so the file still runs as the shell. When it runs, [ensure_extracted] finds the footer, unpacks the archive
//! into a folder named after the archive's hash (once: the next start finds the folder complete and uses it), and the sidecar is
//! started from there. A different build of the app has another hash and gets a folder of its own; the old ones are removed.
//!
//! The archive is a sequence of entries, each `[method u8][path length u32][path][size u64][stored length u64][stored bytes]`
//! (little endian; method 0 is stored as it is, 1 is raw deflate; paths use `/`). The footer is
//! `[XGSPAYLD][version u32][archive offset u64][archive length u64][sha-256 of the archive]`.

use flate2::read::DeflateDecoder;
use flate2::write::DeflateEncoder;
use flate2::Compression;
use sha2::{Digest, Sha256};
use std::fs::{self, File};
use std::io::{self, BufReader, BufWriter, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

const MAGIC: &[u8; 8] = b"XGSPAYLD";
const VERSION: u32 = 1;
const FOOTER_LEN: u64 = 8 + 4 + 8 + 8 + 32;
/// Written into a runtime folder when everything is in it. A folder without it was cut short and is made again.
const COMPLETE_MARKER: &str = ".xgs-complete";
const STORED: u8 = 0;
const DEFLATED: u8 = 1;
/// No path of the sidecar image comes near this; a longer one is a damaged archive, not a reason to allocate.
const MAX_PATH_LEN: u32 = 4096;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Footer {
    pub offset: u64,
    pub length: u64,
    pub key: [u8; 32],
}

impl Footer {
    /// The name of the folder the archive is unpacked into: the first 16 hex digits of its hash.
    pub fn key_name(&self) -> String {
        self.key[..8].iter().map(|byte| format!("{byte:02x}")).collect()
    }

    fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(FOOTER_LEN as usize);
        bytes.extend_from_slice(MAGIC);
        bytes.extend_from_slice(&VERSION.to_le_bytes());
        bytes.extend_from_slice(&self.offset.to_le_bytes());
        bytes.extend_from_slice(&self.length.to_le_bytes());
        bytes.extend_from_slice(&self.key);
        bytes
    }
}

fn invalid(message: impl Into<String>) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message.into())
}

/// The footer at the end of [source], or None when it does not end with one (a plain exe, a short file). A footer that is there but
/// does not fit the file is an error: the file is damaged.
pub fn read_footer<R: Read + Seek>(source: &mut R) -> io::Result<Option<Footer>> {
    let size = source.seek(SeekFrom::End(0))?;
    if size < FOOTER_LEN {
        return Ok(None);
    }
    source.seek(SeekFrom::Start(size - FOOTER_LEN))?;
    let mut raw = vec![0u8; FOOTER_LEN as usize];
    source.read_exact(&mut raw)?;
    if &raw[..8] != MAGIC {
        return Ok(None);
    }
    let number = |from: usize| u64::from_le_bytes(raw[from..from + 8].try_into().expect("8 bytes"));
    let version = u32::from_le_bytes(raw[8..12].try_into().expect("4 bytes"));
    if version != VERSION {
        return Err(invalid(format!("the runtime in this exe has version {version}; this build reads version {VERSION}")));
    }
    let footer = Footer { offset: number(12), length: number(20), key: raw[28..60].try_into().expect("32 bytes") };
    if footer.offset.checked_add(footer.length) != Some(size - FOOTER_LEN) {
        return Err(invalid("the footer of the runtime does not fit the file"));
    }
    Ok(Some(footer))
}

/// What [pack] made.
#[derive(Debug)]
pub struct Packed {
    pub footer: Footer,
    pub files: usize,
    /// The size of the files of the image, and what the archive holds of them.
    pub original_bytes: u64,
    pub stored_bytes: u64,
}

/// A writer that hashes and counts what goes through it.
struct Counting<W: Write> {
    inner: W,
    hasher: Sha256,
    written: u64,
}

impl<W: Write> Write for Counting<W> {
    fn write(&mut self, buffer: &[u8]) -> io::Result<usize> {
        let count = self.inner.write(buffer)?;
        self.hasher.update(&buffer[..count]);
        self.written += count as u64;
        Ok(count)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.inner.flush()
    }
}

/// Every file under [dir] as a path relative to it with `/`, in order (the same folder always gives the same archive).
fn list_files(dir: &Path) -> io::Result<Vec<String>> {
    fn walk(root: &Path, current: &Path, found: &mut Vec<String>) -> io::Result<()> {
        for entry in fs::read_dir(current)? {
            let entry = entry?;
            let path = entry.path();
            if entry.file_type()?.is_dir() {
                walk(root, &path, found)?;
            } else {
                let relative = path.strip_prefix(root).expect("below the root");
                let parts: Vec<String> = relative
                    .components()
                    .map(|part| part.as_os_str().to_str().map(str::to_string).ok_or_else(|| invalid(format!("not a UTF-8 name: {}", path.display()))))
                    .collect::<io::Result<_>>()?;
                found.push(parts.join("/"));
            }
        }
        Ok(())
    }
    let mut found = Vec::new();
    walk(dir, dir, &mut found)?;
    found.sort();
    Ok(found)
}

/// Writes a copy of [exe] to [output] with the files of [image] and the footer appended. An exe that carries a runtime already is
/// refused: packing twice would hide the first one behind the second.
pub fn pack(exe: &Path, image: &Path, output: &Path) -> io::Result<Packed> {
    if read_footer(&mut File::open(exe)?)?.is_some() {
        return Err(invalid(format!("{} already carries a runtime", exe.display())));
    }
    let mut out = BufWriter::with_capacity(1 << 20, File::create(output)?);
    let offset = io::copy(&mut BufReader::new(File::open(exe)?), &mut out)?;
    let mut archive = Counting { inner: out, hasher: Sha256::new(), written: 0 };
    let (mut files, mut original_bytes) = (0usize, 0u64);
    for relative in list_files(image)? {
        let path = image.join(&relative);
        let mut compressed = Vec::new();
        let mut original = 0u64;
        {
            let mut encoder = DeflateEncoder::new(&mut compressed, Compression::new(6));
            let mut input = BufReader::new(File::open(&path)?);
            let mut buffer = vec![0u8; 1 << 16];
            loop {
                let count = input.read(&mut buffer)?;
                if count == 0 {
                    break;
                }
                encoder.write_all(&buffer[..count])?;
                original += count as u64;
            }
            encoder.finish()?;
        }
        // Data that does not shrink (an already compressed jar, say) is kept as it is: that costs nothing to unpack.
        let deflate = (compressed.len() as u64) < original;
        let name = relative.as_bytes();
        archive.write_all(&[if deflate { DEFLATED } else { STORED }])?;
        archive.write_all(&(name.len() as u32).to_le_bytes())?;
        archive.write_all(name)?;
        archive.write_all(&original.to_le_bytes())?;
        if deflate {
            archive.write_all(&(compressed.len() as u64).to_le_bytes())?;
            archive.write_all(&compressed)?;
        } else {
            archive.write_all(&original.to_le_bytes())?;
            io::copy(&mut BufReader::new(File::open(&path)?), &mut archive)?;
        }
        files += 1;
        original_bytes += original;
    }
    let length = archive.written;
    let key: [u8; 32] = archive.hasher.clone().finalize().into();
    let footer = Footer { offset, length, key };
    let mut out = archive.inner;
    out.write_all(&footer.to_bytes())?;
    out.flush()?;
    Ok(Packed { footer, files, original_bytes, stored_bytes: length })
}

/// A path of the archive as a path below the folder it is unpacked into. Refused: anything that could leave that folder or name a
/// drive (`..`, a leading `/`, `\`, `:`) and an empty part.
fn safe_relative(name: &str) -> io::Result<PathBuf> {
    if name.is_empty() || name.contains('\\') || name.contains(':') || name.contains('\0') {
        return Err(invalid(format!("a path in the runtime is not allowed: {name:?}")));
    }
    let mut path = PathBuf::new();
    for part in name.split('/') {
        if part.is_empty() || part == "." || part == ".." {
            return Err(invalid(format!("a path in the runtime is not allowed: {name:?}")));
        }
        path.push(part);
    }
    Ok(path)
}

/// Unpacks the archive at [footer] of [source] into the existing folder [target]. Returns how many files it made.
pub fn extract<R: Read + Seek>(source: &mut R, footer: &Footer, target: &Path) -> io::Result<usize> {
    source.seek(SeekFrom::Start(footer.offset))?;
    let mut archive = BufReader::with_capacity(1 << 20, source.by_ref().take(footer.length));
    let mut files = 0usize;
    loop {
        let mut method = [0u8; 1];
        if archive.read(&mut method)? == 0 {
            return Ok(files);
        }
        let mut word = [0u8; 4];
        archive.read_exact(&mut word)?;
        let name_length = u32::from_le_bytes(word);
        if name_length == 0 || name_length > MAX_PATH_LEN {
            return Err(invalid("the runtime is damaged (a path of an impossible length)"));
        }
        let mut name = vec![0u8; name_length as usize];
        archive.read_exact(&mut name)?;
        let name = String::from_utf8(name).map_err(|_| invalid("the runtime is damaged (a path that is not UTF-8)"))?;
        let mut number = [0u8; 8];
        archive.read_exact(&mut number)?;
        let original = u64::from_le_bytes(number);
        archive.read_exact(&mut number)?;
        let stored = u64::from_le_bytes(number);
        let destination = target.join(safe_relative(&name)?);
        if let Some(parent) = destination.parent() {
            fs::create_dir_all(parent)?;
        }
        let mut data = archive.by_ref().take(stored);
        let mut file = BufWriter::with_capacity(1 << 20, File::create(&destination)?);
        let written = match method[0] {
            STORED => io::copy(&mut data, &mut file)?,
            DEFLATED => io::copy(&mut DeflateDecoder::new(&mut data), &mut file)?,
            other => return Err(invalid(format!("the runtime is damaged (method {other})"))),
        };
        io::copy(&mut data, &mut io::sink())?;
        file.flush()?;
        if data.limit() != 0 {
            return Err(invalid(format!("the runtime is cut short at {name}")));
        }
        if written != original {
            return Err(invalid(format!("the runtime is damaged at {name}: {written} bytes instead of {original}")));
        }
        files += 1;
    }
}

fn is_complete(folder: &Path) -> bool {
    folder.join(COMPLETE_MARKER).is_file()
}

fn remove_folder(folder: &Path) {
    let _ = fs::remove_dir_all(folder);
}

/// A name the cache makes for its own folders: a key (16 hex digits) or a half-made one (`<key>.partial-<pid>`).
fn is_cache_name(name: &str) -> bool {
    let key = name.split(".partial-").next().unwrap_or(name);
    key.len() == 16 && key.chars().all(|c| c.is_ascii_hexdigit() && !c.is_ascii_uppercase())
}

/// The folder with the runtime of [exe], unpacked into [cache] when it is not there yet. None when [exe] carries no runtime. The
/// folders of other builds in [cache] are removed afterwards (best effort: a failure to do so is not a failure to start).
pub fn ensure_extracted(exe: &Path, cache: &Path) -> Result<Option<PathBuf>, String> {
    let describe = |what: &str, error: io::Error| format!("xGetSongs의 실행 파일에 든 런타임을 {what}: {error}");
    let mut file = File::open(exe).map_err(|error| describe("읽지 못했습니다", error))?;
    let Some(footer) = read_footer(&mut file).map_err(|error| describe("읽지 못했습니다", error))? else {
        return Ok(None);
    };
    let name = footer.key_name();
    let ready = cache.join(&name);
    if !is_complete(&ready) {
        remove_folder(&ready); // what an earlier start left half-way
        let partial = cache.join(format!("{name}.partial-{}", std::process::id()));
        remove_folder(&partial);
        let made = fs::create_dir_all(&partial)
            .and_then(|()| extract(&mut file, &footer, &partial))
            .and_then(|_| fs::write(partial.join(COMPLETE_MARKER), name.as_bytes()));
        if let Err(error) = made {
            remove_folder(&partial);
            return Err(describe("풀지 못했습니다", error));
        }
        if let Err(error) = fs::rename(&partial, &ready) {
            remove_folder(&partial);
            // Another copy of the app may have finished the same folder in the meantime: that one is as good.
            if !is_complete(&ready) {
                return Err(describe("자리에 놓지 못했습니다", error));
            }
        }
    }
    if let Ok(entries) = fs::read_dir(cache) {
        for entry in entries.flatten() {
            let other = entry.file_name().to_string_lossy().into_owned();
            if other != name && is_cache_name(&other) {
                remove_folder(&entry.path());
            }
        }
    }
    Ok(Some(ready))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    /// A fresh folder in the temp folder that is deleted again when the test is over.
    struct Scratch(PathBuf);

    impl Scratch {
        fn new(name: &str) -> Scratch {
            let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
            let dir = std::env::temp_dir().join(format!("xgs-payload-test-{}-{name}-{nanos}", std::process::id()));
            fs::create_dir_all(&dir).unwrap();
            Scratch(dir)
        }

        fn path(&self, relative: &str) -> PathBuf {
            self.0.join(relative)
        }
    }

    impl Drop for Scratch {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    /// A sidecar image in small: nested folders, an empty file, text that compresses, bytes that do not, and a Korean name.
    fn image(scratch: &Scratch) -> PathBuf {
        let image = scratch.path("image");
        fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
        fs::create_dir_all(image.join("lib")).unwrap();
        fs::write(image.join("runtime").join("bin").join("java.exe"), b"MZ fake java").unwrap();
        fs::write(image.join("lib").join("empty.txt"), b"").unwrap();
        fs::write(image.join("lib").join("text.txt"), "abc ".repeat(10_000)).unwrap();
        let noise: Vec<u8> = (0..20_000u32).map(|n| (n.wrapping_mul(2_654_435_761) >> 13) as u8).collect();
        fs::write(image.join("lib").join("noise.bin"), noise).unwrap();
        fs::write(image.join("lib").join("가사.txt"), "한글 이름의 파일").unwrap();
        image
    }

    fn fake_exe(scratch: &Scratch) -> PathBuf {
        let exe = scratch.path("shell.exe");
        fs::write(&exe, b"MZ the shell, as far as a test cares".repeat(100)).unwrap();
        exe
    }

    fn same_files(left: &Path, right: &Path) {
        let (a, b) = (list_files(left).unwrap(), list_files(right).unwrap());
        let b: Vec<String> = b.into_iter().filter(|name| name != COMPLETE_MARKER).collect(); // the marker is the cache's, not the image's
        assert_eq!(a, b, "the same files");
        for name in a {
            assert_eq!(fs::read(left.join(&name)).unwrap(), fs::read(right.join(&name)).unwrap(), "{name}");
        }
    }

    /// An archive of the given entries, written by hand, for the cases `pack` never makes.
    fn archive_of(entries: &[(&str, &[u8])]) -> (Vec<u8>, Footer) {
        let mut bytes = Vec::new();
        for (name, data) in entries {
            bytes.push(STORED);
            bytes.extend_from_slice(&(name.len() as u32).to_le_bytes());
            bytes.extend_from_slice(name.as_bytes());
            bytes.extend_from_slice(&(data.len() as u64).to_le_bytes());
            bytes.extend_from_slice(&(data.len() as u64).to_le_bytes());
            bytes.extend_from_slice(data);
        }
        let footer = Footer { offset: 0, length: bytes.len() as u64, key: Sha256::digest(&bytes).into() };
        bytes.extend_from_slice(&footer.to_bytes());
        (bytes, footer)
    }

    #[test]
    fn a_file_that_does_not_end_with_a_footer_has_none() {
        assert_eq!(read_footer(&mut Cursor::new(Vec::new())).unwrap(), None);
        assert_eq!(read_footer(&mut Cursor::new(b"short".to_vec())).unwrap(), None);
        assert_eq!(read_footer(&mut Cursor::new(vec![7u8; 500])).unwrap(), None);
    }

    #[test]
    fn a_footer_that_does_not_fit_the_file_is_an_error() {
        let (mut bytes, _) = archive_of(&[("a.txt", b"data")]);
        bytes.insert(0, 0); // everything moves by one: the footer no longer points at the archive's end

        let error = read_footer(&mut Cursor::new(bytes)).unwrap_err();

        assert!(error.to_string().contains("does not fit"), "{error}");
    }

    #[test]
    fn a_footer_of_another_version_is_an_error() {
        let (mut bytes, _) = archive_of(&[("a.txt", b"data")]);
        let at = bytes.len() - FOOTER_LEN as usize + 8;
        bytes[at] = 9;

        let error = read_footer(&mut Cursor::new(bytes)).unwrap_err();

        assert!(error.to_string().contains("version 9"), "{error}");
    }

    #[test]
    fn what_is_packed_comes_out_the_same_and_the_exe_is_still_in_front() {
        let scratch = Scratch::new("roundtrip");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        let output = scratch.path("packed.exe");

        let packed = pack(&exe, &image, &output).unwrap();

        assert_eq!(packed.files, 5);
        let bytes = fs::read(&output).unwrap();
        let original = fs::read(&exe).unwrap();
        assert_eq!(&bytes[..original.len()], &original[..], "the exe is unchanged in front");
        let mut file = File::open(&output).unwrap();
        let footer = read_footer(&mut file).unwrap().expect("a footer");
        assert_eq!(footer, packed.footer);
        assert_eq!(footer.offset, original.len() as u64);
        let target = scratch.path("out");
        fs::create_dir_all(&target).unwrap();
        assert_eq!(extract(&mut file, &footer, &target).unwrap(), 5);
        same_files(&image, &target);
    }

    #[test]
    fn text_is_stored_smaller_and_noise_is_not_made_bigger() {
        let scratch = Scratch::new("sizes");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));

        let packed = pack(&exe, &image, &scratch.path("packed.exe")).unwrap();

        assert!(packed.stored_bytes < packed.original_bytes, "{packed:?}");
        let noise = 20_000u64;
        assert!(packed.stored_bytes < packed.original_bytes - 30_000 + noise + 5 * 64, "the 40,000 bytes of text shrank: {packed:?}");
    }

    #[test]
    fn the_same_folder_packs_to_the_same_key_and_another_content_to_another() {
        let scratch = Scratch::new("key");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));

        let first = pack(&exe, &image, &scratch.path("a.exe")).unwrap();
        let second = pack(&exe, &image, &scratch.path("b.exe")).unwrap();
        fs::write(image.join("lib").join("text.txt"), "changed").unwrap();
        let third = pack(&exe, &image, &scratch.path("c.exe")).unwrap();

        assert_eq!(first.footer.key, second.footer.key);
        assert_ne!(first.footer.key, third.footer.key);
        assert_eq!(first.footer.key_name().len(), 16);
    }

    #[test]
    fn an_exe_that_carries_a_runtime_is_not_packed_again() {
        let scratch = Scratch::new("twice");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        pack(&exe, &image, &scratch.path("once.exe")).unwrap();

        let error = pack(&scratch.path("once.exe"), &image, &scratch.path("twice.exe")).unwrap_err();

        assert!(error.to_string().contains("already carries"), "{error}");
    }

    #[test]
    fn paths_that_could_leave_the_folder_are_refused() {
        for name in ["../escape.txt", "a/../../escape.txt", "/absolute.txt", "C:/drive.txt", "a\\b.txt", "a//b.txt", "./a.txt", "a/./b.txt"] {
            let scratch = Scratch::new("slip");
            let target = scratch.path("out");
            fs::create_dir_all(&target).unwrap();
            let (bytes, footer) = archive_of(&[(name, b"data")]);

            let error = extract(&mut Cursor::new(bytes), &footer, &target).unwrap_err();

            assert!(error.to_string().contains("not allowed"), "{name:?}: {error}");
            assert!(!scratch.path("escape.txt").exists(), "{name:?} wrote outside");
            assert_eq!(fs::read_dir(&target).unwrap().count(), 0, "{name:?} left something behind");
        }
    }

    #[test]
    fn an_entry_without_a_path_is_an_error() {
        let scratch = Scratch::new("nameless");
        let mut bytes = vec![STORED];
        bytes.extend_from_slice(&0u32.to_le_bytes());
        let footer = Footer { offset: 0, length: bytes.len() as u64, key: [0; 32] };

        let error = extract(&mut Cursor::new(bytes), &footer, &scratch.0).unwrap_err();

        assert!(error.to_string().contains("impossible length"), "{error}");
    }

    /// What is in [cache], by name (nothing when it does not exist).
    fn names_in(cache: &Path) -> Vec<String> {
        fs::read_dir(cache).map(|entries| entries.flatten().map(|entry| entry.file_name().to_string_lossy().into_owned()).collect()).unwrap_or_default()
    }

    #[test]
    fn an_exe_that_lost_bytes_in_the_middle_is_an_error_and_leaves_nothing_in_the_cache() {
        let scratch = Scratch::new("lost");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        let output = scratch.path("packed.exe");
        pack(&exe, &image, &output).unwrap();
        // the footer still sits at the end, but its offsets no longer fit the file
        let mut bytes = fs::read(&output).unwrap();
        let at = fs::metadata(&exe).unwrap().len() as usize + 100;
        bytes.drain(at..at + 200);
        fs::write(&output, bytes).unwrap();
        let cache = scratch.path("cache");

        let error = ensure_extracted(&output, &cache).unwrap_err();

        assert!(error.contains("읽지 못했습니다") && error.contains("does not fit"), "{error}");
        assert_eq!(names_in(&cache), Vec::<String>::new());
    }

    #[test]
    fn an_archive_that_ends_inside_a_file_is_an_error_and_leaves_nothing_in_the_cache() {
        let scratch = Scratch::new("cut");
        let mut bytes = vec![STORED];
        bytes.extend_from_slice(&5u32.to_le_bytes());
        bytes.extend_from_slice(b"a.txt");
        bytes.extend_from_slice(&100u64.to_le_bytes());
        bytes.extend_from_slice(&100u64.to_le_bytes()); // says 100 bytes
        bytes.extend_from_slice(b"only ten b"); // holds 10
        let footer = Footer { offset: 0, length: bytes.len() as u64, key: [1; 32] };
        bytes.extend_from_slice(&footer.to_bytes());
        let exe = scratch.path("cut.exe");
        fs::write(&exe, bytes).unwrap();
        let cache = scratch.path("cache");

        let error = ensure_extracted(&exe, &cache).unwrap_err();

        assert!(error.contains("풀지 못했습니다") && error.contains("cut short at a.txt"), "{error}");
        assert_eq!(names_in(&cache), Vec::<String>::new());
    }

    #[test]
    fn a_file_whose_size_is_not_what_the_entry_says_is_an_error() {
        let scratch = Scratch::new("size");
        let target = scratch.path("out");
        fs::create_dir_all(&target).unwrap();
        let mut bytes = Vec::new();
        bytes.push(STORED);
        bytes.extend_from_slice(&5u32.to_le_bytes());
        bytes.extend_from_slice(b"a.txt");
        bytes.extend_from_slice(&9u64.to_le_bytes()); // says 9 bytes
        bytes.extend_from_slice(&4u64.to_le_bytes()); // holds 4
        bytes.extend_from_slice(b"data");
        let footer = Footer { offset: 0, length: bytes.len() as u64, key: [0; 32] };
        bytes.extend_from_slice(&footer.to_bytes());

        let error = extract(&mut Cursor::new(bytes), &footer, &target).unwrap_err();

        assert!(error.to_string().contains("damaged at a.txt"), "{error}");
    }

    #[test]
    fn ensure_extracted_unpacks_once_and_uses_the_folder_the_next_time() {
        let scratch = Scratch::new("once");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        let output = scratch.path("packed.exe");
        pack(&exe, &image, &output).unwrap();
        let cache = scratch.path("cache");

        let first = ensure_extracted(&output, &cache).unwrap().expect("a runtime");

        same_files(&image, &first.join("."));
        assert!(first.join(COMPLETE_MARKER).is_file());
        // a mark of ours in the folder survives the next start: it is used, not unpacked again
        fs::write(first.join("lib").join("text.txt"), "marked").unwrap();
        let second = ensure_extracted(&output, &cache).unwrap().expect("a runtime");
        assert_eq!(second, first);
        assert_eq!(fs::read_to_string(second.join("lib").join("text.txt")).unwrap(), "marked");
    }

    #[test]
    fn a_folder_without_the_complete_marker_is_unpacked_again() {
        let scratch = Scratch::new("incomplete");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        let output = scratch.path("packed.exe");
        let packed = pack(&exe, &image, &output).unwrap();
        let cache = scratch.path("cache");
        let half = cache.join(packed.footer.key_name());
        fs::create_dir_all(half.join("lib")).unwrap();
        fs::write(half.join("lib").join("text.txt"), "half").unwrap();

        let ready = ensure_extracted(&output, &cache).unwrap().expect("a runtime");

        assert_eq!(ready, half);
        assert_eq!(fs::read_to_string(ready.join("lib").join("text.txt")).unwrap(), "abc ".repeat(10_000));
        assert!(ready.join(COMPLETE_MARKER).is_file());
    }

    #[test]
    fn the_runtimes_of_other_builds_are_removed_and_other_folders_are_left_alone() {
        let scratch = Scratch::new("old");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        let output = scratch.path("packed.exe");
        pack(&exe, &image, &output).unwrap();
        let cache = scratch.path("cache");
        for name in ["0123456789abcdef", "fedcba9876543210.partial-4242", "notes", "FEDCBA0987654321", "short"] {
            fs::create_dir_all(cache.join(name)).unwrap();
        }

        ensure_extracted(&output, &cache).unwrap();

        let mut left: Vec<String> = fs::read_dir(&cache).unwrap().flatten().map(|e| e.file_name().to_string_lossy().into_owned()).collect();
        left.sort();
        assert_eq!(left.len(), 4, "{left:?}");
        assert!(left.contains(&"notes".to_string()) && left.contains(&"short".to_string()) && left.contains(&"FEDCBA0987654321".to_string()));
        assert!(!left.contains(&"0123456789abcdef".to_string()) && !left.contains(&"fedcba9876543210.partial-4242".to_string()));
    }

    #[test]
    fn an_exe_without_a_runtime_gives_none_and_touches_nothing() {
        let scratch = Scratch::new("plain");
        let exe = fake_exe(&scratch);
        let cache = scratch.path("cache");

        assert_eq!(ensure_extracted(&exe, &cache).unwrap(), None);
        assert!(!cache.exists());
    }

    #[test]
    fn an_exe_that_cannot_be_read_is_an_error_that_says_so() {
        let error = ensure_extracted(Path::new("C:\\no\\such\\shell.exe"), Path::new("C:\\no\\cache")).unwrap_err();

        assert!(error.contains("읽지 못했습니다"), "{error}");
    }
}
