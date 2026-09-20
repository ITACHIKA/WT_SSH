#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "src/credential_store.h"
#include "src/scp_paths.h"

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

struct HostEntry {
    std::string name;
    std::string host;
    std::string user;
    int port = 22;
    std::string key_file;
    std::string note;
    std::string id;
    AuthMode auth_mode = AuthMode::Auto;
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

std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

AuthMode auth_from_string(const std::string& value) {
    const auto normalized = lower_ascii(value);
    if (normalized == "key") return AuthMode::Key;
    if (normalized == "password") return AuthMode::SavedPassword;
    return AuthMode::Auto;
}

std::string unescape(const std::string& value) {
    std::string output;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[++i];
            if (next == 't') output.push_back('\t');
            else if (next == 'n') output.push_back('\n');
            else if (next == 'r') output.push_back('\r');
            else output.push_back(next);
        } else {
            output.push_back(value[i]);
        }
    }
    return output;
}

std::vector<std::string> split_record(const std::string& line) {
    std::vector<std::string> columns;
    std::string current;
    bool escaped = false;
    for (char ch : line) {
        if (!escaped && ch == '\\') { escaped = true; current.push_back(ch); }
        else if (!escaped && ch == '\t') { columns.push_back(current); current.clear(); }
        else { current.push_back(ch); escaped = false; }
    }
    columns.push_back(current);
    return columns;
}

std::filesystem::path data_directory() {
    const char* override_dir = std::getenv("WTSSH_DATA_DIR");
    if (override_dir && *override_dir) return std::filesystem::u8path(override_dir);
#ifdef _WIN32
    const char* profile = std::getenv("USERPROFILE");
    return std::filesystem::path(profile ? profile : ".") / ".wt_ssh_manager";
#else
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".wt_ssh_manager";
#endif
}

std::vector<HostEntry> load_hosts() {
    std::vector<HostEntry> entries;
    std::ifstream input(data_directory() / "hosts.db", std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto columns = split_record(line);
        if (columns.size() < 6) continue;
        HostEntry entry;
        entry.name = unescape(columns[0]);
        entry.host = unescape(columns[1]);
        entry.user = unescape(columns[2]);
        try {
            size_t consumed = 0;
            const auto port = unescape(columns[3]);
            entry.port = std::stoi(port, &consumed);
            if (consumed != port.size() || entry.port < 1 || entry.port > 65535) entry.port = 22;
        } catch (...) { entry.port = 22; }
        entry.key_file = unescape(columns[4]);
        entry.note = unescape(columns[5]);
        if (columns.size() >= 8) {
            entry.id = unescape(columns[6]);
            entry.auth_mode = auth_from_string(unescape(columns[7]));
        }
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const HostEntry& left, const HostEntry& right) {
        return lower_ascii(left.name) < lower_ascii(right.name);
    });
    return entries;
}

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& input) {
    if (input.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                         static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring output(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
                        output.data(), size);
    return output;
}

std::string wide_to_utf8(const wchar_t* input, size_t length) {
    if (!input || length == 0) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, input, static_cast<int>(length), nullptr, 0,
                                         nullptr, nullptr);
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
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n' || result.back() == ' '))
        result.pop_back();
    return result;
}

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
        const int extended = _getch();
        if (extended == 72) return 1001;
        if (extended == 80) return 1002;
        return extended;
    }
    return ch;
#else
    termios old_state{}, new_state{};
    tcgetattr(STDIN_FILENO, &old_state);
    new_state = old_state;
    new_state.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
    tcsetattr(STDIN_FILENO, TCSANOW, &new_state);
    int ch = getchar();
    if (ch == 27 && getchar() == 91) {
        const int arrow = getchar();
        if (arrow == 65) ch = 1001;
        else if (arrow == 66) ch = 1002;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &old_state);
    return ch;
#endif
}

std::string prompt_line(const std::string& label, const std::string& default_value = {}) {
    std::cout << color::normal << label;
    if (!default_value.empty()) std::cout << color::hint << " [" << default_value << "]" << color::normal;
    std::cout << ": " << color::reset;
    std::string value;
    std::getline(std::cin, value);
    value = trim(value);
    return value.empty() ? default_value : value;
}

