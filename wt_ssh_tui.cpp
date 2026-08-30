#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "src/credential_store.h"

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

enum class AuthMode { Auto, Key, SavedPassword };
enum class X11Mode { Disabled, Untrusted, Trusted };

struct HostEntry {
    std::string name;
    std::string host;
    std::string user;
    int port = 22;
    std::string key_file;
    std::string note;
    std::string id;
    AuthMode auth_mode = AuthMode::Auto;
    X11Mode x11_mode = X11Mode::Disabled;
    std::string display;
};

namespace color {
constexpr const char* reset = "\x1b[0m";
constexpr const char* title = "\x1b[1;36m";
constexpr const char* hint = "\x1b[2;37m";
constexpr const char* divider = "\x1b[2;34m";
constexpr const char* selected = "\x1b[1;32m";
constexpr const char* normal = "\x1b[0;37m";
constexpr const char* warning = "\x1b[1;33m";
constexpr const char* success = "\x1b[1;32m";
constexpr const char* error = "\x1b[1;31m";
}  // namespace color

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

std::string lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string auth_to_string(AuthMode mode) {
    if (mode == AuthMode::Key) return "key";
    if (mode == AuthMode::SavedPassword) return "password";
    return "auto";
}

AuthMode auth_from_string(const std::string& value) {
    const auto v = lower_ascii(value);
    if (v == "key") return AuthMode::Key;
    if (v == "password") return AuthMode::SavedPassword;
    return AuthMode::Auto;
}

std::string x11_to_string(X11Mode mode) {
    if (mode == X11Mode::Untrusted) return "x";
    if (mode == X11Mode::Trusted) return "y";
    return "off";
}

X11Mode x11_from_string(const std::string& value) {
    const auto v = lower_ascii(value);
    if (v == "x" || v == "untrusted") return X11Mode::Untrusted;
    if (v == "y" || v == "trusted") return X11Mode::Trusted;
    return X11Mode::Disabled;
}

std::string make_id() {
    std::random_device rd;
    static const char* hex = "0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (int i = 0; i < 16; ++i) {
        const unsigned value = rd() & 0xffu;
        id.push_back(hex[value >> 4]);
        id.push_back(hex[value & 0x0f]);
    }
    return id;
}

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& input) {
    if (input.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring output(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), output.data(), size);
    return output;
}

std::string wide_to_utf8(const wchar_t* input, size_t length) {
    if (!input || length == 0) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, input, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string output(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input, static_cast<int>(length), output.data(), size, nullptr, nullptr);
    return output;
}

std::string windows_error(DWORD code = GetLastError()) {
    wchar_t* text = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string result = length ? wide_to_utf8(text, length) : ("Windows error " + std::to_string(code));
    if (text) LocalFree(text);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n' || result.back() == ' ')) result.pop_back();
    return result;
}
#endif

class HostStore {
public:
    HostStore() {
        const char* override_dir = std::getenv("WTSSH_DATA_DIR");
        if (override_dir && *override_dir) data_dir_ = std::filesystem::u8path(override_dir);
        else {
#ifdef _WIN32
            const char* profile = std::getenv("USERPROFILE");
            data_dir_ = std::filesystem::path(profile ? profile : ".") / ".wt_ssh_manager";
#else
            const char* home = std::getenv("HOME");
            data_dir_ = std::filesystem::path(home ? home : ".") / ".wt_ssh_manager";
#endif
        }
        data_file_ = data_dir_ / "hosts.db";
        std::error_code ec;
        std::filesystem::create_directories(data_dir_, ec);
    }

