# `witogram_core` 闭环路线规划

## 1. 文档目的

这份文档用于承接 `route_b_implementation_plan.md` 在 `2026-05-19` 的阶段收束结论，并将后续实现路线明确固定为：

1. 不再继续“`witset_poet` 后段 patch 追平 `octagram`”。
2. 不再规划独立的 `witset_credibility` 插件。
3. 由开源 `witogram` 演进为**可独立闭环的一档核心实现**。
4. 由闭源 `witset` 作为 `witset.schema.yaml` 的唯一编排者，继续承载一档、二档、三档全部产品能力。

本文回答下面几个问题：

1. `witogram` 是否应该继续维持为一个单纯 `grammar` 组件。
2. `witogram` 如果要闭环，应该如何与 `witset` 分工而不冲突。
3. `witset` 如何在保留二档、三档全部闭源的前提下复用开源一档内核。
4. `poet` 应该归属于哪一侧，是否需要两边各维护一套。
5. 后续实施应按什么阶段推进。

## 2. 最终架构结论

### 2.1 总体判断

结论是：**可行，而且这是当前最适合同时满足“开源一档 + 闭源二三档 + 保留现有 `witset.schema.yaml` 主链路”的方案。**

但从 `2026-05-22` 起，需要把下面两条前提升级为整个文档的最高优先级约束：

1. **准确率目标优先**
   - 一档必须以“持平或超过原版 `octagram` 的准确率”为最主要目标。
   - 不允许为了更早完成开源/闭源物理分拆，而降低、推迟或弱化这一目标。
2. **边界必须正确，但分拆时机可以让位于准确率验证**
   - `witogram` 仍必须作为可独立开源给 Rime 社区使用的一档实现。
   - `witset` 的闭源逻辑仍必须在 `witogram` 侧保持隐身。
   - 但如果阶段性出现冲突，只允许牺牲“物理分拆时机与过程整洁度”，不允许牺牲准确率目标本身。

这条路线的核心不是“让一个 translator 去调用另一个 translator”，而是把 `witogram` 重构为三层：

1. `witogram_core`
   - 开源
   - 一档核心能力
2. `witogram_translator`
   - 开源
   - 面向 Rime 生态用户的独立入口
3. `witset_translator`
   - 闭源
   - 继续作为 `witset.schema.yaml` 的唯一主 translator
   - 内部直接调用 `witogram_core`

### 2.2 明确否定的几条路线

本规划明确不采用以下路线：

1. 继续让 `witogram` 只提供一个薄 `grammar` 组件。
2. 新增一个独立 `witset_credibility` 插件作为主架构。
3. 长期采用“`witset_translator` 内部调用 `witogram_translator`”的 translator 套 translator 设计。
4. 两边长期各维护一套一档 `poet` 逻辑。

原因分别是：

1. 单纯 `grammar` 组件的评分契约与当前目标不兼容，且文档与实验已证明效果不足。
2. 独立上游插件会把开源一档核心拆散，不利于 `witogram` 成为可闭环的公开实现。
3. translator 套 translator 在代码层面能做，但 `Translation` 只是候选流接口，不适合作为一档内核复用边界。
4. 两套 poet 分叉会导致一档行为漂移，最终让开源与闭源版本越来越难对齐。

## 3. 新的职责边界

### 3.1 `witogram_core` 的职责

`witogram_core` 是后续一档能力的唯一核心实现，负责：

1. `SyllableGraph` 分析
2. `path credibility / debt ledger`
3. `WordGraph` 改写
4. 一档句子搜索
5. 一档调试特征导出
6. 一档 `poet` 能力

它不负责：

1. 二档 PPL 重排
2. 三档 Beam Search / 大模型驱动引擎
3. 远程/本地模型调度
4. 私有 UI 状态和业务编排

### 3.2 `witogram_translator` 的职责

`witogram_translator` 是 `witogram_core` 面向 Rime 生态用户的公开包装器，负责：

1. 从 schema 接收配置
2. 获取输入段、前文、后文
3. 调用 `witogram_core`
4. 产出标准 Rime `Translation`
5. 提供独立开源 schema 所需的一档使用方式

它的目标是让开源 `witogram` 在**不依赖 `witset`** 的前提下独立可用。

### 3.3 `witset_translator` 的职责

`witset_translator` 继续是 `witset.schema.yaml` 的唯一主 translator，负责：

1. 解释 `llm_level_1 / 2 / 3`
2. 维护私有前后文链路
3. 维护 `SentenceCache`
4. 与 `witset_processor`、`witset_filter` 对接
5. 在一档模式下调用 `witogram_core`
6. 在二档、三档下继续走闭源编排流程

它**不应该**去调用 `witogram_translator`。

### 3.4 `poet` 的归属

后续只保留一个一档核心 `poet`，归属于 `witogram_core`，暂命名：

1. `WitogramPoet`

这个 `WitogramPoet` 由两边复用：

