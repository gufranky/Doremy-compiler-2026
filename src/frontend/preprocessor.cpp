#include "preprocessor.h"

#include <cctype>
#include <string>
#include <unordered_map>

namespace frontend {
namespace {

using MacroTable = std::unordered_map<std::string, std::string>;

bool is_identifier_start(char ch) {
  unsigned char uch = static_cast<unsigned char>(ch);
  return std::isalpha(uch) || ch == '_';
}

bool is_identifier_char(char ch) {
  unsigned char uch = static_cast<unsigned char>(ch);
  return std::isalnum(uch) || ch == '_';
}

std::string trim_left(const std::string& text) {
  size_t start = 0;
  while (start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[start]))) {
    ++start;
  }
  return text.substr(start);
}

std::string trim(const std::string& text) {
  size_t start = 0;
  while (start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[start]))) {
    ++start;
  }

  size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }

  return text.substr(start, end - start);
}

std::string expand_identifiers(const std::string& text, const MacroTable& macros) {
  std::string expanded;
  expanded.reserve(text.size());

  size_t index = 0;
  while (index < text.size()) {
    if (!is_identifier_start(text[index])) {
      expanded.push_back(text[index]);
      ++index;
      continue;
    }

    size_t end = index + 1;
    while (end < text.size() && is_identifier_char(text[end])) {
      ++end;
    }

    std::string token = text.substr(index, end - index);
    auto iter = macros.find(token);
    if (iter != macros.end()) {
      expanded += iter->second;
    } else {
      expanded += token;
    }

    index = end;
  }

  return expanded;
}

bool handle_define(const std::string& directive, MacroTable& macros,
                   std::string& error) {
  size_t index = 1;
  while (index < directive.size() &&
         std::isspace(static_cast<unsigned char>(directive[index]))) {
    ++index;
  }

  const std::string keyword = "define";
  if (directive.compare(index, keyword.size(), keyword) != 0) {
    error = "unsupported directive '" + trim(directive) + "'";
    return false;
  }
  index += keyword.size();

  if (index < directive.size() &&
      !std::isspace(static_cast<unsigned char>(directive[index]))) {
    error = "unsupported directive '" + trim(directive) + "'";
    return false;
  }

  while (index < directive.size() &&
         std::isspace(static_cast<unsigned char>(directive[index]))) {
    ++index;
  }

  if (index >= directive.size() || !is_identifier_start(directive[index])) {
    error = "invalid macro name in '" + trim(directive) + "'";
    return false;
  }

  size_t name_end = index + 1;
  while (name_end < directive.size() && is_identifier_char(directive[name_end])) {
    ++name_end;
  }
  std::string macro_name = directive.substr(index, name_end - index);

  index = name_end;
  while (index < directive.size() &&
         std::isspace(static_cast<unsigned char>(directive[index]))) {
    ++index;
  }

  if (index >= directive.size()) {
    error = "missing replacement list for macro '" + macro_name + "'";
    return false;
  }

  std::string replacement = trim(directive.substr(index));
  macros[macro_name] = expand_identifiers(replacement, macros);
  return true;
}

}  // namespace

bool preprocess_source(const std::string& input, std::string& output,
                       std::string& error) {
  output.clear();
  error.clear();

  MacroTable macros;
  size_t position = 0;
  int line_number = 1;

  while (position < input.size()) {
    size_t line_end = input.find('\n', position);
    bool has_newline = line_end != std::string::npos;
    size_t raw_end = has_newline ? line_end : input.size();

    std::string line = input.substr(position, raw_end - position);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    std::string trimmed = trim_left(line);
    if (!trimmed.empty() && trimmed.front() == '#') {
      if (!handle_define(trimmed, macros, error)) {
        error = "line " + std::to_string(line_number) + ": " + error;
        return false;
      }
      if (has_newline) {
        output.push_back('\n');
      }
    } else {
      output += expand_identifiers(line, macros);
      if (has_newline) {
        output.push_back('\n');
      }
    }

    position = has_newline ? line_end + 1 : input.size();
    ++line_number;
  }

  return true;
}

}  // namespace frontend
