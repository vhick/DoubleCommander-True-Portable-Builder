// Double Commander True Portable Launcher v1.0
// Licensed MIT. Double Commander itself is licensed separately (GPL).
// Uses the application's documented --config-dir option; never edits global configuration.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <system_error>

namespace fs = std::filesystem;

static std::wstring GetEnv(const wchar_t* name) {
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size < 2) return L"";
    std::wstring value(size, L'\0');
    DWORD copied = GetEnvironmentVariableW(name, &value[0], size);
    if (copied == 0 || copied >= size) return L"";
    value.resize(copied);
    return value;
}

static std::wstring WinQuote(const std::wstring& arg) {
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos && !arg.empty()) return arg;
    std::wstring result = L"\"";
    unsigned int slashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(ch);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

static void WriteLog(const fs::path& file, const std::wstring& value) {
    try {
        std::wofstream stream(file, std::ios::app);
        if (!stream) return;
        SYSTEMTIME st{}; GetLocalTime(&st);
        wchar_t timebuf[80];
        swprintf_s(timebuf, L"%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth,
                   st.wDay, st.wHour, st.wMinute, st.wSecond);
        stream << timebuf << L" | " << value << L"\n";
    } catch (...) { /* Logging must not prevent file manager operation. */ }
}

static std::vector<fs::path> WatchedRoots() {
    std::vector<fs::path> out;
    const auto roaming = GetEnv(L"APPDATA");
    const auto local = GetEnv(L"LOCALAPPDATA");
    const auto home = GetEnv(L"USERPROFILE");
    if (!roaming.empty()) out.push_back(fs::path(roaming) / L"doublecmd");
    if (!local.empty()) out.push_back(fs::path(local) / L"doublecmd");
    if (!home.empty()) out.push_back(fs::path(home) / L".doublecmd");
    return out;
}

struct Snapshot {
    bool rootExisted = false;
    std::set<fs::path> files;
    std::set<fs::path> dirs;
};

static Snapshot TakeSnapshot(const fs::path& root) {
    Snapshot s;
    std::error_code ec;
    s.rootExisted = fs::is_directory(root, ec);
    if (!s.rootExisted) return s;
    s.dirs.insert(root);
    auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
    auto end = fs::recursive_directory_iterator();
    for (; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        std::error_code typeError;
        if (it->is_symlink(typeError)) {
            s.files.insert(p); // Never follow or delete links.
        } else if (it->is_directory(typeError)) {
            s.dirs.insert(p);
        } else {
            s.files.insert(p);
        }
    }
    return s;
}

static bool RegExists(const wchar_t* key) {
    HKEY handle = nullptr;
    LONG status = RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &handle);
    if (status != ERROR_SUCCESS) return false;
    RegCloseKey(handle);
    return true;
}

static void InspectAfter(const fs::path& log, const std::vector<fs::path>& roots,
                         const std::vector<Snapshot>& before) {
    for (size_t i = 0; i < roots.size(); ++i) {
        const auto after = TakeSnapshot(roots[i]);
        size_t newFiles = 0, newDirs = 0, cleaned = 0;
        for (const auto& p : after.files) {
            if (!before[i].files.count(p)) {
                ++newFiles;
                WriteLog(log, L"REPORT ONLY new app-specific file: " + p.wstring());
            }
        }
        std::vector<fs::path> dirs;
        for (const auto& p : after.dirs) {
            if (!before[i].dirs.count(p)) {
                ++newDirs;
                dirs.push_back(p);
            }
        }
        std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
            return a.native().size() > b.native().size();
        });
        for (const auto& p : dirs) {
            std::error_code ec;
            // Do not modify anything under a pre-existing host installation.
            // Only remove newly created EMPTY directories within an initially absent app-specific root.
            if (!before[i].rootExisted && fs::is_directory(p, ec) && fs::is_empty(p, ec)) {
                if (fs::remove(p, ec) && !ec) {
                    ++cleaned;
                    WriteLog(log, L"CLEANED new empty app-specific directory: " + p.wstring());
                    continue;
                }
            }
            WriteLog(log, L"PRESERVED directory (nonempty or pre-existing root): " + p.wstring());
        }
        if (newFiles || newDirs) {
            WriteLog(log, L"Host changes for " + roots[i].wstring() + L": " +
                         std::to_wstring(newFiles) + L" files, " + std::to_wstring(newDirs) +
                         L" directories, " + std::to_wstring(cleaned) + L" empty directories cleaned");
        }
    }
}