1. `witogram_translator -> witogram_core -> WitogramPoet`
2. `witset_translator -> witogram_core -> WitogramPoet`

因此：

1. 不再长期维护独立的一档 `WitsetPoet` 分叉版本。
2. `witset` 只保留二档、三档闭源增量逻辑。
3. 不存在两个 poet 在注册层面打架的问题，因为 poet 不是 schema 主链路里并列注册的 translator 组件。

## 4. 为什么不采用 translator 套 translator

### 4.1 代码层面可以做到

从 `librime` 组件模型看：

1. `Translator` 组件支持 `Require(name)` 后 `Create(ticket)` 实例化。
2. `Registry` 可以按名字查组件。
3. `Engine` 自己初始化 translator 时就是这套模式。

所以技术上可以在 `witset_translator` 内部手工创建一个 `witogram_translator` 并调用 `Query()`。

### 4.2 但不应作为长期架构

不推荐的原因是：

1. `Translation` 是给 `Menu` 消费的候选流接口，不是稳定的一档内核 API。
2. `witset` 在一档模式下需要的不只是最终候选文本，还需要：
   - graph
   - ledger
   - debug 特征
   - 句子对象
   - cache 写入材料
3. 如果把这些都塞到 translator 间调用里，会逼出很多 side channel，最终比直接共享 core 更脏。

因此，translator 套 translator 只可视作临时桥接手段，不应写进正式长期架构。

## 5. 如何确保 `witset.schema.yaml` 不冲突且保留全部能力

### 5.1 主链路原则

`witset.schema.yaml` 必须继续保持：

1. `witset_translator` 为唯一主 translator
2. `witset_processor` 为二档、三档交互入口
3. `witset_filter` 为二档、三档结果展示与回填入口

也就是说：

1. `witset.schema.yaml` **不应**把 `witogram_translator` 挂进 `engine/translators`
2. `witset.schema.yaml` **不应**同时让 `witset_translator` 和 `witogram_translator` 共同产出候选

### 5.2 同时注册但不冲突的方式

两个插件可以同时注册组件，但必须遵守：

1. 组件名不同
2. schema 主链路只选择一个主 translator
3. `witogram_translator` 只服务于独立开源 schema
4. `witset_translator` 在内部调用 `witogram_core`，而不是依赖 `witogram_translator`

这样可以同时满足：

1. `witogram` 对外独立可用
2. `witset` 内部复用同一个一档核心
3. 不发生候选链路混杂

### 5.3 对当前三档能力的含义

在这条分工下：

1. 一档
   - 由 `witset_translator` 调用 `witogram_core`
2. 二档
   - 继续由闭源 `witset_processor + witset_filter + AsyncPPLService + SentenceCache` 承载
3. 三档
   - 继续由闭源 `witset` 的 Beam Search / 大模型驱动链路承载

所以 `witset.schema.yaml` 现有的主产品能力可以全部保留，不需要为了开源一档而拆掉二档、三档。

## 6. 开源与闭源的严格边界

### 6.1 开源 `witogram` 允许包含的内容

开源 `witogram` 可以包含：

1. 一档候选图构建
2. 上游 debt ledger
3. 一档 `WitogramPoet`
4. 一档 transition / LM 特征
5. graph / snapshot / debug 导出
6. 独立 schema 所需的 translator 包装

### 6.2 开源 `witogram` 绝对不应包含的内容

开源仓库中不应出现：

1. `llm_level_2`
2. `llm_level_3`
3. `rc_local / rc_remote`
4. `AsyncPPLService`
5. `SentenceCache`
6. `_witset_*` property 协议
7. 私有 PPL 排序逻辑
8. 私有 Beam Search 触发和结果回填逻辑
9. 与闭源服务交互的任何协议、抽象名或预留注释

这里的要求不是“代码不复制”，而是连**思路和接口轮廓**都不应放进开源侧。

### 6.3 闭源 `witset` 保留的内容

闭源 `witset` 保留：

1. 一档调用编排
2. 二档 PPL 重排
3. 三档大模型驱动引擎
4. 所有服务健康状态、配额、刷新协议
5. `SentenceCache` 与相关调试信息
6. 所有 `_witset_*` Context property

## 7. 推荐的数据流

### 7.1 开源独立模式

开源 Rime 用户使用 `witogram` 时的数据流为：

1. `witogram_translator`
2. `SyllableGraph`
3. `witogram_core`
4. `WitogramPoet`
5. `Translation`

这条链路不依赖 `witset`。

### 7.2 闭源集成模式

`witset.schema.yaml` 下的数据流为：

1. `witset_translator`
2. 判断当前为一档 / 二档 / 三档
3. 若为一档，调用 `witogram_core`
4. `witogram_core` 内部使用 `WitogramPoet`
5. 返回句子结果与一档 debug 特征
6. `witset` 继续执行自己的 cache / UI / 二三档逻辑

关键点是：

1. `witset_translator` 调的是 `witogram_core`
2. 不是 `witogram_translator`

## 8. 推荐的文件落点

