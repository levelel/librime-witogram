#ifndef RIME_WITOGRAM_H_
#define RIME_WITOGRAM_H_

#include <rime/common.h>
#include <rime/component.h>
#include <rime/resource.h>
#include <rime/gear/grammar.h>

namespace lm {
namespace ngram {
class QuantTrieModel;
}
}

namespace rime {

extern const ResourceType kGramDbType;
extern const string kGrammarDefaultLanguage;

class Config;
struct GrammarConfig;
class WitogramComponent;

class Witogram : public Grammar {
 public:
  Witogram(Config* config, WitogramComponent* component);
  virtual ~Witogram();
  double Query(const string& context,
               const string& word,
               bool is_rear) override;

 private:
  the<GrammarConfig> config_;
  lm::ngram::QuantTrieModel* model_ = nullptr;
};

class WitogramComponent : public Grammar::Component {
 public:
  WitogramComponent();
  virtual ~WitogramComponent();

  Witogram* Create(Config* config) override;

  lm::ngram::QuantTrieModel* GetModel(const string& language);

 private:
  map<string, the<lm::ngram::QuantTrieModel>> model_by_language_;
};

}  // namespace rime

#endif  // RIME_WITOGRAM_H_
