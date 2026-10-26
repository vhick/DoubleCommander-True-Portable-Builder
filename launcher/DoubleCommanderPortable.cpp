// Double Commander True Portable Launcher v1.1
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
#include <cstdint>
#include <optional>
#include <initializer_list>

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

// Double Commander v1.1 managed plugin/custom-resource overlay.
// Portable data is authoritative. No existing user file is intentionally overwritten
// except an untouched bundled file when its official original changes.
static bool IsReparsePoint(const fs::path& path) {
    DWORD flags = GetFileAttributesW(path.c_str());
    return flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

static bool ExistsPath(const fs::path& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static void LogIssue(const fs::path& log, const std::wstring& message) {
    WriteLog(log, L"OVERLAY: " + message);
}

// Non-cryptographic file fingerprint, used ONLY to recognize untouched bundled files.
// Official downloaded archive has a separate, pinned SHA-256 verification.
static bool Fingerprint(const fs::path& file, std::wstring& hash) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    unsigned long long state = 14695981039346656037ULL;
    char buf[65536];
    while (in) {
        in.read(buf, sizeof(buf));
        const std::streamsize count = in.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            state ^= static_cast<unsigned char>(buf[i]);
            state *= 1099511628211ULL;
        }
    }
    if (in.bad()) return false;
    wchar_t out[20]{};
    swprintf_s(out, L"%016llx", state);
    hash = out;
    return true;
}

static bool ReadHash(const fs::path& path, std::wstring& hash) {
    std::wifstream in(path);
    if (!in) return false;
    std::getline(in, hash);
    return hash.size() == 16;
}

static bool WriteHash(const fs::path& path, const std::wstring& hash) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;
    const fs::path temp = path.wstring() + L".new";
    {
        std::wofstream out(temp, std::ios::trunc);
        if (!out) return false;
        out << hash << L"\n";
        if (!out.good()) return false;
    }
    // A crashed update must not produce a partial canonical hash file.
    if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    fs::remove(temp, ec);
    return false;
}

static bool CopyTreeMissing(const fs::path& source, const fs::path& dest,
                            const fs::path& log) {
    if (!ExistsPath(source)) return true;
    if (IsReparsePoint(source)) {
        LogIssue(log, L"Refusing to traverse external reparse point: " + source.wstring());
        return false;
    }
    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) return false;
    fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) return false;
    for (; it != end; it.increment(ec)) {
        if (ec) return false;
        const auto relative = it->path().lexically_relative(source);
        const auto target = dest / relative;
        if (it->is_symlink(ec) || IsReparsePoint(it->path())) {
            LogIssue(log, L"Skipping untrusted link while importing: " + it->path().wstring());
            it.disable_recursion_pending();
            ec.clear();
            continue;
        }
        if (it->is_directory(ec)) {
            fs::create_directories(target, ec);
            if (ec) return false;
        } else if (it->is_regular_file(ec)) {
            fs::create_directories(target.parent_path(), ec);
            if (ec) return false;
            if (!ExistsPath(target)) {
                fs::copy_file(it->path(), target, fs::copy_options::none, ec);
                if (ec) return false;
            } else {
                std::wstring oldhash, newhash;
                if (Fingerprint(it->path(), oldhash) && Fingerprint(target, newhash) && oldhash != newhash)
                    LogIssue(log, L"Preserved existing Data file during import: " + target.wstring());
            }
        }
        if (ec) return false;
    }
    return !ec;
}

static bool CopyTreeOverwriting(const fs::path& source, const fs::path& dest, const fs::path& log) {
    if (!ExistsPath(source) || IsReparsePoint(source)) return false;
    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) return false;
    fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) return false;
    for (; it != end; it.increment(ec)) {
        if (ec) return false;
        if (it->is_symlink(ec) || IsReparsePoint(it->path())) {
            it.disable_recursion_pending(); ec.clear(); continue;
        }
        const fs::path target = dest / it->path().lexically_relative(source);
        if (it->is_directory(ec)) fs::create_directories(target, ec);
        else if (it->is_regular_file(ec)) {
            fs::create_directories(target.parent_path(), ec);
            if (!ec) fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            LogIssue(log, L"Unable to mirror: " + target.wstring());
            return false;
        }
    }
    return true;
}