    std::vector<HostEntry> load() const {
        std::vector<HostEntry> entries;
        std::ifstream in(data_file_, std::ios::binary);
        if (!in.is_open()) return entries;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            auto cols = split_line(line);
            if (cols.size() < 6) continue;
            HostEntry e;
            e.name = unescape(cols[0]); e.host = unescape(cols[1]); e.user = unescape(cols[2]);
            try {
                size_t consumed = 0;
                const auto value = unescape(cols[3]);
                e.port = std::stoi(value, &consumed);
                if (consumed != value.size() || e.port < 1 || e.port > 65535) e.port = 22;
            } catch (...) { e.port = 22; }
            e.key_file = unescape(cols[4]); e.note = unescape(cols[5]);
            if (cols.size() >= 10) {
                e.id = unescape(cols[6]);
                e.auth_mode = auth_from_string(unescape(cols[7]));
                e.x11_mode = x11_from_string(unescape(cols[8]));
                e.display = unescape(cols[9]);
            }
            entries.push_back(std::move(e));
        }
        sort_entries(entries);
        return entries;
    }

    bool save(const std::vector<HostEntry>& entries, std::string& error_message) const {
#ifdef _WIN32
        const auto suffix = ".tmp." + std::to_string(GetCurrentProcessId());
#else
        const auto suffix = ".tmp." + std::to_string(getpid());
#endif
        auto temp = data_file_;
        temp += suffix;
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) { error_message = "Unable to open temporary configuration file."; return false; }
            out << "# wtssh-v2\n";
            for (const auto& e : entries) {
                out << escape(e.name) << '\t' << escape(e.host) << '\t' << escape(e.user) << '\t'
                    << e.port << '\t' << escape(e.key_file) << '\t' << escape(e.note) << '\t'
                    << escape(e.id) << '\t' << auth_to_string(e.auth_mode) << '\t'
                    << x11_to_string(e.x11_mode) << '\t' << escape(e.display) << '\n';
            }
            out.flush();
            if (!out.good()) {
                error_message = "Unable to write configuration file.";
                out.close();
                std::error_code ignored; std::filesystem::remove(temp, ignored);
                return false;
            }
        }
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), data_file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error_message = "Unable to replace configuration file: " + windows_error();
            std::error_code ignored; std::filesystem::remove(temp, ignored);
            return false;
        }
#else
        std::error_code ec; std::filesystem::rename(temp, data_file_, ec);
        if (ec) { error_message = "Unable to replace configuration file: " + ec.message(); std::filesystem::remove(temp, ec); return false; }
#endif
        return true;
    }

    static void sort_entries(std::vector<HostEntry>& entries) {
        std::sort(entries.begin(), entries.end(), [](const HostEntry& a, const HostEntry& b) {
            return lower_ascii(a.name) < lower_ascii(b.name);
        });
    }

private:
    std::filesystem::path data_dir_;
    std::filesystem::path data_file_;

    static std::string escape(const std::string& s) {
        std::string out;
        for (char ch : s) {
            if (ch == '\\' || ch == '\t' || ch == '\n' || ch == '\r') {
                out.push_back('\\');
                if (ch == '\t') out.push_back('t');
                else if (ch == '\n') out.push_back('n');
                else if (ch == '\r') out.push_back('r');
                else out.push_back('\\');
            } else out.push_back(ch);
        }
        return out;
    }

    static std::string unescape(const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                const char next = s[++i];
                if (next == 't') out.push_back('\t');
                else if (next == 'n') out.push_back('\n');
                else if (next == 'r') out.push_back('\r');
                else out.push_back(next);
            } else out.push_back(s[i]);
        }
        return out;
    }

    static std::vector<std::string> split_line(const std::string& line) {
        std::vector<std::string> cols;
        std::string current;
        bool escaped = false;
        for (char ch : line) {
            if (!escaped && ch == '\\') { escaped = true; current.push_back(ch); }
            else if (!escaped && ch == '\t') { cols.push_back(current); current.clear(); }
            else { current.push_back(ch); escaped = false; }
        }
        cols.push_back(current);
        return cols;
    }
};

