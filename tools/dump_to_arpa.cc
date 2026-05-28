#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <cmath>
#include <iomanip>
#include <utf8.h>
#include <rime/resource.h>
#include "gram_db.h"
#include "gram_encoding.h"

using namespace rime;

string decode(const char* begin, const char* end) {
  string decoded_str;
  for (auto p = begin; p < end; ) {
    unsigned char c1 = *p;
    if ((c1 & 0x80) == 0) {
      if (c1 == 0) {
        decoded_str += '\0';
        p++;
      } else {
        decoded_str += *p++;
      }
    } else if ((c1 & 0xF0) == 0xE0) {
      int bytes_to_decode = (c1 & 0x0F);
      p++;
      uint32_t u = 0;
      for (int i = 0; i < bytes_to_decode; i++) {
        u = (u << 7) | ((unsigned char)(*p++) & 0x7F);
      }
      char utf8_buf[5] = {0};
      utf8::unchecked::append(u, utf8_buf);
      decoded_str += utf8_buf;
    } else {
      uint32_t u = 0;
      if (c1 == 0xE1) {
        p++;
        unsigned char c2 = *p++;
        u = ((c2 - 0x40) << 8);
      } else {
        unsigned char c2 = *p++;
        unsigned char c3 = *p++;
        u = ((c2 - 0x40) << 8);
        u |= c3;
      }
      char utf8_buf[5] = {0};
      utf8::unchecked::append(u, utf8_buf);
      decoded_str += utf8_buf;
    }
  }
  return decoded_str;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: dump_to_arpa <gram_file> <out_arpa_file>" << std::endl;
    return 1;
  }

  string file_path = argv[1];
  string out_path = argv[2];
  GramDb db{path(file_path)};

  if (!db.Load()) {
    std::cerr << "Failed to load " << file_path << std::endl;
    return 1;
  }

  std::vector<std::pair<std::string, int>> extracted;
  db.ExtractAll(extracted);

  std::map<int, std::unordered_map<std::string, float>> ngrams;
  int max_order = 0;
  std::set<std::string> vocab;

  for (const auto& kv : extracted) {
    std::string space_separated;
    int count = 0;
    const char* end = kv.first.c_str() + kv.first.length();
    bool has_control = false;
    for (const char* p = kv.first.c_str(); p < end; ) {
      const char* next_p = grammar::next_unicode(p);
      if (next_p > end) next_p = end;
      std::string encoded_token(p, next_p);
      std::string token = decode(encoded_token.c_str(), encoded_token.c_str() + encoded_token.length());
      
      // Check for control characters or whitespace in token
      for (char c : token) {
        if ((unsigned char)c <= 32 || c == 127) {
          has_control = true;
          break;
        }
      }
      if (has_control) break;

      if (!space_separated.empty()) space_separated += " ";
      space_separated += token;
      vocab.insert(token);
      count++;
      p = next_p;
    }
    
    if (has_control || count == 0) continue;

    
    // Rime stores scores which can be positive (e.g. log frequencies or scaled probabilities).
    // ARPA requires log_10(prob) <= 0.0. We will normalize them later.
    float score = (kv.second / 10000.0f);
    
    ngrams[count][space_separated] = score;
    if (count > max_order) {
      max_order = count;
    }
  }

  // Ensure all prefixes exist
  for (int i = max_order; i > 1; --i) {
    std::unordered_map<std::string, float> missing_prefixes;
    for (const auto& kv : ngrams[i]) {
      // Get the prefix by removing the last token
      std::string ngram = kv.first;
      size_t last_space = ngram.find_last_of(' ');
      if (last_space != std::string::npos) {
        std::string prefix = ngram.substr(0, last_space);
        if (ngrams[i-1].find(prefix) == ngrams[i-1].end() && missing_prefixes.find(prefix) == missing_prefixes.end()) {
          missing_prefixes[prefix] = -10.0f; // Arbitrary low score
        }
      }
    }
    // Insert missing prefixes into ngrams[i-1]
    for (const auto& p_kv : missing_prefixes) {
      ngrams[i-1][p_kv.first] = p_kv.second;
    }
  }

  // Find max score to normalize to <= 0.0, and find min score for fallback
  float max_score = -1e9f;
  float min_score = 1e9f;
  for (int i = 1; i <= max_order; ++i) {
    for (const auto& kv : ngrams[i]) {
      if (kv.second > max_score) {
        max_score = kv.second;
      }
      if (kv.second < min_score) {
        min_score = kv.second;
      }
    }
  }
  
  // Normalize
  for (int i = 1; i <= max_order; ++i) {
    for (auto& kv : ngrams[i]) {
      kv.second = kv.second - max_score;
    }
  }
  float normalized_min_score = min_score - max_score;
  std::cout << "Max score was: " << max_score << ". Normalized min score: " << normalized_min_score << std::endl;

  // Generate missing 1-grams
  vocab.insert("<unk>");
  vocab.insert("<s>");
  vocab.insert("</s>");
  
  for (const auto& token : vocab) {
    if (ngrams[1].find(token) == ngrams[1].end()) {
      // Set missing 1-grams to be slightly worse than the worst known ngram, 
      // but not so disastrously low (-24) that it overrides Rime's native weights.
      // -1.0 worse than the absolute minimum score in the corpus.
      ngrams[1][token] = normalized_min_score - 1.0f; 
    }
  }
  if (max_order < 1) max_order = 1;

  std::ofstream out(out_path);
  out << "\\data\\\n";
  for (int i = 1; i <= max_order; ++i) {
    out << "ngram " << i << "=" << ngrams[i].size() << "\n";
  }
  out << "\n";

  for (int i = 1; i <= max_order; ++i) {
    out << "\\" << i << "-grams:\n";
    for (const auto& kv : ngrams[i]) {
      // prob word [backoff]
      out << std::fixed << std::setprecision(6) << kv.second << "\t" << kv.first;
      if (i < max_order) {
        // [v6.0] Heuristic Backoff Penalty
        // We MUST provide a negative backoff weight (e.g. -1.0). 
        // If it is 0.0, backing off has no penalty, which causes shorter n-grams 
        // to unjustly outscore longer exact-match n-grams.
        out << "\t-1.0"; 
      }
      out << "\n";
    }
    out << "\n";
  }
  out << "\\end\\\n";
  out.close();

  std::cout << "Successfully dumped " << extracted.size() << " entries to " << out_path << std::endl;
  return 0;
}
