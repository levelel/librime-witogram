#include "gram_encoding.h"
#include "witogram.h"
#include <algorithm>
#include <rime/config.h>
#include <rime/resource.h>
#include <rime/service.h>
#include <utf8.h>
#include "lm/model.hh"
#include "lm/state.hh"

namespace rime {

struct GrammarConfig {
  double ngram_weight = 1.0;  // Interpolation weight for KenLM log prob
};

const ResourceType kGramDbType = {"gram_db", "", ".klm"};
const string kGrammarDefaultLanguage = "zh-hant";

Witogram::Witogram(Config* config, WitogramComponent* component)
    : config_(std::make_unique<GrammarConfig>()) {
  string language;
  if (config) {
    if (config->GetString("grammar/language", &language)) {
      LOG(INFO) << "use grammar: " << language;
    } else {
      return;
    }
    config->GetDouble("grammar/weight", &config_->ngram_weight);
  }
  if (!language.empty()) {
    model_ = component->GetModel(language);
  }
}

Witogram::~Witogram() {}

inline static const char* str_begin(const string& str) {
  return str.c_str();
}

inline static const char* str_end(const string& str) {
  return str.c_str() + str.length();
}

inline static const char* last_n_unicode(const string& str,
                                         int max,
                                         int& out_count) {
  const char* begin = str_begin(str);
  const char* p = str_end(str);
  out_count = 0;
  while (p != begin && out_count < max) {
    utf8::unchecked::prior(p);
    ++out_count;
  }
  return p;
}

inline static const char* first_n_unicode(const string& str,
                                          int max,
                                          int& out_count) {
  const char* p = str_begin(str);
  const char* end = str_end(str);
  out_count = 0;
  while (p != end && out_count < max) {
    utf8::unchecked::next(p);
    ++out_count;
  }
  return p;
}

double Witogram::Query(const string& context,
                       const string& word,
                       bool is_rear) {
  if (!model_ || word.empty()) {
    return 0.0;
  }
  
  // Use KenLM max order
  int n = model_->Order() - 1;
  int context_len = 0;
  string context_query = grammar::encode(
      last_n_unicode(context, n, context_len),
      str_end(context));
      
  string word_query = grammar::encode(
      str_begin(word),
      str_end(word));

  // Build KenLM state from context
  lm::ngram::State state;
  model_->NullContextWrite(&state);
  
  const char* p = str_begin(context_query);
  const char* end = str_end(context_query);
  while (p < end) {
    const char* next_p = grammar::next_unicode(p);
    if (next_p > end) next_p = end;
    std::string token(p, next_p);
    lm::ngram::State out;
    lm::WordIndex wid = model_->GetVocabulary().Index(token);
    model_->Score(state, wid, out);
    state = out;
    p = next_p;
  }

  // Score word tokens
  double total_prob = 0.0;
  
  p = str_begin(word_query);
  end = str_end(word_query);
  
  while (p < end) {
    const char* next_p = grammar::next_unicode(p);
    if (next_p > end) next_p = end;
    std::string token(p, next_p);
    
    lm::ngram::State out;
    lm::WordIndex wid = model_->GetVocabulary().Index(token);
    double prob = model_->Score(state, wid, out);
    total_prob += prob;
    
    state = out;
    p = next_p;
  }

  if (is_rear) {
    lm::ngram::State out;
    lm::WordIndex wid = model_->GetVocabulary().Index("</s>"); // Use standard </s> for sentence end
    double prob = model_->Score(state, wid, out);
    total_prob += prob;
  }

  double result = total_prob * config_->ngram_weight;
  
  DLOG(INFO) << "context = " << context << ", word = " << word
             << " / prob = " << total_prob << " / result = " << result;
  return result;
}

WitogramComponent::WitogramComponent() {}

WitogramComponent::~WitogramComponent() {}

Witogram* WitogramComponent::Create(Config* config) {
  return new Witogram(config, this);
}

lm::ngram::QuantTrieModel* WitogramComponent::GetModel(const string& language) {
  auto& loaded = model_by_language_[language];
  if (!loaded) {
    the<ResourceResolver> resolver(
        Service::instance().CreateResourceResolver(kGramDbType));
    string path = resolver->ResolvePath(language).string();
    try {
      loaded = std::make_unique<lm::ngram::QuantTrieModel>(path.c_str());
      LOG(INFO) << "successfully loaded KenLM gram db: " << path;
    } catch (const std::exception& e) {
      LOG(ERROR) << "failed to load KenLM database: " << language << ", error: " << e.what();
      return nullptr;
    }
  }
  return loaded.get();
}

}  // namespace rime
