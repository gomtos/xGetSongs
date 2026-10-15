use crate::payload;
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::process::Command;

const MAIN_CLASS: &str = "com.xgetsongs.server.sidecar.SidecarMainKt";

/// How to start the sidecar: the `java` to run, the folder with its jars, the app data folder it gets as `--app-data` and,
/// when the shell knows one, the folder it should write its log files to (`--log-dir`).
#[derive(Debug)]
pub struct SidecarConfig {
    pub java: PathBuf,
    pub lib_dir: PathBuf,
    pub app_data: PathBuf,
    pub log_dir: Option<PathBuf>,
}

/// Where the app keeps its own files: the yt-dlp it installed, `settings.json`, the temporary download folders. The same folder
/// the Compose app uses: `%APPDATA%\xGetSongs`, else `.xgetsongs` in the user's folder. `XGS_SIDECAR_APPDATA` names another one
/// (the checks use a temp folder: the sidecar clears `work` in it when it starts, which must never happen to the real one of a
/// running app).
pub fn app_data_dir(get: impl Fn(&str) -> Option<OsString>) -> PathBuf {
    let given = |name: &str| get(name).filter(|value| !value.is_empty());
    if let Some(dir) = given("XGS_SIDECAR_APPDATA") {
        return PathBuf::from(dir);
    }
    if let Some(appdata) = given("APPDATA") {
        return PathBuf::from(appdata).join("xGetSongs");
    }
    given("USERPROFILE").map(PathBuf::from).unwrap_or_else(std::env::temp_dir).join(".xgetsongs")
}

/// Where the runtime that an exe carries is unpacked to (a cache: it can be deleted and is made again). Below the app data folder
/// when `XGS_SIDECAR_APPDATA` names one (so that a check that moves the app data folder moves this too), else in
/// `%LOCALAPPDATA%\xGetSongs` (a few hundred MB do not belong in the roaming profile), else below the app data folder.
pub fn runtime_cache_dir(get: impl Fn(&str) -> Option<OsString>) -> PathBuf {
    let given = |name: &str| get(name).filter(|value| !value.is_empty());
    if let Some(dir) = given("XGS_SIDECAR_APPDATA") {
        return PathBuf::from(dir).join("runtime");
    }
    if let Some(local) = given("LOCALAPPDATA") {
        return PathBuf::from(local).join("xGetSongs").join("runtime");
    }
    app_data_dir(&get).join("runtime")
}

impl SidecarConfig {
    pub fn from_env() -> Result<SidecarConfig, String> {
        let exe = std::env::current_exe().ok();
        SidecarConfig::resolve(|key| std::env::var_os(key), exe.as_deref())
    }

    /// [from_lookup] for the running program [exe]: when neither `XGS_SIDECAR_LIB` nor `XGS_SIDECAR_DIR` says where the sidecar is
    /// and [exe] carries one (see [crate::payload]), the sidecar is started from a copy of it unpacked to [runtime_cache_dir] (the
    /// first start takes a while: that is where the time goes). An exe that carries a runtime but cannot give it up is an error, not
    /// a reason to look elsewhere. The log folder is then the sidecar's own choice (the app data folder), not `log` next to an exe
    /// that may lie anywhere; `XGS_SIDECAR_LOGDIR` still names one.
    pub fn resolve(get: impl Fn(&str) -> Option<OsString>, exe: Option<&Path>) -> Result<SidecarConfig, String> {
        // A variable that is set to nothing is not set (a shell that "clears" one by setting it to an empty string leaves exactly that).
        let get = |key: &str| get(key).filter(|value| !value.is_empty());
        let carried = match exe {
            Some(exe) if get("XGS_SIDECAR_LIB").is_none() && get("XGS_SIDECAR_DIR").is_none() => {
                payload::ensure_extracted(exe, &runtime_cache_dir(get))?
            }
            _ => None,
        };
        if let Some(image) = carried {
            let image = image.into_os_string();
            return SidecarConfig::from_lookup(
                |key| if key == "XGS_SIDECAR_DIR" { Some(image.clone()) } else { get(key) },
                None,
            );
        }
        SidecarConfig::from_lookup(get, exe.and_then(Path::parent))
    }

