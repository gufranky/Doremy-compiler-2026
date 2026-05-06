#pragma once

#include <string>

namespace frontend {

bool preprocess_source(const std::string& input, const std::string& source_path,
                       std::string& output, std::string& error);

}
