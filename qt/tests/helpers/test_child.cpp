// xgs-test-child: the child process that tst_system_process_runner (and the differential oracle's argv check) starts. Win32
// and C++ only, no Qt. Lines it writes end in "\n".
//
//   xgs-test-child argv [args...]          every argument after "argv", as the C runtime split them (wmain's argv), one line
//                                          each: its UTF-16 units as 4 lowercase hex digits each, "." for an empty argument
//   xgs-test-child emit <hex> <hex>        writes the first bytes (2 hex digits each, "." or nothing for none) to stdout and
//                                          the second to stderr, as they are; exit code 3
//   xgs-test-child sleep <ms>              writes "started", then sleeps
//   xgs-test-child sleeperr <ms>           the same with "started" on stderr
//   xgs-test-child spawn <pid file> <ms>   starts itself as "sleep <ms>" (inheriting no handle, no console window), writes
//                                          that child's process id into <pid file> (decimal, written whole: through a
//                                          temporary file that is renamed), writes "started", then sleeps <ms>
//   xgs-test-child spawnexit <pid file> <ms>
//                                          the same as spawn, but exits at once (code 0) after writing "started": the
//                                          grandchild, which holds none of the pipes, sleeps on alone
//   xgs-test-child sleepshow <ms>          writes "started <process id> <its standard output handle's value>" (decimal),
//                                          then sleeps
//   xgs-test-child env <name>              the environment variable's value as UTF-8, "(unset)" when there is none
//   xgs-test-child stdin                   reads standard input to its end and writes the number of bytes read
//   xgs-test-child console                 "none" when the process has no console window, else "hidden" or "visible"
//   xgs-test-child fakeytdlp <ms> [args...]
//                                          plays yt-dlp for tst_job_with_real_processes: finds the value after "-o" in args
//                                          (yt-dlp's output template `<dir>\<id>.%(ext)s`), reads it as yt-dlp does for the
//                                          fields the engine uses ("%%" is "%", "%(id)s" the video id taken from the file
//                                          name part, "%(ext)s" "m4a", any other "%" stays), creates the folders it names and
//                                          writes the m4a there, writes two progress lines (XGSP downloading 50 %, finished),
//                                          sleeps <ms> and exits with 0. No "-o": exit code 4; the file cannot be written: 5
//   xgs-test-child fakeytdlpspawn <pid file> <ms> [args...]
//                                          the same, after starting itself as "sleep <ms>" and writing that child's process id
//                                          into <pid file> as spawn does (an ffmpeg that yt-dlp would start)
//   xgs-test-child fakeffmpeg [args...]    plays ffmpeg's tagging pass: writes "fake tagged audio" to the last argument's path
//                                          and exits with 0 (5 when it cannot)
#include <windows.h>

#include <cstdlib>
#include <string>

namespace {

void writeAll(HANDLE handle, const std::string& bytes) {
    size_t done = 0;
    while (done < bytes.size()) {
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data() + done, DWORD(bytes.size() - done), &written, nullptr) || written == 0) return;
        done += written;
    }
}

void line(const std::string& text) {
    writeAll(GetStdHandle(STD_OUTPUT_HANDLE), text + "\n");
}

std::string utf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

std::string hexUnits(const wchar_t* text) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (const wchar_t* p = text; *p; ++p) {
        const unsigned unit = unsigned(*p);
        out += digits[(unit >> 12) & 15];
        out += digits[(unit >> 8) & 15];
        out += digits[(unit >> 4) & 15];
        out += digits[unit & 15];
    }
    return out.empty() ? std::string(".") : out;
}

int hexDigit(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return 0;
}

std::string bytesOf(const wchar_t* hex) {
    std::string out;
    if (!hex || std::wstring(hex) == L".") return out;
    for (const wchar_t* p = hex; p[0] && p[1]; p += 2) out += char(hexDigit(p[0]) * 16 + hexDigit(p[1]));
    return out;
}

DWORD milliseconds(const wchar_t* text) {
    return text ? DWORD(std::wcstoul(text, nullptr, 10)) : 0;
}

// Starts this executable as "sleep <ms>" (inheriting no handle, no console window) and writes that child's process id into
// pidFile (through a temporary file that is renamed). 0, or the exit code to end with.
int startSleeper(const wchar_t* pidFile, const wchar_t* ms) {
    wchar_t self[32768];
    const DWORD length = GetModuleFileNameW(nullptr, self, DWORD(std::size(self)));
    if (length == 0 || length >= std::size(self)) return 10;
    std::wstring commandLine = L"\"" + std::wstring(self) + L"\" sleep " + ms;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(self, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info))
        return 11;
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    const std::wstring temporary = std::wstring(pidFile) + L".tmp";
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 12;
    writeAll(file, std::to_string(info.dwProcessId));
    CloseHandle(file);
    if (!MoveFileExW(temporary.c_str(), pidFile, MOVEFILE_REPLACE_EXISTING)) return 13;
    return 0;
}

int spawn(const wchar_t* pidFile, const wchar_t* ms, bool exitAtOnce) {
    if (const int failed = startSleeper(pidFile, ms)) return failed;
    line("started");
    if (!exitAtOnce) Sleep(milliseconds(ms));
    return 0;
}