#ifdef _WIN32
void enable_ansi() {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (GetConsoleMode(output, &mode)) SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#endif

void clear_screen() { std::cout << "\x1b[2J\x1b[H"; }

int read_key() {
#ifdef _WIN32
    int ch = _getch();
    if (ch == 0 || ch == 224) {
        const int ext = _getch();
        if (ext == 72) return 1001;
        if (ext == 80) return 1002;
        return ext;
    }
    return ch;
#else
    termios oldt{}, newt{};
    tcgetattr(STDIN_FILENO, &oldt); newt = oldt;
    newt.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int ch = getchar();
    if (ch == 27 && getchar() == 91) { const int arrow = getchar(); if (arrow == 65) ch = 1001; else if (arrow == 66) ch = 1002; }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
#endif
}

std::string prompt_line(const std::string& text, const std::string& def = "") {
    std::cout << color::normal << text;
    if (!def.empty()) std::cout << color::hint << " [" << def << "]" << color::normal;
    std::cout << ": " << color::reset;
    std::string line; std::getline(std::cin, line); line = trim(line);
    return line.empty() ? def : line;
}

std::string prompt_optional_edit(const std::string& text, const std::string& current) {
    std::cout << color::normal << text;
    if (!current.empty()) std::cout << color::hint << " [" << current << "]" << color::normal;
    std::cout << color::hint << " (use - to clear)" << color::normal << ": " << color::reset;
    std::string line; std::getline(std::cin, line); line = trim(line);
    if (line.empty()) return current;
    return line == "-" ? std::string{} : line;
}

bool prompt_yes_no(const std::string& text, bool default_yes = false) {
    const auto answer = lower_ascii(prompt_line(text, default_yes ? "Y" : "N"));
    return answer == "y" || answer == "yes";
}

SecretBuffer prompt_password(const std::string& text) {
    std::cout << color::normal << text << ": " << color::reset << std::flush;
    SecretBuffer password;
#ifdef _WIN32
    std::vector<wchar_t> wide;
    while (true) {
        const wchar_t ch = static_cast<wchar_t>(_getwch());
        if (ch == L'\r' || ch == L'\n') break;
        if (ch == L'\b') { if (!wide.empty()) { wide.back() = 0; wide.pop_back(); } }
        else if (ch == 0 || ch == 224) (void)_getwch();
        else if (ch >= 32) wide.push_back(ch);
    }
    auto utf8 = wide_to_utf8(wide.data(), wide.size());
    password.assign(utf8.data(), utf8.size());
    if (!wide.empty()) SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
    if (!utf8.empty()) SecureZeroMemory(utf8.data(), utf8.size());
#else
    termios oldt{}; tcgetattr(STDIN_FILENO, &oldt); termios noecho = oldt;
    noecho.c_lflag &= static_cast<unsigned>(~ECHO); tcsetattr(STDIN_FILENO, TCSANOW, &noecho);
    std::string input; std::getline(std::cin, input); tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    password.assign(input.data(), input.size());
    volatile char* p = input.data();
    for (size_t i = 0; i < input.size(); ++i) p[i] = 0;
#endif
    std::cout << '\n';
    return password;
}

std::optional<SecretBuffer> prompt_new_password(std::string& error_message) {
    auto first = prompt_password("Password (input hidden)");
    if (first.empty()) { error_message = "Password cannot be empty."; return std::nullopt; }
    auto second = prompt_password("Confirm password");
    if (!first.equals(second)) { error_message = "Passwords do not match."; return std::nullopt; }
    return std::optional<SecretBuffer>(std::move(first));
}

bool parse_port(const std::string& value, int& port) {
    try {
        size_t consumed = 0; const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size() || parsed < 1 || parsed > 65535) return false;
        port = parsed; return true;
    } catch (...) { return false; }
}

AuthMode prompt_auth_mode(AuthMode current, bool has_key) {
    const std::string def = auth_to_string(current == AuthMode::Auto && has_key ? AuthMode::Key : current);
    while (true) {
        const auto value = lower_ascii(prompt_line("Authentication (auto/key/password)", def));
        if (value == "auto" || value == "a") return AuthMode::Auto;
        if (value == "key" || value == "k") return AuthMode::Key;
        if (value == "password" || value == "p") return AuthMode::SavedPassword;
        std::cout << color::error << "Enter auto, key, or password.\n" << color::reset;
    }
}

X11Mode prompt_x11_mode(X11Mode current) {
    while (true) {
        const auto value = lower_ascii(prompt_line("X11 forwarding (off/x/y)", x11_to_string(current)));
        if (value == "off" || value == "none" || value == "n") return X11Mode::Disabled;
        if (value == "x" || value == "untrusted") return X11Mode::Untrusted;
        if (value == "y" || value == "trusted") {
            std::cout << color::warning << "Warning: -Y gives remote X11 applications full access to your local X display.\n" << color::reset;
            return X11Mode::Trusted;
        }
        std::cout << color::error << "Enter off, x, or y.\n" << color::reset;
    }
}

bool name_exists(const std::vector<HostEntry>& entries, const std::string& name, const std::string& except_id = {}) {
    return std::any_of(entries.begin(), entries.end(), [&](const HostEntry& e) { return e.name == name && e.id != except_id; });
}

void configure_x11(HostEntry& e, bool editing) {
    e.x11_mode = prompt_x11_mode(e.x11_mode);
    if (e.x11_mode == X11Mode::Disabled) { e.display.clear(); return; }
    const char* inherited = std::getenv("DISPLAY");
    std::string def = e.display;
    if (def.empty() && inherited && *inherited) def = inherited;
    if (def.empty()) def = "localhost:0.0";
    e.display = editing ? prompt_optional_edit("Local DISPLAY", def) : prompt_line("Local DISPLAY", def);
}