std::string flags_for(const HostEntry& entry) {
    if (entry.auth_mode == AuthMode::SavedPassword) return " [password saved]";
    if (entry.auth_mode == AuthMode::Key) return " [key]";
    return {};
}

void draw_ui(const std::vector<HostEntry>& entries, int selected, const std::string& message) {
    clear_screen();
    std::cout << color::title << "SCP Manager" << color::reset << '\n'
              << color::hint << "Arrow Up/Down Select | C/Enter Upload File | Q Quit" << color::reset << '\n'
              << color::divider << "--------------------------------------------------------------------------" << color::reset << '\n';
    if (entries.empty()) {
        std::cout << color::warning << "No saved servers. Add a host with the SSH manager first."
                  << color::reset << '\n';
    } else {
        for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
            const auto& entry = entries[index];
            const bool chosen = index == selected;
            std::cout << (chosen ? color::selected : color::normal) << (chosen ? "> " : "  ")
                      << entry.name << " -> " << (entry.user.empty() ? "" : entry.user + "@")
                      << entry.host << ':' << entry.port << flags_for(entry);
            if (!entry.note.empty()) std::cout << color::hint << "  # " << entry.note;
            std::cout << color::reset << '\n';
        }
    }
    std::cout << color::divider << "--------------------------------------------------------------------------" << color::reset << '\n';
    if (!message.empty()) std::cout << color::normal << message << color::reset << '\n';
}

std::string remote_target(const HostEntry& entry, const std::string& path) {
    std::string host = entry.host;
    if (host.find(':') != std::string::npos && (host.empty() || host.front() != '[')) host = '[' + host + ']';
    return (entry.user.empty() ? "" : entry.user + "@") + host + ':' + path;
}

std::vector<std::string> build_scp_args(const HostEntry& entry, const std::vector<std::string>& local_paths,
                                        const std::string& remote_path) {
    std::vector<std::string> arguments{"-P", std::to_string(entry.port)};
    if (!entry.key_file.empty() && entry.auth_mode != AuthMode::SavedPassword) {
        arguments.push_back("-i");
        arguments.push_back(entry.key_file);
    }
    if (entry.auth_mode == AuthMode::Key) {
        arguments.push_back("-o"); arguments.push_back("PreferredAuthentications=publickey");
        arguments.push_back("-o"); arguments.push_back("IdentitiesOnly=yes");
    } else if (entry.auth_mode == AuthMode::SavedPassword) {
        arguments.push_back("-o"); arguments.push_back("PreferredAuthentications=password");
        arguments.push_back("-o"); arguments.push_back("PubkeyAuthentication=no");
        arguments.push_back("-o"); arguments.push_back("KbdInteractiveAuthentication=no");
        arguments.push_back("-o"); arguments.push_back("NumberOfPasswordPrompts=1");
    }
    arguments.push_back("--");
    arguments.insert(arguments.end(), local_paths.begin(), local_paths.end());
    arguments.push_back(remote_target(entry, remote_path));
    return arguments;
}

#ifdef _WIN32
std::wstring quote_windows_arg(const std::wstring& argument) {
    if (argument.empty()) return L"\"\"";
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') ++backslashes;
        else if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(ch);
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            backslashes = 0;
            result.push_back(ch);
        }
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
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

std::wstring resolve_scp_path(std::string& error_message) {
    const char* override_path = std::getenv("WTSCP_SCP_PATH");
    if (override_path && *override_path) {
        const auto path = utf8_to_wide(override_path);
        if (std::filesystem::exists(path)) return path;
        error_message = "WTSCP_SCP_PATH does not exist.";
        return {};
    }
    const DWORD needed = SearchPathW(nullptr, L"scp.exe", nullptr, 0, nullptr, nullptr);
    if (!needed) { error_message = "scp.exe was not found in PATH."; return {}; }
    std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1);
    if (!SearchPathW(nullptr, L"scp.exe", nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr)) {
        error_message = "Unable to resolve scp.exe: " + windows_error();
        return {};
    }
    return buffer.data();
}

