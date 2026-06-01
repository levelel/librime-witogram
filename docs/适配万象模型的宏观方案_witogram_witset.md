# 适配万象模型的宏观方案：witogram + witset 持平或超过 octagram

## 1. 目标

本方案的目标不是简单“修一个 bug”，而是从宏观层面把：

- `witogram`
- `witset`
- 当前万象词库
- 当前万象 grammar 模型

重新对齐成一个可持续演进的整体系统，使其在一档模式下：

1. 先达到原版 `octagram` 的正确率。
2. 再在保证性能与稳定性的前提下，争取超过原版 `octagram`。

---

## 2. 可行性判断

## 2.1 结论

**可行，但不是“再补一两个末端特征”就能做到。**

更准确地说：

1. **达到持平是可行的**
   - 原版 `octagram` 已经证明：在同一套词典与 grammar 资源前提下，这个目标在资源层面并不被禁止。
   - 当前 `witogram + witset` 的主要问题不是资源天花板，而是契约和解释语义不匹配。

2. **超过原版也是可行的**
   - 但前提是先把当前 mismatch 修掉。
   - 否则 `witogram` 更强的上下文表达和更灵活的搜索，反而只会放大错误解释。

3. **实现路径必须分阶段**
   - 第一阶段先纠正“错误语义”。
   - 第二阶段再重建“正确搜索形态”。
   - 第三阶段才谈“利用 `witogram` 的额外优势超过原版”。

---

## 3. 核心设计判断

### 3.1 当前系统的根本矛盾

当前 `witogram + witset` 的根本矛盾是：

- **词典层** 提供的是表面词块与候选 family。
- **grammar 层** 提供的是拆分 token 视角下的上下文证据。
- **当前实现** 却经常把“表面词块在 grammar 中缺少同粒度 token”解释成显式负证据。

这会导致两个问题：

1. 正确路径被过度惩罚。
2. 句级搜索被迫承担不应该由它承担的补救任务。

### 3.2 目标形态

适配后的系统应该长成这样：

1. **词典层负责提供丰富候选与基础词块。**
2. **translator 层负责更早处理 spelling / joint / family / source-line 风险。**
3. **witogram 负责提供适配当前模型形态的局部上下文证据。**
4. **witset 负责在更干净的候选池上做强句级组合。**

换句话说：

- `translator` 负责“别把坏路径放进来太多”。
- `witogram` 负责“对当前 token 序列给上下文证据”。
- `witset` 负责“在合理候选之间做更强句级选择”。

---

## 4. 总体方案

建议把整个改造拆成四层。

## 4.1 第一层：修正 witogram 的 grammar 解释语义

### 目标

先解决当前最致命的误解释：

- 不能再把“当前 grammar 没有某个 unigram / whole-word token”直接解释成重 OOV / fallback 路径。

### 设计原则

必须把当前结果区分成三类，而不是一刀切：

1. **正证据**
   - 当前 token 或 token 序列获得了明确 grammar 支持。

2. **中性缺证**
   - 当前粒度下没有直接命中，但并不代表这条路径错误。
   - 这类情况在当前万象 grammar 中应被视为常态之一，而不是异常。

3. **真负证据**
   - 已存在更强对照路径，且当前路径表现出明确 joint / family drift / source-line 断裂等坏信号。

### 落地方式

建议把 `witogram` 当前的输出语义改成更细的分层结果，而不是简单输出：

- `used_char_fallback`
- `oov_token_count`

更合理的信号集合应包括：

- `token_evidence_level`
  - `direct_hit`
  - `split_token_supported`
  - `neutral_missing`
  - `true_oov`
- `context_evidence_strength`
- `path_consistency_score`

### 预期收益

这一层修完之后，`第一站是 -> 一` 这类路径就不会再因为 grammar 缺少 unigram `一` 而自动变成显式坏路径。

这一步是整个方案的前提，如果不先修，后续所有搜索优化都会建立在错误语义上。

## 4.2 第二层：把上游 contract 前移到 translator / request-stage

### 目标

让当前系统更接近原版 `octagram` 的正确分工：

- 在候选大规模扩张之前，先保住正确 family，压掉明显错误 family。

### 核心思想

不是简单“给 continuation 加分”，而是建立可审计的 path contract。

建议前移的 contract 至少包括：

1. **source-line ownership**
   - 当前路径是否仍然属于被验证的 source-line family。

2. **family continuity**
   - 当前候选是否延续了正确 family，而不是借相同前缀混入错误 family。

3. **joint legality**
   - 当前局部拼音切分 / joint 是否属于高风险区域。

4. **request-stage admission**
   - 在相同状态或相近状态下，不让错误 family 先大规模进入池子。

### 落地方式

建议将现有试验中已证明有价值的方向正式制度化：

