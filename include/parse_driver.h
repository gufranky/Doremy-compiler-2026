#pragma once

#include <string>

namespace frontend {

bool parse_from_file(const std::string& path);
bool parse_from_stdin();

}