bool env_name_is(const std::wstring& entry, const std::wstring& name) {
    return entry.size() > name.size() && entry[name.size()] == L'=' &&
           _wcsnicmp(entry.c_str(), name.c_str(), name.size()) == 0;
}

void set_child_env(std::vector<std::wstring>& entries, const std::wstring& name, const std::wstring& value) {
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const std::wstring& entry) {
        return env_name_is(entry, name);
    }), entries.end());
    entries.push_back(name + L"=" + value);
}

std::vector<wchar_t> make_environment(const HostEntry& entry) {
    std::vector<std::wstring> entries;
    LPWCH block = GetEnvironmentStringsW();
    if (block) {
        for (const wchar_t* item = block; *item; item += wcslen(item) + 1) entries.emplace_back(item);
        FreeEnvironmentStringsW(block);
    }
    if (entry.auth_mode == AuthMode::SavedPassword) {
        set_child_env(entries, L"SSH_ASKPASS", current_executable_path());
        set_child_env(entries, L"SSH_ASKPASS_REQUIRE", L"force");
        set_child_env(entries, L"WTSCP_ASKPASS_MODE", L"1");
        set_child_env(entries, L"WTSSH_CREDENTIAL_ID", utf8_to_wide(entry.id));
    }
    std::sort(entries.begin(), entries.end(), [](const std::wstring& left, const std::wstring& right) {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    });
    size_t total = 1;
    for (const auto& entry_value : entries) total += entry_value.size() + 1;
    std::vector<wchar_t> result(total, L'\0');
    wchar_t* output = result.data();
    for (const auto& entry_value : entries) {
        std::copy(entry_value.begin(), entry_value.end(), output);
        output += entry_value.size() + 1;
    }
    return result;
}
#else
std::string current_executable_path() {
#ifdef __APPLE__
    uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    std::error_code error;
    const auto canonical = std::filesystem::canonical(buffer.data(), error);
    return error ? std::string(buffer.data()) : canonical.string();
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

bool env_name_is(const std::string& entry, const std::string& name) {
    return entry.size() > name.size() && entry.compare(0, name.size(), name) == 0 && entry[name.size()] == '=';
}

void set_child_env(std::vector<std::string>& entries, const std::string& name, const std::string& value) {
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const std::string& entry) {
        return env_name_is(entry, name);
    }), entries.end());
    entries.push_back(name + "=" + value);
}

std::vector<std::string> make_environment(const HostEntry& entry, std::string& error_message) {
    std::vector<std::string> entries;
    for (char** item = environ; item && *item; ++item) entries.emplace_back(*item);
    if (entry.auth_mode == AuthMode::SavedPassword) {
        const auto executable = current_executable_path();
        if (executable.empty()) {
            error_message = "Unable to determine the scpm executable path for SSH_ASKPASS.";
            return {};
        }
        set_child_env(entries, "SSH_ASKPASS", executable);
        set_child_env(entries, "SSH_ASKPASS_REQUIRE", "force");
        set_child_env(entries, "WTSCP_ASKPASS_MODE", "1");
        set_child_env(entries, "WTSSH_CREDENTIAL_ID", entry.id);
    }
    return entries;
}
#endif

