#include <iostream>
#include <rime/resource.h>
#include "gram_db.h"
#include "gram_encoding.h"
#include <darts.h>

using namespace rime;

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: dump_grammar <gram_file>" << std::endl;
    return 1;
  }

  string file_path = argv[1];
  GramDb db{path(file_path)};

  if (!db.Load()) {
    std::cerr << "Failed to load " << file_path << std::endl;
    return 1;
  }

  // we can use the same DFS logic inside gram_db.cc, but we need access to it.
  // Actually we can just write a separate program or modify convert_grammar to also dump plain text.
  return 0;
}