1. `CredibilityLedgerBuilder`
   - 不只记录 `vertex_risks / edge_risks`
   - 还要输出：
     - family identity
     - source-line status
     - continuation legality
     - ownership eligibility

2. `WordGraphRewriter`
   - 不再做粗粒度 surface bonus
   - 改为做结构化的 family/path 准入与竞争 contract

3. `BuildRequestStagePrefixStates`
   - 继续走多 family state 的方向
   - 但状态定义应围绕 family/source-line contract，而不是围绕表面 text

### 预期收益

这一步的收益不是立刻“靠一个分数翻盘”，而是：

- 错误 family 更早被抑制。
- 正确 family 不会等到末端句级阶段才开始求生。

## 4.3 第三层：把 witset 从“主救火器”改回“强句级组合器”

### 目标

让 `witset` 发挥真正强项，而不是替前两层收拾残局。

### 核心思想

当前 `witset` 过于像：

- 一个宽候选池上的末端总补救器

更合理的目标形态应是：

- 在已被 translator 清洗过一轮的候选池上，做更强上下文的句级组合

### 设计方向

1. **状态化搜索**
   - 保留并继续强化方案 C 已出现正向信号的路线：
     - 按状态准入
     - 近似 beam-Viterbi
     - 更早 best-only admission

2. **减少表面候选池膨胀**
   - 不再允许同一结构状态下的大量近似路径先扩张再补救。

3. **让句级特征服务于强上下文分辨，而不是替 grammar 纠 OOV**
   - `witset` 的强项应放在：
     - 长句一致性
     - 远程上下文
     - 多词块组合偏好
   - 而不是继续承担：
     - “某个 unigram 不在 grammar 里怎么办”

### 预期收益

这一层是未来超过 `octagram` 的关键，因为原版在句级表达上的能力边界相对明确，而 `witset` 仍有更大的扩展空间。

## 4.4 第四层：把当前万象模型视为“局部证据源”，而不是“绝对裁判”

### 目标

明确 grammar 在整个系统中的正确角色。

### 核心判断

当前万象 grammar 不应被视为：

- 一个会对所有表面词块给出同粒度 verdict 的绝对裁判

更合理的定位应是：

- 一个为 token 序列提供局部上下文支持度的证据源

### 这意味着

1. grammar 查不到，不必然是错。
2. grammar 查到，也不必然就能覆盖 spelling / source-line 风险。
3. 真正的最终路径选择，必须综合：
   - 上游 family/path contract
   - grammar 局部证据
   - 句级整体一致性

---

## 5. 分阶段实施路线

## 5.1 阶段一：语义纠偏

### 目标

先让当前系统“不要误伤正确路径”。

### 建议动作

1. 重构 `witogram` 的结果语义
   - 把 `NotFound()` 从重 OOV 语义中拆出来。

2. 审计所有 `used_char_fallback / oov_token_count` 的消费点
   - 尤其是 `witset_poet` 中如何把它们放大成额外惩罚。

3. 建立“中性缺证”口径
   - 对当前万象 grammar，允许一部分 token 缺失只表现为：
     - 没拿到额外证据
     - 而不是显式负担

### 验证目标

先不求整体超过原版，只验证：

- `一` 这类路径不再稳定被系统性误判为坏路径。

## 5.2 阶段二：contract 前移

### 目标

让正确 family 更早保活，错误 family 更早出局。

### 建议动作

1. 扩展 `CredibilityLedger`
2. 强化 `WordGraphRewriter`
3. 正式化 request-stage 多 family state
4. 引入更明确的 source-line ownership contract

### 验证目标

在 shared-prefix 代表集与目标错例上验证：

- 正确 family 是否更早进入 eligible 状态。
- 错误 family 是否不再借同 prefix 轻易混入。

## 5.3 阶段三：搜索形态改造

### 目标

让 `witset` 不再以“先放大再补救”为主。

### 建议动作

1. 继续推进近似 beam-Viterbi
2. 将 best-only admission 前移
3. 重做状态键定义
4. 控制状态分裂与性能成本

### 验证目标

验证的不只是准确率，还包括：

- 平均候选数
- wall time
- 错误 family 的扩张率

## 5.4 阶段四：超过 octagram

### 目标

在完成前三阶段后，开始利用 `witogram + witset` 的特有优势超越原版。

### 可利用方向

1. 更长上下文的一致性表达
2. 更灵活的句级全局重排
3. 更可扩展的多信号融合
4. 更强的调试与可观测性

### 条件

只有当前三阶段完成后，这一层的增强才不会建立在错误底座上。

---

## 6. 能否保持并利用 witogram 相比 octagram 的优势

## 6.1 可以保留的优势

如果方案按上述路径实施，`witogram` 的核心优势不仅可以保留，而且可以更好发挥：