static bool SyncOfficialFiles(const fs::path& bundled, const fs::path& data,
                              const fs::path& state, const fs::path& log) {
    if (!ExistsPath(bundled)) return true;
    if (IsReparsePoint(bundled)) return false;
    std::error_code ec;
    fs::recursive_directory_iterator it(bundled, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) return false;
    for (; it != end; it.increment(ec)) {
        if (ec) return false;
        if (it->is_symlink(ec) || IsReparsePoint(it->path())) {
            it.disable_recursion_pending();
            LogIssue(log, L"Skipped bundled reparse point: " + it->path().wstring());
            ec.clear();
            continue;
        }
        const fs::path relative = it->path().lexically_relative(bundled);
        const fs::path target = data / relative;
        if (it->is_directory(ec)) {
            fs::create_directories(target, ec);
            if (ec) return false;
            continue;
        }
        if (!it->is_regular_file(ec)) { ec.clear(); continue; }
        std::wstring shippedHash;
        if (!Fingerprint(it->path(), shippedHash)) return false;
        const fs::path recorded = state / fs::path(relative.wstring() + L".hash");
        std::wstring previousHash, currentHash;
        const bool hasPrevious = ReadHash(recorded, previousHash);
        const bool hasCurrent = Fingerprint(target, currentHash);
        bool copied = false;
        if (!ExistsPath(target)) {
            fs::create_directories(target.parent_path(), ec);
            if (ec) return false;
            copied = fs::copy_file(it->path(), target, fs::copy_options::none, ec);
            if (ec || !copied) return false;
        } else if (!hasCurrent) {
            LogIssue(log, L"Cannot read existing user file; refusing overwrite: " + target.wstring());
            continue;
        } else if (currentHash != shippedHash && hasPrevious && previousHash == currentHash) {
            // Upgrade an unmodified official plugin or icon. Never overwrite customized files.
            copied = fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
            if (ec || !copied) return false;
            LogIssue(log, L"Updated unchanged bundled file: " + target.wstring());
        } else if (currentHash != shippedHash) {
            LogIssue(log, L"Preserved user-modified or third-party file: " + target.wstring());
            continue;
        }
        if ((copied || currentHash == shippedHash) && !WriteHash(recorded, shippedHash)) return false;
    }
    return !ec;
}

