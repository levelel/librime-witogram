#include "witogram.h"
#include <algorithm>
#include <vector>
#include <rime/config.h>
#include <rime/resource.h>
#include <rime/service.h>
#include <utf8.h>
#include <sentencepiece_processor.h>
#include "octagram_gram_db.h"
#include "octagram_encoding.h"
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

inline const char* str_begin(const string& str) {
  return str.c_str();
}

inline const char* str_end(const string& str) {
  return str.c_str() + str.length();
}

inline const char* last_n_unicode(const string& str, int max, int& out_count) {
  const char* begin = str_begin(str);
  const char* p = str_end(str);
  out_count = 0;
  while (p != begin && out_count < max) {
    utf8::unchecked::prior(p);
    ++out_count;
  }
  return p;
}

inline const char* first_n_unicode(const string& str, int max, int& out_count) {
  const char* p = str_begin(str);
  const char* end = str_end(str);
  out_count = 0;
  while (p != end && out_count < max) {
    utf8::unchecked::next(p);
    ++out_count;
  }
  return p;
}

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

std::vector<string> Witogram::Tokenize(const string& text) const {
  if (use_bpe_ && bpe_processor_) {
    std::vector<string> pieces;
    bpe_processor_->Encode(text, &pieces);
    return pieces;
  }
  return SplitUtf8Tokens(text);
}

struct GrammarConfig {
  double ngram_weight = 1.0;  // Interpolation weight for KenLM log prob
  bool collocation_mode = false;
  double collocation_penalty = -12;
  double non_collocation_penalty = -12;
  double rear_penalty = -18;
};

