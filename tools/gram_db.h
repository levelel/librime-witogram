#ifndef RIME_GRAM_DB_H_
#define RIME_GRAM_DB_H_

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
        darts_trie_(new Darts::DoubleArray) {}

  bool Load();
  void ExtractAll(std::vector<std::pair<std::string, int>>& extracted);

 private:
  the<Darts::DoubleArray> darts_trie_;
  grammar::MetadataV1* metadata_v1_ = nullptr;
};

}  // namespace rime

#endif  // RIME_GRAM_DB_H_
