use std::ffi::OsString;
use std::path::PathBuf;
use std::process::Command;

const MAIN_CLASS: &str = "com.xgetsongs.server.sidecar.SidecarMainKt";

/// How to start the sidecar: the `java` to run, the folder with its jars (`server/build/install/xgs-server/lib`), and the
/// app data folder it gets as `--app-data`.
#[derive(Debug)]
pub struct SidecarConfig {
    pub java: PathBuf,
    pub lib_dir: PathBuf,
    pub app_data: PathBuf,
}

impl SidecarConfig {
    pub fn from_env() -> Result<SidecarConfig, String> {
        SidecarConfig::from_lookup(|key| std::env::var_os(key))
    }

    /// The spike reads `XGS_SIDECAR_LIB` (required), `XGS_SIDECAR_JAVA` (default: `java` on the PATH) and
    /// `XGS_SIDECAR_APPDATA` (default: a temp folder, never the user's real app data).
    pub fn from_lookup(get: impl Fn(&str) -> Option<OsString>) -> Result<SidecarConfig, String> {
        let lib_dir = get("XGS_SIDECAR_LIB").map(PathBuf::from).ok_or_else(|| {
            "XGS_SIDECAR_LIB is not set (the lib folder of server/build/install/xgs-server)".to_string()
        })?;
        let java = get("XGS_SIDECAR_JAVA").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("java"));
        let app_data = get("XGS_SIDECAR_APPDATA")
            .map(PathBuf::from)
            .unwrap_or_else(|| std::env::temp_dir().join("xgs-spike-appdata"));
        Ok(SidecarConfig { java, lib_dir, app_data })
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

    #[test]
    fn the_lib_folder_is_required() {
        let error = SidecarConfig::from_lookup(lookup(&[])).unwrap_err();
        assert!(error.contains("XGS_SIDECAR_LIB"), "{error}");
    }

    #[test]
    fn java_defaults_to_the_one_on_the_path_and_the_app_data_to_a_temp_folder() {
        let config = SidecarConfig::from_lookup(lookup(&[("XGS_SIDECAR_LIB", r"C:\x\lib")])).unwrap();
        assert_eq!(config.java, PathBuf::from("java"));
        assert_eq!(config.app_data, std::env::temp_dir().join("xgs-spike-appdata"));
    }

    #[test]
    fn the_given_values_are_used() {
        let config = SidecarConfig::from_lookup(lookup(&[
            ("XGS_SIDECAR_LIB", r"C:\x\lib"),
            ("XGS_SIDECAR_JAVA", r"C:\jdk\bin\java.exe"),
            ("XGS_SIDECAR_APPDATA", r"C:\data"),
        ]))
        .unwrap();
        assert_eq!(config.lib_dir, PathBuf::from(r"C:\x\lib"));
        assert_eq!(config.java, PathBuf::from(r"C:\jdk\bin\java.exe"));
        assert_eq!(config.app_data, PathBuf::from(r"C:\data"));
    }

    #[test]
    fn the_command_runs_the_main_class_with_the_class_path_and_the_app_data_folder() {
        let config = SidecarConfig {
            java: PathBuf::from("java"),
            lib_dir: PathBuf::from(r"C:\x\lib"),
            app_data: PathBuf::from(r"C:\data"),
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
            ]
        );
    }

    #[test]
    fn the_stderr_log_lives_in_the_app_data_folder() {
        let config = SidecarConfig {
            java: PathBuf::from("java"),
            lib_dir: PathBuf::from(r"C:\x\lib"),
            app_data: PathBuf::from(r"C:\data"),
        };
        assert_eq!(config.stderr_log(), PathBuf::from(r"C:\data\sidecar-stderr.log"));
    }
}