### 8.1 `witogram` 内新增/调整

推荐在 `librime/plugins/witogram` 内新增：

1. `src/witogram_core.h`
2. `src/witogram_core.cc`
3. `src/witogram_poet.h`
4. `src/witogram_poet.cc`
5. `src/witogram_translator.h`
6. `src/witogram_translator.cc`
7. `src/witogram_module.cc`

说明：

1. `witogram_module.cc` 后续负责注册 `witogram_translator`
2. 现有 `grammar_module.cc` 是否保留，不再作为主路线前提

### 8.2 `witset` 内需要调整的文件

推荐只做适配性改动：

1. `librime/plugins/witset/src/witset_translator.h`
2. `librime/plugins/witset/src/witset_translator.cc`
3. `librime/plugins/witset/src/witset_processor.cc`
4. `librime/plugins/witset/src/witset_filter.cc`

原则是：

1. `witset` 只增加对 `witogram_core` 的调用与结果编排
2. 不再维护另一套一档 poet 主体逻辑

## 9. 接口级实施设计

### 9.1 设计原则

接口设计必须同时满足四个目标：

1. 让一档核心以后可从 `witset` 平滑迁出到开源 `witogram`
2. 让当前阶段可以先在现有 `witset` 架构里验证“一档是否能持平并最终超过 `octagram`”
3. 不把二档、三档任何私有概念带入未来的开源接口
4. 不要求一开始就完成大规模文件搬迁
5. 不把当前已经确认失配的 grammar 语义直接固化进未来 `witogram_core` API

因此，当前推荐的实施方式不是“先拆文件再验证”，而是：

1. 先在现有代码里定义**清晰的一档边界**
2. 先让现有 `witset_translator` 在内部走这条边界
3. 先验证一档结果至少持平 `octagram`，并具备继续超过的明确结构信号
4. 再把这条边界对应的实现逐步迁出为 `witogram_core`

### 9.2 推荐的核心输入输出草案

后续无论在 `witset` 内部先行验证，还是未来拆到 `witogram_core`，都建议围绕下面这组结构组织代码：

```cpp
struct WitogramCoreConfig {
  int beam_size = 0;
  int max_candidates = 0;
  int max_homophones = 0;
  int word_beam_size = 0;
  int sentence_beam_size = 0;
  int global_batch_limit = 0;

  double dict_score_weight = 0.0;
  double dict_score_norm_weight = 0.0;
  double lm_total_weight = 0.0;
  double lm_avg_weight = 0.0;
  double boundary_score_weight = 0.0;

  bool dump_snapshot = false;
  bool dump_graph_snapshot = false;
};

struct WitogramCoreContext {
  std::string input;
  std::string preceding_text;
  std::string following_text;
  size_t caret_pos = 0;
};

struct WitogramSentence {
  std::string text;
  double total_score = 0.0;
  double dict_score = 0.0;
  double lm_score = 0.0;
  double credibility_score = 0.0;
};

enum class TokenEvidenceLevel {
  kDirectHit,
  kSplitTokenSupported,
  kNeutralMissing,
  kTrueOov,
};

struct WitogramTokenEvidence {
  TokenEvidenceLevel level = TokenEvidenceLevel::kNeutralMissing;
  double context_evidence_strength = 0.0;
  double path_consistency_score = 0.0;
  bool has_direct_token_hit = false;
  bool has_split_token_support = false;
};

struct WitogramDebugArtifacts {
  bool has_graph_snapshot = false;
  bool has_sentence_snapshot = false;
  std::string graph_snapshot_path;
  std::string sentence_snapshot_path;
};

struct WitogramCoreResult {
  std::vector<WitogramSentence> sentences;
  WitogramDebugArtifacts debug;
};
```

这里的重点不是字段名要一次定死，而是要明确三点：

1. 接口里只能出现一档可公开概念
2. 结果里要能容纳未来 graph / ledger / snapshot 导出
3. 接口里绝不能出现 `SentenceCache`、`AsyncPPLService`、`llm_level_2/3` 之类闭源概念
4. 接口里也不应直接把当前已知错误语义写死为：
   - `NotFound() == 重 OOV`
   - `char fallback == 明确坏路径`

### 9.3 推荐的内部流水线拆分

即便当前先不拆文件，也建议在实现上按下面的流水线拆开：

1. `BuildSyllableGraph`
2. `InterpretGrammarEvidence`
3. `AnalyzeCredibility`
4. `LookupWordGraph`
5. `RewriteWordGraph`
6. `RunWitogramPoet`
7. `BuildTranslationPayload`

推荐对应的内部类或函数层级为：

