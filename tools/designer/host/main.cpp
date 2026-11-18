// HML Designer host: a native window with the OS web view (WebView2 on Windows, WebKitGTK on
// Linux) showing renderer/index.html, plus the few native services the page needs:
// file dialogs, reading / writing the project files, running hml_runtime for previews,
// deploying a project to a target over SSH.
//
// Every page -> host call is a function bound as window.hml_*(...) returning a Promise.
// Arguments arrive as a JSON array, results are JSON objects.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include "webview/webview.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include <gtk/gtk.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- strings / paths
#ifdef _WIN32
std::wstring widen(const std::string& s)
{
    if(s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w)
{
    if(w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
#endif

fs::path upath(const std::string& utf8) { return fs::u8path(utf8); }
std::string ustr(const fs::path& p) { return p.generic_u8string(); }

std::string read_text(const fs::path& p, bool* ok)
{
    std::ifstream f(p, std::ios::binary);
    if(!f) { if(ok) *ok = false; return {}; }
    std::stringstream ss;
    ss << f.rdbuf();
    if(ok) *ok = true;
    return ss.str();
}

bool write_text(const fs::path& p, const std::string& text)
{
    std::error_code ec;
    if(p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if(!f) return false;
    f << text;
    return f.good();
}

fs::path exe_dir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
#endif
}

fs::path config_dir()
{
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
    fs::path p = base ? upath(base) : exe_dir();
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    fs::path p = xdg ? fs::path(xdg) : (home ? fs::path(home) / ".config" : fs::temp_directory_path());
#endif
    p /= "hml_designer";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

std::string base64(const std::string& in)
{
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    while(i + 2 < in.size()) {
        uint32_t v = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8) | static_cast<uint8_t>(in[i + 2]);
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63]; out += tbl[(v >> 6) & 63]; out += tbl[v & 63];
        i += 3;
    }
    if(i < in.size()) {
        uint32_t v = static_cast<uint8_t>(in[i]) << 16;
        if(i + 1 < in.size()) v |= static_cast<uint8_t>(in[i + 1]) << 8;
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63];
        out += (i + 1 < in.size()) ? tbl[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

// ---------------------------------------------------------------- processes
struct ProcResult {
    int code = -1;
    bool timed_out = false;
    std::string output, error;
};

#ifdef _WIN32
std::wstring quote_arg(const std::string& a)
{
    std::wstring w = widen(a), out = L"\"";
    for(wchar_t c : w) { if(c == L'"') out += L"\\\""; else out += c; }
    return out + L"\"";
}
#endif

// Run a program, capture stdout+stderr, kill it after timeout_ms. Extra environment variables
// are set in our own environment for the duration of the call (the child inherits them).
ProcResult run_capture(const fs::path& exe, const std::vector<std::string>& args,
                       const std::vector<std::pair<std::string, std::string>>& env, const fs::path& cwd, int timeout_ms)
{
    ProcResult r;
#ifdef _WIN32
    for(const auto& kv : env) SetEnvironmentVariableW(widen(kv.first).c_str(), widen(kv.second).c_str());
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if(!CreatePipe(&rd, &wr, &sa, 0)) { r.error = "CreatePipe failed"; return r; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = quote_arg(ustr(exe));
    for(const auto& a : args) cmd += L" " + quote_arg(a);
    std::wstring wcwd = cwd.empty() ? L"" : cwd.wstring();
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    CloseHandle(wr);
    for(const auto& kv : env) SetEnvironmentVariableW(widen(kv.first).c_str(), nullptr);
    if(!ok) { CloseHandle(rd); r.error = "cannot start " + ustr(exe) + " (error " + std::to_string(GetLastError()) + ")"; return r; }
    std::atomic<bool> done{ false };
    std::thread watchdog([&] {
        if(WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout_ms)) == WAIT_TIMEOUT && !done) { r.timed_out = true; TerminateProcess(pi.hProcess, 9); }
    });
    char buf[4096];
    DWORD n = 0;
    while(ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0) r.output.append(buf, n);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    done = true;
    watchdog.join();
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    r.code = static_cast<int>(code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    for(const auto& kv : env) setenv(kv.first.c_str(), kv.second.c_str(), 1);
    int fds[2];
    if(pipe(fds) != 0) { r.error = "pipe failed"; return r; }
    std::vector<std::string> argv_s;
    argv_s.push_back(ustr(exe));
    for(const auto& a : args) argv_s.push_back(a);
    std::vector<char*> argv;
    for(auto& s : argv_s) argv.push_back(s.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    if(!cwd.empty()) posix_spawn_file_actions_addchdir_np(&fa, cwd.c_str());
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    for(const auto& kv : env) unsetenv(kv.first.c_str());
    if(rc != 0) { close(fds[0]); r.error = "cannot start " + ustr(exe) + ": " + std::strerror(rc); return r; }
    std::atomic<bool> done{ false };
    std::thread watchdog([&] {
        for(int t = 0; t < timeout_ms && !done; t += 50) usleep(50000);
        if(!done) { r.timed_out = true; kill(pid, SIGKILL); }
    });
    char buf[4096];
    ssize_t n;
    while((n = read(fds[0], buf, sizeof(buf))) > 0) r.output.append(buf, static_cast<size_t>(n));
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    done = true;
    watchdog.join();
    r.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    return r;
}

bool launch_detached(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd, std::string* err)
{
#ifdef _WIN32
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = quote_arg(ustr(exe));
    for(const auto& a : args) cmd += L" " + quote_arg(a);
    std::wstring wcwd = cwd.wstring();
    if(!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi)) {
        if(err) *err = "cannot start " + ustr(exe) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
#else
    pid_t pid = fork();
    if(pid < 0) { if(err) *err = "fork failed"; return false; }
    if(pid == 0) {
        setsid();
        if(!cwd.empty()) { if(chdir(cwd.c_str()) != 0) _exit(127); }
        std::vector<std::string> argv_s;
        argv_s.push_back(ustr(exe));
        for(const auto& a : args) argv_s.push_back(a);
        std::vector<char*> argv;
        for(auto& s : argv_s) argv.push_back(s.data());
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    return true;
#endif
}

// ---------------------------------------------------------------- deploy helpers
// Minimal tar (ustar) writer: the project folder is packed here, so that deploying needs
// nothing but ssh / scp on this machine and tar on the target.
struct TarWriter {
    std::ofstream f;
    int files = 0;
    uintmax_t bytes = 0;
    std::vector<std::string> skipped;

    explicit TarWriter(const fs::path& p) : f(p, std::ios::binary | std::ios::trunc) {}

    static void octal(char* dst, size_t len, uintmax_t v) { std::snprintf(dst, len, "%0*llo", static_cast<int>(len - 1), static_cast<unsigned long long>(v)); }

    // name: path inside the archive, '/' separated
    bool add_file(const fs::path& src, const std::string& name)
    {
        std::string base = name, prefix;
        if(base.size() > 100) {                              // split at a '/' into prefix (<=155) + name (<=100)
            size_t cut = std::string::npos;
            for(size_t i = 0; i < name.size(); i++) if(name[i] == '/' && i <= 155 && name.size() - i - 1 <= 100) cut = i;
            if(cut == std::string::npos) { skipped.push_back(name); return true; }
            prefix = name.substr(0, cut);
            base = name.substr(cut + 1);
        }
        std::ifstream in(src, std::ios::binary);
        if(!in) return false;
        std::error_code ec;
        uintmax_t size = fs::file_size(src, ec);
        if(ec) return false;
        char h[512] = {};
        std::memcpy(h, base.data(), base.size());
        octal(h + 100, 8, 0644);
        octal(h + 108, 8, 0);
        octal(h + 116, 8, 0);
        octal(h + 124, 12, size);
        octal(h + 136, 12, static_cast<uintmax_t>(std::time(nullptr)));
        std::memset(h + 148, ' ', 8);
        h[156] = '0';
        std::memcpy(h + 257, "ustar", 6);
        h[263] = '0'; h[264] = '0';
        std::memcpy(h + 345, prefix.data(), prefix.size());
        unsigned sum = 0;
        for(unsigned char c : h) sum += c;
        std::snprintf(h + 148, 8, "%06o", sum);
        h[155] = ' ';
        f.write(h, sizeof(h));
        char buf[65536];
        uintmax_t left = size;
        while(left > 0) {
            in.read(buf, static_cast<std::streamsize>(std::min<uintmax_t>(left, sizeof(buf))));
            std::streamsize n = in.gcount();
            if(n <= 0) return false;                         // the file shrank while it was read
            f.write(buf, n);
            left -= static_cast<uintmax_t>(n);
        }
        static const char zero[512] = {};
        if(size % 512) f.write(zero, static_cast<std::streamsize>(512 - size % 512));
        files++;
        bytes += size;
        return f.good();
    }

    bool finish()
    {
        static const char zero[1024] = {};
        f.write(zero, sizeof(zero));
        f.close();
        return !f.fail();
    }
};

// Only these characters are passed to the remote shell (no quoting needed, nothing to inject).
bool safe_token(const std::string& s, const char* extra)
{
    if(s.empty()) return false;
    for(unsigned char c : s) if(!(std::isalnum(c) || std::strchr(extra, c))) return false;
    return true;
}

// ssh / scp: on Windows the OpenSSH client that ships with the OS (not whatever PATH has first)
fs::path ssh_tool(const char* name)
{
#ifdef _WIN32
    wchar_t sys[MAX_PATH] = {};
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    std::error_code ec;
    fs::path p = fs::path(std::wstring(sys, n)) / "OpenSSH" / (std::string(name) + ".exe");
    if(fs::exists(p, ec)) return p;
    return std::string(name) + ".exe";
#else
    return name;
#endif
}

std::string trimmed(std::string s)
{
    while(!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while(i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    return s.substr(i);
}

// ---------------------------------------------------------------- file dialogs
// pattern: "*.json" ; returns "" when cancelled
std::string open_file_dialog(void* parent, const std::string& title, const std::string& desc, const std::string& pattern, const std::string& initial_dir)
{
#ifdef _WIN32
    wchar_t buf[4096] = {};
    std::wstring filter = widen(desc) + L'\0' + widen(pattern) + L'\0' + L"All files" + L'\0' + L"*.*" + L'\0' + L'\0';
    std::wstring wtitle = widen(title), wdir = widen(initial_dir);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(parent);
    ofn.lpstrFile = buf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buf));
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrTitle = wtitle.c_str();
    ofn.lpstrInitialDir = wdir.empty() ? nullptr : wdir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if(!GetOpenFileNameW(&ofn)) return {};
    return narrow(buf);
#else
    GtkWidget* dlg = gtk_file_chooser_dialog_new(title.c_str(), GTK_WINDOW(parent), GTK_FILE_CHOOSER_ACTION_OPEN,
                                                 "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, desc.c_str());
    gtk_file_filter_add_pattern(f, pattern.c_str());
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);
    if(!initial_dir.empty()) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), initial_dir.c_str());
    std::string out;
    if(gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if(p) { out = p; g_free(p); }
    }
    gtk_widget_destroy(dlg);
    return out;
#endif
}

std::string save_file_dialog(void* parent, const std::string& title, const std::string& default_name, const std::string& initial_dir)
{
#ifdef _WIN32
    wchar_t buf[4096] = {};
    std::wstring wname = widen(default_name);
    wcsncpy(buf, wname.c_str(), std::size(buf) - 1);
    std::wstring filter = std::wstring(L"JSON") + L'\0' + L"*.json" + L'\0' + L"All files" + L'\0' + L"*.*" + L'\0' + L'\0';
    std::wstring wtitle = widen(title), wdir = widen(initial_dir);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(parent);
    ofn.lpstrFile = buf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buf));
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrTitle = wtitle.c_str();
    ofn.lpstrDefExt = L"json";
    ofn.lpstrInitialDir = wdir.empty() ? nullptr : wdir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if(!GetSaveFileNameW(&ofn)) return {};
    return narrow(buf);
#else
    GtkWidget* dlg = gtk_file_chooser_dialog_new(title.c_str(), GTK_WINDOW(parent), GTK_FILE_CHOOSER_ACTION_SAVE,
                                                 "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), default_name.c_str());
    if(!initial_dir.empty()) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), initial_dir.c_str());
    std::string out;
    if(gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if(p) { out = p; g_free(p); }
    }
    gtk_widget_destroy(dlg);
    return out;
#endif
}

// ---------------------------------------------------------------- the host
struct Host {
    webview::webview* w = nullptr;
    fs::path exe, renderer, runtime, settings_path;
    std::string argv_project;
    json settings = json::object();
    int preview_seq = 0;

    void load_settings()
    {
        bool ok = false;
        std::string t = read_text(settings_path, &ok);
        if(ok) { json j = json::parse(t, nullptr, false, true); if(j.is_object()) settings = j; }
    }
    void save_settings() { write_text(settings_path, settings.dump(2) + "\n"); }

    void find_runtime()
    {
        std::error_code ec;
#ifdef _WIN32
        const char* name = "hml_runtime.exe";
#else
        const char* name = "hml_runtime";
#endif
        if(fs::exists(exe / name, ec)) { runtime = exe / name; return; }
        if(settings.contains("runtime") && settings["runtime"].is_string()) {
            fs::path p = upath(settings["runtime"].get<std::string>());
            if(fs::exists(p, ec)) { runtime = p; return; }
        }
        runtime.clear();
    }

    void* window() { auto r = w->window(); return r.ok() ? r.value() : nullptr; }

    static json err(const std::string& msg) { return json{ { "ok", false }, { "error", msg } }; }

    json info()
    {
        return json{
            { "platform",
#ifdef _WIN32
              "windows"
#else
              "linux"
#endif
            },
            { "exeDir", ustr(exe) }, { "rendererDir", ustr(renderer) },
            { "runtime", runtime.empty() ? json(nullptr) : json(ustr(runtime)) },
            { "argvProject", argv_project }, { "version", HML_DESIGNER_VERSION },
            { "targetRuntimes", target_runtimes() },
        };
    }

    // Linux builds of hml_runtime in the source tree this designer was built from
    // (<root>/build-*/bin/hml_runtime), offered in the Deploy dialog.
    json target_runtimes()
    {
        json out = json::array();
        std::error_code ec;
        for(const fs::path& root : { exe.parent_path().parent_path(), exe.parent_path() }) {
            if(root.empty() || !fs::is_directory(root, ec)) continue;
            for(const auto& e : fs::directory_iterator(root, ec)) {
                if(!e.is_directory(ec) || ustr(e.path().filename()).rfind("build-", 0) != 0) continue;
                fs::path rt = e.path() / "bin" / "hml_runtime";
                if(fs::is_regular_file(rt, ec)) out.push_back(ustr(rt));
            }
            if(!out.empty()) break;
        }
        return out;
    }

    json read_project(const std::string& path)
    {
        fs::path p = upath(path);
        bool ok = false;
        std::string text = read_text(p, &ok);
        if(!ok) return err("cannot read " + path);
        json proj = json::parse(text, nullptr, false, true);
        if(proj.is_discarded() || !proj.is_object()) return err("project.json is not valid JSON");
        fs::path dir = p.parent_path();
        json screens = json::array();
        if(proj.contains("screens") && proj["screens"].is_array()) {
            for(const auto& s : proj["screens"]) {
                if(!s.is_string()) continue;
                bool sok = false;
                std::string st = read_text(dir / upath(s.get<std::string>()), &sok);
                screens.push_back({ { "file", s.get<std::string>() }, { "text", sok ? st : "" } });
            }
        }
        json recent = settings.value("recent", json::array());
        json nr = json::array({ ustr(p) });
        for(const auto& r : recent) if(r.is_string() && r.get<std::string>() != ustr(p) && nr.size() < 10) nr.push_back(r);
        settings["recent"] = nr;
        save_settings();
        return json{ { "ok", true }, { "path", ustr(p) }, { "dir", ustr(dir) }, { "project", text }, { "screens", screens } };
    }

    json write_files(const std::string& dir, const json& files)
    {
        fs::path d = upath(dir);
        std::vector<std::string> failed;
        for(const auto& f : files) {
            if(!f.is_object() || !f.contains("rel") || !f.contains("text")) continue;
            fs::path p = d / upath(f["rel"].get<std::string>());
            if(!write_text(p, f["text"].get<std::string>())) failed.push_back(ustr(p));
        }
        if(!failed.empty()) { std::string m = "cannot write: "; for(const auto& s : failed) m += s + " "; return err(m); }
        return json{ { "ok", true } };
    }

    json pick_image(const std::string& dir)
    {
        std::string p = open_file_dialog(window(), "Choose an image (PNG)", "PNG images", "*.png", dir);
        if(p.empty()) return json{ { "ok", false }, { "error", "cancelled" } };
        fs::path src = upath(p), d = upath(dir);
        std::error_code ec;
        fs::path rel = fs::relative(src, d, ec);
        std::string rels = ec ? "" : ustr(rel);
        if(rels.empty() || rels.rfind("..", 0) == 0 || rel.is_absolute()) {
            fs::path dst = d / "assets" / src.filename();
            fs::create_directories(dst.parent_path(), ec);
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if(ec) return err("cannot copy into assets/: " + ec.message());
            rels = "assets/" + ustr(src.filename());
        }
        return json{ { "ok", true }, { "rel", rels } };
    }

    // runs on a worker thread; resolves the promise on the UI thread
    void preview_async(const std::string& id, const std::string& project, const std::string& screen)
    {
        fs::path rt = runtime, proj = upath(project);
        int seq = ++preview_seq;
        std::thread([this, id, rt, proj, screen, seq] {
            json res;
            if(rt.empty()) res = err("hml_runtime not found");
            else {
                std::error_code ec;
                fs::path bmp = fs::temp_directory_path(ec) / ("hml_preview_" + std::to_string(seq) + ".bmp");
                fs::remove(bmp, ec);
                std::vector<std::string> args{ ustr(proj), "--exit-after", "900", "--shot-at", "600," + ustr(bmp) };
                if(!screen.empty()) { args.push_back("--screen"); args.push_back(screen); }
                ProcResult pr = run_capture(rt, args, { { "SDL_VIDEODRIVER", "dummy" } }, proj.parent_path(), 15000);
                bool ok = false;
                std::string data = read_text(bmp, &ok);
                fs::remove(bmp, ec);
                if(!pr.error.empty()) res = err(pr.error);
                else if(!ok || data.empty()) res = err(pr.timed_out ? "runtime did not exit (timeout)" : "no screenshot produced (exit code " + std::to_string(pr.code) + ")");
                else res = json{ { "ok", true }, { "image", "data:image/bmp;base64," + base64(data) } };
                res["log"] = pr.output;
            }
            std::string dump = res.dump();
            w->dispatch([this, id, dump] { w->resolve(id, 0, dump); });
        }).detach();
    }

    // Deploy a project folder (and optionally a runtime binary) to a target over SSH and restart
    // the runtime there. Options: project, host, user, port, key, remoteDir, runtime, restart.
    // Layout on the target: <remoteDir>/hml_runtime, <remoteDir>/projects/<project folder>/.
    // Blocking (call it from a worker thread). Returns { ok, log, error? }.
    json deploy(const json& o)
    {
        auto S = [&](const char* k, const char* def = "") {
            auto it = o.find(k);
            return trimmed(it != o.end() && it->is_string() ? it->get<std::string>() : std::string(def));
        };
        std::string log;
        auto fail = [&](const std::string& m) { json r = err(m); r["log"] = log; return r; };
        auto step = [&](const std::string& m) { log += "> " + m + "\n"; };

        std::string hostn = S("host"), user = S("user"), key = S("key"), remote = S("remoteDir", "hml"), rt = S("runtime");
        int port = 22;
        if(auto it = o.find("port"); it != o.end()) {
            if(it->is_number()) port = it->get<int>();
            else if(it->is_string() && !trimmed(it->get<std::string>()).empty()) port = std::atoi(it->get<std::string>().c_str());
        }
        bool restart = !(o.contains("restart") && o["restart"].is_boolean() && !o["restart"].get<bool>());
        fs::path proj = upath(S("project"));
        std::error_code ec;
        if(!fs::is_regular_file(proj, ec)) return fail("project.json not found (save the project first)");
        fs::path dir = proj.parent_path();
        std::string pname = ustr(dir.filename());

        if(remote.rfind("~/", 0) == 0) remote = remote.substr(2);
        while(remote.size() > 1 && remote.back() == '/') remote.pop_back();
        if(remote.empty() || remote == "~") remote = ".";
        if(!safe_token(hostn, ".-:_")) return fail("host: enter the target's address or host name");
        if(!safe_token(user, "._-")) return fail("user: enter the login name on the target");
        if(port < 1 || port > 65535) return fail("port: must be 1..65535");
        if(!safe_token(remote, "._-/") || remote[0] == '-') return fail("remote folder: only letters, digits and . _ - / are allowed");
        if(!safe_token(pname, "._-") || pname[0] == '-' || pname[0] == '.')
            return fail("the project folder name '" + pname + "' cannot be used on the target (only letters, digits and . _ - are allowed)");
        if(!key.empty() && !fs::is_regular_file(upath(key), ec)) return fail("private key file not found: " + key);
        fs::path rtp;
        if(!rt.empty()) {
            rtp = upath(rt);
            std::ifstream f(rtp, std::ios::binary);
            char magic[4] = {};
            f.read(magic, 4);
            if(!f || std::memcmp(magic, "\x7f" "ELF", 4) != 0) return fail("runtime: '" + rt + "' is not a Linux executable (pick the hml_runtime built for the target)");
        }

        // ---- pack the project (not the logs recorded here, not this PC's local overrides)
        fs::path tar = fs::temp_directory_path(ec) / ("hml_deploy_" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".tar");
        struct Cleanup { fs::path p; ~Cleanup() { std::error_code e; fs::remove(p, e); } } cleanup{ tar };
        {
            TarWriter tw(tar);
            if(!tw.f) return fail("cannot create " + ustr(tar));
            for(auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                std::string rel = ustr(fs::relative(it->path(), dir, ec));
                if(rel == "logs" || rel == ".git") { it.disable_recursion_pending(); continue; }
                if(rel == "project.local.json" || !it->is_regular_file(ec)) continue;
                if(!tw.add_file(it->path(), rel)) return fail("cannot read " + ustr(it->path()));
            }
            if(!tw.finish()) return fail("cannot write " + ustr(tar));
            step("packed " + std::to_string(tw.files) + " files (" + std::to_string((tw.bytes + 1023) / 1024) + " KB) from " + ustr(dir));
            for(const auto& n : tw.skipped) step("skipped (path too long): " + n);
        }

        // ---- ssh / scp
        std::string dest = user + "@" + hostn;
        std::vector<std::string> common{ "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", "-o", "StrictHostKeyChecking=accept-new" };
        if(!key.empty()) { common.push_back("-i"); common.push_back(key); common.push_back("-o"); common.push_back("IdentitiesOnly=yes"); }
        std::string failure;
        auto run = [&](const fs::path& tool, std::vector<std::string> args, const fs::path& cwd, int timeout_ms, const std::string& what) {
            ProcResult pr = run_capture(tool, args, {}, cwd, timeout_ms);
            std::string out = trimmed(pr.output);
            if(!out.empty()) log += out + "\n";
            if(!pr.error.empty()) failure = what + ": " + pr.error + " (is the OpenSSH client installed?)";
            else if(pr.timed_out) failure = what + ": timed out";
            else if(pr.code != 0) {
                failure = what + " failed (exit code " + std::to_string(pr.code) + ")";
                if(out.find("Permission denied") != std::string::npos)
                    failure += ": the target refused the login. Deploy uses key authentication (no password prompt): choose the private key, or add your public key to ~/.ssh/authorized_keys on the target";
                else if(out.find("REMOTE HOST IDENTIFICATION HAS CHANGED") != std::string::npos)
                    failure += ": the target's host key changed (reinstalled?). Remove the old entry with: ssh-keygen -R " + hostn;
            }
            return failure.empty() ? out : std::string();
        };
        auto ssh = [&](const std::string& cmd, int timeout_ms, const std::string& what) {
            std::vector<std::string> a = common;
            a.insert(a.end(), { "-p", std::to_string(port), dest, cmd });
            return run(ssh_tool("ssh"), a, {}, timeout_ms, what);
        };
        // scp runs in the file's folder and gets "./name": a Windows path ("C:\...") would be
        // taken for a remote host by some scp builds.
        auto scp = [&](const fs::path& file, const std::string& remote_path, const std::string& what) {
            std::vector<std::string> a = common;
            a.insert(a.end(), { "-P", std::to_string(port), "-q", "./" + ustr(file.filename()), dest + ":" + remote_path });
            run(ssh_tool("scp"), a, file.parent_path(), 180000, what);
        };

        std::string pdir = "projects/" + pname;
        step("connecting to " + dest + (port != 22 ? " port " + std::to_string(port) : ""));
        ssh("mkdir -p " + remote + "/" + pdir + " && uname -sm", 25000, "connect");
        if(!failure.empty()) return fail(failure);

        step("sending the project to " + remote + "/" + pdir);
        scp(tar, remote + "/.hml_deploy.tar", "sending the project");
        if(!failure.empty()) return fail(failure);
        if(!rtp.empty()) {
            step("sending the runtime (" + std::to_string((fs::file_size(rtp, ec) + 1023) / 1024) + " KB) to " + remote + "/hml_runtime");
            scp(fs::absolute(rtp), remote + "/hml_runtime.new", "sending the runtime");
            if(!failure.empty()) return fail(failure);
        }

        // install: unpack over the project folder (files only on the target, such as logs/ and
        // project.local.json, stay), then rename the new runtime over the old one.
        std::string cmd = "cd " + remote + " && tar -xf .hml_deploy.tar -C " + pdir + " && rm -f .hml_deploy.tar";
        if(!rtp.empty()) cmd += " && chmod +x hml_runtime.new && mv -f hml_runtime.new hml_runtime";
        cmd += " && echo HML_INSTALLED";
        // SIGUSR2 = restart (older runtimes are terminated by it and restarted by systemd)
        if(restart)
            cmd += " && if pgrep -x hml_runtime >/dev/null; then pkill -USR2 -x hml_runtime; sleep 5;"
                   " if pgrep -x hml_runtime >/dev/null; then echo HML_RUNNING; ps -o args= -C hml_runtime | head -n 1;"
                   " else echo HML_STOPPED; fi; else echo HML_NOT_RUNNING; fi";
        step(restart ? "installing and restarting the runtime" : "installing");
        std::string out = ssh(cmd, 40000, "install");
        if(!failure.empty()) return fail(failure);
        if(out.find("HML_INSTALLED") == std::string::npos) return fail("install: unexpected answer from the target");
        // the markers are for this function, not for the reader
        for(const auto& [mark, text] : { std::pair<const char*, const char*>{ "HML_INSTALLED\n", "" }, { "HML_RUNNING\n", "running: " },
                                         { "HML_STOPPED\n", "" }, { "HML_NOT_RUNNING\n", "" } }) {
            size_t at = log.find(mark);
            if(at != std::string::npos) log.replace(at, std::strlen(mark), text);
        }

        json res{ { "ok", true } };
        std::string note;
        if(restart) {
            if(out.find("HML_NOT_RUNNING") != std::string::npos)
                note = "hml_runtime is not running on the target: the files were installed, start the runtime there.";
            else if(out.find("HML_STOPPED") != std::string::npos)
                note = "hml_runtime did not come back after the restart: check the log on the target.";
            else if(out.find(pdir + "/project.json") == std::string::npos)
                note = "The runtime on the target was restarted, but it runs a different project (see the command line above). "
                       "Point it at " + remote + "/" + pdir + "/project.json.";
        }
        if(!note.empty()) { res["warning"] = note; log += "! " + note + "\n"; }
        step("done");
        res["log"] = log;
        return res;
    }

    void deploy_async(const std::string& id, const json& opts)
    {
        std::thread([this, id, opts] {
            std::string dump = deploy(opts).dump(-1, ' ', false, json::error_handler_t::replace);
            w->dispatch([this, id, dump] { w->resolve(id, 0, dump); });
        }).detach();
    }

    json launch(const std::string& project)
    {
        if(runtime.empty()) return err("hml_runtime not found");
        fs::path p = upath(project);
        std::string e;
        if(!launch_detached(runtime, { ustr(p) }, p.parent_path(), &e)) return err(e);
        return json{ { "ok", true } };
    }

    void bind_all()
    {
        auto args_of = [](const std::string& req) { json a = json::parse(req, nullptr, false); return a.is_array() ? a : json::array(); };
        auto str = [](const json& a, size_t i) { return (i < a.size() && a[i].is_string()) ? a[i].get<std::string>() : std::string(); };

        w->bind("hml_info", [this, args_of](const std::string&) { return info().dump(); });
        w->bind("hml_readProject", [this, args_of, str](const std::string& req) { return read_project(str(args_of(req), 0)).dump(); });
        w->bind("hml_writeFiles", [this, args_of, str](const std::string& req) {
            json a = args_of(req);
            return write_files(str(a, 0), a.size() > 1 && a[1].is_array() ? a[1] : json::array()).dump();
        });
        w->bind("hml_openDialog", [this, args_of, str](const std::string& req) {
            json a = args_of(req);
            std::string kind = str(a, 0), dir = str(a, 1), p;
            if(kind == "runtime") p = open_file_dialog(window(), "Locate hml_runtime", "Executables", "*.exe", dir);
            else if(kind == "key") {                          // SSH private key: start in ~/.ssh
                if(dir.empty()) {
#ifdef _WIN32
                    const char* home = std::getenv("USERPROFILE");
#else
                    const char* home = std::getenv("HOME");
#endif
                    if(home) dir = ustr(upath(home) / ".ssh");
                }
                p = open_file_dialog(window(), "Choose the SSH private key", "All files", "*", dir);
            }
            else if(kind == "targetRuntime") p = open_file_dialog(window(), "Choose the hml_runtime built for the target (Linux)", "All files", "*", dir);
            else p = open_file_dialog(window(), "Open project.json", "HML project (project.json)", "*.json", dir);
            if(kind == "runtime" && !p.empty()) { settings["runtime"] = p; save_settings(); find_runtime(); }
            return json(p).dump();
        });
        w->bind("hml_saveDialog", [this, args_of, str](const std::string& req) {
            json a = args_of(req);
            return json(save_file_dialog(window(), "Save project.json", str(a, 0).empty() ? "project.json" : str(a, 0), str(a, 1))).dump();
        });
        w->bind("hml_pickImage", [this, args_of, str](const std::string& req) { return pick_image(str(args_of(req), 0)).dump(); });
        w->bind("hml_launch", [this, args_of, str](const std::string& req) { return launch(str(args_of(req), 0)).dump(); });
        w->bind("hml_setTitle", [this, args_of, str](const std::string& req) { w->set_title(str(args_of(req), 0)); return std::string("true"); });
        w->bind("hml_settings", [this, args_of](const std::string& req) {
            json a = args_of(req);
            if(a.size() > 0 && a[0].is_object()) { for(auto& [k, v] : a[0].items()) settings[k] = v; save_settings(); }
            return settings.dump();
        });
        w->bind("hml_deploy", [this, args_of](const std::string& id, const std::string& req, void*) {
            json a = args_of(req);
            deploy_async(id, a.size() > 0 && a[0].is_object() ? a[0] : json::object());
        }, nullptr);
        w->bind("hml_preview", [this, args_of, str](const std::string& id, const std::string& req, void*) {
            json a = args_of(req);
            preview_async(id, str(a, 0), str(a, 1));
        }, nullptr);
    }
};

int app_main(const std::vector<std::string>& args)
{
    Host h;
    h.exe = exe_dir();
    bool debug = false;
    std::string renderer_override, deploy_project;
    json deploy_opts = json::object();
    for(size_t i = 1; i < args.size(); i++) {
        auto opt = [&](const char* flag, const char* key) {
            if(args[i] != flag || i + 1 >= args.size()) return false;
            deploy_opts[key] = args[++i];
            return true;
        };
        if(args[i] == "--debug") debug = true;
        else if(args[i] == "--deploy" && i + 1 < args.size()) deploy_project = ustr(fs::absolute(upath(args[++i])));
        else if(opt("--host", "host") || opt("--user", "user") || opt("--port", "port") || opt("--key", "key") ||
                opt("--remote-dir", "remoteDir") || opt("--runtime", "runtime")) {}
        else if(args[i] == "--no-restart") deploy_opts["restart"] = false;
        else if(args[i] == "--renderer" && i + 1 < args.size()) renderer_override = args[++i];
        else if(!args[i].empty() && args[i][0] != '-') h.argv_project = ustr(fs::absolute(upath(args[i])));
    }
#ifdef _DEBUG
    debug = true;
#endif

    // hml_designer --deploy <project.json> [--host <h> --user <u> --port <n> --key <file>
    //   --remote-dir <dir> --runtime <file> --no-restart]: deploy without opening the window.
    // Options left out come from what the Deploy dialog last used for this project.
    if(!deploy_project.empty()) {
        h.settings_path = config_dir() / "settings.json";
        h.load_settings();
        json o = json::object();
        if(h.settings.contains("deploy") && h.settings["deploy"].is_object()) {
            const json& all = h.settings["deploy"];
            for(auto& [k, v] : all.items()) if(v.is_object() && ustr(upath(k)) == deploy_project) o = v;   // ("*" = last used: not a default here)
            if(!(o.contains("sendRuntime") && o["sendRuntime"].is_boolean() && o["sendRuntime"].get<bool>())) o.erase("runtime");
        }
        for(auto& [k, v] : deploy_opts.items()) o[k] = v;
        o["project"] = deploy_project;
        json r = h.deploy(o);
        std::string text = r.value("log", std::string());
        if(!r.value("ok", false)) text += "error: " + r.value("error", std::string("failed")) + "\n";
        std::fputs(text.c_str(), stdout);
        std::fflush(stdout);
        return r.value("ok", false) ? 0 : 1;
    }

    std::error_code ec;
    std::vector<fs::path> candidates;
    if(!renderer_override.empty()) candidates.push_back(upath(renderer_override));
    candidates.push_back(h.exe / "designer");
#ifdef HML_DESIGNER_RENDERER_DIR
    candidates.push_back(upath(HML_DESIGNER_RENDERER_DIR));
#endif
    for(const auto& c : candidates) if(fs::exists(c / "index.html", ec)) { h.renderer = fs::absolute(c); break; }
    if(h.renderer.empty()) {
        std::fprintf(stderr, "hml_designer: renderer/index.html not found (looked next to the executable in designer/)\n");
#ifdef _WIN32
        MessageBoxW(nullptr, L"designer/index.html not found next to hml_designer.exe", L"HML Designer", MB_ICONERROR);
#endif
        return 1;
    }
    h.settings_path = config_dir() / "settings.json";
    h.load_settings();
    h.find_runtime();
#ifdef _WIN32
    // keep the WebView2 cache out of the program directory
    SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", (config_dir() / "webview2").wstring().c_str());
#endif

    webview::webview w(debug, nullptr);
    h.w = &w;
    w.set_title("HML Designer");
    w.set_size(1400, 860, WEBVIEW_HINT_NONE);
    h.bind_all();
#ifdef _WIN32
    if(HWND hwnd = static_cast<HWND>(h.window())) ShowWindow(hwnd, SW_MAXIMIZE);
#endif
    std::string url = "file:///" + ustr(h.renderer / "index.html");
    w.navigate(url);
    w.run();
    return 0;
}

} // namespace

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for(int i = 0; i < argc; i++) args.push_back(narrow(argv[i]));
    LocalFree(argv);
    return app_main(args);
}
#else
int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);
    return app_main(args);
}
#endif
