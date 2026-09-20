#pragma once

#include <string>
#include <vector>

bool parse_local_file_input(const std::string& input, std::vector<std::string>& paths,
                            std::string& error_message);