```cpp
class WitogramCoreEngine {
 public:
  WitogramCoreResult Run(const WitogramCoreContext& context,
                         const WitogramCoreConfig& config);
};

class CredibilityLedgerBuilder {
 public:
  CredibilityLedger Build(const SyllableGraph& syllable_graph);
};

class GrammarEvidenceInterpreter {
 public:
  WitogramTokenEvidence Interpret(const std::string& context,
                                  const DictEntry& entry) const;
};

class WordGraphRewriter {
 public:
  void Apply(const CredibilityLedger& ledger, WordGraph* word_graph);
};

class WitogramPoet {
 public:
  std::vector<WitogramSentence> Decode(const WordGraph& word_graph,
                                       const WitogramCoreContext& context,
                                       const WitogramCoreConfig& config);
};
```

这一层拆分有两个作用：

1. 当前阶段先在 `witset` 内部验证时，也能明确知道哪一段逻辑真正代表“未来 `witogram_core`”
2. 将来真的分拆时，不需要重新发明边界，只是把已经成型的模块搬出去
3. `InterpretGrammarEvidence` 必须显式存在，因为当前已确认：grammar 解释语义本身就是主问题之一，不能再把它隐含在 scorer 细节里

### 9.4 `witset_translator` 近期适配边界

在“先验证、后分拆”的阶段，`witset_translator` 只需要扮演薄 orchestrator：

1. 读取现有 schema 配置
2. 组织一档上下文
3. 调用内部的一档 core 流水线
4. 把结果转回当前候选格式
5. 继续把二档、三档逻辑留在原位置

它在近期不应承担：

1. 新增更多一档 patch 规则
2. 把 debt ledger 再塞回旧的 `WitsetPoet` 私有分支里
3. 为了兼容旧逻辑而在上游和后段同时记同一套账

### 9.5 `WitogramPoet` 与现有 `WitsetPoet` 的过渡关系

目标状态是只有一个一档核心 `WitogramPoet`，但在真正分拆前，代码层面允许存在一个短期过渡期：

1. 先在当前代码库里抽出“未来 `WitogramPoet` 对应的纯一档部分”
2. 让原 `WitsetPoet` 中只保留暂时还没迁出的包装/兼容层
3. 不再继续往 `WitsetPoet` 里添加新的末端救火规则

换句话说：

1. 短期内文件名上可能还保留 `WitsetPoet`
2. 但逻辑上要开始把它收缩成“过渡外壳”
3. 新增的一档主逻辑默认都应按“未来会迁入 `WitogramPoet`”来组织

### 9.5A 正式止损结论

基于 `2026-05-20` 前后多轮真实回放、扩展组验证和三条代表句最小原型验证，现在把这条路线的边界正式写死：

1. `witset_poet` 末端 patch 路线终止。
2. 不再把“也许还差一个 `bonus/penalty/gate`”视作默认待验证假设。
3. 若后续继续推进，一档主线优先落在：
   - `InterpretGrammarEvidence`
   - `AnalyzeCredibility`
   - `RewriteWordGraph`
   - `RunWitogramPoet` 中的**搜索形态改造**
4. 仍然不允许重新回到“往 `witset_poet` 末端追加救火型 bonus / penalty / gate”的旧路线。

这里要明确区分两件事：

1. 这不是第一次提出“应把介入点前移”。
2. 但这是第一次把它升级为**正式止损规则**。

原因不是抽象架构偏好，而是已有证据已经足够形成反证链：

1. 通用末端减法路线已经判负。
2. `clean_first_word_bridge` 这类局部正向项已经判负。
3. 连按 case 机制量身定制的三条最小 `poet` contract 也全部静默。

因此，从本文开始，后续实现与验证都应默认遵守：

1. 可以保留 `WitsetPoet` 作为过渡外壳。
2. 但不再继续往里面新增“救火型一档规则”。
3. 若新增一档逻辑，必须能自然归属到：
   - `InterpretGrammarEvidence`
   - `AnalyzeCredibility`
   - `RewriteWordGraph`
   - `RunWitogramPoet` 中面向一档搜索形态的结构化改造

这里特意补一条，避免后续误解：

1. 允许改 `RunWitogramPoet` 的搜索形态
   - 例如状态化 admission、近似 beam-Viterbi、best-only admission 前移
2. 但这些改动的前提是：
   - 不能重新退化为“末端救火 patch”
   - 不能把 grammar 语义失配继续往后段隐藏

如果未来有人希望重新回到 `witset_poet` 末端 patch 路线，必须先满足两个前提：

1. 有新的实证表明主问题确实重新落回 `poet` 末端，而不是更上游。
2. 新实验不是再加一个通用 patch，而是一次性、封口式的反证验证。

### 9.6 未来 `witogram_core` 迁移清单

下面这份清单不是最终代码移动脚本，而是当前阶段的“归属判定表”。目的只有一个：

1. 让后续验证时新增的一档逻辑不再四处散落
2. 让每一块现有实现都知道未来应该迁到哪里

#### A. 应迁入 `AnalyzeCredibility`

当前主要来源：

1. `witset_translator.cc` 中的 `BuildJointRiskHints(const SyllableGraph&)`
2. `JointRiskHintBundle`
3. `NormalizeCredibilityRisk / KeepStrongCredibilityEdge / ClassifyEdgeSpellingType`
4. 所有基于 `SyllableGraph` 的 edge/vertex risk 归纳逻辑