// Writes bytes to a new file at path (replacing one that is there). False when it cannot.
bool writeFile(const std::wstring& path, const std::string& bytes) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    writeAll(file, bytes);
    CloseHandle(file);
    return true;
}

// Creates dir and the missing folders above it (yt-dlp creates the folders of its output path).
void makeDirectories(const std::wstring& dir) {
    for (size_t at = dir.find(L'\\'); at != std::wstring::npos; at = dir.find(L'\\', at + 1)) {
        CreateDirectoryW(dir.substr(0, at).c_str(), nullptr);   // fails for a drive or a folder that is there; both are fine
    }
    CreateDirectoryW(dir.c_str(), nullptr);
}

// yt-dlp's reading of an output template, for the fields the engine's download command uses.
std::wstring expandTemplate(const std::wstring& text, const std::wstring& id) {
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'%') {
            out += text[i];
        } else if (text.compare(i, 2, L"%%") == 0) {
            out += L'%';
            i += 1;
        } else if (text.compare(i, 6, L"%(id)s") == 0) {
            out += id;
            i += 5;
        } else if (text.compare(i, 7, L"%(ext)s") == 0) {
            out += L"m4a";
            i += 6;
        } else {
            out += L'%';
        }
    }
    return out;
}

// fakeytdlp after its mode's own arguments: args are yt-dlp's arguments.
int fakeYtDlp(int argc, wchar_t** argv, int first, const wchar_t* ms) {
    std::wstring output;
    for (int i = first; i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == L"-o") output = argv[i + 1];
    }
    if (output.empty()) return 4;
    const size_t slash = output.rfind(L'\\');
    if (slash == std::wstring::npos) return 4;
    std::wstring name = output.substr(slash + 1);
    const std::wstring suffix = L".%(ext)s";
    if (name.size() < suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return 4;
    const std::wstring id = name.substr(0, name.size() - suffix.size());
    const std::wstring path = expandTemplate(output, id);
    makeDirectories(path.substr(0, path.rfind(L'\\')));
    if (!writeFile(path, "fake m4a audio")) return 5;
    line("XGSP|downloading|50|100|NA");
    line("XGSP|finished|100|100|NA");
    Sleep(milliseconds(ms));
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    const std::wstring mode = argv[1];
    if (mode == L"argv") {
        for (int i = 2; i < argc; ++i) line(hexUnits(argv[i]));
        return 0;
    }
    if (mode == L"emit") {
        writeAll(GetStdHandle(STD_OUTPUT_HANDLE), bytesOf(argc > 2 ? argv[2] : nullptr));
        writeAll(GetStdHandle(STD_ERROR_HANDLE), bytesOf(argc > 3 ? argv[3] : nullptr));
        return 3;
    }
    if (mode == L"sleep" || mode == L"sleeperr") {
        if (mode == L"sleep") line("started");
        else writeAll(GetStdHandle(STD_ERROR_HANDLE), "started\n");
        Sleep(milliseconds(argc > 2 ? argv[2] : nullptr));
        return 0;
    }
    if (mode == L"spawn" || mode == L"spawnexit") {
        if (argc < 4) return 2;
        return spawn(argv[2], argv[3], mode == L"spawnexit");
    }
    if (mode == L"sleepshow") {
        const auto handle = reinterpret_cast<unsigned long long>(GetStdHandle(STD_OUTPUT_HANDLE));
        line("started " + std::to_string(GetCurrentProcessId()) + " " + std::to_string(handle));
        Sleep(milliseconds(argc > 2 ? argv[2] : nullptr));
        return 0;
    }
    if (mode == L"env") {
        if (argc < 3) return 2;
        const DWORD size = GetEnvironmentVariableW(argv[2], nullptr, 0);
        if (size == 0) {
            line("(unset)");
            return 0;
        }
        std::wstring value(size, L'\0');
        const DWORD length = GetEnvironmentVariableW(argv[2], value.data(), size);
        value.resize(length);
        line(utf8(value));
        return 0;
    }
    if (mode == L"stdin") {
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        unsigned long long total = 0;
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(input, buffer, sizeof(buffer), &read, nullptr) && read > 0) total += read;
        line(std::to_string(total));
        return 0;
    }
    if (mode == L"fakeytdlp") {
        if (argc < 3) return 2;
        return fakeYtDlp(argc, argv, 3, argv[2]);
    }
    if (mode == L"fakeytdlpspawn") {
        if (argc < 4) return 2;
        if (const int failed = startSleeper(argv[2], argv[3])) return failed;
        return fakeYtDlp(argc, argv, 4, argv[3]);
    }
    if (mode == L"fakeffmpeg") {
        if (argc < 3) return 2;
        return writeFile(argv[argc - 1], "fake tagged audio") ? 0 : 5;
    }
    if (mode == L"console") {
        const HWND window = GetConsoleWindow();
        line(window == nullptr ? "none" : IsWindowVisible(window) ? "visible" : "hidden");
        return 0;
    }
    return 2;
}
