#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

struct HostEntry {
    std::string name;
    std::string host;
    std::string user;
    int port = 22;
    std::string key_file;
    std::string note;
};

class HostStore {
public:
    HostStore() {
#ifdef _WIN32
        const char* user_profile = std::getenv("USERPROFILE");
        std::filesystem::path home = user_profile ? user_profile : ".";
#else
        const char* home_env = std::getenv("HOME");
        std::filesystem::path home = home_env ? home_env : ".";
#endif
        data_dir_ = home / ".wt_ssh_manager";
        data_file_ = data_dir_ / "hosts.db";
        std::error_code ec;
        std::filesystem::create_directories(data_dir_, ec);
    }

    std::vector<HostEntry> load() const {
        std::vector<HostEntry> entries;
        std::ifstream in(data_file_);
        if (!in.is_open()) {
            return entries;
        }

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) {
                continue;
            }
            auto cols = split_line(line);
            if (cols.size() < 6) {
                continue;
            }
            HostEntry e;
            e.name = unescape(cols[0]);
            e.host = unescape(cols[1]);
            e.user = unescape(cols[2]);
            try {
                e.port = std::stoi(unescape(cols[3]));
            } catch (...) {
                e.port = 22;
            }
            e.key_file = unescape(cols[4]);
            e.note = unescape(cols[5]);
            entries.push_back(e);
        }

        std::sort(entries.begin(), entries.end(), [](const HostEntry& a, const HostEntry& b) {
            std::string an = a.name;
            std::string bn = b.name;
            std::transform(an.begin(), an.end(), an.begin(), [](unsigned char c) { return std::tolower(c); });
            std::transform(bn.begin(), bn.end(), bn.begin(), [](unsigned char c) { return std::tolower(c); });
            return an < bn;
        });
        return entries;
    }

    bool save(const std::vector<HostEntry>& entries) const {
        std::ofstream out(data_file_, std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }

        for (const auto& e : entries) {
            out << escape(e.name) << "\t"
                << escape(e.host) << "\t"
                << escape(e.user) << "\t"
                << escape(std::to_string(e.port)) << "\t"
                << escape(e.key_file) << "\t"
                << escape(e.note) << "\n";
        }
        return true;
    }

private:
    std::filesystem::path data_dir_;
    std::filesystem::path data_file_;

    static std::string escape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char ch : s) {
            if (ch == '\\' || ch == '\t' || ch == '\n') {
                out.push_back('\\');
                if (ch == '\t') out.push_back('t');
                else if (ch == '\n') out.push_back('n');
                else out.push_back('\\');
            } else {
                out.push_back(ch);
            }
        }
        return out;
    }

    static std::string unescape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char next = s[i + 1];
                if (next == 't') {
                    out.push_back('\t');
                    ++i;
                    continue;
                }
                if (next == 'n') {
                    out.push_back('\n');
                    ++i;
                    continue;
                }
                if (next == '\\') {
                    out.push_back('\\');
                    ++i;
                    continue;
                }
            }
            out.push_back(s[i]);
        }
        return out;
    }

    static std::vector<std::string> split_line(const std::string& line) {
        std::vector<std::string> cols;
        std::string cur;
        bool escaped = false;
        for (char ch : line) {
            if (!escaped && ch == '\\') {
                escaped = true;
                cur.push_back(ch);
                continue;
            }

            if (!escaped && ch == '\t') {
                cols.push_back(cur);
                cur.clear();
                continue;
            }

            cur.push_back(ch);
            escaped = false;
        }
        cols.push_back(cur);
        return cols;
    }
};

#ifdef _WIN32
void enable_ansi() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) return;
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
}
#endif

void clear_screen() {
    std::cout << "\x1b[2J\x1b[H";
}

void wait_enter() {
    std::cout << "\n按回车继续...";
    std::string dummy;
    std::getline(std::cin, dummy);
}