std::optional<HostEntry> prompt_host_fields(const std::vector<HostEntry>& entries, HostEntry e, bool editing,
                                            std::string& error_message) {
    e.name = editing ? prompt_line("Name (unique)", e.name) : prompt_line("Name (unique)");
    if (e.name.empty()) { error_message = "Name cannot be empty."; return std::nullopt; }
    if (name_exists(entries, e.name, editing ? e.id : std::string{})) { error_message = "Name already exists."; return std::nullopt; }
    e.host = editing ? prompt_line("Host / IP", e.host) : prompt_line("Host / IP");
    if (e.host.empty()) { error_message = "Host cannot be empty."; return std::nullopt; }
    e.user = editing ? prompt_optional_edit("Username", e.user) : prompt_line("Username (optional)");
    int parsed_port = e.port;
    if (!parse_port(prompt_line("Port", std::to_string(e.port)), parsed_port)) {
        error_message = "Port must be between 1 and 65535."; return std::nullopt;
    }
    e.port = parsed_port;
    e.key_file = editing ? prompt_optional_edit("Private key path", e.key_file) : prompt_line("Private key path (optional)");
    e.note = editing ? prompt_optional_edit("Note", e.note) : prompt_line("Note (optional)");
    e.auth_mode = prompt_auth_mode(e.auth_mode, !e.key_file.empty());
    if (e.auth_mode == AuthMode::Key && e.key_file.empty()) { error_message = "Key authentication requires a private key path."; return std::nullopt; }
    configure_x11(e, editing);
    return e;
}

#ifdef _WIN32
std::wstring quote_windows_arg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') ++backslashes;
        else if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\'); result.push_back(ch); backslashes = 0;
        } else { result.append(backslashes, L'\\'); backslashes = 0; result.push_back(ch); }
    }
    result.append(backslashes * 2, L'\\'); result.push_back(L'\"');
    return result;
}

std::wstring current_executable_path() {
    std::vector<wchar_t> buffer(512);
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size() - 1) return std::wstring(buffer.data(), length);
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring resolve_ssh_path(std::string& error_message) {
    const char* override_path = std::getenv("WTSSH_SSH_PATH");
    if (override_path && *override_path) {
        const auto path = utf8_to_wide(override_path);
        if (std::filesystem::exists(path)) return path;
        error_message = "WTSSH_SSH_PATH does not exist."; return {};
    }
    DWORD needed = SearchPathW(nullptr, L"ssh.exe", nullptr, 0, nullptr, nullptr);
    if (!needed) { error_message = "ssh.exe was not found in PATH."; return {}; }
    std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1);
    if (!SearchPathW(nullptr, L"ssh.exe", nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr)) {
        error_message = "Unable to resolve ssh.exe: " + windows_error(); return {};
    }
    return buffer.data();
}

bool env_name_is(const std::wstring& entry, const std::wstring& name) {
    return entry.size() > name.size() && entry[name.size()] == L'=' && _wcsnicmp(entry.c_str(), name.c_str(), name.size()) == 0;
}

void set_child_env(std::vector<std::wstring>& entries, const std::wstring& name, const std::wstring& value) {
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const std::wstring& e) { return env_name_is(e, name); }), entries.end());
    entries.push_back(name + L"=" + value);
}

std::vector<wchar_t> make_environment(const HostEntry& entry) {
    std::vector<std::wstring> entries;
    LPWCH block = GetEnvironmentStringsW();
    if (block) {
        for (const wchar_t* p = block; *p; p += wcslen(p) + 1) entries.emplace_back(p);
        FreeEnvironmentStringsW(block);
    }
    if (entry.x11_mode != X11Mode::Disabled) set_child_env(entries, L"DISPLAY", utf8_to_wide(entry.display));
    if (entry.auth_mode == AuthMode::SavedPassword) {
        set_child_env(entries, L"SSH_ASKPASS", current_executable_path());
        set_child_env(entries, L"SSH_ASKPASS_REQUIRE", L"force");
        set_child_env(entries, L"WTSSH_ASKPASS_MODE", L"1");
        set_child_env(entries, L"WTSSH_CREDENTIAL_ID", utf8_to_wide(entry.id));
    }
    std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    size_t total = 1;
    for (const auto& e : entries) total += e.size() + 1;
    std::vector<wchar_t> result(total, L'\0');
    wchar_t* out = result.data();
    for (const auto& e : entries) { std::copy(e.begin(), e.end(), out); out += e.size() + 1; }
    return result;
}
#else
std::string current_executable_path_posix() {
#ifdef __APPLE__
    uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    std::error_code ec;
    const auto canonical = std::filesystem::canonical(buffer.data(), ec);
    return ec ? std::string(buffer.data()) : canonical.string();
#elif defined(__linux__)
    std::vector<char> buffer(512, '\0');
    while (true) {
        const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (length < 0) return {};
        if (static_cast<size_t>(length) < buffer.size() - 1)
            return std::string(buffer.data(), static_cast<size_t>(length));
        buffer.resize(buffer.size() * 2, '\0');
    }
#else
    return {};
#endif
}