后续归属原则：

1. 这部分应成为未来 `CredibilityLedgerBuilder` 的主体
2. 输出不再只是临时 `risk hints`
3. 而应升级为可审计的 ledger：
   - vertex risk
   - edge risk
   - spelling class
   - provenance
   - compactness / ambiguity 相关摘要

当前阶段的实施要求：

1. 可以继续留在 `witset_translator.cc`
2. 但新增逻辑必须以“ledger builder”思路组织
3. 不再以零散 helper 的方式继续扩散

#### B. 应迁入 `RewriteWordGraph`

当前主要来源：

1. `witset_translator.cc` 中 `use_upstream_edge_prior_` 分支
2. `ComputeTranslatorCompetitiveRiskScore`
3. `ComputeTranslatorCompetitiveEdgeBias`
4. `ComputeTranslatorMergeCompetitionBias`
5. 对 `candidate->weight` 的同起点竞争组统一改写逻辑

后续归属原则：

1. 这部分应成为未来 `WordGraphRewriter` 的主体
2. 专注处理：
   - 同组竞争偏置
   - merge/fragment 倾向
   - 最小保活
   - graph 进入 `poet` 前的基础重权

当前阶段的实施要求：

1. 允许继续在 `witset_translator` 中先行验证
2. 但要把算法和 orchestrator 拆开
3. `witset_translator` 最终只应负责调用 `WordGraphRewriter`，不应继续自己持有大段竞争调分逻辑

#### C. 应迁入 `RunWitogramPoet`

当前主要来源：

1. `WitsetPoet::MakeSentences()`
2. `SelectTopLines / SelectTopLinesWithDiversity`
3. `CompressLinePoolByState / PruneLinePool / ShouldTriggerPrune`
4. 纯一档的 beam / line pool / state contract
5. 一档内部使用的 transition LM、negative filter、结构分和长度项

后续归属原则：

1. 这部分应成为未来 `WitogramPoet` 的主体
2. 目标是保留“纯一档句级搜索”与其可解释特征
3. 不再继续把上游 debt ledger 的主逻辑塞在这里补救

当前阶段的实施要求：

1. 先把 `WitsetPoet` 内的纯一档部分与二档、三档耦合点做软分层
2. 新增一档规则默认写到“未来 `WitogramPoet` 会接走”的那一层
3. 不再把新的末端救火 patch 继续塞进 `MakeSentences` 主循环

#### D. 应迁入 `witogram_core` 的 Grammar / LM 证据解释层

当前主要来源：

1. `witogram.cc` 中的 `SplitUtf8Tokens`
2. `ScoreFeatures()`
3. `Query()`
4. KenLM model cache / loader
5. 当前 `whole-word / char-path / fallback / OOV` 的解释语义

后续归属原则：

1. 这部分不再只作为一个薄 `Grammar::Query()` 暴露
2. 而应成为未来 `witogram_core` 内部的 grammar / LM evidence layer
3. 供 `InterpretGrammarEvidence`、`WitogramPoet` 和 graph 评分逻辑直接复用
4. 这里必须先完成语义纠偏，避免把当前已知错误的 `NotFound() -> 重 OOV/fallback` 语义带入未来 core

当前阶段的实施要求：

1. 当前不必急着改 `witogram` 文件结构
2. 但后续任何新增的一档 LM / grammar 特征，应优先按“可被 core 直接调用的证据解释层”组织
3. 不应继续把一档全部价值押在 `grammar` 兼容接口上
4. 不应把当前实验期的 `oov_penalty_weight / char_fallback_penalty` 继续视为未来稳定公开接口

#### E. 继续留在闭源 `witset`

这些内容后续不应进入 `witogram_core`：

1. `SentenceCache`
2. `AsyncPPLService`
3. `llm_level_2 / llm_level_3`
4. 本地/远程模型切换
5. F6 强制 Beam Search
6. `_witset_*` context property 协议
7. `witset_processor` / `witset_filter` 中的刷新和回填链路
8. 所有服务健康状态、请求去抖、异步失效控制

判断原则：

1. 只要它依赖二档、三档交互或服务编排，就继续留在闭源 `witset`
2. 即便当前代码上与一档实现缠在一起，也应在近期验证阶段先做逻辑解耦，而不是把它们一起带入未来 core

#### F. 过渡阶段需要重点清理的混层点

当前最容易阻碍后续迁移的混层点主要有：

1. `witset_translator` 同时承担 graph 构建、上游调分、档位切换、cache 接线、Beam Search 编排
2. `WitsetPoet` 同时承担纯一档句级搜索与部分二档/三档预热或兼容痕迹
3. graph snapshot 与 sentence snapshot 的导出目前分散在 translator 与 poet 两边
4. `witogram` 现有实现仍停留在薄 `grammar` 形态，和未来 core 内部 scorer 的关系尚未显式化

