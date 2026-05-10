#include "gram_db.h"
#include <boost/algorithm/string.hpp>
#include <cmath>
#include <cstdio>
#include <rime/resource.h>
#include <rime/dict/mapped_file.h>

namespace rime {

const string kGrammarFormatV1 = "Rime::Grammar/1.0";
const string kGrammarFormatPrefix = "Rime::Grammar/";

void DfsDarts(const Darts::Details::DoubleArrayUnit* array, size_t size, size_t node_pos, std::string current_key, std::vector<std::pair<std::string, int>>& extracted) {
    auto unit = array[node_pos];
    if (unit.has_leaf()) {
        size_t leaf_pos = node_pos ^ unit.offset();
        if (leaf_pos < size) {
            auto leaf = array[leaf_pos];
            extracted.push_back({current_key, leaf.value()});
        }
    }
    for (int i = 0; i < 256; ++i) {
        size_t child_pos = node_pos ^ unit.offset() ^ i;
        if (child_pos < size) {
            auto child = array[child_pos];
            if (child.label() == static_cast<Darts::Details::id_type>(i)) {
                DfsDarts(array, size, child_pos, current_key + static_cast<char>(i), extracted);
            }
        }
    }
}

void GramDb::ExtractAll(std::vector<std::pair<std::string, int>>& extracted) {
  if (!darts_trie_->total_size()) return;
  const auto* array = static_cast<const Darts::Details::DoubleArrayUnit*>(darts_trie_->array());
  size_t size = darts_trie_->size();
  DfsDarts(array, size, 0, "", extracted);
}

bool GramDb::Load() {
  LOG(INFO) << "loading gram db: " << file_path();

  if (IsOpen())
    Close();

  if (!OpenReadOnly()) {
    LOG(ERROR) << "error opening gram db '" << file_path() << "'.";
    return false;
  }

  // Peek the format string
  char* format_ptr = Find<char>(0);
  if (!format_ptr) {
    LOG(ERROR) << "metadata not found.";
    Close();
    return false;
  }

  string format_str(format_ptr);
  if (boost::starts_with(format_str, kGrammarFormatPrefix)) {
    metadata_v1_ = Find<grammar::MetadataV1>(0);
    
    char* array = metadata_v1_->double_array.get();
    if (!array) {
      LOG(ERROR) << "double array image not found.";
      Close();
      return false;
    }
    size_t array_size = metadata_v1_->double_array_size;
    LOG(INFO) << "found double array image of size " << array_size << " (V1).";
    darts_trie_->set_array(array, array_size);
  } else {
    LOG(ERROR) << "invalid metadata format: " << format_str;
    Close();
    return false;
  }

  return true;
}

}  // namespace rime