int read_key() {
#ifdef _WIN32
    int ch = _getch();
    if (ch == 0 || ch == 224) {
        int ext = _getch();
        if (ext == 72) return 1001;
        if (ext == 80) return 1002;
        return ext;
    }
    return ch;
#else
    termios oldt{}, newt{};
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    int ch = getchar();
    if (ch == 27) {
        int ch1 = getchar();
        if (ch1 == 91) {
            int ch2 = getchar();
            if (ch2 == 65) ch = 1001;
            else if (ch2 == 66) ch = 1002;
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
#endif
}

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

std::string prompt_line(const std::string& text, const std::string& def = "") {
    std::cout << text;
    if (!def.empty()) {
        std::cout << " [" << def << "]";
    }
    std::cout << ": ";
    std::string line;
    std::getline(std::cin, line);
    line = trim(line);
    if (line.empty()) {
        return def;
    }
    return line;
}

std::string shell_quote(const std::string& s) {
    std::string out = "\"";
    for (char ch : s) {
        if (ch == '"' || ch == '\\') out.push_back('\\');
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

void draw_ui(const std::vector<HostEntry>& entries, int selected, const std::string& msg) {
    clear_screen();
    std::cout << "WT SSH Manager (TUI)\n";
    std::cout << "上下方向键选择  A新增  D删除  C连接  Q退出\n";
    std::cout << "------------------------------------------------------------\n";

    if (entries.empty()) {
        std::cout << "(暂无服务器，按 A 添加)\n";
    } else {
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const auto& e = entries[i];
            bool is_sel = (i == selected);
            std::cout << (is_sel ? "> " : "  ")
                      << e.name << " -> "
                      << (e.user.empty() ? "" : e.user + "@")
                      << e.host << ":" << e.port;
            if (!e.note.empty()) {
                std::cout << "  # " << e.note;
            }
            std::cout << "\n";
        }
    }

    std::cout << "------------------------------------------------------------\n";
    if (!msg.empty()) {
        std::cout << msg << "\n";
    }
}

bool name_exists(const std::vector<HostEntry>& entries, const std::string& name) {
    for (const auto& e : entries) {
        if (e.name == name) return true;
    }
    return false;
}

int main() {
#ifdef _WIN32
    enable_ansi();
#endif

    HostStore store;
    auto entries = store.load();
    int selected = 0;
    std::string message;

    while (true) {
        if (!entries.empty()) {
            if (selected < 0) selected = 0;
            if (selected >= static_cast<int>(entries.size())) selected = static_cast<int>(entries.size()) - 1;
        } else {
            selected = 0;
        }

        draw_ui(entries, selected, message);
        message.clear();

        int key = read_key();
        if (key == 'q' || key == 'Q') {
            clear_screen();
            std::cout << "Bye.\n";
            break;
        }

        if (key == 1001 && !entries.empty()) {
            selected = std::max(0, selected - 1);
            continue;
        }
        if (key == 1002 && !entries.empty()) {
            selected = std::min(static_cast<int>(entries.size()) - 1, selected + 1);
            continue;
        }

        if (key == 'a' || key == 'A') {
            clear_screen();
            std::cout << "== 添加服务器 ==\n";
            HostEntry e;
            e.name = prompt_line("名称(唯一)");
            if (e.name.empty()) {
                message = "名称不能为空。";
                continue;
            }
            if (name_exists(entries, e.name)) {
                message = "名称已存在。";
                continue;
            }
            e.host = prompt_line("主机/IP");
            if (e.host.empty()) {
                message = "主机不能为空。";
                continue;
            }
            e.user = prompt_line("用户名(可空)");
            std::string port_str = prompt_line("端口", "22");
            try {
                e.port = std::stoi(port_str);
            } catch (...) {
                message = "端口无效，使用默认22。";
                e.port = 22;
            }
            e.key_file = prompt_line("私钥路径(可空)");
            e.note = prompt_line("备注(可空)");

            entries.push_back(e);
            std::sort(entries.begin(), entries.end(), [](const HostEntry& a, const HostEntry& b) {
                return a.name < b.name;
            });
            store.save(entries);
            for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
                if (entries[i].name == e.name) {
                    selected = i;
                    break;
                }
            }
            message = "已添加: " + e.name;
            continue;
        }

        if ((key == 'd' || key == 'D') && !entries.empty()) {
            clear_screen();
            std::cout << "确认删除 \"" << entries[selected].name << "\" ? (y/N): ";
            std::string ans;
            std::getline(std::cin, ans);
            if (!ans.empty() && (ans[0] == 'y' || ans[0] == 'Y')) {
                std::string deleted = entries[selected].name;
                entries.erase(entries.begin() + selected);
                store.save(entries);
                if (selected >= static_cast<int>(entries.size()) && !entries.empty()) {
                    selected = static_cast<int>(entries.size()) - 1;
                }
                message = "已删除: " + deleted;
            } else {
                message = "已取消删除。";
            }
            continue;
        }

        if ((key == 'c' || key == 'C') && !entries.empty()) {
            const auto& e = entries[selected];
            clear_screen();
            std::ostringstream cmd;
            cmd << "ssh -p " << e.port << " ";
            if (!e.key_file.empty()) {
                cmd << "-i " << shell_quote(e.key_file) << " ";
            }
            std::string target = e.user.empty() ? e.host : e.user + "@" + e.host;
            cmd << shell_quote(target);

            std::cout << "正在连接: " << e.name << " (" << target << ")\n";
            std::cout << "执行命令: " << cmd.str() << "\n";
            std::cout << "(不会保存密码，认证由 ssh 处理)\n\n";
            std::system(cmd.str().c_str());
            wait_enter();
            continue;
        }
    }

    return 0;
}