static std::wstring Lower(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(towlower(ch));
    return text;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::vector<wchar_t> exePath(32768, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, exePath.data(), static_cast<DWORD>(exePath.size()));
    if (len == 0 || len >= exePath.size()) return 10;

    const fs::path root = fs::path(std::wstring(exePath.data(), len)).parent_path();
    const fs::path target = root / L"App" / L"DoubleCommander" / L"doublecmd.exe";
    const fs::path config = root / L"Data" / L"Config";
    const fs::path trace = root / L"Data" / L"TraceGuard";
    std::error_code ec;
    fs::create_directories(config, ec);
    if (ec) {
        MessageBoxW(nullptr, L"Cannot create the portable Data\\Config folder. Check write permissions.",
                    L"Double Commander Portable", MB_OK | MB_ICONERROR);
        return 11;
    }
    fs::create_directories(trace, ec);
    const fs::path log = trace / L"TraceGuard.log";

    if (!fs::exists(target)) {
        WriteLog(log, L"ERROR: missing " + target.wstring());
        MessageBoxW(nullptr, L"Double Commander executable is missing under App\\DoubleCommander.",
                    L"Double Commander Portable", MB_OK | MB_ICONERROR);
        return 12;
    }

    const auto roots = WatchedRoots();
    std::vector<Snapshot> before;
    for (const auto& r : roots) before.push_back(TakeSnapshot(r));
    const std::vector<std::wstring> registryKeys = {
        L"Software\\Double Commander", L"Software\\DoubleCommander", L"Software\\DoubleCmd", L"Software\\doublecmd",
        L"Software\\Classes\\Applications\\doublecmd.exe"
    };
    std::vector<bool> registryBefore;
    for (const auto& k : registryKeys) registryBefore.push_back(RegExists(k.c_str()));

    std::wstring cmd = WinQuote(target.wstring()) + L" " + WinQuote(L"--config-dir=" + config.wstring());
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg(argv[i]);
            // Don't allow forwarded arguments to override the portable data location.
            const auto lower = Lower(arg);
            if (lower.rfind(L"--config-dir", 0) == 0) {
                WriteLog(log, L"Ignored overriding --config-dir command-line argument");
                continue;
            }
            cmd += L" " + WinQuote(arg);
        }
        LocalFree(argv);
    }

    WriteLog(log, L"===== Portable launch =====");
    WriteLog(log, L"Root: " + root.wstring());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdMutable(cmd.begin(), cmd.end());
    cmdMutable.push_back(L'\0');
    BOOL started = CreateProcessW(target.c_str(), cmdMutable.data(), nullptr, nullptr,
                                  FALSE, 0, nullptr, target.parent_path().c_str(), &si, &pi);
    if (!started) {
        const DWORD error = GetLastError();
        WriteLog(log, L"ERROR: CreateProcess failed with code " + std::to_wstring(error));
        MessageBoxW(nullptr, L"Double Commander could not be started. Check the TraceGuard log.",
                    L"Double Commander Portable", MB_OK | MB_ICONERROR);
        return 13;
    }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    WriteLog(log, L"Double Commander exited; exit code=" + std::to_wstring(exitCode));

    InspectAfter(log, roots, before);
    for (size_t i = 0; i < registryKeys.size(); ++i) {
        if (!registryBefore[i] && RegExists(registryKeys[i].c_str())) {
            WriteLog(log, L"REPORT ONLY new application-named registry key: HKCU\\" + registryKeys[i]);
        }
    }
    WriteLog(log, L"TraceGuard finished: pre-existing data preserved; shared Windows history is not cleaned.");
    return static_cast<int>(exitCode);
}
