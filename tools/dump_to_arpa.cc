#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <cmath>
#include <iomanip>
#include <rime/resource.h>
#include "gram_db.h"
#include "gram_encoding.h"

using namespace rime;

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
      std::string token(p, next_p);
      
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

  // Find max score to normalize to <= 0.0
  float max_score = -1e9f;
  for (int i = 1; i <= max_order; ++i) {
    for (const auto& kv : ngrams[i]) {
      if (kv.second > max_score) {
        max_score = kv.second;
      }
    }
  }
  
  // Normalize
  for (int i = 1; i <= max_order; ++i) {
    for (auto& kv : ngrams[i]) {
      kv.second = kv.second - max_score;
    }
  }
  std::cout << "Max score was: " << max_score << ". Normalized all scores by subtracting it." << std::endl;

  // Generate missing 1-grams
  vocab.insert("<unk>");
  vocab.insert("<s>");
  vocab.insert("</s>");
  
  for (const auto& token : vocab) {
    if (ngrams[1].find(token) == ngrams[1].end()) {
      ngrams[1][token] = -10.0f - max_score; // Ensure it's very low and normalized
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
        out << "\t0.0"; // Default backoff
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
