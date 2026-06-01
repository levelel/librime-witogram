#ifndef RIME_WITOGRAM_H_
#define RIME_WITOGRAM_H_

#include <rime/common.h>
#include <rime/component.h>
#include <rime/gear/grammar.h>
#include <rime/resource.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sentencepiece {
class SentencePieceProcessor;
}

namespace lm {
namespace ngram {
class QuantTrieModel;
}
}

namespace rime {

class GramDb;
extern const ResourceType kGramDbType;
extern const string kGrammarDefaultLanguage;

class Config;
struct GrammarConfig;
class WitogramComponent;

enum class WitogramTokenEvidenceLevel {
  kDirectWholeWordHit = 0,
  kSplitTokenSupported,
  kNeutralMissing,
  kMixedTokenMissing,
  kSingleTokenMissing,
  kTrueOov,
};

struct WitogramScoreFeatures {
  double total_log10 = 0.0;
  double avg_log10 = 0.0;
  double eos_log10 = 0.0;
  double whole_word_log10 = 0.0;
  double char_path_log10 = 0.0;
  double char_path_avg_log10 = 0.0;
  size_t token_count = 0;
  size_t context_token_count = 0;
  size_t matched_token_count = 0;
  size_t char_path_matched_token_count = 0;
  size_t oov_token_count = 0;
  size_t char_path_oov_token_count = 0;
  bool used_bos = false;
  bool used_char_fallback = false;
  bool matched_whole_word = false;
  bool has_char_path = false;
  WitogramTokenEvidenceLevel token_evidence_level =
      WitogramTokenEvidenceLevel::kNeutralMissing;
};

class Witogram : public Grammar {
 public:
  Witogram(Config* config, WitogramComponent* component);
  virtual ~Witogram();
  double Query(const string& context,
               const string& word,
               bool is_rear) override;
  double QueryGramDb(const string& context,
                      const string& word,
                      bool is_rear) const;
  double QueryCollocation(const string& context,
                           const string& word,
                           bool is_rear) const;
  bool InterpretGrammarEvidence(const string& context,
                                const string& word,
                                bool is_rear,
                                WitogramScoreFeatures* features) const;
  bool ScoreFeatures(const string& context,
                     const string& word,
                     bool is_rear,
                     WitogramScoreFeatures* features) const;
  double ngram_weight() const;
  size_t max_context_tokens() const;

 private:
  the<GrammarConfig> config_;
  lm::ngram::QuantTrieModel* model_ = nullptr;
  GramDb* gram_db_ = nullptr;
  std::unique_ptr<sentencepiece::SentencePieceProcessor> bpe_processor_;
  bool use_bpe_ = false;

  std::vector<std::string> Tokenize(const string& text) const;
};

class WitogramComponent : public Grammar::Component {
 public:
  WitogramComponent();
  virtual ~WitogramComponent();

  Witogram* Create(Config* config) override;

  lm::ngram::QuantTrieModel* GetModel(const string& language);
  GramDb* GetGramDb(const string& language);

 private:
  map<string, the<lm::ngram::QuantTrieModel>> model_by_language_;
  map<string, the<GramDb>> gram_db_by_language_;
  std::mutex mutex_;
};

}  // namespace rime

#endif  // RIME_WITOGRAM_H_
