//
// Copyright RIME Developers
// Distributed under GPLv3
//

#include <rime/common.h>
#include <rime/registry.h>
#include <rime_api.h>
#include "snapshot_script_translator.h"
#include "witogram.h"

static void rime_witogram_initialize() {
  using namespace rime;

  LOG(INFO) << "registering components from module 'witogram'.";
  Registry& r = Registry::instance();
  r.Register("grammar", new WitogramComponent);
  r.Register("snapshot_script_translator",
             new Component<SnapshotScriptTranslator>);
}

static void rime_witogram_finalize() {}

RIME_REGISTER_MODULE(witogram)