const ResourceType kGramDbType = {"gram_db", "", ".klm"};
const ResourceType kGramDbGramType = {"gram_db_gram", "", ".gram"};
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
    config->GetBool("grammar/collocation_mode", &config_->collocation_mode);
    config->GetDouble("grammar/collocation_penalty",
                      &config_->collocation_penalty);
    config->GetDouble("grammar/non_collocation_penalty",
                      &config_->non_collocation_penalty);
    config->GetDouble("grammar/rear_penalty", &config_->rear_penalty);

    string bpe_model_path;
    if (config->GetString("grammar/bpe_model", &bpe_model_path)) {
      auto resolver = Service::instance().CreateResourceResolver(
          ResourceType{"bpe_model", "", ".model"});
      string resolved = resolver->ResolvePath(bpe_model_path).string();
      bpe_processor_ = std::make_unique<sentencepiece::SentencePieceProcessor>();
      auto status = bpe_processor_->Load(resolved);
      if (status.ok()) {
        use_bpe_ = true;
        LOG(INFO) << "BPE model loaded: " << resolved;
      } else {
        LOG(ERROR) << "Failed to load BPE model: " << resolved
                    << " (" << status.ToString() << ")";
      }
    }
  }
  if (!language.empty()) {
    try {
      model_ = component->GetModel(language);
      if (model_) {
        LOG(INFO) << "successfully loaded KenLM model: " << language;
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "failed to load KenLM model: " << language << ": " << e.what();
    }

    // Try loading GramDb (.gram file) as well
    gram_db_ = component->GetGramDb(language);
    if (gram_db_) {
      LOG(INFO) << "successfully loaded GramDb for: " << language;
    } else {
      LOG(INFO) << "GramDb not available for: " << language
                << " (resolver may not find .gram file)";
    }
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
  auto context_tokens = Tokenize(context);
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
  const auto word_tokens = Tokenize(word);
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
  // GramDb path: exact same semantics as original octagram
  if (gram_db_) {
    return QueryGramDb(context, word, is_rear);
  }
  if (config_->collocation_mode) {
    return QueryCollocation(context, word, is_rear);
  }
  WitogramScoreFeatures features;
  if (!ScoreFeatures(context, word, is_rear, &features)) {
    return 0.0;
  }
  return features.total_log10 * ngram_weight();
}

double Witogram::QueryCollocation(const string& context,
                                   const string& word,
                                   bool is_rear) const {
  if (!model_ || context.empty()) {
    return config_->non_collocation_penalty;
  }

  auto context_tokens = Tokenize(context);
  auto word_tokens = Tokenize(word);
  const auto& vocab = model_->GetVocabulary();

  // Try multiple context suffix lengths (like octagram's prefix search)
  int max_ctx_len = (std::min)((size_t)4, context_tokens.size());
  bool collocation_found = false;

  for (int ctx_len = max_ctx_len; ctx_len >= 1 && !collocation_found; --ctx_len) {
    lm::ngram::State state = model_->BeginSentenceState();
    bool ctx_ok = true;
    size_t start = context_tokens.size() - ctx_len;
    for (size_t i = start; i < context_tokens.size(); ++i) {
      lm::WordIndex wid = vocab.Index(context_tokens[i]);
      if (wid == vocab.NotFound()) { ctx_ok = false; break; }
      model_->FullScore(state, wid, state);
    }
    if (!ctx_ok) continue;

    for (size_t wlen = 1; wlen <= word_tokens.size() && !collocation_found; ++wlen) {
      lm::ngram::State ws = state;
      bool w_ok = true;
      float min_prob = 0.0f;
      for (size_t j = 0; j < wlen; ++j) {
        lm::WordIndex wid = vocab.Index(word_tokens[j]);
        if (wid == vocab.NotFound()) { w_ok = false; break; }
        auto ret = model_->FullScore(ws, wid, ws);
        if (j == 0) min_prob = ret.prob;
        else if (ret.prob < min_prob) min_prob = ret.prob;
      }
      // All word tokens scored without NotFound → valid collocation
      if (w_ok) {
        collocation_found = true;
      }
    }
  }

  double result = collocation_found ? config_->collocation_penalty
                                    : config_->non_collocation_penalty;

  if (is_rear && !word_tokens.empty()) {
    lm::ngram::State word_state = model_->BeginSentenceState();
    bool word_hits = true;
    for (const auto& t : word_tokens) {
      lm::WordIndex wid = vocab.Index(t);
      if (wid == vocab.NotFound()) { word_hits = false; break; }
      model_->FullScore(word_state, wid, word_state);
    }
    if (word_hits) {
      auto rear_ret = model_->FullScore(word_state, vocab.EndSentence(),
                                         word_state);
      if (rear_ret.ngram_length >= (unsigned)(word_tokens.size() + 1)) {
        result = std::min(result, config_->rear_penalty);
      }
    }
  }

  return result;
}

double Witogram::QueryGramDb(const string& context,
                              const string& word,
                              bool is_rear) const {
  if (!gram_db_ || context.empty()) {
    return -12;
  }

  constexpr int kMaxEncodedUnicode = 8;
  constexpr double kValueScale = 10000;
  constexpr double kCollocationPenalty = -12;
  constexpr double kWeakCollocationPenalty = -24;
  constexpr double kNonCollocationPenalty = -12;
  constexpr double kRearPenalty = -18;

  double result = kNonCollocationPenalty;
  GramDb::Match matches[GramDb::kMaxResults];
  int n = (std::min)(kMaxEncodedUnicode, 3);

  int context_len = 0;
  string context_query = grammar::encode(
      last_n_unicode(context, n, context_len),
      str_end(context));
  int word_query_len = 0;
  string word_query = grammar::encode(
      str_begin(word),
      first_n_unicode(word, n, word_query_len));

  for (const char* context_ptr = str_begin(context_query);
       context_len > 0;
       --context_len, context_ptr = grammar::next_unicode(context_ptr)) {
    int num_results = gram_db_->Lookup(context_ptr, word_query, matches);
    for (auto i = 0; i < num_results; ++i) {
      const auto& match(matches[i]);
      const int match_len = grammar::unicode_length(word_query, match.length);
      const int collocation_len = context_len + match_len;
      double scaled = match.value >= 0
          ? double(match.value) / kValueScale
          : -1;
      double penalty = (collocation_len >= 3 ||
                        (context_ptr == str_begin(context_query) &&
                         match.length == word_query.length()))
          ? kCollocationPenalty
          : kWeakCollocationPenalty;
      double new_value = scaled + penalty;
      if (new_value > result) {
        result = new_value;
      }
    }
  }

  if (is_rear) {
    int word_len = utf8::unchecked::distance(word.c_str(),
                                             word.c_str() + word.length());
    if (word_query_len == word_len &&
        gram_db_->Lookup(word_query, "$", matches) > 0) {
      double scaled = matches[0].value >= 0
          ? double(matches[0].value) / kValueScale
          : -1;
      double new_value = scaled + kRearPenalty;
      if (new_value > result) {
        result = new_value;
      }
    }
  }

  return result;
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

GramDb* WitogramComponent::GetGramDb(const string& language) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& loaded = gram_db_by_language_[language];
  if (!loaded) {
    the<ResourceResolver> resolver(
        Service::instance().CreateResourceResolver(kGramDbGramType));
    auto gram_path = resolver->ResolvePath(language);
    loaded = std::make_unique<GramDb>(gram_path);
    if (!loaded->Load()) {
      LOG(ERROR) << "failed to load GramDb: " << language;
      gram_db_by_language_.erase(language);
      return nullptr;
    }
    LOG(INFO) << "successfully loaded GramDb: " << gram_path.string();
  }
  return loaded.get();
}

}  // namespace rime