bool posix_env_name_is(const std::string& entry, const std::string& name) {
    return entry.size() > name.size() && entry.compare(0, name.size(), name) == 0 && entry[name.size()] == '=';
}

void set_posix_child_env(std::vector<std::string>& entries, const std::string& name, const std::string& value) {
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const std::string& entry) {
        return posix_env_name_is(entry, name);
    }), entries.end());
    entries.push_back(name + "=" + value);
}

std::vector<std::string> make_posix_environment(const HostEntry& entry, std::string& error_message) {
    std::vector<std::string> entries;
    for (char** item = environ; item && *item; ++item) entries.emplace_back(*item);
    if (entry.x11_mode != X11Mode::Disabled) set_posix_child_env(entries, "DISPLAY", entry.display);
    if (entry.auth_mode == AuthMode::SavedPassword) {
        const auto executable = current_executable_path_posix();
        if (executable.empty()) {
            error_message = "Unable to determine the wtssh executable path for SSH_ASKPASS.";
            return {};
        }
        set_posix_child_env(entries, "SSH_ASKPASS", executable);
        set_posix_child_env(entries, "SSH_ASKPASS_REQUIRE", "force");
        set_posix_child_env(entries, "WTSSH_ASKPASS_MODE", "1");
        set_posix_child_env(entries, "WTSSH_CREDENTIAL_ID", entry.id);
    }
    return entries;
}
#endif

std::vector<std::string> build_ssh_args(const HostEntry& e) {
    std::vector<std::string> args{"-p", std::to_string(e.port)};
    if (e.x11_mode == X11Mode::Untrusted) args.push_back("-X");
    else if (e.x11_mode == X11Mode::Trusted) args.push_back("-Y");
    if (!e.key_file.empty() && e.auth_mode != AuthMode::SavedPassword) { args.push_back("-i"); args.push_back(e.key_file); }
    if (e.auth_mode == AuthMode::Key) {
        args.push_back("-o"); args.push_back("PreferredAuthentications=publickey");
        args.push_back("-o"); args.push_back("IdentitiesOnly=yes");
    } else if (e.auth_mode == AuthMode::SavedPassword) {
        args.push_back("-o"); args.push_back("PreferredAuthentications=password");
        args.push_back("-o"); args.push_back("PubkeyAuthentication=no");
        args.push_back("-o"); args.push_back("KbdInteractiveAuthentication=no");
        args.push_back("-o"); args.push_back("NumberOfPasswordPrompts=1");
    }
    args.push_back(e.user.empty() ? e.host : e.user + "@" + e.host);
    return args;
}

int launch_ssh(const HostEntry& entry, std::string& error_message) {
    if (entry.x11_mode != X11Mode::Disabled && entry.display.empty()) { error_message = "X11 forwarding requires a local DISPLAY value."; return -1; }
    const auto args = build_ssh_args(entry);
#ifdef _WIN32
    const auto ssh_path = resolve_ssh_path(error_message);
    if (ssh_path.empty()) return -1;
    std::vector<std::wstring> wide_args{ssh_path};
    for (const auto& arg : args) wide_args.push_back(utf8_to_wide(arg));
    std::wstring command_line;
    for (const auto& arg : wide_args) { if (!command_line.empty()) command_line.push_back(L' '); command_line += quote_windows_arg(arg); }
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end()); mutable_command.push_back(L'\0');
    auto environment = make_environment(entry);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(ssh_path.c_str(), mutable_command.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT,
                        environment.data(), nullptr, &startup, &process)) {
        error_message = "Unable to start ssh.exe: " + windows_error(); return -1;
    }
    CloseHandle(process.hThread); WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1; GetExitCodeProcess(process.hProcess, &exit_code); CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