static bool MakeDirectoryLink(const fs::path& link, const fs::path& data) {
    // Developer mode permits an unprivileged symlink; use native flag first.
    if (CreateSymbolicLinkW(link.c_str(), data.c_str(),
                            SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
        return true;
    const std::wstring comspec = GetEnv(L"ComSpec");
    if (comspec.empty()) return false;
    std::wstring cmd = WinQuote(comspec) + L" /d /s /c \"mklink /J " +
                       WinQuote(link.wstring()) + L" " + WinQuote(data.wstring()) + L" >nul 2>nul\"";
    std::vector<wchar_t> writable(cmd.begin(), cmd.end());
    writable.push_back(L'\0');
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(comspec.c_str(), writable.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, link.parent_path().c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    std::error_code ec;
    return exitCode == 0 && fs::equivalent(link, data, ec) && !ec;
}

struct ManagedFolder {
    std::wstring name;
    fs::path app;
    fs::path data;
    bool fallback = false;
};

static bool PrepareManagedFolder(const fs::path& root, const fs::path& log,
                                 const std::wstring& name, ManagedFolder& entry) {
    const fs::path program = root / L"App" / L"DoubleCommander";
    const fs::path dataRoot = root / L"Data";
    entry.name = name;
    entry.app = program / name;
    entry.data = dataRoot / (name == L"pixmaps" ? L"CustomIcons" :
                             name == L"highlighters" ? L"Highlighters" :
                             name == L"plugins" ? L"Plugins" :
                             name == L"scripts" ? L"Scripts" : L"Cache");
    const fs::path marker = dataRoot / L"State" / L"ManagedLinks" / (name + L".txt");
    const fs::path fallbackMarker = dataRoot / L"State" / L"FallbackLinks" / (name + L".txt");
    const bool priorFallback = ExistsPath(fallbackMarker);
    std::error_code ec;
    fs::create_directories(entry.data, ec);
    if (ec) return false;

    if (IsReparsePoint(entry.app)) {
        if (fs::equivalent(entry.app, entry.data, ec) && !ec) {
            // Already pointed into this portable root.
        } else if (ExistsPath(marker)) {
            // Our own link was left pointing to an older drive/location.
            if (!RemoveDirectoryW(entry.app.c_str())) return false;
            LogIssue(log, L"Repaired old managed folder link: " + entry.app.wstring());
        } else {
            LogIssue(log, L"Unrecognized existing link, preserving: " + entry.app.wstring());
            return false;
        }
    } else if (ExistsPath(entry.app) && priorFallback) {
        // App is a real directory on a drive that cannot create junctions. Recover
        // plugin files added during an abnormal exit before using Data again.
        if (!CopyTreeMissing(entry.app, entry.data, log)) return false;
        entry.fallback = true;
    } else if (ExistsPath(entry.app)) {
        // V1.0 might contain third-party plugins here. Archive, never delete them.
        if (!CopyTreeMissing(entry.app, entry.data, log)) return false;
        const fs::path legacy = dataRoot / L"LegacyAppFolders";
        fs::create_directories(legacy, ec);
        if (ec) return false;
        SYSTEMTIME st{}; GetLocalTime(&st);
        wchar_t suffix[80]{};
        swprintf_s(suffix, L"-%04u%02u%02u-%02u%02u%02u-%lu", st.wYear, st.wMonth,
                   st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
        const fs::path moved = legacy / (name + suffix);
        fs::rename(entry.app, moved, ec);
        if (ec) return false; // Never delete the existing folder on failure.
        LogIssue(log, L"Archived former App folder: " + moved.wstring());
    }

    if (!SyncOfficialFiles(program / L"_BundledAssets" / name, entry.data,
                           dataRoot / L"State" / L"BundledHashes" / name, log)) return false;
    if (entry.fallback) {
        if (!CopyTreeOverwriting(entry.data, entry.app, log)) return false;
        return true;
    }
    if (fs::equivalent(entry.app, entry.data, ec) && !ec) return true;
    ec.clear();
    if (MakeDirectoryLink(entry.app, entry.data)) {
        fs::create_directories(marker.parent_path(), ec);
        if (ec) return false;
        std::wofstream m(marker, std::ios::trunc);
        if (!m) return false;
        m << L"DoubleCommanderPortable managed v1.1\n";
        LogIssue(log, L"Linked " + entry.app.wstring() + L" -> " + entry.data.wstring());
        return true;
    }

    // exFAT/FAT and some network drives cannot hold junctions/symlinks.
    // Keep the authoritative copy under Data; mirror for the duration of this session.
    entry.fallback = true;
    fs::create_directories(fallbackMarker.parent_path(), ec);
    if (ec) return false;
    { std::wofstream stateFile(fallbackMarker, std::ios::trunc);
      if (!stateFile) return false;
      stateFile << L"DoubleCommanderPortable copy fallback v1.1\n";
    }
    if (!CopyTreeOverwriting(entry.data, entry.app, log)) return false;
    LogIssue(log, L"Fallback copy mode (not crash-proof): " + name);
    return true;
}

static void FinishManagedFolder(const ManagedFolder& entry, const fs::path& log) {
    if (!entry.fallback) return;
    if (!CopyTreeOverwriting(entry.app, entry.data, log))
        LogIssue(log, L"WARNING: Fallback sync failed; see retained App folder: " + entry.app.wstring());
    else
        LogIssue(log, L"Fallback folder synchronized to Data: " + entry.name);
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

    // Redirect app-installed plugins, icons, themes and scripts into persistent Data.
    // This also imports pre-existing v1.0 App-side additions without overwriting Data.
    std::vector<ManagedFolder> managed;
    for (const wchar_t* folder : {L"plugins", L"pixmaps", L"highlighters", L"scripts", L"cache"}) {
        ManagedFolder item;
        if (!PrepareManagedFolder(root, log, folder, item)) {
            WriteLog(log, L"ERROR: cannot safely prepare managed directory: " + std::wstring(folder));
            MessageBoxW(nullptr,
                L"Could not safely prepare portable plugins/customizations.\n"
                L"No existing files were intentionally deleted. See Data\TraceGuard\TraceGuard.log.",
                L"Double Commander Portable", MB_OK | MB_ICONERROR);
            return 14;
        }
        managed.push_back(item);
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

    for (const auto& folder : managed) FinishManagedFolder(folder, log);
    InspectAfter(log, roots, before);
    for (size_t i = 0; i < registryKeys.size(); ++i) {
        if (!registryBefore[i] && RegExists(registryKeys[i].c_str())) {
            WriteLog(log, L"REPORT ONLY new application-named registry key: HKCU\\" + registryKeys[i]);
        }
    }
    WriteLog(log, L"TraceGuard finished: pre-existing data preserved; shared Windows history is not cleaned.");
    return static_cast<int>(exitCode);
}
