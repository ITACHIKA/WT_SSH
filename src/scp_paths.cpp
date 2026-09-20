#include "scp_paths.h"

#include <cctype>
#include <filesystem>
#include <utility>

namespace {
std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

std::string strip_matching_quotes(const std::string& value) {
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\'')))
        return value.substr(1, value.size() - 2);
    return value;
}
}  // namespace

bool parse_local_file_input(const std::string& input, std::vector<std::string>& paths,
                            std::string& error_message) {
    paths.clear();
    error_message.clear();
    const auto trimmed = trim(input);
    if (trimmed.empty()) {
        error_message = "Local file path cannot be empty.";
        return false;
    }

    // Preserve a single existing unquoted path, including paths containing spaces.
    const auto whole_path = strip_matching_quotes(trimmed);
    std::error_code filesystem_error;
    if (std::filesystem::exists(std::filesystem::u8path(whole_path), filesystem_error) && !filesystem_error) {
        paths.push_back(whole_path);
        return true;
    }

    std::string current;
    char active_quote = '\0';
    for (char ch : trimmed) {
        if ((ch == '"' || ch == '\'') && (active_quote == '\0' || active_quote == ch)) {
            active_quote = active_quote == '\0' ? ch : '\0';
        } else if (std::isspace(static_cast<unsigned char>(ch)) && active_quote == '\0') {
            if (!current.empty()) { paths.push_back(std::move(current)); current.clear(); }
        } else {
            current.push_back(ch);
        }
    }
    if (active_quote != '\0') {
        error_message = "Local file paths contain an unmatched quote.";
        paths.clear();
        return false;
    }
    if (!current.empty()) paths.push_back(std::move(current));
    if (paths.empty()) {
        error_message = "No local files were found in the input.";
        return false;
    }
    return true;
}
