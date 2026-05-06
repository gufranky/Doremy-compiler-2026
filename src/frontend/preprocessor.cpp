#include "preprocessor.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace frontend {
namespace {

struct MacroDef {
  std::string replacement;
  bool zero_arg_function = false;
  bool inject_line_number = false;
};

using MacroTable = std::unordered_map<std::string, MacroDef>;

struct PreprocessContext {
  MacroTable macros;
  std::unordered_set<std::string> include_stack;
};

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

std::string expand_identifiers(const std::string& text, const MacroTable& macros,
                               int line_number) {
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
    bool followed_by_lparen = end < text.size() && text[end] == '(';
    bool followed_by_empty_args =
        followed_by_lparen && end + 1 < text.size() && text[end + 1] == ')';
    if (iter != macros.end() &&
        (!followed_by_lparen ||
         (iter->second.zero_arg_function && followed_by_empty_args))) {
      std::string replacement = iter->second.replacement;
      if (iter->second.inject_line_number) {
        size_t placeholder = replacement.find("__LINE__");
        while (placeholder != std::string::npos) {
          replacement.replace(placeholder, 8, std::to_string(line_number));
          placeholder = replacement.find("__LINE__", placeholder + 1);
        }
      }
      expanded += replacement;
      if (followed_by_empty_args) {
        index = end + 2;
        continue;
      }
    } else {
      expanded += token;
    }

    index = end;
  }

  return expanded;
}

std::string normalize_separators(std::string path) {
  for (char& ch : path) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  return path;
}

bool is_absolute_path(const std::string& path) {
  if (path.empty()) return false;
  if (path[0] == '/' || path[0] == '\\') return true;
  return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) &&
         path[1] == ':';
}

std::string parent_path(const std::string& path) {
  std::string normalized = normalize_separators(path);
  size_t pos = normalized.find_last_of('/');
  if (pos == std::string::npos) {
    return "";
  }
  return normalized.substr(0, pos);
}

std::string join_path(const std::string& base, const std::string& child) {
  if (child.empty()) return normalize_separators(base);
  if (is_absolute_path(child)) return normalize_separators(child);
  if (base.empty()) return normalize_separators(child);
  return normalize_separators(base + "/" + child);
}

std::string normalize_path(std::string path) {
  path = normalize_separators(path);

  std::string prefix;
  size_t start = 0;
  if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) &&
      path[1] == ':') {
    prefix = path.substr(0, 2);
    start = 2;
    if (start < path.size() && path[start] == '/') {
      prefix += '/';
      ++start;
    }
  } else if (!path.empty() && path[0] == '/') {
    prefix = "/";
    start = 1;
  }

  std::vector<std::string> parts;
  size_t i = start;
  while (i <= path.size()) {
    size_t j = path.find('/', i);
    if (j == std::string::npos) j = path.size();
    std::string part = path.substr(i, j - i);
    if (part.empty() || part == ".") {
      // skip
    } else if (part == "..") {
      if (!parts.empty() && parts.back() != "..") {
        parts.pop_back();
      } else if (prefix.empty()) {
        parts.push_back(part);
      }
    } else {
      parts.push_back(part);
    }
    i = j + 1;
  }

  std::ostringstream oss;
  oss << prefix;
  for (size_t idx = 0; idx < parts.size(); ++idx) {
    if (!(prefix.empty() && idx == 0) &&
        !(idx == 0 && !prefix.empty() && prefix.back() == '/')) {
      if (!(idx == 0 && !prefix.empty() && prefix == "/")) {
        oss << '/';
      }
    }
    oss << parts[idx];
  }

  std::string normalized = oss.str();
  if (normalized.empty()) {
    return prefix.empty() ? "." : prefix;
  }
  return normalized;
}

bool load_file_text(const std::string& path, std::string& content) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  content.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
  return true;
}

bool is_supported_sysy_header_line(const std::string& trimmed) {
  if (trimmed.empty()) return true;
  if (trimmed.rfind("#define starttime()", 0) == 0) return true;
  if (trimmed.rfind("#define stoptime()", 0) == 0) return true;
  if (trimmed.rfind("int getint()", 0) == 0) return true;
  if (trimmed.rfind("int getch()", 0) == 0) return true;
  if (trimmed.rfind("int getarray(", 0) == 0) return true;
  if (trimmed.rfind("float getfloat()", 0) == 0) return true;
  if (trimmed.rfind("int getfarray(", 0) == 0) return true;
  if (trimmed.rfind("void putint(", 0) == 0) return true;
  if (trimmed.rfind("void putch(", 0) == 0) return true;
  if (trimmed.rfind("void putarray(", 0) == 0) return true;
  if (trimmed.rfind("void putfloat(", 0) == 0) return true;
  if (trimmed.rfind("void putfarray(", 0) == 0) return true;
  if (trimmed.rfind("void putf(", 0) == 0) return true;
  if (trimmed.rfind("void _sysy_starttime(", 0) == 0) return true;
  if (trimmed.rfind("void _sysy_stoptime(", 0) == 0) return true;
  return false;
}