#else
    const char* override_path = std::getenv("WTSSH_SSH_PATH");
    const std::string program = override_path && *override_path ? override_path : "ssh";
    std::vector<std::string> argument_storage{program};
    argument_storage.insert(argument_storage.end(), args.begin(), args.end());
    std::vector<char*> argument_pointers;
    for (auto& value : argument_storage) argument_pointers.push_back(value.data());
    argument_pointers.push_back(nullptr);

    auto environment_storage = make_posix_environment(entry, error_message);
    if (!error_message.empty()) return -1;
    std::vector<char*> environment_pointers;
    for (auto& value : environment_storage) environment_pointers.push_back(value.data());
    environment_pointers.push_back(nullptr);

    pid_t child = -1;
    const int spawn_result = posix_spawnp(&child, program.c_str(), nullptr, nullptr,
                                          argument_pointers.data(), environment_pointers.data());
    if (spawn_result != 0) {
        error_message = "Unable to start ssh: " + std::string(std::strerror(spawn_result));
        return -1;
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        error_message = "Unable to wait for ssh: " + std::string(std::strerror(errno));
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
#endif
}

std::string display_command(const HostEntry& e) {
    std::ostringstream out; out << "ssh";
    for (const auto& arg : build_ssh_args(e)) {
        const bool quote = arg.find_first_of(" \t\"") != std::string::npos;
        out << ' ' << (quote ? "\"" + arg + "\"" : arg);
    }
    return out.str();
}

int run_askpass(int argc, char** argv) {
    const char* hint = std::getenv("SSH_ASKPASS_PROMPT");
    if (hint && std::string(hint) == "confirm") {
#ifdef _WIN32
        const wchar_t* prompt = L"OpenSSH confirmation";
        std::wstring converted;
        if (argc > 1) { converted = utf8_to_wide(argv[1]); if (!converted.empty()) prompt = converted.c_str(); }
        return MessageBoxW(nullptr, prompt, L"WT SSH Manager", MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND) == IDYES ? 0 : 1;
#else
        const int tty = open("/dev/tty", O_RDWR);
        if (tty < 0) return 1;
        const std::string prompt = std::string(argc > 1 ? argv[1] : "OpenSSH confirmation") + " [y/N]: ";
        (void)write(tty, prompt.data(), prompt.size());
        char answer[8]{};
        const ssize_t count = read(tty, answer, sizeof(answer) - 1);
        close(tty);
        return count > 0 && (answer[0] == 'y' || answer[0] == 'Y') ? 0 : 1;
#endif
    }
    const char* id = std::getenv("WTSSH_CREDENTIAL_ID");
    if (!id || !*id) return 1;
    CredentialStore credentials; std::string error_message;
    auto password = credentials.read(id, error_message);
    if (!password) return 1;
#ifdef _WIN32
    DWORD written = 0;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool secret_ok = WriteFile(output, password->data(), static_cast<DWORD>(password->size()), &written, nullptr) &&
                           written == password->size();
    const char newline = '\n';
    const bool newline_ok = WriteFile(output, &newline, 1, &written, nullptr) && written == 1;
    return secret_ok && newline_ok ? 0 : 1;
#else
    size_t offset = 0;
    while (offset < password->size()) {
        const ssize_t count = write(STDOUT_FILENO, password->data() + offset, password->size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 1;
        offset += static_cast<size_t>(count);
    }
    return write(STDOUT_FILENO, "\n", 1) == 1 ? 0 : 1;
#endif
}

bool is_ok_message(const std::string& msg) {
    return msg.rfind("Added:", 0) == 0 || msg.rfind("Updated:", 0) == 0 || msg.rfind("Deleted:", 0) == 0 ||
           msg.rfind("Migrated", 0) == 0 || msg == "Delete canceled.";
}

std::string flags_for(const HostEntry& e) {
    std::string flags;
    if (e.auth_mode == AuthMode::SavedPassword) flags += " [password saved]";
    else if (e.auth_mode == AuthMode::Key) flags += " [key]";
    if (e.x11_mode == X11Mode::Untrusted) flags += " [X11 -X]";
    else if (e.x11_mode == X11Mode::Trusted) flags += " [X11 -Y]";
    return flags;
}

void draw_ui(const std::vector<HostEntry>& entries, int selected, const std::string& msg) {
    clear_screen();
    std::cout << color::title << "WT SSH Manager" << color::reset << '\n'
              << color::hint << "Arrow Up/Down Select | A Add | E Edit | D Delete | C/Enter Connect | Q Quit" << color::reset << '\n'
              << color::divider << "--------------------------------------------------------------------------" << color::reset << '\n';
    if (entries.empty()) std::cout << color::warning << "No saved servers yet. Press A to add one." << color::reset << '\n';
    else for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& e = entries[i]; const bool chosen = i == selected;
        std::cout << (chosen ? color::selected : color::normal) << (chosen ? "> " : "  ") << e.name << " -> "
                  << (e.user.empty() ? "" : e.user + "@") << e.host << ':' << e.port << flags_for(e);
        if (!e.note.empty()) std::cout << color::hint << "  # " << e.note;
        std::cout << color::reset << '\n';
    }
    std::cout << color::divider << "--------------------------------------------------------------------------" << color::reset << '\n';
    if (!msg.empty()) std::cout << (is_ok_message(msg) ? color::success : color::error) << msg << color::reset << '\n';
}

