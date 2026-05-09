#include "gram_db.h"
#include <boost/algorithm/string.hpp>
#include <cmath>
#include <cstdio>
#include <rime/resource.h>
#include <rime/dict/mapped_file.h>

namespace rime {

const string kGrammarFormatV1 = "Rime::Grammar/1.0";
const string kGrammarFormatV2 = "Rime::MarisaGrammar/1.0";
const string kGrammarFormatPrefix = "Rime::Grammar/";
const string kMarisaFormatPrefix = "Rime::MarisaGrammar/";

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

bool GramDb::UpgradeToMarisa() {
  if (is_marisa_) return true; // Already marisa
  if (!darts_trie_->total_size()) return false;

  LOG(INFO) << "Upgrading Darts GramDb to Marisa format...";

  const auto* array = static_cast<const Darts::Details::DoubleArrayUnit*>(darts_trie_->array());
  size_t size = darts_trie_->size();

  std::vector<std::pair<std::string, int>> extracted;
  DfsDarts(array, size, 0, "", extracted);

  LOG(INFO) << "Extracted " << extracted.size() << " entries from Darts trie.";

  marisa::Keyset keyset;
  for (const auto& kv : extracted) {
    keyset.push_back(kv.first.c_str(), kv.first.length(), 1.0);
  }

  try {
    marisa_trie_->build(keyset);
  } catch (const marisa::Exception& ex) {
    LOG(ERROR) << "marisa trie build failed: " << ex.what();
    return false;
  }

  size_t num_keys = marisa_trie_->num_keys();
  vector<int> weights(num_keys, 0);

  for (const auto& kv : extracted) {
    marisa::Agent agent;
    agent.set_query(kv.first.c_str(), kv.first.length());
    if (marisa_trie_->lookup(agent)) {
      weights[agent.key().id()] = kv.second; // Already scaled
    }
  }

  size_t trie_size = marisa_trie_->io_size();
  size_t weights_size = weights.size() * sizeof(int);
  size_t image_size = trie_size + weights_size;
  const size_t kReservedSize = 1024;

  if (!Create(image_size + kReservedSize)) {
    LOG(ERROR) << "Error creating gram db file '" << file_path() << "'.";
    return false;
  }

  auto metadata = Allocate<grammar::MetadataV2>();
  if (!metadata) {
    LOG(ERROR) << "Error creating metadata in file '" << file_path() << "'.";
    return false;
  }
  metadata_v2_ = metadata;

  char* trie_array = Allocate<char>(trie_size);
  if (!trie_array) {
    LOG(ERROR) << "Error allocating trie image.";
    return false;
  }
  
  string temp_file = file_path().string() + ".tmp";
  try {
    marisa_trie_->save(temp_file.c_str());
    FILE* fp = fopen(temp_file.c_str(), "rb");
    if (fp) {
      fread(trie_array, 1, trie_size, fp);
      fclose(fp);
    }
    remove(temp_file.c_str());
  } catch (const marisa::Exception& ex) {
    LOG(ERROR) << "marisa trie save failed: " << ex.what();
    return false;
  }

  int* weights_array = Allocate<int>(weights.size());
  if (!weights_array) {
    LOG(ERROR) << "Error allocating weights image.";
    return false;
  }
  std::memcpy(weights_array, weights.data(), weights_size);

  metadata->trie_data = trie_array;
  metadata->trie_size = trie_size;
  metadata->weights_data = weights_array;
  metadata->weights_size = weights_size;

  std::strncpy(metadata->format,
               kGrammarFormatV2.c_str(),
               kGrammarFormatV2.length());
               
  is_marisa_ = true;
  darts_trie_->clear();
  metadata_v1_ = nullptr;
  weights_ = weights_array;

  LOG(INFO) << "Upgrade complete.";
  return true;
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
  if (boost::starts_with(format_str, kMarisaFormatPrefix)) {
    is_marisa_ = true;
    metadata_v2_ = Find<grammar::MetadataV2>(0);
    
    char* trie_array = metadata_v2_->trie_data.get();
    if (!trie_array) {
      LOG(ERROR) << "trie image not found.";
      Close();
      return false;
    }
    
    try {
      marisa_trie_->map(trie_array, metadata_v2_->trie_size);
    } catch (const marisa::Exception& ex) {
      LOG(ERROR) << "marisa trie map failed: " << ex.what();
      Close();
      return false;
    }

    weights_ = metadata_v2_->weights_data.get();
    if (!weights_) {
      LOG(ERROR) << "weights array not found.";
      Close();
      return false;
    }
    LOG(INFO) << "successfully loaded marisa gram db (V2).";
  } else if (boost::starts_with(format_str, kGrammarFormatPrefix)) {
    is_marisa_ = false;
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

bool GramDb::Save() {
  LOG(INFO) << "saving gram db: " << file_path();
  if (is_marisa_) {
    if (marisa_trie_->empty()) {
      LOG(ERROR) << "the trie has not been constructed!";
      return false;
    }
  } else {
    if (!darts_trie_->total_size()) {
      LOG(ERROR) << "the trie has not been constructed!";
      return false;
    }
  }
  return ShrinkToFit();
}

bool GramDb::Build(const vector<pair<string, double>>& data) {
  is_marisa_ = true; // Always build using V2 (Marisa)
  
  marisa::Keyset keyset;
  for (const auto& kv : data) {
    keyset.push_back(kv.first.c_str(), kv.first.length(), 1.0);
  }

  try {
    marisa_trie_->build(keyset);
  } catch (const marisa::Exception& ex) {
    LOG(ERROR) << "marisa trie build failed: " << ex.what();
    return false;
  }

  size_t num_keys = marisa_trie_->num_keys();
  vector<int> weights(num_keys, 0);

  for (const auto& kv : data) {
    marisa::Agent agent;
    agent.set_query(kv.first.c_str(), kv.first.length());
    if (marisa_trie_->lookup(agent)) {
      weights[agent.key().id()] = (std::max)(0, int(log(kv.second) * kValueScale));
    }
  }

  size_t trie_size = marisa_trie_->io_size();
  size_t weights_size = weights.size() * sizeof(int);
  size_t image_size = trie_size + weights_size;
  const size_t kReservedSize = 1024;

  if (!Create(image_size + kReservedSize)) {
    LOG(ERROR) << "Error creating gram db file '" << file_path() << "'.";
    return false;
  }

  auto metadata = Allocate<grammar::MetadataV2>();
  if (!metadata) {
    LOG(ERROR) << "Error creating metadata in file '" << file_path() << "'.";
    return false;
  }
  metadata_v2_ = metadata;

  char* trie_array = Allocate<char>(trie_size);
  if (!trie_array) {
    LOG(ERROR) << "Error allocating trie image.";
    return false;
  }
  
  string temp_file = file_path().string() + ".tmp";
  try {
    marisa_trie_->save(temp_file.c_str());
    FILE* fp = fopen(temp_file.c_str(), "rb");
    if (fp) {
      fread(trie_array, 1, trie_size, fp);
      fclose(fp);
    }
    remove(temp_file.c_str());
  } catch (const marisa::Exception& ex) {
    LOG(ERROR) << "marisa trie save failed: " << ex.what();
    return false;
  }

  int* weights_array = Allocate<int>(weights.size());
  if (!weights_array) {
    LOG(ERROR) << "Error allocating weights image.";
    return false;
  }
  std::memcpy(weights_array, weights.data(), weights_size);

  metadata->trie_data = trie_array;
  metadata->trie_size = trie_size;
  metadata->weights_data = weights_array;
  metadata->weights_size = weights_size;

  std::strncpy(metadata->format,
               kGrammarFormatV2.c_str(),
               kGrammarFormatV2.length());
  return true;
}

int GramDb::Lookup(const string& context,
                   const string& word,
                   Match results[kMaxResults]) {
  if (is_marisa_) {
    if (marisa_trie_->empty() || !weights_) return 0;

    string query = context + word;
    marisa::Agent agent;
    agent.set_query(query.c_str(), query.length());
    
    int count = 0;
    while (marisa_trie_->common_prefix_search(agent) && count < kMaxResults) {
      size_t match_len = agent.key().length();
      if (match_len > context.length()) {
        results[count].length = match_len - context.length();
        results[count].value = weights_[agent.key().id()];
        count++;
      }
    }
    return count;
  } else {
    if (!darts_trie_->total_size()) return 0;
    
    size_t node_pos = 0;
    size_t key_pos = 0;
    darts_trie_->traverse(context.c_str(), node_pos, key_pos);
    if (key_pos == context.length()) {
      Darts::DoubleArray::result_pair_type darts_results[kMaxResults];
      int count = darts_trie_->commonPrefixSearch(word.c_str(), darts_results, kMaxResults, 0, node_pos);
      for (int i = 0; i < count; ++i) {
        results[i].length = darts_results[i].length;
        results[i].value = darts_results[i].value;
      }
      return count;
    }
    return 0;
  }
}

}  // namespace rime