std::string filter_sysy_header_source(const std::string& input) {
  std::ostringstream filtered;
  size_t position = 0;
  while (position < input.size()) {
    size_t line_end = input.find('\n', position);
    bool has_newline = line_end != std::string::npos;
    size_t raw_end = has_newline ? line_end : input.size();
    std::string line = input.substr(position, raw_end - position);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::string trimmed = trim(line);
    if (is_supported_sysy_header_line(trimmed)) {
      filtered << line;
    }
    if (has_newline) {
      filtered << '\n';
    }
    position = has_newline ? line_end + 1 : input.size();
  }
  return filtered.str();
}

bool preprocess_source_impl(const std::string& input, const std::string& source_path,
                            std::string& output, std::string& error,
                            PreprocessContext& context);

bool handle_include(const std::string& directive, const std::string& current_path,
                    std::string& output, std::string& error,
                    PreprocessContext& context) {
  const std::string prefix = "#include";
  size_t index = prefix.size();
  while (index < directive.size() &&
         std::isspace(static_cast<unsigned char>(directive[index]))) {
    ++index;
  }

  if (index >= directive.size() || directive[index] != '"') {
    error = "only #include \"...\" is supported";
    return false;
  }
  ++index;

  size_t end = directive.find('"', index);
  if (end == std::string::npos) {
    error = "unterminated include path";
    return false;
  }

  std::string include_name = directive.substr(index, end - index);
  std::string base_dir = parent_path(current_path);
  std::string include_path = normalize_path(join_path(base_dir, include_name));
  std::string include_key = include_path;

  if (context.include_stack.count(include_key) != 0) {
    error = "recursive include detected for '" + include_name + "'";
    return false;
  }

  std::string include_source;
  if (!load_file_text(include_path, include_source)) {
    error = "cannot open include file '" + include_name + "'";
    return false;
  }

  if (normalize_separators(include_name) == "sylib.h" ||
      include_path.size() >= 7 &&
          include_path.substr(include_path.size() - 7) == "sylib.h") {
    include_source = filter_sysy_header_source(include_source);
  }

  context.include_stack.insert(include_key);
  std::string include_output;
  bool ok = preprocess_source_impl(include_source, include_path, include_output,
                                   error, context);
  context.include_stack.erase(include_key);
  if (!ok) {
    return false;
  }

  output += include_output;
  return true;
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

  bool zero_arg_function = false;
  index = name_end;
  if (index + 1 < directive.size() && directive[index] == '(' &&
      directive[index + 1] == ')') {
    zero_arg_function = true;
    index += 2;
  }
  while (index < directive.size() &&
         std::isspace(static_cast<unsigned char>(directive[index]))) {
    ++index;
  }

  if (index >= directive.size()) {
    error = "missing replacement list for macro '" + macro_name + "'";
    return false;
  }

  std::string replacement = trim(directive.substr(index));
  MacroDef macro;
  macro.replacement = replacement;
  macro.zero_arg_function = zero_arg_function;
  macro.inject_line_number = replacement.find("__LINE__") != std::string::npos;
  macros[macro_name] = macro;
  return true;
}

bool preprocess_source_impl(const std::string& input, const std::string& source_path,
                            std::string& output, std::string& error,
                            PreprocessContext& context) {
  output.clear();
  error.clear();

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
      if (trimmed.rfind("#include", 0) == 0) {
        if (!handle_include(trimmed, source_path, output, error, context)) {
          error = "line " + std::to_string(line_number) + ": " + error;
          return false;
        }
      } else if (!handle_define(trimmed, context.macros, error)) {
        error = "line " + std::to_string(line_number) + ": " + error;
        return false;
      }
      if (has_newline) {
        output.push_back('\n');
      }
    } else {
      output += expand_identifiers(line, context.macros, line_number);
      if (has_newline) {
        output.push_back('\n');
      }
    }

    position = has_newline ? line_end + 1 : input.size();
    ++line_number;
  }

  return true;
}

}  // namespace

bool preprocess_source(const std::string& input, const std::string& source_path,
                       std::string& output, std::string& error) {
  PreprocessContext context;
  context.macros["starttime"] = MacroDef{
      "_sysy_starttime(__LINE__)", true, true};
  context.macros["stoptime"] = MacroDef{
      "_sysy_stoptime(__LINE__)", true, true};
  return preprocess_source_impl(
      input, source_path.empty() ? std::string() : normalize_path(source_path),
      output, error, context);
}

}  // namespace frontend