1. **更强句级上下文表达**
   - 原版 `octagram` 更像薄 grammar。
   - `witogram + witset` 仍有空间做更强的长句一致性判断。

2. **统一建模能力更强**
   - 可以统一接收 translator 上游风险、grammar 局部证据和句级全局特征。

3. **扩展空间更大**
   - 后续不论接更复杂 grammar、更多统计特征，还是远端模型协同，`witogram` 路线都更有扩展性。

4. **可观测性更强**
   - 当前已经形成了比原版更强的 probe / graph / snapshot 体系。
   - 这对后续持续优化非常重要。

## 6.2 必须接受的妥协

为了适配当前万象模型与万象词库，确实需要做几项明确妥协。

### 妥协一：放弃“整词 token 命中应该普遍存在”的假设

必须接受：

- 当前万象 grammar 不是一个以整词 token 为中心的模型。

因此要牺牲的是：

- 过去那些强依赖 whole-word 与 char-path 直接对比的设想。

### 妥协二：削弱或条件化当前的 OOV / fallback 语义

必须接受：

- 对当前模型来说，一部分 `NotFound()` 只能被解释为“缺证”，而不是“真错”。

这会牺牲的是：

- 当前实现中那种简单直接、统一口径的重惩罚逻辑。

### 妥协三：更强的上游 contract 会减少一部分“自由探索”

如果把更多 family/path contract 前移：

- 候选池会更早变窄。

这会牺牲的是：

- 当前宽搜索形态带来的部分“广撒网”能力。

但这个牺牲是必要的，因为目前已经证明：

- 对输入法这一类极高歧义任务，错误 family 如果早期不被压住，后续越强的句级表达越容易被噪声拖累。

## 6.3 是否会牺牲掉 witogram 的核心优势

**不必牺牲核心优势，但必须调整优势的发挥方式。**

更准确地说：

- 不需要放弃 `witogram` 的强上下文、统一建模、可扩展性和可观测性。
- 需要放弃的是：
  - 建立在错误 grammar 假设上的那一部分“相对优势幻觉”。

换句话说：

- 不是放弃 `witogram` 的优势。
- 而是要把 `witogram` 的优势建立在适配当前万象模型现实形态的基础上。

---

## 7. 风险与约束

### 7.1 主要风险

1. **语义纠偏后，部分真实坏路径也可能被放宽**
   - 所以必须和上游 contract 前移配套进行。

2. **状态化搜索改造可能带来性能压力**
   - 必须同步做状态压缩与 admission 控制。

3. **若只改语义、不改搜索形态，收益可能有限**
   - 因为当前瓶颈不只在评分，还在搜索结构。

### 7.2 为什么仍值得做

因为当前证据已经足够说明：

- 继续在旧语义和旧搜索形态上做局部 patch，收益正在快速递减。
- 如果不先做结构重对齐，就很难真正接近原版 `octagram`。

---

## 8. 推荐执行顺序

推荐按以下顺序推进：

1. **先做语义纠偏**
   - 修 `NotFound()` 的解释语义。

2. **再做上游 contract 前移**
   - 让 translator 更早保住正确 family。

3. **再做搜索形态改造**
   - 让 `witset` 变成强句级组合器，而不是末端救火器。

4. **最后再做超越原版的增强**
   - 让 `witogram` 真正发挥比 `octagram` 更强的地方。

---

## 9. 最终结论

本方案的最终判断是：

1. **`witogram + witset` 持平原版 `octagram` 是可行的。**
2. **超过原版也是可行的，但必须先完成结构重对齐。**
3. **要达到主要目标，不必放弃 `witogram` 的核心优势。**
4. **必须妥协的，不是 `witogram` 的核心优势，而是过去一些建立在错误 grammar 假设上的实现方式。**

因此，正确路线不是“退回 octagram”，也不是“继续末端 patch”，而是：

- 保留 `witogram` 的强上下文和统一建模方向；
- 同时把它的 contract、语义和搜索形态重新适配到当前万象词库与万象模型的现实形态上。



---

## 10. P0-P1-P2 执行结论（2026-05-27 更新）

### 10.1 已完成覆盖

| 阶段 | 核心交付 | case1 效果 | case2 效果 |
|---|---|---|---|
| P0 | grammar语义纠偏 | 未翻正 | 已翻正 |
| P1 | path_confirmation/family-tail/request-stage/path-debt | 未翻正(均判为下游症状) | 保持正确 |
| P2 | SelectTopLines家族保留/edge_prior调查 | 未翻正 | 保持正确 |

### 10.2 case1 根因完整链

