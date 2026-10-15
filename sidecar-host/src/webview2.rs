//! Whether the window of the app can open at all: it needs Microsoft's WebView2 runtime. Windows 11 and a Windows 10 that has been
//! updated since 2022 come with it; an older one may not. Without it the window fails to open and the app ends without a word, so the
//! app looks for the runtime first and says what is missing.

/// Where Microsoft's updater writes the version (`pv`) of the Evergreen runtime, in the order Microsoft documents: for all users on
/// a 64-bit Windows, for all users on a 32-bit one, for the current user only.
const LOCATIONS: [(Hive, &str); 3] = [
    (Hive::LocalMachine, r"SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
    (Hive::LocalMachine, r"SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
    (Hive::CurrentUser, r"Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
];

/// The page that hands out Microsoft's installer for the runtime.
pub const DOWNLOAD_URL: &str = "https://go.microsoft.com/fwlink/p/?LinkId=2124703";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Hive {
    LocalMachine,
    CurrentUser,
}

/// A runtime that was uninstalled leaves `0.0.0.0` behind; an empty value is no version either.
fn is_real_version(pv: &str) -> bool {
    let pv = pv.trim();
    !pv.is_empty() && pv != "0.0.0.0"
}

/// The first version [read] finds in the places the runtime writes it to. [read] gets the hive and the key and returns the value
/// `pv` of that key, if there is one.
fn find_version(read: impl Fn(Hive, &str) -> Option<String>) -> Option<String> {
    LOCATIONS.iter().filter_map(|(hive, key)| read(*hive, key)).find(|pv| is_real_version(pv)).map(|pv| pv.trim().to_string())
}

/// The version of the WebView2 runtime that is installed, or None when there is none.
#[cfg(windows)]
pub fn installed_version() -> Option<String> {
    use winreg::enums::{HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    use winreg::RegKey;
    find_version(|hive, key| {
        let root = RegKey::predef(match hive {
            Hive::LocalMachine => HKEY_LOCAL_MACHINE,
            Hive::CurrentUser => HKEY_CURRENT_USER,
        });
        root.open_subkey(key).ok()?.get_value::<String, _>("pv").ok()
    })
}

#[cfg(not(windows))]
pub fn installed_version() -> Option<String> {
    None
}

/// What the message says when the runtime is missing, and what the two buttons do.
pub const MISSING_TITLE: &str = "xGetSongs";
pub const MISSING_TEXT: &str = "xGetSongs 창을 여는 데 필요한 Microsoft Edge WebView2 런타임을 이 PC에서 찾지 못했습니다.\n\n\
    [확인]을 누르면 설치 프로그램을 받는 페이지를 엽니다. 설치한 뒤 xGetSongs를 다시 실행해 주세요.";

/// Shows [MISSING_TEXT] with an OK and a Cancel button; true when the person chose OK (to go and get the runtime).
#[cfg(windows)]
pub fn ask_to_install() -> bool {
    use std::ffi::c_void;
    use std::os::windows::ffi::OsStrExt;
    const MB_OKCANCEL: u32 = 0x1;
    const MB_ICONERROR: u32 = 0x10;
    const IDOK: i32 = 1;
    #[link(name = "user32")]
    unsafe extern "system" {
        fn MessageBoxW(window: *mut c_void, text: *const u16, caption: *const u16, kind: u32) -> i32;
    }
    let wide = |text: &str| std::ffi::OsStr::new(text).encode_wide().chain(std::iter::once(0)).collect::<Vec<u16>>();
    let (text, title) = (wide(MISSING_TEXT), wide(MISSING_TITLE));
    // SAFETY: both strings are NUL-terminated and outlive the call; a null owner window is allowed.
    unsafe { MessageBoxW(std::ptr::null_mut(), text.as_ptr(), title.as_ptr(), MB_OKCANCEL | MB_ICONERROR) == IDOK }
}

#[cfg(not(windows))]
pub fn ask_to_install() -> bool {
    false
}

/// Opens [DOWNLOAD_URL] in the browser the person uses (handed to Explorer, as the folders are).
pub fn open_download_page() {
    let _ = std::process::Command::new("explorer.exe").arg(DOWNLOAD_URL).spawn();
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A registry in small: the value `pv` at some of the places (by their index in [LOCATIONS]).
    fn registry(entries: &[(Hive, usize, &str)]) -> impl Fn(Hive, &str) -> Option<String> {
        let entries: Vec<(Hive, &'static str, String)> =
            entries.iter().map(|(hive, index, pv)| (*hive, LOCATIONS[*index].1, pv.to_string())).collect();
        move |hive, key| entries.iter().find(|(h, k, _)| *h == hive && *k == key).map(|(_, _, value)| value.clone())
    }

    #[test]
    fn nothing_in_the_registry_is_no_runtime() {
        assert_eq!(find_version(registry(&[])), None);
    }

    #[test]
    fn a_version_at_any_of_the_three_places_is_found() {
        assert_eq!(find_version(registry(&[(Hive::LocalMachine, 0, "120.0.2210.91")])).as_deref(), Some("120.0.2210.91"));
        assert_eq!(find_version(registry(&[(Hive::LocalMachine, 1, "119.0.1")])).as_deref(), Some("119.0.1"));
        assert_eq!(find_version(registry(&[(Hive::CurrentUser, 2, "118.0.5")])).as_deref(), Some("118.0.5"));
    }

    #[test]
    fn the_leftover_of_an_uninstalled_runtime_is_no_runtime() {
        assert_eq!(find_version(registry(&[(Hive::LocalMachine, 0, "0.0.0.0")])), None);
        assert_eq!(find_version(registry(&[(Hive::LocalMachine, 0, ""), (Hive::CurrentUser, 2, "  ")])), None);
    }

    #[test]
    fn a_real_version_is_found_behind_a_leftover() {
        assert_eq!(
            find_version(registry(&[(Hive::LocalMachine, 0, "0.0.0.0"), (Hive::CurrentUser, 2, "118.0.5")])).as_deref(),
            Some("118.0.5")
        );
    }

    #[test]
    fn the_same_key_in_another_hive_is_not_the_same_place() {
        assert_eq!(find_version(registry(&[(Hive::CurrentUser, 0, "120.0.1")])), None);
    }

    #[cfg(windows)]
    #[test]
    fn the_registry_of_this_pc_gives_a_version_or_none_and_never_a_leftover() {
        match installed_version() {
            None => {}
            Some(version) => {
                assert!(is_real_version(&version), "{version}");
                assert!(version.chars().all(|c| c.is_ascii_digit() || c == '.'), "{version}");
            }
        }
    }

    #[test]
    fn the_message_names_the_runtime_and_what_to_do() {
        assert!(MISSING_TEXT.contains("WebView2"));
        assert!(MISSING_TEXT.contains("[확인]"));
        assert!(DOWNLOAD_URL.starts_with("https://go.microsoft.com/"));
    }
}
