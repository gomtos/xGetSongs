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

impl SidecarConfig {
    pub fn from_env() -> Result<SidecarConfig, String> {
        let exe_dir = std::env::current_exe().ok().and_then(|exe| exe.parent().map(Path::to_path_buf));
        SidecarConfig::from_lookup(|key| std::env::var_os(key), exe_dir.as_deref())
    }

    /// Where java and the jars come from, in this order:
    /// 1. `XGS_SIDECAR_LIB` (the lib folder of `installDist`) with `XGS_SIDECAR_JAVA` (default: `java` on the PATH):
    ///    for development;
    /// 2. a sidecar image, a folder with `runtime\bin\java.exe` and `lib`: `XGS_SIDECAR_DIR`, else the `sidecar` folder next
    ///    to the executable ([exe_dir]). A folder that is named but holds no runtime is an error, not a reason to look on.
    ///
    /// The app data folder is `XGS_SIDECAR_APPDATA`, by default a temp folder, never the user's real app data (the spike
    /// must not clear the `work` folder of a running copy of the app). The log folder is `XGS_SIDECAR_LOGDIR`, else `log`
    /// next to the executable, else the sidecar chooses.
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
        let app_data = get("XGS_SIDECAR_APPDATA")
            .map(PathBuf::from)
            .unwrap_or_else(|| std::env::temp_dir().join("xgs-spike-appdata"));
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
        assert_eq!(config.app_data, std::env::temp_dir().join("xgs-spike-appdata"));
        assert_eq!(config.log_dir, None);
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
