#include <iostream>
#include <rime/resource.h>
#include "gram_db.h"

using namespace rime;

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: convert_grammar <gram_file>" << std::endl;
    return 1;
  }

  string file_path = argv[1];
  GramDb db{path(file_path)};

  if (!db.Load()) {
    std::cerr << "Failed to load " << file_path << std::endl;
    return 1;
  }

  if (!db.UpgradeToMarisa()) {
    std::cerr << "Failed to upgrade or already upgraded." << std::endl;
    return 1;
  }

  if (!db.Save()) {
    std::cerr << "Failed to save upgraded grammar." << std::endl;
    return 1;
  }

  std::cout << "Successfully converted to Marisa format: " << file_path << std::endl;
  return 0;
}