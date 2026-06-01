#include "snapshot_script_translator.h"

#include <fstream>
#include <mutex>

#include <rime/config.h>
#include <rime/context.h>
#include <rime/dict/corrector.h>
#include <rime/engine.h>
#include <rime/gear/poet.h>
#include <rime/schema.h>
#include <rime/service.h>
#include <rime/translation.h>

namespace rime {

static std::mutex g_snapshot_mutex;

namespace {

string EscapeJson(const string& s) {
  string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace

SnapshotScriptTranslator::SnapshotScriptTranslator(const Ticket& ticket)
    : ScriptTranslator(ticket) {
  if (!engine_)
    return;

  if (Config* config = engine_->schema()->config()) {
    config->GetBool(name_space_ + "/debug_dump_local_snapshot",
                    &dump_local_snapshot_);
    config->GetString(name_space_ + "/debug_local_snapshot_path",
                      &local_snapshot_path_);
    if (dump_local_snapshot_ && local_snapshot_path_.empty()) {
      local_snapshot_path_ =
          Service::instance().deployer().user_data_dir.string() +
          "\\debug\\snapshot_script_local_snapshot.jsonl";
    }
  }
}

an<Translation> SnapshotScriptTranslator::Query(const string& input,
                                                const Segment& segment) {
  auto translation = ScriptTranslator::Query(input, segment);
  if (!translation) {
    return nullptr;
  }

  if (dump_local_snapshot_ && !local_snapshot_path_.empty()) {
    vector<an<Candidate>> candidates;
    while (!translation->exhausted()) {
      auto cand = translation->Peek();
      if (cand) {
        candidates.push_back(cand);
      }
      translation->Next();
    }

    string preceding = GetPrecedingText(segment.start);
    DumpLocalSnapshot(input, preceding, candidates);

    auto result = New<rime::FifoTranslation>();
    for (auto& c : candidates) {
      result->Append(c);
    }
    return result;
  }

  return translation;
}

void SnapshotScriptTranslator::DumpLocalSnapshot(
    const string& input,
    const string& preceding_text,
    const vector<an<Candidate>>& candidates) {
  string json = "{\"input\":\"" + EscapeJson(input) + "\"";
  json += ",\"preceding_text\":\"" + EscapeJson(preceding_text) + "\"";
  json += ",\"candidates\":[";
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (i > 0)
      json += ",";
    json += "{\"text\":\"" + EscapeJson(candidates[i]->text()) + "\"}";
  }
  json += "]}\n";

  std::lock_guard<std::mutex> lock(g_snapshot_mutex);
  std::ofstream out(local_snapshot_path_, std::ios::out | std::ios::app);
  if (out.is_open()) {
    out << json;
  }
}

}  // namespace rime
