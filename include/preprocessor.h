#pragma once

#include <string>

namespace frontend {

bool preprocess_source(const std::string& input, std::string& output,
                       std::string& error);

}