    /// Where java and the jars come from, in this order:
    /// 1. `XGS_SIDECAR_LIB` (the lib folder of `installDist`) with `XGS_SIDECAR_JAVA` (default: `java` on the PATH):
    ///    for development;
    /// 2. a sidecar image, a folder with `runtime\bin\java.exe` and `lib`: `XGS_SIDECAR_DIR`, else the `sidecar` folder next
    ///    to the executable ([exe_dir]). A folder that is named but holds no runtime is an error, not a reason to look on.
    ///
    /// The app data folder is [app_data_dir]. The log folder is `XGS_SIDECAR_LOGDIR`, else `log` next to the executable, else
    /// the sidecar chooses.
    pub fn from_lookup(
        get: impl Fn(&str) -> Option<OsString>,
        exe_dir: Option<&Path>,
    ) -> Result<SidecarConfig, String> {
        let (java, lib_dir) = if let Some(lib) = get("XGS_SIDECAR_LIB") {
            let java = get("XGS_SIDECAR_JAVA").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("java"));
            (java, PathBuf::from(lib))
        } else {
            let image = get("XGS_SIDECAR_DIR")
                .map(PathBuf::from)
                .or_else(|| exe_dir.map(|dir| dir.join("sidecar")))
                .ok_or_else(|| {
                    "no sidecar to start: set XGS_SIDECAR_LIB (the lib folder of server/build/install/xgs-server) or \
                     XGS_SIDECAR_DIR (an image folder with runtime and lib), or put a sidecar folder next to the executable"
                        .to_string()
                })?;
            let java = image.join("runtime").join("bin").join("java.exe");
            if !java.is_file() {
                return Err(format!(
                    "no sidecar runtime: {} is missing (set XGS_SIDECAR_LIB for a development build, or XGS_SIDECAR_DIR)",
                    java.display()
                ));
            }
            (java, image.join("lib"))
        };
        let app_data = app_data_dir(&get);
        let log_dir = get("XGS_SIDECAR_LOGDIR").map(PathBuf::from).or_else(|| exe_dir.map(|dir| dir.join("log")));
        Ok(SidecarConfig { java, lib_dir, app_data, log_dir })
    }

    pub fn command(&self) -> Command {
        let mut command = Command::new(&self.java);
        command
            .arg("-Dlogback.configurationFile=logback-sidecar.xml")
            .arg("-cp")
            .arg(self.lib_dir.join("*"))
            .arg(MAIN_CLASS)
            .arg("--app-data")
            .arg(&self.app_data);
        if let Some(log_dir) = &self.log_dir {
            command.arg("--log-dir").arg(log_dir);
        }
        command
    }

    /// Where what the sidecar writes to stderr is kept (the shell has no console to show it).
    pub fn stderr_log(&self) -> PathBuf {
        self.app_data.join("sidecar-stderr.log")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use std::ffi::OsString;

    fn lookup(values: &[(&str, &str)]) -> impl Fn(&str) -> Option<OsString> {
        let map: HashMap<String, OsString> =
            values.iter().map(|(k, v)| (k.to_string(), OsString::from(v))).collect();
        move |key| map.get(key).cloned()
    }

    /// A folder shaped like a sidecar image: `runtime\bin\java.exe` (an empty file is enough) and `lib`.
    fn fake_image(name: &str) -> PathBuf {
        let image = std::env::temp_dir().join(format!("xgs-config-test-{}-{}", std::process::id(), name));
        let _ = std::fs::remove_dir_all(&image);
        std::fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
        std::fs::write(image.join("runtime").join("bin").join("java.exe"), b"").unwrap();
        std::fs::create_dir_all(image.join("lib")).unwrap();
        image
    }

    #[test]
    fn nothing_to_start_is_an_error_that_names_the_ways_to_say_where_it_is() {
        let error = SidecarConfig::from_lookup(lookup(&[]), None).unwrap_err();
        assert!(error.contains("XGS_SIDECAR_LIB"), "{error}");
        assert!(error.contains("XGS_SIDECAR_DIR"), "{error}");
    }

    #[test]
    fn the_lib_folder_variable_keeps_working_for_development() {
        let config = SidecarConfig::from_lookup(lookup(&[("XGS_SIDECAR_LIB", r"C:\x\lib")]), None).unwrap();
        assert_eq!(config.java, PathBuf::from("java"));
        assert_eq!(config.lib_dir, PathBuf::from(r"C:\x\lib"));
        assert_eq!(config.app_data, std::env::temp_dir().join(".xgetsongs"), "no APPDATA and no user folder: the temp folder");
        assert_eq!(config.log_dir, None);
    }

    #[test]
    fn the_app_data_folder_is_the_one_of_the_compose_app_unless_another_is_named() {
        let appdata = r"C:\Users\me\AppData\Roaming";
        assert_eq!(app_data_dir(lookup(&[("APPDATA", appdata)])), PathBuf::from(r"C:\Users\me\AppData\Roaming\xGetSongs"));
        assert_eq!(
            app_data_dir(lookup(&[("APPDATA", appdata), ("XGS_SIDECAR_APPDATA", r"D:\other")])),
            PathBuf::from(r"D:\other"),
            "the variable of the checks wins"
        );
        assert_eq!(
            app_data_dir(lookup(&[("USERPROFILE", r"C:\Users\me")])),
            PathBuf::from(r"C:\Users\me\.xgetsongs"),
            "without APPDATA: the folder in the user folder, like the Compose app"
        );
        assert_eq!(
            app_data_dir(lookup(&[("APPDATA", ""), ("XGS_SIDECAR_APPDATA", ""), ("USERPROFILE", r"C:\Users\me")])),
            PathBuf::from(r"C:\Users\me\.xgetsongs"),
            "empty values count as not set"
        );
    }

    #[test]
    fn the_given_values_are_used() {
        let config = SidecarConfig::from_lookup(
            lookup(&[
                ("XGS_SIDECAR_LIB", r"C:\x\lib"),
                ("XGS_SIDECAR_JAVA", r"C:\jdk\bin\java.exe"),
                ("XGS_SIDECAR_APPDATA", r"C:\data"),
                ("XGS_SIDECAR_LOGDIR", r"C:\logs"),
            ]),
            None,
        )
        .unwrap();
        assert_eq!(config.java, PathBuf::from(r"C:\jdk\bin\java.exe"));
        assert_eq!(config.app_data, PathBuf::from(r"C:\data"));
        assert_eq!(config.log_dir, Some(PathBuf::from(r"C:\logs")));
    }

    #[test]
    fn an_image_folder_from_the_variable_brings_its_own_runtime_and_jars() {
        let image = fake_image("variable");

        let config =
            SidecarConfig::from_lookup(lookup(&[("XGS_SIDECAR_DIR", image.to_str().unwrap())]), None).unwrap();

        assert_eq!(config.java, image.join("runtime").join("bin").join("java.exe"));
        assert_eq!(config.lib_dir, image.join("lib"));
        let _ = std::fs::remove_dir_all(&image);
    }

    #[test]
    fn a_sidecar_folder_next_to_the_executable_is_found_without_any_variable() {
        let exe_dir = std::env::temp_dir().join(format!("xgs-config-test-{}-exe", std::process::id()));
        let _ = std::fs::remove_dir_all(&exe_dir);
        let image = exe_dir.join("sidecar");
        std::fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
        std::fs::write(image.join("runtime").join("bin").join("java.exe"), b"").unwrap();

        let config = SidecarConfig::from_lookup(lookup(&[]), Some(&exe_dir)).unwrap();

        assert_eq!(config.java, image.join("runtime").join("bin").join("java.exe"));
        assert_eq!(config.lib_dir, image.join("lib"));
        assert_eq!(config.log_dir, Some(exe_dir.join("log")), "the logs go next to the executable");
        let _ = std::fs::remove_dir_all(&exe_dir);
    }

    #[test]
    fn an_image_folder_without_a_runtime_is_an_error_and_not_a_silent_fallback() {
        let image = std::env::temp_dir().join(format!("xgs-config-test-{}-empty", std::process::id()));
        let _ = std::fs::remove_dir_all(&image);
        std::fs::create_dir_all(&image).unwrap();

        let error =
            SidecarConfig::from_lookup(lookup(&[("XGS_SIDECAR_DIR", image.to_str().unwrap())]), None).unwrap_err();

        assert!(error.contains("java.exe"), "{error}");
        let _ = std::fs::remove_dir_all(&image);
    }

    #[test]
    fn the_command_runs_the_main_class_with_the_class_path_and_the_folders() {
        let config = SidecarConfig {
            java: PathBuf::from("java"),
            lib_dir: PathBuf::from(r"C:\x\lib"),
            app_data: PathBuf::from(r"C:\data"),
            log_dir: Some(PathBuf::from(r"C:\logs")),
        };
        let command = config.command();
        assert_eq!(command.get_program(), "java");
        let args: Vec<String> = command.get_args().map(|a| a.to_string_lossy().into_owned()).collect();
        assert_eq!(
            args,
            vec![
                "-Dlogback.configurationFile=logback-sidecar.xml",
                "-cp",
                r"C:\x\lib\*",
                "com.xgetsongs.server.sidecar.SidecarMainKt",
                "--app-data",
                r"C:\data",
                "--log-dir",
                r"C:\logs",
            ]
        );
    }

    #[test]
    fn without_a_log_folder_the_command_leaves_the_choice_to_the_sidecar() {
        let config = SidecarConfig {
            java: PathBuf::from("java"),
            lib_dir: PathBuf::from(r"C:\x\lib"),
            app_data: PathBuf::from(r"C:\data"),
            log_dir: None,
        };
        let args: Vec<String> = config.command().get_args().map(|a| a.to_string_lossy().into_owned()).collect();
        assert!(!args.iter().any(|a| a == "--log-dir"), "{args:?}");
    }

    /// A fresh folder in the temp folder, deleted when the test ends.
    struct Scratch(PathBuf);

    impl Scratch {
        fn new(name: &str) -> Scratch {
            let dir = std::env::temp_dir().join(format!("xgs-resolve-test-{}-{name}", std::process::id()));
            let _ = std::fs::remove_dir_all(&dir);
            std::fs::create_dir_all(&dir).unwrap();
            Scratch(dir)
        }

        fn text(&self) -> &str {
            self.0.to_str().unwrap()
        }
    }

    impl Drop for Scratch {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    /// An exe that carries a sidecar image (a fake `java.exe` and a `lib` with one file) as `xgs-pack` makes it.
    fn packed_exe(scratch: &Scratch) -> PathBuf {
        let image = scratch.0.join("image");
        std::fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
        std::fs::create_dir_all(image.join("lib")).unwrap();
        std::fs::write(image.join("runtime").join("bin").join("java.exe"), b"fake java").unwrap();
        std::fs::write(image.join("lib").join("server.jar"), b"fake jar").unwrap();
        let exe = scratch.0.join("shell.exe");
        std::fs::write(&exe, b"MZ fake shell").unwrap();
        let packed = scratch.0.join("xGetSongs.exe");
        crate::payload::pack(&exe, &image, &packed).unwrap();
        packed
    }

    #[test]
    fn an_exe_that_carries_a_sidecar_starts_it_from_a_copy_in_the_app_data_folder() {
        let scratch = Scratch::new("carries");
        let exe = packed_exe(&scratch);
        let data = scratch.0.join("data");

        let config = SidecarConfig::resolve(lookup(&[("XGS_SIDECAR_APPDATA", data.to_str().unwrap())]), Some(&exe)).unwrap();

        let image = config.lib_dir.parent().unwrap().to_path_buf();
        assert_eq!(image.parent().unwrap(), data.join("runtime"), "unpacked below the runtime folder of the app data folder");
        assert_eq!(config.java, image.join("runtime").join("bin").join("java.exe"));
        assert_eq!(std::fs::read(config.java.as_path()).unwrap(), b"fake java");
        assert_eq!(std::fs::read(config.lib_dir.join("server.jar")).unwrap(), b"fake jar");
        assert_eq!(config.app_data, data);
        assert_eq!(config.log_dir, None, "no log folder next to an exe that may lie anywhere: the sidecar writes to the app data folder");
    }

    #[test]
    fn the_unpacked_copy_goes_to_the_local_app_data_folder_when_the_app_data_folder_is_not_named() {
        let scratch = Scratch::new("local");
        let exe = packed_exe(&scratch);
        let local = scratch.0.join("local");

        let config = SidecarConfig::resolve(
            lookup(&[("APPDATA", scratch.0.join("roaming").to_str().unwrap()), ("LOCALAPPDATA", local.to_str().unwrap())]),
            Some(&exe),
        )
        .unwrap();

        assert!(config.java.starts_with(local.join("xGetSongs").join("runtime")), "{:?}", config.java);
        assert_eq!(config.app_data, scratch.0.join("roaming").join("xGetSongs"), "the app's own files stay where the Compose app kept them");
    }

    #[test]
    fn the_log_folder_variable_is_still_honoured_for_an_exe_that_carries_a_sidecar() {
        let scratch = Scratch::new("logdir");
        let exe = packed_exe(&scratch);
        let data = scratch.0.join("data");

        let config = SidecarConfig::resolve(
            lookup(&[("XGS_SIDECAR_APPDATA", data.to_str().unwrap()), ("XGS_SIDECAR_LOGDIR", r"C:\logs")]),
            Some(&exe),
        )
        .unwrap();

        assert_eq!(config.log_dir, Some(PathBuf::from(r"C:\logs")));
    }

    #[test]
    fn the_variables_for_development_win_over_what_the_exe_carries_and_unpack_nothing() {
        let scratch = Scratch::new("variables");
        let exe = packed_exe(&scratch);
        let data = scratch.0.join("data");
        let image = fake_image("variables-image");

        let by_lib = SidecarConfig::resolve(
            lookup(&[("XGS_SIDECAR_APPDATA", data.to_str().unwrap()), ("XGS_SIDECAR_LIB", r"C:\x\lib")]),
            Some(&exe),
        )
        .unwrap();
        let by_dir = SidecarConfig::resolve(
            lookup(&[("XGS_SIDECAR_APPDATA", data.to_str().unwrap()), ("XGS_SIDECAR_DIR", image.to_str().unwrap())]),
            Some(&exe),
        )
        .unwrap();

        assert_eq!(by_lib.lib_dir, PathBuf::from(r"C:\x\lib"));
        assert_eq!(by_dir.lib_dir, image.join("lib"));
        assert!(!data.join("runtime").exists(), "nothing was unpacked");
        let _ = std::fs::remove_dir_all(&image);
    }

    #[test]
    fn an_exe_that_carries_nothing_falls_back_to_the_sidecar_folder_next_to_it() {
        let scratch = Scratch::new("plain");
        let image = scratch.0.join("sidecar");
        std::fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
        std::fs::write(image.join("runtime").join("bin").join("java.exe"), b"").unwrap();
        let exe = scratch.0.join("shell.exe");
        std::fs::write(&exe, b"MZ plain shell").unwrap();

        let config = SidecarConfig::resolve(lookup(&[("XGS_SIDECAR_APPDATA", scratch.text())]), Some(&exe)).unwrap();

        assert_eq!(config.lib_dir, image.join("lib"));
        assert_eq!(config.log_dir, Some(scratch.0.join("log")), "as before: the logs go next to the exe");
    }

    #[test]
    fn an_exe_that_carries_nothing_and_has_no_sidecar_next_to_it_is_an_error_that_names_the_ways_to_say_where_it_is() {
        let scratch = Scratch::new("nothing");
        let exe = scratch.0.join("shell.exe");
        std::fs::write(&exe, b"MZ plain shell").unwrap();

        let error = SidecarConfig::resolve(lookup(&[("XGS_SIDECAR_APPDATA", scratch.text())]), Some(&exe)).unwrap_err();

        assert!(error.contains("java.exe"), "{error}");
    }

    #[test]
    fn a_damaged_runtime_in_the_exe_is_an_error_that_says_so_and_not_a_silent_fallback() {
        let scratch = Scratch::new("damaged");
        let exe = packed_exe(&scratch);
        let mut bytes = std::fs::read(&exe).unwrap();
        bytes.drain(20..40); // the footer no longer fits the file
        std::fs::write(&exe, bytes).unwrap();
        // a sidecar folder next to the exe must not hide that
        std::fs::create_dir_all(scratch.0.join("sidecar").join("runtime").join("bin")).unwrap();
        std::fs::write(scratch.0.join("sidecar").join("runtime").join("bin").join("java.exe"), b"").unwrap();

        let error = SidecarConfig::resolve(lookup(&[("XGS_SIDECAR_APPDATA", scratch.text())]), Some(&exe)).unwrap_err();

        assert!(error.contains("런타임"), "{error}");
    }

    #[test]
    fn variables_that_are_empty_count_as_not_set() {
        // A variable that was "cleared" by setting it to nothing (PowerShell does that) must not switch off the exe's own runtime.
        let scratch = Scratch::new("empty");
        let exe = packed_exe(&scratch);
        let data = scratch.0.join("data");

        let config = SidecarConfig::resolve(
            lookup(&[
                ("XGS_SIDECAR_APPDATA", data.to_str().unwrap()),
                ("XGS_SIDECAR_LIB", ""),
                ("XGS_SIDECAR_DIR", ""),
                ("XGS_SIDECAR_JAVA", ""),
                ("XGS_SIDECAR_LOGDIR", ""),
            ]),
            Some(&exe),
        )
        .unwrap();

        assert!(config.java.starts_with(data.join("runtime")), "{:?}", config.java);
        assert_eq!(config.log_dir, None);
    }

    #[test]
    fn without_an_exe_the_old_rules_apply() {
        let error = SidecarConfig::resolve(lookup(&[]), None).unwrap_err();

        assert!(error.contains("XGS_SIDECAR_LIB"), "{error}");
    }

    #[test]
    fn the_stderr_log_lives_in_the_app_data_folder() {
        let config = SidecarConfig {
            java: PathBuf::from("java"),
            lib_dir: PathBuf::from(r"C:\x\lib"),
            app_data: PathBuf::from(r"C:\data"),
            log_dir: None,
        };
        assert_eq!(config.stderr_log(), PathBuf::from(r"C:\data\sidecar-stderr.log"));
    }
}
