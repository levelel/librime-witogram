#ifndef RIME_GRAM_DB_H_
#define RIME_GRAM_DB_H_

#include <marisa.h>
#include <darts.h>
#include <rime/resource.h>
#include <rime/dict/mapped_file.h>
#include <vector>

namespace rime {

namespace grammar {

// Old format metadata
struct MetadataV1 {
  static const int kFormatMaxLength = 32;
  char format[kFormatMaxLength];
  uint32_t db_checksum;
  uint32_t double_array_size;
  OffsetPtr<char> double_array;
};

// New format metadata
struct MetadataV2 {
  static const int kFormatMaxLength = 32;
  char format[kFormatMaxLength];
  uint32_t db_checksum;
  uint32_t trie_size;
  OffsetPtr<char> trie_data;
  uint32_t weights_size;
  OffsetPtr<int> weights_data;
};

}  // namespace grammar

class GramDb : public MappedFile {
 public:
  struct Match {
    int value;
    size_t length;
  };
  static constexpr int kMaxResults = 8;
  static constexpr double kValueScale = 10000;

  GramDb(const path& file_path)
      : MappedFile(file_path),
        darts_trie_(new Darts::DoubleArray),
        marisa_trie_(new marisa::Trie) {}

  bool Load();
  bool Save();
  bool Build(const vector<pair<string, double>>& data);
  bool UpgradeToMarisa();
  int Lookup(const string& context,
             const string& word,
             Match results[kMaxResults]);

 private:
  the<Darts::DoubleArray> darts_trie_;
  the<marisa::Trie> marisa_trie_;
  bool is_marisa_ = false;
  
  grammar::MetadataV1* metadata_v1_ = nullptr;
  grammar::MetadataV2* metadata_v2_ = nullptr;
  const int* weights_ = nullptr;
};

}  // namespace rime

#endif  // RIME_GRAM_DB_H_