bool add_host(std::vector<HostEntry>& entries, HostStore& store, CredentialStore& credentials, int& selected, std::string& message) {
    clear_screen(); std::cout << color::title << "== Add Server ==" << color::reset << '\n';
    HostEntry seed; seed.id = make_id();
    auto proposed = prompt_host_fields(entries, seed, false, message);
    if (!proposed) return false;
    std::optional<SecretBuffer> password;
    if (proposed->auth_mode == AuthMode::SavedPassword) {
        if (!credentials.available()) {
            message = std::string("Password storage is unavailable (backend: ") + credentials.backend_name() + ").";
            return false;
        }
        password = prompt_new_password(message); if (!password) return false;
        if (!credentials.write(proposed->id, proposed->user, *password, message)) return false;
    }
    auto updated = entries; updated.push_back(*proposed); HostStore::sort_entries(updated);
    if (!store.save(updated, message)) {
        if (proposed->auth_mode == AuthMode::SavedPassword) { std::string ignored; credentials.remove(proposed->id, ignored); }
        return false;
    }
    entries = std::move(updated);
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) if (entries[i].id == proposed->id) selected = i;
    message = "Added: " + proposed->name; return true;
}

bool edit_host(std::vector<HostEntry>& entries, HostStore& store, CredentialStore& credentials, int& selected, std::string& message) {
    if (entries.empty()) return false;
    clear_screen(); std::cout << color::title << "== Edit Server ==" << color::reset << '\n';
    const HostEntry original = entries[selected];
    auto proposed = prompt_host_fields(entries, original, true, message);
    if (!proposed) return false;
    const bool had_password = credentials.exists(original.id);
    std::optional<SecretBuffer> password;
    if (proposed->auth_mode == AuthMode::SavedPassword && !credentials.available()) {
        message = std::string("Password storage is unavailable (backend: ") + credentials.backend_name() + ").";
        return false;
    }
    if (proposed->auth_mode == AuthMode::SavedPassword && (!had_password || prompt_yes_no("Replace saved password", false))) {
        password = prompt_new_password(message); if (!password) return false;
        if (!credentials.write(proposed->id, proposed->user, *password, message)) return false;
    }
    auto updated = entries; updated[selected] = *proposed; HostStore::sort_entries(updated);
    if (!store.save(updated, message)) {
        if (!had_password && password) { std::string ignored; credentials.remove(proposed->id, ignored); }
        return false;
    }
    if (original.auth_mode == AuthMode::SavedPassword && proposed->auth_mode != AuthMode::SavedPassword) {
        std::string deletion_error; if (!credentials.remove(original.id, deletion_error)) message = deletion_error;
    }
    entries = std::move(updated);
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) if (entries[i].id == proposed->id) selected = i;
    if (message.empty() || message.rfind("Unable", 0) != 0) message = "Updated: " + proposed->name;
    return true;
}

bool migrate_entries(std::vector<HostEntry>& entries, HostStore& store, std::string& message) {
    bool changed = false;
    for (auto& entry : entries) if (entry.id.empty()) { entry.id = make_id(); changed = true; }
    if (!changed) return true;
    if (!store.save(entries, message)) return false;
    message = "Migrated existing host records to v2."; return true;
}