因此，近期清理重点应是：

1. 先把“纯一档逻辑”圈出来
2. 再把“闭源编排逻辑”圈出去
3. 中间不要再继续长出新的混层 helper

#### G. 推荐的首批迁移顺序

当后续进入真正分拆阶段时，建议按下面顺序迁移：

1. 先固定 `InterpretGrammarEvidence` 的公开语义边界
   - 因为这是当前一档失配最明确的根因之一
2. 再迁 `AnalyzeCredibility`
   - 因为它最靠近上游介入点，也是当前主验证对象
3. 再迁 `RewriteWordGraph`
   - 因为它直接决定图进入句级搜索前的生存关系
4. 再迁 `WitogramPoet`
   - 前提是已经把纯一档部分和闭源耦合点拆开
5. 最后再处理公开 translator 包装和独立 schema

这样迁的好处是：

1. 先迁最核心的“介入点前移”链路
2. 最晚再处理包装层和公开 translator
3. 可以最大限度减少“分拆了很多文件，但核心效果还没站稳”的风险

## 10. 先验证后分拆的实验路线

### 10.1 总体判断

你刚提出的这条路线，我判断为**可行，而且比立刻分拆更稳**。

原因是：

1. 现在真正还没被验证完的，不是“能不能拆”，而是“一档在当前万象词库 + grammar 模型下，能否先持平、再稳定超过 `octagram`”
2. 如果这个问题还没证明，过早做 `witogram_core` 分拆会把精力花在边界整洁和文件搬迁上，而不是花在最关键的效果验证上
3. 当前仓库里 `witset_translator -> graph -> poet` 的分拆骨架已经存在，足够承接这轮验证

因此，近期主线应改成：

1. **先在现有代码树里按未来 `witogram_core` 边界推进逻辑分层**
2. **先完成 grammar 语义纠偏，再做上游 contract 前移与搜索形态改造**
3. **先达到持平，再继续验证超越**
4. **只有准确率目标已经站稳后，才进入正式物理分拆**

### 10.2 近期验证目标

近期不是验证“开源形态是否漂亮”，而是验证下面四个核心判断：

1. grammar 语义纠偏后，当前目标类错例是否不再被系统性误判为 OOV / fallback 坏路径
2. 只要把 contract 往上移，而不是继续在末端堆 patch，一档准确率是否能明显提升
3. 搜索形态改造后，这种提升是否能在固定 smoke / 基线上稳定复现，而不是只救少量个例
4. 提升后的一档行为是否具备足够清晰的边界，可被后续抽成共享 core

### 10.3 近期验证时允许的做法

在当前阶段，允许：

1. 先把 `grammar evidence / credibility ledger / graph rewrite / poet` 的新逻辑继续写在现有 `witset` 代码树下
2. 先复用现有 snapshot / graph dump 基础设施
3. 先以现有 `witset.schema.yaml` 为唯一实验入口
4. 先保留现有文件组织，只要求逻辑边界清楚

### 10.4 近期验证时不允许的做法

当前阶段不应：

1. 再往末端加新的救火 patch，只为了先把局部 case 做漂亮
2. 把二档、三档私有逻辑混入一档实验判断
3. 为了“模拟未来分拆”而提前大规模重命名、挪目录、拆文件
4. 用改变测试目标或过滤样本的方式制造“超过 `octagram`”的结论
5. 为了保持旧接口稳定，而回避当前最需要改的 grammar 语义失配

### 10.5 近期验证的最低实施顺序

推荐近期按下面顺序做：

1. 在现有 `witset` 架构内，把一档链路整理成未来 `witogram_core` 的逻辑骨架
2. 先完成 `InterpretGrammarEvidence`
   - 纠正 `NotFound()`、fallback 与 OOV 的解释语义
3. 再把真正的介入点前移到：
   - `SyllableGraph` 分析
   - `WordGraph` 改写
   - request-stage / source-line / family ownership contract
4. 再做 `RunWitogramPoet` 的搜索形态改造
   - 目标是减少“先放大再补救”的结构性缺陷
5. 保持二档、三档逻辑不动
6. 对固定 smoke 集和基线集反复验证
7. 只有当“一档至少持平、且具备继续超过的稳定信号”后，才进入正式分拆

### 10.6 验收门槛

近期主线能否继续推进到分拆阶段，建议至少满足：

1. 固定 smoke 集上，目标类错例不再被系统性地因 `NotFound()` 直接误判为重 OOV / fallback 坏路径
2. 固定 smoke 集上，`octagram 对 / 当前错` 的重点错例有持续改善
3. 固定 baseline 集上，一档整体结果至少持平当前 `octagram`
4. 若宣称“进入超越验证阶段”，则必须已出现稳定超过 `octagram` 的明确信号，而不是只在个别样本偶发领先
5. 图快照和候选快照能解释改进来自：
   - grammar 语义纠偏
   - 上游 contract 前移
   - 搜索形态改造
   而不是末端 patch 偶然碰对
