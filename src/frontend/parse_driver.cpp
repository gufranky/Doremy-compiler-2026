#include "parse_driver.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "ast.h"
#include "preprocessor.h"

extern FILE* yyin;
extern int yylineno;
extern int yyparse();
extern CompUnit* root;

namespace frontend {
namespace {

bool load_file(const std::string& path, std::string& source) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }

  source.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
  return true;
}

bool load_stdin(std::string& source) {
  source.assign(std::istreambuf_iterator<char>(std::cin),
                std::istreambuf_iterator<char>());
  return true;
}

bool parse_preprocessed_source(const std::string& source) {
  FILE* buffer = std::tmpfile();
  if (!buffer) {
    std::cerr << "Error: Cannot create temporary parse buffer\n";
    return false;
  }

  if (!source.empty()) {
    size_t written = std::fwrite(source.data(), 1, source.size(), buffer);
    if (written != source.size()) {
      std::cerr << "Error: Cannot write temporary parse buffer\n";
      std::fclose(buffer);
      return false;
    }
  }

  std::rewind(buffer);
  root = nullptr;
  yylineno = 1;
  yyin = buffer;
  int parse_result = yyparse();
  std::fclose(buffer);
  yyin = nullptr;
  return parse_result == 0 && root != nullptr;
}

bool parse_source_impl(const std::string& source, const std::string& label,
                       const std::string& source_path) {
  std::string preprocessed;
  std::string error;
  if (!preprocess_source(source, source_path, preprocessed, error)) {
    std::cerr << "Preprocess error in " << label << ": " << error << "\n";
    return false;
  }

  return parse_preprocessed_source(preprocessed);
}

}  // namespace

bool parse_from_file(const std::string& path) {
  std::string source;
  if (!load_file(path, source)) {
    std::cerr << "Error: Cannot open file " << path << "\n";
    return false;
  }

  return parse_source_impl(source, path, path);
}

bool parse_from_stdin() {
  std::string source;
  if (!load_stdin(source)) {
    std::cerr << "Error: Cannot read from stdin\n";
    return false;
  }

  return parse_source_impl(source, "<stdin>", "");
}

}  // namespace frontend
