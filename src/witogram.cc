#include "witogram.h"
#include <algorithm>
#include <vector>
#include <rime/config.h>
#include <rime/resource.h>
#include <rime/service.h>
#include <utf8.h>
#include "lm/model.hh"
#include "lm/state.hh"

namespace rime {

namespace {

constexpr double kLn10 = 2.302585092994046;
constexpr double kWholeWordBlend = 0.60;
constexpr double kCharPathBlend = 0.40;
constexpr double kNeutralMissingUnknownPenaltyScale = 1.0;
constexpr double kMixedTokenMissingUnknownPenaltyScale = 0.75;
constexpr double kSingleTokenMissingUnknownPenaltyScale = 0.75;

std::vector<string> SplitUtf8Tokens(const string& text) {
  std::vector<string> tokens;
  const char* p = text.c_str();
  const char* end = p + text.length();
  while (p < end) {
    const char* next_p = p;
    utf8::unchecked::next(next_p);
    if (next_p > end) {
      next_p = end;
    }
    tokens.emplace_back(p, next_p);
    p = next_p;
  }
  return tokens;
}

void AppendTokenScore(const lm::ngram::QuantTrieModel* model,
                      const string& token,
                      lm::ngram::State* state,
                      double* total_log10,
                      size_t* matched_token_count,
                      size_t* oov_token_count) {
  const auto& vocab = model->GetVocabulary();
  lm::WordIndex wid = vocab.Index(token);
  if (wid == vocab.NotFound()) {
    ++(*oov_token_count);
  } else {
    ++(*matched_token_count);
  }
  lm::ngram::State out;
  *total_log10 += model->Score(*state, wid, out);
  *state = out;
}

double ScoreUnknownTokenUnigram(const lm::ngram::QuantTrieModel* model) {
  const auto& vocab = model->GetVocabulary();
  lm::ngram::State state;
  lm::ngram::State out;
  model->NullContextWrite(&state);
  return model->Score(state, vocab.NotFound(), out);
}

void AppendNeutralMissingTokenScore(const lm::ngram::QuantTrieModel* model,
                                    const string& token,
                                    lm::ngram::State* state,
                                    double* total_log10,
                                    size_t* matched_token_count,
                                    size_t* oov_token_count,
                                    double unknown_unigram_log10,
                                    double unknown_penalty_scale) {
  const auto& vocab = model->GetVocabulary();
  lm::WordIndex wid = vocab.Index(token);
  if (wid == vocab.NotFound()) {
    ++(*oov_token_count);
    *total_log10 += unknown_unigram_log10 * unknown_penalty_scale;
    return;
  }
  ++(*matched_token_count);
  lm::ngram::State out;
  *total_log10 += model->Score(*state, wid, out);
  *state = out;
}

void AdvanceState(const lm::ngram::QuantTrieModel* model,
                  const string& token,
                  lm::ngram::State* state) {
  const auto& vocab = model->GetVocabulary();
  lm::WordIndex wid = vocab.Index(token);
  lm::ngram::State out;
  model->Score(*state, wid, out);
  *state = out;
}

}  // namespace

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

double Witogram::ngram_weight() const {
  return config_ ? config_->ngram_weight : 1.0;
}

size_t Witogram::max_context_tokens() const {
  if (!model_) {
    return 0;
  }
  return static_cast<size_t>(std::max(0, model_->Order() - 1));
}

bool Witogram::InterpretGrammarEvidence(const string& context,
                                        const string& word,
                                        bool is_rear,
                                        WitogramScoreFeatures* features) const {
  if (!features) {
    return false;
  }
  *features = WitogramScoreFeatures{};
  if (!model_ || word.empty()) {
    return false;
  }

  const int max_context_order = static_cast<int>(max_context_tokens());
  auto context_tokens = SplitUtf8Tokens(context);
  if (static_cast<int>(context_tokens.size()) > max_context_order) {
    context_tokens.erase(context_tokens.begin(),
                         context_tokens.end() - max_context_order);
  }
  features->context_token_count = context_tokens.size();

  lm::ngram::State state;
  model_->BeginSentenceWrite(&state);
  features->used_bos = true;

  for (const auto& token : context_tokens) {
    AdvanceState(model_, token, &state);
  }

  const auto& vocab = model_->GetVocabulary();
  const auto word_tokens = SplitUtf8Tokens(word);
  features->token_count = std::max<size_t>(1, word_tokens.size());

  lm::ngram::State char_state = state;
  double char_total_log10 = 0.0;
  size_t char_matched_token_count = 0;
  size_t char_oov_token_count = 0;
  for (const auto& token : word_tokens) {
    AppendTokenScore(model_, token, &char_state, &char_total_log10,
                     &char_matched_token_count, &char_oov_token_count);
  }
  if (is_rear) {
    lm::ngram::State char_out;
    char_total_log10 += model_->Score(char_state, vocab.EndSentence(), char_out);
  }
  features->has_char_path = true;
  features->char_path_log10 = char_total_log10;
  features->char_path_avg_log10 =
      char_total_log10 / static_cast<double>(features->token_count);
  features->char_path_matched_token_count = char_matched_token_count;
  features->char_path_oov_token_count = char_oov_token_count;
  double neutral_char_total_log10 = char_total_log10;
  const bool mixed_token_missing =
      features->token_count > 1 && char_matched_token_count > 0 &&
      char_oov_token_count == 1;
  if (char_oov_token_count > 0 &&
      (char_matched_token_count > 0 || features->token_count == 1)) {
    const double unknown_unigram_log10 = ScoreUnknownTokenUnigram(model_);
    const double unknown_penalty_scale =
        features->token_count == 1
            ? kSingleTokenMissingUnknownPenaltyScale
            : (mixed_token_missing ? kMixedTokenMissingUnknownPenaltyScale
                                   : kNeutralMissingUnknownPenaltyScale);
    lm::ngram::State neutral_state = state;
    neutral_char_total_log10 = 0.0;
    size_t neutral_matched_token_count = 0;
    size_t neutral_oov_token_count = 0;
    for (const auto& token : word_tokens) {
      AppendNeutralMissingTokenScore(model_, token, &neutral_state,
                                     &neutral_char_total_log10,
                                     &neutral_matched_token_count,
                                     &neutral_oov_token_count,
                                     unknown_unigram_log10,
                                     unknown_penalty_scale);
    }
    if (is_rear) {
      lm::ngram::State neutral_out;
      neutral_char_total_log10 +=
          model_->Score(neutral_state, vocab.EndSentence(), neutral_out);
    }
  }

  lm::WordIndex word_wid = vocab.Index(word);
  double total_log10 = char_total_log10;
  if (word_wid != vocab.NotFound()) {
    features->matched_whole_word = true;
    features->token_evidence_level =
        WitogramTokenEvidenceLevel::kDirectWholeWordHit;
    lm::ngram::State word_state = state;
    lm::ngram::State out;
    double whole_word_log10 = model_->Score(word_state, word_wid, out);
    word_state = out;
    if (is_rear) {
      lm::ngram::State eos_out;
      whole_word_log10 += model_->Score(word_state, vocab.EndSentence(), eos_out);
    }
    features->whole_word_log10 = whole_word_log10;
    total_log10 =
        kWholeWordBlend * whole_word_log10 + kCharPathBlend * char_total_log10;
    features->matched_token_count = std::max<size_t>(1, char_matched_token_count);
    features->oov_token_count = char_oov_token_count;
  } else {
    features->used_char_fallback = true;
    features->matched_token_count = char_matched_token_count;
    features->oov_token_count = char_oov_token_count;
    if (char_oov_token_count == 0) {
      features->token_evidence_level =
          WitogramTokenEvidenceLevel::kSplitTokenSupported;
      features->used_char_fallback = false;
      features->oov_token_count = 0;
    } else if (char_matched_token_count > 0) {
      // Under Wanxiang's split-token grammar, a missing whole-word with some
      // token support can be neutral absence of evidence rather than a strong
      // negative signal.
      features->token_evidence_level =
          mixed_token_missing
              ? WitogramTokenEvidenceLevel::kMixedTokenMissing
              : WitogramTokenEvidenceLevel::kNeutralMissing;
      features->matched_token_count =
          std::max<size_t>(features->matched_token_count, size_t{1});
      features->used_char_fallback = false;
      features->oov_token_count = 0;
      total_log10 = std::max(total_log10, neutral_char_total_log10);
    } else if (features->token_count == 1) {
      // Distinguish a missing single-token unigram from broader multi-token
      // neutral-missing cases so downstream logic can observe it separately
      // without reclassifying it as true OOV penalty.
      features->token_evidence_level =
          WitogramTokenEvidenceLevel::kSingleTokenMissing;
      features->matched_token_count =
          std::max<size_t>(features->matched_token_count, size_t{1});
      features->used_char_fallback = false;
      features->oov_token_count = 0;
      total_log10 = std::max(total_log10, neutral_char_total_log10);
    } else {
      features->token_evidence_level = WitogramTokenEvidenceLevel::kTrueOov;
    }
  }

  if (is_rear) {
    features->eos_log10 = 0.0;
  }
  features->total_log10 = total_log10;
  features->avg_log10 =
      total_log10 / static_cast<double>(features->token_count);
  return true;
}

bool Witogram::ScoreFeatures(const string& context,
                             const string& word,
                             bool is_rear,
                             WitogramScoreFeatures* features) const {
  return InterpretGrammarEvidence(context, word, is_rear, features);
}

double Witogram::Query(const string& context,
                       const string& word,
                       bool is_rear) {
  WitogramScoreFeatures features;
  if (!ScoreFeatures(context, word, is_rear, &features)) {
    return 0.0;
  }
  return features.total_log10 * ngram_weight();
}

WitogramComponent::WitogramComponent() {}

WitogramComponent::~WitogramComponent() {}

Witogram* WitogramComponent::Create(Config* config) {
  return new Witogram(config, this);
}

lm::ngram::QuantTrieModel* WitogramComponent::GetModel(const string& language) {
  std::lock_guard<std::mutex> lock(mutex_);
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