6. 新增逻辑已经能自然归入：
   - `InterpretGrammarEvidence`
   - `AnalyzeCredibility`
   - `RewriteWordGraph`
   - `RunWitogramPoet`
   这些未来 core 边界，而不是继续缠回旧 patch

满足这些条件之后，再分拆才有意义。

## 11. 分阶段实施步骤

### 11.1 阶段 V0：写死边界，暂停末端 patch

目标：

1. 固定“先验证上移介入点，再分拆 shared core”的总路线。
2. 固定“共享 core，不采用 translator 套 translator”的目标架构边界。
3. 固定“二档、三档绝不进入开源仓库”的规则。
4. 停止继续往末端追加一档救火 patch。

具体工作：

1. 更新规划文档
2. 在 `WORKLOG` 中记录新的项目级约束
3. 明确 `witset.schema.yaml` 继续只使用 `witset_translator`
4. 明确近期代码主线仍留在现有 `witset` 架构内验证

验收标准：

1. 文档已不再出现 `witset_credibility` 主线
2. 文档已明确 `witogram_core` 与 `WitogramPoet` 的归属
3. 文档已明确“先验证、后分拆”

### 11.2 阶段 V1：在现有架构内整理未来 `witogram_core` 骨架

目标：

1. 不搬文件，先把现有一档逻辑整理成未来 `witogram_core` 的清晰骨架。

具体工作：

1. 在当前代码里定义 `AnalyzeCredibility / RewriteWordGraph / RunWitogramPoet` 三段边界
2. 新增或整理最小的一档上下文与结果结构
3. 把 graph / ledger / sentence / debug 输出口径固定下来
4. 停止把新增一档规则继续塞进旧的末端 patch 逻辑

验收标准：

1. 当前代码中已经能明确看出未来 `witogram_core` 的逻辑边界
2. 新增结构与命名不携带二档、三档概念

### 11.3 阶段 V2：在现有架构内完成 grammar 语义纠偏

目标：

1. 在不分拆的前提下，先纠正当前一档对万象 grammar 的解释语义。

具体工作：

1. 将 `NotFound()`、`fallback`、`OOV` 从当前统一重惩罚语义中拆开
2. 建立“正证据 / 中性缺证 / 真负证据”的 grammar 解释口径
3. 审计并改造所有 `used_char_fallback / oov_token_count` 的消费点
4. 继续导出 graph / snapshot，并用固定目标错例验证误伤是否下降

验收标准：

1. 目标类错例不再因为 grammar 缺少 unigram/whole-word token 而被系统性误判为坏路径
2. 新语义能够被 graph / snapshot 明确解释

### 11.4 阶段 V3：在现有架构内验证上游 contract 前移

目标：

1. 在 grammar 语义纠偏后，验证 source-line / family-path contract 能否更早保住正确路径。

具体工作：

1. 把主要重心前移到 `SyllableGraph` 分析和 `WordGraph` 改写
2. 补足 `source-line ownership / family continuity / request-stage admission`
3. 让句级 `poet` 回到相对更干净的图上工作
4. 对固定 smoke / baseline 做反复验证

验收标准：

1. 正确 family 更早进入 eligible 状态
2. 错误 family 不再轻易借同 prefix 混入
3. 一档结果至少接近并开始逼近 `octagram`

### 11.5 阶段 V4：在现有架构内验证搜索形态改造

目标：

1. 让 `RunWitogramPoet` 从“末端主救火器”转为“强句级组合器”，验证这一层是否能把结果从持平推进到超过 `octagram`。

具体工作：

1. 推进状态化 admission / 近似 beam-Viterbi / best-only admission 前移
2. 控制状态分裂与重复扩张
3. 让句级层更专注于长句一致性、多词块组合与全局结构
4. 验证 accuracy 改善来自搜索形态优化，而不是末端补丁

验收标准：

1. 一档整体结果达到或超过 `octagram`
2. 若宣称超越成立，需在固定 baseline 上可重复复现
3. 搜索形态改造没有重新退化为末端 patch 堆叠

### 11.6 阶段 V5：确认可迁移性，再开始分拆

目标：

1. 在效果已成立后，再判断这套一档骨架是否适合迁出为共享 core。

具体工作：

1. 逐段检查一档代码是否仍混入了 `witset` 私有概念
2. 把能抽象的输入输出正式固化
3. 列出需要迁出的文件/类清单
4. 列出必须继续留在闭源侧的代码清单

验收标准：

1. 已能明确区分“未来开源 core”与“必须闭源保留”的边界
2. 分拆工作不再需要重新设计职责

### 11.7 阶段 V6：分拆 `witogram_core` 与 `witogram_translator`

目标：

1. 在验证成功后，把一档核心从现有代码中迁出为开源 `witogram` 主体。

具体工作：

1. 新建 `witogram_core` 相关文件
2. 新建 `witogram_translator`
3. 让开源 schema 独立跑通
4. 保持接口中不出现闭源概念

验收标准：