int connect_entry(const HostEntry& e, const CredentialStore& credentials) {
    if (e.auth_mode == AuthMode::SavedPassword && !credentials.exists(e.id)) {
        std::cerr << color::error << "Saved password is missing. Edit this entry and save it again." << color::reset << '\n';
        return 1;
    }
    clear_screen(); const std::string target = e.user.empty() ? e.host : e.user + "@" + e.host;
    std::cout << color::title << "Connecting" << color::reset << ": " << e.name << " (" << target << ")\n"
              << color::hint << "Command" << color::reset << ": " << display_command(e) << '\n';
    if (e.x11_mode != X11Mode::Disabled) std::cout << color::hint << "Local DISPLAY" << color::reset << ": " << e.display << '\n';
    std::cout << '\n';
    std::string error_message; const int result = launch_ssh(e, error_message);
    if (!error_message.empty()) std::cerr << color::error << error_message << color::reset << '\n';
    return result < 0 ? 1 : result;
}

void print_hosts(const std::vector<HostEntry>& entries) {
    for (const auto& e : entries)
        std::cout << e.name << '\t' << (e.user.empty() ? "" : e.user + "@") << e.host << ':' << e.port << flags_for(e) << '\n';
}

int main(int argc, char** argv) {
    const char* askpass = std::getenv("WTSSH_ASKPASS_MODE");
    if (askpass && std::string(askpass) == "1") return run_askpass(argc, argv);
#ifdef _WIN32
    enable_ansi();
#endif
    HostStore store; CredentialStore credentials; auto entries = store.load(); std::string message;
    migrate_entries(entries, store, message);
    if (argc >= 2 && std::string(argv[1]) == "--credential-backend") {
        std::cout << credentials.backend_name() << (credentials.available() ? " (available)" : " (unavailable)") << '\n';
        return credentials.available() ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--self-test-credential") {
        const std::string id = "self-test-" + make_id();
        auto expected = SecretBuffer::from_utf8_literal("WtsshTest42!");
        if (!credentials.write(id, "wtssh-test", expected, message)) {
            std::cerr << message << '\n';
            return 1;
        }
        auto actual = credentials.read(id, message);
        std::string remove_error;
        const bool removed = credentials.remove(id, remove_error);
        if (!actual || !expected.equals(*actual)) {
            std::cerr << (message.empty() ? "Credential round-trip mismatch." : message) << '\n';
            return 1;
        }
        if (!removed) {
            std::cerr << remove_error << '\n';
            return 1;
        }
        std::cout << "Credential Manager round-trip passed.\n";
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "--list") { print_hosts(entries); return 0; }
    if (argc >= 3 && std::string(argv[1]) == "--connect") {
        const auto it = std::find_if(entries.begin(), entries.end(), [&](const HostEntry& e) { return e.name == argv[2]; });
        if (it == entries.end()) { std::cerr << "Host not found: " << argv[2] << '\n'; return 1; }
        return connect_entry(*it, credentials);
    }
    int selected = 0;
    while (true) {
        if (entries.empty()) selected = 0; else selected = std::clamp(selected, 0, static_cast<int>(entries.size()) - 1);
        draw_ui(entries, selected, message); message.clear(); const int key = read_key();
        if (key == 'q' || key == 'Q') { clear_screen(); return 0; }
        if (key == 1001 && !entries.empty()) { selected = std::max(0, selected - 1); continue; }
        if (key == 1002 && !entries.empty()) { selected = std::min(static_cast<int>(entries.size()) - 1, selected + 1); continue; }
        if (key == 'a' || key == 'A') { add_host(entries, store, credentials, selected, message); continue; }
        if ((key == 'e' || key == 'E') && !entries.empty()) { edit_host(entries, store, credentials, selected, message); continue; }
        if ((key == 'd' || key == 'D') && !entries.empty()) {
            clear_screen(); const HostEntry deleted = entries[selected];
            if (!prompt_yes_no("Delete \"" + deleted.name + "\"", false)) { message = "Delete canceled."; continue; }
            auto updated = entries; updated.erase(updated.begin() + selected);
            if (!store.save(updated, message)) continue;
            entries = std::move(updated); std::string credential_error;
            if (!credentials.remove(deleted.id, credential_error)) message = credential_error; else message = "Deleted: " + deleted.name;
            continue;
        }
        if ((key == 'c' || key == 'C' || key == 13) && !entries.empty()) return connect_entry(entries[selected], credentials);
    }
}
