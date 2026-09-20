#include "scp_paths.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}
}  // namespace

int main() {
    std::vector<std::string> paths;
    std::string error;

    if (!expect(parse_local_file_input("\"C:\\One File.txt\" \"D:\\Two File.txt\"", paths, error),
                "Double-quoted drag input was rejected.") ||
        !expect(paths.size() == 2 && paths[0] == "C:\\One File.txt" && paths[1] == "D:\\Two File.txt",
                "Double-quoted drag input was split incorrectly.")) return 1;

    if (!expect(parse_local_file_input("'C:\\One File.txt' 'D:\\Two File.txt'", paths, error),
                "Single-quoted PowerShell input was rejected.") ||
        !expect(paths.size() == 2, "Single-quoted PowerShell input was split incorrectly.")) return 1;

    if (!expect(parse_local_file_input("C:\\one.txt D:\\two.txt", paths, error) && paths.size() == 2,
                "Unquoted space-separated paths were not split.")) return 1;

    if (!expect(!parse_local_file_input("\"C:\\unfinished path.txt", paths, error) && !error.empty(),
                "An unmatched quote was accepted.")) return 1;

    const auto existing = std::filesystem::current_path() / "scp parser existing file.txt";
    { std::ofstream output(existing); output << "test"; }
    const auto existing_text = existing.u8string();
    const bool existing_ok = parse_local_file_input(existing_text, paths, error) &&
                             paths.size() == 1 && paths.front() == existing_text;
    std::error_code ignored;
    std::filesystem::remove(existing, ignored);
    if (!expect(existing_ok, "An existing unquoted path containing spaces was not kept whole.")) return 1;

    return 0;
}
