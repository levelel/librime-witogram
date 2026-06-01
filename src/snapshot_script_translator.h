#ifndef RIME_SNAPSHOT_SCRIPT_TRANSLATOR_H_
#define RIME_SNAPSHOT_SCRIPT_TRANSLATOR_H_

#include <rime/gear/script_translator.h>

namespace rime {

class SnapshotScriptTranslator : public ScriptTranslator {
 public:
  SnapshotScriptTranslator(const Ticket& ticket);

  virtual an<Translation> Query(const string& input,
                                const Segment& segment) override;

 private:
  void DumpLocalSnapshot(const string& input,
                         const string& preceding_text,
                         const vector<an<Candidate>>& candidates);

  bool dump_local_snapshot_ = false;
  string local_snapshot_path_;
};

}  // namespace rime

#endif  // RIME_SNAPSHOT_SCRIPT_TRANSLATOR_H_