1. `witogram` 可独立运行
2. 一档主逻辑已迁出
3. 开源/闭源边界仍清晰

### 11.8 阶段 V7：让闭源 `witset` 接入共享 core

目标：

1. 让 `witset.schema.yaml` 回到“闭源 orchestrator + 共享一档 core”的最终状态。

具体工作：

1. 在 `witset_translator` 中改为调用共享 `witogram_core`
2. 把结果接回闭源二档、三档链路
3. 清理闭源侧重复的一档实现

验收标准：

1. 一档 poet 只剩一套真源
2. 二档、三档行为保持不变

### 11.9 阶段 V8：再继续做一档质量优化

目标：

1. 在架构与边界稳定后，再继续推进一档质量上限。

这一阶段才继续做：

1. 更完整的 debt ledger
2. 更强的 path compactness / provenance
3. 更强的 transition/context 证据
4. 更细的上游竞争组保活策略

前提是：

1. grammar 语义纠偏、上游 contract 前移和搜索形态改造已经在现有架构里被证明确实有效
2. 分拆已经完成
3. 一档开源/闭源两侧行为已经重新对齐

## 12. 风险与控制

### 12.1 主要风险

1. **风险 A：表面共享 core，实际仍保留双实现**
   - 这样最容易重新长出两套 poet。
2. **风险 B：闭源边界污染开源侧**
   - 若开源仓库出现二档、三档相关命名或预留接口，就已经越界。
3. **风险 C：为了兼容 `witset` 而把 `witogram_translator` 做脏**
   - 会伤害它作为独立开源实现的价值。
4. **风险 D：改造时一档、二档、三档耦合回归**
   - 尤其是 `SentenceCache`、状态属性、刷新链路可能反向侵入 core。
5. **风险 E：在验证成功前就过早分拆**
   - 容易把时间耗在目录、命名和链接关系上，而不是效果本身。
6. **风险 F：为了守住开源/闭源边界而回避 grammar 语义纠偏**
   - 会导致边界看起来更整洁，但准确率主目标被拖慢甚至卡死。

### 12.2 风险控制策略

1. 始终以 `witogram_core` 作为唯一一档真源。
2. 任何 `witset` 私有概念都不得进入 `witogram_core` 头文件。
3. `witset.schema.yaml` 不改为双 translator 并挂。
4. 任一阶段若发现又长出第二套一档 poet，就立即止损回收。
5. 在“一档至少持平并具备继续超过 `octagram` 的稳定信号”未被验证前，不进入正式分拆阶段。
6. 若阶段性出现“分拆时机”与“准确率验证效率”冲突，优先保证准确率主目标。

## 13. 与 `octagram`、`witogram`、`witset` 的关系

### 13.1 与 `octagram` 的关系

这条路线仍然是在模仿 `octagram` 真正有效的任务结构：

1. 上游先表达路径可信度
2. 中游先清图
3. 后段再做语言模型精排

区别在于：

1. 我们不再把实现塞进单一 `grammar` 接口
2. 而是把它升级成 `witogram_core` 的完整闭环
3. 同时不照搬原版语义，而是显式适配当前万象 grammar 的现实形态：
   - 缺少 token 命中不必然等于坏路径
   - 上游 contract 与句级组合要共同承担最终判定

### 13.2 与 `witogram` 的关系

`witogram` 的新定位是：

1. 开源的一档核心实现
2. 可以独立运行的 Rime 生态方案
3. 闭源 `witset` 复用的一档真源

它不再以“薄 `grammar` 插件”作为主产品身份。

### 13.3 与 `witset` 的关系

`witset` 的新定位是：

1. `witset.schema.yaml` 的唯一 orchestrator
2. 一档能力的闭源集成方
3. 二档、三档能力的唯一承载方

它不再承担“维护另一套一档核心实现”的职责。

## 14. 推荐执行顺序

当前最推荐的启动顺序是：

1. 先完成文档与边界收口
2. 先在现有 `witset` 架构里完成 grammar 语义纠偏
3. 再验证上游 contract 前移
4. 再验证搜索形态改造是否把一档推到持平并继续逼近/超过 `octagram`
5. 只有准确率目标已站稳后，才正式抽 `witogram_core`
6. 再让 `witogram_translator` 独立跑通
7. 最后让 `witset_translator` 接入共享 core 并清理分叉

## 15. 一句话版

下一阶段最合理的路线不是继续在末端死磕，也不是立刻做大规模物理分拆，而是先在现有 `witset` 代码树里按未来 `witogram_core` 边界完成 grammar 语义纠偏、上游 contract 前移和搜索形态改造，先让一档达到持平并继续验证超过 `octagram`；只有准确率主目标已经站稳后，才把已经成型的一档骨架迁出为开源 `witogram_core + witogram_translator`，再让闭源 `witset` 作为 `witset.schema.yaml` 的唯一主控方复用同一个核心，并由唯一的一档 `WitogramPoet` 同时服务开源独立模式与闭源集成模式。