octagram top1=一直向往着远方, witogram top1=一直想望着远方. 来源链:
词典覆盖(一直向不在step1) -> raw LM gap 22.89(向往-173 vs 想往-151)
  -> Base积累91% + LmScaled95% -> SelectTopLines 78.8分砍掉正确线 -> 终局错误

### 10.3 edge_prior/credibility 不如 octagram 的原因
- witset: edge_risk -> ComputeTranslatorCompetitiveEdgeBias (竞争性bias, 仅对clean_gap > -1.35生效)
- octagram: credibility -> entry->weight (直接无条件减权)
- 两者对同一信号的处置方式根本不同

### 10.4 方案ABCD去重
- A(joint_prior前移): TRIED Top-1=0.538
- B(joint-aware beam): TRIED Top-1=0.538
- C(beam-Viterbi): TRIED Top-1=0.542
- D(模型侧强化): 未尝试，长期路线

### 10.5 更新判断
原结论需要细化: case2类已持平octagram, case1类不改模型/字典无法持平.
整体水平取决于两类比例, 300基线待跑方可量化.
方案D是唯一未覆盖且有理论依据的方向.


## 11. F 路线 + word-ambiguity 检测结论（2026-05-27 更新）

### 11.1 F 路线（直接 credibility penalty）
- 将 edge_risk 惩罚从竞争性 bias 改为直接 penalty（kAmbiguousCredibilityMagnitude * weight * risk）
- 同时保留原有竞争性 edge_bias
- 结果: case1 未翻正。原因是 edge_risk = 0 —— SyllableGraph 没有从 prism 收到 kAmbiguousSpelling

### 11.2 word-ambiguity 补丁
- 在 RewriteWordGraph 中新增 word-level 歧义检测：收集同边候选文本集合，>=2 种不同文本则标记为 word-ambiguous
- 对 word-ambiguous 边：统一施加 kAmbiguousCredibilityMagnitude * weight * 0.5 惩罚
- 对 word-ambiguous 边也施加 per-candidate 差异化惩罚：(ebest - candidate->weight) * weight * 0.25
- 结果: case1 未翻正。word-ambiguity 基础设施已就位，但正确/错误候选可能处在不同 WordGraph 边中

### 11.3 prism 字符级歧义缺口
- 根本原因: prism 数据中 xiang/wang 的 spelling type 不是 kAmbiguousSpelling
- prism 是 Rime 编译产物，witogram/witset 无法修改
- word-level 歧义检测是插件层面的补救，但覆盖面取决于同一 WordGraph 边是否同时包含正确和错误候选

## 12. D2B 训练计划
- 文档: D2B_BPE_KenLM_训练计划.md
- BPE + 维基语料 + KenLM 训练，作为当前 KLM 的补充
- 时间估计: 1-2 天

## 13. 当前状态总结（2026-05-27）
| 维度 | 状态 | Top-1 | Top-3 | vs octagram Top-1 |
|---|---|---|---|---|
| witogram 当前 | — | 53.5% | 71.8% | -14.8pp |
| octagram 参照 | — | 68.3% | 70.0% | baseline |
| P0-P1-P2 全覆盖 | 已完成 | — | — | — |
| D3(伪词打分) | 已落地 | — | — | — |
| F(直接credibility) | 已落地 | — | — | — |
| word-ambiguity | 已落地 | — | — | — |
| D2B(BPE+KenLM) | 计划就绪 | — | — | — |
| D1(词级KenLM) | 未开始 | — | — | — |


## 14. octagram 对标实验（2026-05-27 更新）

### 14.1 实验设计
- 实验1: lm_total=0 + 全部adjustment清零, 仅保留 dict_score + upstream_edge_prior
- 实验2: 同上 + 紧beam (beam=8, sentence_beam=16, min_state=true)

### 14.2 结果
| 配置 | Top-1 | Top-3 | avg_cand |
|---|---|---|---|
| dict+cred, 宽beam | 22.3% | 33.9% | 19.77 |
| dict+cred, 紧beam | 29.2% | 38.9% | 4.99 |
| witogram 完整 | 53.5% | 71.8% | 19.64 |
| octagram | 68.3% | 70.0% | — |

### 14.3 结论
1. 纯 dict+credibility (即使紧beam) 只能达到 29.2%, 远低于 octagram 68.3%
2. KenLM(字级)贡献了 +24.3pp (29.2->53.5)
3. octagram 剩余的 ~39pp 来自薄层grammar + 搜索架构差异 (gate-before-expand vs expand-then-gate)
4. 权重扫描(36组合)全部无法翻正 case1: dict gap(1.0)远小于LM gap(22.89)

### 14.4 路线更新
- 已否决: 纯权重调优、纯搜索约束
- 唯一可行: D2B BPE KenLM (从tokenization层面消除LM偏差)
- 后续可叠加: 方案C (在正确LM基础上优化搜索)