int launch_scp(const HostEntry& entry, const std::vector<std::string>& local_paths, const std::string& remote_path,
               std::string& error_message) {
    const auto arguments = build_scp_args(entry, local_paths, remote_path);
#ifdef _WIN32
    const auto program = resolve_scp_path(error_message);
    if (program.empty()) return -1;
    std::vector<std::wstring> wide_arguments{program};
    for (const auto& argument : arguments) wide_arguments.push_back(utf8_to_wide(argument));
    std::wstring command_line;
    for (const auto& argument : wide_arguments) {
        if (!command_line.empty()) command_line.push_back(L' ');
        command_line += quote_windows_arg(argument);
    }
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');
    auto environment = make_environment(entry);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
                        CREATE_UNICODE_ENVIRONMENT, environment.data(), nullptr, &startup, &process)) {
        error_message = "Unable to start scp.exe: " + windows_error();
        return -1;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
#else
    const char* override_path = std::getenv("WTSCP_SCP_PATH");
    const std::string program = override_path && *override_path ? override_path : "scp";
    std::vector<std::string> argument_storage{program};
    argument_storage.insert(argument_storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argument_pointers;
    for (auto& argument : argument_storage) argument_pointers.push_back(argument.data());
    argument_pointers.push_back(nullptr);
    auto environment_storage = make_environment(entry, error_message);
    if (!error_message.empty()) return -1;
    std::vector<char*> environment_pointers;
    for (auto& environment_entry : environment_storage) environment_pointers.push_back(environment_entry.data());
    environment_pointers.push_back(nullptr);
    pid_t child = -1;
    const int spawn_result = posix_spawnp(&child, program.c_str(), nullptr, nullptr,
                                          argument_pointers.data(), environment_pointers.data());
    if (spawn_result != 0) {
        error_message = "Unable to start scp: " + std::string(std::strerror(spawn_result));
        return -1;
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        error_message = "Unable to wait for scp: " + std::string(std::strerror(errno));
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
#endif
}

std::string display_command(const HostEntry& entry, const std::vector<std::string>& local_paths,
                            const std::string& remote_path) {
    std::ostringstream output;
    output << "scp";
    for (const auto& argument : build_scp_args(entry, local_paths, remote_path)) {
        const bool quote = argument.find_first_of(" \t\"") != std::string::npos;
        output << ' ' << (quote ? "\"" + argument + "\"" : argument);
    }
    return output.str();
}

int run_askpass(int argc, char** argv) {
    const char* hint = std::getenv("SSH_ASKPASS_PROMPT");
    if (hint && std::string(hint) == "confirm") {
#ifdef _WIN32
        const wchar_t* prompt = L"OpenSSH confirmation";
        std::wstring converted;
        if (argc > 1) { converted = utf8_to_wide(argv[1]); if (!converted.empty()) prompt = converted.c_str(); }
        return MessageBoxW(nullptr, prompt, L"SCP Manager", MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND) == IDYES ? 0 : 1;
#else
        const int terminal = open("/dev/tty", O_RDWR);
        if (terminal < 0) return 1;
        const std::string prompt = std::string(argc > 1 ? argv[1] : "OpenSSH confirmation") + " [y/N]: ";
        (void)write(terminal, prompt.data(), prompt.size());
        char answer[8]{};
        const ssize_t count = read(terminal, answer, sizeof(answer) - 1);
        close(terminal);
        return count > 0 && (answer[0] == 'y' || answer[0] == 'Y') ? 0 : 1;
#endif
    }
    const char* id = std::getenv("WTSSH_CREDENTIAL_ID");
    if (!id || !*id) return 1;
    CredentialStore credentials;
    std::string error_message;
    auto password = credentials.read(id, error_message);
    if (!password) return 1;
#ifdef _WIN32
    DWORD written = 0;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool password_ok = WriteFile(output, password->data(), static_cast<DWORD>(password->size()),
                                       &written, nullptr) && written == password->size();
    const char newline = '\n';
    const bool newline_ok = WriteFile(output, &newline, 1, &written, nullptr) && written == 1;
    return password_ok && newline_ok ? 0 : 1;
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

int transfer_files(const HostEntry& entry, const std::vector<std::string>& local_paths,
                   const std::string& remote_path, const CredentialStore& credentials,
                   std::string& message) {
    if (local_paths.empty()) { message = "At least one local file is required."; return 1; }
    if (remote_path.empty()) { message = "Remote destination path cannot be empty."; return 1; }
    for (const auto& local_path : local_paths) {
        std::error_code filesystem_error;
        const auto source = std::filesystem::u8path(local_path);
        if (!std::filesystem::exists(source, filesystem_error) || filesystem_error) {
            message = "Local file does not exist: " + local_path;
            return 1;
        }
        if (!std::filesystem::is_regular_file(source, filesystem_error) || filesystem_error) {
            message = "The source must be a regular file (directory upload is not enabled): " + local_path;
            return 1;
        }
    }
    if (entry.auth_mode == AuthMode::SavedPassword && !credentials.exists(entry.id)) {
        message = "Saved password is missing. Edit this host in the SSH manager and save it again.";
        return 1;
    }
    clear_screen();
    std::cout << color::title << "Uploading " << local_paths.size() << " file"
              << (local_paths.size() == 1 ? "" : "s") << color::reset << '\n';
    for (const auto& local_path : local_paths)
        std::cout << color::hint << "Source" << color::reset << ": " << local_path << '\n';
    std::cout
              << color::hint << "Destination" << color::reset << ": " << remote_target(entry, remote_path) << '\n'
              << color::hint << "Command" << color::reset << ": "
              << display_command(entry, local_paths, remote_path) << "\n\n";
    std::string launch_error;
    const int result = launch_scp(entry, local_paths, remote_path, launch_error);
    if (!launch_error.empty()) message = launch_error;
    else if (result == 0) message = "Upload completed: " + std::to_string(local_paths.size()) + " file(s).";
    else message = "SCP exited with code " + std::to_string(result) + '.';
    return result < 0 ? 1 : result;
}

int main(int argc, char** argv) {
    const char* askpass = std::getenv("WTSCP_ASKPASS_MODE");
    if (askpass && std::string(askpass) == "1") return run_askpass(argc, argv);
#ifdef _WIN32
    enable_ansi();
#endif
    const auto entries = load_hosts();
    CredentialStore credentials;
    if (argc >= 2 && std::string(argv[1]) == "--credential-backend") {
        std::cout << credentials.backend_name() << (credentials.available() ? " (available)" : " (unavailable)") << '\n';
        return credentials.available() ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--list") {
        for (const auto& entry : entries)
            std::cout << entry.name << '\t' << (entry.user.empty() ? "" : entry.user + "@")
                      << entry.host << ':' << entry.port << flags_for(entry) << '\n';
        return 0;
    }
    if (argc >= 5 && std::string(argv[1]) == "--send") {
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const HostEntry& entry) {
            return entry.name == argv[2];
        });
        if (found == entries.end()) { std::cerr << "Host not found: " << argv[2] << '\n'; return 1; }
        std::vector<std::string> local_paths;
        for (int index = 3; index < argc - 1; ++index) local_paths.emplace_back(argv[index]);
        std::string message;
        const int result = transfer_files(*found, local_paths, argv[argc - 1], credentials, message);
        if (result != 0) std::cerr << message << '\n';
        return result;
    }
    if (argc > 1) {
        std::cerr << "Usage: scpm [--list | --credential-backend | --send HOST LOCAL_FILE... REMOTE_PATH]\n";
        return 2;
    }

    int selected = 0;
    std::string message;
    while (true) {
        if (entries.empty()) selected = 0;
        else selected = std::clamp(selected, 0, static_cast<int>(entries.size()) - 1);
        draw_ui(entries, selected, message);
        message.clear();
        const int key = read_key();
        if (key == 'q' || key == 'Q') { clear_screen(); return 0; }
        if (key == 1001 && !entries.empty()) { selected = std::max(0, selected - 1); continue; }
        if (key == 1002 && !entries.empty()) {
            selected = std::min(static_cast<int>(entries.size()) - 1, selected + 1);
            continue;
        }
        if ((key == 'c' || key == 'C' || key == 13) && !entries.empty()) {
            clear_screen();
            std::cout << color::title << "== Upload File to " << entries[selected].name << " ==" << color::reset << '\n'
                      << color::hint << "Type one file path, or drag one or more selected files into this window.\n"
                      << "Quoted paths separated by spaces are recognized as separate files.\n"
                      << "For multiple files, the remote destination must be a directory (for example ~/uploads/).\n"
                      << "The destination host is the selected SSH server.\n" << color::reset;
            const auto local_input = prompt_line("Local file path(s)");
            const auto remote_path = prompt_line("Remote destination path", "~/");
            std::vector<std::string> local_paths;
            if (!parse_local_file_input(local_input, local_paths, message)) {
                message = "Upload canceled: " + message;
                continue;
            }
            transfer_files(entries[selected], local_paths, remote_path, credentials, message);
        }
    }
}
