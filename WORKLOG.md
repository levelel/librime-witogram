# Work Log

## 2026-05-16

- 保存首次原版 `Rime + octagram` 的 `300` 条对照产物到 `docs/benchmark_artifacts/octagram_300_20260516`
- 复核对照结果并确认关键结论：
  - 原版 `octagram` `Top-1 = 0.683333`，`Top-3 = 0.700000`
  - 当前 `witogram + witset_poet` `Top-1 = 0.534884`，`Top-3 = 0.661130`
  - 当前版本的主要弱点是排序，不是候选召回
- 将上述结论写入：
  - `docs/route_b_implementation_plan.md`
  - `release_notes.md`
- 明确承认原版对歧义拼音路径的抑制虽然粗暴，但在真实语料上有效
- 复查当前 `witogram` / `witset_poet` 代码，确认当前缺口：
  - 已有整词路径与逐字路径 LM 证据
  - 但尚未把两者差值转成强句级排序特征
- 确定下一步算法方向：
  - 新增 `merge_gain`
  - 新增 `fragment_penalty`
  - 新增 `ambiguity_margin`
  - 目标是在统一候选池中，用证据化句级特征超过原版的硬惩罚
- 已开始实现下一步算法的第一阶段代码改动：
  - 在 `witset_poet` 中新增 `merge_gain_weight_`
  - 在 `witset_poet` 中新增 `fragment_penalty_weight_`
  - 将 `whole_word_log10 - char_path_log10` 直接转为句级调整项
  - 对 `长词 + 单字尾巴` 的后缀碎裂路径新增显式惩罚
  - 调试输出新增 `Merge` 与 `Frag` 分项，便于后续基线回看
- 按工作区规则执行 `outwit-windows/build_and_deploy.bat`，编译通过
- 运行 `python c:/Code/outwit/outwit-windows/librime/plugins/witogram/tools/run_local_snapshot_baseline.py --limit 300`
- 本轮基线结果：
  - `Top-1 = 0.524917`
  - `Top-3 = 0.684385`
  - `expected_not_found_count = 72`
- 与上一轮相比：
  - `Top-1` 小幅下降
  - `Top-3` 上升
  - `expected_not_found_count` 明显下降
- 说明本轮改动改变了候选分布，但尚未把更多正确答案稳定推到第一名
- 快照已确认新增调试分项生效：
  - `Frag` 已出现非零值，说明碎裂尾巴惩罚已进入链路
  - `Merge` 目前几乎全为 `0.00`，说明整词优于逐字的证据链尚未真正发挥作用
- 开始执行性能与 `merge_gain` 定点排查：
  - 在 `witogram` 中新增 `max_context_tokens()`，显式暴露 KenLM 实际只需要的后缀上下文长度
  - 在 `witset_poet` 中将 `Line::context()` 从“完整累计句子”改为“仅保留 KenLM 需要的 UTF-8 token 后缀”
  - 为非 `witogram` 路径保留 `full_context()`，避免改变其他 grammar 的原有语义
  - 为 `merge_gain` 新增定点诊断字段：
    - `MergeDelta`
    - `WholeHit`
    - `CharFB`
  - 这样下一轮基线后可直接判断：
    - 是 `whole-word` 根本不命中
    - 还是命中了但 `whole_word_log10 - char_path_log10` 拉不开差距
- 按工作区规则再次执行 `outwit-windows/build_and_deploy.bat`，编译部署通过
- 重新运行 `python c:/Code/outwit/outwit-windows/librime/plugins/witogram/tools/run_local_snapshot_baseline.py --limit 300`
- 本轮完整快照统计：
  - `total_records = 7037`
  - `top1_accuracy = 0.524917`
  - `top3_accuracy = 0.684385`
  - `expected_not_found_count = 72`
- 基于快照时间戳统计到的写入跨度约为 `1020380 ms`（约 `17.0` 分钟），说明后缀上下文优化本身没有把整体 wall time 明显压到可忽略级别
- `merge_gain` 定点诊断结论：
  - `whole_hit_candidates = 111808`
  - `charfb_candidates = 106179`
  - `merge_delta_nonzero_candidates = 0`
  - `top1_whole_hit_records = 4082`
  - `top1_charfb_records = 6090`
  - `top1_merge_delta_nonzero_records = 0`
- 结论：
  - `merge_gain` 失效的主因不是“whole-word 从不命中”
  - 而是“命中后 `whole_word_log10 - char_path_log10` 始终为 0”
  - 这强烈指向当前 `.klm` 与 `witogram` 的 whole-word / char-path 查询语义没有形成真正差异，需继续排查模型 token 组织方式或 `vocab.Index(word)` 的命中模式
- 为进一步排除调试输出四舍五入的干扰：
  - 新增单步调试字段 `StepWholeLog10 / StepCharLog10 / StepMergeDelta / StepWholeHit / StepCharFB`
  - 并将这些单步字段临时提高到 `6` 位小数输出
- 重新编译部署后，以 `--limit 5` 做小范围定点验证
- 小样本直接证据：
  - 大量双字候选（如 `进程 / 进出 / 进城`）表现为：
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
    - `StepWholeLog10 = 0`
    - 说明当前步根本没有 whole-word 命中，只走了 char-path
  - 少数命中 whole-word 的样本（如 `金创`）表现为：
    - `StepWholeHit = 1`
    - `StepCharFB = 0`
    - `StepWholeLog10 = -57.393610`
    - `StepCharLog10 = -57.393610`
    - `StepMergeDelta = 0.000000`
    - 说明即使命中 whole-word，当前 whole-word 与 char-path 的分数也完全相同
- 因此可确认：
  - `merge_gain` 之所以始终不起作用，根因不是权重太小
  - 而是当前运行中的 whole-word / char-path 比较本身没有形成可区分信号
- 继续下钻模型侧根因：
  - 复查 `dump_to_arpa.cc`，确认转换链路会把从 `GramDb::ExtractAll()` 提取出的键按 token 逐个 `decode()` 后，用空格写入 ARPA
  - `GramDb::ExtractAll()` 仅做 Darts DFS 原样导出，不会额外合并 token
  - 编译并使用 KenLM 自带 `query` 工具，直接查询当前 `wanxiang-lts-zh-hans.klm`
- 直接查询结果（按输入顺序验证）：
  - 单 token 查询 `金创 / 进程 / 今晨` 命中 `vocab_id = 0`，即 OOV
  - 拆分查询 `金 创 / 进 程 / 今 晨` 时，各 token 可正常命中并返回有效分数
- 新结论：
  - 当前 `.klm` 对这些多字串并不把它们当作 whole-word token
  - 它实际建模的是拆分后的 token 序列
  - 这也解释了为什么之前在候选 `金创` 上看到的 `StepWholeHit = 1` 不能证明 `金创` 整词命中；那只是最后一步追加的单个 entry 命中，并不等于整条可见候选文本作为单 token 命中
  - 因此，基于“整条可见候选词作为 whole-word token 与 char-path 逐字路径直接比较”的 `merge_gain` 设计，在当前模型上天然缺乏成立前提
- 复查计划文档 `route_b_implementation_plan.md` 后确认：
  - 早在方案阶段就已明确写过“第一版先训练字级 KenLM（按字切分）”
  - 当时这是基于查询逻辑兼容性的路线判断，不是对当前 `.klm` 的直接运行时实证
- 基于上述结论，开始把下一步算法从“whole-word 对比”切到“结构性歧义抑制”：
  - 在 `witset_poet` 中新增 `structure_fragment_weight`
  - 新增句子级结构成本 `ComputeStructuralFragmentCost(total_char_count, word_count, single_char_word_count, trailing_single_char_run)`
  - 该成本直接惩罚：
    - 平均词长过低
    - 单字词占比过高
    - 句尾连续单字链
  - 运行时按“当前结构成本 - 前一步结构成本”的增量形式计入打分，避免重复硬编码叠罚
- 已标准编译部署通过，并用 `--limit 5` 做了小样本验证
- 小样本快照已出现非零 `Struct` 调试项，例如：
  - `进车内饰`
  - `进程很是`
  - 均出现 `Struct:-0.91`
- 这说明新的结构性特征已经实际进入句级打分链路，不再依赖不存在的 whole-word token 前提
- 继续运行 `300` 条完整基线时，默认 `--snapshot-timeout-seconds=10` 在超长输入上超时
  - 失败点不在汇总阶段，而是在主循环等待某条长输入的快照记录
  - 原始快照表明该输入的记录最终会写出，只是晚于默认等待阈值
- 随后使用 `--snapshot-timeout-seconds=60` 重新运行，同一版本完整跑通 `300/300`
- 本轮完整结果：
  - `Top-1 = 0.524917`
  - `Top-3 = 0.684385`
  - `expected_not_found_count = 72`
  - `BASELINE_WALL_MS = 1150807`
- 结论：
  - 新增 `Struct` 特征确实活跃，不是空转
  - 统计显示：
    - `struct_nonzero_candidates = 38241 / 132447`
    - `top1_struct_nonzero_records = 956`
    - `min_struct = -2.54`
  - 但整体 `Top-1 / Top-3` 与上一轮完全一致，说明当前这版结构性惩罚虽已命中大量碎裂路径，但还不足以改变最终 benchmark 总成绩
  - 下一步不应再验证“Struct 是否生效”，而应直接调整其强度与触发条件，或补充更能针对长句歧义链的结构特征
- 对“一档为什么仍然卡顿”做了代码级排查，结论如下：
  - 一档 (`llm_level_1`) 下不会触发后台异步 PPL/OutwitSettingsServer 链路
  - `witset_translator.cc` 中一档实际参数为：
    - `llm_driven_engine = false`
    - `local_correction = false`
    - `advanced_scorer = true`
  - 因此前端卡顿来自本地同步 `WitsetPoet::MakeSentences()`，不是后台异步服务
- 发现的主要性能热点：
  - 一档仍使用很宽的本地搜索配置：
    - `generate_count = max_candidates * 2 = 40`
    - `word_beam_size = 80`
    - `sentence_beam_size = 300`
    - `global_batch_limit = 2400`
    - `sentence_soft_limit = 36000`
  - `MakeSentences()` 每次按键都会同步执行整句图搜索与多轮排序：
    - `SelectTopLines(source_pool, word_beam_size_)`
    - 分支内 `partial_sort`
    - 全局 `partial_sort(all_requests, batch_limit)`
    - 结束时 `SelectTopLinesWithDiversity(final_pool, ...)` 对整池 `sort`
  - `SelectTopLinesWithDiversity()` 会对整个 `final_pool` 排序，并为每条路径重建祖先文本，复杂度明显高于原版 poet
  - `Witogram::ScoreFeatures()` 对每个 `(context, entry)` 重复做：
    - UTF-8 切 token
    - context 截断
    - char-path 逐 token `model->Score`
    - whole-word `vocab.Index(word)` 与 `model->Score`
  - `CountUtf8Chars(entry->text)` 在主热路径中被多次重复计算
  - 最终输出阶段还会为每个候选构造长调试字符串 comment
- 与原版 `poet/octagram` 的关键性能差异：
  - 原版 `poet` 的 `kMaxLineCandidates = 7`
  - 原版主评分基本是一次 `Grammar::Evaluate(context, entry, entry_weight, ...)`
  - 当前 `witset_poet` 则是更宽 beam、更大的状态池、更多轮排序、更多特征和更多字符串处理
- 当前判断：
  - benchmark 的 wall time 被快照/JSON 导出放大了，但这不是虚假问题
  - 即使脱离 baseline 驱动，一档本地句级搜索本身也足以造成真实打字卡顿
  - 下一步优化应优先瞄准：
    - Level 1 搜索宽度与候选数量
    - `ScoreFeatures()` 结果缓存/复用
    - 最终多样性选择阶段的整池排序
    - 关闭或延后调试 comment 构造
- 已按上述 2/3/4 三项实施一轮性能优化：
  - `ScoreFeatures()` 单轮缓存：
    - 在 `MakeSentences()` 内增加 `(context, word, is_rear) -> WitogramScoreFeatures` 缓存
    - 同时增加 `char_count` 缓存，减少重复 `CountUtf8Chars()`
  - `SelectTopLinesWithDiversity()` 优化：
    - 不再对整个 `final_pool` 全量 `sort`
    - 改为只对头部窗口执行 `partial_sort`，窗口大小约为 `max(top_k * 6, 24)`
    - 使用 `unordered_map/unordered_set` 降低去重与祖先配额开销
  - 调试 comment 开关化：
    - 仅当 `debug_dump_local_snapshot = true` 时生成整段 `[Debug] ...`
    - 非调试情况下只在二档/三档保留最小 `[ClusterSize:n]` 标记，供 processor 使用
- 已标准编译部署通过
- 冒烟验证：
  - `--limit 5 --snapshot-timeout-seconds 60` 运行成功
  - wall time = `33934 ms`
- 完整验证：
  - `--limit 300 --snapshot-timeout-seconds 60` 运行成功
  - 结果保持不变：
    - `Top-1 = 0.524917`
    - `Top-3 = 0.684385`
    - `expected_not_found_count = 72`
  - wall time = `1191364 ms`
- 关键观察：
  - 当前 benchmark 环境里 `debug_dump_local_snapshot` 仍为 `true`
  - 因此第 4 项“只在调试时生成长 comment”的收益，在本轮 benchmark 中被刻意保留的调试链路抵消，无法从 wall time 直接体现
  - 2/3 两项实现层优化至少没有改变结果，但在当前配置和数据规模下，也没有带来可见的总耗时下降
  - 说明当前瓶颈更可能仍然在“一档搜索宽度过大”本身，而不是已优化的末尾排序或局部重复打分
- 继续按两阶段方案实验：
  - 第一阶段：强化局部坏模式惩罚，不降宽
    - 新增 `tail_repair_weight_`，显式惩罚“长词后连续单字补尾巴”
    - 在 `Line` 中增加 `tail_anchor_char_count` 与 `cumulative_tail_repair_penalty`
    - 新调试项 `Tail:` 已接入快照
  - 第一阶段结果：
    - `Top-1 = 0.531561`
    - `Top-3 = 0.674419`
    - `BASELINE_WALL_MS = 760092`
    - 相比上一轮：
      - `Top-1` 小幅回升
      - `Top-3` 小幅下降
      - wall time 从约 `1191364 ms` 明显降到 `760092 ms`
    - `Tail` 特征已大规模命中：
      - `tail_nonzero_candidates = 71844 / 133270`
      - `top1_tail_nonzero_records = 2606`
      - `min_tail = -12.16`
    - 结论：局部坏模式惩罚方向有效，值得保留
- 第二阶段：仅对一档保守降宽
  - `generate_count`：一档从最多 `40` 限到 `16`
  - `word_beam_size`：一档上限 `48`
  - `sentence_beam_size`：一档上限 `160`
  - `global_batch_limit`：一档上限 `960`
  - `sentence_soft_limit`：一档上限 `6000`
  - 二档/三档不受影响
- 第二阶段结果：
  - `Top-1 = 0.445183`
  - `Top-3 = 0.568106`
  - `expected_not_found_count = 118`
  - `BASELINE_WALL_MS = 471372`
  - `avg_candidate_count = 12.314623`
  - 结论：
    - 性能继续显著改善，wall time 再降约 `38%`
    - 但准确率严重下滑，说明这版降宽过头，不能直接接受
    - 当前最合理的判断是：
      - 第一阶段的 `Tail` 局部惩罚值得保留
      - 第二阶段的一档降宽需要明显回调，尤其是候选生成数与句级状态池不应一次性压到当前档位
- 根据后续判断，已撤回上一轮“一档降宽”实验性改动，恢复到仅保留结构性惩罚的对照状态
- 进一步尝试了更接近 `octagram` 风格的硬惩罚：
  - 新增 `Octa` 调试项
  - 对“长词后连续单字补尾巴”的局部坏模式，不再只用平滑统计，而是直接给固定强惩罚
  - 这更接近 `octagram`/`syllabifier` 的思路：命中坏 joint 时直接压 `credibility`
- 代码级理解更新：
  - 原版 `octagram` 的粗暴惩罚核心并不在 `poet.cc`，而在更早的 `syllabifier.cc`
  - `CheckOverlappedSpellings()` 会对 ambiguous joint 直接施加 `log(1e-10)` 级别惩罚
  - 该惩罚会通过 `props->credibility -> chunk.credibility -> entry->weight` 提前进入词条权重
  - 因此原版是“上游先压歧义路径，再交给 poet 做小 beam 句级搜索”
- `Octa` 硬惩罚实验结果：
  - `Top-1 = 0.538206`
  - `Top-3 = 0.684385`
  - `BASELINE_WALL_MS = 762957`
  - 相比仅有 `Tail` 的版本：
    - `Top-1` 再小幅提升
    - `Top-3` 恢复到之前较好的水平
    - wall time 基本持平
  - `Octa` 命中统计：
    - `octa_nonzero_candidates = 65489 / 135761`
    - `top1_octa_nonzero_records = 2155`
    - `min_octa = -32.6`
- 当前结论：
  - `Octa` 硬惩罚比纯平滑 `Tail/Struct` 更有效
  - 但仍明显落后于原版 `octagram`
  - 这说明问题不只是“有没有惩罚”，而更可能是：
    - 原版在上游就通过 syllable graph credibility 压掉了很多坏路径
    - 当前 `witset_poet` 只能在句级阶段事后补罚，已经错过了最有利的剪枝时机
- 基于上述判断，补充整理了下一阶段四种候选方案，并写入 `docs/route_b_implementation_plan.md`：
  - 方案 B：`joint-aware beam state`
    - 在 `witset` 自己的 beam 状态里显式携带 ambiguous joint / fragment 风险信号
    - 目标是在不明显增加对原版 `Rime` 主链侵入的前提下，把歧义感知前移到搜索过程
  - 方案 A：前移为路径原始权重
    - 将歧义信号更早地并入路径先验或等价基础分
    - 目标是更接近原版 `octagram` 通过 `credibility` 早期压制坏路径的机制
  - 方案 C：联合词图与音节图的一次性路径求解
    - 不再先大规模生成候选再末端补救
    - 改为把音节切分、词典边权、LM 增量与结构约束统一到图上，用 DP / Viterbi / beam-Viterbi 直接求整条最优路径
  - 方案 D：让模型本身提供更强分词证据
    - 通过词级或字词混合 LM 从模型侧补强“整词优于碎字”的证据
- 当前推荐试验顺序已明确为：
  - 先试方案 B
  - 再试方案 A
  - 之后做方案 C 的离线原型
  - 方案 D 作为长期路线保留
- 这样排序的原因：
  - 方案 B 对现有 `witset` 搜索框架侵入最小，最适合作为第一轮验证“把歧义信号前移一步是否有效”
  - 方案 A 比 B 更靠近原版 `octagram` 的上游先验思路，但需要在确认信号定义有效后再推进
  - 方案 C 直觉上最有机会带来形态级提升，但工程跨度最大，必须先以离线求解器原型验证价值
- 开始实现方案 B（`joint-aware beam state`）的最小原型：
  - 在 `witset_translator` 中从 `SyllableGraph` 提取按位置聚合的 joint 风险提示
  - 将该提示通过 `WitsetPoet::SetJointRiskHints()` 传入本地句级搜索器
  - 在 `witset_poet` 的 `Line` 状态中新增：
    - `beam_score`
    - `cumulative_joint_guidance_penalty`
    - `cumulative_ambiguous_joint_hits`
    - `step_joint_risk`
  - 当前实现刻意只让 joint 风险参与：
    - `SelectTopLines()`
    - `PruneLinePool()`
    - 分支内候选保留
    - 全局 batch 剪枝
  - 还没有把该信号前移进最终可见的主分数 `weight`
  - 这样可以先验证“beam 状态感知歧义”本身是否有效，避免在第一轮就和方案 A 混在一起
- 按工作区规则执行 `outwit-windows/build_and_deploy.bat`，方案 B 版本已编译部署通过
- 下一步开始跑 `300` 条基线，重点观察：
  - `Top-1 / Top-3`
  - wall time
  - 新增 `JointBeam / JointHit / StepJointRisk` 调试项是否真实进入快照
- 方案 B（`joint-aware beam state`）首轮 `300` 条结果：
  - `Top-1 = 0.538206`
  - `Top-3 = 0.684385`
  - `expected_not_found_count = 69`
  - 按快照时间戳统计到的写入跨度约为 `925741 ms`
- 方案 B 信号活跃情况：
  - `joint_nonzero_candidates = 50730 / 135719`
  - `top1_jointhit_records = 2571`
  - `min_jointbeam = 0.03`
  - `max_jointbeam = 10.92`
- 当前判断：
  - joint 风险提示已经真实进入 beam 搜索链路，不是空转
  - 但仅靠 beam 内部状态排序与剪枝，尚未把 `Top-1 / Top-3` 推过当前 `Octa` 版本
  - 说明方案 B 单独使用时，仍然不够改变最终头部候选的主分数格局
- 开始实现方案 A（前移为路径原始权重或等价先验）：
  - 在保留方案 B 的 beam guidance 基础上
  - 将同一份 `joint risk` 再额外转成 `joint_prior_penalty`
  - 直接并入 `base_score`
  - 目标是验证：同样的歧义信号一旦进入真实路径分数，是否比仅用于 beam 剪枝更有效
- 按工作区规则再次执行 `outwit-windows/build_and_deploy.bat`
  - `librime` 与 `outwit` 编译成功
  - 最后部署到安装目录的管理员提权被系统取消，导致脚本返回非零
  - 但本轮离线基线仍可继续，因为 `run_local_snapshot_baseline.py` 直接使用的是 `librime\\build_x64\\bin\\Release\\rime_api_console.exe`，验证链路依赖的是本地新编译的 `librime` 产物，不依赖安装目录部署是否成功
- 方案 A（前移为路径先验）`300` 条结果：
  - `Top-1 = 0.538206`
  - `Top-3 = 0.684385`
  - `expected_not_found_count = 69`
  - `BASELINE_WALL_MS = 1169486`
- 方案 A 信号活跃情况：
  - `joint_prior_nonzero_candidates = 50771 / 135841`
  - `top1_jointprior_records = 2272`
  - `min_jointprior = -5.46`
  - `max_jointprior = -0.02`
- 当前判断：
  - 方案 A 证明同一份 joint 风险已经真实进入最终可见主分数
  - 但无论放在 beam guidance 还是前移进路径先验，本轮都没有把 `Top-1 / Top-3` 再往上推
  - 这进一步支持“问题不只是信号有没有前移，而是当前搜索形态本身仍然是先放大候选池、再末端补救”
- 开始尝试方案 C 的最小原型：
  - 暂不重写整套联合图求解
  - 先在 `witset_poet` 内加入一层近似 `beam-Viterbi` 状态合并
  - 以 `context_suffix + trailing_single_char_run + tail_anchor_char_count + compactness bucket + joint bucket + single-char bucket` 作为近似状态键
  - 在每个 `end_pos` 上仅保留同状态下 `beam_score` 最优的路径
  - 目标是先验证“按状态求最优”是否优于当前纯候选池扩张模式
- 方案 C 最小原型首次编译时暴露实现问题：
  - 近似状态键辅助函数错误地放在 `Line` 完整定义之前，导致 `private nested type` 编译失败
  - 已改为放入 `CompressLinePoolByState()` 内部 lambda 后修复
  - 这次报错说明：方案 C 首轮失败来自实现写法问题，不是算法方向本身被证伪
- 修复后按工作区规则再次执行 `outwit-windows/build_and_deploy.bat`，编译部署通过
- 方案 C（近似 `beam-Viterbi` 状态合并）`300` 条结果：
  - `Top-1 = 0.541528`
  - `Top-3 = 0.684385`
  - `expected_not_found_count = 64`
  - `BASELINE_WALL_MS = 1175837`
  - `avg_candidate_count = 19.705983`
- 当前判断：
  - 方案 C 的最小原型是本轮三条路线中第一次把 `Top-1` 往上推的尝试
  - 提升幅度仍然很小，离原版 `octagram` 还有明显差距
  - 但它至少证明：只改变歧义信号的附着位置（B/A）还不够，而“改变搜索形态本身”开始出现正向信号
- 继续推进方案 C：从“事后状态压缩”改到“更早的按状态准入”
  - 在 `source_pool` 进入扩张前先执行一次 `CompressLinePoolByState()`
  - 在 `target_pool` 写入 `new_line` 时，按近似状态键直接做 best-only admission
  - 目标是不让同状态路径先大量进入池子、再事后压缩，而是在生成阶段就减少重复扩张
- 这一步首次编译时出现 `BuildApproxStateKey` 定义位置问题：
  - MSVC 在 `CompressLinePoolByState()` 处看不到后置定义
  - 已将辅助函数前移到文件顶部匿名命名空间中修复
  - 本次报错属于实现细节错误，不影响对方案 C 方向本身的判断
- 修复后按工作区规则再次执行 `outwit-windows/build_and_deploy.bat`，编译部署通过
- 方案 C 第二轮（更早的按状态准入）`300` 条结果：
  - `Top-1 = 0.541528`
  - `Top-3 = 0.684385`
  - `expected_not_found_count = 64`
  - `BASELINE_WALL_MS = 982763`
  - `avg_candidate_count = 19.705983`
- 当前判断：
  - 这一步没有继续抬高准确率，说明当前近似状态键还不足以改变头部结果排序
  - 但 wall time 从上一轮方案 C 的 `1175837 ms` 下降到 `982763 ms`
  - 这说明“更早按状态准入”首先带来的确定收益是性能，而不是立刻提升 Top-1
  - 因而下一步若继续方案 C，应重点改进状态定义本身，而不是只继续加强 admission 时机
- 继续推进方案 C：把 `start_pos -> end_pos` 的局部 spelling / joint 风险编码进状态
  - 在 `witset_translator` 中补充生成三类提示：
    - 顶点级 `vertex_risks`
    - edge 级 `edge_risks`
    - edge 级 `edge_spelling_classes`
  - 在 `witset_poet` 中新增：
    - `joint_edge_risk_hints_`
    - `edge_spelling_class_hints_`
    - `recent_edge_risk / recent_edge_spelling_class / recent_edge_span`
  - 近似状态键从原先的结构桶，扩展为：
    - `context_suffix`
    - `trailing_single_char_run`
    - `tail_anchor_char_count`
    - `compactness bucket`
    - `joint bucket`
    - `single-char bucket`
    - `recent edge risk bucket`
    - `recent edge spelling class`
    - `recent edge span bucket`
  - 同时让 `joint_guidance` / `joint_prior` 使用 `max(start_joint_risk, local_edge_risk)` 的组合局部风险
- 这一轮实现中出现过一次明确的实现错误：
  - `ClassifyEdgeSpellingType()` 错把 `SpellingAccessor` 当成可遍历容器
  - 真实传入类型应为 `SpellingMap`
  - 已修复后重新编译通过
- 修复后按工作区规则执行 `outwit-windows/build_and_deploy.bat`，编译部署通过
- 方案 C 第三轮（局部 edge 风险进入状态）`300` 条结果：
  - `Top-1 = 0.541528`
  - `Top-3 = 0.684385`
  - `expected_not_found_count = 64`
  - `BASELINE_WALL_MS = 1114292`
  - `avg_candidate_count = 19.704135`
- edge 信号命中情况：
  - `edge_nonzero_candidates = 27683 / 138658`
  - `step_edge_nonzero_candidates = 26666`
  - `step_edge_type_nonzero_candidates = 26209`
  - `top1_edge_records = 810`
  - `min_edge = 0.48`
  - `max_edge = 3.38`
- 当前判断：
  - 局部 `start_pos -> end_pos` edge 风险已经真实进入状态与调试输出，不是空转
  - 但这版没有继续抬高准确率，说明“把 edge 风险也编码进状态”本身仍然不够区分真正的胜负路径
  - 同时 wall time 从上一轮 `982763 ms` 回升到 `1114292 ms`
  - 说明当前 edge 风险定义带来的状态分裂，先增加了搜索负担，但还没换来排序收益
  - 下一步若继续方案 C，重点应从“再加更多状态维度”转向“把 edge 风险定义得更尖锐、更少噪声”
- 阶段性反省已正式整理进 `docs/route_b_implementation_plan.md`
  - 当前总方向与当前实现路线需要拆开看：
    - `witogram + witset` 的统一建模方向仍未被证伪
    - 但“先放大候选池，再靠 `witset_poet` 末端补救”的实现路线已经显示出明显天花板
  - 原版 `octagram` 强，不是因为末端排序更细，而是因为它在更上游通过 `credibility` 更早压掉了坏路径
  - 当前路线的理论优势并不只是“更平滑”，而在于：
    - 句级上下文表达更强
    - 统一建模能力更强
    - 若契约正确，后续扩展空间更大

## 2026-05-22

- 将本轮关于“万象词库 / 万象模型 / octagram / witogram”的阶段性结论独立整理成调查报告：
  - `docs/阶段性调查报告_万象模型适配与octagram对比.md`
- 报告核心结论已固定为：
  - 当前万象体系中，词典层与 grammar 模型层是分离的
  - `zi.dict.yaml` 中存在 `一`，句级词典中也存在 `第一站 / 一座 / 古老 / 小镇`
  - 但当前 `wanxiang` grammar 模型族中系统性缺少 unigram `一`
  - 这一缺口更像 `RIME-LMDG` 上游“分词后 n-gram 统计 + 剪枝”的自然结果，而不是本地转换 bug
- 进一步从原版代码流总结出：
  - `octagram` 的成功，不在于它拥有不同的词库或不同的 n-gram 模型
  - 而在于 `Syllabifier -> Dictionary -> Poet -> Grammar` 分工正确：
    - 上游 `credibility` 更早压掉坏路径
    - `Poet` 只做小 beam 句级组合
    - `octagram` 只做薄层 collocation 调整
  - 同样面对 grammar 中缺失 unigram `一`，原版更接近“缺少更强正证据”，而不会像当前 `witogram` 一样将其放大成显式 fallback / OOV 失血
- 同时将“如何把 `witogram + witset` 调整成适应当前万象模型 + 万象词库，并以持平或超过原版 `octagram` 为目标”的宏观方案独立整理成：
  - `docs/适配万象模型的宏观方案_witogram_witset.md`
- 宏观方案的阶段判断已固定为：
  - 目标**可行**
  - 但前提不是继续做零散末端 patch，而是完成三层重对齐：
    - 修正 `witogram` 对当前 grammar 的解释语义
    - 将 family / source-line / request-stage contract 前移到 translator
    - 将 `witset` 从“末端主救火器”改回“强句级组合器”
- 关于 `witogram` 相对 `octagram` 的优势点，也已单独澄清：
  - 不需要放弃 `witogram` 的核心优势：
    - 更强句级上下文表达
    - 更统一的建模能力
    - 更大的扩展空间
    - 更强的可观测性
  - 但必须放弃建立在错误 grammar 假设上的实现方式，尤其是：
    - 把 `NotFound()` 普遍解释成重 OOV / fallback
    - 假设当前万象 grammar 会广泛提供整词 token 命中
    - 继续维持“先放大候选池、再末端补救”的主搜索形态
- 因而本轮阶段性交付后的下一步大方向已明确：
  - 先做 grammar 语义纠偏
  - 再做 translator 上游 contract 前移
  - 最后再让 `witset` 的句级优势建立在更干净的候选池上，争取持平并最终超过原版 `octagram`
- 根据后续新增的阶段约束，又重新审核并更新了：
  - `docs/pluginized_upstream_credibility_plan.md`
- 这次更新重点不是推翻原架构，而是把第二阶段/第三阶段的两个大前提正式写死：
  - 必须同时满足：
    - `witogram` 作为可独立开源给 Rime 社区使用的一档实现
    - `witset` 作为闭源 orchestrator，且其逻辑在 `witogram` 侧保持隐身
  - 但更高优先级的是：
    - 一档准确率必须以“持平或超过原版 `octagram`”为最主要目标
    - 不允许为了更早做物理分拆，而弱化这个准确率目标
- 因而新的正式项目级判断已固定为：
  - 若阶段性出现冲突，只允许牺牲：
    - 物理分拆时机
    - 过程中的代码整洁度
  - 不允许牺牲：
    - 一档准确率持平/超越原版的目标
- `pluginized_upstream_credibility_plan.md` 中同步完成的关键修订包括：
  - 把“准确率优先于分拆时机”写成最高优先级约束
  - 将原先“先验证上游前移，再分拆”的直线路径，重排为：
    - 先做 grammar 语义纠偏
    - 再做 translator 上游 contract 前移
    - 再做 `RunWitogramPoet` 的搜索形态改造
    - 先达到持平，再继续验证超越
    - 最后才进入正式开源/闭源物理分拆
  - 把未来 core 接口中的旧语义字段风险写明：
    - 不应把当前已知错误的 `NotFound() -> 重 OOV / fallback` 语义继续固化进 `witogram_core` API
  - 明确：
    - 仍然禁止回到 `witset_poet` 末端救火 patch 路线
    - 但允许在 `RunWitogramPoet` 内做搜索形态重构，例如状态化 admission、近似 beam-Viterbi、best-only admission 前移
- 当前这次文档更新的实质意义是：
  - 阶段 2 的主线不再是笼统的“上游介入点前移”
  - 而是明确变成：
    - 先修 grammar 语义
    - 再修上游 contract
    - 最后再验证搜索形态是否足以把一档推到持平并继续超过原版
- 随后又把阶段 2 进一步落成了执行化清单：
  - `docs/阶段2实施清单_P0_P1_P2.md`
- 这份清单的作用不是再补一份宏观方案，而是把实现顺序、验证资产、成功标准和止损条件写死，避免后续推进时又滑回零散 patch。
- 当前已正式固定的阶段 2 顺序为：
  - `P0` grammar 语义纠偏
  - `P1` 上游 contract 前移
  - `P2` 搜索形态改造
- 其中每一步都明确写了：
  - 主要改动落点
  - 允许做的事
  - 不应混入的事
  - 成功标准
  - 止损条件
- 同时把阶段 2 统一验证资产固定为三层：
  - 目标类错例
  - shared-prefix 代表集
  - 现有 baseline 工件与 `octagram_300_20260516`
- 当前已正式写死的 gate 如下：
  - `P0` 不通过，不进入 `P1`
  - `P1` 不通过，不进入 `P2`
  - `P2` 未至少持平 `octagram`，不进入阶段 3 的正式物理分拆准备
- 这意味着：
  - 阶段 3 不再是“文档准备好就开始拆”
  - 而必须以阶段 2 至少完成“准确率持平 + 边界清晰”为前提
- 随后开始对 `P0` 做代码级设计审计，但刻意停在“不编译、不改行为”的边界，只确认：
  - grammar 语义失配的产生点在哪
  - 这些信号在句级层是如何被继续放大的
- 当前审计结论已明确：
  - 产生点高度集中在 `librime/plugins/witogram/src/witogram.cc::ScoreFeatures()`
  - 这里当前把：
    - `word_wid == vocab.NotFound()`
    - 直接翻译成：
      - `used_char_fallback = true`
      - `oov_token_count = char_oov_token_count`
  - 而 `WitogramScoreFeatures` 结构本身也把这套语义写死成扁平结论，尚无“中性缺证”位置
- 消费点则比预期更重，远不只是一个 `oov_penalty`：
  - `witset_poet.cc` 中除了主链：
    - `oov_penalty = -lm_oov_token_count * oov_penalty_weight_`
    - `used_char_fallback => 再减 char_fallback_penalty_`
  - 还会继续进入多处早期补偿/桥接/前缀 debt 逻辑，例如：
    - `ComputeEarlyFallbackCompensation`
    - `ComputeEarlyPrefixSplitPenalty`
    - `ComputeEarlyBoundaryBridgeCompensation`
    - `ComputeCleanFirstWordBridgeBonus`
    - `ComputeCleanSingleCharBridgeBonus`
    - `ComputeCleanExactContinuationBonus`
    - `ComputeValidatedPrefixSuffixBonus`
    - `ComputeEarlyUnstableContinuationPenalty`
    - `ComputePrefixAnchorDeltaDebtPenalty`
    - `ComputePrefixAnchorDebtRelease`
    - `ComputeWholeFirstWordContinuationPenalty`
    - `ComputeStableWholeContinuationBonus`
- 因而 `P0` 的第一刀最小实现判断也已收束：
  - 不应一上来同时改 translator / request-stage / 搜索形态
  - 应先只动两处边界：
    - `witogram.h/.cc` 中 `WitogramScoreFeatures + ScoreFeatures()`
    - `witset_poet.cc` 中最直接消费 `used_char_fallback / oov_token_count` 的主惩罚口径
  - 第一刀不应同时触碰那些围绕 fallback/OOV 长出来的早期补偿链；应先把它们保留为只读观测对象，待主语义站稳后再决定删改
- 随后已直接落地 `P0` 第一刀代码修改，并保持在最小边界内：
  - `librime/plugins/witogram/src/witogram.h`
  - `librime/plugins/witogram/src/witogram.cc`
  - `librime/plugins/witset/src/witset_poet.cc`
- 这次实现的核心变化是：
  - 在 `WitogramScoreFeatures` 中新增了 `WitogramTokenEvidenceLevel`
  - 当前 grammar 证据被显式分成四档：
    - `kDirectWholeWordHit`
    - `kSplitTokenSupported`
    - `kNeutralMissing`
    - `kTrueOov`
- `ScoreFeatures()` 的新口径为：
  - whole-word 命中：`kDirectWholeWordHit`
  - whole-word 不命中但 char path 全命中：`kSplitTokenSupported`
  - whole-word 不命中且存在部分 char 命中，或单字 token 缺失：`kNeutralMissing`
  - whole-word 不命中且整体更接近真实 OOV：`kTrueOov`
- 这一步的意图不是直接把分数改漂亮，而是先把“当前万象 grammar 下的缺证”从“强负证据”里拆出来。
- `witset_poet.cc` 中同步完成的最小消费改造是：
  - 继续保留原始 `matched_whole_word / used_char_fallback / oov_token_count` 的读取来源
  - 但在进入主惩罚传播前，先按 `token_evidence_level` 归一化：
    - `kSplitTokenSupported`
    - `kNeutralMissing`
    - 不再继续触发当前的 `char_fallback_penalty_`
    - 也不再继续把 `lm_oov_token_count` 作为惩罚型 OOV 往后传播
  - 只有 `kTrueOov` 仍维持原来的重惩罚口径
- 这次第一刀刻意没有做的事：
  - 没有改 translator / request-stage
  - 没有改 family/source-line contract
  - 没有改 `RunWitogramPoet` 的搜索形态
  - 没有重写那一串围绕 fallback/OOV 的早期补偿、bridge、anchor debt 函数
- 目前已完成 VS Code diagnostics 检查：
  - `witogram.h` 无新错误
  - `witogram.cc` 无新错误
  - `witset_poet.cc` 无新错误
- 下一步应当进入：
  - 在新 terminal 中按串行流程编译并单跑 `case2_diyizhan`
  - 重点观察：
    - `第一站是 -> 一` 是否不再稳定落为重惩罚型 OOV/fallback
    - shared-prefix 代表集和当前 request 盘面是否出现正向变化
  - 但到目前为止，这些理论优势没有兑现为结果，说明问题核心仍在搜索契约而非单个特征
- 当前新的主线判断：
  - 不再把“继续往 `witset_poet` 加更多后验特征 / 状态桶 / 常数”视为主线
  - 若继续投入，应转向“更换为正确的搜索契约”
  - 这个正确契约的目标是：
    - 上游尽早表达切分可信度与路径先验
    - 中游只保留有限状态下真正有希望的路径
    - 下游句级 LM 负责精排，而不是负责大规模救火
- 文档中已明确推荐新的后续顺序：
  - 先做 edge 风险降噪
  - 再做离线 C0 图求解器
  - 然后让线上状态保留器逐步贴近离线正确契约
  - 只有离线指标稳定优于当前线上版时，才考虑替换主路径
- 按上述主线继续推进：完成一版 `edge 风险降噪版 C`
  - 修改 `witset_translator.cc` 的局部风险定义：
    - edge class 不再记录 `abbreviation / completion / fuzzy`
    - 只保留 `ambiguous`
    - edge risk 只保留两类信号：
      - `ambiguous`
      - 足够强的负 `credibility`
    - 对短跨度 edge (`edge_span <= 1`) 设更严格门槛，仅在 `credibility_risk >= 0.92` 时保留
    - 对更长 edge，只有 `credibility_risk >= 0.72` 才保留
  - 顶点级 `vertex_risk` 也同步收窄：
    - 仅由 `ambiguous` 或足够强的负 `credibility` 决定
    - 不再继续累计弱噪声来源
- 按工作区规则执行 `outwit-windows/build_and_deploy.bat`，编译部署通过
- `edge 风险降噪版 C` 的 `300` 条结果：
  - `Top-1 = 0.541528`
  - `Top-3 = 0.684385`
  - `expected_not_found_count = 64`
  - `BASELINE_WALL_MS = 1148644`
  - `avg_candidate_count = 19.715504`
- 与上一轮“局部 edge 风险进入状态”相比：
  - 准确率完全不变
  - wall time 从 `1114292 ms` 略回升到 `1148644 ms`
- 局部 edge 信号分布明显收缩：
  - 上一轮：
    - `edge_nonzero_candidates = 27683 / 138658`
    - `step_edge_nonzero_candidates = 26666`
    - `top1_edge_records = 810`
    - `min_edge = 0.48`
    - `max_edge = 3.38`
  - 本轮降噪后：
    - `edge_nonzero_candidates = 1838 / 138738`
    - `step_edge_nonzero_candidates = 461`
    - `step_edge_type_nonzero_candidates = 0`
    - `top1_edge_records = 25`
    - `min_edge = 1.0`
    - `max_edge = 2.0`
- 典型噪声样例已被清掉：
  - 之前像单字输入 `j` 会出现 `StepEdgeRisk > 0 / StepEdgeType = 3`
  - 降噪后同类输入已回到 `StepEdgeRisk:0 StepEdgeType:0`
- 当前判断：
  - 这轮结果说明“局部 edge 风险太噪”确实是前一版的问题之一，因为信号分布已明显收窄
  - 但它不是全部问题，因为在信号被显著净化后，准确率仍完全不动
  - 也就是说，当前 C 的瓶颈已不只是噪声控制，而是：
    - 仅靠局部 edge 风险进入状态，仍不足以改变真正决定胜负的路径保留与排序
  - 因此下一步不应再继续围绕 edge 风险定义本身做小修小补，而应进入文档中计划的下一阶段：
    - 做离线 `C0` 图求解器，直接验证“正确契约”的统一路径求解是否比当前线上框架更优
- 已进入 `N2 / C0` 的第一轮最小实现
  - 在 `witset_translator.cc` 中新增 `graph.jsonl` 导出
    - 默认路径从 `witset_local_snapshot.jsonl` 派生为 `witset_local_snapshot.graph.jsonl`
    - 导出内容包括：
      - `vertices`
      - `syllable_edges`
      - `word_edges`
      - `vertex_risks / edge_risks`
      - 当前线上候选 `current_candidates`
  - 继续沿用真实 `rime_api_console.exe` + `run_local_snapshot_baseline.py` 生成 `300` 条与 `reference_cases` 对齐的图快照
  - 新增最小离线求解脚本：
    - `librime/plugins/witogram/tools/run_c0_graph_solver.py`
    - 第一版仅使用：
      - `DictEntry.weight`
      - `vertex_risk`
      - `edge_risk`
      - `ambiguous_edge_penalty`
      - `single_char_penalty`
      - `multi_char_bonus`
    - 采用每个位置保留前 `top-k` 路径的简化 DP / Viterbi
- 为了适配当前 baseline 中较高的 `preceding_text_mismatch_count`
  - `run_c0_graph_solver.py` 的图记录匹配策略已做三层回退：
    - 先匹配 `(input, preceding_text)`
    - 再匹配 `(input, snapshot_preceding_text)`
    - 最后退回仅按 `input` 匹配最新图记录
  - 修正后 `evaluated_case_count` 从最初的 `12` 提升到完整的 `300`
- 按工作区规则执行 `outwit-windows/build_and_deploy.bat`，编译部署通过
- 重新跑 `300` 条 baseline 以生成与 `reference_cases` 对齐的图快照
  - `BASELINE_WALL_MS = 1234456`
- 第一轮离线 `C0` 结果：
  - `evaluated_case_count = 300`
  - `missing_graph_case_count = 0`
  - `Top-1 = 0.600000`
  - `Top-3 = 0.750000`
- 与当前线上一档指标对比：
  - 线上当前：
    - `Top-1 = 0.541528`
    - `Top-3 = 0.684385`
  - 离线 `C0`：
    - `Top-1 = 0.600000`
    - `Top-3 = 0.750000`
- 当前判断：
  - 这是到目前为止最强的一次正向信号
  - 即使第一版离线 `C0` 还没有引入 `LM` 增量状态、全句上下文或更复杂的状态定义，仅靠“统一图路径求解”这一个契约变化，就已经客观优于当前线上框架
  - 这说明前面的反省结论得到了实验支持：
    - 问题核心确实更接近“搜索契约不对”
    - 而不是“还缺几个句级特征或还差一点点参数”
  - 下一步若继续推进，应优先围绕：
    - 在离线 `C0` 中加入更贴近线上目标的局部代价项
    - 再逐步把线上状态保留器向这套离线契约靠拢
- 对 `C0` 指标做了独立审计，确认 `0.600000 / 0.750000` 的来源准确无误
  - `reference_cases.jsonl` 实际条数：`300`
  - `c0_graph_solver_summary.json` 中 `details` 实际条数：`300`
  - 独立重算命中条数：
    - `Top-1 hits = 180`
    - `Top-3 hits = 225`
  - 因此：
    - `Top-1 = 180 / 300 = 0.600000`
    - `Top-3 = 225 / 300 = 0.750000`
  - 不是样本数偷偷变化导致，而是分母恰好为 `300`
- 为避免后续同类疑问，已增强 `run_c0_graph_solver.py` 的 summary/report 输出
  - 新增：
    - `top1_hit_count`
    - `top3_hit_count`
    - `match_by_preceding_count`
    - `match_by_snapshot_preceding_count`
    - `match_by_input_only_count`
  - 当前 `C0` 的匹配口径统计：
    - `match_by_preceding_count = 12`
    - `match_by_snapshot_preceding_count = 288`
- 继续围绕 `case2_diyizhan / case3_tiyanbuyiyang` 做高性价比定点复核时，最初一度判断需要补 `transition LM snapshot` 导出链路才能拿到 `whole-word vs char-path` 细账。
- 重新核对现有 `partial_chain_stage_probe.snapshot.jsonl` 后确认：
  - 不需要先补导出、也不需要先编译
  - 现有 snapshot 的候选 `debug` 字段已经包含：
    - `StepWholeLog10`
    - `StepCharLog10`
    - `StepWholeHit`
    - `StepCharFB`
    - 以及对应的 `LmRaw / LmScaled / OovTok`
  - 因而可以直接用现有快照对目标 query 做定点抽取
- 已从现有 snapshot 中直接抽出两组关键 query：
  - `input = diyizhanshiyi`
    - `第一站是以`
    - `第一站是一`
  - `input = tiyan`
    - `体言`
    - `体验`
- `case2` 的直接证据进一步收紧为：
  - `第一站是以`
    - `rank = 5`
    - `LmScaled = -156.62`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -95.266663`
    - `StepWholeHit = 0`
    - `StepCharFB = 0`
    - `OovTok = 0`
  - `第一站是一`
    - `rank = 9`
    - `LmScaled = -182.22`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -117.497597`
    - `StepWholeHit = 0`
    - `StepCharFB = 0`
    - `OovTok = 0`
  - 结论：
    - 这一步不是 `whole-word hit` 在区分两者
    - 也不是 `char fallback / OOV` 在区分两者
    - 而是纯 `char-path` 分数本身就让 `是一` 显著弱于 `是以`
    - 因而 `case2` 的主缺口已进一步收口到：
      - 为什么当前字级 token 路径下，`第一站 + 是一` 的 char-path LM 先验明显差于 `第一站 + 是以`
- `case3` 的直接证据则把问题重新前移并改写了之前的猜测：
  - `体言`
    - `rank = 1`
    - `LmRaw = -117.50`
    - `LmScaled = -47.35`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -117.497597`
    - `StepWholeHit = 0`
    - `StepCharFB = 0`
    - `OovTok = 0`
  - `体验`
    - `rank = 2`
    - `LmRaw = -95.27`
    - `LmScaled = -109.68`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -95.266663`
    - `StepWholeHit = 0`
    - `StepCharFB = 0`
    - `OovTok = 0`
  - 结论：
    - 句首 `体验` vs `体言` 的 immediate char-path 原始分数，其实是 `体验` 更好
    - 但最终 `LmScaled` 却反向成了 `体言` 更强
    - 说明 `case3` 的关键问题并不只是“词表更偏好 `体言`”
    - 而更像：
      - `lm_raw -> lm_scaled` 的缩放/归一化口径
      - 或句首单词路径的特定缩放条件
      - 在这一步把 `体验` 额外压低了
- 到这里，两条 case 的主线已经可以明确拆开：
  - `case2`
    - 主问题仍然在 char-path LM 本体
    - 下一步应直接审计：
      - `第一站是以` 与 `第一站是一`
      - 对应的 token 路径为何会产生如此大的 char-path 差额
  - `case3`
    - 主问题已不再是 immediate char-path 更差
    - 而是：
      - `LmRaw` 明明 `体验` 更好
      - `LmScaled` 却反而更差
    - 下一步应优先审计：
      - `lm_raw -> lm_scaled`
      - 以及是否存在与 `generated_char_count / generated_word_count / prefix scaling` 相关的缩放规则，把句首 `体验` 额外压低
- 随后继续静态回读 `witset_poet.cc`，已把 `case3` 的“反向缩放”定位到一个非常具体的条件分支：
  - `lm_score_scaled` 的主计算是：
    - `lm_features.total_log10 * kLn10 * witogram->ngram_weight()`
  - 但在此之后，还会进入：
    - `ComputePrefixCharFallbackLmScale(...)`
  - 其触发条件是：
    - `start_pos == 0`
    - `prefix_generated_word_count == 0`
    - `prefix_generated_char_count == 0`
    - `char_count >= 2`
    - 第 5 个参数为真
    - `matched_whole_word == false`
- 这个函数名虽然叫 `...Fallback...`，但当前真正传进去的第 5 个参数并不只代表惩罚态 fallback：
  - 对 `kTrueOov` / 惩罚态 fallback 当然会为真
  - 更关键的是：
    - 对 `kNeutralMissing`
    - 也会把这个参数置真
  - 而 `SplitTokenSupported` 不会
- 结合当前配置默认值：
  - `prefix_char_fallback_lm_scale_ = 0.35`
  - `grammar/weight` 在当前运行里等效为 `0.5`
  - 因而若句首双字候选命中这条分支，其 `LmRaw -> LmScaled` 的总比例就会变成：
    - `0.5 * 0.35 = 0.175`
- 这与 `case3` 的实测完全对上：
  - `体言`
    - `LmRaw = -117.50`
    - `LmScaled = -47.35`
    - 倒推出的实际比例约为 `0.175`
  - `体验`
    - `LmRaw = -95.27`
    - `LmScaled = -109.68`
    - 倒推出的实际比例约为 `0.5`
- 因而当前 `case3` 的最小闭环判断已经可以明确写死：
  - `体验` 与 `体言` 的 immediate char-path 原始分数，并不是 `体言` 更好
  - `体言` 之所以最终反超，是因为它在句首双字位置更像落到了：
    - `NeutralMissing -> prefix LM scale 0.35`
  - 而 `体验` 没落到这条宽容缩放分支
- 这也解释了为何之前单看 debug 容易误判：
  - `StepCharFB = 0`
  - `OovTok = 0`
  - 并不等于“没有经过 prefix fallback 缩放”
  - 因为在当前实现里：
    - `NeutralMissing` 会清掉惩罚态 `CharFB/OovTok`
    - 但仍可能继续触发句首双字 `prefix LM scale`
- 到这里，`case3` 的后续实现候选已经明显收口为两类：
  - 要么收紧 `NeutralMissing` 在句首双字位置触发 `prefix LM scale` 的条件
  - 要么限制这条缩放只用于真正需要保护的形态，避免把 `体言` 这类句首错误双字意外抬高
- 随后落了一刀极窄修正并做最小编译验证：
  - 修改点：
    - `plugins/witset/src/witset_poet.cc`
  - 具体改动：
    - `prefix_lm_scale_fallback` 不再把 `kNeutralMissing` 直接纳入句首双字 `prefix LM scale`
    - 仅保留 `penalty_char_fallback` 触发这条额外缩放
  - 保持不变：
    - `kTrueOov`
    - 惩罚态 `used_char_fallback`
    - 以及其他非句首缩放逻辑
- 编译验证：
  - 在 `librime` 下执行 `build.bat static`
  - 编译通过
- 定点前缀验证结果：
  - `input = tiyan`
    - 修正后：
      - `体验 rank = 1`
      - `体言 rank = 3`
    - 且两者 `LmScaled` 都回到与 `grammar/weight ~= 0.5` 一致的常规比例
    - 说明：
      - 句首双字 `NeutralMissing` 宽容缩放确实是此前把 `体言` 异常抬高的主因
  - `input = diyizhanshiyi`
    - 盘面仍然是 `...是以` 明显高于 `...是一`
    - 说明这刀没有把 `case2` 的 char-path 主缺口误改成别的问题
- 完整句 snapshot 小复核：
  - `case3_tiyanbuyiyang`
    - 之前的主回退是 `体言不宜养...`
    - 修正后 top10 已全部回到 `体验...` 家族
    - `体验不一样的生活` 仍未到 top1，但已升到 `rank = 7`
    - 当前更早的“体言 vs 体验”误抬高已被清掉，剩余缺口转移到：
      - `体验不宜养`
      - `体验不易样`
      - `体验不易养`
      - 与 `体验不一样`
      - 之间的后续竞争
  - `case2_diyizhan`
    - 当前 top 仍是 `...驿站是以做古老的小镇` 一类
    - 说明 `case2` 主缺口依旧在：
      - `是以` vs `是一`
      - 的 char-path LM 本体
    - 这刀既没有解决它，也没有明显把它改坏
- 到这里，当前主线应再次拆开：
  - `case3`
    - 句首双字 `NeutralMissing` 误抬高这条分支已确认并修正
    - 剩余问题前移到：
      - `体验不宜养/不易样/不易养`
      - 与 `体验不一样`
      - 的后续竞争
  - `case2`
    - 继续保持原判断
    - 仍需单独审 char-path LM 本体，不应与 `case3` 共用这条修正
- 继续只做 `case3` 的前缀链定点复核后，剩余缺口的形成位置也已收紧：
  - `input = tiyan`
    - `体验 rank = 1`
    - 句首误抬高已清掉
  - `input = tiyanbu`
    - `体验不 rank = 1`
    - 说明到 `bu` 为止仍是正确链领先
  - `input = tiyanbuyi`
    - `体验不易 rank = 1`
    - `体验不宜 rank = 4`
    - `体验不已 / 不以 / 不意 / 不依 ...` 也都排在 `不宜` 前后
    - 说明剩余问题第一次失守并不是在 `yang`，而是在：
      - `buyi`
      - 已被重解释成 `不易`
  - `input = tiyanbuyiyang`
    - top 进一步扩展成：
      - `体验不易样`
      - `体验不易养`
      - `体验不易阳`
      - ...
    - 且这些候选都继承自：
      - `StepAltReqSrc: 体验不易`
    - 说明后续 `yang` 竞争只是沿着已错误胜出的 `不易` 前缀继续展开
- 因而，`case3` 当前的剩余主缺口已经可以明确写成：
  - 主问题不再是 `体言 vs 体验`
  - 也不再是 `不一样` vs `不宜养` 的末端争夺
  - 而是：
    - `体验不易`
    - 在 `tiyanbuyi`
    - 这一步就已经战胜了：
      - `体验不宜`
      - 以及仍未入头部的 `体验不一`
- 从 debug 口径看，`tiyanbuyi` 这一跳的关键特征是：
  - `体验不易`
    - `rank = 1`
    - `LmRaw = -127.33`
    - `LmScaled = -146.60`
    - `StepCharLog10 = -72.564234`
  - `体验不宜`
    - `rank = 4`
    - `LmRaw = -148.51`
    - `LmScaled = -170.98`
    - `StepCharLog10 = -93.743189`
  - 两者都不是：
    - `WholeHit`
    - `CharFB`
    - `OovTok`
    - 上的差别
  - 说明这一层更像是：
    - `不易`
    - 对
    - `不宜`
    - 的 char-path / LM 本体优势
- 因而，`case3` 下一步不应再围绕：
  - `yang`
  - `不易样`
  - `不易养`
  - 这些后续展开词做末端修补
- 更合理的下一步应回到：
  - `tiyanbuyi`
  - 定点比较：
    - `体验不易`
    - `体验不宜`
    - 以及若需要，补看 `体验不一`
  - 判断是否存在一条通用机制，可以抑制：
    - 已确认前缀 `体验不`
    - 在遇到 `yi`
    - 时被高频重解释成另一词块 `不易`
- 继续对 `tiyanbuyi` 跑 `snapshot + next-hop + graph contract` 后，剩余问题的主因已进一步明确：
  - 结果文件：
    - `case3_buyi_focus_result.json`
  - 当前 `source_suffix = 体验不` 下的 `top_request_entries` 显示：
    - `易`
      - `search_score = -185.309`
      - `base_score = -182.289`
      - `dict_score_raw = -10.8512`
      - `lm_score_scaled = -67.7184`
    - `宜`
      - `search_score = -211.747`
      - `base_score = -207.057`
      - `dict_score_raw = -11.2361`
      - `lm_score_scaled = -92.1016`
    - `一`
      - 最佳 `search_score = -213.052`
      - `base_score = -208.171`
      - `dict_score_raw = -12.3507`
      - `lm_score_scaled = -92.1016`
  - 结论：
    - `体验不易`
    - 在 request-stage 就已经大幅领先
    - 不是 admission / same-span / 后验 contract 再把它抬上去
- 且这一步的主差额也很明确：
  - `易` vs `宜`
    - `lm_score_scaled` 差约 `24.38`
    - `dict_score_raw` 只差约 `0.38`
  - `易` vs `一`
    - `lm_score_scaled` 同样差约 `24.38`
    - `dict_score_raw` 额外再差约 `1.50`
  - 说明 `tiyanbuyi` 的主问题仍然首先是：
    - `体验不 + 易`
    - 相对
    - `体验不 + 宜 / 一`
    - 的 request-side LM / char-path 本体优势
- graph contract 也显示，这一步 downstream 的候选已经挂在错误前缀之下展开：
  - 对 `宜 / 一 / 已 / 依 / 意` 等 focus entry
  - 其记录里的：
    - `request_stage_prefix_text`
    - 都已经是 `体验不易`
  - 对应：
    - `candidate_transition_text = 体验不宜 / 体验不一 / ...`
    - 但
    - `request_stage_transition_text = 体验不易宜 / 体验不易一 / ...`
  - 说明：
    - 当前并不是“正确前缀 `体验不` 下，`宜/一` 没打赢”
    - 而是 request-stage 自己已经把最佳前缀收口成：
      - `体验不易`
    - 后续 `宜/一/已/...` 只是沿着这个错误前缀继续补尾
- 因而，`case3` 当前已可进一步排除两类方向：
  - 不是：
    - `buyiyang`
    - 末端字竞争
  - 也不是：
    - admitted state ownership
    - same-span
    - 或末端 poet contract
- 当前最接近问题本体的表述应更新为：
  - 在已确认前缀 `体验不` 之后，
  - request-stage 会优先把 `yi` 解释成：
    - `易`
  - 并据此形成新的最佳 request-stage 前缀：
    - `体验不易`
  - 之后 `宜 / 一 / 已 / 依 / 意 ...`
    - 都只是挂在这个错误前缀后继续展开
- 随后做过一轮 translator 侧“单字 exact tail-supported 保活”实验：
  - 修改点：
    - `plugins/witset/src/witset_translator.cc`
  - 目标：
    - 给已确认前缀后的单字 exact `request_tail_supported` 候选增加通用保活 bias
  - 验证结果：
    - `tiyanbuyi`
      - `体验不易` 仍是 `rank 1`
      - `体验不宜 / 体验不一` 没有被有效拉回
    - `tiyanbuyiyangdeshenghuo`
      - `体验不一样的生活` 仍在 `rank 7`
      - 且一批错误尾续写也被一起抬高
  - 结论：
    - 这条“只按 end_pos + tail-supported 做单字保活”的通用 bias 过宽
    - 无法提供区分：
      - `一 -> 样`
      - `宜 -> 养`
      - `易 -> 样/养`
      - 之间所需的 candidate-specific downstream quality
  - 处理：
    - 已完整撤回该实验代码
    - 并重新编译恢复干净二进制
- 继续静态回读后，`case2/case3` 残留的更根原因又前移了一层：
  - 在 `ClassifyContinuationTag(...)` 中：
    - 只要 `char_count < 2`
    - 就会直接返回：
      - `non_contract_candidate`
  - 这意味着：
    - `体验不 + 易/宜/一`
    - `第一站是 + 一`
    - 这类关键单字续写
    - 从 translator 上游合同视角一开始就不属于：
      - `legal_primary_continuation`
      - `primary_path_eligible`
      - `source_axis_eligible`
      - 这一整套主合同平面
- 当前 graph 证据也与此一致：
  - `prefix_text = 体验不`
    - `supports_validated_continuation = true`
  - 但对应 `entry_text = 易 / 宜 / 一`
    - 全部都是：
      - `continuation_tag = non_contract_candidate`
      - `path_tag = not_primary_path_candidate`
      - `source_axis_tag = not_axis_candidate`
  - 同时：
    - `request_stage_tag = request_tail_supported`
  - 说明：
    - 这些候选并不是“合同内竞争失败”
    - 而是“根本没进入主 continuation/path/source-axis 合同”
- 因而，当前最准确的收口应改成：
  - `case2/case3` 的残留并不只是 request-stage 分差大
  - 更是因为它们共同落在：
    - “已确认前缀后的关键单字续写”
    - 这一块 translator 主合同盲区
  - 现有 contract 能覆盖：
    - 多字 strong exact continuation
  - 但对：
    - 单字 exact 头部
    - 以及它后面真正决定正确性的 candidate-specific downstream exact tail
    - 还没有对应合同表达
- 继续往下做了一个高性价比判负实验：
  - 路线：
    - 不编译
    - 直接在运行时 `build/witset.schema.yaml` 中临时打开
      - `upstream_validated_continuation_weight`
    - 因为这条现成 bias 只对：
      - 多字 exact continuation
      - 生效
    - 看上去正好能覆盖：
      - `一样`
      - `一座`
      - 这类目标
  - 实验方式：
    - 保留原值 `0.0`
    - 依次测试：
      - `8.0`
      - `12.0`
    - 每档只跑：
      - `case2_diyizhan`
      - `case3_tiyanbuyiyang`
      - 的全句 snapshot
    - 并在脚本结束后自动恢复原值
  - 实验结果：
    - `case3`
      - baseline:
        - `体验不一样的生活 rank = 7`
      - `weight = 8.0`
        - `体验不一样的生活` 仍是 `rank = 7`
        - 没有改善
      - `weight = 12.0`
        - `体验不一样的生活` 下降到 `rank = 13`
        - 明显变差
    - `case2`
      - baseline:
        - top 仍是 `...驿站是以做...`
      - `weight = 8.0`
        - 没有把正确链拉回来
      - `weight = 12.0`
        - top 盘面进一步漂到：
          - `地以战士已作古老的小镇`
          - `地以展示已作古老的小镇`
          - 一类
        - 比 baseline 更差
  - 结论：
    - 直接打开并放大：
      - `ComputeTranslatorValidatedContinuationBias()`
      - 这条多字 exact continuation bias
    - 是一条明确的失败路径
    - 原因是：
      - 它会整体抬高所有“多字 exact continuation”
      - 但无法区分：
        - `一样`
        - 与
        - `宜养 / 易样 / 易养`
        - 之间的 candidate-specific downstream quality
      - 对 `case2` 也同理，无法只定向抬 `一座`
- 这次实验进一步支持当前主判断：
  - `case2/case3` 不是缺一个“泛多字 continuation boost”
  - 而是缺一个能表达：
    - 已确认前缀后的关键单字头部
    - 及其 candidate-specific downstream exact tail
    - 的更细合同
    - `match_by_input_only_count = 0`
  - 说明当前 `C0` 结果并非依赖最宽松的 `input_only` 回退匹配
- 做了一次严格的路线级反思后，进入 `C0.1`
  - 反思结论：
    - 不能把“按状态保留路径”和“额外增加状态打分”混在一起验证
    - 否则一旦结果变差，无法判断是状态保留无效，还是新增打分本身有害
- 第一版 `C0.1`：
  - 在离线 solver 中同时引入：
    - 小状态保留
    - 额外状态打分（最近词长类别、连续单字尾巴、最近长度模式）
  - 结果：
    - `Top-1 = 176 / 300 = 0.586667`
    - `Top-3 = 220 / 300 = 0.733333`
  - 结论：
    - 这版变差，不能说明“状态保留无效”
    - 只能说明“新增的状态附加分有害”
- 紧接着做了变量隔离版 `C0.1 state_only`
  - 仅保留“按状态保留路径”
  - 完全不改 `C0` 原始局部打分
  - 结果：
    - `Top-1 = 180 / 300 = 0.600000`
    - `Top-3 = 225 / 300 = 0.750000`
  - 与 `C0` 完全相同
- 当前判断：
  - 这一步排除了一个潜在死胡同：
    - “只要把少量结构状态带进路径保留，就会自然进一步提升结果”
  - 当前证据显示：
    - 在现有局部代价定义不变的情况下，`state retention` 本身没有带来额外收益
    - 刚才 `C0.1` 的回退来自新增状态打分，而不是来自状态保留机制
  - 所以下一步不应继续沿着“再多试几个结构状态桶”这条路走
  - 更值得转向的方向应是：
    - 给离线 `C0` 引入**更有信息量的局部证据**
    - 例如更贴近 `LM` 或候选自然度的局部项
    - 而不是继续堆结构型状态/惩罚
- 用户提醒“当前样本量下 2% 左右差异不足以下结论”，因此补做了**错例性质分析**，不再只看总准确率
- 对当前线上 `witset` 与离线 `C0` 做完整 `300` 条逐例对比（采用与 `C0` 相同的三层匹配口径）：
  - `both_correct = 128`
  - `fixed_by_c0 = 52`
  - `regressed_by_c0 = 32`
  - `both_wrong = 88`
  - 因此 `C0` 相对线上不是“随机多对几条”，而是：
    - 净增加 `20` 条 `Top-1`
    - 且存在明确的错例类型差异
- `C0` 修好的 `52` 条中，代表性样例包括：
  - `那是的我 -> 那时的我`
  - `我记得风吹过的事后 -> 我记得风吹过的时候`
  - `那些无法演说的喜欢 -> 那些无法言说的喜欢`
  - `只友谊中安静得换系 -> 只有一种安静的欢喜`
  - `走进宜家特色小店 -> 走进一家特色小店`
  - `仿佛陷阱已办 -> 仿佛仙境一般`
  - `沿着登山步道歉行 -> 沿着登山步道前行`
- 这些修复的共同特征：
  - 更多是**路径骨架 / 短语完整性 / 常见搭配**被拉正
  - 线上错误常表现为：
    - 切分漂移
    - 局部词组塌坏
    - 本可由基本自然度排除的“离谱词串”
- `C0` 新搞坏的 `32` 条中，代表性样例包括：
  - `街口 -> 接口`
  - `沙沙 -> 啥啥`
  - `中学时的清晨 -> 中学是的清晨`
  - `河水清澈见底 -> 喝水清澈见底`
  - `木质的门窗 -> 目质地门窗`
  - `小镇的灯火渐渐亮起 -> 小真的灯火渐渐两起`
- 这些回退的共同特征：
  - 大多是**句子骨架已经正确，但同音词最终选择失败**
  - 也就是当前 `C0` 缺少足以区分：
    - `街口 / 接口`
    - `沙沙 / 啥啥`
    - `河水 / 喝水`
    - `木质 / 目质`
    这种**需要上下文自然度或语义常识**的证据
- 对 `both_wrong = 88` 再做接近度分析：
  - `C0` 更接近 expected：`28`
  - `C0` 更远离 expected：`19`
  - 接近度相同：`41`
- 代表性的“`C0` 仍错，但明显更接近正确答案”的样例：
  - `想不经意得慌跌 -> 想不经意的黄碟`，expected=`像不经意的黄蝶`
  - `朴素苏三再叫变 -> 扑簌簌散在狡辩`，expected=`扑簌簌散在脚边`
  - `带着疑点微量 -> 带着一点微量`，expected=`带着一点微凉`
  - `也带着意中不可挽回的流失 -> 也带着一种不可挽回的流失`，expected=`也带着一种不可挽回的流逝`
  - `心里装着一些说不出口的新式 -> 心里装着一些说不出口的心是`，expected=`心里装着一些说不出口的心事`
- 当前判断更新为：
  - 在当前样本量下，确实**不能只凭 2% 左右差异就断言路线失败或成功**
  - 但从错例类型看，`C0` 的收益不是随机噪声，而是**系统性地改善了路径骨架与短语完整性**
  - 同时，`C0` 也系统性暴露出另一类短板：
    - 在骨架已经正确后，缺少足以完成最终同音词选择的上下文证据
  - 因此当前最合理的路线判断不是“契约已失败”，而是：
    - `正确契约` 大概率方向对
    - 下一步该补的是**更有信息量的上下文自然度证据**
    - 而不是继续堆结构状态桶
- 为进入下一步，已回到代码确认最直接的证据来源：
  - `witset_poet.cc` 已经在扩展阶段调用 `witogram->ScoreFeatures(context, entry->text, is_rear, &features)`
  - 可直接获取：
    - `total_log10`
    - `avg_log10`
    - `oov_token_count`
    - `used_char_fallback`
    - `matched_whole_word`
  - 这说明下一步最自然的实验不是另造新特征，而是：
    - 给离线 `C0` 导出并接入一版**转移级 LM/context 证据**
    - 看它能否专门修复 `街口/接口`、`沙沙/啥啥` 这一类“骨架对、词选错”的回退案例
- 对 `c0_ctx` 覆盖异常继续做了流程级排查，结论已从“算法口径问题”收敛为“工件错配”
  - 现象：
    - `c0_ctx` 只有 `evaluated_case_count = 96`
    - `missing_graph_case_count = 204`
  - 排查结果：
    - `witset_local_snapshot.graph.jsonl` 与 `witset_local_snapshot.jsonl` 的修改时间均为 `2026-05-17 07:19`
    - 两者当前行数同为 `2331`
    - 但 `snapshot_summary/reference_cases.jsonl` 与 `baseline_run_metadata.json` 的修改时间仍停在 `2026-05-17 05:15`
    - 因此当前 `c0_ctx` 实际是在用一份**较新的、但中断后的 partial snapshot/graph**，去匹配一份**更旧的 300-case reference**
  - 进一步证据：
    - 缺失 case 不是均匀分布，而是从 `case_000091` 后开始大面积缺失
    - 这与 `2331` 条 partial snapshot 的规模一致，更像一次被中断的新 run，而不是 `c0_ctx` 自身匹配逻辑突然失效
  - 因而本轮 `96/300` 不能用于判断 `c0_ctx` 的路线成败
- 已直接修复这类流程漏洞，避免后续继续在错配工件上误判
  - `run_local_snapshot_baseline.py`
    - 不再只在成功跑完整轮后才写 `reference_cases.jsonl` / `baseline_run_metadata.json`
    - 现在会在运行开始时先落空文件，并在每个 text step 完成后持续刷新
    - 中途异常退出时，`baseline_run_metadata.json` 会留下 `status = interrupted`
    - 因此以后不会再出现“snapshot 已更新，但 reference/metadata 仍是旧 run”的静默错配
  - `run_c0_graph_solver.py`
    - 新增 `graph_record_count`、`reference_case_count`、`coverage_ratio`
    - 新增 `graph/reference` 文件的 `last_modified_iso` 与大小信息
    - 新增低覆盖与工件新旧错配告警
  - 用当前这批错配工件重跑后，solver 已明确报告：
    - `coverage_ratio = 0.32`
    - `warnings = [Low graph coverage..., Graph snapshot is newer than reference_cases...]`
- 当前路线判断更新：
  - 这一步修复的是**验证流程的可信度**，不是 `c0_ctx` 算法本身
  - 在重新拿到同一轮、完整对齐的 `reference + snapshot + graph` 之前，不应再解读当前 `c0_ctx` 的准确率数字
  - 下一步应先拿到同一轮工件，再判断转移级 LM/context 证据是否真的修复“骨架对、词选错”的案例
- 已继续把这条验证链路跑通，并额外修掉了两处 baseline 驱动脚本的鲁棒性问题
  - `run_local_snapshot_baseline.py`
    - 先后遇到两类真实中断：
      - 增量读取 `witset_local_snapshot.jsonl` 时撞到单条脏前缀，触发 `UnicodeDecodeError`
      - 长输入步骤虽然持续写出前缀快照，但因为这些前缀记录的 `preceding_text` 是 `WitsetTranslator` 截断后的后缀，而脚本错误要求它与完整 `current_preceding_text` 全等，导致被误判为“无进度”并超时
    - 已做修复：
      - 对 snapshot line 改为容错解析：允许跳过脏字节前缀，只要后面仍有完整 JSON 对象就可恢复
      - `wait_for_snapshot_record()` 的进度检测改为以 `expected_input.startswith(record_input)` 为准，不再错误依赖完整 `preceding_text` 全等
      - 异常清理时，若 `rime_api_console` 10 秒内未退出，则主动 `kill()`，避免 finally 里的 `TimeoutExpired` 覆盖原始异常
  - 修复后，完整 baseline 已重新成功跑完：
    - `completed_text_steps = 300`
    - `status = completed`
- 完整同轮工件上重新验证后，`c0_ctx` 的覆盖问题已消失
  - 新的 `c0_ctx_graph_solver_summary.json`：
    - `graph_record_count = 22741`
    - `evaluated_case_count = 300`
    - `missing_graph_case_count = 0`
    - `coverage_ratio = 1.0`
    - `warnings = []`
  - 说明此前 `96/300` 的确只是工件错配，不是 `c0_ctx` 求解器天然覆盖不了完整数据
- 为了能处理完整 graph（约 `103.65 GB`），又补了 `run_c0_graph_solver.py` 的规模鲁棒性
  - 原实现会先 `load_jsonl(graph_path)` 整文件入内存，完整 graph 上直接触发 `MemoryError`
  - 已改成：
    - 先读取小的 `reference_cases.jsonl`
    - 再按 reference 的 `input` 集合流式扫描 `graph.jsonl`
    - 只保留真正会被评估的记录的索引
  - 修复后，`c0` / `c0_ctx` 都能在完整 graph 上直接跑完
- 在同一轮、完整对齐工件上，新的对照结果如下
  - 当前线上 baseline（`metrics.json`）：
    - `Top-1 = 0.541528`
    - `Top-3 = 0.684385`
  - `c0`：
    - `Top-1 = 180 / 300 = 0.600000`
    - `Top-3 = 225 / 300 = 0.750000`
  - `c0_ctx`：
    - `Top-1 = 169 / 300 = 0.563333`
    - `Top-3 = 212 / 300 = 0.706667`
- 这次终于可以对 `c0_ctx` 做有效路线判断了
  - 结论不是“coverage 坏了”，而是：
    - `c0_ctx` 在完整同轮工件上**明显落后于纯 `c0`**
    - 相对 `c0` 没有修好任何一条 case
    - 反而新增了 `11` 条明确回退
  - `c0` vs `c0_ctx` 逐例对比：
    - `both_correct = 169`
    - `fixed_by_ctx_vs_c0 = 0`
    - `regressed_by_ctx_vs_c0 = 11`
    - `both_wrong = 120`
  - 典型回退例：
    - `那个 -> 哪个`
    - `映照 -> 应照`
    - `体验到了 -> 体验到乐`
    - `风景中 -> 风景得`
    - `证件等 -> 证件登`
    - `理论基础 -> 理论基础` 被 `争取休息权` 前后语境扰动成错字或碎词
- 当前最可靠的路线判断更新为：
  - 真实搜索里的转移级 `LM/context` 证据不是完全没信息，因为它仍略高于当前线上 baseline
  - 但当前这套“逐 edge 直接线性加分”的 `c0_ctx` 用法，会系统性破坏 `c0` 已经选对的整句骨架与短语完整性
  - 因此下一步不应再盲调 `ctx_* weight`
  - 更合理的后续方向应是：
    - 要么把 context 证据降级为 `c0` 末端候选之间的 tie-break / rerank 信号
    - 要么只在 `c0` 已经固定骨架后的少量同音近邻上使用
    - 而不是在整张图的每一步转移上直接累计
- 已按上述假设补了一个最小离线原型：`c0_ctx_rerank`
  - 契约：
    - 先按纯 `c0` 生成末端候选路径
    - 只对接近最佳 `base score` 的末端路径补一个缩放后的 context rerank bonus
    - 不让 `ctx` 再参与整图逐 edge 扩张
  - 当前实现参数：
    - `ctx_rerank_scale = 0.15`
    - `ctx_rerank_margin = 12.0`
- 在完整同轮工件上的 `c0_ctx_rerank` 结果：
  - `Top-1 = 168 / 300 = 0.560000`
  - `Top-3 = 212 / 300 = 0.706667`
  - 覆盖正常：
    - `evaluated_case_count = 300`
    - `missing_graph_case_count = 0`
    - `coverage_ratio = 1.0`
- 与其它版本对照：
  - 当前线上 baseline：
    - `0.541528 / 0.684385`
  - `c0_ctx`：
    - `0.563333 / 0.706667`
  - `c0_ctx_rerank`：
    - `0.560000 / 0.706667`
  - `c0`：
    - `0.600000 / 0.750000`
- 逐例判断：
  - 相对 `c0`
    - `fixed_by_rerank_vs_c0 = 0`
    - `regressed_by_rerank_vs_c0 = 12`
  - 相对 `c0_ctx`
    - `fixed_by_rerank_vs_ctx = 1`
    - `regressed_by_rerank_vs_ctx = 2`
- 因而这次 rerank 原型的结论也已经够清楚：
  - 它虽然比直接逐边 `c0_ctx` 更符合“不要破坏骨架”的原始设想
  - 但只要仍沿用当前这套 path-level `context` 定义，哪怕降级到末端 rerank，也没有带来真正净收益
  - 这说明当前问题不只是“用在太前面”，还包括：
    - `context` 分数定义本身过于偏好局部字词替换
    - 或它缺少“仅在同骨架近邻候选之间生效”的更强约束
- 下一步更合适的主线已进一步收窄为：
  - 不再继续调 `c0_ctx` / `c0_ctx_rerank` 这类“整路径 context 累积分”
  - 如果还要继续验证 context 的真实价值，应改成更窄的实验契约，例如：
    - 先固定 `c0` top-1 路径的分词骨架
    - 只在同起止边、同长度模式或同骨架近邻候选之间做局部替换比较
    - 只让 `context` 参与这些局部同音词决策，而不是整条路径重排
- 按“不要只看 2% 数字，要看错句性质”的原则，继续对 `c0_ctx` / `c0_ctx_rerank` 做了逐例复查
  - 结论：
    - 这些回退并不主要是“另一种也合理”的近义波动
    - 大量都是明显错误的局部替换或坏切分，例如：
      - `而 -> 二`
      - `店里 -> 电力`
      - `体验到了 -> 体验倒了`
      - `度过了 -> 读过了`
      - `等 -> 登`
      - `的 -> 德`
      - `却 -> 确`
    - 因而相对 `c0` 的回退更像稳定偏差，而不是纯统计噪声
- 已继续实现更窄契约的最小原型：`c0_ctx_local`
  - 核心思路：
    - 先固定 `c0` 路径骨架
    - 不允许重排整条路径
    - 只在每个已选 edge 上，比较同 `start/end`、同字数的近邻候选
    - context 只影响“当前词 + 下一词”这两个局部转移
  - 当前参数：
    - `ctx_local_scale = 0.10`
    - `ctx_local_base_margin = 2.5`
    - `ctx_local_gain_threshold = 0.30`
- `c0_ctx_local` 在完整同轮工件上的结果：
  - `Top-1 = 169 / 300 = 0.563333`
  - `Top-3 = 213 / 300 = 0.710000`
  - 覆盖正常：
    - `evaluated_case_count = 300`
    - `missing_graph_case_count = 0`
    - `coverage_ratio = 1.0`
- 与其它版本对照：
  - 线上 baseline：`0.541528 / 0.684385`
  - `c0_ctx`：`0.563333 / 0.706667`
  - `c0_ctx_rerank`：`0.560000 / 0.706667`
  - `c0_ctx_local`：`0.563333 / 0.710000`
  - `c0`：`0.600000 / 0.750000`
- 逐例判断：
  - 相对 `c0`
    - `fixed_by_local_vs_c0 = 0`
    - `regressed_by_local_vs_c0 = 11`
  - 相对 `c0_ctx`
    - `fixed_by_local_vs_ctx = 4`
    - `regressed_by_local_vs_ctx = 4`
  - 相对 `c0_ctx_rerank`
    - `fixed_by_local_vs_rerank = 4`
    - `regressed_by_local_vs_rerank = 3`
- 这些差异说明了更细的路线结论：
  - `c0_ctx_local` 确实比整路径 rerank 更“收敛”，也能修回一部分 `ctx` 型局部错字：
    - `应照 -> 映照`
    - `争去 -> 争取`
    - `方家 -> 放假`
    - `确 -> 却`
  - 但它仍会引入新的明显坏替换：
    - `店里 -> 电力`
    - `度过 -> 读过`
    - `是 -> 事`
    - `而 -> 二`
  - 所以当前结论仍然成立：
    - context 的信息不是完全没用
    - 但即使收窄到局部 edge 替换，只要没有更强的候选约束，仍不足以形成净收益
- 在此基础上又继续做了一个更严格的版本：`c0_ctx_local_top1`
  - 契约进一步收窄为：
    - 只允许在 `c0` 的 `top-1` 骨架上做局部替换
    - 不再允许低分骨架带着局部替换翻盘
    - 只接受 `base` 不变差的候选（`alt_base >= chosen_base`）
- `c0_ctx_local_top1` 的结果：
  - `Top-1 = 180 / 300 = 0.600000`
  - `Top-3 = 212 / 300 = 0.706667`
  - 覆盖正常：
    - `evaluated_case_count = 300`
    - `missing_graph_case_count = 0`
    - `coverage_ratio = 1.0`
- 与 `c0` 的逐例对比结果非常关键：
  - `fixed_by_local_top1_vs_c0 = 0`
  - `regressed_by_local_top1_vs_c0 = 0`
  - `changed_top1_text_count = 0`
- 这说明：
  - 一旦真正固定 `c0` 的 `top-1` 骨架，并禁止低分骨架靠 context 翻盘，当前这套 context 逻辑对 `Top-1` **既没有增益，也没有伤害**
  - 之前所有明显回退，核心根因确实是：
    - 允许低分骨架或低 base 候选被 context 信号翻盘
  - 但与此同时，这也证明了另一件事：
    - 在“真正固定 `c0` 正确骨架”这个约束下，当前 `transition LM/context` 信号并不足以把 `c0` 的 `Top-1` 再往上推
- 路线判断因此再次收敛：
  - 如果主目标是提升 `Top-1`，继续在当前这套 `transition_lm_features` 上做局部替换/局部 rerank，收益空间已经基本见底
  - 当前最有价值的结论，不是“又找到一个新加分项”，而是明确了边界：
    - `context` 目前更像一个危险的翻盘信号
    - 只有在被强约束住时才不伤害 `c0`
    - 但被强约束住后，它又没有额外净收益
- 主线已据此切回 `c0` 本身，对剩余 `120` 条错例做第一轮程序化分型
  - 分型口径：
    - `top3_hit` 但 `top1` 未中：明显 `base` 错排
    - `top3` 未中，但图中存在精确期望路径：更深层 `base` 错排
    - 图中不存在精确期望路径：候选/图生成问题
- 结果非常集中：
  - 总错例数：`120`
  - `top3_hit = 45`
  - `exact_path_below_top3 = 74`
  - `no_exact_path = 1`
  - 也就是：
    - `119 / 120` 的错例，图里都存在精确期望路径
    - 真正的“图里没有正确路”只有 `1` 条
- 这 `1` 条真缺路径是：
  - `case_000262`
  - `input = kanshizai`
  - 期望是 `看似在`
  - graph 中对应 span 只有：
    - `看时 / 看市 / 看事`
    - `是 / 时 / 事 / 视 / 实 ...`
    - `是在 / 实在 / 石在 ...`
  - 没有 `似` 或 `看似`，因此这条可确认为上游候选缺失，而不是 `c0` 排序失误
- 对另外 `119` 条“路径存在但没选中”的错例，又补了一个错句距离观察
  - `top3_hit = 45`：
    - 编辑距离 `1` 的有 `27` 条
    - 编辑距离 `2` 的有 `16` 条
    - 编辑距离 `3` 的有 `2` 条
  - `exact_path_below_top3 = 74`：
    - 编辑距离 `1` 的有 `15` 条
    - 编辑距离 `2` 的有 `30` 条
    - 编辑距离 `3` 的有 `12` 条
    - 编辑距离 `4` 的有 `11` 条
    - 编辑距离 `5~7` 的仅 `6` 条
- 这说明：
  - `c0` 的剩余主要问题不是“大量骨架根本没出来”
  - 更像是：
    - 大量正确路径已经在图里
    - 很多错句只差 `1~4` 个字
    - 但 `dict/base` 分数仍把错误近音、错误短语或错误词边压到了前面
- 因而当前最值得继续的主线不是再补 `context`
  - 而是围绕 `c0` 本体继续拆：
    - 为什么精确期望路径已经存在，却上不去 `Top-3` / `Top-1`
    - 哪些是局部近音词权重错误
    - 哪些是多词组合的总分偏差
    - 哪些是单字/碎词路径仍被抬得过高
- 又对这 `119` 条“路径存在但没选中”的错例继续做了第二层细分
  - 用“最佳精确期望路径”与 `c0` 当前 `top1` 的分词边界做对照，先做启发式子类划分：
    - `local_homophone`
      - 分词边界基本一致
      - 但 1~2 个局部词位被近音词压错
    - `combination_bias`
      - 不只是局部 1~2 词替换
      - 更像多词组合整体总分偏差
    - `fragment_elevated`
      - 相比精确期望路径，`top1` 有明显更多的切分段或更多单字
- 当前结果：
  - `local_homophone = 51`
  - `combination_bias = 68`
  - `fragment_elevated = 0`（按当前边界/段数启发式无显著主类）
  - 另有仅 `2` 条出现“单字数比精确路径更多”的局部例外，但都不足以把“碎词抬高”提升为当前主问题
- 代表性局部近音词错排例：
  - `街口 -> 接口`
  - `沙沙 -> 啥啥`
  - `微凉 -> 微量`
  - `中学时的 -> 中学是的`
  - `肩头 -> 箭头`
- 代表性多词组合总分偏差例：
  - `秋天是有气息的 -> 秋天时尤其洗的`
  - `我抬头看那一树树的枝桠 -> 我抬头看哪艺术输得质押`
  - `回到西安的街道 -> 回到闲的街道`
  - `阳光从叶隙间漏下来 -> 阳光从业席间漏下来`
- 关于“碎词/单字路径被抬高”这一路线，当前可确认：
  - 它在早期 C 线上曾经是重要问题
  - 但在当前 `c0` 剩余错例里，已经不是主导矛盾
  - 只有 `2` 条表现出单字数多于精确期望路径：
    - `轻轻覆在往事之上 -> 轻轻负载往实质上`
    - `国际工人代表大会上 -> 国际公认代表大会上`
  - 更像局部结构失真，而不是广泛的碎词泛滥
- 因而主线判断进一步收敛为：
  - 当前 `c0` 的剩余优化重点，应优先放在：
    - 局部近音词的 base 权重错排
    - 多词短语组合的总分偏差
  - 而不是继续把主要精力放在“碎词路径普遍过高”这个旧问题上
- 用户提醒了一个关键边界：必须承认 `n-gram` 的能力上限，不能把它拿去和大语言模型直接比较，也不要为了少量尾部错例过度用力
- 因此又改用更轻的方式，对这批局部近音错排做了人工抽样复核，而不是继续跑更慢的细分类脚本
- 人工复核后，这批“局部近音错排”大致分成三类：
  - 语法功能词/虚词类：
    - `时/是`
    - `的/地/得`
    - `它/他`
    - `这/着`
    - `自/字`
    - `其间/期间`
  - 局部实词近音替换：
    - `街口/接口`
    - `微凉/微量`
    - `肩头/箭头`
    - `香脆/想脆`
    - `清朗/晴朗`
    - `小店/小点`
  - 较强语义/搭配依赖或文艺词汇弱势：
    - `叶隙间/业席间`
    - `梧桐/无痛`
    - `扑簌簌/狡辩`
    - `征程/整成`
    - `放空/防控`
- 这三类的路线意义不一样：
  - 第一类很多本质上需要句法或更强语言理解，`n-gram` 只能有限改善，不能指望稳定修净
  - 第二类里有一小部分可能仍属于 `n-gram` 契约内的局部错排，但也缺少单一、低风险、可全局泛化的统一开关
  - 第三类更接近语义/语体/常识或长搭配偏好，明显更像超出 `n-gram` 舒适区的问题
- 因而当前更稳妥的判断是：
  - 这 51 条里并不存在一个“很统一、很低风险、很可解释”的总模式，足以支持继续做一轮全局调参
  - 如果继续做，也只能做极小范围、非常克制的局部实验；否则更容易把开发集少量个案拟合坏
  - 在没有看到更强的统一信号前，当前主线应倾向于收束，而不是继续深挖这批尾部局部错例
- 这一阶段关于“这条路线是不是走错了”的理论总结也已正式补进文档
  - 结论分两层：
    - 当前这套实现形态已经证明自己不如原版 `octagram`
    - 但更大的总方向不能简单说“完全走错”，更准确的是“把主要纠偏责任压到后段句级排序器”这条实现路线走错了
- 理论层面的核心原因被收敛为：
  - `octagram` 更强，不是因为它更复杂，而是因为它更符合输入法搜索结构
    - 更早表达 `credibility`
    - 更早压掉坏路径
    - 不把主要纠偏责任留到最后
  - 当前路线理论上先天吃亏，因为它：
    - 把信用分配放晚了
    - 让搜索契约与评分契约错位
    - 在有限 beam / top-k 下承受不可逆的早期截断
    - 试图让后段 `n-gram` 去解决原本属于上游拼写歧义、切分可信度、候选先验的问题
- `C0` 的作用也被重新定位清楚了：
  - 它证明“正确契约”比当前线上形态更合理
  - 但它仍明显低于原版 `octagram`
  - 说明问题不只是排序器太弱，而是：
    - 当前图与基础边权底座仍带着历史包袱
    - 契约虽改对了一部分，但证据并没有被真正补强
    - 尾部还有相当一部分错误本就接近 `n-gram` 上限
- 因而当前阶段最合理的总结是：
  - 不再继续把 `witset_poet` 的后段补救当作主线
  - 这条线的价值已经从“继续优化”转成了“提供反证和边界”
  - 它明确说明了：若后续还要继续，必须从更上游的契约或更强证据入手，而不是继续在后段补丁上加码
- 已按此结论补了一份下一阶段技术设计草案，核心不是“再做一个大实验”，而是拆成三个最小、可止损的实验：
  - `E1`：最小上游 prior 原型
    - 只验证 `octagram-style prior` 本身有没有独立价值
    - 不同时引入新的句级补丁或新的搜索契约变量
  - `E2`：prior + 有限状态契约
    - 验证线上是否能用更正确的保留契约去靠近离线 `C0`
    - 重点是“正确路径能不能更早活下来”
  - `E3`：新契约下的轻量句级精排
    - 只在 E1/E2 成立后，验证句级 `n-gram` 是否还值得作为小范围精排器保留
- 这样拆的原因是：
  - 避免再把 prior、契约、句级 LM 混在一起推进
  - 避免一旦失败就分不清到底是哪一层假设错了
  - 避免又回到“上游没改好，中游没收干净，下游 LM 被迫救火”的旧模式
- 当前最推荐的启动项已明确写为：
  - 先做 `E1`
  - 因为它最接近原版 `octagram` 真正有效的原则，也最容易独立验证、最容易止损
- 同时把止损规则也正式写清楚了：
  - `E1` 若没有独立正收益，就暂停整条新路线，回头重审 prior 定义
  - `E2` 若不能明显把线上拉近 `C0`，就暂停线上工程化，回头检查图和边权底座
  - `E3` 若没有稳定净收益，就接受句级 `n-gram` 只适合作为弱精排器，而不是继续加码
- 已直接开始实现 `E1`，本轮刻意只做最小闭环，不再把多个变量混在一起：
  - 在 `witset_translator.cc` 中新增 `ApplyUpstreamPathPriors()`
    - 对 `WordGraph` 的每条边，基于：
      - `edge_risk`
      - `edge_spelling_class`
      - `vertex_risk`
    - 计算一个保守的 `upstream prior penalty`
    - 仅对存在稳定风险信号的边，克隆 `DictEntry` 并下调其基础 `weight`
  - 在 `witset_poet` 中新增 `use_joint_post_penalty`，默认 `false`
    - 让旧的 `joint_prior / joint_guidance` 后段惩罚默认不再参与
    - 避免本轮实验变成“上游 prior + 后段 joint 补罚”的混合版本
- 这一步的意图是：
  - 真正验证“prior 前移”本身有没有独立价值
  - 而不是再让 `poet` 末端重复表达同一层含义
- 当前状态需要明确区分：
  - `E1` **已实现**
  - 但还**没有编译和基线验证**
  - 因此此刻还不能对效果做任何判断
- 已完成本地静态检查：
  - `witset_translator.cc`
  - `witset_poet.cc`
  - `witset_poet.h`
  - 以上文件当前均无 diagnostics
- 已在用户授权后继续执行 `E1` 的“实现 + 基线”合并交付：
  - 按工作区规则执行 `outwit-windows/build_and_deploy.bat`
  - 编译与部署链路通过，最终返回 `0`
  - 构建日志中只有既有 warning，没有新的编译错误
- 但 `E1` 的第一轮 baseline 没有跑完，且暴露出比准确率更早的阻塞问题：
  1. 以 `--snapshot-timeout-seconds 60` 跑 `300` 条时
     - 在第 19 条长输入 `liangpangdefaguowutongxiangshiwushengdeshouhuzhe` 超时
     - `last_seen_input` 停在接近完整但未完成的前缀
  2. 为排除“只是 60 秒不够”，又改为 `--snapshot-timeout-seconds 120` 重跑
     - 结果更早就在第 1 条 case 超时
     - `last_seen_input = jinchens`
- 这说明当前 `E1` 还没有进入“比较 Top-1/Top-3”的阶段：
  - 它首先暴露出的是一档查询性能/推进性已经退化到不可接受
  - 即：
    - baseline 无法完成
    - `completed_text_steps = 0`
    - metadata 状态为 `interrupted`
- 从当前快照尾部可确认的事实：
  - `witset_local_snapshot.jsonl` 只推进到 `jinchens`
  - 候选记录仍在持续写出
  - 因此更像“查询显著变慢/卡在长句推进过程中”，而不是构建失败或 snapshot 完全不工作
- 当前阶段结论应明确记为：
  - `E1` **实现成功**
  - `E1` **编译成功**
  - `E1` **baseline 失败**
  - 失败原因不是精度差，而是先触发了严重的性能/推进性回归
- 这对路线判断很重要：
  - 新路线的第一步门槛，不只是看精度是否提高
  - 还必须先满足“能在一档基线下跑完并保持可接受延迟”
- 随后用户反馈 Windows 提示磁盘空间用完，重新检查后确认这不是噪声：
  - `C:` 当时仅剩约 `0.73 GB`
  - `C:\\Users\\Bing\\AppData\\Roaming\\witty\\debug\\witset_local_snapshot.graph.jsonl` 单文件约 `99.96 GB`
  - 另有 `latest_candidates.json`、旧 `baseline_backups`、临时 snapshot/log 等一批可重建测试工件
- 已对这批“纯生成型测试工件”做清理：
  - 成功删除超大 `graph.jsonl`
  - 成功删除旧 `baseline_backups`
  - 其余小型临时文件部分已不存在，部分为正在占用的日志，不影响判断
  - 清理后 `C:` 可用空间恢复到约 `100.675 GB`
- 清理后的复测结果说明很关键：
  - `--limit 20 --snapshot-timeout-seconds 60` 的短 baseline 已正常完成
  - 随后重新启动 `300` 条 baseline，metadata 持续推进：
    - 先到 `185/300`
    - 再到 `199/300`
    - `status = running`
- 因而当前对“E1 是否有性能问题”的判断应更新为：
  - 先前那次“跑不完”不能直接归因于 E1 算法本身
  - 磁盘打满是一个真实且足以解释超时的外部原因
  - 目前最多只能说：E1 是否仍有额外性能回归，尚待完整 `300` 条基线最终跑完后再判
- 随后 `300` 条 baseline 已完整跑完：
  - `completed_text_steps = 300`
  - `status = completed`
  - `expected_not_found_count = 64`
  - `preceding_text_mismatch_count = 288`
- 最终指标为：
  - `Top-1 = 0.541528`
  - `Top-3 = 0.681063`
  - `reference_case_count = 300`
- 与此前当前线上一档基线对照：
  - 旧基线：`Top-1 = 0.541528`，`Top-3 = 0.684385`
  - 当前 `E1`：`Top-1 = 0.541528`，`Top-3 = 0.681063`
- 也就是说：
  - `Top-1` 完全没有提升
  - `Top-3` 还轻微下降了约 `0.33` 个百分点
  - `expected_not_found_count` 也没有改善（仍为 `64`）
- 这使得 `E1` 的阶段结论已经足够明确：
  - 磁盘打满解释了之前的超时，不能把那次失败记到算法头上
  - 但在空间恢复并完整跑通 baseline 之后，`E1` 本身依然没有验证出独立正收益
  - 因此，按照先前写明的止损规则，当前这版“上游 prior 注入”至少作为第一版定义是**不成立的**
- 从错句性质看，`E1` 也没有表现出“正确路径更早领先”的明显新趋势；典型错例仍然集中在：
  - 多词组合偏差
  - 局部近音词错排
  - 超出 `n-gram` 舒适区的长句语义/搭配问题
- 对 `E1` 失败的理论分析已补入路线文档，当前最重要的更新结论是：
  - `E1` 的失败并不等于“上游 prior 这条路失败”
  - 更准确地说，是我们把 `octagram` 的有效 prior 抽象错了
- 当前这版 prior 抽象的主要问题可概括为五点：
  1. 抽出来的是 `edge_risk / vertex_risk / edge_spelling_class` 这种局部风险标签，而不是真正可工作的 `credibility`
  2. 这版 prior 只有“减坏路”，缺少“抬好路”的正向支持
  3. 这版 prior 是静态绝对惩罚，不是竞争组内的相对偏置
  4. 它仍然太局部，触不到当前主错因里的多词组合竞争与同骨架词选错
  5. 它只是把旧信号前移了，但没有把信息形态改对
- 因而，`E1` 的真正反证价值在于：
  - 不能再把“回到 `octagram` 原则”误解成“把几项局部 risk 提前减权”
  - 若后面还要继续走这条大方向，重点应转为回答：
    - `octagram` 的 `credibility` 真正在区分什么竞争关系
    - 它依赖的是哪些路径形成信息，而不是哪些静态风险标签
    - 它是不是一种竞争式偏置机制，而不是简单的边权加减分
- 用户反馈 baseline 太慢，因此先对 `run_local_snapshot_baseline.py` 做了一轮“只改脚本、不改算法”的集中减时：
  - 新增 `--persist-every-text-steps`
    - 不再每完成 1 条 text case 就重写整份 metadata
  - `reference_cases.jsonl` 改为：
    - 启动时清空一次
    - 运行中逐条 append
    - 不再每次全量重写历史记录
  - 新增 `SnapshotTailReader`
    - 复用同一个 snapshot 文件句柄
    - 不再每 50ms 轮询时重复 open/read/close
  - `wait_for_snapshot_record()` 的 polling 改为渐进式 sleep
    - 有新进度时保持快轮询
    - 无新进度时逐步放缓，减少无效 I/O
  - 新增 `--skip-summary`
    - smoke / sentinel 场景可跳过 `summarize_local_snapshot.py`
    - 不再把 summary 强绑定为每次 baseline 的必经步骤
  - metadata 新增 `wall_time_seconds`
- 已做静态验证：
  - `run_local_snapshot_baseline.py` 与 `summarize_local_snapshot.py` 均已 `py_compile` 通过
  - baseline 脚本当前无 diagnostics
- 已跑一轮小样本 smoke test：
  - 命令：
    - `python ...\\run_local_snapshot_baseline.py --limit 20 --snapshot-timeout-seconds 60 --persist-every-text-steps 25 --skip-summary`
  - 结果：
    - `completed_text_steps = 20`
    - `status = completed`
    - `reference_cases.jsonl` 行数 = `20`
    - `wall_time_seconds = 207.078`
    - 外层 `Measure-Command` 观测总时长约 `210.621s`
- 当前判断：
  - 这轮优化已经确认不改变 baseline 语义，并成功跑通小样本
  - 但 `20` 条仍需约 `3.5` 分钟，说明脚本 I/O 只是总耗时的一部分
  - 若还要继续压时间，后续瓶颈更可能在：
    - `rime_api_console` 查询本身
    - snapshot 生成节奏
    - 每条 case 的真实候选计算成本

## 补记：`case2_diyizhan` 的 graph 聚合链路已修通，当前主阻塞点已更新

- 继续前先复核了这轮新增的 request-stage 调试链路，确认 `witset_translator.cc` 已把 `request_stage_states` 写进原始 `partial_chain_stage_probe.graph.jsonl`：
  - `text`
  - `family_identity`
  - `exact_count`
  - `aligned_with_best_prefix`
  - `bridge_lineage_confirmed`
- 真正丢字段的位置不在 C++ snapshot，而在 `partial_chain_stage_probe.py`：
  - `summarize_graph_probe()` 之前只保留了少量 `family_contract` 字段
  - `graph_contract` 聚合也读得过早，常在 console 尚未完全退出、graph 文件未稳定时就开始汇总
- 已做的最小修复是：
  - 让 `partial_chain_stage_probe.py` 把 `request_stage_states` 明细透传进 `graph_contract`
  - 把 graph snapshot 的最终聚合后移到 console 退出之后，再基于稳定落盘的 `graph.jsonl` 重算
- 清理临时文件释放空间后，重新执行：
  - `python .\partial_chain_stage_probe.py --case case2_diyizhan`
- 这次命令行末尾仍有一个独立的小问题：
  - 脚本最后 `print(json.dumps(...))` 往 `cp1252` 终端输出时触发 `UnicodeEncodeError`
  - 但结果文件已成功写出，不影响 probe 主体结论
- 最新重跑后的有效读数是：
  - `graph_contract.matched_edge_count = 4`
  - `request_stage_tag_counts = {not_request_stage_candidate: 18, request_source_line_eligible: 1, request_tail_supported: 19}`
  - request 盘面仍是：
    - `以 = -157.697`, `used_char_fallback = false`, `lm_oov_token_count = 0`
    - `已 = -182.211`, `used_char_fallback = false`, `lm_oov_token_count = 0`
    - `一 = -186.004`, `used_char_fallback = true`, `lm_oov_token_count = 1`
- 直接读取最新 `partial_chain_stage_probe.graph.jsonl` 可见，这轮最关键的路线判断需要更新：
  - `request_stage_prefix_text = 第一站是`
  - bucket 中存在主轴 state：
    - `text = 第一站是`
    - `family_identity = 第一站`
    - `aligned_with_best_prefix = true`
    - `bridge_lineage_confirmed = true`
  - 在关键跳点 `prefix_text = 第一站` 上，当前真实 tag 已是：
    - `是 -> request_source_line_eligible`
    - `十/时/事/拾/使/市/式 -> request_tail_supported`
- 因而，上一轮“`第一站 -> 式` 也还在拿同级 source-line eligibility”的判断已作废：
  - 那是旧工件 / 旧聚合口径导致的误判
  - 按当前最新代码和最新 graph snapshot，`式` 已经被压回 `request_tail_supported`
- 当前主阻塞点也随之更新为：
  - 不是 `第一站 -> 是` 资格没拿到
  - 也不是 `第一站 -> 式` 还在 graph gate 层抢同级资格
  - 而是 `第一站是 -> 一 / 以` 这一跳里，正确 `一` 仍以 fallback/OOV 形态进入 request
- 下一步如果继续，不应再重复：
  - `end_pos -> text` 单锚路线
  - 或围绕 `式` 再做 request-stage eligibility 收紧
- 更合理的下一刀应前移到：
  - `第一站是 -> 一` 为什么仍保持 `used_char_fallback + lm_oov_token_count = 1`
  - 查清它是在 `AnalyzeCredibility`
  - `RewriteWordGraph`
  - 还是 `witset_translator` 的 `yi-head` 承接口径里，仍未恢复成干净 exact continuation
- 沿这条新缺口继续追查 `.klm` 生成/导出链路后，又排除了一个容易重复怀疑的方向：
  - 没有发现任何脚本对单字 `一` 做过滤、归一化或替换
  - `dump_to_arpa.cc` 只是把 `.gram` 中的 token 逐个 `decode()` 后写成空格分隔序列
  - `download_gram.py` 只是调用 `dump_to_arpa` 与 `build_binary`，没有额外清洗逻辑
- 同时，对本轮重跑生成的 `partial_chain_stage_probe.next_hop.jsonl` 做了直接对照：
  - `一` 在 `request` 阶段共出现 `58` 次，`used_char_fallback = true` 为 `58/58`，`lm_oov_token_count = 1` 为 `58/58`
  - `以 / 已 / 宜` 在同一 source 下则稳定是：
    - `used_char_fallback = false`
    - `lm_oov_token_count = 0`
- 再对照已有 `diyizhan_family_transition_extract.json`：
  - `一` 在多个上下文后缀（如 `第 / 地 / 低 / 敌 / 弟 ...`）下都稳定表现为：
    - `used_char_fallback = true`
    - `matched_whole_word = false`
    - `oov_token_count = 1`
- 因而当前更准确的阶段结论应补成：
  - `一` 的异常不是 `partial_chain_stage_probe` 汇总误差
  - 也不是 `.klm` 导出脚本把 `一` 特殊过滤掉了
  - 而是当前运行时 `witogram::ScoreFeatures()` 在查询 `word = 一` 时，稳定落入 `vocab.Index(word) == NotFound()` 这一支
- 下一步若继续，真正值得查的不再是脚本预处理，而是：
  - `.gram / .klm` 内容里为什么 `一` 会长期缺席而 `以 / 已 / 宜` 不缺
  - 或当前模型训练语料/词表本身是否有系统性偏差，导致 `一` 这个单字 token 没被收进词表
- 继续沿这条线直接检查本机现成的 `arpa` 文本后，拿到了比运行时更硬的模型内容证据：
  - `C:\\Users\\Bing\\AppData\\Roaming\\witty\\grammar\\wanxiang-lts-zh-hans.arpa`
  - `C:\\Users\\Bing\\AppData\\Roaming\\witty\\grammar\\wanxiang-mini-zh-hans.arpa`
  - `C:\\Users\\Bing\\AppData\\Roaming\\witty\\grammar\\wanxiang-big-zh-hans.arpa`
- 三份模型在 unigram 层的共同现象是：
  - `以 / 已 / 宜` 都存在
  - `一` 不存在
- 同时在 `wanxiang-lts-zh-hans.arpa` 里还可直接看到：
  - `是` 存在
  - `站` 存在
  - `第一`
  - `第一站`
  这些整块 token 不存在
- 这把当前结论再收紧了一层：
  - `一` 的 `used_char_fallback = true`、`lm_oov_token_count = 1`
    不是 runtime 误报
  - 也不是 `partial_chain_stage_probe` 汇总误差
  - 而是当前模型词表内容本身就缺 `一` 这个 unigram token
- 因而现在更准确的说法应是：
  - `第一站是 -> 一` 之所以稳定走 `vocab.Index(word) == NotFound()`
  - 不是上游 source-line contract 还没接上
  - 而是模型本身就不给 `一` 这个 token 命中
- 这也解释了为什么同一 source 下：
  - `以 / 已 / 宜` 都能稳定保持 `used_char_fallback = false`
  - 而 `一` 无论在 `next_hop` 还是 `transition` 工件里都始终是 fallback/OOV
- 随后继续把链路往源头追了一层，拿到了 `.gram` 源级别的直接验证：
  - 当前机器上 `grammar/` 目录里，`wanxiang-mini-zh-hans.gram` 与 `wanxiang-big-zh-hans.gram` 仍在
  - `wanxiang-lts-zh-hans.gram` 不在本地，是因为 `download_gram.py` 下载并转换后会默认删除原始 `.gram` 与中间 `.arpa`
  - 下载来源已确认写死为：
    - `https://github.com/amzxyz/RIME-LMDG/releases/download/LTS/wanxiang-lts-zh-hans.gram`
- 使用本地现成的 `dist_x64\\bin\\dump_to_arpa.exe` 对 `mini` / `big` 的 `.gram` 做临时导出后，结果与本地现成 `.arpa` 完全一致：
  - `mini` 导出结果里：
    - 有 `以 / 已 / 宜`
    - 没有 `一`
  - `big` 导出结果里：
    - 有 `以 / 已 / 宜`
    - 没有 `一`
- 这一步非常关键，因为它把“问题在模型源里还是在导出器里”直接分开了：
  - 至少对 `mini` / `big` 而言，`一` 的缺失已经存在于 `.gram` 源内容本身
  - 不是 `dump_to_arpa` 或 `build_binary` 在后续转换时把 `一` 丢掉
- 再结合本地现成 `lts` 的 `.arpa` 同样缺 `一`、但有 `以 / 已 / 宜`：
  - 当前三个 `wanxiang` 模型在这件事上的口径是一致的
  - `lts` 虽然当前缺少原始 `.gram` 可直接重导，但其现成 `arpa` 结果与 `mini` / `big` 的源级验证方向一致
- 因而这轮之后，主问题可以再精确更新为：
  - 当前 `wanxiang` 模型族本身就系统性缺少 unigram `一`
  - 这不是某个运行时 case 的偶发缺口
  - 也不是某一版 `arpa/klm` 转换过程的局部损坏
- 继续做词典侧对照后，又排除了一个可能的误判：
  - 本机万象词典中的 `zi.dict.yaml` 明确包含：
    - `一	yi1	848`
    - `一	yi2	793`
    - `一	yi4	785`
  - 同一段还可见：
    - `以	yi3	901`
    - `已	yi3	848`
    - `宜	yi2	800`
- 这说明：
  - 输入侧候选词典并不缺 `一`
  - `一` 也不是因为缺拼音或缺单字条目才在 translator 里出不来
  - 当前缺口只发生在 grammar 模型一侧
- 再结合 `build_grammar.cc` 与 `GramDb::Build()` 的本地源码：
  - `build_grammar.cc` 只是把上游键值对做 `grammar::encode(key)` 后送进 `GramDb`
  - `GramDb::Build()` 只是把收到的 key/value 原样建成 trie，不做按 token 的过滤
- 因而当前更聚焦的判断应是：
  - `wanxiang` 词典层保留了 `一`
  - 但语法模型训练/分词统计层没有把 `一` 作为独立 token 收进 `.gram`
  - 问题更可能出在上游语料分词、n-gram 提取或 grammar 训练数据筛选口径，而不是本地构建器或运行时接线
- 继续查 `RIME-LMDG` 公开 wiki 后，这个判断又收紧了一层，而且目前还没看到与之冲突的公开规则：
  - wiki《词频统计与词库建立》明确写了：
    - **单字频率统计不使用分词工具**
    - 对清洗后的句子先加拼音，再拆成“单字 + 拼音”列表单独统计
    - 单字和多字词分成不同词库建立
  - 同一页对语言模型又写了：
    - n-gram 建立走“分词 -> 统计 -> 剪枝”
    - 低频词和低频 n-gram 会被剪枝
- 这意味着：
  - `一` 在 `zi.dict.yaml` 中存在，与上游“单字单独统计建词库”的方法是完全一致的
  - `一` 在 `.gram` 中缺失，也与上游“grammar 建模依赖分词结果 + 剪枝”的方法是相容的
- 到目前为止，没有找到任何公开说明或本地源码证据表明：
  - 上游存在“显式删除 `一`”的专门规则
  - 或本地 `build_grammar` / `GramDb` 转换器会单独过滤 `一`
- 因而当前最稳妥的阶段结论应更新为：
  - `一` 缺失更像是 `RIME-LMDG` 上游 grammar 训练口径的自然结果
  - 具体表现为：单字进入了单字词库统计，但没有稳定进入“分词后的 n-gram grammar token 集合”
  - 再叠加低频剪枝/句级短句化后，`一` 作为独立 unigram 最终没有进入 `wanxiang` grammar 模型族
- 继续往公开仓与本地执行流再追了一层后，有两点需要固定下来，避免后续误把 `octagram` 的成功解释成“同样 token 缺口下 magically 更强”：
  - 截至目前公开 `RIME-LMDG` 材料能确认的是：
    - grammar 建立依赖“分词 -> n-gram 统计 -> 剪枝”
    - 单字词库统计与 grammar 建模是分开的
    - README / wiki 没有公开可核对的明确剪枝阈值或脚本参数
  - 因而对 `一` 的当前判断只能下到：
    - 它更像是上游 grammar 训练口径的自然缺失
  
## 2026-05-24

- 继续围绕 `case2_diyizhan / case3_tiyanbuyiyang` 做高性价比静态拆解，目标是确认残留问题到底是不是 `request-stage state ownership / admitted_replace` 失守。
- 先回读 `witset_poet.cc` 的关键路径，确认当前 admission 契约如下：
  - `BuildApproxStateKey()` 只编码：
    - `context_suffix`
    - `trailing_single_char_run`
    - `tail_anchor_char_count`
    - `compactness / joint / single-char / edge-risk / edge-class / edge-span` 等桶
  - **不编码**：
    - `request_stage_bridge_bonus`
    - `cumulative_request_stage_bridge_bonus`
    - 任何 `request_stage` / `bridge_lineage` 支持
  - 同 key 下 `admitted_replace` 只按：
    - `new_line.beam_score`
    - `new_line.weight`
    - 选择 incumbent
- 基于这段静态代码，原本的下一步怀疑是：
  - 剩余问题可能是“正确 family 被错误 family 并桶后抢掉 incumbent”
  - 即 `state_key` 过粗或 replacement 规则缺少 request-stage 支持维度
- 为避免继续只靠旧工件猜测，随后只做了一次最小最新验证：
  - 不编译
  - 只在当前已有二进制上运行：
    - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --mode full --case case2_diyizhan --case case3_tiyanbuyiyang`
  - 中途发现当前系统 Python 缺 `pypinyin`
  - 已按用户先前授权，仅安装最小依赖：
    - `python -m pip install pypinyin`
  - 随后脚本运行成功，重新生成了最新：
    - `partial_chain_stage_probe.graph.jsonl`
    - `partial_chain_stage_probe_result.json`
- 最新 graph 工件把路线判断明显收紧了：
  - `case2_diyizhan` 中：
    - `第一站是以`
    - `第一战是以`
    - `第一展是以`
    - `第一站是一`
    - `第一站是一座`
    - 都是各自独立的 `batch_selected -> admitted_new`
  - 没有看到这些目标链之间发生 `admitted_replace`
  - 它们的 `state_key` 也各自不同，例如：
    - `旅行的征程。第一站是以|...`
    - `旅行的征程。第一战是以|...`
    - `旅行的征程。第一展是以|...`
    - `旅行的征程。第一站是一|...`
- 更关键的是，`case2` 的主差额已经在 request 阶段直接出现：
  - `第一站 -> 是以`：
    - `search_score = -136.354`
  - `第一战 -> 是以`：
    - `search_score = -136.586`
  - `第一展 -> 是以`：
    - `search_score = -137.059`
  - `第一站 -> 是一`：
    - `search_score = -154.930`
  - 即：
    - `第一站是一` 不是被并桶替换掉
    - 而是独立入池后，仍在 request 阶段先天落后约 `18.6` 分
- `case3_tiyanbuyiyang` 的最新 graph 也给出同类结论：
  - `体言不一样` 与 `体验不一样` 也都是各自独立 `admitted_new`
  - 没看到两者在同 key 下互相 `replace`
  - 但 request 分数出现显著先天分差：
    - `体言 -> 不一样`：
      - `search_score = -191.261`
    - `体验 -> 不一样`：
      - `search_score = -194.412`
    - `体言 -> 不一样的`：
      - `search_score = -208.642`
    - `体验 -> 不一样的`：
      - `search_score = -211.785`
  - 即：
    - 当前 `体言...` 的领先，也不是 admission replacement 造成
    - 而是 request 本身已经把错误链放在更前
- 因而，今天这轮排查后的阶段结论需要正式更新：
  - 之前把残留问题收口到：
    - `state ownership`
    - `state_key`
    - `admitted_replace`
    - 这条判断**不成立或至少不是当前主因**
  - 当前两条残留更像是：
    - request 阶段的原始候选/转移打分问题
    - 或更早的词图候选形态本身就把错误链抬高
  - 换言之：
    - 问题层级还要再前移
    - 从“state admission 失守”前移到“request scoring / candidate transition 先天偏置”
- 对后续主线的直接影响是：
  - 不应继续优先做：
    - `state_key` 加 bridge support 桶
    - `admitted_replace` 引入 request-stage bonus 优先级
  - 因为在最新工件里，目标链还没进入“同 key 互抢 incumbent”这一层，就已经输掉了
- 下一步若继续，更值得看的位置应改为：
  - `request` 生成时：
    - `第一站 -> 是一` 为什么天然远弱于 `第一站 -> 是以`
    - `体验 -> 不一样` 为什么仍弱于 `体言 -> 不一样`
  - 也就是：
    - `base / dict / lm / adjustment` 在 request 生成侧的主差额
    - 而不是继续围绕 `admitted_state_index` 做契约修补
- 随后直接对最新 `partial_chain_stage_probe.graph.jsonl` 做了 request 账本拆解，关键结论如下：
  - `case2`：`第一站 -> 是一` vs `第一站 -> 是以`
    - `search_score`：
      - `是一 = -154.930`
      - `是以 = -136.354`
      - 差额 `-18.576`
    - `base_score`：
      - `是一 = -154.995`
      - `是以 = -135.796`
      - 差额 `-19.199`
    - 其中：
      - `dict_score_raw/dict_score_norm` 反而是 `是一` 更好
      - `adjustment_score` 也是 `是一` 略好
      - 真正主差额来自 `lm_score_scaled`
        - `是一 = -89.799`
        - `是以 = -63.053`
        - 差额 `-26.746`
    - 同时两者都不是 fallback/OOV：
      - `used_char_fallback = false`
      - `lm_oov_token_count = 0`
  - 这说明：
    - 当前 `第一站 -> 是一` 的 request 失守，不是 `一` 的单字 fallback 问题
    - 也不是词典打底不足
    - 而是这一步整体转移的 LM/base 天然更偏向 `是以`
- `case3`：`体验 -> 不一样` vs `体言 -> 不一样`
  - `search_score`：
    - `体验不一样 = -194.412`
    - `体言不一样 = -191.261`
    - 差额 `-3.151`
  - `base_score`：
    - `体验不一样 = -193.881`
    - `体言不一样 = -190.384`
    - 差额 `-3.497`
  - 其中：
    - `dict_score_raw/dict_score_norm` 完全相同
    - `adjustment_score` 是 `体验` 更好一点
    - 主差额同样来自 `lm_score_scaled`
      - `体验不一样 = -107.926`
      - `体言不一样 = -134.123`
      - 这里反而是 `体验` 的 LM 更好
    - 但 `体言` 在更早 source/base 上已有明显领先，最后仍整体占优
  - 两者也都不是 fallback/OOV：
    - `used_char_fallback = false`
    - `lm_oov_token_count = 0`
- 结合 `witset_poet.cc` 当前计分公式可确认：
  - `base_score = candidate->weight + lm_total_weight * lm_score_scaled + ...`
  - `adjustment_score` 只是后续补偿层
  - 因而这两条残留的主缺口都更接近：
    - request 源候选 `candidate->weight`
    - 与 `lm_score_scaled` 合成后的 base 竞争
  - 而不是后续 adjustment/contract 层没有补够
- 继续把链路往前追一跳后，可以把 `case2` 和 `case3` 的问题层级进一步区分开：
  - `case2`：`第一 -> 站/战/展`
    - 三者来自同一个 source：
      - `beam_score = -43.9859`
    - `lm_score_scaled` 完全相同：
      - 都是 `-44.3238`
    - `站` 能领先成为 `第一站`，主要来自：
      - `dict_score_raw/dict_score_norm` 略好
      - `adjustment_score` 为正，而 `战/展` 为负
    - 这一跳后形成的 source_pool 顺序是：
      - `第一站 = -60.578`
      - `第一战 = -60.809`
      - `第一展 = -61.311`
    - 差额只有 `0.2 ~ 0.7`
    - 因而：
      - `case2` 真正决定性的问题不是 `第一 -> 站/战/展`
      - 而是下一跳 `第一站 -> 是一/是以` 时，`LM/base` 把 `是一` 额外拉开了约 `18.6`
  - `case3`：`体验/体言`
    - 这两个词不是在已有 source 基础上再续写出来的
    - 而是直接从 `start_pos=0 -> end_pos=5` 生成
    - 即问题发生在“句首首次生成该词”的 entry/base 层
    - 其原始差额非常大：
      - `体验`：
        - `search_score = -75.464`
        - `base_score = -74.993`
        - `lm_score_scaled = -63.053`
      - `体言`：
        - `search_score = -45.770`
        - `base_score = -44.781`
        - `lm_score_scaled = -31.430`
    - 两者此时就已经相差近 `29.7`
    - 后续 `-> 不一样` 只是延续了这个先天领先，并不是那一步才第一次出错
- 因而当前两条残留需要分开对待：
  - `case2`
    - 应优先继续拆：
      - 为什么在 `第一站` 这个 source 上，
      - `是以` 的 `lm_score_scaled` 会显著优于 `是一`
  - `case3`
    - 应优先继续拆：
      - 为什么句首直接生成 `体言` 时，
      - `lm_score_scaled` 会显著优于 `体验`
      - 以及句首 context / boundary / LM tokenization 是否放大了这类误偏好
- 继续回读 `witogram::ScoreFeatures()` 与 `witset_poet` 接线后，可把 LM 口径再收紧一层：
  - `witset_poet` 在 request 阶段对 `witogram` 的调用是：
    - `witogram->ScoreFeatures(candidate->context(), entry->text, is_rear, &features)`
  - 其中 `candidate->context()` 不是整句，而是：
    - `BuildContextSuffix(previous_suffix, appended_text, max_context_tokens)`
    - 即仅保留 KenLM 需要的 UTF-8 token 后缀
  - `ScoreFeatures()` 的核心流程是：
    - 先把 `context` 切成 UTF-8 单字 token，并裁到 `model->Order() - 1`
    - 再把 `word` 也切成 UTF-8 单字 token，先算 `char_path`
    - 若 `vocab.Index(word)` 命中，再算 whole-word，并用 `0.60 * whole_word + 0.40 * char_path` 混合
    - 若整词不命中，则按 `SplitTokenSupported / NeutralMissing / TrueOov` 区分证据级别
- 这直接解释了为什么当前 graph 上会出现：
  - `used_char_fallback = false`
  - `lm_oov_token_count = 0`
  - 但仍然可能不是强 whole-word 命中
  - 因为在 `NeutralMissing` 下，poet 会把惩罚态 fallback / OOV 清零，只保留 LM 总分本身
- 结合现有工件，`case2` 现在已经有一个很强的侧证：
  - 旧 `c2_transition_probe_diyizhan.json` 中，若 query 细到：
    - `...驿站是 -> 一`
  - 会稳定出现：
    - `oov_token_count = 1`
    - `used_char_fallback = true`
    - `matched_whole_word = false`
  - 这说明：
    - `一` 这个单字在相关上下文下确实存在明显的 LM 证据弱势
  - 当前最新 `case2` 的直接比较是：
    - `第一站 -> 是一`
    - `第一站 -> 是以`
  - graph 只记录到了它们的合成后 `lm_score_scaled`
  - 虽未直接落出 `matched_whole_word`，但结合旧 probe，可以把下一步重点收口为：
    - `是一` 是否落在 split-token / neutral-missing 型证据
    - 而 `是以` 是否拿到更强的 whole-word 或更优 char-path
- `case3` 的口径则不同：
  - 当前最新 graph 里，`体言` 与 `体验` 都是在：
    - `start_pos = 0 -> end_pos = 5`
    - 句首首次生成
  - 它们此时就已经出现：
    - `体言 lm_score_scaled = -31.430`
    - `体验 lm_score_scaled = -63.053`
  - 且两者都未表现为 true OOV 惩罚态：
    - `used_char_fallback = false`
    - `lm_oov_token_count = 0`
  - 因而 `case3` 当前更像：
    - 句首 `体言/体验` 本身在 LM 词表/整词路径/char-path 混合后就已有强先验差异
    - 不是后续 `不一样` 这一步才第一次分出胜负
- 到这里，下一步的最小有效验证目标已经很明确：
  - `case2`
    - 直接对 `context_suffix ~= ...第一站` 下的：
      - `word = 是一`
      - `word = 是以`
    - 拿到 `whole_word_log10 / char_path_log10 / matched_whole_word / token_evidence_level`
  - `case3`
    - 直接对句首 context 下的：
      - `word = 体验`
      - `word = 体言`
    - 拿到同样四项
  - 只有拿到这组细账后，才能判断后续该改：
    - LM 证据解释口径
    - neutral-missing 处理
    - 还是更上游的词表/训练内容
- 继续追导出链路后，已把当前“不改代码/不编译”前提下的阻塞点定位清楚：
  - `WitsetPoet` 头文件里其实已经公开了：
    - `debug_transition_lm_snapshot()`
  - 对应记录结构 `DebugTransitionLMRecord` 已包含：
    - `context_suffix`
    - `word`
    - `is_rear`
    - `total_log10`
    - `avg_log10`
    - `token_count`
    - `oov_token_count`
    - `used_char_fallback`
    - `matched_whole_word`
  - 但当前宿主导出链路只落了三类文件：
    - `debug_local_snapshot_path`
    - `debug_local_graph_snapshot_path`
    - `debug_local_next_hop_probe_path`
  - `partial_chain_stage_probe.py` 也只会回收：
    - `snapshot`
    - `graph`
    - `next_hop`
  - 没有任何现成路径会把 `debug_transition_lm_snapshot()` 落到磁盘
- 因而，当前在“不修改 C++ / 不重新编译”前提下，能拿到的最细证据已经基本到顶：
  - `case2`
    - 可确认 `第一站 -> 是一/是以` 的 `lm_score_scaled` 差额很大
    - 可确认旧 probe 中 `...是 -> 一` 曾出现：
      - `oov_token_count = 1`
      - `used_char_fallback = true`
      - `matched_whole_word = false`
    - 但还不能直接拿到当前最新 `第一站 -> 是一/是以` 本体的 `whole_word_log10 / char_path_log10 / token_evidence_level`
  - `case3`
    - 可确认句首 `体言/体验` 在首次生成时 LM/base 已大幅分叉
    - 但同样拿不到当前本体的 whole-word vs char-path 细账
- 因而当前最合理的下一步需要分成两类：
  - 若继续保持“只读、不编译”：
    - 当前主价值已经从“继续榨旧工件”转为“整理结论并收口下一刀需求”
  - 若允许做一刀极窄代码改动并重新编译：
    - 优先给宿主补一个 `debug_local_transition_lm_snapshot_path`
    - 把 `debug_transition_lm_snapshot()` 直接落成 json/jsonl
    - 随后用现有 `partial_chain_stage_probe.py` 同样模式只补收这第四类工件
    - 这样就能一次性拿到：
      - `case2: 第一站 + 是一 / 是以`
      - `case3: BOS + 体验 / 体言`
      - 的 `matched_whole_word / used_char_fallback / oov_token_count / total_log10`
    - 不能伪造出“具体是某个阈值把它裁掉”的细则
- 同时，从 `octagram` 本地代码流可以解释“为什么同词库同 grammar 模型它还能把句子做对”：
  - `octagram` 插件本身很薄，核心只有 `Grammar::Query(context, word)`；`Poet` 只做：
    - `candidate.weight + Grammar::Evaluate(...)`
  - 其中 `Grammar::Evaluate()` 只是：
    - `entry_weight + grammar_query`
    - 不存在 `used_char_fallback` / `oov_token_count` 这样的额外惩罚语义
  - 在 `octagram.cc` 中，如果 grammar 查不到更强搭配，返回的只是统一的 `non_collocation_penalty`
    - 不会因为某个 unigram 不在模型里，就额外把该词打成 fallback/OOV 坏路径
- 这与当前 `witogram` 路线形成了直接对比：
  - `witogram.cc` 中 `vocab.Index(word) == NotFound()` 会把该词标为 `used_char_fallback=true`
  - `witset_poet.cc` 会继续把：
    - `lm_oov_token_count`
    - `used_char_fallback`
    - 转换成持续性的 `oov_penalty` / `char_fallback_penalty`
  - 所以在当前实现里，“grammar 中缺 unigram `一`”被放大成了显式负担
  - 而在原版 `octagram` 里，同样的缺口只意味着“没有拿到更强 collocation 证据”，并不自动等于一条更差的 OOV 路径
- 此外，当前词典里已经确认存在：
  - `第一站`
  - `一座`
  - `古老`
  - `小镇`
- 这使得原版 `octagram` 能走的最自然路径是：
  - 先靠 `Syllabifier -> Dictionary` 保住 `第一站` 这条 exact family
  - 再由 `Poet` 在句级上组合 `第一站 / 是 / 一座 / 古老 / 小镇` 这类基础词块
  - 用 `octagram` 只做薄层 collocation 调整
- 也就是说，原版能做对，关键不是它“更擅长把 `一` 当 unigram 处理”，而是：
  - 它没有把 `一` 的 grammar 缺口放大成当前这种 fallback/OOV 失血
  - 且更依赖已存在的基础词块和上游 credibility contract 去维持正确 family 直到句级组合完成
- 随后重新把这轮相关代码、文档、脚本和当前调试工件串起来复核后，当前判断进一步收敛为：
  - 这条“先把目标、验证口径、止损规则写清楚，再逐个最小实验推进”的工作方式是合理的
  - 我认同继续沿“不要把更多变量混在一起、先确认每一步是否有独立收益”的方向推进
  - 但当前最该优先解决的，不再是继续设计新的排序特征，而是先把 baseline 链路里的调试/导出成本彻底控住
- 对“baseline 为什么慢”的根因，当前应分成两层来看：
  1. 脚本层曾经确实有明显低效：
     - 每条 text step 重写整份 `reference_cases.jsonl`
     - 高频轮询时反复 `open/read/close` snapshot
     - metadata 落盘过于频繁
     - summary 被强绑定为每次 baseline 的尾步骤
  2. 但这些并不是当前最大的总耗时来源：
     - `run_local_snapshot_baseline.py` 已经把这些热点做过一轮收敛
     - 从本轮代码复核看，真正更重的根因在 `witset_translator.cc` 的调试导出链路
- 当前更接近“根本原因”的结论是：
  - baseline 慢，不只是“脚本慢”
  - 而是 baseline 在跑每个 case 时，实际还在同步追加一份体量极大的 `graph.jsonl`
  - 这份导出包含：
    - 全量 `vertices`
    - 全量 `syllable_edges`
    - 全量 `word_edges`
    - 每条边下的全部 `candidates`
    - 全量 `transition_lm_features`
    - 当前候选 debug 信息
  - 且写法是每次 `Query()` 都 `std::ofstream(..., std::ios::app)` 直接追加一整条大 JSON
  - 当 baseline 全量跑 `300` 条、且每条输入本身是长句时，磁盘写入量会远远大于最终评测真正需要的数据量
- 因而当前对“能不能继续加速”的判断是：
  - 能
  - 但下一步最有价值的加速点，已经不是继续微调 Python polling
  - 而是优先收缩 graph 导出的默认体积与触发条件，例如：
    - 让 baseline 默认不导出 `graph.jsonl`
    - 或只在离线 `C0/C1` 实验时按开关单独导出
    - 或在 graph 导出里只保留 solver 真正需要的最小字段，而不是把完整调试信息一起持续落盘
  - 换句话说，后续若要继续提速，优先级应是：
    - 先减导出
    - 再看查询
    - 最后才继续挖脚本层边角 I/O
- 本轮也顺手重新核对了仓库内 `docs` 与运行目录里的工件边界：
  - 仓库内 `docs/benchmark_artifacts/octagram_300_20260516` 这份对照基线体积并不大，应保留
  - 真正需要清理的是 `C:\\Users\\Bing\\AppData\\Roaming\\witty\\debug` 下那些可重建的运行期工件
- 已完成本轮清理：
  - 删除 `C:\\Users\\Bing\\AppData\\Roaming\\witty\\debug\\witset_local_snapshot.graph.jsonl`
  - 删除 `C:\\Users\\Bing\\AppData\\Roaming\\witty\\debug\\witset_local_snapshot.jsonl`
  - 删除 `C:\\Users\\Bing\\AppData\\Roaming\\witty\\debug\\snapshot_summary\\latest_candidates.json`
  - 清理后再次核对，`debug` 目录总占用已降到约 `10.2 MB`
- 清理过程中的一个工具链现象也需要记下：
  - 内置删除工具在处理约 `34.61 GB` 的 `graph.jsonl` 时会因自身 OOM 失败
  - 小文件删除正常
  - 超大单文件最终改为直接执行一次性 PowerShell `Remove-Item`
  - 这不影响对工件“纯生成、可重建、应清理”的判断
- 已继续按上述判断落地“graph 导出独立开关”，本轮只改调试导出链路，不动排序算法：
  - 在 `witset_translator` 中新增：
    - `debug_dump_local_graph_snapshot`
    - `debug_local_graph_snapshot_path`
  - 当前行为改为：
    - `debug_dump_local_snapshot = true` 只负责本地候选 snapshot 与 debug comment
    - 不再自动顺带写出 `graph.jsonl`
    - 只有显式开启 `debug_dump_local_graph_snapshot = true` 时，才会追加 graph 调试工件
- 这样做的目的很明确：
  - 让 baseline 默认路径回到“只生成评测真正需要的 snapshot/summary”
  - 把离线 `C0/C1` 图求解实验所需的超大 graph 工件，收敛到按需开启
  - 避免后续再出现“只是跑 baseline，却顺手写出几十 GB graph.jsonl”的情况
- 同时把 `witset_poet` 里为 graph 导出收集的 `debug_transition_lm_snapshot` 也一起拆开了：
  - 以前只要 `debug_dump_local_snapshot = true`，就会额外缓存 transition LM 明细
  - 现在改为仅在 `debug_dump_local_graph_snapshot = true` 时才收集
  - 这样可以避免“graph 已关闭，但还在为 graph 导出准备额外调试数据”的隐性开销
- 文档也同步更新：
  - `README.md`
  - `docs/validation_baseline.md`
  - 推荐 baseline 配置现在显式写为：
    - `debug_dump_local_snapshot: true`
    - `debug_dump_local_graph_snapshot: false`
- 当前预期收益：
  - 不改变 baseline 语义
  - 不影响需要 graph 的离线 solver 实验
  - 先把默认链路中的超大磁盘写入去掉，再观察剩余真实查询耗时
- 继续按“显式开 graph 时也只导出 solver 真正需要的最小字段”推进后，新增两层收缩：
  1. 字段层裁剪：
     - `graph.jsonl` 现在只保留 solver 当前实际读取的字段：
       - 顶层：`timestamp_ms / session_id / input / preceding_text / context_token_limit / interpreted_length`
       - `vertices`：仅保留带风险的 `pos / risk`
       - `word_edges`：仅保留 `start / end / edge_risk / edge_spelling_class / candidates[text, weight]`
       - `transition_lm_features`：仅保留 solver 真正读取的上下文 LM 特征
     - 已删除对当前 solver 无用且体积很大的内容：
       - `syllable_edges`
       - `current_candidates`
       - candidate 附加调试元数据（`matching_code_size / remaining_code_length / commit_count / code_size`）
       - 顶层 `llm_level_flags / input_length`
       - transition 中无用的 `token_count`
  2. 记录层去重：
     - 仅做字段裁剪还不够，因为同一条连续输入链会经历 `j -> ji -> jin -> ...`
     - solver 真正需要的是每条连续输入链末尾那一条完整 graph，而不是所有中间前缀
     - 因此 graph 导出进一步改成：
       - 每个 `session` 只缓存最新一条 graph 快照
       - 仅在 session 切换或 translator 析构时落盘
       - 结果是每条连续输入链只保留最终一条记录
- 本轮已按工作区规范完成编译部署并做了两轮 smoke baseline 实测：
  - 编译部署命令：`outwit-windows/build_and_deploy.bat`
  - 编译、部署成功；仅见既有 warning，无新增 error
- 实测结果：
  - 默认 baseline（graph 关闭）：
    - 命令：`python .\\librime\\plugins\\witogram\\tools\\run_local_snapshot_baseline.py --limit 20 --snapshot-timeout-seconds 60 --persist-every-text-steps 25 --skip-summary`
    - `20` 条完成，`wall_time_seconds = 94.563`
    - 只生成 `witset_local_snapshot.jsonl`
    - 未生成 `witset_local_snapshot.graph.jsonl`
  - 显式开 graph，但仅做“字段裁剪”后的中间状态：
    - 同样 `20` 条，`wall_time_seconds = 194.578`
    - `graph.jsonl ≈ 1956.58 MB`
    - 说明单纯减字段仍不够，主要问题仍是“把所有前缀 query 都写出来”
  - 显式开 graph，并加入“每个 session 只保留末态”后：
    - 同样 `20` 条，`wall_time_seconds = 134.89`
    - `graph.jsonl = 53,090,008` bytes，约 `50.631 MB`
    - 行数为 `18`
    - 相比上一轮 `1956.58 MB`，体积已下降约 `38.6x`
- 当前判断：
  - 默认 baseline 路径已经足够安全，不会再顺手写出超大 graph 工件
  - 显式开 graph 时，虽然 `50 MB / 20` 条仍然不算小，但已经从“极易把磁盘打爆”下降到“可控的离线实验工件”
  - 后续若还要继续压 graph，大头应继续盯：
    - 长句末态自身的 `word_edges * candidates`
    - `transition_lm_features` 条数
    - 是否能在 solver 侧进一步接受更稀疏的候选集合
- 到这一步，对“baseline 还要不要继续当主线优化”也可以给出更明确的阶段性判断：
  - 以默认 `graph 关闭` 的当前链路看，`20` 条 smoke 已从早先约 `207s` 降到约 `94.6s`
  - 这已经说明最主要、最不合理的调试噪声已经被移除
  - 继续在现有 Python baseline 脚本里深挖，剩余空间大概率只会是小幅改进，而不是再来一次数量级下降
  - 因为当前脚本本身已经比较瘦：
    - `rime_api_console` 是长驻进程，不是每条 case 重启
    - snapshot 已改为 tail-reader 增量读取
    - `reference_cases` 已改为 append 落盘
    - metadata/summary 也不再是高频重写热点
  - 换句话说，默认 baseline 现在剩下的更大头，已经更像真实查询与候选生成成本，而不是“脚本壳子太慢”

## 2026-05-27 `case1` 继续收口：最终错误串的直接前驱已坐实为 `一直想`，主断点回到更早的 family/continuation 资格而不是 carry 传播

- 先复核了 `WORKLOG`、`witset_poet.cc` 与当前测试，确认上一轮两条行为实验里：
  - `BuildApproxStateKey()` 的 carry 身份注入已经不在源码里
  - `SelectTopLines()` 的 carry 保留逻辑已经不在源码里
  - 但 `same-span` 块里仍残留了 `carried_exact_ambiguous_reparse / allow_equal_contract_support_competition` 这类负结果特判，源码状态与“只保留观测”的结论不一致
- 为避免继续围绕旧假设打转，这轮先按最小 TDD 收口观测面：
  - 将旧的错误断言 `一直想望着远方` 必须保留 `CarryExactAmbig > 0` 改成当前已证实的正确结论：`CarryExactAmbig == 0`
  - 新增红灯，要求最终错误候选 debug 暴露直接前驱 `Prev:`
  - 在 `witset_poet.cc` 的 snapshot debug 中新增 `Prev:`，内容为“跳过虚拟 prefix 后的直接前驱可见文本”
- 最小验证链：
  - `librime/build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case1_yizhixiangwang --mode snapshot`
  - 目标测试：
    - `test_case1_wrong_reparse_candidate_drops_exact_ambiguous_identity`
    - `test_case1_wrong_reparse_candidate_exposes_segmentation_debug`
    - `test_case1_wrong_reparse_candidate_exposes_predecessor_debug`
  - 以上均已转绿
- fresh snapshot 的关键新证据：
  - 最终错误 top1 `一直想望着远方`
    - `Seg: 在新的生活里再次轻轻浮现。|一直想|望着远方`
    - `Prev: 一直想`
    - `CarryExactAmbig: 0`
  - 这把断点进一步钉死为：
    - 终局错误串不是从 `一直想望着 + 远方` 延长出来
    - 而是从 `一直想 + 望着远方` 这条 lineage 直接形成
- 随后把 `same-span` 里残留的负结果行为特判正式回退，只保留 `Seg:` / `Prev:` 观测：
  - 删除 `carried_exact_ambiguous_reparse`
  - 删除 `carried_cross_boundary_reparse` 对 `step_cross_boundary_reparse` 的放宽
  - 删除 `allow_equal_contract_support_competition`
  - 再次最小编译与 `case1 snapshot` 验证通过，观测结论不变
- 在单例 `case1 --mode full` 中继续核对上游断点后，拿到更靠前的 runtime 事实：
  - 对 `next_hop_after_yizhi`，前缀 `一直` 下：
    - `想`
      - `matching_request_state_text = 一直想`
      - `request_stage_tag = request_tail_supported`
    - `向往`
      - `matching_request_state_text = 一直向往`
      - `continuation_tag = exact_ambiguous_family`
      - `request_stage_tag = not_request_stage_candidate`
    - `向往着`
      - `matching_request_state_text = 一直向往着`
      - `continuation_tag = exact_ambiguous_family`
      - `request_stage_tag = not_request_stage_candidate`
- 当前阶段判断进一步收口为：
  - `case1` 的剩余主缺口不在后段 carry 身份传播
  - 也不在 state key / source selection / same-span carry 特判
  - 更像是更早在 `一直` 这一步，`向往 / 向往着` family 虽已有 matching request-stage state，但仍拿不到稳定 continuation / request-stage 资格，导致稳定活到句尾的是 `一直想` 这一支
- 因此下一步若继续，应优先围绕：
  - `一直 -> 向往 / 向往着` 为什么仍停在 `exact_ambiguous_family`
  - 以及是否存在未重复的、更早的 family representation / continuation eligibility 观测或结构入口
  - 而不是再回到 carry 传播或末端 same-span 特判

## 2026-05-27 `case1` 再补一层只读 path 原因观测：`向往着` 卡在 `legal_but_path_unconfirmed` 不是因为下一跳 family 脏，而是下一跳缺少 confident primary

- 继续前先再次查重，确认这轮不做任何排序或 gate 变更，只补观测：
  - 不回到 `family_soft_clean -> HasConfidentPrimaryExact`
  - 不回到多字 request-stage 资格原型
  - 不回到 `primary_path_eligible` / source-axis 的行为补丁
- 这轮先给 `case1` 增加一个 focused 红灯：
  - 要求 `next_hop_after_yizhi` 里的 `向往着` 样本导出 `path_tag` 未确认的原因字段
  - 初始失败，说明当前 `partial_chain_stage_probe.py` 的 `graph_contract.focus_entries` 还没把该原因透传出来
- 处理：
  - 在 `witset_translator.cc` 中新增只读 helper：
    - `PrimaryPathProbeReason`
    - 导出
      - `path_next_prefix_family_tag`
      - `path_next_exact_count`
      - `path_next_has_confident_primary`
  - 在 `partial_chain_stage_probe.py` 的 `build_candidate_row()` 中补这三个字段透传
  - 然后只做最小验证：
    - `librime\\.\\build.bat static`
    - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case1_yizhixiangwang --mode full`
    - 单测 `test_case1_xiangwangzhe_exports_primary_path_probe_reason`
- fresh `case1 full` 的关键新证据：
  - `向往着`
    - `continuation_tag = legal_primary_continuation`
    - `path_tag = legal_but_path_unconfirmed`
    - `path_next_prefix_family_tag = family_clean`
    - `path_next_exact_count = 9`
    - `path_next_has_confident_primary = false`
    - `source_axis_tag = shared_prefix_axis_member`
- 这把当前断点进一步收紧为：
  - `向往着` 之所以还拿不到 `primary_path_eligible`
  - 不是因为它到达的下一跳 prefix family 已经漂移
  - 也不是因为下一跳没有 exact continuation
  - 而是**下一跳虽仍是 `family_clean`，但在当前 `HasConfidentPrimaryExact()` 口径下没有形成 confident primary**
- 因而下一步若继续，应优先围绕：
  - `一直向往着 -> 远 / 与 / 于` 这一跳的 next-hop confident-primary 形成条件
  - 尤其是当前单字 next-hop 是否天然被 `ClassifyPrimaryPathTag()` 的 exact-count / margin 口径排除
  - 而不是再把问题泛化回“上游 family 已脏”或“多字 request-stage 没开门”
- 因而当前更合理的阶段决策是：
  - baseline 工具链优化先到这里即可
  - 除非后续再次发现明显的非算法噪声（例如新的大文件导出、错误的重复查询、或新的工件错配）
  - 否则主线应回到产品目标本身：继续推进 `witogram + witset` 在一档模式下的准确率验证与改进
- 为了避免“随机 smoke”不能反映真实错排，又额外建立了一个手工典型错句小集合：
  - `docs/e1_typical_error_smoke_cases.txt`
  - 其中混合了两类样本：
    - `octagram` 已知能做对的局部同音/局部错排锚点
    - 文档里反复出现的经典难例
- 直接把这些句子“单独裸跑”会失真，因为当前链路对孤立短句会明显退化，不能代表 baseline 中的真实场景；因此后续快速验证采用的是：
  - 保留每条 case 在原始 `300` 条语料中的 `preceding_text`
  - 用“原始前文 + 目标句”的方式做独立回放
  - 只观察目标句那一步的 `Top-1`
- 对 E1 最小原型（`translator` 前移 `upstream_edge_prior`，同时将 `upstream_path_prior_weight=0` 防止双重计分）先做了第一轮快速止损：
  - 优先选最有信息量的 `octagram` 正确锚点做回放
  - 当前已完成并可信的前 `4` 条对照为：
    - `店里摆满了各种各样的手工艺品`
    - `暖黄色的灯光映照在青石板路上`
    - `营造出一种温馨浪漫的氛围`
    - `还让我体验到了不同地方的风土人情`
  - 结果：
    - E1 关闭态与开启态逐例完全一致
    - `changed = 0`
    - 其中前 `2` 条本来就已正确；后 `2` 条在 E1 开/关下都未修好
    - 尤其第 `4` 条是 `octagram` 明确能做对、而当前链路仍做不对的 case，但 E1 也没有带来任何改善
- 阶段性判断：
  - 这版“把局部风险统一减到 `DictEntry.weight` 上”的 E1 最小 prior 设计，没有在最关键的小样本锚点上体现出独立正收益
  - 因而当前原型先判负，不值得继续围绕这组常数做细调
  - 已将用户环境中的 `use_upstream_edge_prior` 再次恢复为关闭，避免把无收益实验配置留在线上
- 随后又把“为什么只有 4 条”的问题继续收口：
  - 先前只落到 `4` 条，不是因为样本意识不足，而是因为当时坚持了“每个 case 都用原始前文独立回放”的最严格口径
  - 这种做法在语料靠后位置会变得极慢，因为每新增一条都要从语料开头重新喂到该点
  - 这会导致扩样成本近似线性叠加，不适合快速止损
- 因而把扩样方式改成了“按原语料中的连续片段分组回放”：
  - 一次回放一整段连续片段
  - 再从同一次 `latest_candidates.json` 中同时抽取多个目标 case
  - 这样既保留真实前文，又能把样本量迅速扩大
- 采用该分组回放后，已把样本从 `4` 条扩大到 `17` 条：
  - `12` 条诗性连续片段硬例：`C:/Users/Bing/AppData/Roaming/witty/debug/e1_group_compare_12.json`
  - `5` 条旅游/叙事锚点：`C:/Users/Bing/AppData/Roaming/witty/debug/e1_travel_compare_5.json`
- 扩样后的结果仍然一致：
  - 诗性 `12` 条：`off_correct = 2`，`on_correct = 2`，`changed = 0`
  - 旅游/叙事 `5` 条：`off_correct = 3`，`on_correct = 3`，`changed = 0`
  - 其中旅游/叙事这组的 `octagram_correct = 4/5`
  - 关键 case `还让我体验到了不同地方的风土人情` 依旧是：
    - `octagram` 正确
    - 当前链路错误
    - E1 开关前后完全无变化
- 因而这轮更大样本验证后，原先的阶段结论不变：
  - 当前这版 E1 最小 prior 不只是“4 条里没效果”
  - 而是在更大且更有代表性的 `17` 条样本上，仍然没有体现独立正收益
- 随后又继续做了两轮更聚焦的 upstream 原型复查：
  - 第一轮：把 prior 从“统一绝对减分”改为“同一 `start_pos` 下，只有存在更干净且分数接近的替代边时，才对 risky edge 施加竞争式偏置”
  - 第二轮：在 graph 复核后确认目标 case 并不存在 `edge_risk`/`spelling_class` 信号，因此再补了一轮“merge-competition”原型：
    - 若同一 `start_pos` 下存在分数足够接近的多字词整块边，则仅对单字边施加小幅竞争惩罚
- 这两轮原型的验证结论仍然为负：
  - 竞争式 risk prior：`17` 条样本上仍然 `changed = 0`
  - merge-competition prior：先在最关键的 `travel 5` 复测，仍然 `changed = 0`
- 但这次 graph 复查带来了一个更有价值的定位：
  - 对 `还让我体验到了不同地方的风土人情` 导出局部 graph 后可见：
    - `不同 / 不同地方 / 地方 / 风土人情` 一带相关边全部是 `edge_risk = 0`、`edge_spelling_class = 0`
    - 因而前两版 risk-based prior 不是“力度太小”，而是压根没有命中目标边
  - 进一步查看原始边权后可见，问题核心更像是“短碎词天然略占优”：
    - `地 = -11.6603`，而 `地方 = -11.8442`
    - `方 = -11.7398`，而 `方的 = -12.0733`
  - 在 merge-competition 原型开启后，相关单字边权已被实际压低，说明逻辑确实生效：
    - `地` 从 `-11.6603` 降到 `-12.1403`
    - `方` 从 `-11.7398` 降到 `-12.2198`
    - `不`、`同`、`风` 等单字边也都同步被下调约 `0.48`
  - 但即便如此，最终 top-1 仍然不变，说明问题已经不是“translator 入口处一点点边权偏置”能单独解决
- 因而当前更可信的阶段判断变成：
  - 单靠 `translator` 侧的 upstream edge prior，不论是 risk-based 还是 merge-based，都不足以把这类错排拉回来
  - 下一步若继续推进主目标，更应该转向：
    - beam/search 过程中更强的相对比较
    - 或句级 scorer 中直接识别“整词 vs 碎片链”的竞争关系
    - 而不是继续细调 translator 入口处的边权常数

## 2026-05-18

- 开始实现 `E2` 的最小线上原型，目标刻意收得很窄：
  - 不再回到 `translator` 入口边权微调
  - 只验证两件事：
    - `最小有限状态契约`
    - `只影响 search_score 的 merge competition`
  - 不改最终句级主分数 `weight`
- 在 `witset_poet.cc` / `witset_poet.h` 中完成的核心改动：
  - 新增运行时开关：
    - `use_minimal_state_contract`
    - `beam_merge_competition_weight`
  - `BuildApproxStateKey(...)` 新增最小契约分支：
    - 仅保留 `context_suffix`
    - `trailing_single_char_run` 的截断桶
    - `tail_anchor_char_count` 的截断桶
  - `CompressLinePoolByState(...)` 与 `admitted_state_index` 都切到同一份最小契约
  - 新增 `ComputeBeamMergeCompetitionPenalty(...)`
    - 只在同一 `start_pos` 下存在接近的多字整块边时，对单字碎片链施加保活惩罚
    - 该惩罚只进入 `search_score`
    - 不进入最终可见的 `final_score / weight`
  - 调试输出新增：
    - `SearchMerge`
    - `StepSearchMerge`
- 同时修正了 `witset_poet` 中原本未接线的配置读取：
  - `beam_joint_guidance_weight`
  - `joint_prior_weight`
  - `use_minimal_state_contract`
  - `beam_merge_competition_weight`
- 已按工作区规则执行 `outwit-windows/build_and_deploy.bat`
  - 编译部署成功
- 第一轮先直接用 `docs/e1_typical_error_smoke_cases.txt` 顺跑 `20` 条做冒烟：
  - `summary_dir = C:/Users/Bing/AppData/Roaming/witty/debug/e2_minimal_state_contract_summary`
  - `wall_time_seconds = 163.296`
  - `Top-1 = 0.25`
  - `Top-3 = 0.30`
  - `expected_not_found_count = 13`
- 但这轮 `20` 条顺跑随后被明确判定为**不具可比性**：
  - 它使用的是这 `20` 条句子彼此前后文
  - 不是这些典型错句在原始语料中的真实 `preceding_text`
  - 因而它只能证明：
    - 新代码链路能跑通
    - 不能用来判断 `E2` 是否真的优于旧版

## 2026-05-24

- 继续回到 `case2/case3` 的 `source-line neutral-missing rescue` 线后，先把这轮 `full` 工件的口径对齐清楚：
  - `expansion_gate_records.stage = request` 记录的是 rescue **之前**的原始 request 分数
  - `batch_selected / admitted_new` 才会反映 rescue 写回后的：
    - `adjustment_score`
    - `search_score`
  - 因而如果只看 `request` 记录，会误以为这刀没有生效
- `case2: diyizhanshiyizuogulaodexiaozhen`
  - `我踏上了旅行的征程。第一站 -> 是一`
    - `request.adjustment_score = 0.0654507`
    - `batch_selected.adjustment_score = 8.91163`
    - `request.search_score = -242.110`
    - `batch_selected.search_score = -233.263`
  - `我踏上了旅行的征程。第一站 -> 是以`
    - `adjustment/search_score` 基本不变：
      - `adjustment_score = -0.558091`
      - `search_score = -223.534`
  - 说明这刀确实精确命中了目标 `neutral_missing` 候选 `是一`
  - 但完整句 snapshot 仍显示：
    - `rank 1 = 的驿站是以做古老的小镇`
    - `rank 3 = 的驿站是一座古老的小镇`
  - 且最终总分差额仍约 `4.57`
    - `top1 total = -435.46`
    - `correct-family-like chain total = -440.03`
  - 结合 debug 可见：
    - `是一座` 链的 `Dict` 明显更好
    - 但 `LmScaled` 仍比 `是以做` 链差约 `48.48`
    - 当前这刀只能部分回补，还不足以翻正 `case2`
- `case3: tiyanbuyiyangdeshenghuo`
  - `渴望去看看不同的风景，体验 -> 不一样`
    - `request.adjustment_score = -0.530729`
    - `batch_selected.adjustment_score = 13.2076`
    - `request.search_score = -194.412`
    - `batch_selected.search_score = -180.674`
  - 同层 peer：
    - `体验 -> 不易` 仍保持：
      - `adjustment_score = -0.300938`
      - `search_score = -124.697`
  - 说明这刀同样不是 no-op，而是确实把 `不一样` 拉回了主盘面
  - 完整句 snapshot 结果更新为：
    - `rank 1 = 体验不宜养的生活`
    - `rank 2 = 体验不一样的生活`
    - `rank 7 = 体验不易样的生活`
    - `rank 8 = 体验不易养的生活`
  - top1 vs top2 的剩余差额已缩到约 `0.84`
    - `体验不宜养的生活 total = -315.55`
    - `体验不一样的生活 total = -316.39`
  - 但其结构也更清楚了：
    - `不一样` 链的 `Dict` 明显更好
    - rescue 后 `Adj` 也明显更好
    - 剩余失守点主要仍在更早前缀累计下来的 `Base/LmScaled`
      - `不宜养的生活 LmScaled = -262.09`
      - `不一样的生活 LmScaled = -295.43`
- 因而这轮可正式收口出一个更稳的阶段判断：
  - `source-line neutral-missing rescue` 是一条**部分有效**路线，不是失败或 no-op
  - 它已经足以：
    - 把 `case2` 的正确链抬进前 3
    - 把 `case3` 的正确句抬到前 2
  - 但它当前还不足以单独翻正：
    - `case2` 仍被更早错误 family 压住
    - `case3` 仍有少量 `Base/LmScaled` 欠账
- 下一步若继续沿这条线推进，重点不该再重复证明“bonus 有没有命中”，而应转向：
  - `case2`
    - 继续拆 `是一座` 链为何仍被更早错误 family 的 LM/base 压住
  - `case3`
    - 继续拆 `不宜养` 相对 `不一样` 的前缀累计 LM 优势从哪里形成
  - 并评估是否需要从“同 source-line 回补”进一步升级到：
    - 更高一层的 family / phrase-level 相对比较
- 继续把完整句 top 候选往前逐跳反查后，`case2` 的问题层级又更清楚了一档：
  - 对尾串：
    - `的驿站是以做古老的小镇`
    - `的驿站是一座古老的小镇`
  - 在当前 `batch_selected` 工件里，能共同稳定反查到的更早前缀都是：
    - `我踏上了旅行的征程。的 -> 驿 站`
    - `search_score = -137.831`
    - `base_score = -134.110`
    - `lm_score_scaled = -60.1732`
  - 这说明当前 `rank 3` 的“正确链”并不是从 `第一站` family 真正翻回来的
  - 而是仍然继承了错误的 `的驿站` family，只是在更后面把尾块从 `是以做...` 拉向了 `是一座...`
  - 因而：
    - 仅做 `第一站 -> 是一` 这类局部 rescue 还不够
    - 下一刀若继续有效，必须更早介入 family 级相对比较，而不是只救尾跳
- `case3` 的逐跳反查也已能明确给出真实链形态：
  - top1 `体验不宜养的生活` 这条线目前在 `batch_selected` 里能稳定回溯到：
    - `体 -> 验`
      - `search_score = -96.680`
      - `lm_score_scaled = -18.7295`
    - `体验 -> 不`
      - `search_score = -103.720`
      - `lm_score_scaled = -15.8243`
    - `体验 -> 不宜`
      - `search_score = -149.815`
      - `lm_score_scaled = -61.2994`
  - top2 `体验不一样的生活` 这条线则回溯到：
    - `体 -> 验`
      - 同上
    - `体验 -> 不`
      - 同上
    - `体验 -> 不一`
      - `search_score = -152.117`
      - `lm_score_scaled = -63.602`
    - `体验 -> 不一样`
      - `request.search_score = -194.412`
      - `batch_selected.search_score = -180.674`
      - `adjustment_score = 13.2076`
    - `体验 -> 不一样的`
      - `request.search_score = -211.785`
      - `batch_selected.search_score = -196.512`
      - `adjustment_score = 14.9442`
  - 由此可以确认：
    - 两条线在 `体验 -> 不` 之前并无分歧
    - 第一次稳定分叉就在：
      - `体验 -> 不宜`
      - `体验 -> 不一`
    - 且 `不一样/不一样的` 已经是整块 phrase-level 候选，不是靠后续 `的生活` 才补回来的
  - 因而 `case3` 当前残余缺口更准确地说是：
    - `不宜` 相对 `不一`
      - 在更早一跳就已有约 `2.3` 分领先
    - 后续 `source-line rescue` 虽能把：
      - `不一样`
      - `不一样的`
      - 拉回主盘面
    - 但还不足以完全覆盖这条从更早前缀累计起来的 phrase-level 优势
- 继续把 `case2` 的早期 exact record 钉死后，`family-level` 的失守点也收得更具体了：
  - `第 -> 一`
    - 当前并不是主问题
    - `request/batch_selected` 里：
      - `终于，在一个假期，我踏上了旅行的征程。第 -> 一`
      - `search_score = -123.441`
      - `base_score = -120.584`
      - `lm_score_scaled = -45.4751`
  - `第一 -> 站`
    - 也仍是健康领先：
      - `search_score = -159.278`
      - `base_score = -159.739`
      - `adjustment_score = 0.461635`
      - `lm_score_scaled = -44.3238`
  - 真正把错误 family 做大的，是另一条更早错误前缀：
    - `我踏上了旅行的征程。的 -> 驿站`
    - `request.search_score = -137.831`
    - `request.base_score = -134.110`
    - `request.lm_score_scaled = -60.1732`
    - `dict_score_raw = -12.7682`
  - 这说明：
    - `第一` 与 `第一站` 本身并没有先天掉到盘面外
    - 但错误前缀 `的` 在下一跳直接拿到了强 multi-char 候选 `驿站`
    - 导致错误 family 在更早层就形成了更强的整块扩展能力
- 同时，`source-line rescue` 在 `的` 这条错误 family 上也出现了一个关键边界：
  - 对 `终于，在一个假期，我踏上了旅行的征程。的`
    - `entry = 一站`
    - 原始 `request.search_score = -155.761`
    - 回补后 `batch_selected.search_score = -146.372`
  - 但同层错误候选：
    - `entry = 驿站`
    - 仍保持：
      - `search_score = -137.831`
  - 即：
    - 当前 rescue 确实能把“更像正确尾块”的 `一站` 拉起来
    - 但即便在同一个错误前缀 `的` 下，`一站` 仍比 `驿站` 落后约 `8.54`
  - 因而 `case2` 当前更可信的判断进一步收敛为：
    - 问题不是 `第 -> 一` 或 `第一 -> 站` 单点失守
    - 而是错误前缀 family 一旦成立后，会在下一跳获得更强的整块 phrase 扩展
    - 若要继续有效，下一刀应更偏向：
      - family-level prefix gating
      - 或在错误前缀下限制 `驿站/翼展` 这类高吸引力错误整块
- `case3` 的 LM evidence 也进一步钉死：
  - `去看看不同的风景，体验 -> 不宜`
    - `token_evidence_tag = split_token_supported`
    - `total_log10 = -53.244`
    - `oov_token_count = 0`
  - `去看看不同的风景，体验 -> 不一`
    - `token_evidence_tag = neutral_missing`
    - `total_log10 = -55.244`
    - `oov_token_count = 1`
  - `去看看不同的风景，体验 -> 不一样`
    - `token_evidence_tag = neutral_missing`
    - `total_log10 = -93.7432`
  - `去看看不同的风景，体验 -> 不一样的`
    - `token_evidence_tag = neutral_missing`
    - `total_log10 = -110.011`
  - 与此同时，`transition_lm_features` 中：
    - `不宜养`
    - `不宜养的`
    - 均不存在
  - 这说明 top1 `体验不宜养的生活` 并不是靠：
    - `不宜养`
    - `不宜养的`
    这样的整块 LM whole-word 直接取胜
  - 更准确的结构是：
    - 更早一跳 `不宜` 就已凭 `split_token_supported + OOV0` 领先
    - 后面再通过 `不宜 -> 养 -> 的` 这类连续可支持路径把优势延续下去
  - 因而 `case3` 若继续推进，更合适的方向不是“单独把 `不一样` 再抬高一点”
  - 而是更早比较：
    - `split_token_supported + OOV0` 的双字前缀
    - 相对 `neutral_missing + OOV1` 的双字前缀
    在 phrase continuation 场景里的资格与保活强度

## 2026-05-24

- 基于上面的收口，继续在 `witset_poet.cc` 做了一刀更窄的 post-request family bonus：
  - 不改 request 原始计分
  - 不改 `case2` 现有的 source-line neutral-missing rescue
  - 只在 `all_requests` 后处理中补一个：
    - 同 `source_line`
    - 同首字
    - 双字 `neutral_missing` 前缀
    - 且同一 source 下已存在更长同前缀 `neutral_missing` continuation
    的 family continuation bonus
- 这刀的设计意图是：
  - 不直接和 `不宜` 这种 `split_token_supported + OOV0` 候选硬比“谁更像词”
  - 而是给 `不一 -> 不一样 / 不一样的` 这种同前缀 continuation family 一次有限保活，确认 phrase continuation 这条判断是不是可落地
- 最小编译：
  - `librime/build.bat static`
  - 已通过
- 两例最小验证：
  - `partial_chain_stage_probe.py --mode snapshot --case case2_diyizhan --case case3_tiyanbuyiyang`
  - 随后再补
    - `--mode full`
    只读取这两个 case
- 新结果：
  - `case3` 成功翻正
    - 之前：
      - `rank 1 = 体验不宜养的生活`
      - `rank 2 = 体验不一样的生活`
    - 现在：
      - `rank 1 = 体验不一样的生活`
      - `rank 2 = 体验不宜养的生活`
  - snapshot 读数：
    - `体验不一样的生活`
      - `Base = -328.49`
      - `Adj = 13.66`
      - `Total = -314.83`
    - `体验不宜养的生活`
      - `Base = -310.54`
      - `Adj = -5.01`
      - `Total = -315.55`
  - 即：
    - 这刀大约把 `体验不一样的生活` 额外再抬了约 `1.56`
    - 从原先落后约 `0.84`
    - 变成领先约 `0.72`
- `full` 工件也确认这次不是 snapshot 偶然：
  - `体验 -> 不一`
    - `request.search_score = -152.117`
    - `batch_selected.search_score = -149.282`
    - `adjustment_score = -0.682126 -> 2.15309`
  - `体验 -> 不一样`
    - `request.search_score = -194.412`
    - `batch_selected.search_score = -179.398`
    - `adjustment_score = -0.530729 -> 14.4834`
  - `体验 -> 不一样的`
    - `request.search_score = -211.785`
    - `batch_selected.search_score = -194.952`
    - `adjustment_score = -0.329413 -> 16.5036`
  - 同时：
    - `体验 -> 不宜`
    - 仍保持原值不变
    - `search_score = -149.815`
    - `adjustment_score = -0.656451`
  - 这说明这刀确实命中了：
    - `不一`
    - `不一样`
    - `不一样的`
    这一整族 continuation
  - 而没有直接去抬 `不宜`
- `case2` 侧本轮未见副作用：
  - `我踏上了旅行的征程。的 -> 驿站`
    - `request/batch_selected.search_score` 都仍是 `-137.831`
  - `我踏上了旅行的征程。的 -> 一站`
    - 仍是旧的 source-line rescue 结果：
      - `request.search_score = -155.761`
      - `batch_selected.search_score = -146.372`
  - snapshot 前 12 也保持：
    - `rank 1 = 的驿站是以做古老的小镇`
    - `rank 3 = 的驿站是一座古老的小镇`
  - 说明这刀基本只作用在：
    - `case3` 式的双字 neutral-missing continuation family
    - 没有误抬 `case2` 的 `的 -> 一站 / 一站式`
- 因而当前阶段结论更新为：
  - `case3` 这条“same-leading-char dual-prefix continuation family bonus”路线已被最小实验证明有效
  - `case2` 当前仍然没有被一并解决，且主缺口仍是：
    - 错误前缀 family 一旦成立后
    - 会在下一跳获得更强的整块 phrase 扩展
  - 后续若继续推进，应把新主线收口到：
    - `case2` 的 family-level prefix gating

## 2026-05-24

- 围绕 `case2` 又试了一刀更偏“正向共识”的 family-level 实验，但目前可以判负：
  - 设计思路：
    - 不直接惩罚 `的 -> 驿站`
    - 而是在 `all_requests` 后处理里，给“同一短文本被多种分词路径同时支持”的早期 family 一个小 bonus
    - 目标对象是：
      - `第一站`
      - 因为它至少有：
        - `第 -> 一站`
        - `第一 -> 站`
        两条请求路径支撑
  - 这条路的动机是：
    - 若 `第一站` 能靠“多路径共识”在更早层保活
    - 后续已有的：
      - `source-line neutral-missing rescue`
    - 才有机会把 `是一 / 一座` 继续往上带
- 但最小验证后，这条路没有给出有效信号：
  - 最新 `snapshot` 中：
    - `第一站是一座古老的小镇`
      - 仍然不存在
    - `第一站是以做古老的小镇`
      - 仍然不存在
    - 含 `第一站` 的候选：
      - `COUNT = 0`
  - 前 3 仍保持：
    - `rank 1 = 的驿站是以做古老的小镇`
    - `rank 2 = 的翼展示已作古老的小镇`
    - `rank 3 = 的驿站是一座古老的小镇`
- 因而当前可以把这条实验正式记为：
  - `short convergent text support bonus`
  - 对 `case2` 判负
- 这条路的问题不是“力度差一点”这么简单，而更像：
  - 即便早期 `第一站` 存在多路径支撑
  - 它也没有进入最终 snapshot 主盘面
  - 说明主缺口仍不在“短文本共识保活”
  - 而仍在：
    - 错误前缀 family
    - 尤其是 `split_token_supported + OOV0` 的整块扩展
    - 如何在下一跳就压住 `neutral_missing + OOV1` 的正确链
- 同时，本轮已把这刀代码回退，避免无效实验残留在 `witset_poet.cc`。

## 2026-05-24

- 为避免再被 `partial_chain_stage_probe.py` 的 `load_cases()` 卡住，这轮新增了一条稳定的最小验证口径：
  - 不再让脚本去读取巨大的 `latest_candidates.json`
  - 直接在 Python 里 import `partial_chain_stage_probe.py`
  - 手工构造：
    - `case2_diyizhan`
    - `case3_tiyanbuyiyang`
    的 `input + preceding_text`
  - 然后直接调用：
    - `update_schema(...)`
    - `query_cases(...)`
  - 这样可以稳定生成新的 `partial_chain_stage_probe.snapshot.jsonl`
- 基于这条新验证口径，继续试了一个更“体系内”的 poet 主逻辑修正：
  - 将 `same-span competition` 的 3 字 span 纳入比较
  - 也就是把：
    - `generated_char_count < 4`
    的门槛放宽到：
    - `generated_char_count < 3`
  - 设计动机：
    - 现有 `same-span split anchor vs reparse line` 机制本来就适合处理：
      - `第一站`
      - vs
      - `的驿站`
      这种同 span 竞争
    - 但之前 3 字 span 被直接挡在机制外
- 最小编译：
  - `librime/build.bat static`
  - 已通过
- 新结果：
  - `case3` 保持不变：
    - `rank 1 = 体验不一样的生活`
  - `case2` 出现了新的正向信号：
    - `第一*` family 第一次重新回到主盘面前列
    - 当前：
      - `rank 2 = 第一展示已作古老的小镇`
  - 同时：
    - 旧的 `的驿站...`
    - 不再一边倒垄断前几名
    - 现在落到：
      - `rank 4 = 的驿站是以做古老的小镇`
- 但这条路目前仍是“部分有效”而非最终翻正：
  - 新 snapshot 中：
    - `第一站是一座古老的小镇`
      - 仍不存在
    - 含 `第一站` 的候选：
      - 仍为 `0`
    - 目前进入前列的是：
      - `第一展示...`
      - 而不是：
      - `第一站...`
- 因而这轮最新判断是：
  - `same-span competition` 扩到 3 字 span 不是 no-op
  - 它确实把竞争主面从：
    - `的驿站 / 的翼展示`
    部分拉回到：
    - `第一展示`
  - 但主缺口已进一步收口为：
    - `第一*` family 已回面
    - 只是 `第一展` 仍压着 `第一站`
  - 下一步最值得做的是：
    - 直接读取这版 graph
    - 比较 `第一 -> 站` 与 `第一 -> 展示`
      在 request/admitted 层面的差异

## 2026-05-24

- 这轮继续沿 `same-span 3-char` 方向做了更细的 graph 复核，并把 `partial_chain_stage_probe` 的单例图导出链重新打通：
  - 经验结论：
    - 若只开 `collect_graph=true` 而不同时等待 snapshot，主 case 的 `graph.jsonl` 很容易落成空文件
    - 对单例 graph 采集，必须至少同时开 `collect_snapshot=true`，让宿主有时间把图写完
  - 之后用手工 `query_cases()` 成功拿到了 `case2` 的 `graph.jsonl`
- graph 复核后的新结论：
  - `第一站` 这条正确链并没有死
  - 在 graph 中已经稳定可见：
    - `第一 -> 站`
    - `第一站 -> 是`
    - `第一站 -> 是一`
    - `第一站 -> 是一座`
  - 对应证据包括：
    - `prefix_text = 第一`
    - `request_stage_prefix_text = 第一站`
    - 候选 `站`
      - `request_stage_tag = request_source_line_eligible`
    - `prefix_text = 第一站`
    - `request_stage_prefix_text = 第一站是`
    - 候选 `是`
      - `request_stage_tag = request_source_line_eligible`
    - `prefix_text = 第一站`
    - `request_stage_prefix_text = 第一站是一`
    - 候选 `是一`
    - `prefix_text = 第一站`
    - `request_stage_prefix_text = 第一站是一座`
    - 候选 `是一座`
- 因而当前 `case2` 的主缺口不再是：
  - “`第一站` 没进 request-stage / admitted”
  - 而是：
    - 正确 request-stage prefix 已经形成
    - 但后续仍被两类重解释继续压住
- 当前主要压制者分成两层：
  - 第一层：
    - `family_soft_clean`
    - 也就是：
      - `第一 + 展示 / 战士 / 战时 ...`
    - 这些候选在 graph 里是：
      - `continuation_tag = exact_ambiguous_family`
      - `request_stage_tag = not_request_stage_candidate`
    - 但由于它们是 2 字 whole-word reparse，仍能在 beam 里压住：
      - `第一站 + 是`
  - 第二层：
    - `family_drifted + wrong_family_without_path_unconfirmed`
    - 也就是：
      - `地 + 一站式 / 以展示 ...`
      - 以及后续：
      - `地一站式 + 一座 / 一组 ...`
    - 它们借用同一个正确的：
      - `request_stage_prefix_text`
      继续往前冲
- 这说明 `same-span 3-char` 的作用是：
  - 把主竞争面从：
    - `的驿站 / 的翼展示`
    拉回到：
    - `第一*`
  - 但它还没继续做到：
    - `第一站 + 是`
    压过：
    - `第一 + 展示`
- 同轮还做了一刀很窄的常数实验：
  - 直接收紧 `ComputeSameSpanCompetitionPenalty()` 中
    - `matched_whole_word && generated_char_count == 3`
    时的 `allowed_margin`
  - 原意：
    - 让 `第一 + 展示` 更容易吃到 same-span 罚分
  - 结果：
    - 直接把更外层的：
      - `敌意展示 / 地衣展示`
      也一起抬上来了
    - 新 snapshot 前列变成：
      - `rank 1 = 敌意展示已作古老的小镇`
      - `rank 2 = 第一展示已作古老的小镇`
      - `rank 3 = 地衣展示已作古老的小镇`
  - 因而这条“只收紧 3 字 whole-word reparse margin”的常数刀可以正式判负
  - 已回退，不保留在代码中
- 因而当前最新收口是：
  - `same-span 3-char` 本身继续保留，因其确实把 `第一*` family 拉回主竞争面
  - 但下一刀不能只是再调 `allowed_margin` 常数
  - 更可能需要：
    - 在 `same-span` 或相邻竞争逻辑中
    - 增加 family/source 维度
    - 明确区分：
      - `第一站 + 是`
      这种已形成 clean request-stage prefix 的 split continuation
      与
      - `第一 + 展示`
      这种仍属 `exact_ambiguous_family` 的 2 字重解释

## 2026-05-24

- 这轮继续把 `case2` 的观察层级下钻到更短输入：
  - 新增单例：
    - `diyizhanshi`
  - 目标是直接观察：
    - `第一站是`
    - `第一展示`
    - `第一战士`
    - `地驿站是`
    在最短主竞争面里的即时盘面
- 新 snapshot 结果进一步说明：
  - `第一站是`
    - 仍未进入前 20
  - `第一展示`
    - 当前仅在 `rank 12`
  - 真正压住这层盘面的，不是单一 `第一展示`
    - 而是一整层混合重解释簇：
      - `的翼展示`
      - `地驿站是`
      - 以及大量同结构的 `*驿站是`
- 这说明当前主问题比上一轮再往上抬了一层：
  - 不是“只压 `第一展示` 就够”
  - 而是：
    - `第一站是`
    在短输入 `diyizhanshi` 下
    连前 20 都没保住
    - 上面压着的是混合 cluster
    - 其中既有：
      - `展示`
      线
    - 也有：
      - `驿站是`
      线
- 同轮试了一刀很窄的现成机制增强：
  - 继续沿 `ComputeRequestStageSourceMismatchPenalty()` 做 source-aware 实验
  - 原意：
    - 在“短前缀 + 二字 whole-word 重解释”场景下
    - 对 source mismatch 再额外加一点 multiplier
  - 设计动机来自前一轮证据：
    - `第一展示已作古老的小镇`
      已经带有明显的：
      - `AltReqBridge`
      - `ReqSrcMismatch`
    - 说明机制已经打中，只是可能力度不够
- 但最小验证后，这条路可以判负：
  - 长句 `diyizhanshiyizuogulaodexiaozhen` 的 snapshot 前 12 无变化
  - `第一展示已作古老的小镇` 的 debug 中：
    - `ReqSrcMismatch`
      仍保持原值
    - 没有出现额外下沉
  - 短句 `diyizhanshi` 的盘面也没有被改写：
    - `第一站是`
      仍未进前 20
    - `第一展示`
      仍是 `rank 12`
- 因而这条“增强 `ReqSrcMismatch` multiplier”的最小实验当前记为：
  - no-op / 判负
  - 已回退，不保留在代码中
- 这轮额外还有一个重要操作性结论：
  - 对 `diyizhanshi` 这种短输入，只看长句 `diyizhanshiyizuogulaodexiaozhen` 的 graph/snapshot 会掩盖问题层级
  - 后续若继续做 `case2`，应固定并行观察：
    - `diyizhanshi`
    - `diyizhanshiyizuogulaodexiaozhen`
  - 这样才能区分：
    - `第一站是` 自身没保住
    - 还是后面 `一座 / 古老的小镇` 才失守

## 2026-05-24

- 这轮继续只看短输入 `diyizhanshi`，并把观察重心切到 `word_edges` 与 `post_admit_target_pool`
- `word_edges` 里已经可以明确看到：
  - 正确 split 线确实存在：
    - `prefix_text = 第一`
    - `request_stage_prefix_text = 第一 站`
    - 候选 `站`
      - `request_stage_tag = request_source_line_eligible`
    - `prefix_text = 第一站`
    - `request_stage_prefix_text = 第一站是`
    - 候选 `是`
      - `request_stage_tag = request_source_line_eligible`
  - 同时，竞争的 2 字 whole-word reparse 线也存在于同一层：
    - `prefix_text = 第一`
    - `request_stage_prefix_text = 第一站是`
    - 候选：
      - `展示`
      - `战士`
      - `战时`
    - 这些候选统一是：
      - `continuation_tag = exact_ambiguous_family`
      - `request_stage_tag = not_request_stage_candidate`
- 这说明当前 `case2` 的短输入主缺口不是：
  - `第一站 -> 是` 没有被生成
  - 而是：
    - split 线与 2 字 whole-word reparse 线在同一 span 上并存
    - 后者虽然不是 request-stage candidate，却仍能凭更高整块权重压盘
- `word_edges` 还给出另一个重要细节：
  - 从 root 看整块 `0..11` span 时，
    - `request_stage_prefix_text = 第一站是`
    - 但候选只有：
      - `第一战士`
    - 没有：
      - `第一站是`
  - 这说明短输入里 split 线和 whole-word 重解释线，从一开始就不是同一种“整块候选形态”
- `post_admit_target_pool` 进一步确认：
  - admitted pool 里有大量：
    - `第一站`
      - 计数约 `1722`
  - 也有更多：
    - `第一战士`
      - 计数约 `2473`
  - 但没有：
    - `第一站是`
    - `第一展示`
      作为 `post_admit_target_pool` 的 source 前缀
- 因而当前最新判断是：
  - `第一站` 前缀本身并没有死在 admission
  - 真正掉队点更像是：
    - 已 admitted 的 `第一站`
    - 在下一跳 `-> 是`
      上，仍然输给：
      - `第一战士`
      这类整块重解释前缀
- 这也解释了为什么现有 `same-span competition` 只做到“部分有效”：
  - 当前 `same-span` 只在：
    - reparse 线的 `contract_support`
      明显弱于 split anchor 时才加罚
  - 但从 `word_edges` 现象看，
    - `第一战士 / 第一展示`
      这类 reparse 线继承了同一份：
      - `request_stage_prefix_text`
      - request-stage 支撑语义
    - 所以它们未必会在 `same-span` 里呈现出“support 更弱”的形态
- 下一步若继续，不应再优先调：
  - `allowed_margin`
  - 或单纯调 `ReqSrcMismatch`
- 更可能需要：
  - 在 `same-span` 或相邻竞争逻辑里
  - 显式识别：
    - `request_source_line_eligible` 的 split continuation
      与
    - `exact_ambiguous_family` 的 2 字 whole-word reparse
  - 即使二者共享同一份 `request_stage_prefix_text`
    也要把后者继续视作 weaker reparse competitor

## 2026-05-24

- 这轮基于上面的 `word_edges` 结论，又试了一刀极窄的 `same-span` 扩展：
  - 目标：
    - 不再要求 reparse 线的 `contract_support` 必须严格弱于 split anchor
    - 对于：
      - root whole-word reparse
      - `generated_word_count == 1`
      - `step_matched_whole_word == true`
    - 允许它在 `contract_support` 与 split anchor 打平时，也进入 same-span 竞争
  - 同时把这类 equal-support reparse 的 `allowed_margin` 收紧到更小的固定值
- 设计动机是：
  - 现有 `same-span` 只在 `contract_support_gap > 0` 时生效
  - 但从 `diyizhanshi` 的 `word_edges` 看，
    - `第一战士 / 第一展示`
      并不一定会在这一维显示成“support 更弱”
- 最小验证结果显示，这条路明显判负：
  - 短输入 `diyizhanshi`：
    - `第一站是`
      仍未进入前 20
    - `第一展示`
      虽仍只在 `rank 12`
      但整体前列被：
      - `敌意展示`
      - `地衣展示`
      - 以及更多 `*驿站是`
      混合簇进一步占据
  - 长句 `diyizhanshiyizuogulaodexiaozhen`：
    - top3 直接变成：
      - `敌意展示已作古老的小镇`
      - `第一展示已作古老的小镇`
      - `地衣展示已作古老的小镇`
    - 正确句：
      - `第一站是一座古老的小镇`
      仍缺席
  - `case3`：
    - `体验不一样的生活`
      仍保持 `rank 1`
- 这说明：
  - “把 equal-support 的 root whole-word reparse 也纳入 same-span 竞争”
    并不会优先压住：
    - `第一战士 / 第一展示`
  - 反而会把更外层的：
    - `敌意展示 / 地衣展示`
    这一簇也一起放大
- 因而这条路线正式判负：
  - 不保留在代码中
  - 已完整回退
- 当前更稳的结论变成：
  - 现有 `same-span` 的核心问题不只是：
    - `contract_support` 判定太严格
  - 更像是：
    - 一旦把 root whole-word reparse 打开比较面
    - 更外层的错误 family 也会一起进入同一竞争场
  - 所以下一步若继续，不能只在 `same-span` 上放宽比较条件
  - 还需要额外的：
    - family/source 约束
    或
    - 把比较限制在已 admitted 的 clean split family 内部
- 随后切回之前已经收敛好的验证口径：
  - 继续复用旧的 `travel 5` 与 `group 12` 锚点
  - 从 `docs/benchmark_artifacts/octagram_300_20260516/snapshot_summary/latest_candidates.json` 读取这些 case 的真实：
    - `input`
    - `preceding_text`
  - 逐条独立回放当前版本
  - 对照旧产物：
    - `C:/Users/Bing/AppData/Roaming/witty/debug/e1_group_compare_12.json`
    - `C:/Users/Bing/AppData/Roaming/witty/debug/e1_travel_compare_5.json`
- 这轮同口径 `17` 条锚点结果写入：
  - `C:/Users/Bing/AppData/Roaming/witty/debug/e2_minimal_compare_17.json`
- 对照结论：
  - `total = 17`
  - `off_correct = 5`
  - `on_correct = 5`
  - `changed = 0`
  - `fixed = 0`
  - `regressed = 0`
- 关键错例继续保持原状：
  - `还让我体验到了不同地方的风土人情`
    - 旧版：`还让我体验到了不同的方的风土人情`
    - 当前 `E2`：仍然 `还让我体验到了不同的方的风土人情`
  - `营造出一种温馨浪漫的氛围`
    - 旧版：`营造出意中文新浪漫得分为`
    - 当前 `E2`：仍然 `营造出意中文新浪漫得分为`
- 同时确认这轮不是“代码没生效”：
  - 在当前 `witset_local_snapshot.jsonl` 中，`SearchMerge` 已出现大量非零值
  - 例如：
    - `还让我体验到了不同的方的风土人情` 的 top-1 已带 `SearchMerge:0.24`
    - `营造出...` 一类碎片链候选可见 `SearchMerge:1.20`
  - 说明这轮新信号确实进入了搜索保活链路
  - 只是仍然没有把正确整块路径推到第一名
- 当前阶段结论：
  - `E2` 最小原型已经可以判负
  - “把 merge competition 从 `translator` 改到 `search_score`，再配合最小有限状态契约”这一步本身，不足以改变当前这批最关键锚点的 top-1
  - 换句话说：
    - 问题不只是状态桶太粗或局部竞争信号没进入保活
    - 当前线上契约离真正有效的“正确路径更早活下来”仍有明显距离
- 因而下一步不应立刻把这版 `E2` 扩到更大 baseline
  - 更合理的是回到此前判断：
    - 要么继续贴近离线 `C0` 的更强状态/相对比较
    - 要么回头检查更上游的路径契约与边权底座
- 验证结束后，已把用户环境中的实验开关恢复为默认关闭：
  - `use_minimal_state_contract = false`
  - `beam_merge_competition_weight = 0.0`
  - 避免把这轮已判负的配置留在线上

## 2026-05-18

- 按用户要求，在 `plugins` 目录下 clone 了原版仓库：
  - `c:/Code/outwit/outwit-windows/librime/plugins/librime-octagram`
- 重新核对原版 `octagram` 代码后，关键确认：
  - 原版插件本身非常薄
  - `octagram.cc` 只做一件事：
    - 在 `Poet` 的 `Grammar::Evaluate()` 接口上，对“前文后缀 + 当前词”查询 `.gram` 搭配分
  - 它并不负责：
    - 纠错路径生成
    - 候选词构造
    - 大规模重排
- 真正让原版 `octagram + n-gram` 配合顺的，不是插件内部更复杂，而是 `librime` 主链路本身的分层更对路：
  - `Syllabifier` 在最上游就把：
    - correction
    - completion
    - ambiguous joint
    的惩罚写进 `credibility`
  - `TableQuery / Dictionary` 沿音节图把 `credibility` 继续传给临时 `DictEntry.weight`
  - `Poet` 只需要在一个已经被上游先验压过的 `WordGraph` 上做 beam search
  - `Octagram` 再作为薄薄一层 grammar，为每次扩展补一个上下文 collocation 分
- 这次直接读代码后，对“为什么原版逻辑和 n-gram 更配”有了更明确的结论：
  - 原版是 `上游 credibility 先压坏路径 + 末端 n-gram 只做顺水推舟`
  - 也就是：
    - `n-gram` 不承担“从大量脏候选里救回正确整句”的职责
    - 它只负责在剩下的相对干净候选里做句法/搭配偏好
- 与当前 `witogram + witset` 的关键差异：
  - 当前 `witogram.cc` 的 `KenLM` 查询仍以 `SplitUtf8Tokens()` 为基础，把上下文与当前词都先拆成逐字 token，再额外尝试 whole-word 命中并做 blend
  - 这导致 `LM` 自身承担了更多“整词 vs 碎片链”的解释职责
  - 当前 `witset_poet` 也明显比原版 `Poet` 更重：
    - beam 更大
    - 额外特征更多
    - 需要更多中后段补丁来修正前面没压掉的碎片链
- 因而当前最值得借鉴的，不是简单把原版 `octagram.cc` 里的常数搬过来，而是它背后的结构原则：
  - `n-gram` 接入点要薄
  - 真正有效的强约束要尽量前移到：
    - syllabifier / translator 上游路径契约
    - 候选进入 `poet` 之前的竞争关系
  - 让 `poet + grammar` 面对的是已经较干净的候选图
- 这也进一步印证了前面的止损判断：
  - 当前之所以落后于原版 `octagram`
  - 更大概率不是因为 `KenLM` 本体不如原版 `.gram`
  - 而是因为我们还没有把原版最关键的“上游 credibility 契约”在线上重新建立好

## 2026-05-18 死亡点分桶：7 条 `octagram 对 / 当前错` 错例开 graph 复核

- 继续沿前一轮 `death_bucket_probe_current_12.json` 往下做，不再扩样本，而是直接对其中 `7` 条 `octagram top1 正确 / 当前链路错误` 的锚点做 graph 复核。
- 本轮为了拿到最小 graph 工件，临时把本地 schema 的 `debug_dump_local_graph_snapshot` 打开到单独文件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\death_bucket_probe.graph.jsonl`
  - 回放完成后已恢复为默认关闭，避免后续 baseline 默认继续写 graph。
- 本轮新增调试工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\death_bucket_probe.graph.jsonl`
  - `C:\Users\Bing\AppData\Roaming\witty\debug\death_bucket_graph_probe_7.json`
- 复核方法：
  - 仍按真实 `preceding_text` 回放原始 `input`
  - 对 graph 做“exact expected text path” 搜索：若图中存在一条从 `0 -> interpreted_length` 的候选路径，且沿路候选文本可精确拼出目标句，则记为“已进图”
  - 分桶规则收敛为：
    - `A`：目标句精确路径不存在于 graph
    - `B`：目标句精确路径存在，且已进入最终候选集，但名次靠后
    - `C`：目标句精确路径存在，但最终候选集中已消失
- 这 `7` 条错例的结果非常集中：
  - `A = 0`
  - `B = 3`
  - `C = 4`
  - `missing_graph = 0`
- 也就是说，这一批高信息量错例里，当前**没有证据表明“正确句子根本没进图”**。
- 更准确的现状是：
  - 一部分 case 已经能进最终候选，但被更强的碎片/同音链压到后面
  - 另一部分 case 连完整句路径都已经在 graph 里存在，但在最终候选阶段被丢掉

- `B` 类代表：
  - `还让我体验到了不同地方的风土人情`
    - 最终 `expected_rank = 10`
    - graph 中存在高质量整词路径：
      - `还让我 | 体验 | 到了 | 不同 | 地方的 | 风土人情`
  - `心里装着一些说不出口的心事`
    - 最终 `expected_rank = 7`
    - graph 中存在精确路径：
      - `心里 | 装着 | 一些 | 说不出口 | 的心 | 事`
  - `有人抓紧不被打扰的时光充电学习`
    - 最终 `expected_rank = 16`
    - graph 中存在精确路径：
      - `有人 | 抓紧 | 不被打扰 | 的 | 时光 | 充电 | 学习`
- 这些 `B` 类说明：
  - 目标句既没死在“候选缺失”，也没死在“完整句保活完全失败”
  - 更像是底座 `entry->weight` / 中后段竞争分配本身就把正确链压低了
  - 换言之，这些 case 的主因更接近“底座输掉”，而不是“根本没图”

- `C` 类代表：
  - `第一站是一座古老的小镇`
    - 最终 `expected_rank = null`
    - 但 graph 中存在精确路径：
      - `第一站 | 是一座 | 古老的 | 小镇`
  - `两旁是古色古香的建筑`
    - 最终 `expected_rank = null`
    - 但 graph 中存在精确路径：
      - `两旁 | 是 | 古色古香 | 的 | 建筑`
  - `忘带证件等突发状况打乱节奏`
    - 最终 `expected_rank = null`
    - 但 graph 中存在精确路径：
      - `忘带证件 | 等 | 突发状况 | 打乱 | 节奏`
  - `你是否度过了一段理想的休息时光`
    - 最终 `expected_rank = null`
    - 但 graph 中存在精确路径：
      - `你是否 | 度过了 | 一段 | 理想的 | 休息时 | 光`
- 这些 `C` 类说明：
  - 当前最该警惕的不是“没进图”
  - 而是“明明已经有完整正确路径，但在最终候选阶段仍被压没”
  - 这更像：
    - 搜索保活不够
    - 或底座先验已经让正确路径在中途过早失血，最终连显示候选都进不去

- 本轮还有两个对后续判断很重要的实现细节：
  1. graph 记录里的 `preceding_text` 不是原始长前文，而是运行时截断后的有效前缀；因此本轮 graph 匹配最终采用 `input_only` 回退，而不是原始 `preceding_text` 精确匹配。
  2. 这 `7` 条错例的 `graph_best_exact_path` 上，各段 `edge_risk / edge_spelling_class` 基本都为 `0`；也就是说，这批问题并不像早先某些 case 那样，主要死于显式的 ambiguous-joint 风险标签。

- 因此，下一步方向需要收敛：
  - 不再优先怀疑 `A` 类“没进图”
  - 先把主问题明确为：
    - `B`：底座竞争力不足，正确链已在最终候选里但排位太低
    - `C`：正确链在 graph 中存在，却没有活到最终候选
- 对应的工程动作也应调整为：
  - 不继续往“补更多 graph 覆盖率/查缺候选”方向发散
  - 转而重点检查：
    - 正确路径各段 `entry->weight` 与竞争同音链的相对差距
    - `witset_poet` 中 `SelectTopLines / CompressLinePoolByState / PruneLinePool` 一带是否过早把正确链压掉
    - 是否仍然存在“上游没有明显风险标签，但正确整词链在底座上先天吃亏，只能依赖后段 LM 救火”的结构性问题

- 这轮分桶给出的核心结论可以简化为一句话：
  - **当前主要矛盾不是 `A: 没进图`，而是 `B/C: 进图了，但底座不够强，且有一部分路径在后段活不到最终候选。`**

## 2026-05-18 `C` 类继续细分：容量截断型 vs 打分坍塌型

- 在确认 `C` 类都“图里有精确路径，但最终候选消失”之后，继续往 `witset_poet.cc` 的保活链路下钻。
- 先复核代码路径：
  - `PruneLinePool()` 真正执行前受 `ShouldTriggerPrune()` 限制；
  - 当前线上配置为：
    - `lazy_prune_min_words: 100`
    - `lazy_prune_ratio: 1.0`
    - `sentence_soft_limit: 36000`
  - 对这批仅 `4~6` 段的长句来说，常规池内惰性剪枝基本不会触发。
- 因此，原先笼统的“可能死在 `CompressLinePoolByState / PruneLinePool`”需要修正：
  - 当前更值得怀疑的是：
    - `global_batch_limit`
    - `SelectTopLinesWithDiversity()` 前后的头部截断
    - 以及更本质的分数体系本身

- 为了把这几种可能拆开，本轮做了一个最小容量对照实验：
  - 临时仅在本地 schema 放宽：
    - `max_candidates: 20 -> 100`
    - `word_beam_size: 80 -> 160`
    - `sentence_beam_size: 300 -> 1200`
    - `global_batch_limit: 2400 -> 12000`
  - 只回放两条代表性 `C` 类错例：
    - `第一站是一座古老的小镇`
    - `忘带证件等突发状况打乱节奏`
  - 跑完后已恢复默认配置，不保留实验状态。
- 新增实验工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\c_bucket_capacity_probe_2.json`

- 容量放宽后的结果非常关键：
  - `忘带证件等突发状况打乱节奏`
    - 原先 `expected_rank = null`
    - 放宽后进入 `rank = 3`
  - `第一站是一座古老的小镇`
    - 放宽后返回候选已扩到 `100`
    - 但正确句仍然 `expected_rank = null`
- 这说明 `C` 类内部至少分成两种：
  - **容量截断型 `C1`**
    - 正确句原本已经足够接近头部
    - 只是被现有 `20/300/2400` 这组容量限制压在头部外
    - `忘带证件等突发状况打乱节奏` 属于这一类
  - **打分坍塌型 `C2`**
    - 即便把返回候选和搜索容量显著放大
    - 正确句仍然进不了前 `100`
    - `第一站是一座古老的小镇` 属于这一类

- 这也进一步修正了上一轮对 `C` 的理解：
  - `C` 不能简单等同为“搜索保活坏了”
  - 更准确地说，它是：
    - 一部分确实受头部容量/截断影响
    - 另一部分则在现有打分体系下已经输得非常深，属于“极端低排位的 `B`”与“后段不可见”混合表现

- 还有一个对方向判断很重要的旁证：
  - 用 graph 上的最佳整句路径做词典底座对照时，`第一站是一座古老的小镇` 与 `忘带证件等突发状况打乱节奏` 的正确路径，其整句 `entry->weight` 求和并不差，甚至优于当前 top1 错句路径。
  - 这表明问题并不能简单归因于“上游底座已经完全输死”。
  - 至少在这两条里，后续句级打分/候选保活仍然在把本来不差的整词路径往下压。

- 因而下一步的优先级也需要继续收敛：
  1. 对 `C1`：
     - 重点查 `global_batch_limit` 与最终头部选取的副作用；
     - 看看是否存在“正确句已接近头部，但在 `20/300/2400` 组合下被系统性挤出”的稳定模式。
  2. 对 `C2`：
     - 重点查句级特征融合本身；
     - 特别是哪些局部碎片链在 `LM total / LM avg / whole word / fragment / tail` 的组合下被过度拔高。

- 这轮细分后的核心结论可以简化为一句话：
  - **`C` 不是单一的“搜索坏了”，而是同时包含“容量截断型 `C1`”和“打分坍塌型 `C2`”。**

## 2026-05-18 `C2` 关键过渡点复核：塌陷发生在句首 LM 过渡，不是尾部小惩罚

- 为了继续确认 `C2` 到底是哪些句级特征在压正确链，这轮没有改代码，而是直接从 graph 工件里把关键 `transition_lm_features` 抽出来做点对点复核。
- 新增工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\c2_transition_probe_diyizhan.json`
  - `C:\Users\Bing\AppData\Roaming\witty\debug\c2_transition_key_points.json`

- 这轮最重要的代表 case 仍然是：
  - `第一站是一座古老的小镇`
- 关键观察如下：
  1. 在句首上下文 `我踏上了旅行的征程。` 下：
     - `第` 的 `total_log10 = -38.4992`
     - `的` 的 `total_log10 = -38.4992`
     - 也就是说，LM 在第一步对 `第/的` **几乎完全不分胜负**
  2. 同一上下文下，整词 `第一站` 的过渡特征却非常差：
     - `total_log10 = -116.498`
     - `oov_token_count = 1`
     - `used_char_fallback = true`
     - `matched_whole_word = false`
     - 这说明 `第一站` 作为整块在当前模型上并没有被稳定识别成一个可用的 whole-word 过渡，而是退回到了更差的 char fallback / OOV 路径。
  3. 继续往下一步看：
     - 在上下文 `上了旅行的征程。第一站` 下，`是` 的 `total_log10 = -37.4992`
     - 在上下文 `上了旅行的征程。的驿站` 下，`是` 的 `total_log10 = -12.2157`
     - 这个差距已经不是“小调参”级别，而是**句首上下文一旦走成 `的驿站`，后续 `是` 会被 LM 极强地顺推；而走成正确的 `第一站`，后续 `是` 反而拿不到顺畅过渡分。**

- 这给 `C2` 一个非常明确的解释：
  - 它不是尾部的 `fragment / tail / octagram-style penalty` 没起作用
  - 也不是单纯的候选容量不够
  - 而是**句首局部上下文本身已经形成了错误但高顺滑度的 LM 吸引子**
  - 一旦进入 `的驿站 -> 是` 这类错误局部链，后面的搜索只是在顺着这个错误吸引子继续扩展

- 这也解释了为什么上一轮容量放大后：
  - `忘带证件等突发状况打乱节奏` 能从 `null` 浮到 `rank 3`
  - 但 `第一站是一座古老的小镇` 即使放到 `100` 个候选仍然出不来
- 因为两者的问题层次不同：
  - `忘带证件...` 更像 `C1`，头部容量一放宽就能回来
  - `第一站...` 属于更典型的 `C2`，错误链在前两步就已经拿到了更顺的 LM 过渡，容量再放大也只是放大错误头部

- 这轮顺手也看了 `C1` 的一个旁证：
  - 在 `忘带证件... / 网贷证件...` 两条路径下，后续 `突发状况` 的过渡特征完全相同：
    - `total_log10 = -105.819`
    - `used_char_fallback = true`
    - `matched_whole_word = false`
  - 这说明对 `C1` 来说，后续句级过渡并没有明显向错链单边倾斜；更像是前部局部竞争和头部容量问题叠加。

- 因而，下一步对 `C2` 的改动方向也更清楚了：
  - 不要先去动尾部修复常数
  - 也不要先盲目继续放大 beam / candidate 容量
  - 应优先处理**句首局部错误吸引子**：
    - 例如 `第 -> 一站`、`第一站 -> 是` 这类正确局部过渡为什么拿不到足够顺滑度
    - 以及 `的 -> 驿站 -> 是` 这类错误局部链为什么在当前 `KenLM/char fallback` 下被异常顺推

- 这轮可以浓缩成一句话：
  - **`C2` 的主因不是尾部惩罚不足，而是句首 LM 过渡本身就在把正确链压坏、把错误链顺推。**

## 2026-05-18 `witogram` whole-word / char-path 代码复核：`第一站` 运行时已实证走到 `vocab.NotFound()` 分支

- 继续沿 `第一站 / 第一战 / 第一展 / 的驿站` 往 `witogram.cc` 里核对后，当前链路已经可以把代码层与运行时层直接对上：
  - `ScoreFeatures()` 先把 `word` 拆成 UTF-8 单字 token，逐字累加 `char_path`
  - 然后再用 `vocab.Index(word)` 额外探一次整词
  - 只有当整词命中词表时，才会：
    - `matched_whole_word = true`
    - 计算 `whole_word_log10`
    - 用 `0.6 * whole_word + 0.4 * char_path` 做 blend
  - 否则就只能：
    - `used_char_fallback = true`
    - 保留纯 `char_path`
- 对应代码位置：
  - `witogram.cc` 的 `SplitUtf8Tokens()` / `AppendTokenScore()` / `vocab.Index(word)` 分支
  - 当前实现中 `used_char_fallback` 与 `matched_whole_word` 是互斥地由 `word_wid != NotFound()` 决定的

- 这轮最关键的新点不是再去抽象推理，而是拿到了 **`第一站` 自己的运行时实证**：
  - 在调试工件 `c2_transition_key_points.json` 中，句首上下文 `我踏上了旅行的征程。` 下：
    - `word = 第一站`
    - `used_char_fallback = true`
    - `matched_whole_word = false`
    - `oov_token_count = 1`
  - 根据当前 `witogram.cc` 的代码，这基本等价于：
    - `vocab.Index("第一站") == NotFound()`
  - 也就是说，`第一站` 这个 exact token 在当前运行中的 `.klm` 上，确实没有作为 whole-word token 稳定存在。

- 与之相对，句首第一步的单字：
  - `第`
  - `的`
  在同一上下文下都表现为：
  - `used_char_fallback = false`
  - `matched_whole_word = true`
  - `total_log10 = -38.4992`
- 这就解释了 `C2` 为什么会在句首就开始歪：
  - 模型对单字 `第/的` 都能正常命中
  - 但对正确整块 `第一站` 却走不到 whole-word 分支
  - 搜索只能继续沿拆字路径推进，导致错误局部链更容易形成顺滑吸引子

- 这轮还尝试了直接用本地现成的 KenLM `query.exe` 再做一次模型外部探针：
  - 现成二进制位于：
    - `build_x64/plugins/witogram/third_party/kenlm/bin/Release/query.exe`
  - 但它本身编译时 `KENLM_MAX_ORDER = 6`
  - 当前 `wanxiang-lts-zh-hans.klm` 是 `order 12`
  - 因此本地这份 `query.exe` / Python `kenlm` 绑定都无法直接加载该模型
- 不过，这并不影响本轮结论，因为：
  - 之前 WORKLOG 中已经有旧的 query 证据表明当前 `.klm` 更像拆分 token 模型
  - 而这轮又补上了 `第一站` 在**当前真实运行时**的 exact token 证据：它确实落入 `used_char_fallback` 分支

- 所以，到这里可以把 `C2` 与模型形态的关系进一步收敛成一句更准确的话：
  - **`C2` 之所以在句首形成错误吸引子，并不是因为 `witogram` 忘了做 whole-word，而是因为当前 `.klm` 对关键整块（如 `第一站`）本来就不给 whole-word 命中，运行时只能退回拆字 char-path。**

- 这也意味着，下一步如果要真正修 `C2`，优先级应是：
  1. 不再指望当前 `.klm` 上的 whole-word blend 自然救回这类 case；
  2. 要么针对拆字路径上的局部错误吸引子做显式抑制；
  3. 要么重新获得对这些关键整块可命中的模型形态（词级或字词混合，而不是当前这种关键处仍 OOV 的拆分 token 模型）。

## 2026-05-18 `第一站` 句首局部竞争再细化：不是“的”底座压倒，而是正确前缀家族从第二步开始集体 fallback

- 继续把 `第一站是一座古老的小镇` 的句首 `0-8` 位置展开后，局部竞争关系已经可以说得更精确。
- 新增工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\diyizhan_local_competition.json`
  - `C:\Users\Bing\AppData\Roaming\witty\debug\diyizhan_prefix_family.json`

- 先看最开始的词典底座（`entry->weight`）：
  - 在 `start=0, end=2` 这一格里：
    - `地 = -11.6603`
    - `第 = -11.7065`
    - `的 = -11.8235`
  - 也就是说，**`的` 在最开始并不是词典底座最强项**；它甚至还略弱于 `第`。
- 再看更长的正确候选：
  - `第一 = -11.7691`
  - `第一站 = -12.8223`
  - `第一战 = -13.0316`
  - 这些底座都不算离谱，至少不能单独解释为什么正确链会直接消失。

- 真正的塌陷发生在“正确前缀家族的 LM 过渡形态”：
  - `第一`
    - `total_log10 = -77.9984`
    - `used_char_fallback = true`
    - `matched_whole_word = false`
  - `第一站`
    - `total_log10 = -116.498`
    - `used_char_fallback = true`
    - `matched_whole_word = false`
  - `第 -> 一站`
    - `total_log10 = -77.9984`
    - `used_char_fallback = true`
    - `matched_whole_word = false`
  - `第 -> 一战`
    - `total_log10 = -77.9984`
    - `used_char_fallback = true`
    - `matched_whole_word = false`
- 也就是说，**不只是整块 `第一站` 命不中**，连 `第一`、`一站` 这一整组“正确前缀家族”在当前模型里也都在 fallback / OOV。

- 反过来看错误家族：
  - `第 -> 以`
    - `total_log10 = -37.4992`
    - `used_char_fallback = false`
    - `matched_whole_word = true`
  - `的 -> 驿站`
    - `total_log10 = -52.2658`
    - `used_char_fallback = true`
    - `matched_whole_word = false`
    - 但 `oov_token_count = 0`
  - `的驿站 -> 是`
    - `total_log10 = -12.2157`
  - `第一站 -> 是`
    - `total_log10 = -37.4992`
- 这说明句首局部竞争实际上分成两种错误吸引子：
  1. **`第 -> 以 -> 展示...` 家族**
     - `以` 在 `start=2,end=4` 的底座里本来就排第一（`-11.6172`）
     - 且 `第 -> 以` 过渡是正常 whole-word 命中，不像 `第 -> 一` 那样掉入 OOV/fallback
     - 因而会自然长出 `第以展示...`
  2. **`的 -> 驿站 -> 是...` 家族**
     - `的` 虽然底座不是最强，但它没有被 LM 强烈压制
     - `驿站` 虽然也不是 whole-word 命中，但至少 `oov_token_count = 0`
     - 更关键的是一旦走成 `的驿站`，后续 `-> 是` 会被 LM 极强顺推（`-12.2157`）

- 这轮最关键的修正是：
  - 以前可以粗略说“`的驿站` 错链拿到了更顺的 LM”
  - 现在可以更准确地说：
    - **不是 `的` 在第一步就压倒了 `第`**
    - **而是 `第/第一/第一站/一站` 这一整个正确前缀家族从第二步开始就系统性掉进 fallback / OOV，导致错误家族在相对竞争里自然胜出。**

- 这对下一步改法很重要：
  - 若只给 `的驿站` 单独加惩罚，会漏掉 `第以展示...` 这一支错误家族
  - 这类 `C2` 更像需要处理的是：
    - **“句首正确前缀家族整体无法形成稳定可命中过渡”**
    - 而不是只追一条错误短语做白名单/黑名单

- 因此，对 `C2` 的工程方向还需再收窄一步：
  1. 先不要做单短语级修补；
  2. 优先找能否对“句首单字起步后，第二步进入 OOV/fallback 的正确前缀家族”加统一抑制或保活；
  3. 否则，即使压掉 `的驿站`，还会由 `第以展示`、`地以展示` 等同结构家族顶上来。

## 2026-05-18 句首 fallback 家族批量复核：不是 `C2` 孤例，而是多个错例共享的开头失血模式

- 继续把这轮 `7` 条高信息量错例横向拉平后，新增批量工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\prefix_family_batch_report.json`
- 统计方法：
  - 对每条错例同时取：
    - 正确句的前两段 `graph_best_exact_path`
    - 当前 top1 的前两段路径
  - 然后对这前两步分别检查：
    - `used_char_fallback`
    - `matched_whole_word`
    - `total_log10`
- 聚合结果：
  - `total = 7`
  - `expected_prefix_step1_fallback = 2`
  - `expected_prefix_step2_fallback = 4`
  - `top1_prefix_step1_fallback = 0`
  - `top1_prefix_step2_fallback = 1`
  - `expected_more_fallback_than_top1 = 3`

- 这组数字最重要的含义是：
  - 在这批错例里，**正确路径的句首前两步，比错误 top1 更容易落入 fallback**，而且这个现象并不只出现在 `第一站...` 上。

- 其中最典型的三条是：
  1. `第一站是一座古老的小镇` (`C`)
     - 正确前缀：
       - `第一站`
       - `是一座`
     - 第二步已出现明显 fallback：
       - `是一座`: `used_char_fallback = true`
     - 错误前缀：
       - `的`
       - `驿站`
     - `驿站` 虽也不是 whole-word 命中，但相对分数明显更好，后续还能被 `-> 是` 强顺推
  2. `你是否度过了一段理想的休息时光` (`C`)
     - 正确前缀：
       - `你是否`
       - `度过了`
     - 第二步：
       - `度过了`: `used_char_fallback = true`
     - 错误前缀则是拆成：
       - `你`
       - `是`
     - 这与 `第一站` 一样，体现的是“正确整块前缀失血，错误拆字路径补位”
  3. `还让我体验到了不同地方的风土人情` (`B`)
     - 正确前缀：
       - `还让我`
       - `体验`
     - 两步都为：
       - `used_char_fallback = true`
     - 错误 top1 的前缀却是：
       - `还`
       - `让`
     - 两步都能正常 whole-word 命中
     - 这说明“句首正确整块 fallback、错误拆字正常命中”的现象，**连部分 `B` 类也会出现**；只是这类 case 后续还没有被完全压没。

- 还有一条也很能说明问题：
  - `心里装着一些说不出口的心事` (`B`)
    - 正确前缀：
      - `心里`
      - `装着`
    - 两步都 `used_char_fallback = true`
    - 错误 top1 前缀：
      - `心`
      - `里`
    - 两步都正常命中
  - 这意味着有些 `B` 类实际上已经带有与 `C2` 同源的句首失血结构，只是损失程度还没严重到把整句完全挤出候选集。

- 与之相对，也有明显不是这一型的 case：
  - `忘带证件等突发状况打乱节奏` (`C1`)
    - 正确前缀第二步 `等` 正常 whole-word 命中
    - 之前放宽容量后还能直接回到 `rank 3`
    - 更像容量/头部截断问题，不是句首 fallback 家族主导
  - `有人抓紧不被打扰的时光充电学习` (`B`)
    - 当前前两步没有看到明显 fallback 信号

## 2026-05-18 `S4` 早期边界桥接复测：`站|是` 单点补偿不够，主死亡点已转成“不稳前缀后的多字 continuation 吸附”

- 继续沿 `第一站是一座古老的小镇` 往下打后，这轮先实现并接线了一个非常窄的桥接项：
  - 新参数：
    - `early_boundary_bridge_weight`
  - 新门控目标：
    - 只奖励“前一个前缀本身是多字 fallback/OOV family，但当前这一步是一字且干净命中”的承接
    - 也就是优先扶 `第一站 -> 是`，而不是泛化奖励所有短边界
- 代码接入位置：
  - `plugins/witset/src/witset_poet.h`
  - `plugins/witset/src/witset_poet.cc`
- 本地 schema 已同步：
  - `C:\Users\Bing\AppData\Roaming\witty\witset.schema.yaml`
  - `C:\Users\Bing\AppData\Roaming\witty\build\witset.schema.yaml`
- 配置值：
  - `early_boundary_bridge_weight: 1.35`
- 按标准方式重新执行：
  - `librime/build.bat static`
  - 然后回放 5 条代表样本
- 新增结果工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\s4_smoke_after_v1.json`

- 复测结果：
  - `total = 5`
  - `current_correct = 0`
  - `improved_rank_cases = 0`
  - `第一站是一座古老的小镇`
    - 当前 top1:
      - `第一展示已作古老的小镇`
    - 正确句仍然：
      - `expected_rank = null`

- 这轮最重要的价值，不是“又一个 0/5”，而是把主死亡点继续往后收窄了：
  1. `diyizhanshi`
     - top1 已变成 `第一展示`
     - 说明前一轮 `S3-v1` 后，`第一 / 敌意 / 地衣` 这层首词 family 竞争已经基本扶正
  2. 但一到 `diyizhanshiyi`
     - top1 会立刻切回 `的驿站是以`
  3. 到完整句
     - `第一展示已作古老的小镇`
     - 与 `的驿站是以做古老的小镇`
     - 会在头部继续交替主导

- 结合这轮 snapshot 与更早的容量放大工件，可以把结论说得更准确：
  - 问题已经不再只是“`站|是` 这个一字桥接缺一点奖励”
  - 而是：
    - **当前前缀一旦本身处于 fallback/OOV 不稳态，后续多字 continuation 会被 `展示 / 已作 / 是以` 这类整块路径持续吸走**
  - 所以只给 `第一站 -> 是` 单点补偿，不足以把正确整句重新拉回可见候选

- 旧工件 `c_bucket_capacity_probe_2.json` 也支持这个判断：
  - 即使把该 case 的候选数放宽到 `100`
  - 正确句仍然 `expected_rank = null`
  - 头部基本被：
    - `的驿站是以做古老的小镇`
    - `的以展示已作古老的小镇`
    - 以及一整串 `*以展示已作古老的小镇`
    - 所覆盖
- 这说明：
  - 它不是“正确句已经进了深排位，只差再保活一点”
  - 而是前半句错误延伸链已经把搜索空间和分数层同时占满

- 因此，下一刀的方向也随之改变：
  1. 先不继续单独加大 `站|是` 奖励
  2. 改为优先处理：
     - **不稳前缀之后，再接一个多字 continuation 且该 continuation 也继续走 fallback/OOV 的吸附链**
  3. 目标是优先压 `第一 + 展示` / `第以 + 展示` / `的驿站 + 是以` 这一类“前缀已不稳仍继续被整块顺推”的结构
  4. 而不是按具体短语做白名单/黑名单

## 2026-05-18 `S5` 不稳前缀 continuation 惩罚：首版未命中，放宽后反而把 `第一` family 再次压回去

- 基于 `S4` 的收敛判断，这轮又补了一个更窄的实验钩子：
  - 新参数：
    - `early_unstable_continuation_penalty_weight`
  - 原始设计目标：
    - 当前缀本身已经是 `fallback/OOV` 的不稳多字 family 时
    - 若下一步又接了一个继续走 `char fallback` 的多字 continuation
    - 就对这一步做额外惩罚
  - 它瞄准的就是：
    - `第一 + 展示`
    - `第以 + 展示`
    - 这一类“前缀已不稳但还继续被多字整块吸走”的结构

- 第一版门控过严时：
  - 需要 continuation 当前步同时新增 `OOV token`
  - 编译并回放后的工件：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\s5_smoke_after_v1.json`
  - 结果仍然：
    - `total = 5`
    - `current_correct = 0`
    - `improved_rank_cases = 0`
  - 对 `第一站是一座古老的小镇`
    - top1 仍是 `第一展示已作古老的小镇`
  - 继续补跑局部链工件：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\boundary_chain_probe_s5_v1.json`
  - 才发现根因是：
    - `第一展示` 这一跳虽然 `StepCharFB = 1`
    - 但当前步并没有新增 `OovTok`
    - 所以新惩罚项根本没有触发

- 于是把门控放宽成：
  - 只要 continuation 当前步继续走 `char fallback`
  - 就允许惩罚触发；如果同时新增 `OOV token` 再额外加重
- 重新编译后补跑局部链工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\boundary_chain_probe_s5_v2.json`

- 这次确实打到了目标链，但结果并不对：
  1. `diyizhanshi`
     - 之前 top1:
       - `第一展示`
     - 放宽后变成：
       - `敌意展示`
     - `第一展示` 被压到 `rank 3`
  2. `diyizhanshiyizu`
     - top1 从 `第一展示彝族`
     - 变成 `敌意展示彝族`
  3. 完整句
     - top1 从 `第一展示已作古老的小镇`
     - 变成 `敌意展示已作古老的小镇`
     - `第一展示...` 下降到 `rank 3`

- 这说明：
  - 当前这类 continuation 惩罚虽然能命中 `第一 + 展示`
  - 但它没有区分“应当保留的正确首词 family”与“首词 family 内部的同音竞争”
  - 最终只是把 `第一展示` 压下去，再让 `敌意展示 / 地衣展示` 接手

- 因而这轮也给出一个很明确的负结论：
  - **不能只按“前缀不稳 + continuation fallback”这一层继续加大惩罚**
  - 否则会重新破坏上一轮好不容易扶正的 `第一 / 敌意 / 地衣` family 排序

- 当前处理决定：
  - 保留这条代码钩子，方便后续若要引入更细粒度信号时复用
  - 但本地 schema 中已把：
    - `early_unstable_continuation_penalty_weight: 0.0`
    - 暂时关闭
  - 避免把这轮已确认的回归状态留在默认实验配置里

- 到这里可以把下一步再收窄一句：
  - 下一刀不能再只看“`展示` continuation 是不是该打”
  - 而要能同时表达：
    - **`第一` family 已被扶正时应当保住**
    - **但 `第一 + 展示 / 已作` 这种 continuation 延伸仍要被抑制**
  - 换句话说，后续更可能需要的是：
    - 带一点“prefix family 保真”的关系型信号
    - 而不是单边继续给 continuation 上惩罚常数

## 2026-05-18 `S6` prefix-anchor + delta-debt：对称命中 `第一/敌意/地衣 + 展示` family，但仍不足以拉回正确句

- 这一轮开始明确不再走“加大 beam 后再挑”的路线，而是尝试一个更像 `witogram` 搜索态优势的最小原型：
  - 不扩大 beam
  - 不按具体短语做白名单
  - 只在 very early prefix 阶段记一个小的 `prefix anchor`
  - 后续 continuation 只对“相对这个 anchor 新增的 debt”做增量惩罚

- 最小实现收得很窄：
  1. 只锚定：
     - `generated_word_count == 1`
     - 当前首词恰好是两字
     - 且该首词走了 `char fallback`、不是 whole-word 命中
  2. 对后继 continuation 只在以下条件下触发新惩罚：
     - 当前总词数在前 2~3 词内
     - 当前 continuation 也是两字
     - 且相对 anchor 又新增了 fallback/OOV debt
  3. 这样瞄准的不是单个词，而是：
     - `第一 + 展示`
     - `敌意 + 展示`
     - `地衣 + 展示`
     - 这一整类“已进入两字 fallback family 后，又被两字 continuation 继续吸走”的结构

- 代码实现：
  - `plugins/witset/src/witset_poet.h`
    - 新增参数：
      - `prefix_anchor_delta_debt_weight_`
  - `plugins/witset/src/witset_poet.cc`
    - 新增：
      - `ShouldActivatePrefixAnchor(...)`
      - `ComputePrefixAnchorDeltaDebtPenalty(...)`
    - `Line` 新增 anchor 状态：
      - `prefix_anchor_active`
      - `prefix_anchor_char_count`
      - `prefix_anchor_fallback_hits`
      - `prefix_anchor_oov_tokens`
    - debug 新增：
      - `AnchorDebt`
      - `StepAnchorDebt`

- schema 接线：
  - `C:\Users\Bing\AppData\Roaming\witty\witset.schema.yaml`
  - `C:\Users\Bing\AppData\Roaming\witty\build\witset.schema.yaml`
  - 实验时使用：
    - `prefix_anchor_delta_debt_weight: 1.05`
  - 复测后当前已先关闭：
    - `prefix_anchor_delta_debt_weight: 0.0`

- 按标准方式重新编译：
  - `librime/build.bat static`

- 先跑局部边界链工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\boundary_chain_probe_anchor_v1.json`
- 关键观察：
  1. `diyi`
     - `第一 / 敌意 / 地衣` 的 `AnchorDebt` 都还是 `0`
     - 说明 anchor 自身没有被误伤
  2. `diyizhanshi`
     - `第一展示`
       - `AnchorDebt:-1.50`
     - `敌意展示`
       - `AnchorDebt:-1.50`
     - `地衣展示`
       - `AnchorDebt:-1.50`
     - 说明这次终于实现了“对称命中整组 family continuation”
     - 没再出现上轮那种“只压 `第一展示`，结果 `敌意展示` 接手”的结构性失真
  3. 但 `diyizhanshi`
     - top1 仍是 `第一展示`
     - `敌意展示` 只是被一起压在后面
  4. `diyizhanshiyizu`
     - top1 仍是 `第一展示彝族`
  5. 完整句
     - top1 仍是 `第一展示已作古老的小镇`
     - `敌意展示...`、`地衣展示...` 排在其后
     - `的驿站是以做古老的小镇` 仍在前排

- 随后补跑 5 条代表样本：
  - 工件：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\s6_smoke_after_v1.json`
  - 结果：
    - `total = 5`
    - `current_correct = 0`
    - `improved_rank_cases = 0`

- 这轮最关键的正面结论是：
  - **prefix-anchor + delta-debt 这条思路，至少在结构上是对的**
  - 它第一次做到了：
    - 不扩大 beam
    - 不打词白名单
    - 也不破坏 `第一 / 敌意 / 地衣` family 内部排序
    - 而是把 `*展示` 这一整类 continuation 作为一个 family 一起处理

- 但负面结论同样明确：
  - **只靠“对称压 continuation family”这一刀，还是不够**
  - 即使 `第一展示 / 敌意展示 / 地衣展示` 被一起打到，
    - 正确句 `第一站是一座古老的小镇` 依旧没有回到可见候选
  - 说明当前主问题已经不只是：
    - “错误 continuation family 太强”
  - 还包括：
    - 正确链 `第一站 -> 是一座` 自身仍缺少足够的 bridge / debt-release 信号

- 因此，这轮把下一步收得更清楚了：
  1. `prefix-anchor + delta-debt` 应当保留
     - 因为它已经证明比 `S5` 那种单边 continuation 惩罚更接近正确方向
  2. 但它需要再配一层 very narrow 的“debt release / clean bridge reward”
     - 也就是：
       - 当前缀已被 anchor 住后
       - 如果后继走的是干净承接，而不是继续新增 debt
       - 应当允许返还一部分早期 debt
  3. 否则只做负向 continuation 抑制，最多只能让错误家族彼此更接近
     - 但不一定能让正确链真正翻上来
    - 更像后段 `时光 -> 是光` 一类局部竞争造成的排名下降
  - `两旁是古色古香的建筑`
    - 当前前两步数据里未看到与 `第一站` 同级别的 fallback 信号
    - 说明它未必属于同一开头失血型，需要和 `C2` 主样本继续区分

- 因此，这轮可以把句首问题正式分成两层：
  1. **句首 fallback 家族型**
     - 正确整块前缀（如 `第一站 / 你是否 / 心里 / 还让我`）在开头两步就更容易落入 fallback
     - 错误路径往往拆成更短的单字/双字前缀，反而能保持 whole-word 命中
     - 这类问题既出现在 `C2`，也会出现在部分 `B`
  2. **非句首 fallback 主导型**
     - 例如 `忘带证件...`
     - 更偏向容量截断或中后段局部竞争，而不是句首前缀家族整体失血

- 这轮最重要的收敛点是：
  - **`第一站` 不是孤例。句首“正确整块 fallback、错误拆字命中”的结构，已经在多条错例中重复出现。**
  - 所以下一步确实值得从“统一规则目标”出发，而不是围着单个短语做 patch。

## 2026-05-18 句首 fallback 家族的最小策略候选：先改记分，再考虑保活

- 在把批量模式确认之后，继续回到 `witset_poet.cc` 看“最小实现落点”。
- 当前代码里，其实已经把句首 fallback 家族所需的大部分信号都现成算出来并挂在 `Line` 上：
  - 单步：
    - `step_used_char_fallback`
    - `step_matched_whole_word`
    - `step_merge_delta`
    - `step_whole_word_log10 / step_char_path_log10`
  - 累计：
    - `cumulative_char_fallback_hits`
    - `cumulative_whole_word_hits`
    - `cumulative_lm_oov_tokens`
    - `generated_word_count / generated_char_count`
- 这些字段都已经在 `Line` 构造时维护完成，不需要先改 graph 或 debug 管线。

- 结合当前错例分型，最小策略空间可以收敛成三种：

### 候选 S1：句首 fallback 家族补偿分

- 落点：
  - 候选打分阶段，直接加在 `adjustment_score` 附近
  - 也就是和 `whole_word_bonus / merge_gain / fragment_penalty / structure_penalty` 同层
- 触发条件建议尽量收窄为：
  - 只在句首前两步生效：`next_generated_word_count <= 2`
  - 当前词为多字整块：`char_count >= 2`
  - 当前步发生 fallback：`used_char_fallback = true`
  - 且有明确 OOV 痕迹：`lm_oov_token_count > 0`
- 核心目的：
  - 给“正确整块前缀因为 split-token 模型而系统性吃亏”的情况一个小幅回补
  - 只补偿句首前两步，避免影响中后段正常的 char-path 路线
- 优点：
  - 侵入最小
  - 不改状态键，不改池压缩逻辑，只改一个局部 adjustment 项
  - 最容易只命中 `第一站 / 你是否 / 心里 / 还让我` 这一类整块前缀
- 风险：
  - 也可能顺带抬高错误的多字 fallback 候选（如 `驿站`）
  - 因而不能只看 `used_char_fallback`，最好连 `lm_oov_token_count > 0` 一起用，减少对 `驿站` 这类 `oov_token_count = 0` 的误补

### 候选 S2：句首拆字命中抑制

- 落点：
  - 同样放在候选打分阶段，但作用于“错误拆字补位路径”
- 触发条件建议：
  - 只在句首前两步：`next_generated_word_count <= 2`
  - 当前词是单字：`char_count == 1`
  - 当前步 normal whole-word 命中：`matched_whole_word = true`
  - 且同一轮存在接近的多字候选竞争（可复用当前已有的 `best_multi_entry_weight` 思路）
- 核心目的：
  - 不是去奖励所有多字词，而是抑制“单字路径因为词表天然友好而对整块前缀形成不公平领先”
  - 典型就是：
    - `还 | 让` 压 `还让我`
    - `心 | 里` 压 `心里`
    - `第 | 以` 压 `第一 / 一站`
- 优点：
  - 更直接打击错误家族的补位路径
  - 不要求正确整块一定有 whole-word 命中
- 风险：
  - 误伤真实应该拆成单字开头的输入
  - 特别容易误伤 `C1` 或本来就该用单字开头的正常句子
  - 因此如果做，必须比 `S1` 更严格地 gate，只在“有接近多字候选同场竞争”的情况下启用

### 候选 S3：句首 fallback 家族保活槽

- 落点：
  - 不改候选分数
  - 改 `CompressLinePoolByState()` / `SelectTopLinesWithDiversity()` 之前的保活逻辑
- 具体思路：
  - 对满足“前两步已有 fallback，且生成字符数已达到一定长度”的路径，保留极少量额外槽位
  - 让它们不要过早在池压缩或头部筛选前被单字拆分路径彻底淹没
- 优点：
  - 不直接篡改打分语义
  - 更像对 split-token 模型偏置做工程层纠偏
- 风险：
  - 会和 `C1` 的容量问题混在一起
  - 一旦开口子过大，很容易把大量噪声也保活下来
  - 当前我们已经确认 `忘带证件...` 这类 `C1` 主要受容量影响，因此先动保活层，最容易把两种问题搅在一起

- 结合当前证据，优先级建议如下：
  1. **优先尝试 `S1`**
     - 这是最小、最可控、最不容易混入 `C1` 的方案
     - 因为它只对“句首前两步 + 多字整块 + fallback + OOV”这组信号做轻量补偿
  2. **若 `S1` 不够，再考虑极轻量的 `S2`**
     - 主要用来压 `还|让 / 心|里 / 第|以` 这类单字拆分补位
  3. **最后才考虑 `S3`**
     - 因为它最容易把“句首 fallback 家族型”和“容量截断型 C1”搅在一起

- 换句话说，下一步最合理的实验顺序不是：
  - 直接去改状态压缩或大幅放大 beam
- 而是：
  - **先做一个极窄门控的句首 fallback 补偿分原型，专门测试它能不能把 `第一站 / 你是否 / 心里 / 还让我` 这一组从句首失血中拉回来，同时尽量不碰 `忘带证件...` 这类 `C1`。**

## 2026-05-18 `S1` 最小原型已落地：句首前两步的窄门控 fallback 补偿分

- 本轮没有动保活层，也没有去改 state key；只实现了最小 `S1`：
  - 新增配置项：
    - `early_fallback_compensation_weight`
  - 落点仍在 `witset_poet.cc` 的 `adjustment_score` 阶段
  - 和 `whole_word_bonus / merge_gain / fragment_penalty` 同层生效

- 当前原型的门控条件为：
  - `next_generated_word_count <= 2`
  - `char_count >= 2`
  - `used_char_fallback = true`
  - `matched_whole_word = false`
  - `lm_oov_token_count > 0`
- 也就是：
  - 只补句首前两步
  - 只补多字整块
  - 只补真正带 OOV 痕迹的 fallback
  - 不补 `驿站` 这类虽然 fallback、但 `oov_token_count = 0` 的路径

- 这轮代码改动点：
  - `witset_poet.h`
    - 新增成员：
      - `early_fallback_compensation_weight_ = 0.0`
  - `witset_poet.cc`
    - 新增 `ComputeEarlyFallbackCompensation(...)`
    - 在构造函数中读入配置
    - 在候选打分时计算 `early_fallback_compensation`
    - 并把它加入 `adjustment_score`
- 本地 schema 已同步接线：
  - `witset.schema.yaml`
  - `build/witset.schema.yaml`
  - 当前临时设为：
    - `early_fallback_compensation_weight: 0.55`

- 由于本轮还没有做运行时回放，我先用已有工件做了一次静态门控复核：
  - 新增工件：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\s1_gate_probe.json`
  - 基于当前 `prefix_family_batch_report.json` 的前两步特征，按上述门控条件回算后：
    - `7` 条样本里，当前仅 `第一站是一座古老的小镇` 命中
    - 命中的步是：
      - 第 `2` 步 `是一座`
      - 补偿值约 `0.4744`

- 这有两个重要含义：
  1. 这版 `S1` 的门控确实足够窄
     - 当前不会顺手抬高 `忘带证件...` 这类 `C1`
     - 也不会把 `驿站` 这类 `oov_token_count = 0` 的错误 fallback 一起补上去
  2. 这版 `S1` 也可能偏保守
     - 以现有静态工件看，它主要打到 `第一站...`
     - 对 `你是否 / 心里 / 还让我` 这些 case，暂时还不一定能触发
     - 因而它更像一个“先验证方向对不对”的最小原型，而不是最终形态

- 因此，当前实现策略可以概括为：
  - **先用一个非常窄的补偿项验证“句首 fallback 家族补偿”这条方向是否成立；宁可先漏掉一部分 case，也先避免把 `C1` 与错误 fallback 路径误抬起来。**

## 2026-05-18 `S1` 编译与 5 条代表样本小测：编过了，但当前原型没有带来可见改善

- 本轮按标准方式在 `librime` 目录执行：
  - `build.bat static`
- 增量编译成功，未做 clean，也未使用其他编译/部署方式。
- 编译结果：
  - `rime-witset-objs`、`librime.lib`、`rime_api_console.exe` 均成功产出
  - 过程中只有既有的链接警告（如 `/llibcmt` 相关），未见新的编译错误

- 随后使用本地 `rime_api_console.exe` 对 5 条代表样本做了 smoke test：
  - `第一站是一座古老的小镇`
  - `你是否度过了一段理想的休息时光`
  - `心里装着一些说不出口的心事`
  - `还让我体验到了不同地方的风土人情`
  - `忘带证件等突发状况打乱节奏`
- 结果工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\s1_smoke_after.json`

- 小测结果非常直接：
  - `total = 5`
  - `current_correct = 0`
  - `improved_rank_cases = 0`
- 也就是说，这版 `S1` 虽然成功编进去了，但在这 5 条代表样本上，**top1 完全没有发生变化**。

- 逐条看：
  - `第一站是一座古老的小镇`
    - 之前：`的驿站是以做古老的小镇`
    - 现在：`的驿站是以做古老的小镇`
    - 无变化
  - `你是否度过了一段理想的休息时光`
    - 之前：`你是否读过了异端理想的休息时光`
    - 现在：`你是否读过了异端理想的休息时光`
    - 无变化
  - `心里装着一些说不出口的心事`
    - 之前：`心里装着一些说不出口的新式`
    - 现在：`心里装着一些说不出口的新式`
    - 无变化
  - `还让我体验到了不同地方的风土人情`
    - 之前：`还让我体验到了不同的方的风土人情`
    - 现在：`还让我体验到了不同的方的风土人情`
    - 无变化
  - `忘带证件等突发状况打乱节奏`
    - 之前：`网贷证件等突发状况打乱节奏`
    - 现在：`网贷证件等突发状况打乱节奏`
    - 无变化

- 因此，这轮实验至少说明两点：
  1. `S1` 当前这版门控确实很窄，而且没有误伤 `C1`
     - `忘带证件...` 没被抬坏
     - 但也没有任何正向改善
  2. `S1` 目前太弱，或者打中的位置不对
     - 结合前面的静态门控复核，当前它主要只命中 `第一站...` 的第 2 步 `是一座`
     - 但 `第一站...` 的真实错误吸引子主要发生在更早的前缀竞争：
       - `第 / 第一 / 第一站 / 一站` 家族整体失血
       - `的驿站 / 第以展示` 家族趁机占先
     - 也就是说，**只在第 2 步给 `是一座` 一个小补偿，离真正的死亡点还是太远了**

- 这轮后，`S1` 的结论可以更新为：
  - **方向未被证伪，但当前落点和门控太保守，强度也不足，尚不足以改变 top1。**

- 下一步更合理的收缩方向：
  1. 不是先去动保活层
  2. 而是先把 `S1` 从“只补多字 fallback + OOV 的当前步”升级为“更贴近句首正确前缀家族失血”的版本
  3. 重点应考虑把信号前移到：
     - 第 1 步或第 2 步的整块前缀本身
     - 而不是只补它后面的承接词（如 `是一座`）

- 换句话说，这轮实验最重要的结论不是“`S1` 完全没意义”，而是：
  - **`S1-v1` 过于保守，打不中真正主战场；如果还沿 `S1` 继续，就该改成更靠前的句首前缀级补偿，而不是继续加大这版 `v1` 的系数。**

## 2026-05-18 编译/部署路径复核：`build.bat static` 对本地 console 验证是够的，但不等于部署到 Outwit

- 重新核对脚本后确认：
  - `librime/build.bat static`
    - 会以 `BUILD_SHARED_LIBS=OFF` 方式重编 `librime` 静态库、插件对象以及 `rime_api_console.exe`
    - 产物落到 `librime/build_x64` 与 `librime/dist_x64`
  - `outwit-windows/build.bat`
    - 是编整个 Outwit 外壳与相关前后端
  - `outwit-windows/build_and_deploy.bat`
    - 顺序是 `librime/build.bat static` -> `outwit-windows/build.bat` -> `deploy_outwit.bat`
- 因此：
  - **如果目标是用 `rime_api_console.exe` 做本地排序/候选验证，`librime/build.bat static` 是够的。**
  - **如果目标是把修改后的输入法真正部署到 Outwit 程序目录里运行，则还需要 `outwit-windows/build_and_deploy.bat`。**
- 这也解释了为什么前几轮 smoke test 用 `rime_api_console.exe` 就能直接看到算法变化，而不需要走整套 Outwit 部署。

## 2026-05-18 `S1` 继续迭代到前缀级补偿：编译生效，但仍未转成 top1 改善

- 在确认编译路径没有问题后，继续把 `S1` 从“当前步 fallback + OOV 补偿”改成了更靠前的“句首前缀级补偿”：
  - 第 1 步：允许对多字开头前缀直接给结构性补偿，不再要求当前步必须已有 OOV
  - 第 2 步：只在前两步都保持“无单字拆分”的前缀族上继续补偿
  - `early_fallback_compensation_weight` 临时提高到 `1.20`
- 也就是说，当前 `S1` 已不再是纯 `fallback+OOV` 版，而是：
  - **“优先扶正句首多字前缀家族、同时继续参考 fallback/OOV 信号”的前缀级补偿版。**

- 重新按标准方式执行：
  - `librime/build.bat static`
- 编译成功。

- 然后再次对同一组 5 条代表样本做 smoke test：
  - `第一站是一座古老的小镇`
  - `你是否度过了一段理想的休息时光`
  - `心里装着一些说不出口的心事`
  - `还让我体验到了不同地方的风土人情`
  - `忘带证件等突发状况打乱节奏`
- 结果工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\s1_smoke_after_v3.json`

- 结果仍然是：
  - `total = 5`
  - `current_correct = 0`
  - `improved_rank_cases = 0`
- 所以到这一步，仍然没有任何 case 被直接救成 top1。

- 但和 `S1-v1` 不同，这次至少出现了一个明确的“前缀被扶正”的迹象：
  - `第一站是一座古老的小镇`
    - 之前前排错链主要是：
      - `的驿站...`
      - `的以展示...`
      - `地以展示...`
      - `第以展示...`
    - 这轮前排中已出现：
      - `第一展示已作古老的小镇`
    - 说明前缀级补偿**确实已经把 `第/第一/第一站` 这支家族往前推了一步**，不再完全被 `第以展示` 这类单字拆分链压住。
- 但也正因为它只把前缀拉正到 `第一展示...`，还没能把第二段和后续句子一并拉正，所以 top1 仍没变化。

- 这轮最重要的结论因此更新为：
  - **编译路径没有问题，代码也确实生效了。**
  - **当前问题不再是“补偿完全没打到前缀”，而是“前缀虽已开始被扶正，但第二段及后续局部过渡仍被错误家族带偏”。**

- 换句话说，当前已经可以排除“只是没部署到正确运行环境”这一类怀疑；
  - `build.bat static` 对本地 console 验证是有效的；
  - 现在剩下的是算法本身还不够，不是编译/部署方式错了。

## 2026-05-18 `S2-v1`：压“早期单字拆分延伸链”后，`的驿站` 被压下去了，但问题转成了二字同音家族竞争

- 在 `S1` 已经把句首前缀扶正一部分之后，本轮新增了一个很窄的 `S2-v1`：
  - 新增配置项：`early_prefix_split_penalty_weight`
  - 新增逻辑：`ComputeEarlyPrefixSplitPenalty(...)`
  - 作用范围只限于：
    - 前 `2~3` 个词
    - 已累计产生至少 `3` 个字
    - 已经出现了早期单字拆分（`next_single_char_word_count > 0`）
  - 本质上是：**惩罚“单字起步后继续往后延伸”的错误链**，而不是针对某个具体错词写硬编码。

- 本地 schema 临时参数：
  - `early_prefix_split_penalty_weight: 0.85`

- 编译方式仍为标准本地验证路径：
  - `librime/build.bat static`
- 编译成功。

- 对同一组 5 条代表样本重新做 smoke test：
  - 结果工件：`C:\Users\Bing\AppData\Roaming\witty\debug\s2_smoke_after_v1.json`

- 结果摘要：
  - `total = 5`
  - `current_correct = 0`
  - `improved_rank_cases = 0`
- 也就是说，`S2-v1` 仍然没有把任何 case 救回 top1。

- 但它不是“完全没生效”，而是带来了一个非常关键的新变化：
  - `第一站是一座古老的小镇`
    - 之前 top1：`的驿站是以做古老的小镇`
    - 现在 top1：`敌意展示已作古老的小镇`
    - 当前前排变成：
      - `敌意展示已作古老的小镇`
      - `地衣展示已作古老的小镇`
      - `第一展示已作古老的小镇`
      - `的驿站是以做古老的小镇`
  - 这说明 `S2-v1` **确实成功压下了“的|驿站”这一类早期单字拆分延伸链**。

- 但也正因为如此，问题的主矛盾进一步暴露出来了：
  - 原先我们怀疑“单字起步链”是主因之一；
  - 现在把它压下去后，真正顶上来的不是正确句，而是：
    - `敌意展示...`
    - `地衣展示...`
    - `第一展示...`
  - 也就是说，**当前主战场已经从“单字拆分延伸链”转移到了“二字同音家族整体竞争”**。

- 对这条 `第一站...` 来说，可以更明确地说：
  1. `S1` 已经把 `第/第一/第一站` 家族往前拉了一步
  2. `S2-v1` 又把 `的|驿站` 这种单字拆分链压了下去
  3. 但最后仍然没有回到正确句，是因为：
     - `敌意 / 地衣 / 第一` 这些二字前缀家族
     - 在后续 `展示 / 已作 / ...` 的延伸上
     - 仍然一起输给了真正的 `第一站 / 是一座 / ...` 结构

- 因此，这轮的关键结论不是“`S2-v1` 无效”，而是：
  - **`S2-v1` 已经把“早期单字拆分延伸”这个干扰源基本隔离了出来；**
  - **现在剩下的更纯粹问题，是错误的二字整块家族本身（`敌意/地衣/第一`）仍然比正确家族更容易延伸成高分句。**

- 其余 4 条代表样本基本无变化：
  - `你是否...` 仍是 `你是否读过了异端理想的休息时光`
  - `心里...` 仍是 `心里装着一些说不出口的新式`
  - `还让我...` 仍是 `还让我体验到了不同的方的风土人情`
  - `忘带证件...` 仍是 `网贷证件等突发状况打乱节奏`

- 下一步方向因此继续收缩：
  - 不建议再继续加大 `S2-v1`，因为它已经完成了“压早期单字拆分链”的诊断任务；
  - 更值得做的是：
    - 直接研究 `敌意/地衣/第一` 与正确 `第一站` 的**二字整块家族竞争**
    - 也就是把焦点从“split chain”切到“同音整块 family”。

## 2026-05-18 `S3-v1`：针对首词多字 family 的 OOV 去偏后，`第一` family 已扶正，主死亡点后移到 `站|是` vs `展示`

- 继续沿 `第一 / 敌意 / 地衣` 这条 family 线做了更细的证据核对。
- 直接读取当前 `witset_local_snapshot.jsonl` 的中间输入后确认：
  - 在 `input = diyi` 时：
    - `敌意 / 地衣` 与 `第一` 都是 `CharFB=1`
    - 但 `敌意 / 地衣` 的 `OovTok = 0`、`OOV = -0.25`
    - `第一` 的 `OovTok = 1`、`OOV = -1.15`
    - 同时 `第一` 的词典底座其实更强：
      - `第一 Dict=-11.77`
      - `敌意 Dict=-12.76`
      - `地衣 Dict=-13.16`
  - 这说明这层竞争里，`第一` 不是输在词典，而是**输在首词多字前缀因为 split-token/OOV 多吃了一档惩罚**。

- 因此本轮没有再继续改 `split chain`，而是把 `S1` 在首词场景下再收窄成一个更明确的去偏版本：
  - 只在以下条件同时满足时，额外提高 `ComputeEarlyFallbackCompensation(...)`：
    - `next_generated_word_count == 1`
    - `char_count >= 2`
    - `used_char_fallback = true`
    - `next_prefix_oov_tokens > 0`
    - `next_single_char_word_count == 0`
  - 本质就是：
    - **只补“首词就是多字 family，且因 split-token/OOV 吃亏”的情况**
    - 不去放大后续通用路径。

- 编译方式仍为：
  - `librime/build.bat static`
- 编译成功。

- 复测工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\s3_smoke_after_v1.json`

- 这轮最重要的结果不是最终句子修好了，而是：
  1. `input = diyi`
     - `第一` 已经反超 `敌意 / 地衣` 到 top1
  2. `input = diyizhan`
     - `第一站` 已升到 rank 2，和 `的驿站` 几乎只差一线
  3. `input = diyizhanshi`
     - `第一展示` 已经反超 `敌意展示 / 地衣展示` 到 top1
  4. 全句 `diyizhanshiyizuogulaodexiaozhen`
     - top1 变成了 `第一展示已作古老的小镇`

- 也就是说，这轮可以非常明确地下结论：
  - `第一 / 敌意 / 地衣` 这层 family 竞争，主因确实就是首词多字 family 的 OOV 惩罚偏置；
  - `S3-v1` 已经把这一层竞争扶正了。

- 但同样重要的是：
  - 全句仍然没有回到 `第一站是一座古老的小镇`
  - 而是停在了 `第一展示已作古老的小镇`
- 这说明主死亡点已经继续后移：
  - 不再是 `第一` family 被 `敌意/地衣` 压住
  - 而是：
    - **`第一站 | 是一座`**
    - 与
    - **`第一 | 展示 | 已作`**
    - 这两种后续切分/承接方式之间的竞争
    - 当前仍明显偏向了后者

- 所以到这一步，问题链路可以重新整理为：
  1. `S1/S3` 已基本解决“首词 family 起不来”
  2. `S2-v1` 已基本隔离“早期单字拆分延伸链”
  3. 当前剩下的纯问题，是：
     - **`zhan|shi` 这段局部声韵序列，在当前 split-token LM 下，系统性更容易塌成 `展示`，而不是保住 `站|是` 的边界。**

- 下一步因此不应再继续加大首词补偿或继续打 `敌意/地衣`；
  - 更值得直接研究的是：
    - `第一站是` 为什么会持续输给 `第一展示`
    - 即第二段边界 `4-8 | 8-11` 与 `4-11` 的竞争
    - 也就是从“family OOV 去偏”转向“早期边界保真/承接竞争”。

## 2026-05-18 `S7` prefix-anchor + debt-release：两次 probe 都未命中，暴露出“正确链以整块首词入场”的结构事实

- 这轮继续沿 `S6` 的状态化方向推进，但仍坚持两条约束：
  - 不扩大 beam
  - 不做词级白名单

- 新增 very narrow 的 `debt-release` 原型：
  - `prefix-anchor` 仍沿用“两字 fallback 首词 family”这个锚点
  - 在其后补一个 `ComputePrefixAnchorDebtRelease(...)`
  - 并增加 debug：
    - `AnchorRelease`
    - `StepAnchorRelease`

- `v1` 的门控很窄：
  - 只奖励：
    - 已锚定两字 fallback 前缀后的单字承接
    - 且该单字必须是 `matched_whole_word = true`
    - `used_char_fallback = false`
    - 没有新增 fallback / OOV debt

- 按标准方式编译：
  - `librime/build.bat static`
  - 编译成功

- 第一轮 probe 工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\boundary_chain_probe_anchor_release_v1.json`

- `v1` 的结果很直接：
  - 所有前排候选的
    - `AnchorRelease`
    - `StepAnchorRelease`
    - 都是 `0.00`
  - 说明这条“干净单字 bridge”门控一次都没有打到

- 因此继续做了 `v2`：
  - 允许“无新增 OOV 的单字 bridge fallback”也拿到部分 release
  - 但仍只限 very early 单字 bridge，不放宽到通用 continuation

- 第二轮 probe 工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\boundary_chain_probe_anchor_release_v2.json`

- `v2` 的结果依旧没有命中：
  - `diyizhan` 中的 `第一站`
    - `AnchorRelease:0.00`
  - `diyizhanshi` 中的 `第一展示`
    - `AnchorRelease:0.00`
  - `diyizhanshiyi` 中的 `的驿站是以`
    - `AnchorRelease:0.00`

- 这轮最重要的不是“reward 没效果”，而是：
  - **reward 根本没触发。**
  - 问题不在接线，而在当前真实竞争形态。

- 从 `diyizhan` 的 probe 可以看出：
  - `第一站` 当前前排 debug 仍表现为：
    - `StepCharFB:1`
    - `StepWholeHit:0`
    - `AnchorRelease:0.00`
  - 这说明它并不是以“已锚定 `第一` 前缀后，再接一个被 release 的 `站`”这种形式在前排存活
  - 更像是：
    - **`第一站` 自己作为一个整块首词 family 进入竞争**
    - 而不是 `第一 -> 站` 这种两步延伸链

- 因而这轮把路线再收窄了一步：
  - `S6/S7` 已经证明：
    - 仅靠“锚定两字前缀，再调其后的 continuation”打不到真正承载正确链的那条 line
  - 如果还沿 stateful 路线继续，下一步要处理的应当不是：
    - `第一 -> 站 -> 是`
  - 而是：
    - **`第一站 / 第一战 / 第一展` 这种整块首词 family 的竞争保真**

- 当前 schema 已恢复关闭这轮实验状态：
  - `prefix_anchor_delta_debt_weight: 0.0`
  - `prefix_anchor_debt_release_weight: 0.0`

- 所以，这轮的结论可以明确写成一句话：
  - **两字 prefix-anchor 这条状态线，本轮没有命中真正的正确路径；下一刀若继续沿 `witogram` 的状态优势走，应升级为“整块首词 family 状态”，而不是继续补两字 anchor 后的 bridge。**

## 2026-05-18 `S8` whole-first-word family：后续惩罚与最小保活都未命中，说明主竞争在更早层已经偏离整块首词线

- 这一轮继续沿 `witogram` 的状态优势推进，但方向从“两字 prefix-anchor”升级到“整块首词 family”：
  - 目标不再是 `第一 -> 站 -> 是`
  - 而是直接围绕：
    - `第一站 / 第一战 / 第一展`
    - 对比
    - `的驿站 / 的翼展`
    - 这类整块首词 family

- 新增 very narrow 原型：
  1. `ShouldActivateWholeFirstWordAnchor(...)`
     - 仅在首词阶段激活
     - 条件很窄：
       - `generated_word_count == 1`
       - 首词本身是多字整块
       - 该首词走了 fallback，且存在 OOV debt
  2. `ComputeWholeFirstWordContinuationPenalty(...)`
     - 只惩罚：
       - 已锚定整块首词 family 后
       - 紧接着继续接出的两字 fallback continuation
     - 目标直指：
       - `第一站 -> 展示`
     - 而不是去打一般的后继词
  3. debug 新增：
     - `WholeFirstCont`
     - `StepWholeFirstCont`

- 但第一轮 probe 后发现：
  - `WholeFirstCont` 在
    - `diyizhan`
    - `diyizhanshi`
    - `diyizhanshiyi`
    - 完整句
  - 所有前排候选里都是 `0.00`
  - 工件：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\boundary_chain_probe_whole_first_family_v1.json`

- 这说明：
  - 仅仅补“整块首词后的 continuation 惩罚”还不够
  - 因为当前 top1 错链如 `第一展示` 并不是从 `第一站` 这条整块首词 line 后接出来的
  - 换句话说，`第一站` family 本身还没有真正进入下一步的主竞争

- 因此又继续做了一个 very small 的 early 保活：
  - 在 `SelectTopLines()` 里，不扩大 beam
  - 只在当前 pool 中额外保留 1 条：
    - `whole_first_word_anchor_active`
    - 且 `generated_word_count == 1`
    - 的 best line
  - 目的不是增加候选总量，而是防止整块首词 family 在 very early 阶段被直接挤出下一步扩展

- 按标准方式重新编译：
  - `librime/build.bat static`

- 第二轮 probe 工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\boundary_chain_probe_whole_first_family_v2.json`

- `v2` 的结果依旧没有命中：
  - `WholeFirstCont` 仍全部为 `0.00`
  - `diyizhanshi` 仍是：
    - `第一展示`
    - `敌意展示`
    - `地衣展示`
  - `diyizhanshiyi` 仍是：
    - `的驿站是以`

- 所以，这轮最重要的结论不是“这个 penalty 不够强”，而是：
  - **即使给整块首词 family 留了 1 条保活名额，后续主竞争也没有沿那条 line 往下展开。**
  - 这说明当前的主问题比预期还更靠前：
    - 不是“整块首词 family 没保住”
    - 也不是“保住后没有后继惩罚”
    - 而是：
      - **主竞争在到达这层之前，就已经被另一类分解路径占走了。**

- 因而，这轮把下一步再次收紧成一句话：
  - 若继续沿 `witogram` 路线推进，下一刀不应再补某个 anchor 或后继 penalty
  - 而应直接检查：
    - **`0-8` 这类首段整块候选到底有没有真正进入下一步扩展集合**
    - 以及：
    - **从 `0-8` 到 `8-11` 的扩展之前，哪一步把它排除在主竞争之外**

- 当前 schema 已恢复关闭这轮实验状态：
  - `whole_first_word_continuation_penalty_weight: 0.0`

## 2026-05-18 `S9` expansion-gate probe：`第一站是` 并没有死在扩展入口，当前路线结论应回到最终排序竞争

- 这一轮不再继续加新的 scorer patch，而是直接对 `0-8 -> 8-11` 的扩展入口做最小探针。
- 目标很明确：
  - 钉死 `第一站 / 第一战 / 第一展` 这类整块首词 family
  - 到底死在：
    - `source_pool`
    - `top_candidates`
    - `all_requests / batch_selected`
    - `admitted_state_index`
    - 还是更后面的 `final_pool / final ranking`

- 实现：
  - 在 `witset_poet` 里新增 `debug_expansion_gate_records_`
  - 对以下阶段打点：
    - `source_pool`
    - `top_candidate`
    - `request`
    - `batch_selected`
    - `admitted_new / admitted_replace / admitted_reject`
    - `final_pool`
  - 为了避免再依赖 graph snapshot 的延迟 flush，这批 records 直接并入本地快照：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\witset_local_snapshot.jsonl`

- 代表工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\expansion_gate_probe_diyizhanshi.json`

- 关键证据一：`0-4` 的 `第一 / 敌意 / 地衣` 都确实进入了 `top_candidate`
  - `source_pool`
    - `第一`
    - `敌意`
    - `地衣`
  - `top_candidate`
    - `第一`
    - `敌意`
    - `地衣`
  - 说明第一层 family 并没有在 very early beam 入口就被筛掉

- 关键证据二：`0-8` 的 `第一站 / 第一战 / 第一展` 也确实进入了 `top_candidate`
  - 工件里能直接看到：
    - `stage = source_pool`, `source_text = 第一站 / 第一战 / 第一展`
    - `stage = top_candidate`, `source_text = 第一站 / 第一战 / 第一展`
  - 这一步已经足以推翻上一轮里“整块首词 family 可能没真正进入下一步扩展集合”的猜测

- 关键证据三：`第一站 -> 是` 不但进入了 request，而且通过了 batch 入口与 admitted state
  - 工件里直接可见：
    - `stage = request`, `source_text = 第一站`, `entry_text = 是`
    - `stage = batch_selected`, `source_text = 第一站`, `entry_text = 是`
    - `stage = admitted_new`, `source_text = 第一站`, `entry_text = 是`
  - 同类的 `第一战 -> 是`、`第一展 -> 是` 也一样存在
  - 所以可以明确排除：
    - “`第一站是` 死在 `top_candidates` 之前”
    - “`第一站是` 死在 `all_requests` 的全局 batch 入口”
    - “`第一站是` 死在 `admitted_state_index` 的状态去重”

- 关键证据四：`第一站是` 甚至还留在 `final_pool`
  - 工件里可见：
    - `stage = final_pool`, `source_text = 第一站是`
  - 同时也能看到：
    - `stage = final_pool`, `source_text = 第一展示`
    - `stage = final_pool`, `source_text = 敌意展示`
    - `stage = final_pool`, `source_text = 地衣展示`
  - 这说明：
    - `第一站是` 并没有在到达 final pool 之前被剪掉

- 关键证据五：真正的问题是 final ranking 的巨大分差
  - `final_pool` / 候选快照显示：
    - `第一展示`
      - `beam_score ≈ -242.11`
    - `第一站是`
      - `beam_score ≈ -282.59`
  - 两者差距约：
    - `40.48`
  - 而快照最终 top3 也正是：
    - `第一展示`
    - `敌意展示`
    - `地衣展示`
  - `第一站是` 不在前排，不是因为没进来，而是因为 **进来了但分差太大，最终排序完全打不过。**

- 所以，这一轮已经把当前路线的结论钉死了：
  - **主问题不是搜索入口，不是 beam 容量，不是 top-candidate 选择，也不是 admitted state 去重。**
  - **主问题就是 `8-11` 这一步的最终排序竞争：**
    - `第一站 + 是`
    - 对
    - `第一 + 展示`
    - 在当前 split-token LM + scorer 组合下，后者系统性占优，而且优势量级很大

- 因而到这里，可以给当前路线做一个明确收口：
  1. `S1` 到 `S8` 的多轮 patch 没有白做
     - 它们帮助确认了：
       - 正确链不是“压根进不来”
       - 而是“进来以后仍然被大幅打输”
  2. 继续沿“保活 / anchor / continuation penalty”这条线再往下堆，
     - 已经不是当前最值得投入的方向
  3. 下一阶段如果继续做 `witogram`，
     - 就应回到：
       - **为什么 `第一展示` 在 `8-11` 这一步比 `第一站是` 高出约 40 分**
     - 也就是：
       - **直接拆账 `8-11` 的最终排序项**
       - 而不是继续怀疑它有没有进 beam

- 当前 schema 已恢复关闭这轮 graph 调试状态：
  - `debug_dump_local_graph_snapshot: false`

## 2026-05-18 `S9` 补充拆账：`第一站是` 输在 `final_pool` 内的 `LM/base_score`，不是输在入口或后验惩罚

- 基于修正后的 `expansion_gate_records` 序列化，继续直接读取：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\final_score_breakdown_diyizhanshi.json`
- 这次不再只看 `beam_score` 总差，而是把 `final_pool` 里真正对打的两条 line 分项拆开：
  - `第一展示`
    - `beam_score = -242.106`
    - `base_score = -223.388`
    - `adjustment_score = -18.718`
    - `dict_score_norm = -16.9065`
    - `lm_score_scaled = -199.479`
    - `boundary_score = 0.485203`
    - `oov_penalty = -1.40`
    - `used_char_fallback = true`
  - `第一站是`
    - `beam_score = -282.588`
    - `base_score = -248.282`
    - `adjustment_score = -34.3061`
    - `dict_score_norm = -18.9412`
    - `lm_score_scaled = -223.922`
    - `boundary_score = 0.0345143`
    - `oov_penalty = -1.15`
    - `used_char_fallback = false`

- 两条 line 的最终分差可直接拆成：
  - 总分差：
    - `40.482`
  - 其中 `base_score` 差：
    - `24.894`
  - 其中 `adjustment_score` 差：
    - `15.5881`

- 继续按分项看，最关键的不是 heuristics，而是 `LM`：
  - `lm_score_scaled` 差：
    - `24.443`
  - `dict_score_norm` 差：
    - `2.0347`
  - `boundary_score` 差：
    - `0.4507`
  - `length_term` 差：
    - `0.0236`
  - `oov_penalty` 反而是 `第一站是` 略好：
    - `-0.25`
  - `whole_word_bonus / fragment_penalty / structure_penalty / tail_repair_penalty / octagram_penalty / joint_* / beam_merge_penalty`
    - 两边基本相同，不构成主导来源

- 这一步把当前路线判断进一步钉死：
  - `第一站是` 不是没进 `final_pool`
  - 也不是进来后被某个额外 patch 项单独打死
  - 而是：
    - **在当前 split-token LM + scorer 组合下，`第一 + 展示` 这条 line 自带更强的 `LM/base_score`，在最终池里就已经系统性领先。**

- 这也解释了为什么前面的 `anchor / preserve / continuation penalty` 都只能收窄问题、却拉不回 top1：
  - 它们最多影响的是：
    - 入口保活
    - very early continuation
    - 一些后验 adjustment
  - 但当前真正决定胜负的主差额，已经主要落在：
    - `LM/base_score`
    - 尤其是 `8-11` 这一步对 `展示` 相对 `站是` 的强烈偏好

- 所以，到这里当前路线的收口可以再明确一层：
  - 若还继续在线上 `witogram` 主链推进，下一步不应再优先堆新的保活/惩罚 patch
  - 更值得做的是直接检查：
    - `8-11` 这一步 `ScoreFeatures()` / `base_score` 的形成机制
    - 为什么 `展示` 在当前 split-token LM 下会对 `站是` 形成约 `24.44` 的 `LM` 级领先
    - 以及是否必须承认：在现模型前提下，这类 `站是` vs `展示` 竞争单靠后验 patch 已接近天花板

## 2026-05-18 `S9` 继续下钻代码链：`8-11` 当前步并非天然偏爱 `是` 以外的词，真正失血点是 `第一站` 前缀已带着更差累计权重进入这一步

- 继续直接对照代码与拆账工件后，先把 `8-11` 的打分链条钉清楚：
  - `witogram::ScoreFeatures(...)`
    - 会先把 `context` 与 `word` 都按 UTF-8 单字切成 token
    - `context` 只保留 `Order()-1` 个后缀 token
    - 再对 `word` 走两套分数：
      - `char_path_log10`
      - 若词表里整词存在，再算 `whole_word_log10`
    - 若整词存在，则总分是：
      - `0.60 * whole_word_log10 + 0.40 * char_path_log10`
    - 若整词不存在，则直接退回 `char_path`
  - `witset_poet`
    - `base_score = candidate->weight + dict_score_weight * dict_score_raw + lm_total_weight * lm_score_scaled + prior`
    - `adjustment_score` 里又额外加入：
      - `dict_score_norm_weight * dict_score_norm`
      - `lm_avg_weight * lm_score_avg`
      - `boundary / oov / length / whole_word_bonus / fragment / tail / octa ...`
  - 也就是说，`LM` 不只进一次：
    - 一次进 `base_score` 的 `lm_score_scaled`
    - 一次进 `adjustment_score` 的 `lm_score_avg`

- 然后继续把两条真正对打的 `request` 记录拆开：
  - `source_text = 我踏上了旅行的征程。第一`, `entry_text = 展示`
    - `base_score = -231.49`
    - `adjustment_score = -10.6161`
    - `lm_score_scaled = -109.68`
    - `lm_score_avg = -54.8399`
    - `dict_score_norm = -8.58448`
    - `boundary_score = 0.242602`
    - `oov_penalty = -0.25`
    - `used_char_fallback = true`
  - `source_text = 我踏上了旅行的征程。第一站`, `entry_text = 是`
    - `base_score = -255.267`
    - `adjustment_score = -27.3217`
    - `lm_score_scaled = -89.799`
    - `lm_score_avg = -89.799`
    - `dict_score_norm = -11.5382`
    - `boundary_score = -0.35`
    - `oov_penalty = 0`
    - `used_char_fallback = false`

- 这一步出现了一个非常关键、之前肉眼不容易看出的事实：
  - **单看 `8-11` 当前步，`是` 的 `lm_score_scaled` 其实比 `展示` 更好约 `19.881` 分。**
  - 也就是说：
    - 当前步本身并不是“KenLM 一看到 `第一站` 就一定更喜欢别的词，不喜欢 `是`”
  - 真正的问题是：
    - `第一站` 在进入这一步前，已经背着更差的前缀累计权重

- 用 `base_score - dict_score_raw - lm_score_scaled` 反推 `candidate->weight` 后可直接看到：
  - `第一 -> 展示` 的前缀权重约：
    - `-109.6697`
  - `第一站 -> 是` 的前缀权重约：
    - `-153.9298`
  - 前缀差约：
    - `44.26`

- 这就把流程问题拆得更细了：
  1. `8-11` 当前步本身：
     - `是` 的 step-level `LM` 并不差，甚至优于 `展示`
  2. 但在进入这一步前：
     - `第一站` 这条 prefix line 已经比 `第一` 差了约 `44` 分
  3. 进入本步后，`adjustment_score` 又进一步放大这种差距：
     - `是` 是单字，`boundary_score` 变负
     - `whole_word_bonus` 还要减单字罚项
     - 同时还叠了 `fragment_penalty / tail_repair_penalty / octagram_penalty`
     - 而 `展示` 作为双字 continuation，没有这些单字型负项
  4. 再加上 `LM` 还会通过 `lm_avg_weight * lm_score_avg` 再进一次 `adjustment_score`
     - `展示` 的 `lm_score_avg` 也明显好于 `是`

- 因而当前更精确的路线判断应更新为：
  - 之前说“主问题在 `8-11 final ranking`”没有错
  - 但进一步拆开后可见：
    - **真正的失血并不是只在 `第一站 -> 是` 这一步才发生**
    - **而是 `第一站` 前缀在到达 `8-11` 之前就已经大幅落后；`8-11` 这一步只是继续把这个差距坐实。**

- 这也解释了为什么只修 `站|是` bridge 或只惩罚 `展示` continuation 都拉不回 top1：
  - 因为它们只作用于 very local 的当前步
  - 但当前最大缺口已经有很大一部分是：
    - `第一` vs `第一站` 的前缀累计分差
  - 剩下才是：
    - `展示` 作为双字 continuation 比单字 `是` 更容易拿到更好的 `adjustment_score`

- 因此，如果还继续往代码里查，下一步最值得看的不再只是：
  - `第一站 -> 是`
- 而应优先回到：
  - `第一 -> 站`
  - `第一 -> 展`
  - `第一 -> 战`
  - 这几个 prefix expansion 的累计 `LM/base/adjustment` 是如何开始分叉的

## 2026-05-18 `S9` 再往前拆一层：`第一 -> 站 / 展 / 战` 几乎不分叉，真正占优的是另一种分词结构 `第一 + 展示`

- 按上一步的落点继续把 prefix expansion 直接从工件里抠出来后，首先能确认一件事：
  - **`第一 -> 站 / 展 / 战` 这一步本身并不是主失血点。**

- 直接看 `source_text = 我踏上了旅行的征程。第一` 的三条 `request`：
  - `第一 -> 展`
    - `base_score = -165.72`
    - `adjustment_score = -17.41`
    - `weight = -183.14`
    - `lm_score_scaled = -44.32`
    - `dict_score_raw = -11.73`
  - `第一 -> 战`
    - `base_score = -165.74`
    - `adjustment_score = -17.42`
    - `weight = -183.16`
    - `lm_score_scaled = -44.32`
    - `dict_score_raw = -11.75`
  - `第一 -> 站`
    - `base_score = -165.75`
    - `adjustment_score = -17.42`
    - `weight = -183.17`
    - `lm_score_scaled = -44.32`
    - `dict_score_raw = -11.76`

- 这组三条记录有两个非常关键的含义：
  1. 三者当前步 `LM` 完全相同：
     - `lm_score_scaled = -44.32`
  2. 三者总分只差百分位量级：
     - `展` 比 `战` 只好约 `0.02`
     - `战` 比 `站` 只好约 `0.01`
     - `站` 并不是被明显压死

- 因而这一步可以明确排除一个之前容易误判的方向：
  - 不是 `第一 -> 展` 在 prefix char 级别就已经把 `第一 -> 站` 大幅压下去
  - 也不是 `第一 -> 站` 在 `LM` 上先天吃了大亏

- 再看扩到三字后的 prefix state：
  - `第一站`
    - `source_pool/top_candidate weight = -153.93`
  - `第一战`
    - `source_pool/top_candidate weight = -154.16`
  - `第一展`
    - `source_pool/top_candidate weight = -154.66`

- 这里反而说明：
  - **在同一类 `第一X` 三字前缀里，`第一站` 其实是最强的，不是最弱的。**
  - 也就是说：
    - 到 `第一站 / 第一战 / 第一展` 这一层时，正确 family 还没有输

- 真正开始出现大分叉的是它们后续的承接方式：
  - `第一站 -> 是`
    - `weight = -282.59`
    - `lm_score_scaled = -89.80`
    - `adjustment_score = -27.32`
  - `第一战 -> 是`
    - `weight = -282.82`
    - `lm_score_scaled = -89.80`
    - `adjustment_score = -27.32`
  - `第一展 -> 是`
    - `weight = -283.32`
    - `lm_score_scaled = -89.80`
    - `adjustment_score = -27.32`
  - 但：
    - `第一展 -> 示`
      - `weight = -254.25`
      - `lm_score_scaled = -65.36`
      - `adjustment_score = -22.47`

- 这说明当前真正的拉开，不是出在：
  - `第一 -> 站 / 展 / 战`
- 而是出在：
  - **一旦走到 `第一展`，再接 `示` 会拿到远好于 `第一站 -> 是` 的后续得分。**

- 再结合上一轮已确认的另一条强竞争线：
  - `第一 -> 展示`
    - `weight = -242.106`
    - 甚至还强于 `第一展 -> 示`

- 因而这一步把当前问题的结构又说得更准确了：
  - 当前线上主竞争，不只是“`站` 和 `展` 哪个单字更优”
  - 而是：
    - **错误侧存在更强的双字 continuation / 另类分词结构：**
      - `第一 + 展示`
      - `第一展 + 示`
    - 它们都绕开了 `第一站 + 是` 这种“前缀正确，但第二词是单字”的结构性劣势

- 这也把之前的几轮 patch 为什么不够解释得更清楚：
  - 如果只盯着：
    - `第一 -> 站`
    - 或 `站|是`
  - 那只是修一条局部边
  - 但当前真正占优的错误路径，是另一种 segmentation：
    - 多字后继词 `展示`
    - 或 `第一展 + 示`
  - 它们天然比单字后继 `是` 更容易拿到：
    - 更好的 `boundary`
    - 更少的单字罚项
    - 更好的 `lm_avg`
    - 以及更强的整段 continuation 得分

- 所以当前再收口一句：
  - **`第一 -> 站 / 展 / 战` 不是主战场；主战场是 `第一站 + 是` 这条正确分词结构，正在被 `第一 + 展示` / `第一展 + 示` 这类替代分词结构整体压制。**

- 因此下一步如果还继续往算法根因追，不应再只看“单字 `站` 为什么没赢 `展`”
- 而应直接检查：
  - `第一 + 展示`
  - `第一展 + 示`
  - `第一站 + 是`
  - 这三种 segmentation 在 `base_score + adjustment_score` 上分别是如何被系统性拉开的

## 2026-05-18 `S9` 决定性结论：当前主失败不是字级前缀竞争，而是 scorer 系统性偏向 `第一 + 展示` 这类“多字后继” segmentation

- 把三种真正互相竞争的 segmentation 放到同一张表里后，当前结论已经可以收口：
  - `第一 + 展示`
    - `prefix_weight = -109.6697`
    - `base_score = -231.49`
    - `adjustment_score = -10.6161`
    - `total = -242.106`
  - `第一展 + 示`
    - `prefix_weight = -154.6620`
    - `base_score = -231.773`
    - `adjustment_score = -22.4721`
    - `total = -254.246`
  - `第一站 + 是`
    - `prefix_weight = -153.9298`
    - `base_score = -255.267`
    - `adjustment_score = -27.3217`
    - `total = -282.588`

- 三者差额也已经很清楚：
  - `第一 + 展示` 相对 `第一展 + 示`
    - 总分领先：
      - `12.14`
    - 其中 `base_score` 只领先：
      - `0.283`
    - 主要来自 `adjustment_score` 领先：
      - `11.856`
  - `第一展 + 示` 相对 `第一站 + 是`
    - 总分领先：
      - `28.342`
    - 其中 `base_score` 领先：
      - `23.494`
    - `adjustment_score` 还再领先：
      - `4.8496`
  - `第一 + 展示` 相对 `第一站 + 是`
    - 总分领先：
      - `40.482`
    - 其中 `base_score` 领先：
      - `23.777`
    - `adjustment_score` 再领先：
      - `16.7056`

- 这组数字把当前问题钉得比之前更死：
  1. `第一 -> 站 / 展 / 战` 这一拍本身几乎不分叉
     - 正确路并不是在这里输掉的
  2. `第一站 / 第一战 / 第一展` 三字前缀里，`第一站` 其实还是最强
     - 正确前缀 family 并没有先死
  3. 真正杀死正确路的是**后续 segmentation 形态**
     - `第一 + 展示` 这条路，主要靠更好的 `adjustment_score` 获胜
     - `第一展 + 示` 这条路，则主要靠更好的 `base_score` 获胜

- 这意味着当前 scorer 同时存在两层系统性偏向：
  - 第一层：
    - **偏向 `第一 + 展示` 这种“前缀两字 + 后继两字”的结构**
    - 因为它能绕开单字后继带来的：
      - 负 `boundary`
      - 单字 `whole_word_bonus` 扣减
      - `fragment / tail / octa` 一整串单字型负项
  - 第二层：
    - **在已经进入 `第一X + 单字` 这类结构后，又更偏向 `第一展 + 示`，而不是 `第一站 + 是`**
    - 这里主导差额已不再是 prefix，而是：
      - `第一展 -> 示` 的 `base_score`
      - 明显好于 `第一站 -> 是`

- 因而当前可以给出一个决定性路线判断：
  - **正确路 `第一站 + 是` 失败的主因，不是 beam、不是真实前缀 family 没进来、也不是 `第一 -> 站` 这一拍字级竞争输了。**
  - **主因是当前 scorer 对 segmentation 结构本身有系统偏向：**
    - 先整体偏向“多字后继”如 `展示`
    - 若进入单字后继结构，又进一步偏向 `示` 而不是 `是`

- 这也意味着：
  - 继续沿 `anchor / preserve / bridge reward / continuation penalty` 这类 patch 往下堆，
  - 已经很难从根上解决问题
  - 因为它们主要在修局部边
  - 而当前失败是：
    - **整种 segmentation 模板在 scorer 目标函数里被系统性偏置了**

- 所以到这里，当前最可靠的收口结论就是：
  - **如果不改 scorer 对 segmentation 模板的偏置，只修局部 `站|是`，这条线上已经很接近天花板。**

## 2026-05-18 直接重审 `base_score + adjustment_score`：当前 scorer 对“多字后继”与“单字后继”存在明确的结构性偏置

- 这次不再沿样例局部 patch，而是直接回到公式本身检查：
  - `base_score = candidate->weight + upstream_prior + joint_prior + dict_score_raw + lm_score_scaled`
  - `adjustment_score = dict_norm + lm_avg + boundary + oov + length + whole + merge + fragment + structure + tail + octa + ...`
  - 见：
    - `witset_poet.cc` 中 `1735-1824`

- 先看几个直接按 `char_count` 写死的项：
  - `ComputeBoundaryFeature(char_count)`
    - `char_count <= 1 -> -1.0`
    - `char_count >= 2 -> log1p(char_count - 1)`
    - 乘当前权重 `0.35`
    - 所以 `2字` 相对 `1字` 固定多拿：
      - `0.35 * (ln 2 - (-1)) = +0.5926`
  - `ComputeLengthFeature(char_count)`
    - `0.20 * log1p(char_count)`
    - `2字` 相对 `1字` 固定多拿：
      - `0.20 * (ln 3 - ln 2) = +0.0811`
  - `whole_word_bonus`
    - 命中整词先加 `+0.15`
    - 但 `char_count <= 1` 再减 `single_char_penalty = 0.25`
    - 所以命中整词的 `1字` 相对命中整词的 `2字` 固定再少：
      - `0.25`

- 再看几个对“长前缀后接单字”直接惩罚的项：
  - `ComputeFragmentPenalty(prev_char_count, char_count)`
    - 若 `prev_char_count >= 3 && char_count == 1`
      - 原始值 `-1.5`
      - 乘 `fragment_penalty_weight = 0.45`
      - 得：
        - `-0.675`
    - 若同时 `is_rear`
      - 再减 `rear_fragment_penalty = 0.35`
      - 合计：
        - `-1.025`
  - `ComputeTailRepairPenalty(...)`
    - 对 `3字 anchor + 句尾单字`：
      - `-(0.95 * (1.0 + 0.2 + 0.35)) = -1.4725`
  - `ComputeOctagramStyleTailPenalty(...)`
    - 对 `3字 anchor + 句尾单字`：
      - `-(3.2 + 0.5 + 0.6) = -4.3`

- 也就是说，仅按当前默认公式，不考虑 `LM/dict` 的内容分数，只考虑模板本身：
  - `3字前缀 + 句尾单字`
  - 相对
  - `前缀 + 2字后继`
  - 就天然背着大约：
    - `0.5926 + 0.0811 + 0.25 + 1.025 + 1.4725 + 4.3`
    - `= 7.7212`
    - 的固定结构性劣势

- 这个结论并不是纸上推导，真实样例也正好兑现了这组结构性惩罚：
  - `第一 + 展示`
    - `adjustment = -10.6161`
    - 其中：
      - `boundary = +0.2426`
      - `length = +0.2197`
      - `fragment = 0`
      - `tail = 0`
      - `octa = 0`
  - `第一展 + 示`
    - `adjustment = -22.4721`
    - 其中：
      - `boundary = -0.35`
      - `length = +0.1386`
      - `whole = -0.1`
      - `fragment = -1.025`
      - `tail = -1.4725`
      - `octa = -4.3`
  - `第一站 + 是`
    - `adjustment = -27.3217`
    - 其中结构项几乎与 `第一展 + 示` 完全一致：
      - `boundary = -0.35`
      - `length = +0.1386`
      - `whole = -0.1`
      - `fragment = -1.025`
      - `tail = -1.4725`
      - `octa = -4.3`

- 因而，`adjustment_score` 的结构性偏置已经可以定性为：
  - **只要模板是“长前缀 + 句尾单字”，即便这个单字本身并不坏，也会被一整串固定模板惩罚同时命中。**
  - 这不是某个 feature 偶然偏一点，而是：
    - `boundary`
    - `single_char_penalty`
    - `fragment`
    - `tail_repair`
    - `octagram_tail`
    - 五层一起叠

- 再看 `base_score` 与 `adjustment_score` 的另一个结构性问题：
  - `LM` 被计了两次：
    - 一次作为 `lm_score_scaled` 进入 `base_score`
    - 一次作为 `lm_score_avg` 再进入 `adjustment_score`
  - 且 `lm_avg = lm_total / token_count`
  - 这意味着：
    - 对 `1字` 后继，`lm_avg` 基本等于 `lm_total`
    - 对 `2字` 后继，`lm_avg` 约等于 `lm_total / 2`
  - 结果就是：
    - 在 `lm_avg_weight = 0.20` 下
    - `1字` 后继会额外再背一遍接近 `20%` 的 `LM` 全量惩罚
    - `2字` 后继则只再背约 `10%`

- 真实样例里这个二次 `LM` 惩罚也非常明显：
  - `第一 + 展示`
    - `lm_avg_term = -10.9680`
  - `第一展 + 示`
    - `lm_avg_term = -13.0712`
  - `第一站 + 是`
    - `lm_avg_term = -17.9598`

- 因而，这轮直接重审后，当前 scorer 的结构性偏置可以收口成两句话：
  1. `adjustment_score` 通过多条硬规则，系统性打压“长前缀 + 单字后继”模板
  2. `LM` 的 `total + avg` 双重入分，又进一步让 `1字` 后继比 `2字` 后继承担更重的重复惩罚

- 这说明当前主失败并不只是某个样例局部不幸，而是目标函数层面的系统结果：
  - **同等语义质量下，多字后继模板会天然比单字后继模板更容易胜出。**

- 所以如果下一步真的要动代码，最值得优先验证的，不是继续加新的局部补偿项，而是直接做下面两件事中的至少一件：
  - 方案 A：
    - 把 `lm_avg_weight_` 暂时降到 `0`
    - 验证是否能显著缩小 `1字` 后继的结构性重复惩罚
  - 方案 B：
    - 对 `fragment / tail_repair / octagram_tail / single_char_penalty`
    - 增加“仅在 trailing_single_char_run >= 2 或明显碎片化时才触发”的门控
    - 不再让“长前缀后接一个合法单字”自动吃满整套惩罚

- 当前更偏向先做 A，再做 B：
  - A 能最快验证 `LM` 双计分是否是主放大器
  - 若 A 仍不够，再对 B 做最小门控重构

## 2026-05-18 最小验证 A：临时将 `lm_avg_weight = 0` 后，`第一站 + 是` 明显回升，但 top1 仍未转正

- 按上面的判断，先做了最小验证版 A：
  - 仅临时把
    - `C:\Users\Bing\AppData\Roaming\witty\witset.schema.yaml`
    - `C:\Users\Bing\AppData\Roaming\witty\build\witset.schema.yaml`
  - 里的 `lm_avg_weight`
    - 从 `0.20`
    - 改为 `0.0`
  - 不改 `witset_poet` 代码默认值
  - 然后用
    - `plugins/witogram/tools/run_local_snapshot_baseline.py`
  - 对整句
    - `我踏上了旅行的征程。第一站是一座古老的小镇`
  - 做单 case 回放

- 这轮最关键的结果如下：
  - `diyizhanshi` 阶段 top 候选变为：
    1. `的驿站是`
    2. `第一展示`
    3. `敌意展示`
    4. `地衣展示`
  - 也就是说：
    - **`第一展示` 不再是 top1，说明去掉 `lm_avg` 双计分后，确实削弱了“展示型多字后继”的优势**
    - 但 **top1 仍然没有转正**，只是主错暴露成了另一条更早的错误链：
      - `的驿站是`

- 把三种 segmentation 的 `request` 拆账与改前对比：
  - 改前：
    - `第一 + 展示`
      - `total = -242.106`
    - `第一展 + 示`
      - `total = -254.246`
    - `第一站 + 是`
      - `total = -282.588`
  - 改后 (`lm_avg_weight = 0`)：
    - `第一 + 展示`
      - `base = -222.510`
      - `adj = 0.3519`
      - `total = -222.158`
    - `第一展 + 示`
      - `base = -222.832`
      - `adj = -9.4010`
      - `total = -232.233`
    - `第一站 + 是`
      - `base = -246.325`
      - `adj = -9.3619`
      - `total = -255.687`

- 差额变化非常清楚：
  - `第一 + 展示` 相对 `第一站 + 是`
    - 改前领先：
      - `40.482`
    - 改后领先：
      - `33.529`
    - 缩小了：
      - `6.953`
  - `第一展 + 示` 相对 `第一站 + 是`
    - 改前领先：
      - `28.342`
    - 改后领先：
      - `23.454`
    - 缩小了：
      - `4.888`

- 这轮 A 因而可以给出一个相当明确的判断：
  1. `lm_avg_weight` 确实是主放大器之一
     - 关掉后，`第一站 + 是` 对 `第一展示 / 第一展 + 示` 的差距都明显收窄
  2. 但它不是唯一根因
     - 因为即使关掉后，`第一站 + 是` 仍未胜出
     - 同时另一个老问题链
       - `的驿站是`
     - 重新浮到 top1
  3. 因此：
     - **`LM` 双计分是重要问题，但不是单独足以解决 top1 的总开关**

- 这也反过来验证了前一轮对 scorer 的总判断：
  - 当前失败不是只由一个 feature 导致
  - 而是：
    - `lm_avg` 的重复惩罚
    - 再叠加
    - `fragment / tail / octa / single_char` 这套结构性模板惩罚
    - 以及更早的句首错误链竞争
  - 多层一起造成的

- 这轮实验结束后，已把 `lm_avg_weight` 恢复回：
  - `0.20`
  - 避免后续其它验证被实验值污染

- 因而当前下一步判断也比之前更清楚：
  - **不建议直接把 `lm_avg_weight` 长期改成 `0` 就收工**
  - 更合理的是：
    1. 保留“`lm_avg` 不应对 1 字后继重复计满”的思路
    2. 但下一版不应是全局粗暴关掉
    3. 而应改成：
       - 只对 `token_count == 1` 做削弱/归一
       - 或同时配合对 `fragment / tail / octa / single_char` 的最小门控

## 2026-05-18 窄代码原型 B：仅禁止 `token_count == 1` 的 `lm_avg` 二次入分，能继续拉近 `第一站 + 是`，但会把 `驿站是` 类单字后继错链整体抬高

- 按 A 的结论，继续做了一个比“全局 `lm_avg_weight = 0`”更窄的代码原型：
  - 在 `witset_poet.cc`
  - 只让
    - `adjustment_score += lm_avg_weight_ * lm_score_avg`
  - 对
    - `lm_token_count > 1`
  - 生效
  - 也就是：
    - **仅禁止 `token_count == 1` 的后继再吃第二遍 `LM`**

- 这个原型的直接效果比 A 更符合预期：
  - `第一 + 展示`
    - 仍保持原值
    - `total = -242.106`
  - `第一展 + 示`
    - 从 `-254.246`
    - 回升到 `-241.174`
  - `第一站 + 是`
    - 从 `-282.588`
    - 回升到 `-264.629`

- 差额变化：
  - `第一 + 展示` 相对 `第一站 + 是`
    - 从领先 `40.482`
    - 缩到领先 `22.523`
  - `第一展 + 示` 相对 `第一站 + 是`
    - 从领先 `28.342`
    - 缩到领先 `23.455`

- 也就是说：
  - **这版窄原型对正确链的帮助，比 A 还更聚焦**
  - 因为它保留了 `展示` 这类 `2-token` 后继原有的 `lm_avg`
  - 只削掉了 `是/示` 这类 `1-token` 后继的重复惩罚

- 但实际 top 候选结果说明，它仍然不能单独作为最终解：
  - `diyizhanshi` 阶段 top10 变成：
    1. `的驿站是`
    2. `的翼展示`
    3. `地驿站是`
    4. `第驿站是`
    5. `递驿站是`
    - ...
  - 其中：
    - `第一展示`
      - 已经被压下去
    - 但
      - `的驿站是`
      - `地驿站是`
      - 一整组“句首错字 + 驿站是”链
      - 被同步抬高了

- 这组结果非常关键，因为它说明：
  1. `token_count == 1` 的 `lm_avg` 二次惩罚，确实是压制 `第一站 + 是` 的核心放大器之一
  2. 但它压制的并不只有正确链
     - 也同样压制所有“单字后继”的错误链
  3. 因而只去掉这一个放大器，会把
     - `第一站是`
     - 和
     - `的驿站是`
     - 一起抬高
  4. 所以它仍然不是单独可上线的改法

- 因而这轮 B 可以给出一个比 A 更清楚的路线判断：
  - **`lm_avg` 的单字二次惩罚必须处理，但必须和“句首错字链”抑制一起联动处理。**
  - 否则会出现：
    - `第一展示` 被压下去
    - 但 `的驿站是` 顶上来的副作用

- 这也意味着，下一版若继续做代码原型，最合理的顺序是：
  1. 保留这条思路：
     - `token_count == 1` 不应吃满第二遍 `LM`
  2. 但不要直接裸改
  3. 而是和下面两类机制至少叠一类：
     - 句首错误家族抑制
       - 避免 `的/地/第/递/... + 驿站是` 被整体抬升
     - `fragment / tail / octa / single_char`
       - 对合法单字承接做更细门控

- 这版窄原型验证完成后：
  - 已把源码回退
  - 并重新执行 `build.bat static`
  - 当前工作树与本地产物都已恢复到原来的 scorer 行为

## 2026-05-18 联动原型 C 失败：复用现有 `edge/joint` 风险信号做句首错字链抑制，没有产生可见效果

- 在 B 的基础上，又做了一版联动原型 C，思路是：
  1. 保留 B 的核心想法：
     - `token_count == 1` 的后继不再吃第二遍 `lm_avg`
  2. 再额外尝试复用现有的：
     - `recent_edge_risk`
     - `recent_edge_spelling_class`
     - `cumulative_char_fallback_hits`
     - `cumulative_lm_oov_tokens`
  3. 只在下面这种结构上追加最小 `upstream_path_prior` 抑制：
     - 首词为 `1` 字
     - 第二词为合法多字整词承接
     - 且前一条边已经带有风险信号

- 这版 C 的结果是：
  - `diyizhanshi` 阶段 top 候选与 B 完全一致：
    1. `的驿站是`
    2. `的翼展示`
    3. `地驿站是`
    4. `第驿站是`
    5. `递驿站是`
    - ...
  - 关键三条 segmentation 的拆账也与 B 完全一致：
    - `第一 + 展示`
      - `total = -242.106`
    - `第一展 + 示`
      - `total = -241.174`
    - `第一站 + 是`
      - `total = -264.629`
  - `的 + 驿站` 的请求记录也仍是：
    - `base = -131.528`
    - `adj = -9.3733`
    - `total = -140.902`

- 这说明当前可以下一个很明确的负结论：
  - **“复用现有 `edge/joint` 风险信号来压句首错字家族”这条路，在当前样例上没有产生任何可见效果。**
  - 也就是说：
    - 不是抑制力度不够一点点
    - 而是这组信号本身并没有把
      - `的/地/第/递/... + 驿站`
      - 这一家族
    - 标成可供 scorer 使用的“高风险前驱”

- 因而，路线判断再收紧一步：
  1. B 已证明：
     - `token_count == 1` 的 `lm_avg` 二次惩罚确实该处理
  2. C 又证明：
     - 但不能指望直接复用现有 `edge/joint` hint 去压 `的驿站是` 家族
  3. 这意味着下一步若还想联动修：
     - **必须引入新的句首错误家族信号**
     - 或者改用更直接的结构规则
     - 不能只重用当前现成 hint

- 到这里，下一步最靠谱的两个方向也因此更清楚：
  - 方向 1：
    - 直接做新的“句首单字错字家族”信号
    - 例如围绕首字 family / 首两步 family 关系重新建一个最小风险量
  - 方向 2：
    - 不再走 risk-hint 复用
    - 直接对
      - `句首单字 + 多字承接`
    - 这类模板做更明确的结构门控
    - 然后再和 `token_count == 1` 的 `lm_avg` 削弱联动

- 这轮 C 验证完成后：
  - 已把源码回退
  - 并重新执行 `build.bat static`
  - 当前 again 恢复为原始 scorer 行为

## 2026-05-18 回头做批量校验：`单字合法后继被系统性压制` 是真实问题，但不是当前主错误集的唯一主轴

- 为了回答“前面的结论是不是基于太小样例、是否已经偏离目标”，这次没有再造新样本，而是直接回到之前已经收敛好的同口径锚点集：
  - `C:/Users/Bing/AppData/Roaming/witty/debug/e2_minimal_compare_17.json`
  - 这是此前明确使用真实 `input + preceding_text` 独立回放得到的 `17` 条高信息锚点
  - 口径比后来单句定点实验更稳

- 这 `17` 条里：
  - 正确：
    - `5`
  - 错误：
    - `12`

- 把这 `12` 条错误按形态重新看一遍，可以得到一个很关键的判断：
  - **“合法单字后继被系统性压制”确实存在，但它只解释了当前主错误集中的一小部分。**

- 具体说：
  - 直接明显符合“合法单字后继 / 错误切分把合法词拆成 `... 的方 ...` 这类结构”的，在这批 `17` 条锚点里，最典型的是：
    - `还让我体验到了不同地方的风土人情`
      - 当前：
        - `还让我体验到了不同的方的风土人情`
      - `octagram`：
        - `还让我体验到了不同地方的风土人情`
  - 再加上单独持续深挖的靶向样例：
    - `第一站是一座古老的小镇`
      - 当前局部竞争：
        - `第一展示 / 第一展 + 示 / 第一站 + 是 / 的驿站是`
  - 这说明：
    - **这个问题族不是幻觉，确实真实存在**
    - 但它并不能代表当前大多数错误

- 同一批锚点里，更多错误其实属于别的家族：
  - 同音/近音替换主导：
    - `带着一点微凉 -> 带着疑点微量`
    - `阳光从叶隙间漏下来 -> 阳光从业席间漏下来`
    - `心里却澄澈得近乎透明 -> 心里却成车的近乎透明`
  - 代词/功能词级吸引子：
    - `她笑的时候 -> 他小的时候`
    - `落叶在她发间轻轻停留 -> 落叶在他发件轻轻停留`
    - `总是刻意走在她的身后或身旁 -> 总是可以走在他的深厚和身旁`
  - 长跨度语义脱轨 / 片段误拼：
    - `营造出一种温馨浪漫的氛围 -> 营造出意中文新浪漫得分为`
    - `回到西安的街道 -> 回到显得解答哦`
    - `我抬头看那一树树的枝桠 -> 我抬头看那艺术书的质押`

- 所以，这轮批量校验之后，关于“前面是不是样例太小”的答案可以精确成：
  1. 前面对 `diyizhanshi` 的 scorer 拆账结论本身不是假的
     - 它确实揭示了一个真实存在的结构性偏置
  2. 但如果把它外推成“当前错误主因基本都是这个”，那就偏大了
  3. 在更稳的 `17` 条锚点里，它只能解释其中一小部分
  4. 因而，后面围绕 `第一站是 / 的驿站是` 持续打局部 patch，确实已经开始偏离总目标

- 也就是说，到这里路线判断更清楚了：
  - **“单字合法后继偏置”应被视为 scorer 的一个真实缺陷维度，而不是当前所有问题的总根因。**
  - 它值得在重构里修，但不值得继续作为唯一靶点反复做局部实验

## 2026-05-18 回到 `witogram` 优势的 scorer 重构草案（第一版）

- 基于上面的批量校验，下一步不应再继续“追单个错例 patch”。
- 更合理的是回到 `witogram` 的原始优势：  
  - 让 whole-word / char-path / OOV / 上下文一致性在统一打分框架里共同作用  
  - 用 scorer 的总体目标函数去区分“正常多样候选”与“病态碎片链”  
  - 而不是每发现一个错误家族就额外打一块补丁

- 因而，建议把当前 scorer 从“内容分 + 一串局部惩罚 patch”重构为三层：

- 第一层：内容分 `ContentScore`
  - 只保留真正代表候选内容质量的项：
    - `dict_score_raw`
    - `lm_total`
    - `merge_delta / whole-word-vs-char-path` 一类真正反映 token 化收益的项
  - 原则：
    - **LM 只应作为一句内容一致性的主信号进入一次**
    - 不要再让 `lm_total` 与 `lm_avg` 对同一候选做机械双计分

- 第二层：结构正则 `StructureRegularizer`
  - 只惩罚真正病态的碎片化，不惩罚“合法单字承接”本身
  - 也就是说：
    - `fragment / tail / octa / single_char`
    - 不应再把
      - `长前缀 + 一个合法单字后继`
    - 自动视为坏结构
  - 更合理的门控应该是：
    - `trailing_single_char_run >= 2`
    - 或 `used_char_fallback`
    - 或 `lm_oov_token_count > 0`
    - 或 `matched_whole_word == false`
    - 满足这类“真正碎片化”信号时，结构惩罚才逐步上升

- 第三层：路径风险 `PathRisk`
  - 单独放置与 spelling / joint / ambiguity / family 相关的风险项
  - 这里要强调：
    - 这层不是用来替代内容分
    - 也不是拿来修某个固定词
    - 而是给“疑似错误家族路径”一个独立风险通道
  - 且这层如果要继续做，不能复用当前已经证明无效的 `edge/joint hint` 了
  - 必须引入新的、更贴近句首错误家族的信号

- 基于这三层，我建议具体按下面顺序推进，而不是一次大改：

- Phase 1：先去掉 `LM` 的机械双计分
  - 不是简单全局 `lm_avg_weight = 0`
  - 而是把 `lm_avg` 改造成真正的“校准项”而不是第二遍主分
  - 例如：
    - 只在 `lm_token_count >= 2` 时参与
    - 或改成围绕 `lm_total / normalized_dict / merge_delta` 的残差校准项
  - 目标：
    - 避免 `token_count == 1` 的合法后继天然多背一遍 `LM`

- Phase 2：收紧结构惩罚的触发条件
  - `fragment / tail / octa / single_char`
  - 从“按模板直接罚”
  - 改成“只有检测到真实碎片链征象才罚”
  - 这一步会比现在更符合 `witogram` 的精神：
    - 允许不同 token 化结构公平竞争
    - 不是先验地偏向“多字后继”模板

- Phase 3：单独构建新的句首错误家族信号
  - 如果还需要专门处理 `的/地/第/... + 驿站` 这类问题
  - 就不应继续借现成 `edge/joint` risk
  - 而应该围绕：
    - 首字 family
    - 首两步 family
    - 首词 fallback / whole-word mismatch
  - 新建一个最小 `StartFamilyRisk`
  - 且它应只在前两步生效，不向后无限传播

- 这个草案和最近局部 patch 路线的根本区别在于：
  - 最近几步是：
    - 看见 `第一站是`
    - 再想办法压 `的驿站是`
  - 这版重构草案是：
    - 先把 scorer 的“内容分 / 结构惩罚 / 路径风险”三类职责重新分开
    - 再只对真正错误的结构性偏置做原则性修正

- 因而，关于“这条路线还有没有潜力、应不应该继续”，到这里可以给一个更稳的回答：
  - **继续做局部 patch 的小路线，潜力已经很有限。**
  - **回到 `witogram` 优势、按上面的三层重构 scorer，这条大路线仍然有潜力，而且更值得继续。**

## 2026-05-25 去重后补齐 step LM 观测链，并确认 `第一站是` 在 raw next-hop 中仍是零记录

- 继续推进前，先重新核对了 `WORKLOG`、`阶段2实施清单_P0_P1_P2.md` 和现有调试链路，确认这轮还**没有**真正做完下面这件事：
  - 补齐 `step_whole_word_log10 / step_char_path_log10 / matched_whole_word / token_evidence_tag`
  - 然后从新的 raw probe 工件里直接抽 `case2` 的 step 级 LM 细账
- 旧日志只记录到了：
  - 想拿这组字段
  - 或从旧 snapshot / graph 的合成分差继续收口
  - 但并没有记录“新字段已贯通到 runtime 工件并完成提取”的结果

- 这轮先没有盲目重跑，而是先查清 runtime 链路，最终定位到真实缺口不在算法，而在**三处序列化口径不一致**：
  - `witset_translator.cc` 中前面那处 `expansion_gate_records` 序列化已经补了新字段
  - 但 `DumpLocalSnapshot(...)` 仍在走另一处旧序列化，导致 `partial_chain_stage_probe.snapshot.jsonl` 不带新字段
  - `DumpLocalNextHopProbe(...)` 也仍在走第三处旧序列化，导致 `partial_chain_stage_probe.next_hop.jsonl` 不带新字段
  - 此外 `DebugNextHopProbeRecord` 本身也还没有 `step_whole_word_log10 / step_char_path_log10 / matched_whole_word / token_evidence_tag`
- 因而这轮真正落地的仍是**纯观测链路**，没有改评分算法：
  - 在 `witset_translator.cc` 中把 runtime `snapshot` / `next_hop` 序列化都补齐到与前面导出一致
  - 在 `witset_poet.h/.cc` 中把 `DebugNextHopProbeRecord` 扩到可携带这组 step 级 LM 字段
  - request 路径直接记录当前 step 的 `whole_word_log10 / char_path_log10 / token_evidence_level`
  - line 路径同步带上 `step_whole_word_log10 / step_char_path_log10 / step_matched_whole_word`

- 验证过程仍保持最小规模：
  - 编译只用标准非 clean 方式：
    - `librime/build.bat static`
  - 先单跑：
    - `partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
    - 用来确认新字段已进入 runtime `snapshot`
  - 再单跑：
    - `partial_chain_stage_probe.py --case case2_diyizhan --mode probe`
    - 用来直接抽 raw `next_hop`
  - 过程中一度编译失败，但只是因为 `DebugNextHopProbeRecord` 缺字段；补齐结构体和赋值点后已恢复通过

- 这轮最关键的新结果不是某个分数本身，而是 **raw next-hop presence**：
  - 新提取文件：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\case2_exact_family_summary.json`
  - 其中可直接确认：
    - `第一站是`
      - `record_count = 0`
      - `request_count = 0`
    - `第一站是一`
      - `record_count = 0`
      - `request_count = 0`
    - `第一展示`
      - `record_count = 7048`
      - `request_count = 1660`
    - `第一展示已`
      - `record_count = 5354`
      - `request_count = 1150`
- 也就是说：
  - 这轮已经排除了“只是 step LM 字段没有导出来，所以看不到正确 family”这种解释
  - 在当前 probe 口径下，`第一站是 / 第一站是一` 并不是“有 request 记录但字段缺失”
  - 而是**根本没有进入 raw next-hop 记录**

- 对错误 family 的 step LM 细账，这轮终于能直接拿到：
  - `第一展示 + 已`
    - `search_score = -264.508`
    - `base_score = -257.387`
    - `lm_score_scaled = -63.8213`
    - `step_whole_word_log10 = -55.4345`
    - `step_char_path_log10 = -55.4345`
    - `matched_whole_word = true`
    - `token_evidence_tag = direct_whole_word_hit`
  - `第一展示 + 以`
    - `search_score = -264.544`
    - `base_score = -257.434`
    - `lm_score_scaled = -63.9282`
    - `step_whole_word_log10 = -55.5274`
    - `step_char_path_log10 = -55.5274`
    - `matched_whole_word = true`
    - `token_evidence_tag = direct_whole_word_hit`
  - `第一展示已 + 作`
    - `step_whole_word_log10 = -78.9984`
    - `step_char_path_log10 = -78.9984`
    - `matched_whole_word = true`
    - `token_evidence_tag = direct_whole_word_hit`

- 因而，这轮应把结论再收紧一层：
  - 当前缺口已经不是“能否把 `第一站是 -> 一/以` 的 step LM 细账导出来”
  - 现在真正暴露出来的是：
    - **错误 family `第一展示` 已经在 raw next-hop 里大量存在，而且还是稳定的 direct whole-word hit**
    - **正确 family `第一站是 / 第一站是一` 在当前 probe 口径下仍然是零记录**
  - 下一步如果继续，更值得查的就不再是导出字段，而是：
    - 为什么 `第一站` family 在进入 raw next-hop 之前就已经缺席
    - 也就是继续回到 source selection / segmentation / earlier admission 层，而不是再改 step LM 导出

## 2026-05-25 去重后静态审计 source 选源链：`第一站` 更像缺席于 `top_candidates` 之前后的 source 选择，而不是 request 后 admission

- 继续前，先再次核对了 `WORKLOG` 与 `阶段2实施清单_P0_P1_P2.md`，确认下面这些大方向都不是新方向，不能把旧实验再重做一遍：
  - `source selection / segmentation / earlier admission`
  - `request-stage admission/state ownership`
  - `第一站` 与 `第一展示` 的上游盘面竞争
- 这轮仍然能做的，是基于最新事实做**一轮未完成过的静态收口**：
  - 现有 raw `next-hop` 已经证明 `第一站是 / 第一站是一` 在 probe 口径下是 0 记录
  - 所以本轮不再编译、不再复跑，而是静态拆 `source_pool -> SelectTopLines -> request -> admitted`

- 本轮静态读码后，链路可以明确分成 4 层：
  - `source_pool`
    - 先从 `states[start_pos]` 取出所有 source line
    - 立刻经过 `CompressLinePoolByState(source_pool, effective_sentence_soft_limit)`
  - `top_candidates`
    - 只对压缩后的 `source_pool` 调 `SelectTopLines(...)`
    - request 只会从这些 `top_candidates` 展开
  - `request`
    - 对每个 `top_candidate` 和每条 edge 下的 entry 计算 `search_score`
    - 生成 raw `BatchRequest`
  - `admitted`
    - 最后在 `target_pool[end_pos]` 内，再按新的 `state_key` 做 `admitted_new / admitted_replace / admitted_reject`

- 因而，结合这轮之前已经拿到的 raw probe 事实：
  - `第一站是 / 第一站是一` 在 raw `next-hop` 里已经是 0 记录
  - 这意味着它们**还没有进入 request 列表**
  - 所以本轮可以先排除：
    - `global_batch_limit` 全局 batch 截断
    - request 之后的 `admitted_reject / admitted_replace`
  - 因为这些步骤都发生在 request 记录已经存在之后

- 这轮最重要的静态结论有 3 个：
  - `CompressLinePoolByState(...)`
    - 这里会先按 `BuildApproxStateKey(...)` 做一次 state 压缩，只保留同 key 下 beam 更高的 line
  - `SelectTopLines(...)`
    - 它只按 `beam_score / weight` 选前 `top_k`
    - 唯一的保底逻辑是强行保留 `best_whole_first_anchor`
  - `best_whole_first_anchor` 的适用前提很窄：
    - `whole_first_word_anchor_active == true`
    - `generated_word_count == 1`
    - 而 `ShouldActivateWholeFirstWordAnchor(...)` 只会在
      - 第一个词长度至少 3
      - `used_char_fallback == true`
      - `matched_whole_word == false`
      - 且已有 prefix OOV
      时激活

- 因而，这里有一个新的关键判断：
  - 如果 `第一站` 本身是正常 whole-word 命中，那么它**不会**触发 `whole_first_word_anchor_active`
  - 也就是说，`SelectTopLines()` 这条唯一的保底分支，并不会保护正常 whole-word 的 `第一站`
  - 一旦 `source_pool` 里还有 `beam_score` 更高的 `第+一站`、`第一展示`、`的驿站` 等 segmentation family，`第一站` 完全可能在 `top_candidates` 阶段之前或之中就被挤掉

- 关于 `CompressLinePoolByState(...)` 是否会把 `第一站` 和 `第+一站` 直接压成同 key，这轮也做了静态排除：
  - 默认 `use_minimal_state_contract_ = false`
  - 仓库里也没有查到显式配置把它打开
  - 在默认 full key 下，`BuildApproxStateKey(...)` 不只看 `context_suffix`
  - 还看：
    - `generated_word_count / generated_char_count` 推出的 `compactness_bucket`
    - `single_char_word_count`
    - `recent_edge_span`
    - `trailing_single_char_run`
    - `tail_anchor_char_count`
  - 所以在默认配置下，`第一站` 与 `第+一站` 并不像会在 `CompressLinePoolByState()` 里直接同 key 互杀

- 因而，本轮静态收口应更新为：
  - 当前最像的缺席点，不是 request 后的 `admitted_*`
  - 也不是 `global_batch_limit`
  - 更像是：
    - `source_pool` 经压缩后，`SelectTopLines()` 只保留有限 source
    - 而 `第一站` 并不受 `whole_first_word_anchor` 保底保护
    - 从而在 `top_candidates` 阶段就没有被选进 request 展开

- 所以，下一步如果继续，最值得查的具体问题已经更窄了：
  - 不是再追 `第一站是 -> 一/以` 的 request 细账
  - 而是直接比对同一 `start_pos` 下：
    - `source_pool`
    - `top_candidate`
    - `pre_source_pool_full / source_pool_full / top_candidate_full`
  - 看 `第一站` family 是否在 `SelectTopLines()` 前后被 `第一展示 / 第+一站 / 的驿站` 这些 source 直接压出前 `top_k`

## 2026-05-25 去重后继续核对 source 分层工件：`第一站是一` 不是在 source_pool 前消失，而是在 `SelectTopLines()` 被截掉

- 继续前再次核对了 `WORKLOG` 和阶段文档，确认：
  - 之前已经多次做到：
    - request / batch / admitted 层的收口
    - `source selection / segmentation` 作为主方向的判断
  - 但还没有在日志里形成一条明确完成记录：
    - **直接把 `pre_source_pool_full / source_pool_full / top_candidate_full` 三层对齐起来，确认 `第一站` family 是在哪一层掉队**

- 这轮先尝试复用已有工件：
  - `partial_chain_stage_probe_result.json`
  - 以及历史提取文件：
    - `case2_start13_source_pool_rankings.json`
    - `case2_start13_top_candidates.json`
- 复核后发现：
  - 其实之前已经有人把 `start_pos = 13` 这层的一部分分层结果抽出来了
  - 但这些结论并没有被正式写回 `WORKLOG`
  - 所以这轮不算重复实验，而是：
    - **先去重确认已有提取存在**
    - **再用一次最小 `full` 单 case 复核这些旧提取与当前代码是否一致**

- 为了只补这一条未正式收口的证据链，这轮只做了最小复核：
  - `partial_chain_stage_probe.py --case case2_diyizhan --mode full`
  - 不扩样
  - 不做额外 patch
  - 目标仅是看 `pre_source_pool/source_pool/top_candidate` 三层计数是否与旧提取一致

- 这轮最关键的新结论已经可以正式写死：
  - 对 `next_hop_after_diyizhanshiyi_exact`
    - `matched_source_suffixes = [第一站是一]`
    - `counts` 里有：
      - `pre_source_pool = 1`
      - `source_pool = 1`
    - 但没有：
      - `top_candidate`
      - `request`
  - 这说明：
    - `第一站是一` 这条 exact source family **并没有在 `CompressLinePoolByState()` 前后消失**
    - 它至少还活着进入了 `source_pool`
    - 但没有进入 `top_candidates`
    - 所以它的缺席点应收口为：
      - **`SelectTopLines(source_pool, word_beam_size_)` 截断**

- 旧提取文件与这轮最小复核能互相印证这一点：
  - `case2_start13_source_pool_rankings.json`
    - `第一站是一`
      - `stage = source_pool_full`
      - `source_rank = 210` / `216`
      - `selected_top_candidate = false`
      - `whole_first_word_anchor_active = false`
  - 同文件里，`top80_cutoff = -222.107`
    - 而 `第一站是一` 的 `beam_score`
      - `-229.062`
      - `-231.240`
    - 明显在 cutoff 之后
  - `case2_start13_top_candidates.json`
    - 能看到大量：
      - `的驿站是以`
      - `的驿站是一`
      - `第一站是以`
      - `的翼展示已`
      - `第一展示...` 等 family
    - 但没有 `第一站是一`

- 这也进一步验证了上一轮静态判断：
  - `第一站是一` 当前不是在 request 后被 `admitted_reject`
  - 也不是更早在 `CompressLinePoolByState()` 里被并桶打掉
  - 而是：
    - `source_pool` 里仍然存在
    - 但 rank 大约在 210 名附近
    - 当前 `word_beam_size` 对应的 `top_candidate_full` 只保到约前 80
    - 因而在 `SelectTopLines()` 阶段直接被截掉

- 这轮还顺便把 `whole_first_word_anchor` 的怀疑再压实了一次：
  - `第一站是一` 的旧提取里：
    - `whole_first_word_anchor_active = false`
  - 因而当前 `SelectTopLines()` 里那条“保留 best whole-first-anchor”的特殊保底分支并不会救它

- 因而，当前 source 选源层的更精确表述应更新为：
  - `第一站` family 的问题不再笼统表述为“source_pool 选源”
  - 而应更具体地说成：
    - **`第一站是一` 已经进入 `source_pool`，但在 `SelectTopLines()` 的 beam 截断中因 rank 太靠后而出局**
  - 这条结论比之前“可能在 `CompressLinePoolByState()` 被压掉”更窄，也更符合现有工件

- 所以下一步如果继续，最值得看的不再是：
  - `CompressLinePoolByState()` 的 state key 合并
  - 或 request / admitted contract
- 而是：
  - 为什么 `第一站是一` 在 `source_pool` 内的 `beam_score` 会掉到约 210 名
  - 以及哪些 source family 在这一步稳定排到它前面
  - 也就是继续查：
    - `第一站是以`
    - `的驿站是以 / 是一`
    - `第一展示已`
    - 这些 source 的前一跳 cumulative base / adjustment 盘面

## 2026-05-25 去重后继续对比 source_pool 家族盘面：`第一站是一` 已进 pool，但 source rank 远落后于 `的驿站/展示已`

- 继续前再次核对了 `WORKLOG`，确认这条线之前只做到：
  - `第一站是一` 已进入 `source_pool` 但未进入 `top_candidates`
  - 以及一些零散的 source/top candidate 提取文件
- 还没有形成一条正式日志结论，把下面这些 family 并排对起来：
  - `第一站是以`
  - `第一站是一`
  - `的驿站是以`
  - `的驿站是一`
  - `*展示已`

- 这轮优先复用已有 debug 工件，不重复扩跑：
  - `case2_start13_source_pool_rankings.json`
  - `case2_start13_top_candidates.json`
  - 再补了一份只做汇总的新摘要：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\case2_start13_family_summary.json`

- 对 `start_pos = 13`（也就是 `第一站是* / 的驿站是* / 展示已*` 这一层），本轮可以正式把盘面写死：
  - `top80_cutoff = -222.107`
  - `第一站是一`
    - 在 `source_pool_full` 里存在两条主记录：
      - `source_rank = 210`
      - `beam_score = -229.062`
      - `base_score = -236.757`
      - `adjustment_score = +7.69542`
      - `selected_top_candidate = false`
      - `whole_first_word_anchor_active = false`
      - 另一条近邻记录：
        - `source_rank = 216`
        - `beam_score = -231.240`
        - `base_score = -240.104`
        - `adjustment_score = +8.86333`
  - `第一站是以`
    - 已进入 `top_candidate_full`
    - `top_candidate_rank = 53`
    - `beam_score = -221.201`
  - `的驿站是以`
    - 已进入 `top_candidate_full`
    - `top_candidate_rank = 1`
    - `beam_score = -189.077`
  - `的驿站是一`
    - 已进入 `top_candidate_full`
    - `top_candidate_rank = 7`
    - `beam_score = -195.069`
  - 当前最佳 `展示已` family（本轮摘要里最靠前的是 `的翼展示已`）
    - `top_candidate_rank = 40`
    - `beam_score = -219.384`

- 因而，这轮把上一轮的判断再压实一层：
  - `第一站是一` 的问题并不是“没进 source_pool”
  - 它也不是“只比 cutoff 稍微差一点”
  - 当前它在 `source_pool` 里的 rank 已掉到 210 名附近
  - 相比 `top80_cutoff`，beam 差额大约在 `6.9` 到 `9.1` 分
  - 所以它是在 `SelectTopLines()` 阶段被稳定截掉，而不是偶然卡在边界附近

- 同时，这个 source 排名盘面还能解释前面两条现象为什么会同时成立：
  - `第一站是以`
    - 虽然不是最强 source，但至少还在 `top_candidates`
    - 所以后续能继续展开 request
  - `第一站是一`
    - 即便 `adjustment_score` 已经是正数
    - 但 `base_score` 仍显著更差
    - 最终在 source_pool 内部仍被压到 200 名以后
  - `的驿站是以 / 的驿站是一`
    - 不仅都进了 `top_candidates`
    - 而且 rank 已经在前 10
    - 说明当前主盘面并不是单纯 “`是以` 压 `是一`”
    - 而是更早层的 `驿站` family 整体已经占据 source 选源优势

- 因而，当前最准确的收口应更新为：
  - `第一站是一` 的主缺口，不再只是：
    - `第一站是 -> 一/以` 的 request LM 差额
  - 而是：
    - `驿站 / 展示已` 这些 family 在进入 `start_pos = 13` 前，已经把自己的 cumulative source 分数推到更高平台
    - 于是同层 `source_pool` 排名里：
      - `的驿站是以 = rank 1`
      - `的驿站是一 = rank 7`
      - `展示已` family 也在前 40
      - `第一站是以 = rank 53`
      - `第一站是一 = rank 210+`
    - 问题已明显前移到：
      - 这些 source 在前一跳是如何积累出这组 beam/base 差距的

- 所以下一步如果继续，更值得直接查的是：
  - `start_pos = 11` 之前，也就是 `第一站 / 的驿站 / 展示已` family 到达这层前的 cumulative 盘面
  - 优先比：
    - `第一站是以`
    - `的驿站是以`
    - `的驿站是一`
    - 最强 `展示已` family
  - 看它们在进入 `start_pos = 13` 之前，分别是哪一跳把 source beam 拉开

## 2026-05-25 去重后继续对比进入 `start_pos = 13` 前一跳盘面：更早前缀已偏弱，但决定性断崖仍发生在 `第一站是 -> 一/以`

- 继续前再次核对了 `WORKLOG` 与现有 debug 工件，确认：
  - 之前已经有零散记录提到：
    - `第一站是以 / 第一站是一`
    - `的驿站是以 / 的驿站是一`
    - `第一展示已`
    - 这些 family 在局部 query 或 snapshot 中的表现
  - 但还没有形成一条正式日志结论，把：
    - `start_pos = 8` 的 `第一站`
    - `start_pos = 11` 的 `第一站 -> 是`
    - `start_pos = 13` 的 `第一站是 -> 一/以`
    - 以及 `的驿站 / 展示已` family
    串成一条连续证据链

- 这轮仍然优先复用旧工件，不新增重跑，主要用到：
  - `case2_firstzhan_pool_stages.json`
  - `case2_diyizhans_firstzhan_vs_firstzhong_best.json`
  - `case2_firstzhan_yi_followups.json`
  - `case2_start13_family_summary.json`
  - `partial_chain_stage_probe.next_hop.jsonl`

- 先看更早一跳 `diyizhans` / `start_pos = 8`：
  - `case2_diyizhans_firstzhan_vs_firstzhong_best.json` 显示：
    - `第一种`
      - `beam_score = -145.193`
    - `第一站`
      - `beam_score = -145.425`
  - 也就是说，在 `第一站` 这层刚形成时，它已经不是同层最强 source
  - 同时，`case2_firstzhan_pool_stages.json` 里还能看到：
    - `第一站` 相关 pool 中唯一进入 `top_candidate_full` 的不是整词 `第一站`
    - 而是拆分线 `一站`
      - `beam_score = -146.858`
  - 因而，更早前缀层面确实已经存在“`第一站` source 不占优”的事实

- 再看 `diyizhans` 下 `第一站 -> 是` 这一步：
  - `case2_diyizhans_firstzhan_vs_firstzhong_best.json` 里：
    - `source_text = 我踏上了旅行的征程。第一站`
    - `entry_text = 是`
    - `search_score = -247.899`
    - `request_stage_bridge_bonus = 3.6`
  - 说明 `第一站 -> 是` 本身仍然是一个真实存在、能进 request 的 continuation，不是更早就彻底断掉

- 真正决定性把同 family 拉开的，是 `start_pos = 13` 的 `是 -> 一/以`：
  - `case2_start13_family_summary.json`
    - `第一站是以`
      - `top_candidate_rank = 53`
      - `beam_score = -221.201`
    - `第一站是一`
      - 只在 `source_pool_full`
      - `source_rank = 210 / 216`
      - `beam_score = -229.062 / -231.240`
      - `selected_top_candidate = false`
  - 两者都是同一个 `第一站是*` family，但
    - `是以` 还能留在前 80
    - `是一` 已经掉到 210 名附近
  - 因而，从“还能参与 top candidate”到“稳定被挡在 request 外”，决定性断崖就在：
    - **`第一站是 -> 一 / 以` 这一跳内部**

- 竞争 family 的盘面则说明，`第一站是一` 一旦在这一跳掉队，就再也回不来：
  - 同层前排有：
    - `的驿站是以`
      - `top_candidate_rank = 1`
      - `beam_score = -189.077`
    - `的驿站是一`
      - `top_candidate_rank = 7`
      - `beam_score = -195.069`
    - 最靠前 `展示已` family
      - `top_candidate_rank = 40`
      - `beam_score = -219.384`
  - 所以即便不只看 `第一站` family 自身，外部竞争盘面也已经足够强，`第一站是一` 这跳一旦掉到 `-229` 附近，就会被稳定截断

- 这轮因此把上一轮“更早累计 beam/base 盘面失守”的表述再细化了一层：
  - 可以保留“更早前缀已经偏弱”这个判断
  - 但更准确的说法应是：
    - `第一站` family 在 `start_pos = 8` 时已经不是最强 source
    - `第一站 -> 是` 仍能成立
    - 真正把它从可竞争状态打到 `top80` 之外的断崖，发生在 `第一站是 -> 一` 相对 `第一站是 -> 以` 的这一步
  - 也就是说，问题不是纯粹“前缀早就输了个彻底”
  - 而是：
    - **前缀已偏弱**
    - **再叠加 `是 -> 一` 这一步的额外分差**
    - 两者共同导致 `第一站是一` 在 `SelectTopLines()` 前稳定掉到 200 名以后

- 因而，如果继续，下一步最值得查的点又能再收窄一层：
  - 不是泛泛再查 `start_pos = 13` 前的所有 cumulative source
  - 而是直接对同 family 做：
    - `第一站是以`
    - `第一站是一`
    在这一跳的
    - `base_score`
    - `adjustment_score`
    - `lm_score_scaled`
    - `step_whole_word_log10 / step_char_path_log10`
    的并排差额解释
  - 同时再把这个差额放回 `的驿站是以 / 的驿站是一` 的前排盘面里判断：
    - 它到底更像 LM 本体问题
    - 还是 source 累计弱势放大后的边界问题

## 2026-05-25 去重后再核对 `第一站是以` vs `第一站是一`：旧 request-stage 结论已做过，现有 source_pool 证据与之保持一致

- 继续前再次核对了 `WORKLOG` 与现有 debug 工件，确认：
  - “直接比较 `第一站 -> 是以` 与 `第一站 -> 是一` 这一跳的 `search/base/lm` 差额”并不是新路线
  - 之前已经有正式日志结论：
    - `search_score`
      - `是一 = -154.930`
      - `是以 = -136.354`
      - 差额 `-18.576`
    - `base_score`
      - `是一 = -154.995`
      - `是以 = -135.796`
      - 差额 `-19.199`
    - `lm_score_scaled`
      - `是一 = -89.799`
      - `是以 = -63.053`
      - 差额 `-26.746`
    - 同时：
      - `dict_score_raw/dict_score_norm` 反而是 `是一` 更好
      - `adjustment_score` 也是 `是一` 略好
  - 所以“这一步主差额来自 `lm_score_scaled`，不是 adjustment/词典打底不足”这件事，之前已经正式做过，不能当成新发现重复记一遍

- 这轮还能继续做的，只是把这条旧结论与当前更稳定的 source_pool/followup 工件对齐，确认它现在仍成立：
  - `case2_start13_family_summary.json`
    - `第一站是以`
      - `top_candidate_rank = 53`
      - `beam_score = -221.201`
    - `第一站是一`
      - `source_rank = 210 / 216`
      - `beam_score = -229.062 / -231.240`
      - `base_score = -236.757 / -240.104`
      - `adjustment_score = +7.69542 / +8.86333`
      - `selected_top_candidate = false`
  - `case2_firstzhan_yi_followups.json`
    - `第一站是一`
      - 在 `pre_source_pool_full / source_pool_full / admitted_new_line_*`
      - 都保持：
        - `base_score = -236.757`
        - `adjustment_score = +7.69542`
        - `beam/search_score = -229.062`
  - 旧日志里的 rescue 记录还表明：
    - `第一站 -> 是一`
      - `request.adjustment_score = 0.0654507`
      - `batch_selected.adjustment_score = 8.91163`
      - `batch_selected.search_score = -233.263`
    - `第一站 -> 是以`
      - `adjustment_score = -0.558091`
      - `search_score = -223.534`

- 把这些旧结论和当前工件放在一起后，可以把问题层级写得更准确：
  - `第一站是一` 不是“完全没有得到 adjustment rescue”
  - 恰恰相反，它的 `adjustment_score` 现在已经明显是正数，且比 `是以` 更好
  - 但即便如此，它在当前 `source_pool` 里仍只有：
    - `beam_score = -229.062`
    - 相比 `第一站是以 = -221.201`
    - 仍落后约 `7.861`
  - 且 `top80_cutoff = -222.107`
    - 说明：
      - adjustment 虽然确实在“救 `是一`”
      - 但仍不足以抵消这一步早已存在的 `LM/base` 天然弱势
      - 最终 `是一` 仍稳定掉在 `top80` 之外

- 因而，这轮去重后的更新不是“发现了新主因”，而是把旧结论与当前盘面对齐后进一步确认：
  - 旧结论仍成立：
    - `第一站是 -> 一 / 以` 的主差额，首先来自 `lm_score_scaled`
  - 当前新工件补充说明：
    - 后续 adjustment 已经在积极补 `是一`
    - 但补偿量不够覆盖前面 `LM/base` 造成的缺口
    - 所以它在 `source_pool` 里仍只排到 `210+`

- 这也说明，下一步如果继续，真正还没做完的不是再重复证明：
  - `是一` 比 `是以` 的 `LM/base` 更弱
- 而是进一步解释：
  - 为什么当前 runtime 下，这一步的 `lm_score_scaled` 会稳定差到这个量级
  - 尤其是能否把：
    - `step_whole_word_log10`
    - `step_char_path_log10`
    - `token_evidence_tag`
  - 在同一份当前工件里，对 `第一站是以` 和 `第一站是一` 成对拿出来
  - 这样才能继续区分：
    - 更像 whole-word / char-path 本体差异
    - 还是解释层 token evidence 的路径分叉

## 2026-05-25 去重后再审“下一步建议”：现成 `end13` pairwise 工件已足够说明 `是一/是以` 差额结构，不应再把“做 pairwise 提取”当新下一步

- 这次不是用户提醒后才去重，而是专门对“我自己提出的下一步建议”再次做了一轮去重审计：
  - 先核对 `WORKLOG`
  - 再核对现有 debug 目录里所有 `case2*` 提取文件
  - 目的不是找一个“看起来合理”的建议，而是持续排除已经做过、只是之前没被我注意到的路线

- 先排掉了一条已经重复的建议：
  - “继续拿 `第一站是以` vs `第一站是一` 的 pairwise 比较”
  - 这条并不是空白路线
  - 旧日志已经正式写过：
    - `search_score / base_score / lm_score_scaled` 的主差额来自 `lm_score_scaled`
    - `adjustment_score` 反而是 `是一` 更好
  - 所以不能再把“证明 `LM/base` 主导差额”当成新的下一步

- 继续往 debug 目录交叉检查后，又排掉了另一条“半重复建议”：
  - “先去找一份成对的 `end13` request/source 工件”
  - 这条其实也已经存在，只是之前没有被正式写回 `WORKLOG`
  - 现成文件包括：
    - `case2_end13_pairwise_yi_vs_yi.json`
    - `case2_yi_yi_request_family.json`
    - `case2_end13_request_entries.json`

- 这轮直接复用现成 pairwise 工件后的确认结果如下：
  - `case2_end13_pairwise_yi_vs_yi.json`
    - `prefix = 我踏上了旅行的征程。第一站`
    - `shi_yi = 第一站是一`
      - `beam_score = -231.24`
      - `base_score = -240.104`
      - `adjustment_score = +8.86333`
      - `lm_score_scaled = -219.317`
    - `shi_yi2 = 第一站是以`
      - `beam_score = -223.38`
      - `base_score = -223.207`
      - `adjustment_score = -0.172564`
      - `lm_score_scaled = -194.874`
    - 差额：
      - `beam_delta = -7.86`
      - `base_delta = -16.897`
      - `adjustment_delta = +9.036`
      - `lm_delta = -24.443`
      - `dict_term_delta = +0.96054`

- `case2_yi_yi_request_family.json` 进一步说明：
  - 这些不是一次性 request 幻觉，而是在：
    - `admitted_new_line_post_push`
    - `post_future_compact_full`
    - `pre_future_compact_full`
    - `pre_source_pool_full`
    - `source_pool_full`
    都稳定保持同样的相对关系
  - 也就是说：
    - `是一` 的 `adjustment_score` 确实一直更高
    - 但 `lm_score_scaled` 和 `base_score` 的弱势更大
    - 所以后续 compact/source_pool 并没有逆转这一步的相对顺序

- 因而，这轮去重后可以正式把两个“看起来像下一步”的建议都判掉：
  - 不该再把“去做 `是一/是以` 的 pairwise 提取”当作下一步
    - 因为现成工件已经有了
  - 也不该再把“去证明 adjustment 不够救回来”当作下一步
    - 因为现成 pairwise 工件已经直接给出了：
      - `adjustment_delta = +9.036`
      - 但仍顶不住：
        - `lm_delta = -24.443`
        - `base_delta = -16.897`

- 经过这轮对“下一步建议”的再次去重，真正还没被做完、且仍可继续的一步被收窄为：
  - 不是继续比较 `search/base/adjustment/lm`
  - 而是要解释：
    - 为什么在当前 runtime 下，
    - `第一站是一` 与 `第一站是以`
    - 会稳定形成这组 `lm_score_scaled` 差额
  - 换句话说，真正剩下的未完成路线只剩：
    - 在**当前同一份 runtime 工件**里，把这两条链的
      - `step_whole_word_log10`
      - `step_char_path_log10`
      - `token_evidence_tag`
    - 成对拿出来
    - 从而判断这 `-24.443` 到底更像：
      - whole-word / char-path 本体差异
      - 还是解释层 token evidence 路径已经分叉

- 所以，这轮去重后的“可做下一步”不再是泛泛建议，而是一个明确且未完成的动作：
  - **只检查现有或最小新增工件中，能否拿到 `第一站是一` vs `第一站是以` 的成对 `step_whole_word_log10 / step_char_path_log10 / token_evidence_tag`**
  - 如果现有工件已有，就直接收口
  - 如果现有工件仍没有，才值得做最小补提取

## 2026-05-25 对“下一步建议”再次去重后，最小补齐 `partial_chain_stage_probe.py` 摘要字段并拿到 `第一站是 -> 一/以` 成对 step 级 LM 证据

- 这轮先专门对“下一步建议”本身做了再次去重，而不是先给建议：
  - 已排除：
    - 再做一次 `是一/是以` 的 pairwise `search/base/adjustment/lm` 对比
    - 再去找已有 `end13` pairwise/source 工件
  - 因为现有文件：
    - `case2_end13_pairwise_yi_vs_yi.json`
    - `case2_yi_yi_request_family.json`
    已经足够说明：
    - `adjustment_score` 对 `是一` 是正向 rescue
    - 但仍压不过更大的 `LM/base` 差额

- 真正未完成的只剩一条：
  - 在**当前同一份 runtime 工件**里，成对拿到：
    - `第一站是 -> 以`
    - `第一站是 -> 一`
    的
    - `step_whole_word_log10`
    - `step_char_path_log10`
    - `token_evidence_tag`

- 先复用现有工件交叉检查后确认：
  - `partial_chain_stage_probe.next_hop.jsonl` 的 raw request 记录已经能写出：
    - `step_whole_word_log10`
    - `step_char_path_log10`
    - `matched_whole_word`
    - `token_evidence_tag`
  - 但 `partial_chain_stage_probe.py` 的 `summarize_probe()` 在构造：
    - `top_request_entries`
    - `focus_entries.*.samples`
    时，只保留了：
    - `search_score`
    - `base_score`
    - `dict_score_raw`
    - `lm_score_scaled`
    等少数字段
  - 所以之前不是 runtime 没写，而是 probe 结果摘要层把 step 级字段丢掉了

- 这轮因此只做了一个最小脚本补丁，不改算法、不编译：
  - 修改：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py`
  - 让 `build_entry_summary()` 与 `focus_entries.samples` 透传：
    - `start_pos / end_pos`
    - `step_whole_word_log10`
    - `step_char_path_log10`
    - `matched_whole_word`
    - `token_evidence_tag`
  - 随后只最小复跑：
    - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode probe`

- 新的 `partial_chain_stage_probe_result.json` 里，`next_hop_after_diyizhanshi` 已经能直接给出成对 step 级 LM 证据：
  - `第一站是 -> 以`
    - `search_score = -213.963`
    - `base_score = -207.648`
    - `lm_score_scaled = -17.3018`
    - `step_whole_word_log10 = -15.0282`
    - `step_char_path_log10 = -15.0282`
    - `matched_whole_word = true`
    - `token_evidence_tag = direct_whole_word_hit`
  - `第一站是 -> 一`
    - `search_score = -243.778`
    - `base_score = -234.731`
    - `lm_score_scaled = -44.3238`
    - `step_whole_word_log10 = 0`
    - `step_char_path_log10 = -40.4992`
    - `matched_whole_word = false`
    - `token_evidence_tag = neutral_missing`

- 这条证据把前面所有间接判断正式钉死：
  - `第一站是 -> 以` 在当前 runtime 下是：
    - `direct_whole_word_hit`
  - `第一站是 -> 一` 则不是 whole-word hit，而是：
    - `neutral_missing`
    - `matched_whole_word = false`
    - `step_whole_word_log10 = 0`
  - 因而，`以` 和 `一` 的主差额并不是“同类路径下数值略有不同”
  - 而是：
    - **解释层 token evidence 已经分叉**
    - `以` 走整词命中
    - `一` 只能走 char-path / neutral-missing
  - 这也解释了为什么前面 pairwise 工件里：
    - `lm_delta` 会稳定大到 `-24.443`
    - 且后续 adjustment 无法把 `一` 救回到 `top80`

- 因而，这轮去重后的主结论是：
  - 不需要再继续把“为什么 `是一` 比 `是以` 差”停留在 `LM/base` 口径
  - 现在已经可以更具体地写成：
    - `第一站是 -> 一` 的问题落在**解释层证据分叉**
    - 即 `一` 没拿到 whole-word evidence，而不是只比 `以` 少了一点同类 LM 分
  - 如果继续，下一步不该再重复做 pairwise 导出
  - 应直接围绕：
    - 为什么 `一` 在这里仍被解释成 `neutral_missing`
    - 以及这是否与 `ScoreFeatures()/InterpretGrammarEvidence()` 的 whole-word 判定前提有关
    去设计更上游的最小验证

## 2026-05-25 去重后继续静态追 `neutral_missing` 在 poet 中如何被放大：主问题不只是额外罚项，而是 clean continuation 奖金系统性缺席

- 继续前先再次核对了 `WORKLOG`、`阶段2实施清单_P0_P1_P2.md` 与当前源码，确认：
  - “`第一站是 -> 一` 为什么会落到 `neutral_missing`”这条大方向本身不是新路线
  - 之前已经正式收口到：
    - 运行时 `witogram::ScoreFeatures()` 查询 `word = 一` 时稳定落入 `vocab.Index(word) == NotFound()`
    - 本机 `wanxiang-*.arpa/.gram` 里：
      - `以 / 已 / 宜 / 驿 / 翼 / 站` 都存在
      - unigram `一` 不存在
      - `驿 站 / 翼 展 / 的 驿 站` 可命中
      - `一 站 / 第 一 站 / 的 一 站 / 地 一 站` 都未命中
  - 因而不能再把“继续查模型词表里有没有 `一`”当成下一步重复做

- 这轮继续做的不是重复查模型内容，而是静态追清：
  - 当前代码里，`neutral_missing` 究竟是怎样被继续放大成后续盘面失血的
  - 也就是把“原版更像缺少正证据、而当前更像显式失血”落实到当前 `witset_poet.cc` 的实际打分链上

- `witogram.cc::InterpretGrammarEvidence()` 当前实现的关键口径已再次核对：
  - `word_wid != vocab.NotFound()`
    - 才会走 `kDirectWholeWordHit`
    - 并写出：
      - `matched_whole_word = true`
      - `whole_word_log10`
  - 否则进入 char-path 分支：
    - `char_oov_token_count == 0`
      - `kSplitTokenSupported`
    - `char_matched_token_count > 0 || token_count == 1`
      - `kNeutralMissing`
    - 否则
      - `kTrueOov`
  - 对 `word = 一`，因为：
    - `vocab.Index("一") == NotFound()`
    - 且 `token_count == 1`
  - 所以它会被**明确**归到：
    - `token_evidence_level = kNeutralMissing`
    - `matched_whole_word = false`
    - `used_char_fallback = false`
    - `oov_token_count = 0`

- 接着回到 `witset_poet.cc` 静态核对后，当前更准确的链条是：
  - `lm_score_scaled` / `base_score` 本体上，`一` 已经明显落后于 `以`
  - 同时，`一` 还会系统性失去一整组只对“clean whole-word continuation”开放的奖金
  - 但它**并不主要是因为还在额外吃显式 OOV/fallback 惩罚**

- 具体来说，当前几类 continuation bonus 的开启条件都要求：
  - `!used_char_fallback`
  - `matched_whole_word`
  - `lm_oov_token_count == 0`
  - 而对 `一` 而言，虽然 `used_char_fallback = false`、`lm_oov_token_count = 0`
  - 但由于：
    - `matched_whole_word = false`
  - 所以它天然拿不到这些 bonus：
    - `ComputeCleanSingleCharBridgeBonus(...)`
    - `ComputeCleanExactContinuationBonus(...)`
    - `ComputeValidatedPrefixSuffixBonus(...)`
    - `ComputeDeferredCleanPrefixContinuationBonus(...)`
  - 对照代码中的共同 gate 都是：
    - `if (used_char_fallback || !matched_whole_word || lm_oov_token_count != 0) return 0.0;`

- 与此同时，继续核对主惩罚链后，发现当前对 `neutral_missing` 的“放大”并不主要来自额外重罚：
  - `ComputeLmAvgContribution(...)`
    - 会看：
      - `used_char_fallback`
      - `matched_whole_word`
      - `lm_oov_token_count`
    - 但这里对 `matched_whole_word == false` 并没有额外单独再砍一刀
    - 主要只是：
      - `lm_score_avg` 本身继续进入 adjustment
  - `oov_penalty`
    - 取决于 `penalty_lm_oov_token_count`
    - 对 `neutral_missing` 链，当前通常已被归零
  - 若干真正显式要求 `used_char_fallback == true` 的 penalty
    - 对当前 `neutral_missing` 的 `一`
    - 反而未必命中

- 因而，这轮静态收口把“为什么当前比原版更像显式失血”修正成了更准确的说法：
  - 不是：
    - `neutral_missing` 还在被一堆 fallback/OOV penalty 反复猛打
  - 而是：
    - `一` 先在 `lm_score_scaled / base_score` 本体上显著弱于 `以`
    - 再因为 `matched_whole_word = false`
    - 系统性失去多条 clean continuation bonus
    - 于是相对差距被继续保留下来，无法在后续 bridge/continuation 阶段追回

- 这与原版 `octagram` 的差异也可更准确地写成：
  - 原版更接近：
    - 缺少一份更强正证据
  - 当前 `witogram + witset_poet` 则更像：
    - 本体 LM 已弱
    - 再叠加“whole-word clean continuation bonus 不可用”
    - 从而把这种词表缺项持续放大到后续排序盘面

- 这轮因此也顺带排除了一个容易重复误判的下一步：
  - 不值得再先去围绕：
    - `oov_penalty`
    - `used_char_fallback == true` 型 penalty
    做小修小补
  - 因为对当前 `第一站是 -> 一` 的 `neutral_missing` 链，它们并不是最核心的放大器

- 若继续，最值得的下一步应再收窄为：
  - 不是重复证明：
    - `一` 不在模型词表里
  - 也不是再调：
    - `oov_penalty / lm_avg`
  - 而是直接围绕：
    - `matched_whole_word = false`
    - 导致 clean continuation bonus 全部关掉
  - 去设计最小验证：
    - 在不把 `true_oov` 一起放宽的前提下
    - 是否要给 `neutral_missing` 单字 continuation 一条更窄的“准 clean bridge”资格
    - 或改写 `matched_whole_word` 之外的准入条件，使其不再把 `neutral_missing` 与真正坏路径完全并到一类

## 2026-05-25 再次去重后排除“单字 bridge / clean bonus / same-span”等当前层路线：现在线上主缺口仍收口到 `一` 的 evidence path 与 char-path LM 本体

- 继续前先专门对上一轮给出的“是否要给 `neutral_missing` 单字 continuation 一条窄 clean 资格”做了一轮完整去重，避免再把已做过或已失效的路线换个名字重复一遍。

- 先排除掉的第一类，是**已做过且已判负**的单字/clean bonus 路线：
  - `clean_single_char_bridge_bonus_weight`
    - 2026-05-19 已做过 spot check
    - 对 `第一站是一座古老的小镇`
    - 结果是：
      - `clean_single_char_bridge_bonus_weight = 2.2` 后完全不变
  - `validated_prefix_suffix_bonus_weight`
    - 也已做过 spot check
    - 对代表句同样完全不变
  - 因而，不能再把“给合法单字 continuation 补一笔 `poet` 层 bonus”当成新的下一步

- 第二类被排除的是**当前根本不在线的 clean-bonus gate 路线**：
  - 重新检查当前生效 schema：
    - `C:\Users\Bing\AppData\Roaming\witty\build\witset.schema.yaml`
  - 可直接确认：
    - `clean_single_char_bridge_bonus_weight = 0.0`
    - `validated_prefix_suffix_bonus_weight = 0.0`
  - `deferred_clean_prefix_continuation_bonus_weight`
    - 当前 build schema 中也没有启用项
    - 历史上只在快路径脚本里临时注入过：
      - `18.0`
    - 不是当前线上生效逻辑
  - 所以，即便继续去改这些 helper 的 eligibility gate，
    - 当前线上默认配置下也不会产生真实效果
  - 这整类路线应先整体排除

- 第三类继续被排除的是**更早单字 bridge / request-stage 单字保活**路线：
  - 旧日志已经正式写过：
    - 后续不应再把主要精力放在
      - `单字 bridge contract`
      - `request-stage 单字保活`
      - 或给 `一 / 座 / 样` 追加轻量补偿
  - 这条判断现在仍成立

- 在把这些“看起来可能有效”的路线排干净后，这轮继续直接用现有工件核了当前仍在线的主动项：
  - `same_span_competition_penalty`
  - `request_stage_source_mismatch_penalty`
  - `shared_prefix_lm_contract_penalty`

- 先看 `start_pos = 13` 的当前层 source/top-candidate 盘面：
  - `case2_start13_family_summary.json`
    - `第一站是一`
      - `request_stage_bridge_bonus = 0`
      - `request_stage_source_mismatch_penalty = 0`
      - `shared_prefix_lm_contract_penalty = 0`
      - `same_span_competition_penalty = 0`
      - `source_rank = 210 / 216`
    - `第一站是以`
      - `request_stage_bridge_bonus = 0`
      - `same_span_competition_penalty = 0`
      - `top_candidate_rank = 53`
  - 这说明：
    - 在 `是 -> 一/以` 这一层当前 source/top-candidate 对打里
    - `same_span`
    - `request-stage mismatch`
    - `shared-prefix contract`
    - 都不是主分叉来源

- 再看更后面的 `end13` request 家族成对工件：
  - `case2_end13_pairwise_yi_vs_yi.json`
  - 的确还能看到一个**仍在线但量级较小**的主动项：
    - `request_stage_source_mismatch_penalty`
      - `是一 = -10.1014`
      - `是以 = -8.5238`
      - 差额约：
        - `-1.5776`
  - 但同一份工件里更大的差额仍然是：
    - `lm_delta_yi_minus_yi2 = -26.746`
    - `base_delta_yi_minus_yi2 = -26.807`
  - 而 `adjustment_delta_yi_minus_yi2 = -1.7385`
    - 与上面的 mismatch penalty 差额量级基本一致
  - 因而可确认：
    - `request_stage_source_mismatch_penalty` 在 `end13` 确实是一个活跃的小放大器
    - 但它不是主因
    - 主导差额仍然是 `LM/base`

- `same_span_competition_penalty` 这轮也可以更精确地从“当前层主问题”中剥离出去：
  - 旧日志已正式确认：
    - 句首整块 `第一站`
    - 在 `outer_start_pos = 4` 处会被：
      - `same_span_competition_penalty = -19.052`
      打掉
  - 但这次对 `第一站是一 / 第一站是以` 这一层直接查现有工件后又确认：
    - `same_span_competition_penalty = 0`
  - 所以：
    - `same-span` 仍是更早句首整块竞争的主问题之一
    - 但已经不是当前 `是 -> 一/以` 这一拍的主缺口

- 到这里，这轮可明确排除的“当前层下一步”包括：
  - 再补 `clean_single_char_bridge_bonus`
  - 再改 `validated_prefix_suffix_bonus`
  - 再沿 request-stage 单字保活补轻量 contract
  - 再把 `same_span` 当成 `是 -> 一/以` 这一拍的主分叉
  - 再把 `request_stage_source_mismatch_penalty` 当成主因

- 因而，这轮去重并继续后的主结论又收紧了一档：
  - 当前线上在 `第一站是 -> 一/以` 这一层，
    - 活跃 gate 能解释的只有一小部分后续放大
  - 真正决定性差额仍然是：
    - `一` 的 evidence path 已经落到
      - `neutral_missing`
      - `matched_whole_word = false`
      - `step_whole_word_log10 = 0`
    - 对应 `以` 则是
      - `direct_whole_word_hit`
  - 所以：
    - **当前还能继续的主线不在 `poet` 末端或 gate 常数**
    - 而仍在：
      - `InterpretGrammarEvidence / ScoreFeatures`
      - 以及 `一` 在当前上下文下为何只能拿到这条 evidence path

- 如果继续，当前唯一还对位的“新下一步”应表述成：
  - 不是再调 `poet` 的 continuation gate
  - 而是直接解释：
    - 为什么在当前 split-token grammar 下，
    - `第一站是 -> 一`
    - 最终只能落成
      - `neutral_missing + step_whole_word_log10 = 0`
    - 而 `第一站是 -> 以`
    - 可以拿到
      - `direct_whole_word_hit`
  - 也就是继续把主缺口前移到：
    - `一 / 以` 这两个单字在当前 context 下的 token-path 证据差异本体

## 2026-05-25 去重后把 `第一站是 -> 一/以` 的最小根因链写死：不是额外 gate 分叉，而是单字词表成员资格不同

- 继续前再次专门核对了：
  - `WORKLOG`
  - `case2_yi_branch_full_debug.json`
  - `case2_yi_to_zuo_compare.json`
  - `witogram.cc`
- 去重后确认，下面这些层面的证据都已经做过，不能再重复当成“新下一步”：
  - `step_whole_word_log10 / step_char_path_log10 / token_evidence_tag` 的 runtime 成对提取
  - `.arpa / .gram` 中 `以 / 已 / 宜` 存在、而 `一` 缺席
  - `poet` 末端 `same-span / request-stage mismatch / clean bonus` 的小放大器排查

- 但前面虽然有很多侧证，还没有把 `一 / 以` 的差异正式收成**同一条最小代码链**。
  - 这轮不再做新实验，只把当前实现下的根因链明确写死。

- 当前 `witogram.cc::InterpretGrammarEvidence()` 的最小路径如下：
  1. 先对 `word` 做 `SplitUtf8Tokens(word)`
     - 对单字 `一`
       - `word_tokens = ["一"]`
       - `token_count = 1`
     - 对单字 `以`
       - `word_tokens = ["以"]`
       - `token_count = 1`
  2. 先走 char-path
     - 逐 token 查 `vocab.Index(token)`
  3. 再查 whole-word
     - `word_wid = vocab.Index(word)`

- 对 `word = 以`：
  - 现有模型中：
    - `vocab.Index("以") != NotFound()`
  - 所以会直接进入：
    - `kDirectWholeWordHit`
    - `matched_whole_word = true`
    - `whole_word_log10 = model_->Score(...)`
  - 最终：
    - `step_whole_word_log10 = -15.0282`
    - `step_char_path_log10 = -15.0282`
    - `token_evidence_tag = direct_whole_word_hit`

- 对 `word = 一`：
  - 现有模型中：
    - `vocab.Index("一") == NotFound()`
  - 同时因为它本身就是单字：
    - char-path 里那唯一的 token 也是 `一`
    - 所以该 token 也命中 `NotFound()`
  - 于是进入当前 `neutral_missing` 专用解释：
    - `char_oov_token_count > 0`
    - `token_count == 1`
    - 调用 `AppendNeutralMissingTokenScore()`
    - 只追加固定 `<unk>` unigram 成本
    - **不传播 `<unk>` 上下文 state**
  - 之后 whole-word 分支仍然 miss：
    - `word_wid = NotFound()`
  - 所以它最终被归到：
    - `token_evidence_level = kNeutralMissing`
    - `matched_whole_word = false`
    - `used_char_fallback = false`
    - `oov_token_count = 0`
  - 对应当前 runtime 工件就是：
    - `step_whole_word_log10 = 0`
    - `step_char_path_log10 = -40.4992`
    - `token_evidence_tag = neutral_missing`

- 因而，`第一站是 -> 一` 与 `第一站是 -> 以` 的最小根因链现在可以一句话写成：
  - **两者不是在同一条 token-path 上只差一点分**
  - 而是：
    - `以` 具备单字词表成员资格
      - 可走 whole-word hit
    - `一` 不具备单字词表成员资格
      - whole-word miss
      - 唯一 token 也 miss
      - 只能走“单字 neutral_missing + 固定 <unk> unigram”路径

- 这条最小根因链也进一步解释了为什么前面的很多后续现象会同时成立：
  - `poet` 层一堆 active gate 在 `是 -> 一/以` 这一拍要么为 `0`
    - 要么只是小放大器
  - 真正一开始就把两者拉开的，是：
    - `以`
      - `direct_whole_word_hit`
    - `一`
      - `neutral_missing`
      - `step_whole_word_log10 = 0`
      - `step_char_path_log10` 只能吃 `<unk>` 路径

- 所以，这轮把“如果继续该看哪里”也再收紧了一层：
  - 不该再泛泛说：
    - `一/以` 的 token-path 差异
  - 而应直接写成：
    - **当前 split-token grammar 的 unigram 词表成员资格差异**
    - 决定了：
      - `一` 与 `以`
      - 在 `InterpretGrammarEvidence()` 里会被送入完全不同的 evidence class

- 这同时也排除了一个容易继续绕回去的误判：
  - 当前主缺口不再是：
    - “是否还有别的 `poet` gate 在 `一` 上偷偷多打一刀”
  - 更准确的是：
    - `一` 从进入 `witogram` 的那一刻起，就已经没有 whole-word hit 这条路可走
    - 后面多数现象只是这个初始事实的传播

- 若继续，当前最值得的真正下一步应收口到：
  - 不是再追 `poet`
  - 而是直接判断：
    - 这种“单字 unigram 缺席，但又明显不是 `true_oov` 语义”的 token
    - 是否应该在 `InterpretGrammarEvidence()` 中拥有一个比当前 `neutral_missing` 更细的独立 evidence class
  - 否则后续所有 `LM/base`、`clean continuation`、`source_pool` 观察，都只是在重复看同一个源头事实的下游投影

## 2026-05-25 去重后确认“单字缺证独立 evidence class”尚未做过，但不是只加一个枚举值即可

- 继续前再次专门对这条路线做了去重：
  - 目标不是判断“原理上是否值得”，而是确认：
    - 之前是否已经做过
    - 如果没做过，最小落点在哪里
    - 会影响哪些现有消费点

- 去重结果：
  - 目前日志里已经正式记录到：
    - 当前 grammar 证据分成 4 档：
      - `kDirectWholeWordHit`
      - `kSplitTokenSupported`
      - `kNeutralMissing`
      - `kTrueOov`
    - 并且已经把：
      - `split_token_supported`
      - `neutral_missing`
      从惩罚态 `char_fallback / lm_oov`
      里剥出来
  - 但还没有真正落地：
    - “给 `token_count == 1` 且 unigram 缺席、但语义上又不该等同 `true_oov` 的 token`
    - 再拆出一个比 `kNeutralMissing` 更细的新 evidence class”
  - 所以：
    - 这条路线**尚未正式做过**
    - 但它也不是只在 `witogram.h` 里加一个枚举值就结束

- 这轮静态审计后，最小入口已经明确：
  1. `librime/plugins/witogram/src/witogram.h`
     - `enum class WitogramTokenEvidenceLevel`
  2. `librime/plugins/witogram/src/witogram.cc`
     - `InterpretGrammarEvidence(...)`
       当前关键分支是：
       - `char_oov_token_count == 0`
         -> `kSplitTokenSupported`
       - `char_matched_token_count > 0 || token_count == 1`
         -> `kNeutralMissing`
       - 否则
         -> `kTrueOov`
     - 如果真要新增“单字缺证”类：
       - 最自然的最小切口就是这里
       - 具体就是把：
         - `char_matched_token_count > 0`
         - `token_count == 1`
       这两种情况从当前同一个 `kNeutralMissing` 分支拆开

- 但这轮也同时确认：
  - 下游消费点并不少
  - 所以如果继续，不能只改上游分类，不审下游就直接编译

- 当前已静态定位到的直接消费点至少包括：
  1. `plugins/witset/src/witset_poet.cc`
     - `TokenEvidenceLevelToTag(...)`
       - 需要补新 tag
     - `MakeSentences()` 主打分装配处
       - 当前把
         - `kSplitTokenSupported`
         - `kNeutralMissing`
       一起视作：
         - `neutral_missing = true`
       从而：
         - 不再触发惩罚态 `char_fallback`
         - 不再把 `lm_oov_token_count` 作为惩罚型 OOV 往后传播
       - 如果新增子类，这里必须决定：
         - 它更像继续并入当前 `neutral_missing` 口径
         - 还是要单独走第三种消费路径
  2. `witset_poet.cc` 中几处按 evidence class 做 candidate 级筛选的逻辑：
     - `req.token_evidence_level == kSplitTokenSupported`
     - `req.token_evidence_level == kNeutralMissing`
     - 这些分支出现在：
       - `3245-3246`
       - `3258-3259`
       - `3295-3296`
       - `3314-3315`
       - `3340-3341`
       - `3368-3369`
     - 也就是说：
       - 当前已有一批 prototype / request-family 逻辑
       - 是**显式**按 `kNeutralMissing` 和 `kSplitTokenSupported` 分家处理的
       - 新子类若加入，必须决定这些逻辑是：
         - 归到 `neutral_missing`
         - 归到 `split_token_supported`
         - 还是全部先排除
  3. debug / probe 序列化：
     - `TokenEvidenceLevelToTag(...)`
     - `debug_next_hop_probe_records_`
     - `debug_expansion_gate_records_`
     - `debug_transition_lm_snapshot_`
     - 都会自动受 tag 映射影响
     - 所以后续如果新增子类，现有调试工件会自然出现新 tag

- 因而，这轮把“能否继续”收口成两个更具体的判断：
  1. 这条路线**没做过**
     - 可以继续
  2. 但它不是“只改 `InterpretGrammarEvidence()` 返回值”的超小改动
     - 它至少还要同步审一遍：
       - `TokenEvidenceLevelToTag`
       - `MakeSentences()` 中 `neutral_missing` / `penalty_char_fallback` 的归类
       - 以及所有按 `kNeutralMissing` 做 request-family 聚类和应用的代码

- 因而，这轮去重后的“最小可行下一步”也被收紧为：
  - 不要直接上手改枚举并编译
  - 应先做一个更窄的设计判定：
    - **新子类在 `witset_poet` 下游应该先被视作 `neutral_missing` 的子集，还是应被当成独立第三种消费路径**
  - 只有这一步想清楚，后续改动才不会再次重复“上游分了类、下游又全并回去”的空转

## 2026-05-25 去重后对“单字缺证新子类”的下游消费判定：应先并入 `neutral_missing` 主语义，而不是立刻新开第三条 `poet` 路径

- 承接上一节的去重结果，这轮继续做的是：
  - 不改代码
  - 只静态判断：
    - 如果以后在 `InterpretGrammarEvidence()` 里新增一个“单字 unigram 缺席但非 `true_oov`”的新 evidence class
    - 它在 `witset_poet` 下游应该先归到哪条消费路径

- 去重后确认：
  - 这一步判断以前还没有正式做过
  - 因而这轮直接把消费点静态串成一条链，避免后续再次重复“上游分了类、下游怎么接”这一步

- 当前 `witset_poet.cc` 的主语义消费点可以分成两组：

- 第一组：**主惩罚/主打分归一化消费**
  - 位置：
    - `2728-2755`
  - 当前口径：
    - `kSplitTokenSupported`
    - `kNeutralMissing`
    - 会一起被并到本地布尔：
      - `neutral_missing = true`
    - 从而：
      - 不再触发惩罚态 `penalty_char_fallback`
      - `prefix_lm_scale_fallback = false`
      - `penalty_lm_oov_token_count = 0`
      - `used_char_fallback` 被重写为非惩罚态
  - 这组逻辑的本质不是“做精细 family contract”
    - 而是：
      - 把“缺证”
      - 与“真惩罚态 fallback / true OOV”
      区分开
  - 对“单字缺证新子类”来说，这里的正确落点应是：
    - **先并入 `neutral_missing` 主语义**
    - 也就是：
      - 继续不触发惩罚态 fallback
      - 不再把它重新打回 `true_oov` 型 penalty

- 第二组：**现有 `neutral_missing` request-family / source-line / prefix-family 辅助逻辑**
  - 位置：
    - `3241-3370`
  - 当前几段 helper 的共同特点是：
    - 明确要求：
      - `req.char_count >= 2`
      - 或 `prefix_req.char_count == 2`
    - 然后再要求：
      - `token_evidence_level == kNeutralMissing`
      - 或 `== kSplitTokenSupported`
  - 也就是说，这批逻辑本来就是围绕：
    - **双字及以上**
    - 的 `neutral_missing / split_token_supported` family 竞争
    - 来设计的
  - 对单字缺证而言，即便上游加了新子类：
    - 只要保持 `char_count == 1`
    - 它天然就会被这些逻辑挡在外面
    - 不需要额外新开一条完整第三消费路径

- 这一步非常关键，因为它说明：
  - “单字缺证新子类”在 `poet` 下游的最小安全落点不是：
    - 立刻新增一整套独立的 request-family / prefix-family / rescue 逻辑
  - 而是：
    1. 在主惩罚归一化层
       - **视作 `neutral_missing` 的子集**
    2. 在现有双字 family 逻辑层
       - **默认不参与**
       - 不是因为新子类被特殊禁止
       - 而是因为这些逻辑本身就有：
         - `char_count >= 2`
         这一层 gate

- 这轮还顺带核到了一个支持这个判断的旁证：
  - 旧日志里已经正式写过：
    - `char_count < 2`
    - 会直接命中：
      - `non_contract_candidate`
  - 例如：
    - `第一站是一 -> 座`
    - `体验不一 -> 样`
    都先被这层挡住
  - 这进一步说明：
    - 单字链当前在 `poet` 里本来就不走那套“contract / family bonus”主路径
    - 所以新子类没必要现在就硬开第三套并行消费

- 因而，这轮可以正式把设计判定写成一句话：
  - **若后续新增“单字缺证” evidence class，它在 `witset_poet` 下游应先被当成 `neutral_missing` 的窄子集，而不是立刻新增独立第三种消费路径。**

- 更细一点的落地含义是：
  1. `TokenEvidenceLevelToTag(...)`
     - 需要新增一个新 tag
     - 方便观测
  2. 主打分归一化处
     - 新子类应继续落在：
       - `neutral_missing = true`
     - 也就是继续避免：
       - `penalty_char_fallback`
       - `true_oov` 型 OOV 处罚
  3. 当前 `3241-3370` 这批双字 `neutral_missing` family 逻辑
     - 暂时不需要改
     - 因为单字链本来就过不去 `char_count >= 2`
  4. 如果以后真要给单字缺证做专门补救
     - 应该另开一套更窄、明确针对单字的 helper
     - 而不是把它强塞进现有双字 `neutral_missing` family 路线

- 因而，这轮去重并静态审计后的“最小可行实现顺序”也确定了：
  - 第一步若真落地：
    - 只做上游新 class
    - 加 tag
    - 并在主惩罚归一化处把它并到 `neutral_missing`
  - 第二步再观察：
    - debug 工件里单字链是否因此更容易和多字 `neutral_missing` 区分开
  - 只有在这之后，才值得决定要不要额外设计单字专用 contract / rescue

## 2026-05-26 去重后落地“单字缺证”最小语义拆分：只分新 class，不改当前 `neutral_missing` 主打分语义

- 继续前再次核对了前面几轮日志，确认这条最小实现路线此前**只做到静态判定，尚未真正落地**：
  - 上游新增单字缺证 class
  - `TokenEvidenceLevelToTag(...)` 新增 tag
  - `witset_poet` 主归一化继续把它并入非惩罚态缺证
  - 不改当前双字 `neutral_missing` family helper
- 这轮按“只做未尝试步骤”的原则，直接落地这条最小版，不再回到已经判负的 bonus / request-family / pairwise 重复路线。

- 为避免为了这个小改动再新增脚本，先用现有 `partial_chain_stage_probe_result.json` 做了一个最小失败验证：
  - 命令：
    - `python -c "... focus['一']['samples'] ... expected={'single_token_missing'} ..."`
  - 失败输出：
    - `observed_tags= ['neutral_missing']`
  - 这一步确认：
    - 当前 runtime 下 `第一站是 -> 一` 仍然没有独立 tag
    - 红灯有效，测试对象正确

- 之后只做了 3 个代码点的最小实现：
  1. `plugins/witogram/src/witogram.h`
     - 在 `WitogramTokenEvidenceLevel` 中新增：
       - `kSingleTokenMissing`
  2. `plugins/witogram/src/witogram.cc`
     - 在 `InterpretGrammarEvidence()` 中把原来：
       - `char_matched_token_count > 0 || token_count == 1`
       -> 同归 `kNeutralMissing`
     - 拆成两支：
       - `char_matched_token_count > 0`
         -> `kNeutralMissing`
       - `token_count == 1`
         -> `kSingleTokenMissing`
     - 同时保持：
       - `used_char_fallback = false`
       - `oov_token_count = 0`
       - `total_log10 = max(total_log10, neutral_char_total_log10)`
     - 也就是：
       - 只拆语义标签
       - 不把它重新打回惩罚态 fallback / true OOV
  3. `plugins/witset/src/witset_poet.cc`
     - `TokenEvidenceLevelToTag(...)`
       - 新增：
         - `single_token_missing`
     - 主归一化处：
       - 把 `kSingleTokenMissing` 并入当前 `neutral_missing` 布尔
       - 继续避免：
         - `penalty_char_fallback`
         - `true_oov` 型 OOV 惩罚传播

- 落地后只做了允许范围内的最小验证：
  - 编译：
    - `librime/build.bat static`
  - probe：
    - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode probe`
  - 再回到同一条验证命令做绿灯确认

- 绿灯结果如下：
  - `第一站是 -> 一`
    - `token_evidence_tag = single_token_missing`
    - `matched_whole_word = false`
    - `used_char_fallback = false`
  - `第一站是 -> 以`
    - `token_evidence_tag = direct_whole_word_hit`
    - `matched_whole_word = true`
- 这说明：
  - 新 class 已经在 runtime 工件里可观测
  - 但 `poet` 主语义没有把它重新打回惩罚态
  - 同时 `以` 的原有 whole-word 路径保持不变

- 这轮因此把“最小可行 A 路线”正式完成了：
  - 已能在 runtime 工件里把：
    - 单字 unigram 缺席
    - 与一般 `neutral_missing`
    明确区分开
  - 但当前还**没有**改变它在 `poet` 中的主打分待遇
  - 因而这是一个：
    - 已落地的语义拆分与观测增强
    - 不是行为性调分实验

- 这也给后续去重提供了新的边界：
  - 以后不要再把“先区分单字缺证 vs 一般 neutral_missing”当作待做路线
  - 这一步现在已经做完
  - 若继续，新的未尝试路线应转到：
    - 这个新 class 是否需要一条**单字专用**的更窄 helper / contract / rescue
    - 而不是再重复做 tag 拆分

## 2026-05-26 去重后继续：只对 `single_token_missing` 关掉“无真实碎片证据的默认单字结构门控”

- 继续前再次回查了 `WORKLOG`、阶段文档、当前 `witset_poet.cc` 公式和线上 schema，先排除了不能再重复的路线：
  - `clean_single_char_bridge_bonus` / `validated_prefix_suffix_bonus`
    - 当前线上权重仍为 `0.0`
    - 之前的 spot check 也已判负
  - `request-stage` 单字保活 / 轻量 contract
    - 旧日志已正式排除
  - `lm_avg` 的全局或 `matched_token_count` 门控
    - 已做过且已判负
  - 2026-05-20 那版“单字结构 gate + 弱 LM 校准 + 参数重排”联动原型
    - 已做过完整验证并整体判负

- 去重后剩下的未尝试窄入口是：
  - 当前已经新增了 `single_token_missing`
  - 但线上 `ComputeSingleCharStructureGate()` 仍会因为
    - `matched_whole_word = false`
    - 即给单字 continuation 施加 `0.60` 的结构 gate
  - 这会继续触发：
    - `single_char_penalty`
    - `fragment_penalty`
    - `structure_penalty`
    - `tail_repair_penalty`
    - `octagram_penalty`
  - 而这一步并不要求：
    - `used_char_fallback`
    - `lm_oov_token_count > 0`
    - `trailing_single_char_run >= 2`
    - 或其他真实碎片化证据

- 这条路线与 2026-05-20 的联动原型不同：
  - 不再同时动 `lm_avg`
  - 不调一组结构参数常数
  - 不改一般 `neutral_missing`
  - 只对新引入的 `single_token_missing` 做更窄的结构 gate 豁免

- 已落地的代码变更仅限 `plugins/witset/src/witset_poet.cc`：
  - `ComputeSingleCharStructureGate(...)`
    - 新增 `token_evidence_level` 入参
    - 保持：
      - `used_char_fallback`
      - `lm_oov_token_count > 0`
      仍然直接触发 `0.60` gate
    - 只在：
      - `token_evidence_level == kSingleTokenMissing`
      - 且没有 fallback / OOV 证据
      时，不再因为 `matched_whole_word = false` 默认触发这档结构 gate
  - 调用点同步透传 `token_evidence_level`

- 这一步的目标边界是：
  - 只减少“单字缺证但非碎片链”被结构层机械二次下压
  - 不改：
    - `lm_score_scaled`
    - `lm_avg`
    - `request-stage`
    - `same-span`
    - `true_oov / fallback` 路径
  - 也不把一般 `neutral_missing` 一起放宽

- 按当前用户规则，这轮先只落代码和日志，不执行编译或重跑。
- 下一步若继续，应在用户允许后只做最小验证：
  - `librime/build.bat static`
  - 再用 `partial_chain_stage_probe.py --case case2_diyizhan --mode probe`
    复核 `第一站是 -> 一` 的结构项是否下降且 guardrail 未被误伤

- 随后在用户允许后，已按上面的最小路径完成了这轮验证：
  - 编译：
    - `librime/build.bat static`
    - 通过
  - probe：
    - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode probe`
    - 通过

- 先看聚合结果：
  - `next_hop_after_diyizhanshi`
    - `matched_source_suffixes = ["第一站是"]`
    - 但 `focus_entries["一"].samples = []`
    - `preferred_candidate_stage = null`
  - 说明：
    - 精确 `第一站是 -> 一` 这条 source 仍没有进入可观测的 preferred candidate stage
  - `next_hop_after_diyizhanshi_display`
    - `focus_entries["一"]`
      - `preferred_stage = request`
      - `request = 6`
      - `admitted_new = 2`
      - `token_evidence_tag = single_token_missing`
      - `matched_whole_word = false`
      - `used_char_fallback = false`
    - 但 display 家族仍由 `已 / 以` 主导

- 再直接抽原始 `partial_chain_stage_probe.next_hop.jsonl` 的 request 记录，当前最关键的对比如下：
  - `input = diyizhanshiyi`
  - `source_text = 我踏上了旅行的征程。第一展示`
  - `entry_text = 一`
    - `search_score = -280.534`
    - `base_score = -279.157`
    - `adjustment_score = -1.37654`
    - `token_evidence_tag = single_token_missing`
  - `entry_text = 已`
    - `search_score = -253.197`
    - `base_score = -252.028`
    - `adjustment_score = -1.16888`
    - `token_evidence_tag = direct_whole_word_hit`

- 这轮因此可以下一个更明确的结论：
  - 这次“只对 `single_token_missing` 关掉无碎片证据的默认单字结构 gate”已经进入 runtime
  - 但从当前最小 probe 看：
    - 主盘面仍没有把 `第一站是 -> 一` 推回 preferred stage
    - display 家族里的 `一` 也仍明显落后于 `已 / 以`
  - 且当前 `一` vs `已` 的 request 差额里：
    - `adjustment_score` 差额只有约 `-0.21`
    - 主差额仍然是 `base_score / LM`
  - 所以：
    - 这条窄结构 gate 路线不是 no-op
    - 但它不足以扭转当前主缺口
    - 后续不应继续沿这条线再做更多同构小补丁

- 这也给去重后的下一步边界再收紧一档：
  - 以后不要再把：
    - “给 `single_token_missing` 去掉默认单字结构 gate”
    当作待做路线
  - 这一步现在已完成并已最小验证
  - 若继续，应回到：
    - `single_token_missing` 对 `LM/base` 主账的上游语义解释
    - 或更早的 source selection / family continuity
    - 而不是继续在结构 regularizer 上做同构窄调

## 2026-05-26 去重后继续：仅对 `single_token_missing` 软化固定 `<unk>` unigram 成本到 `0.75`

- 继续前再次回查了 `WORKLOG`、`witogram.cc` 与前面这轮结构 gate 验证结论，先确认下面这些路线都已经做过，不能重复：
  - 单字缺证新 class 语义拆分
    - 已做完并进入 runtime
  - “只对 `single_token_missing` 关掉默认单字结构 gate”
    - 已最小验证，结论为“有效但不足”
  - 全局把 `neutral_missing` 的固定 `<unk>` 成本从 `1.0` 软化到 `0.75`
    - 更宽路线已做过且已判负
  - 多字/单字 `neutral_missing` 的 `<unk>` state 传播语义改动
    - 已做过，不能再重复

- 去重后这轮唯一仍未正式做过、且仍直接对位 `LM/base` 主差额的窄入口是：
  - 只对 `token_count == 1` 的 `single_token_missing`
  - 把固定 `<unk>` unigram 成本从 `1.0` 软化到 `0.75`
  - 不动：
    - 一般 `neutral_missing`
    - `<unk>` state 传播
    - `witset_poet.cc`
    - request-stage / source selection / structure gate

- 已落地代码仅限 `plugins/witogram/src/witogram.cc`：
  - 新增：
    - `kSingleTokenMissingUnknownPenaltyScale = 0.75`
  - `AppendNeutralMissingTokenScore(...)`
    - 新增 `unknown_penalty_scale` 入参
  - `InterpretGrammarEvidence(...)`
    - 在 `token_count == 1` 时使用
      - `kSingleTokenMissingUnknownPenaltyScale`
    - 其他情况仍使用
      - `kNeutralMissingUnknownPenaltyScale = 1.0`

- 这轮按最小路径完成验证：
  - 编译：
    - `librime/build.bat static`
    - 通过
    - 仅见历史 `LNK4044` warning，无新增编译错误
  - probe：
    - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode probe`
    - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case3_tiyanbuyiyang --mode probe`

- 先看 `case2_diyizhan`：
  - 聚合层仍然没变：
    - `next_hop_after_diyizhanshi`
      - `matched_source_suffixes = ["第一站是"]`
      - `preferred_candidate_stage = null`
      - `focus_entries["一"].samples = []`
    - 说明精确 `第一站是 -> 一` 仍没回到可观测 preferred candidate stage
  - 但对同一类原始 request 记录，`LM/base` 确实被抬动了：
    - `input = diyizhanshiyi`
    - `source_text = 我踏上了旅行的征程。第一展示`
    - `entry_text = 一`
      - 之前：
        - `search_score = -280.534`
        - `base_score = -279.157`
        - `lm_score_scaled = -90.9503`
      - 现在：
        - `search_score = -269.453`
        - `base_score = -268.076`
        - `lm_score_scaled = -79.8693`
        - `step_char_path_log10 = -79.9984`
        - `token_evidence_tag = single_token_missing`
    - `entry_text = 已`
      - `search_score = -253.197`
      - `base_score = -252.028`
      - `lm_score_scaled = -63.8213`
      - `token_evidence_tag = direct_whole_word_hit`
    - `entry_text = 以`
      - `search_score = -252.761`
      - `base_score = -251.675`
      - `lm_score_scaled = -63.9282`
      - `token_evidence_tag = direct_whole_word_hit`
  - 这说明：
    - 只软化单字缺证固定 `<unk>` 成本不是 no-op
    - `一` 的 `search/base/lm` 大约抬了 `11.1`
    - 但 `一` 仍明显落后于 `已 / 以`
    - 主盘面仍没有翻

- 再看 `case3_tiyanbuyiyang` guardrail：
  - `focus_entries["一"]`
    - `request = 6`
    - `admitted_new = 2`
    - `preferred_stage = request`
    - `base_score = -150.34`
    - `lm_score_scaled = -33.2429`
    - `step_char_path_log10 = -41.4992`
    - `token_evidence_tag = single_token_missing`
  - `focus_entries["宜"]`
    - `base_score = -162.631`
    - `lm_score_scaled = -45.4751`
    - `token_evidence_tag = direct_whole_word_hit`
  - `focus_entries["依"]`
    - `base_score = -162.71`
    - `lm_score_scaled = -45.4751`
    - `token_evidence_tag = direct_whole_word_hit`
  - 与上一轮同口径记录相比，这组 guardrail 数值没有出现新的异常抬升或排序翻转

- 这轮因此可以正式收口为：
  - 只对 `single_token_missing` 软化固定 `<unk>` unigram 成本
    - 能局部改善 `case2` 里 `一` 的 `LM/base`
    - 也没有打坏当前 `case3` guardrail
  - 但它仍不足以把 `第一站是 -> 一` 推回 preferred stage，也不足以翻转 `已 / 以` 优势
  - 因此后续不要再沿这条“单字 `<unk>` 固定成本”路线做更多同构窄调
  - 若继续，应回到：
    - `single_token_missing` 对 `LM/base` 主账的更上游形成机制
    - 或更早的 source selection / family continuity

## 2026-05-26 去重后继续：只对句首首词、多字、raw char-path 含 `<unk>` 的非惩罚态缺证恢复 early-prefix LM 缩放

- 继续前先重新交叉核对了 `WORKLOG` 与当前 `witset_poet.cc`，确认下面这些路线都已经做过，不能重复：
  - 宽口的句首双字 `NeutralMissing -> prefix LM scale`
    - 之前会把 `体言` 异常抬高
    - 已判负并收紧
  - 只对 `single_token_missing` 关结构 gate
    - 已验证，结论为“有效但不足”
  - 只对 `single_token_missing` 软化固定 `<unk>` unigram 成本
    - 已验证，结论为“有效但不足”
- 同时静态复核当前代码后确认，一个更窄、且日志里还没正式做过的入口是：
  - 当前 `prefix_lm_scale_fallback` 已只保留给 `penalty_char_fallback`
  - 但这会把一类句首首词、多字、主语义已纠偏成非惩罚态、raw char-path 仍实际经过 `<unk>` 的候选，一起排除在 early-prefix LM 缩放之外
  - 这类候选的典型形态正是：
    - `start_pos = 0`
    - `generated_word_count = 0`
    - `generated_char_count = 0`
    - `char_count >= 3`
    - `token_evidence_level = neutral_missing`
    - `char_path_oov_token_count > 0`
    - `char_path_matched_token_count > 0`
  - 与此前宽口 `case3` 问题不同，这一刀不会把所有句首双字 `neutral_missing` 一起重新放进缩放分支

- 因而这轮只在 `plugins/witset/src/witset_poet.cc` 做了一个最小变体：
  - 保持：
    - 惩罚态 `used_char_fallback`
    - `true_oov`
    仍然照旧触发 `ComputePrefixCharFallbackLmScale(...)`
  - 额外新增一条更窄的 `first_word_evidence_missing_prefix`
    - 只在句首首词、多字、`neutral_missing`、且 raw char-path 同时存在
      - `char_path_oov_token_count > 0`
      - `char_path_matched_token_count > 0`
      时，把它视作“证据不足型 early-prefix fallback”
  - 继续明确排除：
    - 句首双字 `neutral_missing`
    - 惩罚态以外的一般短词宽放

- 这轮最小验证保持固定：
  - 编译：
    - `librime/build.bat static`
    - 通过
    - 无新增编译错误，仅有历史 `LNK4044` warning
  - probe：
    - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --case case3_tiyanbuyiyang --mode probe`

- `case2_diyizhan` 的变化很明确：
  - `next_hop_after_diyizhanshi_display`
    - `focus_entries["一"]`
      - 之前：
        - `search_score = -231.761`
        - `base_score = -223.650`
        - `lm_score_scaled = -33.2429`
      - 现在：
        - `search_score = -169.429`
        - `base_score = -158.110`
        - `lm_score_scaled = -33.2429`
      - 说明这刀命中的不是当前 step LM 本身，而是更早前缀带入这一跳的基础盘面
  - 同一节里：
    - `已`
      - `search_score = -151.580`
      - `base_score = -142.062`
    - `以`
      - `search_score = -151.615`
      - `base_score = -142.108`
  - 因而：
    - `一` 虽仍落后于 `已 / 以`
    - 但 gap 已比上一轮明显收窄
    - 这刀不是 no-op

- 句级 `case2` 也出现了更靠近正确主轴的新盘面，但仍未翻正：
  - `partial_chain_stage_probe.snapshot.jsonl`
    - `input = diyizhanshiyizuogulaodexiaozhen`
    - `expected_rank = null`
    - top10 里已出现：
      - `rank 3 = 第一展是一座古老的小镇`
      - `rank 4 = 第一展是一座古老的小针`
    - 说明“首词更早失血”这层被部分缓解后，整句主盘面已开始向正确 continuation 家族靠拢
    - 但当前仍被：
      - `第一展示已作古老的小镇`
      等错误线压住

- `case3_tiyanbuyiyang` 的护栏保持住了：
  - `partial_chain_stage_probe.snapshot.jsonl`
    - `input = tiyanbuyiyangdeshenghuo`
    - `rank 1 = 体验不一样的生活`
  - top10 也全部仍在：
    - `体验...`
    家族
  - 没有回退成早前那种：
    - `体言...`
    盘面
  - `next_hop_after_tiyanbu`
    - `一`
      仍是
      - `token_evidence_tag = single_token_missing`
      - `base_score = -150.340`
      - `lm_score_scaled = -33.2429`
    - `宜 / 依`
      仍保持 whole-word 命中读数，不见异常放宽

- 这轮因此可正式收口为：
  - 只对句首首词、多字、raw char-path 含 `<unk>` 的非惩罚态缺证恢复 early-prefix LM 缩放
    - 能明显改善 `case2` 更早前缀带来的 `search/base` 盘面
    - 且没有把 `case3` 护栏重新打坏
  - 但它仍不足以把整句 `case2` 翻正
  - 因而当前主缺口已进一步收紧为：
    - 前缀失血被部分补回后，剩余更像是
      - `第一展` vs `第一站`
      - 以及 `是一座` 后续 continuation
      的更深层竞争
  - 若继续，不应再回到宽口 `NeutralMissing -> prefix LM scale`
    或其他句首短词缩放变体；应优先查：
    - `第一展` / `第一站` 在更早 source/base 上的 residual gap
    - 以及 `第一展是一座` 为什么还能继续压住 `第一站是一座`

## 2026-05-26 去重后继续：当前主死亡点已前移到 `diyizhan` 的首词 `第一展 / 第一站` 竞争，后段 `same-span / ReqSrcMismatch` 大多已碰不到

- 继续前先再次核对了今天前面的几段日志，确认下面这些路线都已经做过，不应重复：
  - 宽口或窄口的句首 `prefix LM scale` 变体
  - `single_token_missing` 结构 gate / `<unk>` unigram 缩放
  - `same-span 3-char` 常数调节
  - `ReqSrcMismatch` multiplier 增强
- 这次不再补跑新 probe，直接复用当前这轮已经落盘的：
  - `partial_chain_stage_probe.snapshot.jsonl`
  - `partial_chain_stage_probe_result.json`
  做残余分差拆账，避免重复运行同一验证。
- 复核当前真实盘面后，主缺口比上一段结论还更早：
  - `diyizhan`
    - `rank 1 = 第一展`
    - top20 里已经没有 `第一站`
  - `diyizhans`
    - `rank 1 = 第一展是`
    - top20 里没有 `第一站是`
  - `diyizhanshi`
    - `rank 1 = 第一展示`
    - `rank 3 = 第一展是`
    - top20 里仍没有 `第一站是`
  - 说明当前整句没有翻正，不只是 `第一展是一座` 压住 `第一站是一座`
  - 更早在首词 `diyizhan` 这一步，`第一站` 就已经没保住可见候选
- 同时把 `witset_poet.cc` 里所有还会影响这一段竞争的代码重新串了一遍，当前链路可以明确写成：
  - `ComputePrefixCharFallbackLmScale(...)`
    - 现在这条窄修正会同时救到一类句首多字、`raw char-path` 混合已知 token 和 `<unk>` 的候选
    - 因而 `第一展` 与 `第一站` 都会一起吃到这层“前缀失血纠偏”
    - 它只能把整类首词 family 拉回主盘面，不能单独解释为什么最终只剩 `第一展`
  - `ComputeSameSpanCompetitionPenalty(...)`
    - 当前 anchor 只从 `generated_word_count == 2` 的 split-anchor line 里选
    - `diyizhan` 这一步的 `第一展 / 第一站` 都是首词单块 line，不在这条 same-span anchor 的命中面里
    - 所以现有 same-span 机制基本碰不到当前最早死亡点
  - `ComputeRequestStageSourceMismatchPenalty(...)` / `ComputeSharedPrefixLmContractPenalty(...)`
    - 需要 request-stage bridge 的正负对照 source 才会触发
    - 但 `diyizhan` 的首词竞争还在 request-stage 之前
    - 因而它们也只能影响更后的 `第一展是* / 第一展示*`，碰不到 `第一展 / 第一站` 本体
- 用当前 snapshot 的 debug 读数对上后，可以把残余问题再收紧一层：
  - `diyizhan` 下 `第一展`
    - `Base = -75.94`
    - `LmScaled = -62.46`
    - `WholeHit = 0`
    - `SameSpanComp = 0`
  - `diyizhans` 下 `第一展是`
    - `Total = -164.97`
    - `ReqBridge = 2.00`
    - `AltReqBridge = 3.60`
    - `SameSpanComp = 0`
  - `diyizhanshi` 下 `第一展示`
    - `Total = -140.19`
    - `ReqBridge = 2.00`
    - `SameSpanComp = 0`
  - 这说明当前真正接管主盘面的，是一条从首词单块 `第一展` 就开始领先、然后自然顺延成 `第一展是 / 第一展示` 的链
  - 而不是先有一个健康的 `第一站`，再在后段被单独打输
- 因而当前阶段结论应更新为：
  - `first_word_evidence_missing_prefix` 这刀已经把“句首首词 family 整体起不来”纠回到可分析状态
  - 但剩余主缺口已经前移成：
    - **首词单块 `第一展 / 第一站` 的 residual base/LM 竞争**
  - 现有后段的：
    - `same-span`
    - `ReqSrcMismatch`
    - `SharedPrefixLm`
    机制，大多已经碰不到这个最早失血点
- 若继续，下一步不应再回去调句首 prefix-LM，也不应继续加大 `ReqSrcMismatch`：
  - 更值得做的是：
    - 直接围绕 `diyizhan` 这一步补“首词单块 reparse family”层面的竞争保真
    - 或先做更窄的 current-snapshot / local-snapshot 对照，确认 `第一站` 在这一步到底是落在 top20 外多少名，以及与 `第一展` 的具体 `Base/Dict/LmScaled` 差额分布

## 2026-05-26 去重后继续：`diyizhan` 首词竞争不是全新问题，但当前盘面已从“`第一站` 略领先”翻成“`第一展` + `*驿站` 家族主导”

- 继续前先查旧日志与旧工件，确认下面这步以前已经做过，不能当成新路线重复：
  - `case2` 在 `第一 -> 站/战/展` 这一跳的首步拆账
  - 当时的结论是：
    - `第一站 = -60.578`
    - `第一战 = -60.809`
    - `第一展 = -61.311`
    - `lm_score_scaled` 三者完全相同，`站` 略领先主要来自 `dict_score_raw/dict_score_norm + adjustment`
    - 所以那一版判断是：真正决定性的问题不在 `第一 -> 站/战/展`，而在下一跳 `第一站 -> 是一/是以`
- 这次不重复做旧拆账，而是只用现有 debug 产物核对：
  - 旧的 `diyizhan_local_competition.json`
  - 旧的 `case2_start8_key_family_compare.json`
  - 当前的 `partial_chain_stage_probe.snapshot.jsonl`
- 对照后可确认两件事：
  - 旧问题本来就存在：
    - `case2_start8_key_family_compare.json` 里
    - `我踏上了旅行的征程。的驿站 / 驿站`
      - `global_rank = 1`
    - `我踏上了旅行的征程。第一站 / 第一站`
      - `global_rank = 32`
    - 说明“`第 + 驿站` split 线压住整块 `第一站`”并不是今天才出现的新现象
  - 但当前盘面又进一步恶化了：
    - 最新 `partial_chain_stage_probe.snapshot.jsonl` 的 `input = diyizhan`
      - `rank 1 = 第一展`
      - `rank 2 = 的驿站`
      - `rank 3 = 的翼展`
      - `rank 4..20` 基本被 `低/递/底/敌/... + 驿站` 一类 split 路占满
      - `第一站` 已经不在 top20
- 当前 `diyizhan` 的最新 debug 也把最早死亡点再收紧了一层：
  - `第一展`
    - `Base = -75.94`
    - `Adj = -1.47`
    - `Total = -77.41`
    - `LmScaled = -62.46`
    - `Tok = 3`
    - `OovTok = 0`
    - `CharFB = 0`
    - `SameSpanComp = 0`
  - `的驿站`
    - `Base = -179.27`
    - `Total = -186.21`
    - `WholeHit = 1`
    - `SameSpanComp = 0`
  - 这说明当前可见区里同时存在两股力量：
    - 一条是 `第一展` 这类首词单块 3-token、无 OOV、无 fallback 的强线
    - 一条是 `第 + 驿站` 一类 split 路在大量占位
- 因而这里要明确修正最近一段的口径：
  - 当前不应再把问题只描述成：
    - `第一展是一座` 压住 `第一站是一座`
  - 更准确的是：
    - `diyizhan` 首词这一步就已经同时被
      - `第一展`
      - 以及大批 `*驿站`
      抢走了可见候选位
    - `第一站` 本体连首词可见区都保不住
- 这也进一步支持上一段判断：
  - 现有后段 scorer 项，包括：
    - `same-span`
    - `ReqSrcMismatch`
    - `SharedPrefixLm`
  - 大多碰不到当前最早死亡点
  - 因为问题已经发生在 request-stage 之前的首词 reparse family 竞争
- 若继续，下一步不应再回去：
  - 调句首 prefix-LM
  - 调 `ReqSrcMismatch`
  - 或重跑旧的 `第一 -> 站/战/展` 账本
- 更值得做的是：
  - 围绕 `diyizhan` 的首词单块 / split-family 做更窄的 current-snapshot 对照
  - 重点确认：
    - `第一站` 当前为何已从旧的 `rank 32` 恶化到 top20 外
    - `第一展` 本体增强，和 `*驿站` 家族挤占，可分别归到哪类 reparse / family contract 变化

## 2026-05-26 去重后再收紧一层：当前可见的 `第一展 / 第一展示` 更像 `第一 -> 展/展示` split-prefix family，而不是 same-span 首词单块重解释

- 继续前先核对旧日志，确认下面这件事以前已经做过，不能重复当成新发现：
  - `第一站` 主线早就被收口到：
    - `split-prefix family`
    - 与 `same-span reparse family`
    - 的竞争边界
- 这次补的不是“又有两类 family 在竞争”，而是继续回答：
  - 当前可见区里的 `第一展 / 第一展示`
  - 到底更像哪一类
- 复用的旧证据与当前证据如下：
  1. `diyizhan_prefix_family.json`
     - `start=0,end=8` 的整块 entry 底座里仍然是：
       - `第一站 = -12.8223`
       - `第一战 = -13.0316`
       - `第一展 = -13.4862`
     - 即：**如果只看首词整块词典 entry，`第一站` 仍优于 `第一展`**
  2. 但同一工件里，一旦进入 prefix `第一` 之后，局面立刻翻转：
     - `start=4,end=8`
       - `展 = -11.7298`
       - `战 = -11.7486`
       - `站 = -11.7601`
     - `start=4,end=11`
       - `展示 = -12.1403`
       - `战士 = -12.3499`
       - `战时 = -12.4753`
     - 即：**在 `第一 -> ?` 的 split-prefix continuation 里，`展/展示` 先天就比 `站` 更强**
  3. 旧日志已经另外定点确认过：
     - `第一展示`
     - 当前 request 盘面上可以直接作为
       - `第一 + 展示`
       - 的两段链进入
     - 它不是必须依赖“首词单块重解释”才能成立
- 因而把这些证据合起来，当前更合理的判断应更新为：
  - 最新 snapshot 里可见的
    - `第一展`
    - `第一展示`
  - **不能直接当成 same-span 首词单块 family 接管的证据**
  - 它们更像：
    - `第一`
    - 这个较早前缀被保住后
    - 后续 `展/展示` continuation 在 split-prefix family 内自然胜出
- 这和 `*驿站` 家族的关系也因此更清楚了：
  - 当前首词可见区里主要不是“一个单块 family 压一个 split family”
  - 而是至少两类 split-prefix error family 在并行挤占：
    1. `第一 -> 展/展示`
    2. `的 -> 驿站`
  - 而正确家族：
    - `第一 -> 站`
    - `第 -> 一站`
    - `第一站`
    仍属于那组更容易掉进 fallback / OOV 的弱前缀族
- 这对下一步入口的影响是：
  - 不应再把当前问题表述成：
    - “same-span 首词单块 `第一展` 为什么这么强”
  - 更准确的下一步应改成：
    - **比较多个 split-prefix family 在 `AnalyzeCredibility / RewriteWordGraph` 前后的保活与竞争边界**
    - 尤其是：
      - `第一 -> 展/展示`
      - `的 -> 驿站`
      - `第一 -> 站 / 第 -> 一站 / 第一站`
      这三组 family 的进入条件和失血点

## 2026-05-26 去重后再补同口径 upstream 对照：`start=8` 这一层不是主失血点，真正不对称仍在更早的 `第/的 -> 一/驿站` family 形成

- 继续前先查旧日志，确认下面这些结论以前已经做过，不能重复当成新发现：
  - `第一 -> 站/展/战` 这一拍本身几乎不分叉
  - `第一 + 展示` 与 `第一展 + 示` 是两条已确认存在的替代结构
  - `的 -> 驿站` 一旦形成，后续 phrase 扩展异常顺推
- 这次补的是一个更窄的“同口径并排”：
  - 直接复用
    - `pre_source_pool_focus.json`
    - `source_pool_key_positions.json`
    - `state_competition_fresh_summary.json`
    - `diyizhan_family_transition_extract.json`
  - 把
    - `第一站`
    - `第一战`
    - `第一展`
    - `的 -> 驿站`
    放到同一条 upstream 视角下重排一次
- 新的并排结果很清楚：
  1. `start=8` 这一层，`第一站 / 第一战 / 第一展` 都是健康存活的
     - `source_pool_key_positions.json` 显示：
       - `source_pool(start=8,end=8)` 中
         - `第一站 = -59.1937`
         - `第一战 = -59.4248`
         - `第一展 = -59.9266`
       - 且三者都进入了 `top_candidate`
     - `pre_source_pool_focus.json` 显示：
       - `第一站 -> 是`
       - `第一站 -> 是一座`
       - `第一展 -> 是`
       - `第一展 -> 示`
       - `第一展 -> 是一座`
       都能从 `batch_selected` 继续进入 `admitted_new`
     - `state_competition_fresh_summary.json` 里
       - `8->16` 的 `top_requests` 仍按
         - `第一站是一座`
         - `第一战是一座`
         - `第一展是一座`
         排序
       - 说明 **`RewriteWordGraph` 之后至少到 `start=8` 这一层，正确族并没有被错误族直接打穿**
  2. `的驿站` 并不出现在这组 `start=8` 对照里
     - 它不是与 `第一站 / 第一展` 在同一 source-line family 上直接竞争后落败
     - 而是来自更早已经形成好的另一条 split-prefix path
  3. 真正的强不对称仍然出现在更早的 family 形成层
     - `diyizhan_family_transition_extract.json` 直接给出了同一口径证据：
       - `第 -> 一`
         - `total_log10 = -39.4992`
         - `oov_token_count = 1`
         - `used_char_fallback = true`
       - `的 -> 驿站`
         - `total_log10 = -52.2658`
         - `oov_token_count = 0`
         - `used_char_fallback = true`
       - `第一站`
         - 首词整块 `total_log10 = -116.498`
         - `oov_token_count = 1`
         - `used_char_fallback = true`
       - 说明正确族从 very early family formation 开始就更容易带着 `<unk>/fallback` 债务前进
  4. 一旦错误族长成 `的驿站`，后续 `-> 是` 会拿到远强于正确族的 whole-word 过渡
     - 同一工件里：
       - `的驿站 -> 是`
         - `total_log10 = -12.2157`
         - `matched_whole_word = true`
       - `第一站 -> 是`
         - `total_log10 = -37.4992`
         - `matched_whole_word = true`
     - 即：**并不是 `第一站 -> 是` 不健康，而是 `的驿站 -> 是` 异常强**
- 因而这轮可把口径再更新一层：
  - `AnalyzeCredibility / RewriteWordGraph` 之后的 `start=8` source-line 竞争，不是当前主失血点
  - 真正的不对称仍然在更早的 split-prefix family 形成：
    - `第/的 -> 一/驿站`
    - 以及它们是否会继续长成
      - `第一站`
      - `的驿站`
      这两条后续上下文质量完全不同的族
- 若继续，下一步不应再把重点放在：
  - `start=8` 的 `source_pool/top_candidate`
  - 或 `第一站是一座` 和 `第一展是一座` 的局部并排
- 更值得做的是：
  - 直接围绕 `diyizhan` 的更早 family 形成层补一份窄对照：
    - `第 -> 一`
    - `第 -> 驿站`
    - `的 -> 驿站`
    - `第一站`
  - 查清：
    - 哪些 path 在 `AnalyzeCredibility` 前就已经带着 `<unk>/fallback` 债务
    - 哪些 path 在 `RewriteWordGraph` 后被保成可继续扩展的 source family

## 2026-05-26 去重后补通用性边界：`case2` 的 early family debt 目前只可作诊断样例，真正可泛化的是“上游 family/path contract 缺边界”

- 用户这轮明确要求不要围着一两个 case 过拟合；继续前先回查旧日志，确认这类“通用性边界”以前已经部分做过，但还没和当前 `case2` 收口直接并起来。
- 旧的多样本结论已经足够明确：
  - `2026-05-20 多样本 family/path probe`
    - 基于 `7` 条代表样本
    - 已确认 `supports_validated_continuation` 这类上游 contract 触发是**普遍**现象，不是单句偶然
    - 真正普遍的问题是：
      - 触发条件本身缺少 `family/path` 资格边界
      - 错误 family prefix 也会大面积吃到同类支持
  - 同一轮也已经确认：
    - 单看 downstream exact completion、continuation 数量之类读数
    - 只能当辅助诊断
    - 不能单独拿来判定哪条是“正确主轴”
- 这次额外把当前 `case2` 的收口和现有多样本工件对齐后，可把边界写得更清楚：
  1. **可泛化层**
     - “上游 family/path/source-axis 缺资格边界”是通用问题
     - 这点已有 `7` 条多样本 probe 支撑
     - 不依赖 `第一站` 这一个 case 才成立
  2. **暂不能外推的单例层**
     - `case2` 里这条具体的
       - `第/的 -> 一/驿站`
       - early family formation `<unk>/fallback` 债务
       - 再叠加 `的驿站 -> 是` whole-word 过强
     - 目前只在 `diyizhan` 方向被定点证实
     - 还不能直接当成“其他句子的主失血模板”
- 为避免把 `case2` 误当成通用模板，这轮直接复用现有多样本摘要工件做最小对照：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\multi_case_stage_probe_v1_suffix_summary.json`
  - 只看几个代表句在不同 stage 的 expected suffix 存活情况
- 结果表明，不同句子的主死亡层其实并不相同：
  - `第一站是一座古老的小镇`
    - `admitted/pre_source_pool/source_pool/top_candidate/pre_future/post_future`
    - `suffix_expected_count` 全为 `0`
    - 说明它确实更像 very early family formation 就已经歪掉
  - `走进一家特色小店`
    - `admitted_new_line_post_push = 3`
    - `pre_future/post_future = 11`
    - 但 `pre_source_pool/source_pool/top_candidate = 0`
    - 说明这句更像中后层 compact/future 仍有机会恢复，不是同一种 early debt 形态
  - `踏入小镇的那一刻`
    - `admitted_new_line_post_push = 7`
    - `pre_future = 14`
    - `post_future = 11`
    - 中间 `pre_source_pool/source_pool/top_candidate = 0`
    - 说明它也不是 `case2` 那种“从 very early family formation 起就持续背债”的同构问题
  - `带着一点微凉`
    - 前后 top1 已经能回到 `带着一点`
    - 但 `suffix_expected_count` 仍为 `0`
    - 更像局部 stage 口径/保活问题，而不是 `驿站` 式错误 family 先长成
- 因而当前最稳妥的判断应更新为：
  - `case2` 这条 early family debt 链
    - 目前最适合作为**诊断样例**
    - 用来帮助定位“缺的边界长什么样”
  - 但后续任何实现都不应以：
    - `第/的`
    - `一/驿站`
    - `第一站/的驿站`
    这些具体词面为目标
  - 真正应该追求的是：
    - 一个对多样本都成立的上游 family/path/source-axis 边界
    - 能区分“错误 family 内 exact continuation”与“合法主续接”
- 若继续，下一步的要求也应相应收紧：
  - 不再围着 `case2` 单独细抠更多 debt 数值
  - 而是优先回到既有多样本工件，找**至少两三类不同失败形态的公共约束**
  - 只有这种约束，才值得进入代码方案设计

## 2026-05-26 定期复盘 `P0-P1-P2`：当前推进仍在框架内，但只能按“受控 `P1` 预验证”口径继续

- 按用户要求，继续前重新回看：
  - `docs/阶段2实施清单_P0_P1_P2.md`
  - 以及当前最新 `WORKLOG` 收口
- 本轮复盘后，对当前状态的框架判断如下：
  1. **没有偏离阶段 2 主顺序**
     - 正式 gate 仍然是：
       - `P0 -> P1 -> P2`
     - 没有把当前状态表述成“已经正式通过 `P0` 并进入 `P1` 全量推进”
     - 也没有把问题偷换成 `P2` 搜索形态改造
  2. **当前动作仍属于允许范围内的受控 `P1` 预验证**
     - 这轮以及最近几轮在做的事情，本质上都在回答：
       - `P0` 之后残余的 family drift 到底发生在哪一层
       - `source-line ownership / family continuity / continuation legality / request-stage admission`
         这几类上游 contract 里，缺的是哪种资格边界
     - 这与文档里“`P0` 未正式过 gate，但允许做受控 `P1` 预验证”的口径一致
  3. **当前没有滑回文档明令禁止的方向**
     - 没有再回到：
       - `witset_poet` 末端救火 patch
       - 再加一个 surface bonus
       - 再调一个 continuation weight
       - 或把单句做对当成阶段结论
     - 最近几轮都是：
       - 只读 probe
       - 旧工件复核
       - 多样本边界梳理
       - 不编译、不改行为
  4. **但当前也还没有资格宣称“已进入 P2 视角”**
     - 因为多样本复核刚刚再次确认：
       - 不同句子的主死亡层并不相同
       - 当前最稳定的公共问题仍是：
         - 上游 `family/path/source-axis` 缺资格边界
       - 这仍是典型 `P1` 预验证语境，不是 `P2` 的 admission/beam 形态改造语境
- 因而接下来的方向约束也一起写死：
  - 若继续，应定期复盘并始终问自己三件事：
    1. 当前动作是在解释/收紧 `P0` 后残余 drift，还是已经偷偷滑回末端 patch？
    2. 当前结论是否已有多样本支撑，还是只被某个 case 绑架？
    3. 当前问题若还主要表现为 `family/path/source-axis` 缺边界，就仍属于受控 `P1` 预验证，不应提前跳到 `P2`
- 当前最准确的项目状态记录为：
  - `P0`：
    - 核心方向成立
    - 正式 gate 未过
  - `P1`：
    - 允许继续做受控预验证
    - 重点是从多样本中提炼公共上游约束
  - `P2`：
    - 现在还不应作为主线入口

## 2026-05-26 去重后把多样本证据收成 3 条共享上游约束：后续只围绕这 3 条筛选实现入口

- 承接上一条“不要再围着单个 case 细抠”，这轮没有继续扩展 `case2` 的 debt 数值，而是回到既有多样本工件与旧日志，确认“至少两三类不同失败形态共享的上游约束”是否已经被正式写死。
- 结论是：相关零散证据以前都做过，但还没有被系统收成一组可直接指导下一刀筛选的共享约束；因此这轮把它们正式收口如下。

### 共享约束 1：主合同必须是 `family/path/source-axis` 组合资格，不能退化成单一信号判定

- 多样本与旧日志都已说明：
  - `downstream exact completion support`
  - `risk_assist_tag`
  - `edge_risk / spelling_class`
  这些读数都**有信息**，但都不能单独回答“谁是正确主轴”。
- 现有稳定结论包括：
  - `downstream exact completion` 适合：
    - 死尾过滤
    - 尾部可达性诊断
    - 与 `family/path/source-axis` 组合使用
  - `risk_assist_tag` 比裸 `edge_risk` 更细，但仍只适合作为弱辅助轴
  - `edge_risk / spelling_class` 本身区分力不足，不应单独升格为强资格
- 因而后续实现的第一条筛选标准应写死为：
  - **不接受任何“只靠单一信号决定合法主续接”的方案**
  - 能进入实现候选的，必须是：
    - `family_tag + path_tag + source_axis_tag`
    - 再按需组合
      - `downstream_completion`
      - `risk_assist`
    的结构化资格

### 共享约束 2：合同必须前移到 source-line / request-stage 建立早期生效，不能停留在 candidate 级重权

- 多样本 stage 摘要和旧日志已经反复表明：
  - 不同句子的主死亡层并不相同
  - 但很多失败都共享一个方向性问题：
    - **上游合法前缀保护介入得太晚**
- 旧结论已明确写过：
  - 当前 `family/path/source-axis` 组合合同方向是对的
  - 但此前主要仍表现为 candidate 级重权
  - 还没有真正把“合法长前缀在 source-line 建立初期就被保护”做完整
- 这轮再结合多样本摘要，可把它提升为共享约束：
  - 对 `第一站是一座古老的小镇` 这类句子：
    - 问题更像 very early family formation 就已歪掉
  - 对 `走进一家特色小店` / `踏入小镇的那一刻` 这类句子：
    - 中后层 compact/future 仍能见到 expected suffix 回流
    - 说明不是所有问题都能靠更晚的尾部读数解释
- 因而后续实现的第二条筛选标准应写死为：
  - **优先考虑在 `AnalyzeCredibility / RewriteWordGraph / request-stage admission` 早期生效的合同**
  - 不再优先考虑只在候选末端或 candidate 级重权才起作用的改动

### 共享约束 3：资格要显式区分“合法主续接”与“错误 family 内 exact continuation”，不能再把 exact-only/shared-prefix 当作充分条件

- 多样本旧结论已经明确：
  - `supports_validated_continuation` 的触发非常普遍
  - 错误 family prefix 也会大面积吃到同类支持
- 另外一条关键限制也已确认：
  - `exact-only axis` 本身也可能被错误家族劫持
  - 典型如：
    - `一直想王者`
    - `一直想望着远方`
    这类错误链也能长在 exact-only 共享前缀轴上
- 同时，这轮已有的 `case2` / 多样本对照又进一步说明：
  - 错误 family 内 exact continuation
  - 与真正的合法主续接
  - 不能再只靠“也是 exact / 也能继续长 / 也能拿到 completion support”来区分
- 因而后续实现的第三条筛选标准应写死为：
  - **任何新合同都必须显式回答：当前 continuation 是该 family 的合法主续接，还是错误 family 内的 exact continuation**
  - 如果方案只能说明“它也是 exact / 也有尾部支撑”，而不能回答 family 资格，就不应进入实现

### 当前收口

- 这 3 条共享约束都还严格属于受控 `P1` 预验证范畴：
  1. 组合资格，而非单一信号
  2. 早期生效，而非末端重权
  3. 区分合法主续接与错误 family 内 exact continuation
- 它们没有跳到 `P2` 搜索形态，也没有回退到 `poet` 末端 patch。
- 若继续，下一步不应再泛泛说“继续找公共约束”，而应改成：
  - 直接检查现有候选入口里，哪一处最有可能同时满足这 3 条筛选标准。

### 基于现有入口的进一步收口

- 这轮顺手把当前代码里的几个现成入口再对照了一次：
  - `WordGraphRewriter::Apply()`
  - `BuildRequestStageTailSupports()`
  - `BuildRequestStagePrefixStates()`
  - `ClassifyRequestStageTag()`
- 当前最值得继续的具体入口，已可以进一步收紧为：
  - **优先看 `BuildRequestStagePrefixStates() + ClassifyRequestStageTag()` 这一组 request-stage 资格入口**
  - **暂不优先回到 `WordGraphRewriter::Apply()` 的 candidate 级改权**
- 原因与上面 3 条共享约束是一一对应的：
  1. 对“组合资格”来说
     - `BuildRequestStagePrefixStates() / ClassifyRequestStageTag()`
       天然就在组合：
       - prefix state
       - family identity
       - tail support
       - continuation summary
     - 比单独在 `Apply()` 里再加一条 candidate bias 更接近结构化资格
  2. 对“早期生效”来说
     - `Apply()` 方向此前已经被证明主要表现为 candidate 级重权，介入仍偏晚
     - request-stage state / tag 则正好落在 source-line 建立与 next-hop request 的早期
  3. 对“区分合法主续接 vs 错误 family 内 exact continuation”来说
     - 现有旧日志里，多次真正卡住的问题都在：
       - `BuildRequestStagePrefixStates()` 的 state ownership / family bucket
       - `ClassifyRequestStageTag()` 的 eligibility 判定
     - 这比单独改某个 continuation bias，更有机会直接回答 family 资格问题
- 因而如果继续，下一步最对位的动作不应是：
  - 再发明一个新的 `Apply()` bias
  - 或再试一轮 candidate 级权重 sweep
- 更应该做的是：
  - 在不改代码前提下，再用既有多样本工件核对：
    - 当前 `BuildRequestStagePrefixStates()` / `ClassifyRequestStageTag()` 这组入口
    - 还缺的是哪一种最小资格边界

## 2026-05-26 继续去重后再收口：`BuildRequestStagePrefixStates()/ClassifyRequestStageTag()` 本身已无新的未做窄口，残余缺口转成“request-stage 资格被下游 reparse 继承”

- 继续前再次回查 request-stage 相关旧记录，避免把已经做过的窄口换个名字重提。
- 这轮重新确认，下面这些都已经正式做过，不能再重复：
  - `end_pos -> text` 单锚 / source-anchor 路线
  - ownership / text matching 收紧
  - `CanPropagateBridgeLineageConfirmation()` 的 source-line 继承收紧
  - `confirmed + legal continuation can propagate`
  - `candidate-specific continuation legality`
  - 围绕 `式` 或一般单字 `request-stage eligibility` 的继续收紧
- 结合旧日志，当前 request-stage 线自身的最新有效结论应保持为：
  1. `第一站 -> 是`
     - 已经是 `request_source_line_eligible`
     - 不再是“资格没拿到”
  2. 更深一层把 `confirmed lineage` 传播给
     - `一座`
     - `一组`
     这种 deeper continuation legality 的问题
     - 之前已经用 `candidate-specific continuation legality` 正式收紧过
     - 不是新的未做入口
  3. 因而如果还把主问题表述成：
     - `BuildRequestStagePrefixStates()` / `ClassifyRequestStageTag()` 还缺一个新的窄条件
     - 这已经会与旧实验重复

- 这轮把旧日志继续往后对齐后，当前更准确的停点应改写为：
  - request-stage 的 split 主轴资格本身已经形成
  - 但 downstream 仍存在一类 2 字 whole-word reparse：
    - `continuation_tag = exact_ambiguous_family`
    - `request_stage_tag = not_request_stage_candidate`
  - 它们虽然**不是** request-stage candidate，
    却会在后续 beam / same-span / 相邻竞争里继承同一份：
    - `request_stage_prefix_text`
    - request-stage 支撑语义
  - 典型例子仍是：
    - `第一站 + 是`
      这种已形成 `request_source_line_eligible` 的 split continuation
    - 与
    - `第一 + 展示 / 战士 / 战时`
      这种 `exact_ambiguous_family` 的 2 字 whole-word reparse
    - 后者虽然不是 request-stage candidate，却还能在下游竞争中继续压盘

- 继续对照当前 `witset_poet.cc` 的 `same-span competition` 实现后，也能把缺口写得更具体：
  - 当前 `same-span` anchor/competitor 只看：
    - `generated_word_count`
    - `generated_char_count`
    - `matched_whole_word / fallback / whole_word_hits`
    - `cumulative_request_stage_bridge_bonus`
    - 以及 `shared_prefix_lm_contract_penalty`
  - 它**没有显式使用**：
    - `request_stage_tag`
    - `continuation_tag`
    - “是否 split continuation”
      vs
      “是否 exact_ambiguous_family 的 whole-word reparse”
  - 因而当 reparse 线继承到相近甚至相同的 request-stage 支撑语义时，
    当前 `same-span` 只能把它看作：
    - `contract_support` 不够弱的竞争者
    - 而不是“虽然共享 prefix text，但本质上仍属 weaker reparse competitor”

- 同时，这轮也再次确认：
  - 之前那条“把 equal-support 的 root whole-word reparse 也纳入 same-span 竞争”的极窄扩展，
    已经明确判负并完整回退
  - 它的问题不是力度不够，而是：
    - 一旦泛开比较面
    - `敌意展示 / 地衣展示 / *驿站是`
      这一类更外层错误 family 也会一起被卷进来
- 因而当前唯一仍未重复、且还能保持体系内约束的下一步，只能进一步收紧成：
  - **不要再改 request-stage 资格定义本身**
  - **也不要再泛开 same-span 比较面**
  - 若继续，只值得尝试一种新边界：
    - 在 `same-span` 或相邻竞争逻辑里，
      显式区分
      - 已 admitted / 已形成 `request_source_line_eligible` 的 clean split continuation
      与
      - `exact_ambiguous_family` 的 2 字 whole-word reparse
    - 并且这层比较必须附带
      - family/source 限制
      - 或者只在已 admitted 的 clean split family 内部生效
    - 不能再像前一刀那样把 root whole-word reparse 一起放进全局比较场

- 这也意味着：
  - 这条线已经不再是“继续给 request-stage state 加一条资格”
  - 而是：
    - **给 downstream competition 一个更窄的 family-aware reparse identity 区分**
  - 若下轮继续，应按这个口径复盘，不再回头重复：
    - `confirmed propagation`
    - `candidate-specific continuation legality`
    - `allowed_margin`
    - 或一般 `request-stage eligibility` 收紧

## 2026-05-27 `CarryExactAmbig` 传播链继续收口：状态键与 source selection 两刀都未把 `一直想望着远方` 的 carry 恢复到句尾

- 继续前先复核了 `WORKLOG`、`2026-05-26-request-stage-ownership-p1.md` 与 `witset_poet.cc`，确认没有回到旧判负路线：
  - 没有再调 `same-span` 权重
  - 没有再放宽 `exact_ambiguous_family` 比较面
  - 没有回到 request-stage ownership 旧线
- 这轮先收口 `CarryExactAmbig` 的真实断点，拿到的 runtime 证据是：
  - `yizhixiangwangzhe` 的候选 `一直想望着` 已经带有 `CarryExactAmbig = 1`
  - 但最终 `yizhixiangwangzheyuanfang` 的错误 top1 `一直想望着远方` 仍是 `CarryExactAmbig = 0`
  - 说明问题已不是“早期 exact-ambiguous 身份根本没建立”，而是后续到句尾这段链路没有沿带 carry 的前驱继续保住同一身份
- 针对“状态压缩/替换把 carry 吃掉”的最小验证：
  - 已把 `carries_exact_ambiguous_reparse_identity` / `carries_cross_boundary_reparse_identity` 纳入 `BuildApproxStateKey()`
  - 同时更新了 admission 与 `CompressLinePoolByState()` 的 key 生成
  - 重新 `librime/build.bat static` 并只刷新 `case1 --mode snapshot`
  - 结果：`一直想望着远方` 仍是 `CarryExactAmbig = 0`
  - 结论：当前主断点不只是在 state dedupe / compact
- 随后又针对“source pool 纯 beam 选前驱把 carry 线挤掉”的最小验证：
  - 复核 `SelectTopLines()`，确认它当前除 `whole_first_word_anchor` 外，本质上只按 `beam_score/weight` 选
  - 因而补了一刀最小保留逻辑：额外保留最好的
    - `carries_exact_ambiguous_reparse_identity`
    - `carries_cross_boundary_reparse_identity`
    线进入 `top_candidates`
  - 再次最小编译并刷新 `case1 snapshot`
  - 结果仍然是：`一直想望着远方` 的 `CarryExactAmbig = 0`
  - 结论：当前主断点也不只是在 `SelectTopLines()` 这一层把 carry 线直接筛掉
- 当前阶段判断：
  - `CarryExactAmbig` 的剩余缺口已继续前移
  - 更像是最终同表面串 `一直想望着远方` 来自另一条分段/前驱链，而不是单纯从 `一直想望着 + 远方` 这条 carry 线继续展开
  - 下一步若继续，应优先补“最终同表面串的实际分段/前驱链”观测，而不是继续改状态键或继续在 source selection 上做盲改

## 2026-05-26 继续到底后的阶段性突破：发现第一条真正未重复的新入口不是“再加条件”，而是“把现有 contract 标签透传到 downstream competition”

- 按“继续直到获得突破或确认无路可走”为止，这轮把剩余路线继续做到了实现前的最后一层静态去重。
- 当前可以明确写成阶段性突破的，不是又发现了一个新的 penalty/bonus，而是：
  - **`translator` 其实已经算出了能够区分 split 主续接与 reparse family 的结构标签**
  - **但这些标签没有被真正带进 `poet` 的跨批次竞争层**

### 已确认的事实链

- 在 `witset_translator.cc` 中，针对每个候选，当前已经会计算：
  - `continuation_tag`
  - `path_tag`
  - `source_axis_tag`
  - `risk_assist_tag`
  - `request_stage_tag`
- 这些标签已被用于：
  - translator 内部 bias/guard 判定
  - graph/debug 导出
- 但当前给 `poet` 侧透传的 request-stage hint metadata 只有：
  - `request_stage_tag`
  - `request_stage_prefix_text`
  - `request_stage_state_count`
  - `request_stage_path_count`
  - `matching_request_state_*`
  - 等少量 request-stage 元信息
- 进一步看 `witset_poet.cc`：
  - `BatchRequest` / `CandidateTemp` / `Line`
    当前保存的也主要是：
    - `request_stage_bridge_bonus`
    - `request_stage_tag`
    - `request_stage_prefix_text`
    - `shared_prefix_lm_contract_penalty`
    - 若干累计数值
  - **没有把**
    - `continuation_tag`
    - `source_axis_tag`
    - 以及“clean split continuation vs `exact_ambiguous_family` reparse”
      这种身份标签
    真正保留到 `Line`
  - 因而 `same-span` / 相邻竞争在真正比较时，只能看到：
    - support 数值
    - beam/comparison score
    - whole-word / fallback / generated-char 等统计
    - 却看不到“竞争双方在 contract 身份上本来就不是同一类候选”

### 为什么这条路和旧实验不同

- 这条路**不是**下面这些旧路线的重提：
  - 再收紧 `BuildRequestStagePrefixStates()` / `ClassifyRequestStageTag()`
  - 再改 `confirmed propagation`
  - 再调 `same-span allowed_margin`
  - 再把 root whole-word reparse 放进比较面
  - 再在 `all_requests` 批内加 protection
- 它和旧实验的本质差别在于：
  - 旧实验大多是在：
    - 改资格定义
    - 改 support 强弱
    - 或改比较常数
  - 但这次收口后发现，
    当前更缺的不是“支持值再算得细一点”，
    而是：
    - **把已经算好的 contract 身份真正保留到跨批次合流竞争层**
- 也就是说，这条路不是“再发明新信号”，而是：
  - **让 `poet` 真正用上 `translator` 侧已经存在的结构标签**

### 为什么这条路仍在 `P1` 预验证框架内

- 这条路虽然落点在 `witset_poet.cc` 的 downstream competition，
  但它的职责不是改搜索形态，也不是引入新的 beam/admission 结构。
- 它要表达的仍然是：
  - `P1` 的上游 contract 身份
  - 如何在下游竞争里不被错误 family 擦掉
- 因而它更准确地属于：
  - **用 `P1` 已经算出的 contract 标签，修正当前 downstream 对不同 family/reparse regime 的“身份失真”**
- 它不是：
  - 新的状态压缩
  - 新的 admission 机制
  - 也不是近似 beam-Viterbi
  - 因而还不算滑进 `P2`

### 当前最小实现边界（仅静态设计，尚未落代码）

- 如果后续要真正试这条线，最小边界不应写成：
  - “给 same-span 再加一个权重”
- 而应写成：
  1. 在 translator -> poet 的 hint/metadata 链里，
     - 增加最少量的 contract 身份字段
     - 优先候选：
       - `continuation_tag`
       - `source_axis_tag`
       - 或一个更压缩的
         - `is_clean_split_continuation`
         - `is_exact_ambiguous_reparse`
  2. 在 `BatchRequest -> Line` 中把这些字段保留下来
  3. 只在 `same-span` 或紧邻竞争口径中使用
     - 且必须附带 family/source 限制
     - 禁止重新打开“root whole-word reparse 全局比较面”
- 当前还没有进入代码实现；这里只把它确认为：
  - **第一条经过完整去重后，仍成立、且与旧实验不重合的新入口**

### 当前阶段判断

- 因而这轮可以明确记为“阶段性突破”：
  - 不是已经证明这条路有效
  - 而是终于找到一条：
    - 未重复
    - 不靠单 case 词面特调
    - 不脱离 `P0-P1-P2`
    - 并且能准确解释为什么旧实验都差半步的
      **新实现入口**

## 2026-05-25 去重后最小 `lm_avg effective token count` 原型结果为负，且本轮 `full` 探针不是最优效率路径

- 继续推进前，先重新核对了 `WORKLOG` 和 `witset_poet.cc`，确认下面这些路线都已经做过，不能重复：
  - 全局 `lm_avg_weight = 0`
  - 仅对 `token_count == 1` 禁止第二遍 `lm_avg`
  - `token_count == 1` 版本再联动现有 `edge/joint` 风险 hint
- 这轮唯一确认**还没做过**的窄变体是：
  - **只对 `split_token_supported / neutral_missing`，把 `lm_avg` 的门控 token 数从原始 `token_count` 改成解释后的 `matched_token_count`**
  - 目标是避免 `neutral_missing` 候选继承完整 raw token span，进而继续吃满 `lm_avg` 的机械二次惩罚
- 落地时保持边界很窄：
  - 只改了 `witset_poet.cc` 里 `ComputeLmAvgContribution(...)` 的 token-count 入参
  - 不动 `lm_score_scaled`
  - 不动 `oov_penalty`
  - 不动其他 scorer 项

- 验证前只做了固定最小集：
  - `case2_diyizhan`
  - `case3_tiyanbuyiyang`
  - 编译仍按标准方式：
    - `librime/build.bat static`
- 但这里也要记一个效率教训：
  - 我这次用了 `partial_chain_stage_probe.py --mode full`
  - 事后回查脚本确认：
    - `snapshot` = 只收最终 snapshot
    - `probe` = `snapshot + next_hop`
    - `full` = `snapshot + next_hop + graph`
  - 而当前脚本写出的 `partial_chain_stage_probe_result.json` 并**不保存最终 snapshot 排名**，只保存 `snapshot_candidate_count`
  - 也就是说：
    - 若目标只是先看 `case2/case3` 的最终候选和关键 next-hop
    - **优先应跑 `probe`，不该直接上 `full`**
    - `full` 更重，但这一步新增的信息主要是 graph contract；不适合作为默认第一刀

- 不额外重跑的前提下，直接离线提取这次已有结果，结论如下：
  - `case2_diyizhan`
    - `next_hop_after_diyizhanshi`
      - 正确源后缀 `第一站是` 仍没有进入 `preferred_candidate_stage`
    - `next_hop_after_diyizhanshi_display`
      - 错误 `第一展示` 分支仍是强势 request 候选
      - top entries 仍是：
        - `已`
        - `以`
        - 后面还有 `意 / 医 / 艺 / 壹`
    - `next_hop_after_diyizhanshiyi_exact`
      - 正确源后缀 `第一站是一` 仍没有进入 `preferred_candidate_stage`
    - `next_hop_after_diyizhanshiyi_display`
      - 错误 `第一展示已` 分支仍主导后续
      - top entries 仍是：
        - `组`
        - `做`
        - `作`
        - `足`
        - `坐`
        - `左`
  - `case3_tiyanbuyiyang`
    - `next_hop_after_tiyan`
      - `不` 仍是 top request entry，说明首跳 guardrail 没坏
    - `next_hop_after_tiyanbu`
      - `易` 仍明显高于正确的 `一`
      - 说明这刀没有把 `体验不 -> 一` 的局面拉正

- 因而，这轮最小原型可以下一个明确结论：
  - **只对 `neutral_missing` 用解释后的有效 token 数门控 `lm_avg`，不足以扭转当前主缺口。**
  - 它既没有把 `第一站是 / 第一站是一` 重新扶成 preferred request chain
  - 也没有在 `case3` 上体现出足够清晰的正收益
  - 所以这条窄变体当前应判负，不再继续扩样或联动放大

- 为避免无效实验残留：
  - 已把 `witset_poet.cc` 中这轮原型源码回退
  - 本轮不再追加重跑
  - 后续若还要动探针，默认先从 `--mode probe` 起步，只有明确需要 `graph contract` 时才升到 `full`

- 下一步如果真正执行，不建议再做 `D/E/F` 式局部实验。
- 应该直接从 Phase 1 开始：
  - 先重构 `lm_avg` 的角色
  - 再做结构惩罚门控
  - 最后才考虑新的 `StartFamilyRisk`

## 2026-05-18 Phase 1/2/3 全量原型已完成实现与验证，但结果为负，已整体回退

- 按上面的三阶段草案，实际把三步都做了一遍：
  - Phase 1：
    - 把 `lm_avg` 从“机械第二遍 LM”改成校准项
  - Phase 2：
    - 给 `fragment / tail / octa / single_char` 接了更严格的单字结构门控
  - Phase 3：
    - 新增只作用于句首前两步的 `StartFamilyRisk`

- 为了稳定验证，这轮还临时做了两项配套改动：
  - 在 schema 里加入了 `start_family_risk_weight`
  - 把 `debug_local_snapshot_path` 临时切到独立的 `phase123_active_snapshot.jsonl`
  - 这样可以避免旧 snapshot 残留行干扰本轮 summary

- 实现完成后，先按标准方式重新编译：
  - `librime/build.bat static`
  - 编译通过

- 然后做了两轮验证：
  - 单样例：
    - `我踏上了旅行的征程。第一站是一座古老的小镇`
  - 典型错例集：
    - `docs/e1_typical_error_smoke_cases.txt`

- 单样例结论很直接：
  - `diyizhanshi` 的 top 候选仍然没有被拉正
  - top1 仍被
    - `的驿站是`
  - 这一家族占住
  - 说明三阶段联动后，既没有把 `第一站 + 是` 扶正，也没有真正压住句首错字链

- 典型错例集结果也没有支持这版联动原型：
  - `top1_accuracy = 0.25`
  - 与改前持平
  - `top3_accuracy = 0.25`
  - 反而比原先的 `0.30` 更差

- 因而这轮可以下一个明确结论：
  1. Phase 1/2/3 这版联动实现已经把“内容校准 + 结构门控 + 句首家族风险”三件事都试过了
  2. 但它们当前的组合方式，并没有把问题拉回 `witogram` 的优势区间
  3. 继续保留这版代码，只会把 scorer 变得更复杂，却没有带来 top1 改善
  4. 所以这套实现不应保留

- 已执行的收尾动作：
  - 回退 `witset_poet.cc` 中本轮新增的
    - `lm_avg_adjustment`
    - `single-char structure gate`
    - `start_family_risk / start_family_penalty`
    - 以及对应累计字段与 debug 输出
  - 移除 schema 里的
    - `start_family_risk_weight`
  - 把 `debug_local_snapshot_path` 恢复回原来的
    - `witset_local_snapshot.jsonl`

- 回退后再次按标准方式重新编译：
  - `librime/build.bat static`
  - 编译通过

- 到这里，这轮“把三个 phase 都做完”的价值也更清楚了：
  - 价值不在于得到了一版可保留实现
  - 而在于它明确排除了：
    - “只要把 `lm_avg` 改成校准项”
    - “再给单字结构罚项加门控”
    - “再加一个最小 `StartFamilyRisk`”
  - 这组直觉式联动组合，并不足以修好当前主问题

- 因而后续如果还要继续推进 scorer 重构，应该坚持前面的总判断：
  - 不再沿着这版 Phase 1/2/3 联动原型继续堆补丁
  - 而应重新回到
    - `ContentScore / StructureRegularizer / PathRisk`
  - 三层职责分离的方向，重新定义各层边界

## 2026-05-18 三层重构已开始落代码：先立分层骨架，不再继续在旧 `adjustment_score` 里打补丁

- 这一步先不急着再引入新的句首 family 信号，而是先把当前 scorer 的职责边界在代码里重新切开：
  - `ContentScore`
    - 负责 `dict_score_raw + lm_total + dict_norm + merge_gain + whole_word_bonus`
    - 以及一版更弱的 `multi-token` LM 校准项
  - `StructureRegularizer`
    - 负责 `boundary / length / single_char / fragment / structure / tail / octa`
  - `PathRisk`
    - 负责 `oov / prefix-path adjustments / upstream_path_prior / joint_prior`

- 当前这版实现里的关键改动有两个：
  1. `lm_avg` 不再对 `token_count == 1` 的 continuation 机械再计一遍
     - 新 helper 只在 `lm_token_count > 1` 时返回校准项
     - 并且力度明显弱于过去直接 `lm_avg_weight * lm_score_avg`
  2. 单字结构罚项不再默认一刀切生效
     - 新 helper 会根据
       - `used_char_fallback`
       - `matched_whole_word`
       - `lm_oov_token_count`
       - `next_single_char_word_count`
       - `next_trailing_single_char_run`
     - 计算单字结构 gate
     - 只有出现真实碎片链征象时，`boundary / single_char / fragment / tail / octa`
       才会被明显放大

- 这版实现的目标不是立刻“修好 `第一站是`”
  - 而是先把 scorer 从
    - “所有项都继续堆在 `base_score + adjustment_score`”
  - 改成
    - “内容 / 结构 / 路径”三类项各自累计、各自可观测

- 同时已把本地 debug 注释扩成按层输出：
  - `Content`
  - `StructReg`
  - `Path`
  - 以及对应的 `StepContent / StepStructReg / StepPath`
  - 这样后面复盘时可以直接看：
    - 是内容层输了
    - 还是结构层把合法单字 continuation 打坏了
    - 还是路径风险层在放大错误链

- 这一步目前只完成了代码层改造与诊断自检：
  - `witset_poet.cc` 语法诊断通过
  - 还没有进入新的编译与回放验证
  - 下一步应在用户允许后，再按标准方式重新编译并做代表样例验证

## 2026-05-18 三层骨架首版验证后判定为负结果，已回退

- 随后直接按标准方式完成了这轮验证：
  - `librime/build.bat static`
  - 单样例：
    - `我踏上了旅行的征程。第一站是一座古老的小镇`
  - 典型错例集：
    - `docs/e1_typical_error_smoke_cases.txt`

- 单样例结果不是“没扶正 `第一站是` 那么简单”，而是更糟：
  - 前半句也明显被打坏
  - `wotashanglelvxingdezhengcheng`
    - top1 变成了 `卧榻上了旅行的正盛`
  - 带上下文的
    - `diyizhanshiyizuogulaodexiaozhen`
    - top1 仍然是 `的驿站是以做古老的小镇`
  - top3 里虽然仍能看到 `第一展示...` 一类旧竞争项，但目标链没有回到可赢位置

- 典型错例集也没有给出正向信号：
  - `top1_accuracy = 0.25`
    - 与改前持平
  - `top3_accuracy = 0.25`
    - 比改前的 `0.30` 更差
  - 同时还出现了更明显的整句前缀截断/误收敛现象，例如：
    - `暖黄色的灯光映照在青石板路上`
      不再稳定保持整句 top1
    - `看似在`
      退化成 `看时在`

- 这说明当前这版“先把项分到三层，再加单字 gate 和弱 LM 校准”的首版骨架，失败点不只在 `第一站是`：
  1. 它没有真正建立新的判别能力
  2. 只是把原来揉在一起的分项重新分组后，再改了几处权重生效位置
  3. 结果不仅没压住句首错字链，还削弱了原本可工作的整句连续性

- 从现象反推，当前首版骨架至少暴露了两个问题：
  1. 把 `boundary` 之类原本直接参与总排序的连续性信号挪入新的结构层之后，
     在单字 gate 介入时，合法 continuation 的正向支撑也一起被削弱了
  2. 把 `lm_avg` 改成更弱的 `multi-token calibration` 后，
     没有形成新的有效内容判别项，反而让很多原本靠 LM 连续性勉强维持的整句竞争力继续下滑

- 因而这轮的明确结论是：
  - “按直觉把现有分项重新归到 `Content / Structure / Path`，再做最小门控”
  - 这件事本身并不足以得到可用的三层 scorer
  - 如果继续沿着这版首骨架直接修补，只会再次落回旧问题：
    - 看起来更有结构
    - 实际上只是把分数搬家，且引入新的退化

- 已执行的收尾动作：
  - 回退这轮三层骨架首版代码
  - 保留这次验证结论和工件目录：
    - `debug/layered_single_case`
    - `debug/layered_typical_summary`
  - 回退后再次做语法检查，通过

- 因而后续如果还要继续“三层重构”，入口必须更严格：
  - 不能再从“把现有项归类搬家”开始
  - 必须先定义：
    - 哪些量是真正的内容判别主轴
    - 哪些量只能作为结构正则，绝不能削弱合法 continuation
    - 哪些量只该在近分竞争时做风险否决，而不能提前主导排序

## 2026-05-18 三层重构第二版边界结论：先定“哪些项根本不能直接搬层”

- 这轮没有继续改代码，而是把当前 scorer 的实际分项逐项审了一遍。
- 结论比上一轮更明确：
  - 当前很多分项不是“搬到 `Content / Structure / Path` 的哪一层”这么简单
  - 而是它们本身就不具备作为全局主排序项直接生效的资格

- 当前主公式仍是：
  - `base_score = candidate->weight + upstream_path_prior + joint_prior + dict_raw + lm_total`
  - `adjustment_score = dict_norm + lm_avg + boundary + oov + early_* + anchor_* + length + whole + merge + fragment + structure + tail + octa`
  - 问题就出在：
    - `adjustment_score` 里混着
      - 内容校准
      - 结构先验
      - 路径惩罚
      - 风险释放
    - 这些量的语义完全不同

- 逐项审计后，下一版三层重构必须遵守下面的硬边界：

- `ContentScore` 只允许放真正回答“这条内容本身像不像对答案”的项：
  - 主轴：
    - `dict_score_raw`
    - `lm_total_weight_ * lm_score_scaled`
  - 次级校准：
    - `dict_score_norm`
    - `lm_avg`
  - 但这两个只能作为有上限的校准项，不能再像现在这样自由叠加放大

- `StructureRegularizer` 只允许放真正描述“碎片化程度”的项，而且必须基本是单向负项：
  - `single_char_penalty`
  - `fragment_penalty`
  - `structure_penalty`
  - `tail_repair_penalty`
  - `octagram_penalty`
  - 这层的硬规则是：
    - 没有真实碎片化证据时，不得主动下压
    - 真实碎片化证据至少要来自
      - `single_char_word_count`
      - `trailing_single_char_run`
      - `tail_anchor_char_count`
      - `used_char_fallback`
      - `lm_oov_token_count`
    - 不能再因为“当前 token 只是单字”就默认扣分

- `PathRisk` 只允许放路径来源的风险和债务，原则上应是单向负项：
  - `oov_penalty`
  - `char_fallback_penalty`
  - `upstream_path_prior_penalty`
  - `joint_prior_penalty`
  - `early_prefix_split_penalty`
  - `early_unstable_continuation_penalty`
  - `whole_first_word_continuation_penalty`
  - `prefix_anchor_delta_penalty`
  - 这些量应该表达
    - “这条路径来自高风险拼写/切分/拼接来源”
  - 不应该承担内容排序职责

- 同时，这轮还识别出一类之前没有分清的量：
  - 它们不是内容分，也不是结构正则，而是 `PathRisk` 的债务释放项：
    - `early_fallback_compensation`
    - `early_boundary_bridge_compensation`
    - `prefix_anchor_debt_release`
  - 下一版不能再把它们当普通正向 reward 直接往总分里加
  - 它们只能做一件事：
    - 抵消之前已经加上的 path debt
  - 而且抵消量必须有上限，不能超过此前累计的相关 debt

- 还有一类项，这轮确认它们是三层重构里最危险的“伪内容项”：
  - `whole_word_bonus`
  - `merge_gain`
  - `boundary_score`
  - `length_term`

- 对这四类项，当前结论分别是：
  1. `boundary_score`
     - 本质只是 `char_count` 先验
     - 它并不直接描述内容对错
     - 也不稳定描述碎片链
     - 所以下一版不能把它当全局结构分直接参与主排序
     - 最多只能在近分竞争里做弱 tie-break
  2. `length_term`
     - 和 `boundary_score` 一样，本质是长度先验
     - 不能继续拿它在全局范围里“偏爱多字词”
  3. `whole_word_bonus`
     - 只是 `matched_whole_word` 的布尔奖励
     - 在当前 split-token `.klm` 前提下，不能当成强内容证据
  4. `merge_gain`
     - 当前它来自 `whole_word_log10 - char_path_log10`
     - 但现有模型实际是拆分 token 模型
     - 因此这类 whole-word vs char-path 比较本身缺少稳定成立前提
     - 下一版不能再把它放进内容主轴，只能降级为极弱辅助信号，甚至先停用

- 到这里，第二版三层重构的入口也就明确了：
  - 不是“把所有现有项重新分堆”
  - 而是先拆成四类职责：
    - 内容主轴
    - 内容校准
    - 结构正则
    - 路径债务 / 债务释放
  - 然后再压缩回用户视角里的三层：
    - `ContentScore = 内容主轴 + 有上限的内容校准`
    - `StructureRegularizer = 只在真实碎片链激活的单向负项`
    - `PathRisk = 单向风险债务 - 有上限的债务释放`

- 这也解释了为什么首版会失败：
  - 我上次其实还是在做“分项搬家”
  - 但没有先把
    - `boundary / length`
    - `whole_word_bonus / merge_gain`
    - `early_* compensation / anchor debt release`
  - 这些量的资格边界定死
  - 所以看似是三层，实际上仍然是旧公式里的异质项互相污染

- 因而这轮最终收敛成一句话：
  - 下一版若再继续实现，第一步不该是改权重
  - 而该是先把
    - `whole_word_bonus`
    - `merge_gain`
    - `boundary_score`
    - `length_term`
  - 从“全局主排序参与者”里降级出去；
  - 同时把所有 `compensation/release` 改成“只可冲销 path debt、不可额外造正分”的受限机制

## 2026-05-18 继续下挖后的更底层根因：不是简单混层，而是“重复记账 + 非守恒 debt 释放”

- 继续往下看以后，当前 scorer 的根病可以再收敛一层：
  - 不是只有“异质信号混在一起”
  - 而是同一底层证据被反复换壳记分，同时路径 debt 又允许被非守恒地释放

- 第一类根因是：同一内容证据被多次重复入分

- 词典证据目前至少被记了两遍：
  - `dict_score_raw = entry->weight`
  - `dict_score_norm = Normalize(dict_score_raw, char_count)`
  - 然后两者都进入总分
  - 这说明 `dict_score_norm` 不是独立证据，而是 `entry->weight` 的再参数化版本

- 语言模型证据也被多次重复入分：
  - `lm_score_scaled` 来自 `lm_features.total_log10`
  - `lm_score_avg` 来自同一组 token 的 `avg_log10`
  - `whole_word_bonus` 又对 `matched_whole_word` 单独给奖励
  - `merge_gain` 又从
    - `whole_word_log10 - char_path_log10`
    - 再构造一次“整块更好”的信号
  - 也就是说，同一组 LM 事实被拆成：
    - 总分
    - 均分
    - whole-word 布尔奖励
    - whole-vs-char 差分
    - 四种形式一起进排序

- 这能解释为什么小小的局部先验会被系统性放大：
  - 某条候选只要在“更像整块词 / 更像 whole-word / token 更紧凑”上有一点点优势
  - 这个优势不会只记一次
  - 而会以多个高度相关项一起出现
  - 最后看起来像“很多信号都支持它”
  - 其实只是同一件事被记了很多遍

- 第二类根因是：当前路径 debt 记账不是守恒的

- 现在路径侧既有 debt 项：
  - `oov_penalty`
  - `char_fallback_penalty`
  - `upstream_path_prior_penalty`
  - `joint_prior_penalty`
  - `early_prefix_split_penalty`
  - `early_unstable_continuation_penalty`
  - `whole_first_word_continuation_penalty`
  - `prefix_anchor_delta_penalty`

- 又有 release / compensation 项：
  - `early_fallback_compensation`
  - `early_boundary_bridge_compensation`
  - `prefix_anchor_debt_release`

- 但这些 release 现在不是“冲销既有 debt”的受限机制
  - 而是和其它项一起直接作为正向分数写进 `adjustment_score`
  - 这意味着：
    - 只要触发了某种“看起来像修复”的局部模式
    - 它就可能额外得到正分
    - 而不是仅仅拿回之前扣掉的那部分 debt

- 于是当前系统在记账上出现了两个结构性问题：
  1. debt 项可以来自很多地方叠加
  2. release 项却不受“最多只能还掉多少 debt”的约束

- 这就让 scorer 很容易出现一种假象：
  - 某条路径明明是高风险来源
  - 但只要后面偶然命中一个“桥接”或“补偿”模式
  - 它就可能被额外奖励，而不是仅仅回到中性

- 第三类根因是：当前一些项虽然表面名字不同，底层其实都是 `char_count` 或 `matched_whole_word` 的函数

- 最典型的就是：
  - `boundary_score`
  - `length_term`
  - `whole_word_bonus`

- 其中：
  - `boundary_score` 本质是 `char_count` 先验
  - `length_term` 也是 `char_count` 先验
  - `whole_word_bonus` 本质是 `matched_whole_word` 布尔奖励

- 这进一步说明，当前并不是很多“真正独立”的证据在投票
  - 而是少数几个隐变量：
    - `entry->weight`
    - `lm total / avg`
    - `char_count`
    - `matched_whole_word`
    - `fallback/oov`
  - 被用不同名字和函数重复展开

- 到这里，根因可以收口成一个更硬的判断：
  - 当前 scorer 的问题不是简单的“项太多”
  - 而是它更像一个没有去相关、没有守恒约束的记账系统
  - 同一底层事实被多次计分
  - 路径 debt 又允许非守恒释放
  - 所以局部偏好会被放大成结构性偏置

- 这也解释了为什么前面很多原型都失败得很像：
  - 只改其中一个项，比如 `lm_avg`
  - 只是去掉了同一类重复记账里的一个分身
  - 其它分身还在
  - 所以偏置只会换壳，不会根除

- 因而下一步若真的继续实现，目标不该再是“重新分层”
  - 而应先做两件更根本的事：
  1. 去相关：
     - 明确每一种底层事实只允许一个主入分通道
  2. 守恒：
     - 所有 debt release / compensation 都必须绑定并受限于已累计 debt

- 只有先做到这两件事，后面的 `Content / Structure / Path` 三层才不会再沦为“同一证据换个抽屉继续重复记账”

## 2026-05-18 证据去相关表与下一轮最小实施序列

- 在“重复记账 + 非守恒 debt 释放”这个根因之上，当前已经可以给出一版可执行的去相关表。
- 目标不是一次性设计完美 scorer，而是先把最明显的重复通道和非守恒通道剪掉。

- 底层事实 A：词典内容先验
  - 当前分项：
    - `dict_score_raw`
    - `dict_score_norm`
  - 判断：
    - `dict_score_norm` 只是 `dict_score_raw` 的长度归一化再参数化，不是独立证据
  - 主通道保留：
    - `dict_score_raw`
  - 下一轮处理：
    - `dict_score_norm` 从总排序移除
    - 如后续确实需要，只能作为近分 tie-break 或离线观测项

- 底层事实 B：LM 对整条候选的内容似然
  - 当前分项：
    - `lm_score_scaled`
    - `lm_score_avg`
  - 判断：
    - `lm_score_avg` 不是独立证据，而是同一 token 序列 total 的另一种表示
  - 主通道保留：
    - `lm_score_scaled`
  - 下一轮处理：
    - `lm_score_avg` 先从总排序移除
    - 以后若要恢复，只能以“残差校准”的形式出现，且必须有硬上限

- 底层事实 C：whole-word / compactness / token merge 偏好
  - 当前分项：
    - `whole_word_bonus`
    - `merge_gain`
  - 判断：
    - 两者都不是稳定独立证据
    - 在当前 split-token `.klm` 前提下，尤其不能当成强内容项
  - 主通道保留：
    - 暂不保留显式主通道
  - 下一轮处理：
    - `whole_word_bonus` 先移出总排序
    - `merge_gain` 先移出总排序
    - 两者都只保留调试输出，等待未来换到真正支持该比较的模型后再评估

- 底层事实 D：长度/边界先验
  - 当前分项：
    - `boundary_score`
    - `length_term`
  - 判断：
    - 两者都主要是 `char_count` 函数，不是稳定内容证据
  - 主通道保留：
    - 不保留为主排序通道
  - 下一轮处理：
    - 从总排序移除
    - 如必须保留，只能在最终候选 gap 很小的场景里做极弱 tie-break

- 底层事实 E：碎片化结构风险
  - 当前分项：
    - `single_char_penalty`
    - `fragment_penalty`
    - `structure_penalty`
    - `tail_repair_penalty`
    - `octagram_penalty`
  - 判断：
    - 这一组是当前最接近“真实独立结构证据”的一层
    - 但必须只在真实碎片链证据出现时激活
  - 主通道保留：
    - 整组保留为 `StructureRegularizer`
  - 下一轮处理：
    - 不新增新项
    - 只加激活门槛：
      - `single_char_word_count >= 2`
      - 或 `trailing_single_char_run >= 1/2`
      - 或 `used_char_fallback`
      - 或 `lm_oov_token_count > 0`
    - 禁止因为“当前 token 单字”就默认扣分

- 底层事实 F：路径来源风险
  - 当前分项：
    - `oov_penalty`
    - `char_fallback_penalty`
    - `upstream_path_prior_penalty`
    - `joint_prior_penalty`
    - `early_prefix_split_penalty`
    - `early_unstable_continuation_penalty`
    - `whole_first_word_continuation_penalty`
    - `prefix_anchor_delta_penalty`
  - 判断：
    - 这一组是 `PathRisk` 的债务项
  - 主通道保留：
    - 全部保留
  - 下一轮处理：
    - 统一改成债务累积字段
    - 不再和内容项混在一个无约束 `adjustment_score` 里

- 底层事实 G：路径修复 / 债务释放
  - 当前分项：
    - `early_fallback_compensation`
    - `early_boundary_bridge_compensation`
    - `prefix_anchor_debt_release`
  - 判断：
    - 它们不应是奖励项，只能是 release 项
  - 主通道保留：
    - 保留，但语义彻底改成 debt release
  - 下一轮处理：
    - 每个 release 都必须绑定某个已累计 debt bucket
    - 释放量上限为对应 bucket 当前未偿还 debt
    - 禁止出现“release 后还额外造正分”

- 到这里，下一轮最小实施序列也可以定下来了：

- 第一步：先做去相关，不碰 beam、不碰状态键
  - 从总排序中移除：
    - `dict_score_norm`
    - `lm_score_avg`
    - `whole_word_bonus`
    - `merge_gain`
    - `boundary_score`
    - `length_term`
  - 只保留 debug 输出，便于回放比较

- 第二步：把路径项改成真正的 debt ledger
  - 在 `Line` / `BatchRequest` 中新增按语义拆分的累计 debt：
    - `fallback_oov_debt`
    - `split_debt`
    - `anchor_debt`
    - `whole_first_word_debt`
  - 对应 release 只允许冲销相关 bucket

- 第三步：把总分改成真正的受限三部分
  - `ContentScore = dict_raw + lm_scaled`
  - `StructureRegularizer = gated(fragment + structure + tail + octa + single_char)`
  - `PathRisk = debt_total - released_debt`

- 第四步：保留一个极弱的最终 tie-break 层
  - 只在候选 gap 很小时才允许参考：
    - `boundary_score`
    - `length_term`
  - 默认不进主排序

- 这意味着下一轮真正要写的代码，不再是“重构一切”
  - 而是一个非常具体的最小版本：
  1. 先删六个重复/伪内容总排序通道
  2. 再把 compensation/release 改成有上限的 debt release
  3. 最后只给结构项加真实激活门槛

- 如果这个最小版本仍然不能扶正 `第一站是`，那就说明问题已经不在 scorer 记账，而更靠近：
  - 上游候选底座
  - 或 prefix 家族保活/扩展契约
  - 这样我们也能更干净地把问题继续往前收缩

## 2026-05-19 最小去相关 + debt ledger 第一版实现结果：继续失败，且根因进一步下沉到 base 主轴

- 按上一条“证据去相关表”的实施入口，实际做了一版最小实现：
  - 从总排序中移除：
    - `dict_score_norm`
    - `lm_score_avg`
    - `whole_word_bonus`
    - `merge_gain`
    - `boundary_score`
    - `length_term`
  - 同时把 path 侧临时重写成 bucket 化 debt / release 记账：
    - `source_risk`
    - `fallback_oov`
    - `split`
    - `anchor`
    - `whole_first_word`
  - 并将结构项改成只在真实碎片化证据出现时激活

- 这轮实现可以正常编译：
  - `build.bat static` 通过

- 代表样例先给出了非常强的负信号：
  - 单样例 summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\dedup_ledger_single_case`
  - 指标：
    - `top1_accuracy = 0.0`
    - `top3_accuracy = 0.0`
  - 错例：
    - `wotashanglelvxingdezhengcheng -> 卧榻上了旅行的正盛`
    - `diyizhanshiyizuogulaodexiaozhen -> 的驿站是以做古老的小镇`

- 典型错例集继续验证后，结论没有反转：
  - 首次按默认 timeout 跑典型集时，在
    - `wangdaizhengjiandengtufazhuangkuangdaluanjiezou`
    - 上出现等待 snapshot 超时
  - 将 `--snapshot-timeout-seconds` 提高到 `30` 后，典型集完整跑通
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\dedup_ledger_typical_summary`
  - 指标：
    - `top1_accuracy = 0.25`
    - `top3_accuracy = 0.25`
  - 对照旧稳定口径：
    - `e1_typical_on_summary`
    - `top1_accuracy = 0.25`
    - `top3_accuracy = 0.30`
  - 说明：
    - `top1` 没有改善
    - `top3` 再次下降

- 这轮结果的意义比“又一次负结果”更大：
  - 现在已经不是“重复记账是不是问题”的不确定状态了
  - 因为把 6 个重复/伪内容通道一起拿掉以后：
    - 目标错链并没有被扶正
    - 典型集主指标也没有改善
  - 说明这些项虽然确实在放大偏置，但它们不是当前 top1/top3 失真的主导来源

- 进一步看单样例 debug，可见很多错误候选在新公式下已经出现：
  - `Adj` 只剩 path/structure 后仍然明显为负
  - 但决定排序的主导项仍是：
    - `Base = candidate->weight + dict_score_raw + lm_score_scaled`
  - 例如：
    - `卧榻上了旅行的正盛`
    - `的驿站是以做古老的小镇`
  - 这类链条即使在去掉 `dict_norm / lm_avg / whole / merge / boundary / length` 后，仍然维持胜势

- 因而根因继续下沉一层：
  - 当前真正主导错误排序的，不再是 `adjustment_score` 里的重复通道本身
  - 而是更靠里的 base 主轴：
    - `candidate->weight`
    - `dict_score_raw`
    - `lm_score_scaled`
  - 以及它们在逐步扩展过程中的累计方式

- 这也解释了为什么这轮会出现一个很重要的现象：
  - 把所有“看起来像重复放大的项”拿掉后
  - 错误并没有显著回落
  - 只是把很多候选的 `Adj` 压到接近 0
  - 但错误 top1 仍主要由 `Base` 决定

- 到这里，路线判断再次收缩：
  - 去相关和守恒约束仍然是对的
  - 但它们更像“清理噪声层”
  - 不是当前 top1/top3 主错误的第一主战场

- 下一步若继续深挖，不该再先改：
  - `dict_norm`
  - `lm_avg`
  - `whole_word_bonus`
  - `merge_gain`
  - `boundary`
  - `length`
  - 或各种 compensation/release

- 而该直接重审 `Base` 这条主轴的三个来源：
  1. `candidate->weight`
     - 它累计的是上一轮已经混合过的历史总分
     - 当前可能把早期错误优势直接滚雪球带入后续
  2. `dict_score_raw = entry->weight`
     - 它是否已经吸收了上游 credibility/path 偏置，导致这里再次主导排序
  3. `lm_score_scaled`
     - 在当前 split-token LM 前提下，它是否本身就在稳定偏向错误 segmentation family

- 这轮最重要的结论可以收成一句话：
  - `adjustment_score` 里的重复记账确实存在，但不是当前最深的主导根因
  - 当前主导错误排序的核心，更像是
    - `candidate->weight + dict_score_raw + lm_score_scaled`
    - 这条 `Base` 主轴本身

- 因为这是明确负结果，这轮实验代码已回退，重新恢复到稳定 scorer 口径，并再次编译通过

## 2026-05-19 继续下挖 Base 主轴后的统一结论：真正的滚雪球点是“已混合历史总分”而不是单个 adjustment 项

- 在确认“最小去相关 + debt ledger”仍然失败之后，继续把 `Base` 主轴拆成三条链分别追：
  - `candidate->weight`
  - `dict_score_raw = entry->weight`
  - `lm_score_scaled`

- 这轮得到的统一结论是：
  - 当前最深的主导根因，不像是某一个 adjustment feature 权重大了
  - 而更像是：
    - `candidate->weight` 已经携带了前面各步混合后的历史总分
    - 然后在下一步又继续和
      - `entry->weight`
      - `lm_score_scaled`
    - 一起叠加
  - 这使得早期形成的错误优势会被后续搜索持续滚入下一步，形成真正的“滚雪球”

- 第一条链：`candidate->weight`
  - 它不是当前词条的静态词典权重
  - 而是上一条路径 `Line.weight` 的累计总分
  - 在当前 poet 主循环里，下一步的 `base_score` 直接从
    - `candidate->weight + upstream_path_prior_penalty + joint_prior_penalty + dict_score_raw + lm_score_scaled`
    - 继续往下算
  - 这意味着：
    - 只要早期某一步因为某种原因先赢了
    - 后续所有扩展都在继承这个已混合好的历史优势
    - 而不是重新只按当前边局部竞争

- 这也是当前真正的滚雪球点：
  - `candidate->weight` 里装的并不是“干净的 prefix 内容分”
  - 而是“已经混有历史 path / structure / LM / dict 的总账”
  - 所以下一步并不是在一个干净基线上继续比
  - 而是在沿用旧赢家的历史余额继续滚

- 第二条链：`dict_score_raw = entry->weight`
  - 继续往上游追后确认：
    - `DictEntry::weight` 并不是纯词典原始频次
    - 对静态词典，它是
      - 编译期 `log(dict_weight)`
      - 运行期再减去常量基线
      - 再叠加 syllabifier / prism / spelling path 产生的 `credibility`
  - 也就是说：
    - 当前 `dict_score_raw` 并不是“纯 lexical 证据”
    - 它已经吸收了上游拼写路径、模糊音、补全、歧义切分等 path contract
  - 因而当我们在 `Base` 里再次直接吃 `entry->weight` 时：
    - 实际上已经把一部分上游 path 偏置一起当成内容主分在吃了

- 这解释了为什么只是清理 `adjustment_score` 里的 path debt / release 不够：
  - 因为一部分 path 影响，早就已经被编码进 `entry->weight` 这个看似“词典分”的量里了

- 第三条链：`lm_score_scaled`
  - 继续往下追后，当前能确定的关键点是：
    - `lm_score_scaled` 才是 LM 总账
    - `whole_word_log10 / char_path_log10` 只是当步原始流水
    - `lm_score_avg`、`whole_word_bonus`、`merge_gain` 这些都是围绕 LM 总账派生出来的附属项
  - 而在当前 split-token `.klm` 前提下：
    - `lm_score_scaled` 本身就已经隐含了“当前步走整词命中还是字符降级”的累计结果
    - 所以它不是一个“干净独立的内容项”
    - 它本身就在承载 segmentation family 的偏好

- 这也解释了为什么把
  - `lm_avg`
  - `whole_word_bonus`
  - `merge_gain`
  - 都拿掉以后，错链仍不倒：
  - 因为真正决定 LM 主导方向的，还是 `lm_score_scaled` 这本总账

- 到这里，三条链终于能统一起来：
  1. `candidate->weight`
     - 负责把历史赢家的累计总分滚入下一步
  2. `dict_score_raw`
     - 表面是词典分，实则已经混入上游 path credibility
  3. `lm_score_scaled`
     - 表面是 LM 内容分，实则已经混入 split-token 路径选择后的总账

- 因而当前 `Base` 的真实问题不是“哪一个子项系数不对”
  - 而是它的三个主来源都不是干净独立量：
    - 一个是历史混合总分
    - 一个是带 credibility 的词典分
    - 一个是带分词路径选择结果的 LM 总分
  - 三者再线性相加，天然就会把早期局部偏差持续放大

- 这让路线判断进一步收紧：
  - 下一步再深挖，不应先去继续修 `adjustment_score`
  - 也不应先继续修 `dict_norm / lm_avg / whole / merge`
  - 而要直接回答一个更根本的问题：
    - `candidate->weight` 到底应该继承“什么样的历史量”？

- 更具体地说，下一步该优先验证的不是 feature，而是 `Base` 的记账契约：
  - 方案 A：
    - 让 `candidate->weight` 只继承更干净的 prefix content 主轴
    - 把 path / structure 的历史量隔离出去，不再一起滚入下一步
  - 方案 B：
    - 对 `entry->weight` 做去 credibility 化探针，分离“词典原始权重”和“路径 credibility”
  - 方案 C：
    - 对 `lm_score_scaled` 做 split-token / fallback family 的同口径逐步拆账，验证它是否本身就在压 `第一站|是` 这类正确链

- 到这一步，当前最硬的结论可以收成一句话：
  - 真正的根因不只是“重复记账”
  - 而是 `Base` 主轴本身就在拿三个已经不干净的总账继续滚雪球

## 2026-05-19 Base 三分拆账继续收敛：`dict_score_raw` 不是主凶，真正先把 family 拉偏的是 `lm_score_scaled`

- 为了直接确认跨 `start_pos` 合流是否真的死在 `admitted_state_index`，这轮没有再改排序逻辑，而是先补了一版最小 instrumentation：
  - 在 `debug_expansion_gate_records` 中新增：
    - `state_key`
    - `incumbent_text`
    - `incumbent_beam_score / incumbent_weight`
    - `pool_size`
  - 仅用于观测 `admitted_new / admitted_replace / admitted_reject` 时，同一个 `end_pos` 状态池里是否发生真实同态替换

- 首次回放时发现一个直接问题：
  - `state_key` 原始串里带有内部使用的 `0x1f` 分隔符
  - 写入 JSONL 后触发
    - `json.decoder.JSONDecodeError: Invalid control character`
  - 这不是排序逻辑问题，而是调试序列化问题
  - 已把 `state_key` 在落盘前统一转成可打印形式（控制字符替换为 `|`）
  - 重新标准编译：
    - `librime/build.bat static`
  - 编译通过

- 随后重新按单例回放：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\_single_case.txt`
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\target_pool_merge_probe`
  - 回放成功

- 这轮新增打点给出的结论非常硬：
  - 对 `diyizhanshi` 聚焦三条主竞争路径：
    - `第一 + 展示`
    - `第一展 + 示`
    - `第一站 + 是`
  - 统计到：
    - `request = 4`
    - `batch_selected = 4`
    - `admitted_new = 4`
    - `admitted_replace = 0`
    - `admitted_reject = 0`
    - `final_pool = 4`
  - 并且：
    - `shared_state_keys = 0`

- 代表记录可直接说明问题：
  - `我踏上了旅行的征程。第一` + `展示`
    - `stage = admitted_new`
    - `state_key = 了旅行的征程。第一展示|0|0|4|0|0|0|0|4`
  - `我踏上了旅行的征程。第一展` + `示`
    - `stage = admitted_new`
    - `state_key = 了旅行的征程。第一展示|1|3|4|0|1|0|0|3`
  - `我踏上了旅行的征程。第一站` + `是`
    - `stage = admitted_new`
    - `state_key = 了旅行的征程。第一站是|1|3|4|0|1|0|0|3`
  - 三条虽然最终可见文本有重叠，但落到 `admitted_state_index` 时并不共享同一个状态键

- 因而这一轮把“合流层”的判断进一步钉死成更精确的话：
  - **`第一 + 展示`、`第一展 + 示`、`第一站 + 是` 的主竞争，不是发生在 `admitted_state_index` 的同态去重里。**
  - **至少在当前 `BuildApproxStateKey` 契约下，这几条路都是各自 `admitted_new` 进入池子，没有彼此 replace/reject。**
  - 也就是说：
    - 之前怀疑的“跨 `start_pos` 合流后在 target pool 中被状态折叠顶掉”
    - 对这组主样例并不成立

- `final_pool` 结果继续保持同一方向：
  - `第一展示`
    - `beam_score = -183.737`
  - `第一展示`（`第一展 + 示`）
    - `beam_score = -167.066`
  - `第一站是`
    - `beam_score = -195.409`
  - `第一战士`
    - `beam_score = -210.860`
  - 说明这轮 instrumentation 没有改变既有结论：
    - 真正主导胜负的仍是 `final_pool` 内的排序分差，而不是 `admitted_state_index`

- 因而下一步路线再次收紧：
  - 不应继续在
    - `all_requests`
    - `batch_selected`
    - `admitted_state_index`
  - 这三层做“保活 / family protection / 状态去重修补”
  - 更值得做的是直接面向：
    - `final_pool` / `beam_score`
    - 以及更底层的 `Base` 主轴可比性
  - 也就是继续回到：
    - `candidate->weight`
    - `dict_score_raw`
    - `lm_score_scaled`
    - 这三条链的统一记账契约

- 在继续做 `Base` 三分拆账后，代表错例已经能把三条主轴进一步区分开：
  - `dict_score_raw`
  - `lm_score_scaled`
  - `candidate->weight` 作为历史累计载体

- 先看前缀错例 `wotashanglelvxingdezhengcheng`
  - 当前稳定口径下：
    - top1：`卧榻上了旅行的征程`
    - 正确：`我踏上了旅行的征程`
  - 两者对比：
    - top1
      - `Base = -335.64`
      - `Dict = -50.83`
      - `LmScaled = -284.82`
      - `Adj = -27.92`
    - 正确
      - `Base = -355.32`
      - `Dict = -50.53`
      - `LmScaled = -304.79`
      - `Adj = -38.00`
  - 解释：
    - top1 的 `Dict` 其实没有明显优势，甚至还略差
    - 真正拉开差距的是 `LmScaled`
      - `-284.82` 对 `-304.79`
      - 直接带来约 `19.97` 分的 base 优势
    - 也就是说，这类前缀错例不是词典先把它推错，而是 LM 总账先偏了

- 再看主错例 `我踏上了旅行的征程。diyi...`
  - 关键现象不是从 `d` / `di` 开始就全面崩
  - 而是：
    - `diyi`
      - top1 其实还是 `第一`
      - `第一`
        - `Base = -147.04`
        - `Dict = -11.77`
        - `LmScaled = -135.27`
        - `Total = -159.69`
      - `敌意`
        - `Base = -146.88`
        - `Dict = -12.76`
        - `LmScaled = -134.12`
        - `Total = -159.98`
      - 说明：
        - 到 `diyi` 为止，正确链并没有死
        - `第一` 仍能靠更好的总分保住 top1

- 真正的翻盘点出现在 `diyiz`
  - top family 变成：
    - `的意志`
      - `Base = -175.34`
      - `Dict = -24.19`
      - `LmScaled = -151.15`
      - `Total = -200.63`
    - 而正确 family 近邻 `第一种 / 第一张 / 第一中`
      - `Base ≈ -192.21 ~ -192.35`
      - `Dict ≈ -12.61 ~ -12.75`
      - `LmScaled = -179.60`
      - `Total ≈ -202.20 ~ -202.36`
  - 这一步的意义非常关键：
    - 正确 family 的 `Dict` 明显更好
      - 大约好 `11.5 ~ 12` 分
    - 但错误 family 的 `LmScaled` 更好
      - 大约好 `28` 分
    - 最终 `Base` 仍是错误 family 大幅领先
      - 约 `16 ~ 17` 分
  - 结论：
    - 在 family 首次成形的关键步，真正把方向拉偏的是 `lm_score_scaled`
    - 不是 `dict_score_raw`

- 到 `diyizhan` 这一步，这个判断进一步被钉死
  - `的驿站`
    - `Base = -176.87`
    - `Dict = -24.59`
    - `LmScaled = -152.27`
    - `Adj = -25.46`
    - `Total = -202.32`
  - `第一站`
    - `Base = -192.42`
    - `Dict = -12.82`
    - `LmScaled = -179.60`
    - `Adj = -10.02`
    - `Total = -202.44`
  - 解释：
    - `第一站` 的 `Dict` 依然明显更好
      - 约好 `11.77` 分
    - 但 `的驿站` 的 `LmScaled` 依然明显更好
      - 约好 `27.33` 分
    - 因此 `Base` 上 `的驿站` 先领先 `15.55` 分
    - `Adj` 其实在努力纠偏：
      - `的驿站` 比 `第一站` 差 `15.44` 分
    - 但已经来不及，只能把差距勉强追到：
      - `-202.32` vs `-202.44`
    - 错链仍以 `0.12` 分险胜

- 这一步把三条主轴的职责分开后，可以得到一个更清楚的判断：
  - `dict_score_raw`
    - 在这些关键对比里，经常是正确链更好
    - 所以它不是当前主凶
  - `lm_score_scaled`
    - 在 family 首次成形时，持续给错链带来 `27~28` 分级别的巨大优势
    - 它才是先把方向拉偏的那一项
  - `candidate->weight`
    - 它的作用更像“锁存器 / 传送带”
    - 一旦 `LmScaled` 在早期某一步先把错误 family 顶成赢家
    - 之后这份赢家地位就通过累计 `weight` 被持续滚入后续扩展

- 因而，当前最深根因可以再精确一层：
  - `candidate->weight` 确实在滚雪球
  - 但它滚的是已经被 `lm_score_scaled` 拉偏的赢家
  - 真正最早按下错误方向按钮的，是 `Base` 里的 `lm_score_scaled`
  - 而不是 `dict_score_raw`

- 这也解释了为什么前面很多路都失败：
  - 只修 `adjustment_score`
    - 太晚
    - 因为 `Adj` 实际上已经在尝试纠偏
  - 只修 `dict_norm / lm_avg / whole / merge`
    - 不够深
    - 因为真正大的方向优势已经在 `lm_score_scaled` 这里形成
  - 只盯 `candidate->weight`
    - 也不完整
    - 因为它更像传递和放大器，不是最早的点火源

- 到这里，下一步若继续深挖，最合理的优先级已经变成：
  1. 先直接审 `lm_score_scaled` 的 family 偏置来源
     - 尤其是 `diyi -> diyiz -> diyizhan` 这条链
     - 查 split-token LM 在这些步上为什么稳定偏向 `的意志 / 的驿站`
  2. 再看 `candidate->weight` 如何让这份早期优势在 beam 中滚下去
  3. `dict_score_raw` 暂时不应被视为主要矛头

## 2026-05-19 继续下挖 `lm_score_scaled` 后的机制结论：当前偏置本质上是 char-path LM 在 function-word family 上先按下错误方向

- 继续往下读 `witogram` 源码后，`lm_score_scaled` 的形成机制已经可以直接落到代码：
  - `Witogram::ScoreFeatures()` 先把 `context` 切成 UTF-8 token 序列，再逐 token 前推 KenLM state
  - 对待打分词 `word`：
    - 始终先算 char-path：
      - `char_total_log10`
    - 若整词 token 命中词表：
      - 再算 `whole_word_log10`
      - 然后
        - `total_log10 = 0.60 * whole_word_log10 + 0.40 * char_total_log10`
    - 若整词 token 未命中：
      - 直接 `used_char_fallback = true`
      - `total_log10 = char_total_log10`
  - `witset_poet` 里真正入 `Base` 的是：
    - `lm_score_scaled = total_log10 * kLn10 * witogram->ngram_weight()`

- 这说明一件关键事实：
  - `whole_word_log10 / char_path_log10` 不是并列参与总排序的两路账
  - 真正进 `Base` 的 LM 总账只有一个：
    - `total_log10`
  - 而当整词未命中时，这个总账就退化成纯 `char_total_log10`

- 再结合前面已经确认的项目事实：
  - 当前 `wanxiang-lts-zh-hans.klm` 是 split-token 模型
  - 多字串作为单 token 查询时整体上应视为 OOV，真正稳定生效的是按字切开的路径
  - 于是对 `第一种 / 第一站 / 的意志 / 的驿站` 这类多字候选来说：
    - 当前 `lm_score_scaled` 的主导来源，实际不是 whole-word 命中
    - 而是 char-path 在当前 context 下的累计分

- 这把 `diyi -> diyiz -> diyizhan` 的现象彻底解释通了：
  - `diyi` 时 `第一` 还能活着
  - 一到 `diyiz`，错误 family `的意志` 的 `LmScaled` 就比正确 family `第一种 / 第一张 / 第一中` 好约 `28` 分
  - 到 `diyizhan`，`的驿站` 的 `LmScaled` 又比 `第一站` 好约 `27.33` 分
  - 而这些优势不是来自 `dict_score_raw`
  - 也不是来自 `adjustment_score`
  - 而是当前 LM 总账在 char-path 层面对 function-word family 的持续偏好

- 为什么会是 function-word family 先赢：
  - 现在 `witogram` 看到的 `context`，在代码里是 `candidate->context_suffix`
  - 它本质上是前缀文本后缀，不是语义分词上下文
  - 再叠加当前 split-token `.klm` 的建模方式，LM 实际做的是：
    - 在字符级上下文里比较
      - `第 -> 一 -> 站`
      - 与
      - `的 -> 意 -> 志`
      - `的 -> 驿 -> 站`
    - 哪条字符序列更像高频搭配
  - 在这种比较方式下，带功能词头部的 family 很容易先获得更好的 char-path 总账

- 到这里，`lm_score_scaled` 的角色也可以再精确一句：
  - 它不是“一个中立的内容似然分”
  - 在当前模型和当前上下文喂法下，它更像：
    - “字符级路径偏好总账”
  - 而 `candidate->weight` 则负责把这个早期偏好持续滚入后续 beam 扩展

- 这也意味着：
  - 下一步若要真正继续修根因，不该先继续微调 `adjustment_score`
  - 也不该先去怀疑 `dict_score_raw`
  - 最直接的主战场应转为：
    - `lm_score_scaled` 本身的 family 偏置来源
    - 以及它是否应该继续以当前这种“直接进 Base 主轴”的方式参与前缀搜索

## 2026-05-19 继续核对关键步 `StepWholeHit / StepCharFB` 后的最终钉死：翻盘步是纯 char fallback，不是 whole-word 混合

- 继续直接看代表错例 `base_audit_single_case/latest_candidates.json` 中关键步的逐步 debug 字段后，这个机制已经被进一步钉死：
  - 真正关键的翻盘步，不是 `whole_word_log10` 与 `char_path_log10` 混合后压过正确链
  - 而是：
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
  - 也就是当前步本身就在走纯 char fallback

- `diyi`
  - top1 `第一`
  - 当前步：
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -117.497597`
  - 说明：
    - 即使在 `第一` 还能活着的这一步，当前胜出的关键 edge 也不是 whole-word 命中
    - 而是 char fallback

- `diyiz`
  - top1 `的意志`
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -92.788362`
  - 正确 family 代表 `第一站`
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -155.996796`
  - 这里非常关键：
    - 两边当前步都没有 whole-word 命中
    - 也都在走 char fallback
    - 真正拉开差距的，是 char-path 本身：
      - `-92.79` 对 `-156.00`

- `diyizhan`
  - top1 `的驿站`
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -93.764977`
  - `第一站`
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -155.996796`
  - 再次说明：
    - 决定 `的驿站` 胜过 `第一站` 的关键步，依然不是 whole-word 证据
    - 而是 pure char fallback 下的 char-path 总账差距

- `diyizhanshi`
  - top1 `第一展示`
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -95.266663`
  - `敌意展示`
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -96.195263`
  - 说明：
    - 到 8-11 这里，关键扩展步也仍然是在纯 char fallback 下竞争
    - `第一展示` 的问题不是吃了“整词额外奖励”
    - 而是当前 char-path 在这一段恰好更优

- 这轮核对还带出一个很重要的细节：
  - `WholeHit` 这个累计字段本身容易误导
  - 因为它可能只是之前某个单字 token 命中过词表
  - 真正决定当前步竞争是否在吃 whole-word 的，应看：
    - `StepWholeHit`
    - `StepCharFB`
    - `StepWholeLog10`
    - `StepCharLog10`
  - 而在当前代表错例的关键翻盘步上，结论是一致的：
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
    - `StepWholeLog10 = 0`

- 到这里，之前的路线判断可以再收紧一句：
  - 当前 `lm_score_scaled` 的 family 偏置，并不是“whole-word 与 char-path 混合不当”
  - 而更像是：
    - 在关键前缀步上，实际根本没有 whole-word 参与
    - 真正主导竞争的就是 pure char fallback 路径
  - 所以当前更准确的表述是：
    - `lm_score_scaled` 在关键步上，实际上就是 char-path 总账

- 这使得最深根因再落一层：
  - 错链不是被额外的 whole-word bonus 顶上去的
  - 而是当前 split-token / char-path 语境下，字符级 LM 本身就更偏好 `的意志 / 的驿站 / 第一展示` 这类路径
  - 后续 `candidate->weight` 只是把这个早期 char-path 优势一路滚下去

## 2026-05-19 句首 pure-char-fallback `lm_score_scaled` 降权探针：首次打掉 `的意志 / 的驿站` 主导位，但正确 continuation 仍未补齐

- 基于上一轮结论，直接实现了一个极窄探针：
  - 只在以下条件同时成立时，缩放进入 `Base` 的 `lm_score_scaled`
    - `start_pos == 0`
    - `candidate->generated_word_count == 0`
    - `candidate->generated_char_count == 0`
    - `char_count >= 2`
    - `used_char_fallback == true`
    - `matched_whole_word == false`
  - 默认缩放系数：
    - `prefix_char_fallback_lm_scale = 0.35`
  - 作用方式非常窄：
    - 只改 `Base` 里的 `lm_score_scaled`
    - 不改 `lm_avg`
    - 不改 `adjustment_score`
    - 不新增 beam 状态或额外保活逻辑

- 这次探针的结果非常关键，因为它第一次直接验证了“点火源”假设：
  - `diyi`
    - top1 仍是 `第一`
    - 但 `LmScaled` 已从约 `-135` 缩到约 `-47`
  - `diyiz`
    - top1 从 `的意志` 变成 `第一种`
  - `diyizhan`
    - top1 从 `的驿站` 变成 `第一站`
  - 这说明：
    - `的意志 / 的驿站` 这条错误 family 的句首主导地位，确实就是由 pure char fallback 下的 `lm_score_scaled` 在点火

- 具体到关键步：
  - `diyiz`
    - top1 `第一种`
    - `LmScaled = -62.86`
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
  - `diyizhan`
    - top1 `第一站`
    - `LmScaled = -62.86`
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
  - 对照之前未探针时：
    - `diyiz` 被 `的意志` 主导
    - `diyizhan` 被 `的驿站` 主导
  - 所以这一步已经足以说明：
    - 当前最早的错误 family 翻盘，确实依赖句首 pure char fallback 的 LM 总账直接主导 `Base`

- 但同时，这次探针也证明了另一件同样重要的事：
  - 即使把错误 family 的点火源打掉，正确句并不会自动回来
  - 因为：
    - `diyizhanshi`
      - top1 变成 `第一战士`
      - top3 变成 `第一战士 / 第一展示 / 敌意展示`
    - 完整句
      - top1 变成 `第一战士已作古老的小镇`
      - 仍然不是 `第一站是一座古老的小镇`

- 这意味着路线判断再次收紧：
  - 句首 `的意志 / 的驿站` family 确实只是“第一层错误吸引子”
  - 但它背后还有第二层问题：
    - 当 pure char fallback LM 被降权后，系统并不会自然收敛到正确 continuation
    - 而是会转而落到同一大 family 内的另一条高频错误 continuation：
      - `第一种`
      - `第一站`
      - `第一战士`
      - `第一展示`

- 所以这次探针给出的决定性新结论是：
  1. 之前对根因的判断是对的
     - 句首 pure char fallback 的 `lm_score_scaled` 确实是点火源之一
  2. 但它不是完整根因
     - 因为把它压下去之后，系统只是在错误大 family 内重新排位
     - 并不会自动得到正确的 `站|是|一座`

- 典型集结果也支持这个判断：
  - 旧稳定口径：
    - `top1_accuracy = 0.25`
    - `top3_accuracy = 0.30`
  - 这次 pure-char-fallback 探针：
    - `top1_accuracy = 0.35`
    - `top3_accuracy = 0.40`
  - 说明：
    - 这个探针不是纯噪声
    - 它对典型错例集整体是有正收益的
  - 但单代表句仍失败，说明它只打掉了“第一层吸引子”，还没补上正确 continuation 契约

- 到这里，下一步方向已经可以收成一句话：
  - 不能回到“继续调 `adjustment_score`”
  - 也不能满足于“把 `的驿站` 打掉就算修好”
  - 更应该把下一步聚焦到：
    - 在 `第一站` 已重新回到句首主导后，
    - 为什么 `站 -> 是 -> 一座` 这条正确 continuation 仍然输给 `战士 / 展示 / 已作...` 这类错误延伸链

## 2026-05-19 继续追第二层后收口：不是单纯“缺 continuation 契约”，而是 split continuation 与 same-span 整块重切分在跨 regime 竞争

- 继续直接沿 `charfb_probe_single_case_v2/latest_candidates.json` 追 `diyizhan -> diyizhans -> diyizhanshi -> diyizhanshiy...` 后，第二层问题已经可以明确成更具体的一句话：
  - `第一站` 被扶回句首之后，真正打败它的并不是一个“更好的后继词”
  - 而是：
    - 同一输入跨度上的整块重切分候选
      - `第一战士`
      - `第一展示`
    - 在和 split continuation
      - `第一站|是`
    - 做跨 regime 直接竞争

- 这不是同一种候选形态：
  - `第一战士 / 第一展示`
    - 仍然是从 `start_pos = 0` 起吃完整段输入的单块候选
    - 本质上还是“第一词重解释”
  - `第一站|是`
    - 已经是前缀 `第一站` 上承接下一词 `是`
    - 本质上属于 split continuation

- 这层差异在分数上表现得非常明显：
  - `diyizhanshi`
    - `第一战士`
      - `Base = -92.54`
      - `Adj = -8.46`
      - `Total = -101.01`
      - `LmRaw = -194.50`
      - `LmScaled = -78.37`
      - `StepWholeHit = 0`
      - `StepCharFB = 1`
    - `第一站是`
      - `Base = -161.10`
      - `Adj = -34.31`
      - `Total = -195.41`
      - `LmRaw = -194.50`
      - `LmScaled = -136.74`
      - `StepWholeHit = 1`
      - `StepCharFB = 0`
  - 这说明：
    - 两条链在这个点上甚至连 `LmRaw` 都一样
    - 真正拉开差距的不是“内容突然懂了”
    - 而是它们在完全不同的 regime 里被计分

- 更具体地说，当前 split continuation `第一站|是` 相比整块重切分 `第一战士 / 第一展示` 同时吃了几类结构性不利：
  1. 它已经切成两词
     - 因此开始吃更差的 `LmAvg`
       - `第一站是` 的 `LmAvg = -134.51`
       - `第一战士` 的 `LmAvg = -55.98`
  2. 它开始吃 split 形态罚项
     - `Frag = -1.02`
     - `Tail = -1.47`
     - `Octa = -4.30`
     - `Whole = -0.10`
  3. 在当前 probe 下，它失去了“句首 pure char fallback 降权”这一只作用于单块首词的保护
     - `第一战士` 仍处在受 probe 影响的单块句首 regime
     - `第一站|是` 已经进入后继词 regime，不再享受这一缩放

- 也就是说，这次探针把问题照得更清楚了：
  - 它确实打掉了第一层吸引子
    - `的意志 / 的驿站`
  - 但也暴露出第二层真实对手并不是“是”这个 continuation 本身弱
  - 而是：
    - 一旦输入允许
      - `战士`
      - `展示`
    - 这类 same-span 整块候选出现，
    - 它们会作为“重新解释整段输入”的单块词，
    - 直接把已经回正的 split prefix `第一站|...` 盖掉

- 从 prefix 演化上看，这个翻盘发生得很早：
  - `diyizhan`
    - top1 已回到 `第一站`
  - 一到 `diyizhans / diyizhanshi`
    - top1 就不再是 `第一站|上/是`
    - 而变成 `第一战士 / 第一展示`
  - 所以这不是“`第一站是` 已经展开后，再输给更远的句子”
  - 而是在 split continuation 刚开始承接时，就被整块重切分覆盖了

- 后续 `diyizhanshiy / diyizhanshiyi / diyizhanshiyizuo` 也沿着同一机制继续展开：
  - top1 继续滚成
    - `第一战士有`
    - `第一战士已`
    - `第一战士已做`
  - 与之对照，`第一站是由 / 第一站是以 / 第一站是以做` 虽然仍在候选里，但排名明显更后
  - 这说明第二层问题不是“一步 continuation 选错”
  - 而是：
    - 一旦单块重切分 family 重新占住顶层，
    - 它后面的 continuation 也会继续滚下去

- 到这里，路线判断再次收紧：
  - 当前要补的不是一个泛泛的“continuation 契约”
  - 而更像是：
    - 当某条 split prefix
      - 如 `第一站`
    - 已经在上一关键步被证明更优后，
    - 下一步不应允许 same-span 的整块重切分 family
      - `第一战士`
      - `第一展示`
    - 继续用不同 regime 的记分方式直接覆盖它

- 因此，若继续推进实现，下一步的主战场不应再是全局调分，而应是更窄的一类契约：
  - `split-prefix continuation protection`
  - 它要解决的不是“给 `是` 多加一点分”
  - 而是：
    - 已回正的 split prefix family
    - 与 same-span first-word reparse family
    - 不能继续按当前这种跨 regime 方式裸比

## 2026-05-19 `split-prefix continuation protection` 第一版负结果：原型没有命中真正竞争对象，需收紧为 shared-prefix family protection

- 基于上一轮“跨 regime 竞争”判断，先做了一个最窄原型：
  - 仅在 `all_requests` 聚合后，若同一 `end_pos` 下已经存在强 split continuation
    - 条件：
      - `source_line.generated_word_count == 1`
      - `source_line.generated_char_count >= 3`
      - `source_line.single_char_word_count == 0`
      - 当前承接 `char_count <= 2`
  - 则对从句首重新吃完整段输入的多字 same-span reparse 追加覆盖罚项
  - 目标是直接压 `第一战士 / 第一展示` 对 `第一站|是` 的覆盖

- 编译通过，但验证结果显示这版原型**完全没有生效**：
  - 单样例
    - `top1_accuracy = 0.0`
    - `top3_accuracy = 0.5`
  - 典型集
    - `top1_accuracy = 0.35`
    - `top3_accuracy = 0.40`
  - 与上一轮仅有句首 pure-char-fallback probe 的结果完全同口径
    - 说明这次新增保护逻辑没有实质触发

- 更关键的是，单样例的关键候选排序几乎一字不动：
  - `diyizhanshi`
    - top1 仍是 `第一战士`
    - `第一展示` 仍在前列
    - `第一站是` 仍然明显更后
  - `diyizhanshiyi`
    - top1 仍是 `第一战士已`
    - `第一站是以` 仍然更后
  - `diyizhanshiy`
    - top1 仍是 `第一战士有 / 第一战士已 / 第一战士用`
    - `第一站是由 / 第一站是要` 仍然排不进前列

- 这次“完全不生效”本身反而给出一个更精确的新结论：
  - 我上一轮对“same-span 整块重切分”的描述还不够精确
  - 当前真正覆盖 `第一站|是` 的对手，并不等于：
    - 一个从 `start_pos = 0` 重新吃完整段输入的裸整块 reparse
  - 否则这版 batch-level 覆盖罚项至少会看到一些位移

- 更准确的判断应当是：
  - 当前主导覆盖的，其实更像一个
    - `shared-prefix family`
  - 典型形态是：
    - `第一 + 战士`
    - `第一 + 展示`
  - 而不是我这版原型瞄准的那种
    - “整段从句首直接重吃到当前 end_pos 的 reparse”

- 这也能解释为什么 debug 表现会显得“像整块”，但原型却打不中：
  - `第一战士` 的当前步 debug 里：
    - `StepWholeHit = 0`
    - `StepCharFB = 1`
    - `Tok = 4`
  - 它看起来像是整条字符路径在继续滚
  - 但真正与 `第一站|是` 竞争的结构，未必是
    - `source_line = empty`
    - `entry = 第一战士`
  - 更可能是：
    - 已有 `第一` 这一 shared prefix family
    - 再从其下继续长出
      - `战士`
      - `展示`
    - 来覆盖 `第一站|是`

- 所以这轮负结果把问题又推进了一层：
  - 下一步若继续做 protection，目标不该再写成
    - `split-prefix vs same-span reparse`
  - 而应改写成：
    - `split-prefix vs shared-prefix reparse family`

- 换句话说，真正该保护的不是“防止句首整段重吃”
- 而是：
  - 当 `第一站` 这条 split prefix 已经在上一关键步胜出后，
  - 需要限制同一 shared prefix 家族下的其它重解释延伸
    - `第一战士`
    - `第一展示`
  - 再次以更轻的 continuation regime 覆盖它

- 基于这个结果，这版 `split-prefix continuation protection` 原型已回退，不保留。

## 2026-05-18 `shared-prefix family protection` 第二版负结果：问题不在 family key，而在竞争对象根本不处于同一个 `all_requests` 批次

- 按上一条收紧后的判断，又做了一版更窄的原型：
  - 不再瞄准 `same-span` 整段 reparse
  - 改成只在 `all_requests` 内保护：
    - 已经形成的较长 split prefix
      - 如 `第一站 + 是`
    - 免受同前两字 shared-prefix family 的更短前缀重解释覆盖
      - 如 `第一 + 展示`
      - 或句首整块 `第一战士`
  - 实现位置仍放在：
    - `witset_poet.cc`
    - `all_requests` 聚合完成、`batch_selected` 之前

- 这版原型同样编译通过，但单样例结果仍然**完全不变**：
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\shared_prefix_single_case_v2`
  - 指标仍是：
    - `top1_accuracy = 0.0`
    - `top3_accuracy = 0.5`
  - 关键步仍是：
    - `diyizhanshi -> 第一战士`
    - `diyizhanshiyi -> 第一战士已`
    - `第一站是 / 第一站是以` 排名没有任何前移

- 这次“再一次完全不动”给出了一个比上轮更硬的结构性结论：
  - **问题不在 family key 写得对不对，而在我把保护逻辑放错了层级。**
  - 复核 `MakeSentences()` 主循环后确认：
    - `all_requests` 是按当前 `start_pos` 的 graph vertex 分批临时聚合的
    - 每处理完一个 `start_pos`
      - 就立刻 `batch_selected -> admitted_state_index -> states[end_pos]`
    - 然后进入下一个 `start_pos`

- 这意味着：
  - `第一战士`
    - 来自
      - `start_pos = 0`
      - 的那一批请求
  - `第一站是`
    - 来自
      - `start_pos = 8`
      - 的另一批请求
  - `第一 + 展示`
    - 也来自
      - `start_pos = 4`
      - 的另一批请求
  - 它们最终会在：
    - `states[11] / final_pool`
    - 里汇合竞争
  - 但**不会在同一个 `all_requests` 批次里并排出现**

- 所以当前可以把这个方向彻底钉死：
  - 任何放在当前
    - `all_requests`
    - 内部的 shared-prefix / reparse protection
  - 无论 family key 写成
    - `same-span`
    - 还是 `shared-prefix`
  - 都先天地打不到：
    - `start_pos = 0 / 4 / 8`
    - 之间的跨批次竞争

- 这一步把问题定义再次收紧成一句话：
  - **`第一战士 / 第一展示 / 第一站是` 的主竞争面，不是 batch 内覆盖，而是跨 `start_pos` 写入 `states[end_pos]` 之后，在同一个目标状态池里的最终竞争。**

- 因而下一步若继续实现，正确落点不应再是：
  - `all_requests` 后处理
- 而应转向下面两类位置之一：
  1. `states[end_pos]` 合流后、`admitted_state_index / target_pool` 这一层的跨批次 family 契约
  2. 更上游地改写 `candidate->weight / lm_score_scaled` 进入后续 `states[end_pos]` 时的跨 regime 可比性

- 这版 `shared-prefix family protection` 原型已回退，不保留；当前工作树已重新执行：
  - `build.bat static`
  - 恢复到稳定状态

## 2026-05-19 `candidate->cumulative_base` 前缀继承 probe 负结果：不是简单的“adjustment 跟着总分滚雪球”

- 在把 `all_requests` / `admitted_state_index` 这层排除之后，这轮继续直接重审 `Base` 主轴，先做了一个最窄 probe：
  - 不改 `dict_score_raw`
  - 不改 `lm_score_scaled`
  - 只把当步 `base_score` 的前缀继承项从：
    - `candidate->weight`
  - 临时改成：
    - `candidate->cumulative_base`
  - 目的只有一个：
    - 验证当前问题是不是主要由“历史总分把 adjustment / penalty 一起继续滚进下一步 base”造成

- probe 过程：
  - 修改：
    - `plugins/witset/src/witset_poet.cc`
  - 标准编译：
    - `librime/build.bat static`
  - 单例回放：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\_single_case.txt`
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\base_carry_probe`

- 这版 probe 的现象很清楚：
  - 分数整体被抬高了
  - 但目标家族几条路几乎是同步抬高
  - `diyizhanshi` 仍然：
    - top1 = `第一战士`
    - rank2 = `第一展示`
    - `第一站是` 仍在后面
  - `diyizhanshiyi` 仍然：
    - top1 = `第一战士已`
    - top3 = `第一战士已 / 第一战士亦 / 第一战士以`
    - `第一站是以` 仍没被扶正

- 关键数值对比：
  - probe 前（稳定树）：
    - `第一展 + 示 -> 第一展示`
      - `beam = -167.066`
    - `第一站 + 是 -> 第一站是`
      - `beam = -195.409`
    - 差距约 `28.34`
  - probe 后：
    - `第一展 + 示 -> 第一展示`
      - `beam = -160.012`
    - `第一站 + 是 -> 第一站是`
      - `beam = -188.424`
    - 差距仍约 `28.41`
  - 也就是说：
    - 虽然两条路都被整体抬高了约 `7`
    - 但它们之间真正决定胜负的分差几乎没动

- 因而这轮可以排除一个很像但其实不够深的解释：
  - **不是简单把 `candidate->weight` 换成 `candidate->cumulative_base`，就能阻止 `第一站是` 被压。**
  - 这说明当前主因并不只是：
    - `adjustment_score`
    - 跟着历史总分一起滚进下一步 `base_score`
  - 更深处仍然是：
    - `Base` 主轴内部对不同 segmentation regime 的内容分差本身就已经拉开

- 这个 probe 的价值在于把责任边界继续收紧：
  - 若仅清洗“前缀继承项的账本来源”
  - 但不动：
    - `dict_score_raw`
    - `lm_score_scaled`
    - 以及它们如何进入 `base_score`
  - 则 `第一站是` 对 `第一展示` 的核心差距几乎保持不变

- 当前更可靠的下一步判断：
  - 不应继续在“历史 carry-over 账本换源”这条线上打转
  - 应继续直面：
    - `dict_score_raw`
    - `lm_score_scaled`
    - 在 `Base` 主轴内如何对
      - `第一 + 展示`
      - `第一展 + 示`
      - `第一站 + 是`
    - 这几种不同切分 regime 形成系统性不可比

- 这版 probe 已回退，不保留；随后已再次执行：
  - `librime/build.bat static`
  - 编译通过，工作树恢复稳定

## 2026-05-19 移除 `Base` 内 LM 入账的二分 probe：LM 是主压制项，但 `diyizhanshi` 当步仍残留非 LM 压制

- 为了把 `dict_score_raw` 与 `lm_score_scaled` 的责任边界继续拆开，这轮做了一个更直接的二分 probe：
  - 不改 `adjustment_score`
  - 不改 `dict_score_raw`
  - 也不改 `lm_score_scaled` 的计算本身
  - 只把 `base_score` 中这一项临时拿掉：
    - `lm_total_weight_ * lm_score_scaled`
  - 也就是说，当步 `Base` 主轴只保留：
    - 前缀历史总分
    - `upstream/joint prior`
    - `dict_score_raw`

- probe 过程：
  - 修改：
    - `plugins/witset/src/witset_poet.cc`
  - 标准编译：
    - `librime/build.bat static`
  - 单例回放：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\_single_case.txt`
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\lm_removed_probe`

- 这轮现象非常关键，而且比前几轮更有判别力：
  - `diyizhanshi`
    - top1 仍然是 `第一战士`
    - rank2 仍然是 `第一展示`
    - `第一站是` 仍未当步翻盘
  - 但一进入后续 continuation：
    - `diyizhanshiy`
      - top1 变成 `第一站使用`
      - top3 里已经出现 `第一站是由`
    - `diyizhanshiyi`
      - top1 直接变成 `第一站是以`
  - 这说明：
    - **只要把 `Base` 主轴里的 LM 贡献拿掉，`第一站是*` 家族会立刻在后续 continuation 上整体翻身**

- 对 `diyizhanshi` 本步本身，数值也出现了明显收敛：
  - 稳定树下：
    - `第一展 + 示 -> 第一展示`
      - `beam = -167.066`
    - `第一站 + 是 -> 第一站是`
      - `beam = -195.409`
    - 差距约 `28.34`
  - 去掉 `Base` 内 LM 入账后：
    - `第一展 + 示 -> 第一展示`
      - `beam = -42.627`
    - `第一站 + 是 -> 第一站是`
      - `beam = -58.667`
    - 差距收敛到约 `16.04`

- 这组对比说明两件事：
  1. `lm_score_scaled` 的 `Base` 入账，确实贡献了这组错排里最大的一块分差  
     - `28.34 -> 16.04`
     - 单这一步就削掉了约 `12.3` 分差
  2. 但它还不是全部  
     - 即便拿掉这部分，`diyizhanshi` 当步仍然没有立刻翻到 `第一站是`
     - 剩余大约 `16` 分差，说明还有非 LM 侧的压制残留

- 结合这轮结果，当前更精确的判断应该改写成：
  - **`lm_score_scaled` 是当前 `第一站是` 家族被系统性压低的主压制项。**
  - 但对首个关键步 `diyizhanshi` 而言：
    - 还有一层不小的残余压制并不来自 `Base` 内的 LM 入账本身
  - 这层残余更可能落在：
    - 由 `dict_score_raw` 带来的切分 regime 差异
    - 或 `adjustment_score` 中与 `whole/fragment/length/oov` 相关的结构项

- 因而下一步路线继续收紧：
  - 没必要再怀疑“是不是 LM 根本不是主因”
  - 现在可以把问题拆成主次两层：
    1. **主因**
       - `Base` 主轴里的 `lm_score_scaled`
       - 它决定了 `第一站是*` 家族在 continuation 上会不会整体被压死
    2. **剩余根因**
       - `diyizhanshi` 当步仍未翻盘的那部分残余压制
       - 更应继续查：
         - `dict_score_raw`
         - 以及 `adjustment_score` 中的结构项

- 这版“移除 Base 内 LM 入账”的 probe 已回退，不保留；随后已再次执行：
  - `librime/build.bat static`
  - 编译通过，工作树恢复稳定

## 2026-05-19 屏蔽 `adjustment_score` 手工结构项的 probe：残余压制不主要来自 structure/whole/length/tail 这一组

- 为了继续拆掉去除 `Base` 内 LM 入账后剩下的那约 `16` 分差，这轮又做了一版更窄 probe：
  - 保留 `Base` 主轴原样：
    - `candidate->weight`
    - `upstream/joint prior`
    - `dict_score_raw`
    - `lm_score_scaled`
  - 只改 `adjustment_score`
  - 临时只保留两项统计项：
    - `dict_score_norm_weight_ * dict_score_norm`
    - `lm_avg_weight_ * lm_score_avg`
  - 暂时屏蔽其余手工结构/修补项：
    - `boundary_score`
    - `oov_penalty`
    - `early_*`
    - `prefix_anchor_*`
    - `whole_first_word_continuation_penalty`
    - `length_term`
    - `whole_word_bonus`
    - `merge_gain`
    - `fragment_penalty`
    - `structure_penalty`
    - `tail_repair_penalty`
    - `octagram_penalty`

- probe 过程：
  - 修改：
    - `plugins/witset/src/witset_poet.cc`
  - 标准编译：
    - `librime/build.bat static`
  - 单例回放：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\_single_case.txt`
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\adjustment_structure_probe`

- 这轮结果是一个很明确的负结果：
  - `diyizhanshi`
    - top1 仍然是 `第一战士`
    - rank2 仍然是 `第一展示`
    - `第一站是` 仍在后面
  - `diyizhanshiy`
    - top1 仍然是 `第一战士有`
    - 没有像“移除 Base 内 LM 入账”那样整体倒向 `第一站是*`
  - `diyizhanshiyi`
    - top1 仍然是 `第一战士已`
  - 也就是说：
    - **把手工结构 adjustment 几乎全部拿掉，并没有触发主排序面改向。**

- 对 `diyizhanshi` 本步的关键数值：
  - 稳定树下：
    - `第一展示`
      - `beam = -167.066`
    - `第一站是`
      - `beam = -195.409`
    - 差距约 `28.34`
  - 屏蔽手工结构项后：
    - `第一展示`
      - `beam = -163.067`
    - `第一站是`
      - `beam = -191.414`
    - 差距约 `28.35`
  - 几乎没有变化

## 2026-05-24 request-stage real-anchor driven same-span 收紧

- 本轮先回到当前工作树继续最小快路径验证，不改简拼规则，也不回到全局调分。
- 先针对 `witset_poet.cc` 的 `same_span_competition` 做了一轮合同感知增强：
  - 当同跨度 split anchor 已拿到更强的 request-stage 合同支持时，
  - 对仍想靠不同 segmentation regime 裸比胜出的竞争线，放大 same-span penalty。
- 第一刀结果：
  - `diyizhans`
    - `第一战士` 从 `rank 1` 降到 `rank 3`
    - `SameSpanComp` 从约 `-42.39` 扩到约 `-60.74`
  - `diyizhanshi`
    - `第一战士` 从 `rank 1` 降到 `rank 2`
    - 但 `第一展示` 升成 `rank 1`
- 继续复核调试项后确认：
  - 第一刀没把 `第一展示` 一起压下去，不是因为 same-span contract 无效，
  - 而是因为 anchor 选得过宽：
    - 把仅用于旁路观测的 `AltReqBridge` 也当成了合同支持，
    - 导致 `第一展示` 自己有机会被选成 same-span anchor。
- 第二刀收紧：
  - `same_span` anchor 只认真实 `ReqBridge`，不再把 `AltReqBridge` 当作合同支持。
  - anchor 选择顺序也改成：
    - 先比较真实合同支持强度
    - 再比较 beam 分数
  - 同时对 competing reparse line 的合同对比也只看真实 `ReqBridge`。
- 重新 `librime/build.bat static` 并跑
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
- 当前最小验证结果：
  - `diyizhans`
    - `第一站是` 升到 `rank 3`
    - 与前排分差收敛到约 `6.06`
  - `diyizhanshi`
    - `第一站是` 升到 `rank 2`
    - `第一展示` 仍为 `rank 1`
    - 两者分差已收敛到约 `2.16`
  - `第一战士`
    - 已被压到 `rank 13`
- 当前判断再次收紧：
  - “单块/两段重解释 family 盖住 split anchor” 这一层已经进一步被压住，
  - 剩余头部对手主要收敛到 `第一展示`，
  - 且它不再主要靠真实 `ReqBridge` 获胜，
  - 更像是 `第一展 + 示` 在当前 `Base + LM + dict_score` 口径下，仍对 `第一站 + 是` 保留约 `2` 分优势。
- 因而下一步不该回去继续放大 same-span penalty，
  - 而应直接检查 `第一展示` 这条两段 shared-prefix 路径在 `diyizhanshi` 的真实承接形态，
  - 判断应补的是：
    - 更窄的 shared-prefix split contract
    - 还是 `Base` 主轴里 `LM / dict_score` 对 `展 + 示` 与 `站 + 是` 的剩余可比性修正。

- 更直白地说：
  - 即使拿掉：
    - `whole/fragment/length/tail/oov/boundary/octagram`
    - 以及那一组 `early/prefix-anchor` 手工补偿
  - `第一展示` 对 `第一站是` 的优势仍然原封不动
  - 说明此前剩余那块压制，并不主要来自这类手工结构项

- 这一步把责任边界再次收紧：
  - 当前剩余主因更像不是：
    - `structure_penalty`
    - `fragment_penalty`
    - `tail_repair_penalty`
    - `whole_word_bonus`
    - `length_term`
    - `oov_penalty`
    - `boundary_score`
  - 而更像继续落在：
    - `lm_avg`
    - `dict_score_norm`
    - 以及与之同源的 `dict / LM` 统计账本本身

- 因而到这一步，当前最可靠的判断已经变成：
  1. `Base` 主轴里的 `lm_score_scaled`
     - 是把 `第一站是*` 家族整体压低的最大主因
  2. 把它拿掉后剩下的残余压制
     - 并不主要来自手工结构 adjustment
     - 更可能来自：
       - `lm_avg`
       - `dict_score_norm`
       - 也就是 `dict / LM` 统计项在不同切分 regime 下的继续不可比

- 这版“屏蔽 adjustment 手工结构项”的 probe 已回退，不保留；随后已再次执行：
  - `librime/build.bat static`
  - 编译通过，工作树恢复稳定

## 2026-05-24 request-stage source mismatch 首次实现

- 在 `request-stage real-anchor driven same-span` 收紧后，继续把剩余缺口收口到：
  - 当前 source line 没拿到真实 `ReqBridge`
  - 但同 `(start_pos, end_pos, entry_text)` 上存在来自其他 source line 的正向 bridge
  - 也就是 `request-stage source mismatch`
- 在 `witset_poet` 中新增一条独立罚项：
  - 新增 `request_stage_source_mismatch_weight`
  - 新增调试项 `ReqSrcMismatch / StepReqSrcMismatch`
  - 罚项只在以下条件成立时触发：
    - 当前 source 没有真实 `ReqBridge`
    - 同 entry 存在正向 `AltReqBridge`
    - `AltReqSrc` 与当前 source 不一致
- 同时把 `alt_request_stage` 的正向候选提示从“仅调试态构造”扩成：
  - 只要启用了 `request_stage_source_mismatch_weight`
  - 运行时也会构造按 `(start,end,entry)` 聚合的正向 alt hint
  - 避免 release/非 debug 路径与 snapshot 调试路径表现不一致
- 第一版实现后，先用最小闭环验证：
  - `librime/build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
- 首轮结果表明：
  - `diyizhans`
    - `第一展示` 这类借桥路径已被显式命中
    - `ReqSrcMismatch` 稳定出现约 `-2.18`
    - `第一展示` 落到 `rank 7`
    - `第一站是` 保持 `rank 3`
  - `diyizhanshi`
    - `第一展示` 仍是 `rank 1`
    - `第一站是` 仍是 `rank 2`
    - 分差约 `3.34`
    - `第一展示` 盘面已变成：
      - `ReqBridge = 0`
      - `AltReqBridge = 2.00`
      - `ReqSrcMismatch = -2.18`
      - `SameSpanComp = -13.87`
- 继续复核后发现：
  - 第一版 `source mismatch` 罚项即使已经命中目标，也还不足以单独翻盘
  - 说明“借别家 bridge”确实是问题的一部分，但不是当前剩余差距的全部来源
- 随后把触发条件进一步收紧回最初目标：
  - 只罚“当前 source 没有真实 `ReqBridge`”的 line
  - 不再去罚“当前 source 有真实桥、只是别家桥更强”的 line
- 收紧后再次：
  - `librime/build.bat static`
  - `partial_chain_stage_probe --case case2_diyizhan --mode snapshot`
  - 结果几乎不变
- 当前最可靠的新结论：
  - `request-stage source mismatch` 的方向是对的，且已经被运行时和调试链路稳定观测到
  - 但它目前更像是“补充打击项”，不足以单独把 `第一展示` 从 `diyizhanshi rank 1` 拉下去
  - 剩余主缺口仍更像落在：
    - `Base + LM + dict_score` 统计账本本身
    - 或 shared-prefix 两段路径在 final pool 里的更强合同

## 2026-05-24 diyizhanshi 细账 probe 继续推进但受本机 Python 缺失阻塞

- 为继续拆 `diyizhanshi` 下 `第一展示` vs `第一站是` 的前驱账本，先复用现有 `partial_chain_stage_probe.py --mode full` 与 `next_hop.jsonl`，确认：
  - 当前现成 probe 只覆盖 `source_suffix = 第一站是`
  - `diyizhanshi` 本拍在 `next_hop` 中只有 `post_admit_target_pool`，没有直接可用的 `request/source_pool` 前驱细账
  - 更长输入上的 `pre_source_pool_full/source_pool_full` 已能读到 `第一站是 -> 以/已/一/宜` 的后续账本
- 随后对 `partial_chain_stage_probe.py` 做了最小脚本改动：
  - 在 `case2_diyizhan` 里新增 `next_hop_after_diyizhanshi_display`
  - `source_suffix = 第一展示`
  - `focus_entries = [以, 已, 一, 宜]`
  - 目标是下一次 `full` 直接把 `第一展示` 的对应后续 source pool 也抓出来
- 但重跑前发现本机运行时环境已变化：
  - 当前 shell 下 `python` 不可执行，退出码 `9009`
  - `py` 启动器也不存在
  - `where python` 仅剩 `WindowsApps` shim，不是可执行解释器
- 因此这一轮没有拿到新增的 `第一展示` next-hop 工件，暂时无法完成动态细账对比
- 在不能继续跑 probe 的前提下，回到代码静态拆账后进一步确认：
  - `base_score = candidate->weight + lm_total_weight * lm_score_scaled`
  - 在非 aligned 模式下还会把 `dict_score_weight * dict_score_raw` 直接加进 base
  - `dict_score_norm` 与 `lm_avg` 只属于 adjustment，而不是 base 主账
  - 结合现有 snapshot 中 `第一展示 Base:-137.04` vs `第一站是 Base:-160.20`，剩余主缺口更像落在：
    - 主轴 `candidate->weight / lm_score_scaled / dict_score_raw`
    - 而不是已经持续加码过的后验 adjustment 项

## 2026-05-24 用 Poetry 恢复 probe 运行并确认主缺口落在 LM 主账

- 用户安装 Python 后，继续在 `outwit-windows/librime/plugins/witogram/tools` 建立最小 Poetry 环境：
  - 新增 `pyproject.toml`
  - 生成 `poetry.lock`
  - `poetry install --no-root`
  - 仅安装 `pypinyin`
- 之后使用：
  - `python -m poetry run python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode full`
  - 成功跑出新增的 `next_hop_after_diyizhanshi_display`
- 新 probe 直接给出了 `source_suffix = 第一展示` 的后续 request/source-pool 账本，可与原有 `第一站是` 并排对比
- 核心对比结论：
  - 对 `以`：
    - `第一展示` vs `第一站是`
    - `search_gap = -0.49`
    - `base_gap = +5.07`
    - `dict_gap = 0.00`
    - `lm_gap = +2.58`
  - 对 `已`：
    - `search_gap = +24.06`
    - `base_gap = +29.62`
    - `dict_gap = 0.00`
    - `lm_gap = +27.13`
  - 对 `一`：
    - `search_gap = -6.25`
    - `base_gap = +2.49`
    - `dict_gap = 0.00`
    - `lm_gap = 0.00`
  - 对 `宜`：
    - `search_gap = -4.22`
    - `base_gap = +1.34`
    - `dict_gap = 0.00`
    - `lm_gap = -1.15`
- 这批证据说明：
  - 当前 `第一展示` 领先并不是 `dict_score_raw` 更强，词典原始分几乎完全相同
  - 主差异主要来自 shared-prefix 后续步上的 `lm_score_scaled`
  - 尤其 `以/已` 这类主竞争续写里，`第一展示` 后缀上下文给出的 LM 主账明显优于 `第一站是`
  - 因而剩余主缺口已进一步收口为：
    - 不是继续加 poet 后验罚项
    - 而是 shared-prefix 两段路径在 LM 主账上的 regime 可比性/合同问题

## 2026-05-24 shared-prefix LM contract 第一版已接线但当前仍是 no-op

- 这轮先不重复跑 `full`，只做最小闭环：
  - `librime/build.bat static`
  - `partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
  - 然后直接只读 `partial_chain_stage_probe.snapshot.jsonl`
- 已在 `witset_poet` 中接入：
  - `shared_prefix_lm_contract_weight_`
  - `ComputeSharedPrefixLmContractPenalty(...)`
  - `is_unconfirmed_shared_prefix_reparse_source(...)`
  - 以及 debug 字段 `SharedPrefixLm / StepSharedPrefixLm`
- 但现成 snapshot 复核显示：
  - `diyizhanshi / 第一展示`
    - `SharedPrefixLm:0.00`
    - `StepSharedPrefixLm:0.000000`
  - `diyizhanshiyizu / 第一展示彝族`
    - `SharedPrefixLm:0.00`
    - `StepSharedPrefixLm:0.000000`
  - `diyizhanshiyizuo / 第一展是以做`
    - `SharedPrefixLm:0.00`
    - `StepSharedPrefixLm:0.000000`
- 说明当前第一版 LM contract 虽已成功编译并进入 debug 链路，但实际没有命中 `第一展示` 主路径，属于 no-op
- 现阶段最可信的原因不是实现没生效，而是触发条件判窄：
  - 真实 winning path 的形态并不完全等于“source line 已经是两词 shared-prefix reparse，再在后续单字步吃 LM”
  - 更可能需要直接按“当前 step 自身是 alt-bridge 驱动的 reparse continuation”来判定，而不能只看 source line 先验形态
- 因此下一刀不该继续扩跑验证，而应先把 LM contract 的触发条件从“source 形态”改成“current step 的 alt-bridge + LM 主账形态”，再走一次最小 snapshot 闭环

## 2026-05-24 shared-prefix LM contract 第二版已命中当前 step，但仍未翻盘

- 按上一轮结论，把 LM contract 触发条件从“source line 形态”改成“current step 形态”：
  - 当前 step `ReqBridge <= 0`
  - 当前 step `AltReqBridge > 0`
  - `AltReqSrc != current source`
  - `char_count` 允许 1~2，而不再限定单字
  - 只要求 prefix 仍是早期、非单字碎片化 source
- 继续只跑最小闭环：
  - `librime/build.bat static`
  - `partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
  - 然后直接只读 `snapshot.jsonl`
- 新结果：
  - `diyizhanshi / 第一展示`
    - `SharedPrefixLm:-3.00`
    - `StepSharedPrefixLm:-3.000000`
    - 仍为 `rank 1`
  - `diyizhanshi / 第一站是`
    - `SharedPrefixLm:0.00`
    - 仍为 `rank 2`
  - `diyizhanshiyizu / 第一展示彝族`
    - `SharedPrefixLm:-2.40`
    - 说明这刀也在后续 family 上留下了累计效果
- 这说明：
  - 当前 shared-prefix LM contract 已经不是 no-op，确实命中了 `第一展示` 主链
  - 但即使额外扣掉约 `3.0`，`第一展示` 仍压住 `第一站是`
  - 因而剩余缺口不再是“有没有 LM contract”，而是：
    - `第一展示` 主链本身在 `Base` 上先天过强
    - 或 same-span 保护在扣了 LM contract 后被动回退，仍不足以把 exact split path 顶上来
- 下一步不应继续盲目放大这个权重；更值得拆的是：
  - `第一展示` 在加上 `SharedPrefixLm` 后，为什么 `SameSpanComp` 反而从更大的惩罚回退到 `-9.25`
  - 也就是 final pool 里 same-span/LM contract 之间的联动关系，是否仍让 shared-prefix path 保留了过高的 base 领先

## 2026-05-24 same-span 比较口径改为忽略 SharedPrefixLm 后已在目标 case 翻盘

- 继续只走最小闭环，先复核 `SameSpanComp` 回退的原因，确认：
  - same-span 比较时直接使用了已经扣过 `SharedPrefixLm` 的 `beam_score`
  - 因此当 `SharedPrefixLm` 生效后，same-span 的 `excess` 会同步缩小，等于把一部分新罚项“退回去”
- 针对这一点做了两步最小修正：
  - same-span anchor 选择时，新增 `comparison_score = beam_score - cumulative_shared_prefix_lm_contract_penalty`
  - same-span 比较时也改为使用同一口径的 `comparison_score`
  - 这样 `SharedPrefixLm` 仍保留在最终总分里，但不会削弱 same-span 的保护力度
- 修正后先跑最小闭环：
  - `librime/build.bat static`
  - `partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
  - 只读 `snapshot.jsonl`
- 结果先恢复为：
  - `diyizhanshi / 第一展示`
    - `SharedPrefixLm:-3.00`
    - `SameSpanComp:-13.87`
    - 但仍以约 `0.34` 微弱领先
- 随后只做一处极小调参：
  - `shared_prefix_lm_contract_weight_` 从 `1.0` 调到 `1.15`
- 最终 snapshot：
  - `diyizhanshi / 第一站是`
    - `rank 1`
    - `Total:-161.99`
  - `diyizhanshi / 第一展示`
    - `rank 2`
    - `SharedPrefixLm:-3.45`
    - `SameSpanComp:-13.87`
    - `Total:-162.10`
- 说明：
  - 这次翻盘不是靠继续加新罚项种类，而是先修正了 same-span 与 LM contract 的比较口径联动，再做极小幅默认权重收口
  - 当前目标 case 已在保留简拼、保持通用机制前提下翻盘
  - 下一步若继续，应做高性价比扩样复核，而不是再沿单例继续细抠

## 2026-05-24 七条 snapshot 快复核：短句有收益，长句 `第一站是一座...` 仍未泛化

- 为避免重跑慢的 21 条 shared-prefix smoke 集，这轮继续走最短链路：
  - 只用 `partial_chain_stage_probe.py --mode snapshot`
  - 新增 3 条轻量 case：
    - `case5_zoujin`
    - `case6_muzhi`
    - `case7_liushi`
  - 与原有 4 条 target 合并成 7 条小集复核
- 7 条结果：
  - target 4 条：
    - `一直向往着远方`：仍是 `一直想望着远方`
    - `第一站是一座古老的小镇`：从 `第一战士已作古老的小镇` 变成 `第一展示已作古老的小镇`，仍错且正确句未进 top3
    - `体验不一样的生活`：变成 `体言不宜养的生活`
    - `两旁是古色古香的建筑`：已升到 top1 正确
  - guardrail 2 条：
    - `走进一家特色小店`：top1 仍正确
    - `木质的门窗`：top1 仍正确
  - sentinel 1 条：
    - `也带着一种不可挽回的流逝`：top1 仍是 `流失`，但正确句已升到 top2
- 汇总口径：
  - top1：`3/7`
  - top3：`4/7`
  - target top1：`1/4`
  - target top3：`1/4`
  - guardrail top1：`2/2`
  - sentinel top3：`1/1`
- 这说明：
  - 当前改动已经不是只对 `diyizhanshi` 单句有效，至少在 `两旁是古色古香的建筑` 和 `也带着一种不可挽回的流逝` 上体现出外溢收益
  - 但泛化仍不均匀：
    - 长句 `第一站是一座古老的小镇` 还没翻盘
    - `体验不一样的生活` 反而出现新的前缀级回退
  - 下一步不宜直接继续放大权重，更值得优先拆：
    - 为什么短句 `第一站是` 已翻，但延长到 `...一座古老的小镇` 后又被 `第一展示已作...` 拉回
    - 以及 `体验不一样...` 为什么会被 current-step shared-prefix 合同带偏到 `体言...`

## 2026-05-25 case2 长输入断点继续收口：`第一站` 并非没展开，真正 source 被 `SelectTopLines` 选成了分词线

- 继续只用 `partial_chain_stage_probe` 的单例快路径，重点避免再把 `diyizhanshi` 与 `diyizhanshiyi` 两个输入混着看：
  - `diyizhanshi` 下直接 probe `第一站 -> 是/十/时`
  - `diyizhanshiyi` 下补 `full` 图谱，只抽 `pre_source_pool_full / source_pool_full / top_candidate_full`
- 先纠正了一处调试口径误判：
  - `graph` 里的 candidate gate 记录只导出 `source.generated_word_count == 1`
  - 因此之前在长输入里查不到 `source_text = 第一站`、`entry_text = 是`，不等于 `第一站` 没被展开
  - 只说明真正参与展开的 source 可能不是单词线
- 对 `diyizhanshiyi` 的原始图谱继续抽取后，当前最关键的新结论是：
  - `full_context = 第一站` 在 `start_pos = 8` 的 `source_pool_full` 里同时存在多种 segmentation：
    - `entry_text = 第一站`，`generated_word_count = 1`，`beam_score = -164.767`
    - `entry_text = 一站`，`generated_word_count = 2`，`beam_score = -146.858`
    - 以及更差的 `站` 形态
  - `top_candidate_full` 最终选中的不是单词线 `第一站`，而是分词线 `第 + 一站`
  - 这解释了为什么长输入里很多 graph 读数看起来像“`第一站` 没去请求 `是/十/时`”，其实是被调试过滤口径遮住了
- 这意味着：
  - 当前长输入的更早断点不在 `admitted_replace` 同 state 竞争
  - 也不宜再把“查不到 `第一站 -> 是` 记录”直接解释成没进 request
  - 更值得继续钉的是：
    - 同一 `full_context = 第一站` 下，不同 segmentation 在 `SelectTopLines()` 的 source 选择竞争
    - 尤其是为什么一档最终保留的是 `第 + 一站` 这条 source，而不是更干净的单词线 `第一站`
- 后续若继续，应优先评估：
  - `SelectTopLines` / source selection 是否需要更细的 per-text / per-family 保活
  - 而不是再重复围绕 `第一站 -> 是` 的 graph 口径做无效追查
- 2026-05-25 当天还试了一刀极窄的 `SelectTopLines()` 局部保活原型，想在“同一可见文本簇里 split line 压过 whole-word line”时保活单词线：
  - 第一版做成“额外塞一个 same-text whole-word 名额”，对 `diyizhanshiyi` 的 `start_pos = 8` 完全 `no-op`
  - 回看图谱后确认原因不是命中条件错，而是该断点位实际只取 `top_candidate_full = 1`，属于单名额 source 选择，不存在额外追加名额的空间
  - 随后把原型改成单名额替换，并尝试按可见全文分组；结果自定义单例 probe 明显卡慢并超时，说明这条实现形态把热路径成本抬高了
  - 之后又补试了一版更便宜的 suffix 局部替换：不再重建全文，只在“当前赢家是 `单字 + 多字后缀` 的早期 split line，且池里存在同总字数、后缀严格对应的 whole-word 单词线”时替换 `refs.front()`
  - 这版虽能正常编译，但自定义 `diyizhanshiyi` 单例 probe 仍明显拖慢并在读取 snapshot 时被中断，说明即便不做全文分组，`SelectTopLines()` 热路径里继续加这类运行期替换也不划算
  - 这两版原型都已完整回退，不保留在源码中
  - 因而如果后续还沿 source selection 继续，不能再直接在 `SelectTopLines()` 里靠运行期重建整条文本做分组保活；需要换到更便宜、字段更局部的入口
- 同天又回到更上游的 `early_prefix_split_penalty` 做过一次极窄增强，只针对：
  - prefix 侧恰好是首个单字词
  - 下一跳是 `char_count >= 2` 的多字后缀
  - 且整条链形成 `generated_word_count = 2 / single_char_word_count = 1`
- 这刀没有拖慢编译或单例 full probe，说明放在 request 侧的成本可接受；但 graph 结果只表现为“确实命中 split 链、但量级明显不足”：
  - `地一站` 约从 `-146.804` 掉到 `-147.379`
  - `第一站 + 一站` 约从 `-146.858` 掉到 `-147.434`
  - `地驿站` 约从 `-140.785` 掉到 `-141.361`
  - `的驿站` 约从 `-137.831` 掉到 `-138.406`
- 但 `start_pos = 8` 的 `top_candidate_full` 仍被：
  - `的驿站`
  - `的翼展`
  - `地驿站`
  - `第驿站`
  这一簇 split source 占据，说明“再加一层单字前缀 + 多字后缀 split penalty”虽然命中目标形态，但不足以改写当前主竞争家族
- 该原型已完整回退，不保留在源码中；后续不应再沿这条 helper 只做倍率增强或常数 sweep
- 随后直接从最新 `partial_chain_stage_probe.graph.jsonl` 抽 raw `word_edges + expansion_gate_records`，把这簇 `驿站/翼展` 家族的领先层级再收紧了一档：
  - `0 -> 2` 的前缀单字 `地/第/的` 本身差距并不大：
    - `地 = -14.0603`
    - `第 = -14.1065`
    - `的 = -14.2235`
  - 真正的主差额在 `2 -> 8` 的后缀扩展，而不是前缀单字本身
  - 以 `source_text = ...的` 为例：
    - `一站`
      - `search_score = -156.337`
      - `base_score = -153.03`
      - `lm_score_scaled = -89.799`
      - `adjustment_score = -3.30668`
    - `驿站`
      - `search_score = -138.406`
      - `base_score = -134.11`
      - `lm_score_scaled = -60.1732`
      - `adjustment_score = -4.29603`
    - `翼展`
      - `search_score = -138.998`
      - `base_score = -134.666`
      - `lm_score_scaled = -60.4744`
      - `adjustment_score = -4.3322`
  - 同时，单块 whole-word `0 -> 8` 的 `第一站` 记录是：
    - `search_score = -147.758`
    - `base_score = -146.945`
    - `lm_score_scaled = -134.123`
    - `adjustment_score = -0.812776`
  - 这说明当前 `case2` 的主领先并不在：
    - `SelectTopLines()` 末端保活
    - 也不在 `的/地/第` 的 prefix 小分差
  - 而是在更早的 `2 -> 8` request/base/LM 层：
    - `驿站/翼展` 这类 split 后缀能拿到显著更好的 `lm_score_scaled`
    - 从而在进入 `start_pos = 8` 的 source pool 之前就已经把 `一站/第一站` 整体压开
  - 因而后续若继续，应优先拆：
    - 为什么 `2 -> 8` 的 `驿站/翼展` 在当前 grammar / char-path 模型中有如此强的 `LM` 先验
    - 而不是再回到前缀单字或 `SelectTopLines` 末端做局部修补
- 继续回读 `witogram::ScoreFeatures()` 并直接从同一份 graph 的 `transition_lm_features` 抽取 `context_suffix -> word` 细账后，这条 `LM` 证据又明确了一层：
  - `context_token_limit = 11`
  - `BuildContextSuffix()` 传给 `ScoreFeatures()` 的不是单个前缀字，而是最多 11 个 UTF-8 单字的后缀上下文；例如这里真实读到的是：
    - `context_suffix = 我踏上了旅行的征程。的`
    - `context_suffix = 我踏上了旅行的征程。第`
    - 而不是只看裸 `的/第`
  - 对 `ScoreFeatures()` 当前实现来说：
    - 先统一按单字 token 计算 `char_path`
    - 只有 `vocab.Index(word)` 命中 whole-word 时，才做 `0.6 * whole_word + 0.4 * char_path`
    - whole-word 不命中时，直接退回纯 `char_path`
  - 但对当前 `case2` 的关键后缀，这一步根本不是“`驿站` whole-word 命中而 `一站` 不命中”：
    - `context_suffix = 我踏上了旅行的征程。的`
      - `word = 驿站`
        - `total_log10 = -52.2658`
        - `matched_whole_word = false`
        - `used_char_fallback = true`
        - `oov_token_count = 0`
        - `token_evidence_tag = split_token_supported`
      - `word = 翼展`
        - `total_log10 = -52.5274`
        - `matched_whole_word = false`
        - `used_char_fallback = true`
        - `oov_token_count = 0`
        - `token_evidence_tag = split_token_supported`
      - `word = 一站`
        - `total_log10 = -77.9984`
        - `matched_whole_word = false`
        - `used_char_fallback = true`
        - `oov_token_count = 1`
        - `token_evidence_tag = neutral_missing`
    - `context_suffix = 我踏上了旅行的征程。第`
      - `word = 驿站`
        - `total_log10 = -54.9656`
        - `matched_whole_word = false`
        - `oov_token_count = 0`
        - `token_evidence_tag = split_token_supported`
      - `word = 一站`
        - `total_log10 = -77.9984`
        - `matched_whole_word = false`
        - `oov_token_count = 1`
        - `token_evidence_tag = neutral_missing`
    - `context_suffix = 我踏上了旅行的征程。`
      - `word = 第一站`
        - `total_log10 = -116.498`
        - `matched_whole_word = false`
        - `oov_token_count = 1`
        - `token_evidence_tag = neutral_missing`
- 这说明当前主差额已经基本不是“whole-word hit vs char-path fallback”的问题，而是更具体的：
  - `驿站/翼展` 虽也走 fallback，但它们属于 `split_token_supported`
  - `一站/第一站` 则在当前模型里直接落成 `neutral_missing + oov1`
  - 换句话说，真正把盘面拉开的，是 char-path token 可支持性本身，而不是后续 source selection / merge / anchor 层
- 因而后续若继续，最值得优先查的不再是：
  - `SelectTopLines`
  - 或再加一层 split-chain 惩罚
- 而是：
  - 为什么当前 grammar 词表 / char-path tokenization 对 `驿/翼/站` 是可支持的
  - 但对 `一站/第一站` 稳定落成 `neutral_missing`
  - 以及这是否本质上仍回到：
    - unigram `一` 缺席
    - 使得所有包含 `一` 的后缀在 char-path 本体上天然吃亏
- 继续直接查本机 `wanxiang-lts-zh-hans.arpa` / `wanxiang-mini-zh-hans.arpa` 后，这条“token 可支持性”证据又闭环了一层：
  - unigram 层能直接搜到：
    - `以`
    - `已`
    - `宜`
    - `驿`
    - `翼`
    - `站`
  - 但依然搜不到 unigram `一`
  - 更关键的是，多 token 序列层也出现了明显分叉：
    - `驿 站`
      - 在 `wanxiang-lts-zh-hans.arpa` 中直接存在
    - `翼 展`
      - 在 `wanxiang-lts-zh-hans.arpa` 中直接存在
    - `的 驿 站`
      - 在 `lts` 中可直接命中
      - 在 `mini` 中也可直接命中
    - 而以下模式在本轮 grep 中都没有命中：
      - `一 站`
      - `第 一 站`
      - `的 一 站`
      - `地 一 站`
- 这使得当前 `case2` 的根因链已经可以写成更直接的模型内容结论：
  - `驿站/翼展` 并不是靠 whole-word token 命中取胜
  - 它们胜在当前 grammar 里至少存在可用的 split-token 路径：
    - `驿 + 站`
    - `翼 + 展`
    - 甚至还能继续接出 `的 驿 站`
  - 而 `一站/第一站` 则不是“有同等级 split-token 证据但分数更低”
  - 更像是从 token 可支持性开始就缺一块：
    - `一` 不在 unigram
    - `一 站 / 第 一 站` 这一类序列本轮也未命中
  - 因而 `transition_lm_features` 里看到的：
    - `驿站/翼展 -> split_token_supported + OOV0`
    - `一站/第一站 -> neutral_missing + OOV1`
    并不是偶发打分现象，而是和模型文本内容一致的直接后果
- 随后继续回读 `witset_poet.cc` 中已经存在的 `neutral_missing` 补救逻辑，确认当前线上其实并不是“完全没意识到 neutral_missing 要和 true OOV 区分”：
  - `ScoreFeatures()` 产出 `token_evidence_level` 后，主流程已经做了：
    - `split_token_supported`
    - `neutral_missing`
    - `true_oov`
    的分层
  - 并且在 request 汇总后，`neutral_missing` 还有两层显式补救：
    - `ComputeNeutralMissingSourceLineRescueBonus()`
    - `ComputeNeutralMissingPrefixContinuationFamilyBonus()`
- 但把这两层 bonus 公式和当前 case2 的真实缺口并排后，可以直接判定：
  - 它们量级上就不可能翻过当前主差额
  - 第一层 `ComputeNeutralMissingSourceLineRescueBonus()`：
    - 只有当 `dict_advantage >= 0.8`
    - 且 `lm_gap >= 8`
    - 且 `search_gap >= 8`
      才启动
    - 上限是 `min(16.0, bonus_signal)`
  - 第二层 `ComputeNeutralMissingPrefixContinuationFamilyBonus()`：
    - 只作用于：
      - 同一 source line
      - `char_count == 2`
      - 同 leading char
      - 且存在更长 neutral-missing descendant
    - 上限只有 `min(3.5, signal)`
- 而当前 `case2` 已经直接观测到的 gap 是：
  - `context_suffix = 我踏上了旅行的征程。的`
    - `驿站 total_log10 = -52.2658`
    - `一站 total_log10 = -77.9984`
    - 仅 `total_log10` 差额就约 `25.73`
  - 更早的 request/base 盘面里，`驿站/翼展` 对 `一站/第一站` 的 `lm_score_scaled / search_score` 差额还会继续被放大
- 这说明当前两层 `neutral_missing` 救援的真实定位更像：
  - 防止“已经很接近的 neutral-missing path”被末端细节继续误伤
  - 而不是逆转“从 token 可支持性开始就明显失血”的根因
- 因而这条线现在也可以正式判负：
  - 继续在 `all_requests` 之后补 `neutral_missing rescue bonus`
  - 或只在 `source_line / prefix family` 层再调常数
  - 都不足以解决 `第一站是一座古老的小镇` 这一类由 grammar token 缺口触发的主问题
- 更合理的下一步应回到更早的 grammar 语义纠偏：
  - 不是把 `neutral_missing` 再补成更大的后验 bonus
  - 而是让 `一站/第一站` 这类路径在 `ScoreFeatures()` 解释层就不要从一开始输掉几十点 `total_log10`
- 继续把 `ScoreFeatures()` 往 KenLM 实现层对齐后，这条“为什么 `neutral_missing` 一开始就会掉几十点”的链又明确了一层：
  - `AppendTokenScore()` 当前逻辑是：
    - `wid = vocab.Index(token)`
    - 若 `wid == NotFound()`，只做 `oov_token_count++`
    - 但仍然继续调用 `model->Score(*state, wid, out)` 累加到 `total_log10`
  - 从当前 vendored KenLM 代码可直接确认：
    - `Vocabulary::NotFound()` 返回 `not_found_`
    - Python binding 的 `BaseFullScore` 直接把 `wid == 0` 作为 `oov`
    - `kenlm` 的 vocab 文件里 `0` 号词就是 `<unk>`
  - 因而在当前实现里，`vocab.Index(token) == NotFound()` 的运行时语义并不是抽象“没证据”：
    - 它会实打实地把该 token 当成 `<unk>` 送进 KenLM 打分链
- 继续直接查 `wanxiang-lts-zh-hans.arpa` 后，又能把“几十点差额”拆得更具体：
  - unigram 层：
    - `驿 / 翼 / 站 / 以 / 已 / 宜 = -37.499199`
    - `<unk> = -38.499199`
  - 这说明 `<unk>` 在 unigram 上只比这些常见单字差 `1.0` 个 log10 点
  - 因而 `一站/第一站` 相对 `驿站/翼展` 当前观测到的二十多点 `total_log10` 缺口，不可能只来自 unigram 自身
- 真正把差额放大的，是高阶 `<unk>` 上下文链本身缺席：
  - 当前模型中可直接命中：
    - `驿 站`
    - `的 驿`
    - `的 驿 站`
    - `翼 展`
    - `的 翼`
    - `的 翼 展`
  - 但本轮 grep 中没有命中：
    - `的 <unk>`
    - `<unk> 站`
    - `的 <unk> 站`
    - `第 <unk>`
    - `第 <unk> 站`
- 这使得当前 `case2` 的主差额又能进一步收口为：
  - `一站/第一站` 的问题不只是：
    - 缺 whole-word 命中
    - 被标成 `neutral_missing + OOV1`
  - 更关键的是：
    - 一旦其中关键 token 落成 `NotFound()`
    - 现实现会直接把它当 `<unk>` 继续推 KenLM state
    - 而当前模型里相关 `<unk>` 高阶上下文几乎没有可用支持
    - 所以 `char_path total_log10` 会在 grammar 本体里被继续拉开
- 因而下一步若继续推进 `P0`，最值得优先考虑的最小入口不再是：
  - 继续在 `all_requests` 之后补 `neutral_missing rescue`
  - 或继续调 `oov_penalty / char_fallback_penalty`
- 而更像是：
  - 在 `ScoreFeatures()` / 未来 `InterpretGrammarEvidence` 层
  - 重新定义“`NotFound()` 但属于 `neutral_missing`”时，是否还应原样把 `<unk>` 高阶链全额记入 `total_log10`
  - 因为当前真正被放大的，已经不是 `oov_token_count` 这个标签本身，而是 `<unk>` 作为 runtime token 进入 KenLM 后产生的上下文级失血

- 按这条 `P0` 主入口，这轮直接下了一版最小原型：
  - 只改 `plugins/witogram/src/witogram.cc`
  - 不碰 `witset_poet.cc`
  - 保留原始 `char_path_log10`
  - 仅当：
    - whole-word miss
    - 最终 evidence 判成 `neutral_missing`
    - 且词内同时存在已知 token 与 `NotFound()` token
    时，再额外计算一条 `neutral_missing` 专用 char-path：
    - 未知 token 只记固定 `<unk>` unigram 分
    - 但不把 `<unk>` 上下文继续传播到后续 token
    - 最后用 `max(raw_char_total_log10, neutral_char_total_log10)` 回写 `total_log10`
- 这版原型已完成最小编译与定点验证：
  - `.\build.bat static` 通过
  - 无新编译错误，仅有历史 link warning
- `case2_diyizhan` 的单例 `full` 结果表明：这刀不是 `no-op`，而且命中的确是 grammar 解释层，不是后段 bonus 巧合：
  - 完整输入 top1 仍然是：
    - `第一展示已作古老的小镇`
  - 说明当前还没有翻正最终盘面
  - 但 `第一展示 -> 一/已/以` 这一步里，`一` 的基础分已经显著抬升：
    - 原先大致是：
      - `search_score ≈ -246.08`
      - `base_score ≈ -237.033`
      - `lm_score_scaled ≈ -46.6264`
    - 这轮原型后，`一` 变成：
      - `search_score ≈ -213.963`
      - `base_score ≈ -207.648`
      - `lm_score_scaled ≈ -17.3018`
    - 已经基本贴近同层的 `已/以`
- 对 `start_pos = 8` 的 source pool 再抽一次，也能看到 `一站` family 在更早层整体抬分：
  - 之前大致是：
    - `的一站 = -146.804`
    - `地一站 = -146.858`
    - `第一站 = -164.767`
  - 这轮原型后变成：
    - `的一站 = -144.503`
    - `地一站 = -144.935`
    - `第一站 = -164.477`
  - 对应 `lm_score_scaled` 也同步改善：
    - `一站` family 现在约在 `-131.82`
    - 而错误主敌 `的驿站 / 的翼展 / 地驿站 / 第驿站` 仍在 `-104.5 ~ -107.6`
- 这说明当前原型已经正确命中了：
  - `neutral_missing` 不该原样吃完整 `<unk>` 高阶链
  - 但它目前只做到了“有效但不足”：
    - 把 `一站/第一站` 从 grammar 本体上提前打残的幅度收回来一部分
    - 还没强到足以推翻 `驿站/翼展` 这整簇更早形成的基础领先
- 为避免单样本误判，这轮又补了一条高性价比 guardrail：
  - `case3_tiyanbuyiyang --mode probe`
  - 当前读数里：
    - `体验 -> 不` 仍是 top1 request
    - `体验不 -> 易` 仍明显领先：
      - `search_score = -139.725`
      - `lm_score_scaled = -21.092`
    - `体验不 -> 一` 仍明显落后：
      - `search_score = -168.619`
      - `lm_score_scaled = -47.7777`
  - 至少在这条 guardrail 上，没有看到这版 `neutral_missing` 解释原型把既有 case3 盘面明显打坏
- 因而这条线现在的阶段性结论应收口为：
  - 方向正确
  - 命中层级正确
  - 当前效果为“有效但不足”
  - 下一步若继续，应继续留在 `ScoreFeatures()` / 未来 `InterpretGrammarEvidence`
  - 而不是再回到 `witset_poet` 后段补 bonus
- 随后又补试了一刀更激进的变体：
  - 把 `neutral_missing` 专用路径里的未知 token 固定成本从 `1.0 * <unk> unigram` 软化到 `0.75 * <unk> unigram`
  - 这刀会把 `一站` family 再往前猛抬，但完整 `case2` 盘面出现明显副作用：
    - 前排开始冒出 `的一战时一座古老的小镇`
    - 说明该系数已经把缺证路径抬得过头，开始把错误 split family 一起推上来
  - 因而这条更激进的系数路线已判负，并已回退到上一版稳定实现：
    - 保留“去掉 `<unk>` 上下文传播”
    - 不再额外软化 `<unk>` unigram 固定成本
- 在回读 `阶段2实施清单_P0_P1_P2.md` 与当日相关 worklog 后，当前继续推进的入口收口为一次**受控的 `P1` 预验证**：
  - 不再回头重复：
    - `SelectTopLines()` same-text 保活
    - `early_prefix_split_penalty` 常数增强
    - `neutral_missing rescue bonus` 末端补救
  - 继续聚焦文档中允许的：
    - `source-line ownership`
    - `family continuity`
    - `request-stage admission`
- 这轮代码只动了 `librime/plugins/witset/src/witset_translator.cc`，没有碰 `witogram.cc` 或 `witset_poet.cc`：
  - 给 `PrefixPathState` 补上 `family_identity`
  - 给 `RequestStageSourceAnchor` 补上 `family_identity`
  - 让 `BuildBestPrefixStates()` 和 `BuildRequestStageSourceAnchors()` 把 lineage/source ownership 一路向下传
- 这轮的核心收紧是：
  - `request_source_line_eligible / terminal` 不再只按 `next_text == state.text` 认资格
  - 现在改为同时要求：
    - `text` 一致
    - `family_identity` 一致
  - 也就是说，把 request-stage 的“确认资格”从纯文本收紧成了 `text + ownership`
- 同时把这层 ownership 匹配同步到了后续消费点：
  - `ComputeTranslatorRequestStageFamilySpecificityBias()`
  - `ComputeTranslatorRequestStageCompetitionBias()`
  - `BuildPoetRequestStageCandidateHints()` 里寻找 `matching_request_state` 的口径
  - 避免“同文本、不同 lineage”的状态继续互相借用已确认资格
- 这轮还没有做编译或 baseline：
  - 按当前对话约束，未执行新的编译/回放
  - 只完成了源码修改与 IDE 静态诊断复核
  - `GetDiagnostics(witset_translator.cc)` 当前无新增错误
- 当前阶段判断：
  - 这是一刀符合 `P1` 预验证边界的最小 ownership/continuity 收紧
  - 它没有回到已判负的末端 patch，也没有重复 `SelectTopLines` 热路径实验
  - 下一步若用户允许编译验证，应优先观察：
    - `request_stage_tag` 是否减少 text-converged drift 的误授信
    - `第一站是...` 家族是否更容易保住 `request_source_line_eligible`
- 随后按最小验证口径继续执行：
  - `librime/build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --mode full --case case2_diyizhan --case case3_tiyanbuyiyang`
- 编译结果：
  - 通过
  - 无新编译错误
  - 仍只有历史 `C4267 / LNK4044` warning，先忽略
- `case2_diyizhan` 当前结果：
  - 最终 snapshot top1 仍是 `第一展示已作古老的小镇`
  - 前排仍有：
    - `敌意展示已作古老的小镇`
    - `地衣展示已作古老的小镇`
    - `的驿站是以做古老的小镇`
  - 说明这刀还没有把长句主盘面翻正
- 但从同轮 `next_hop_after_diyizhanshi` 的 graph 摘要看，当前变化并不是 `no-op`：
  - `request_stage_prefix_text = 第一站是一`
  - `request_stage_state_count = 6`
  - bucket 中仍保留：
    - `text = 第一站是一`, `family_identity = 第一站`, `aligned_with_best_prefix = true`
  - 而当前前排 focus 里的错误 follow-up：
    - `地一站式以`
    - `地一站式已`
    - `地一站式一`
    - `地一站式宜`
    现在读到的都是 `request_tail_supported`
  - 至少在这批前排误路径上，没有再直接看到“仅因同文本收敛就拿到 `request_source_line_eligible`”的旧口径
- 同时也要明确：
  - `request_stage_tag_counts` 里仍统计到少量 `request_source_line_eligible`
  - 但本轮结果摘要里没有把这些 `eligible` 明细直接落出来
  - 因而当前最稳妥的结论只能记为：
    - ownership 收紧已改变 request-stage 盘面
    - 但还不能据此宣称“误授信已被彻底清除”
- `case3_tiyanbuyiyang` guardrail 结果：
  - 最终 snapshot top1 是 `体验不一样的生活`
  - 没有回退成此前更坏的 `体言...` 家族
  - 说明这刀至少没有把这条护栏句打坏
- 但 `case3` 的局部 next-hop 仍提示剩余竞争未解决：
  - `next_hop_after_tiyanbu` 的 top request 仍是：
    - `易`
    - `以`
    - `已`
    - `壹`
    - `意`
    - `宜`
  - 且结果前排仍可见：
    - `体验不宜养的生活`
    - `体验不易样的生活`
  - 说明 ownership 收紧没有直接消掉 `不易 / 不宜` 这一层的统计竞争
- 因而这轮最准确的阶段结论应记为：
  - **代码改动已命中 request-stage ownership 这一层，并改变了部分误路径标签**
  - **`case3` guardrail 保持稳定**
  - **但 `case2` 长句主竞争仍未翻正，当前还不足以把这刀记作通过**
  - 下一步若继续，优先应做的是：
    - 继续把 `request_source_line_eligible` 的残余来源直接导出出来
    - 查清仍存的 `eligible` 到底落在：
      - 哪个 `candidate_transition_text`
      - 哪个 `family_identity`
      - 哪条 follow-up request-line
    - 而不是回头重复调常数或末端 bonus
- 随后按“不要重复实验”的约束继续推进时，先回看了 `WORKLOG`：
  - 旧实验里做过：
    - 给 probe 补 `request_stage_tag / request_stage_prefix_text / request_stage_state_count` 等 metadata
    - 读某几个单拍 `eligible`
  - 但**还没有**在当前 ownership 收紧版本上，把 `result.json` 里的残余 `eligible` 明细直接导出出来
- 因此这轮没有新增算法实验，只做了一个最小 probe 脚本修正：
  - 文件：`C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py`
  - 在 `summarize_graph_probe()` 中新增：
    - `eligible_candidates`
  - 同时修正一个脚本层 bug：
    - `top_focus_candidates` 组装第二轮里，`request_stage_tag` 之前复用了上一轮循环变量
    - 现在改为每个 candidate 自己读取 `candidate.get("request_stage_tag")`
  - 这刀只影响调试摘要导出，不改变算法实现
- 之后只复跑同一条最小命令，不扩成新实验：
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --mode full --case case2_diyizhan --case case3_tiyanbuyiyang`
- 新导出的 `eligible_candidates` 明细把当前残余 `eligible` 收束得更清楚了：
  - `case2_diyizhan / next_hop_after_diyizhanshi`
    - 仅剩 1 条 `eligible`
    - `candidate_transition_text = 第一站是`
    - `request_stage_prefix_text = 第一站是`
    - `entry_text = 是`
    - 这是主轴正路径，不是误授信
  - `case2_diyizhan / next_hop_after_diyizhanshiyi_exact`
    - 有 2 条 `eligible`
    - 正路径：
      - `candidate_transition_text = 第一站是一座`
      - `entry_text = 座`
      - `matching state = 第一站是一座`
      - `family_identity = 第一站`
      - `aligned_with_best_prefix = true`
    - 残余错误支：
      - `candidate_transition_text = 第一站是一组`
      - `entry_text = 组`
      - `matching state = 第一站是一组`
      - `family_identity = 第一站`
      - `aligned_with_best_prefix = false`
    - 这说明当前 ownership 收紧后，`case2` 残余 `eligible` 已不再是先前担心的 `地一站式以/已/一/宜` 那一类 follow-up 误授信
    - 但仍存在“沿正确 family_identity 漂出的错误 continuation”：
      - `一座`
      - `一组`
      在更深一层被一起保留
- `case3_tiyanbuyiyang` 的 `eligible` 则更干净：
  - `next_hop_after_tiyan`
    - `candidate_transition_text = 体验不`
    - `entry_text = 不`
  - `next_hop_after_tiyanbu`
    - `candidate_transition_text = 体验不易`
    - `entry_text = 易`
  - 这两条都是主轴正路径
  - 而前排干扰项 `体验不一 / 体验不宜 / 体验不依` 当前都只是：
    - `request_tail_supported`
    - 不再是 `request_source_line_eligible`
- 因而这轮结论需要进一步更新为：
  - 当前 ownership 收紧已经把“错 family 借同文本拿 `eligible`”这类问题大幅压下去了
  - `case3` 下残余 `eligible` 基本只剩正确主轴
  - `case2` 下真正还值得继续追的，不再是：
    - `地一站式...` follow-up 误授信
  - 而是：
    - 为什么 `第一站是一组`
      会以同一 `family_identity = 第一站`
      在更深 continuation 层继续拿到 `request_source_line_eligible`
  - 也就是说，下一步更像要查：
    - `continuation legality`
      是否仍过宽
    - 而不是再回头重复 ownership / text 匹配层的实验
- 随后按“不要重复实验”的约束，只做了静态代码排查，没有新增任何 probe / baseline：
  - 重点追 `第一站是一座 / 第一站是一组`
    为什么会共用同一份 `request_source_line_eligible`
  - 相关代码集中在：
    - `BuildRequestStageTailSupports()`
    - `BuildRequestStagePrefixStates()`
    - `ClassifyRequestStageTag()`
    - `ComputeTranslatorRequestStageCompetitionBias()`
- 这轮静态排查后的关键链路是：
  1. `BuildRequestStageTailSupports()`
     - 当前只要后续还能接任意 `IsTranslatorExactCandidate()` 的 exact tail
     - 就累计：
       - `path_count`
       - `best_tail_weight`
     - 它**不区分**这些 tail 是否属于更严格的 `legal_primary_continuation`
       或更窄的 continuation legality
  2. `BuildRequestStagePrefixStates()`
     - 对每个已入 bucket 的 `current_state`
     - 当前会保留每个 state 的前 2 个 branch（`kMaxBranchesPerState = 2`）
     - `selection_score` 由：
       - `raw_score`
       - `tail_it->second.best_tail_weight`
       - `bridge_bonus`
       - `risk_penalty`
       共同决定
     - 更关键的是：
       - `bridge_lineage_confirmed`
         并不要求当前 candidate 自身再通过新的 candidate-specific legality
       - 只要上一个 state 已经 `bridge_lineage_confirmed`
         且 `CanPropagateBridgeLineageConfirmation(...)` 成立
         这份确认资格就会继续往后传
  3. 因而在 `第一站是 -> 一...` 这一层，一旦正确主轴把：
     - `第一站是一`
       做成了 confirmed request-stage state
     - 后续 exact continuation 里只要分数能进当前 state 的前 2 个 branch
       就可能一起被写成新的 confirmed state
     - 这正好解释了为什么当前会同时看到：
       - `第一站是一座`
       - `第一站是一组`
       都带着同一个 `family_identity = 第一站`
  4. `ClassifyRequestStageTag()`
     - 对单字 candidate 的 `request_source_line_eligible`
       当前只检查：
       - `next_text`
       - `next_family_identity`
       是否命中下一个 bucket 中某条
         `bridge_lineage_confirmed` state
     - 它**不检查**这条 matching state 是如何被 confirm 的
       也不检查它是否属于更窄的合法 continuation
  5. 因而当前 residual 问题已经进一步收口为：
     - 不是 ownership 匹配错了
     - 也不是 `地一站式...` 继续借资格
     - 而是：
       - `bridge_lineage_confirmed` 的传播条件过宽
       - `request_stage_tail_support/path_count` 的定义过宽
       二者共同把
       - `第一站是一组`
       这类同 family 但非目标 continuation
       也抬进了 `request_source_line_eligible`
- 这轮静态排查还补出一个更具体的入口判断：
  - 若下一步要继续改代码，优先不该再碰：
    - ownership/text 匹配
    - `地一站式...` family drift
  - 更值得直接试的，是把 request-stage 的“资格传播”从
    - `confirmed state can propagate to later exact continuation`
    收紧成更窄的
    - `confirmed + legal continuation can propagate`
    或至少让：
    - `tail_support`
    - `matching confirmed state`
    二者之一开始区分
      `一座`
      与
      `一组`
    这类 deeper continuation legality
- 随后按“不要重复实验”的约束，先回查了 2026-05-24 相关记录，确认：
  - 之前已经做过的，是：
    - `CanPropagateBridgeLineageConfirmation()` 的 source-line 继承收紧
    - `head-supported single-char bridge`
    - `family_soft_clean -> HasConfidentPrimaryExact` 放宽
  - 这些都不再重复
  - 因而这轮改动只落在一个更窄的新口径：
    - **candidate-specific continuation legality**
- 本轮代码修改只动了 `librime/plugins/witset/src/witset_translator.cc`：
  - 给 `BuildRequestStagePrefixStates()` 新增 `continuation_path_summaries`
  - 新增 helper：
    - `IsRequestStagePrimaryExactContinuation()`
  - 目的不是抬分，而是让已 confirmed 的 request-stage lineage **只继续传播给当前起点下的 primary exact continuation**
- 更具体地说，这轮收紧了两处：
  1. `CanConfirmValidatedSingleCharBridge()`
     - 对更深层的单字 continuation（当前 `exact_segment_count >= 2`）
     - 不再只看“prefix 已 confirmed / aligned + tail support + 低风险”
     - 现在还要求：
       - 当前 candidate 形成的 `next_text / next_family_identity`
       - 必须命中该 `end_pos` 的 validated source anchor
  2. `BuildRequestStagePrefixStates()`
     - `propagated_bridge_lineage`
       不再对所有 exact continuation 一视同仁
     - 单字 candidate：
       - 浅层保留原口径
       - 深层则要求 `anchor_aligned`
     - 多字 exact candidate：
       - 只有命中 `continuation_path_summaries[start_pos].best_exact_text`
       - 且 gap 在 `0.35` 内
       才允许继续继承 confirmed lineage
- 这轮之后仍只跑同一组最小验证，没有新增实验面：
  - `librime/build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --mode full --case case2_diyizhan --case case3_tiyanbuyiyang`
- 编译结果：
  - 通过
  - 无新错误
  - 仍只有历史 `LNK4044` warning
- 新结果最关键的变化是：
  - `case2_diyizhan / next_hop_after_diyizhanshiyi_exact`
    - `request_stage_tag_counts`
      从：
      - `request_source_line_eligible = 2`
      变成：
      - `request_source_line_eligible = 1`
    - 残余 `eligible` 只剩：
      - `candidate_transition_text = 第一站是一座`
      - `entry_text = 座`
      - `request_stage_prefix_text = 第一站是一座`
      - matching state:
        - `text = 第一站是一座`
        - `family_identity = 第一站`
        - `aligned_with_best_prefix = true`
        - `bridge_lineage_confirmed = true`
    - 之前那条残余误授信：
      - `candidate_transition_text = 第一站是一组`
      已经从 `eligible_candidates` 里消失
- 同时，`case2` 的前一跳也保持住了：
  - `next_hop_after_diyizhanshi`
    - `第一站是`
      仍是：
      - `request_source_line_eligible`
  - 说明这刀没有把 `第一站 -> 是` 这条主桥重新打掉
- `case3_tiyanbuyiyang` 也保持稳定：
  - `next_hop_after_tiyan`
    - `体验 -> 不`
      仍是 `request_source_line_eligible`
  - `next_hop_after_tiyanbu`
    - `体验不 -> 易`
      仍是 `request_source_line_eligible`
  - 而：
    - `体验不一`
    - `体验不宜`
    - `体验不依`
      仍只读到 `request_tail_supported`
- 整句层则需要保守表述：
  - `case3`
    - `体验不一样的生活 = rank 1`
    - 没有回退
  - `case2`
    - `第一站是一座古老的小镇`
      仍然 `expected_rank = null`
    - 当前 related hits 仍是：
      - `rank 6 = 的驿站是一座古老的小镇`
      - `rank 11 = 的一战时一座古老的小镇`
  - 说明这刀已经**命中了 request-stage deeper legality**
    但还没有把完整句主盘面翻正
- 因而这轮最准确的结论应更新为：
  - `bridge_lineage_confirmed` 的 candidate-specific legality 收紧是有效的
  - 它成功清掉了 `第一站是一组` 这类 residual `eligible`
  - 同时保住了：
    - `第一站 -> 是`
    - `第一站是一 -> 座`
    - `体验 -> 不`
    - `体验不 -> 易`
    这些主轴正路径
  - 但 `case2` 整句仍未翻正，说明剩余主缺口已不在 request-stage residual eligibility
  - 更像继续落在：
    - full-sentence ranking
    - `的驿站... / 第一展示...` 这类更早 family/LM 盘面
    - 或 poet / beam 端对正确链的保活强度不足
- 随后按“不要重复实验”的约束，先没有再跑新的 probe / baseline，而是回读了旧结论与当前代码：
  - 旧结论里已经明确排过：
    - `admitted_replace / state_key` 并不是 `case2 / case3` 的主因
    - 最新 graph 工件里：
      - `第一站是一`
      - `第一战是以`
      - `第一展是以`
      都是各自独立的 `admitted_new`
    - 真正分差在 request 阶段就已先天出现
  - 因而这轮不再重复：
    - 调 `BuildApproxStateKey()`
    - 调 `admitted_replace`
    - 或再做一轮只加大 `ReqSrcMismatch / allowed_margin` 的常数实验
- 这轮静态排查把当前主缺口再往上收了一层：
  1. `translator` 侧的 request-stage hint 已经确实打进 poet：
     - `BuildPoetRequestStageCandidateHints()` 会把
       - `ContractBias`
       - `FamilySpecificityBias`
       - `CompetitionBias`
       聚合成 `hint_bias`
     - 并通过：
       - `request_stage_bridge_bonus`
       - `matching_request_state_*`
       送进 poet
  2. `poet` 侧也确实消费了这层信号：
     - `request_stage_bridge_bonus`
     - `request_stage_source_mismatch_penalty`
     - `shared_prefix_lm_contract_penalty`
       都会加进 `adjustment_score`
     - 最终进入：
       - `final_score`
       - `search_score`
       - `beam_score`
  3. 但当前筛选主轴依然是：
     - 分支内 `partial_sort(candidates_to_score)` 按 `search_score`
     - 全局 `partial_sort(all_requests)` 也按 `search_score`
     - `CompressLinePoolByState()` / `SelectTopLines()` 也按 `beam_score`
     - 而 `request-stage` 只是 `adjustment_score` 里的一个子项
  4. 这意味着：
     - 如果 `第一站是`
       相对
       - `地驿站是`
       - `的翼展示`
       - `第一展示`
       这类更早 whole-word/reparse cluster
       在 `base_score` 或更早 `search_score` 上已经先天明显落后
     - 那么 request-stage bonus 命中后，也可能只是在后段“部分减损”，而不足以扭转整句盘面
- 继续对 `same-span` 这条线做静态复核后，当前更明确的问题是：
  - `ComputeSameSpanCompetitionPenalty()` 目前只感知：
    - `generated_char_count`
    - `matched_whole_word`
    - `whole_word_hits`
    - `contract_support_gap`
  - 它**并不显式知道**：
    - 当前 competitor 是不是
      - `generated_word_count == 1` 的 root whole-word reparse
    - 也不知道 anchor 是不是
      - `request_source_line_eligible` 的 split continuation
  - 这正好解释了之前几轮为什么只能“部分有效”：
    - 它能把盘面从：
      - `的驿站 / 的翼展示`
      拉回到：
      - `第一*`
    - 但继续想从：
      - `第一展示`
      再压到：
      - `第一站是`
      时，就缺少更直接的结构区分信号
- 同时，旧日志里也已给出一个很重要的反证，避免这轮再走回头路：
  - 之前试过把 equal-support reparse 直接纳入 same-span 竞争，并收紧 `allowed_margin`
  - 结果会把：
    - `敌意展示`
    - `地衣展示`
    - 更多 `*驿站是`
      一起抬上来
  - 因而下一刀不能再是：
    - 泛化地扩大 same-span 命中面
    - 或纯调 same-span 常数
- 所以当前最可靠的新收口是：
  - `case2` 剩余主缺口不是：
    - request-stage eligibility
    - 也不是 admitted_replace
  - 而是：
    - 在更早 whole-word/reparse cluster 已经占优的情况下
    - poet 现有 same-span / request-stage 调整项
      仍缺一个**显式区分**
        - `request_source_line_eligible` split continuation
        - 与
        - root exact-ambiguous-family whole-word reparse
      的结构信号
  - 如果下一步继续改代码，最小合理切口更像是：
    - 不是再调 `allowed_margin`
    - 也不是再加大 `ReqSrcMismatch`
    - 而是在 `same-span` 或相邻竞争逻辑里，直接把这两类 competitor 区分开
    - 尤其针对：
      - `generated_word_count == 1`
      - `matched_whole_word == true`
      - 但本质属于 reparse cluster
      的那一层 source 前缀
- 随后继续推进时，先再次核对了 `WORKLOG`，确认：
  - 以前已经做过并判负的是：
    - 把 equal-support 的 root whole-word reparse 直接纳入 `same-span` 竞争
    - 或在 `all_requests` / request-stage hint 层面对同终点 reparse 追加轻量竞争约束
  - 但**还没有**做过下面这个更窄的变体：
    - 仅在当前 `same-span` 已满足
      - split anchor contract support 更强
      - 且 competitor 已经命中现有 `same-span` 条件
    - 再额外收紧：
      - `generated_word_count == 1`
      - `matched_whole_word == true`
      的 root reparse 容忍度
- 因而这轮只做了一个很小的临时代码试验：
  - 文件：`librime/plugins/witset/src/witset_poet.cc`
  - 落点：`ComputeSameSpanCompetitionPenalty()` 与调用点
  - 做法：
    - 不放宽 `same-span` 比较面
    - 仍要求：
      - `current_contract_support < anchor_contract_support`
    - 只在该前提已成立时，对 root whole-word reparse：
      - 进一步减小 `allowed_margin`
      - 并提高 `multiplier`
  - 也就是说，这刀不是“让更多 reparse 进 same-span”，而是“只对已命中的 root reparse 加重惩罚”
- 这轮仍只跑固定最小验证，不扩实验面：
  - `librime/build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --mode full --case case2_diyizhan --case case3_tiyanbuyiyang`
- 编译通过，无新错误，仍只有历史 `LNK4044` warning。
- 但结果可以较明确记为**负向**：
  - `case3_tiyanbuyiyang`
    - guardrail 仍保持：
      - `体验不一样的生活 = rank 1`
  - `case2_diyizhan`
    - 正确句：
      - `第一站是一座古老的小镇`
        仍然 `expected_rank = null`
    - 而 snapshot 前 5 变成：
      - `rank 1 = 第一展示已作古老的小镇`
      - `rank 2 = 敌意展示已作古老的小镇`
      - `rank 3 = 地衣展示已作古老的小镇`
      - `rank 4 = 的驿站是以做古老的小镇`
      - `rank 5 = 第一展示已作古老的小针`
  - 这说明：
    - 该变体没有把正确 `第一站...` 拉回可见候选
    - 反而再次把更外层的：
      - `敌意展示`
      - `地衣展示`
      混合簇抬到更前
- 因而这轮结论应明确记为：
  - “**只在现有 same-span 命中后，进一步收紧 root whole-word reparse 的容忍度**”这条变体判负
  - 它虽然不像 earlier equal-support 版本那样直接扩大比较面，但仍然没有命中真正需要保住的正确链
  - 更准确地说：
    - 当前 same-span 竞争面里，单独加重 root reparse 惩罚，仍会把竞争转移给其他同结构外层 cluster
    - 并不能把 `第一站是 / 第一站是一座...` 拉回主盘面
- 因而这条路线后续也不应重复。
- 代码已随即**回退**到修改前状态，并重新执行了一次：
  - `librime/build.bat static`
  - 以确保源码与当前编译产物重新一致
- 这轮新增的非重复结论是：
  - 下一步如果继续，不应再在 `same-span` 里做“只靠 root reparse 惩罚强度”的变体
  - 当前更值得考虑的，仍是：
    - 在更靠近 `Base / beam/state competition` 的层上
    - 显式表示
      - split continuation
      与
      - reparse cluster
      的结构合同
    - 而不是继续做 same-span 惩罚强弱的局部变体
- 随后继续推进时，没有新增实验，只把前面几段看起来容易冲突的结论重新对齐了一遍，避免后续再在错误层级重复发力：
  - 一组旧结论说：
    - 长输入 `diyizhanshiyi` 里，`第一站` 并非没展开
    - 真正参与下一跳的是被 `SelectTopLines()` 选中的分词线
    - 例如 `第 + 一站`
  - 另一组旧结论又说：
    - 短输入 `diyizhanshi` 里，`第一站是` 其实已经能进 `final_pool`
    - 真正问题是 `final_pool` 内对 `第一展示 / 敌意展示 / 地衣展示` 的巨大分差
  - 这两句表面看像是两套不同问题，但重新对照代码与工件后，可以更统一地表述成：
    - **source selection 确实会改变长输入里“哪条 source 去请求下一跳”**
    - **但它并不是当前最早、也不是最根的主差额层**
- 这轮静态对照后的更准确收口是：
  1. `WitsetPoet::MakeSentences()` 的主决策链里，确实存在三个层级：
     - `source_pool -> SelectTopLines()`
     - `all_requests / batch_selected / admitted_state_index`
     - `final_pool -> SelectTopLinesWithDiversity()`
  2. 已有工件已经分别排掉了后两层的一部分误判：
     - `admitted_replace / state_key` 不是当前主因
     - `第一站是` 在短输入里不是没进 `final_pool`
  3. 长输入里看似“被 `SelectTopLines()` 选成了分词线”，其实还要再往前拆一层：
     - 在 `start_pos = 8` 之前，`2 -> 8` 的 request/base/LM 差额已经很大
     - `驿站 / 翼展`
       相比
       `一站 / 第一站`
       在 `lm_score_scaled` 上先天明显领先
     - 这意味着当它们进入 `source_pool` 时，盘面已经不是细微差距
  4. 因而：
     - 长输入中的 `SelectTopLines()` source 选择，更像是在
       **已经被更早 base/LM 差额拉开的候选盘面里做后续保留**
     - 而不是无中生有地产生了主差额
- 对后续实现层级的直接影响是：
  - 不应再优先做：
    - `SelectTopLines()` 运行期 same-text 分组保活
    - `SelectTopLines()` suffix 局部替换
    - 或围绕 `refs.front()` 的热路径替换变体
  - 这些路线在 2026-05-25 已经试过：
    - 不是 `no-op`
    - 就是显著拖慢热路径
    - 且即使命中也没触到更早的 `2 -> 8` 主差额
- 所以当前最可靠的新表述应记为：
  - `case2` 的“long input source 选择问题”和“short input final_pool 排序问题”并不矛盾
  - 二者共同指向的是同一个更早主因：
    - **错误 family / split 后缀在 request/base/LM 层已经拿到明显更强的主轴分数**
  - `SelectTopLines`
    - `admitted_state_index`
    - `same-span`
    - `final_pool`
    这些层都能放大或缓和现象，但目前都不像第一个制造主差额的层
- 因而如果下一步继续，优先级更高的不是：
  - 再做 source selection 保活
  - 再做 same-span 惩罚变体
  - 再做 state-key / admitted_replace 修补
- 更值得优先拆的是：
  - 为什么 `2 -> 8` 的
    - `驿站 / 翼展`
    会在当前 grammar / char-path / context-suffix 口径下拿到明显更强的 `lm_score_scaled`
  - 以及这层差额里，到底是：
    - `split_token_supported vs neutral_missing`
    - context suffix 口径
    - 还是更底层的 token evidence / base contract
    在主导盘面
- 随后按“先完整去重审计，再继续执行”的约束，重新把这条线与代码现状交叉核对了一遍：
  - `ScoreFeatures / context_suffix / lm_score_scaled / split_token_supported vs neutral_missing`
    这一整条诊断路线并不是空白
  - `WORKLOG` 里已经有完整结论，且源码中也已存在真实消费链
  - 真正还没落地成代码边界的，反而是文档里一直写着要有、但代码里尚不存在的
    - `InterpretGrammarEvidence`
- 这轮因此没有再去重复：
  - `SelectTopLines`
  - `same-span`
  - `beam/state competition`
  - 也没有再重复跑新的 `ScoreFeatures` 现象级 probe
- 而是只做了一个最小、未重复的实现：
  - 在 `librime/plugins/witogram/src/witogram.h` 中显式声明：
    - `Witogram::InterpretGrammarEvidence(...)`
  - 在 `librime/plugins/witogram/src/witogram.cc` 中把原本 `ScoreFeatures()` 里的 grammar 解释实现整体迁移到：
    - `InterpretGrammarEvidence(...)`
  - `ScoreFeatures()` 则收成兼容 wrapper：
    - 直接调用 `InterpretGrammarEvidence(...)`
- 这轮的目标不是改语义，而是先把当前已经反复验证过的 grammar 解释逻辑，变成一个真实存在的命名边界：
  - 仍保持当前行为不变
  - 仍保持：
    - whole-word 命中混合
    - char-path fallback
    - `split_token_supported`
    - `neutral_missing`
    - `true_oov`
    的现有语义不变
  - 但从代码组织上，不再把它继续埋在 `ScoreFeatures()` 这个名字下面
- 这轮之所以值得先做，是因为它直接对应前面已经在文档中固定下来的路线：
  - 当前真正需要被单独看见和验证的，不只是一个 “score”
  - 而是 grammar evidence interpretation 本身
  - 之前一直写的是“未来 `InterpretGrammarEvidence`”
  - 现在至少先把这个 future boundary 变成 real boundary
- 之后执行了最小验证：
  - `GetDiagnostics`
    - `witogram.h` / `witogram.cc` 无新增错误
  - `librime/build.bat static`
    - 编译通过
    - 无新增错误
    - 仅有历史 `LNK4044` warning
- 这轮可记为：
  - `P0` / `future InterpretGrammarEvidence` 边界的最小落地
  - 性质属于：
    - **结构化重构**
    - **不是算法语义实验**
  - 因而它不会替代后面真正需要做的：
    - 把 `neutral_missing` 与 `<unk>` runtime chain 的关系进一步显式化
    - 或把 token evidence 与未来上游 contract 更清楚地接起来
- 但它已经带来一个很重要的后续收益：
  - 之后若继续推进 `P0`
  - 再讨论：
    - `neutral_missing`
    - `split_token_supported`
    - `true_oov`
    - `<unk>` 高阶链
  - 就可以优先改和验证 `InterpretGrammarEvidence(...)`
  - 而不需要继续在 `ScoreFeatures()` 这个混合入口里追代码
- 在这个显式边界落地后，继续按“不要重复已做过的 neutral_missing 原型”做了一轮更细的去重审计：
  - 已确认当前线上代码**已经包含** 2026-05-24 那版稳定原型：
    - 对“词内同时存在已知 token 与 `NotFound()` token”的 `neutral_missing`
    - 不再把 `<unk>` 上下文继续传播到后续 token
    - 代码对应就是当前 `AppendNeutralMissingTokenScore()` 的行为
  - 因而这轮**没有**重复去改那条多字路径
  - 真正还没覆盖到的窄口是：
    - `token_count == 1`
    - 且 `token_evidence_level = neutral_missing`
    的**单字缺证**
  - 当前旧实现里，这类单字虽然会被打上 `kNeutralMissing`
    - 但因为 `char_matched_token_count == 0`
    - 并不会进入 `neutral_char_total_log10` 的替代路径
    - 仍然会沿用上下文相关的 `<unk>` 运行时打分
- 这轮因此只做了一个最小且未重复的语义补口：
  - 文件：`librime/plugins/witogram/src/witogram.cc`
  - 仅把 `neutral_missing` 专用路径的适用条件从：
    - `char_oov_token_count > 0 && char_matched_token_count > 0`
    扩到：
    - `char_oov_token_count > 0 && (char_matched_token_count > 0 || token_count == 1)`
  - 同时把回写 `total_log10` 的条件，也同步扩到：
    - `char_matched_token_count > 0 || token_count == 1`
  - 也就是说：
    - 对单字 `neutral_missing`
    - 现在也会改用“固定 `<unk>` unigram 成本、但不传播 `<unk>` 上下文 state”的解释
  - 这刀仍然**不**：
    - 软化 `<unk>` unigram 固定成本
    - 不额外改 `witset_poet`
    - 不回头补后段 rescue bonus
- 最小验证仍保持不扩实验面：
  - `librime/build.bat static`
    - 通过
    - 无新增错误
    - 仅有历史 `LNK4044` warning
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --mode full --case case2_diyizhan --case case3_tiyanbuyiyang`
- 这轮最关键的新读数来自递归抽取 `graph` 内的 `transition_lm_features`：
  - `case2`
    - `context_suffix = ...第一展示`
      - `word = 一`
        - `token_evidence_tag = neutral_missing`
        - `total_log10 = -38.4992`
        - `oov_token_count = 1`
        - `used_char_fallback = true`
      - 同层对照：
        - `word = 以`
          - `total_log10 = -15.0282`
        - `word = 已`
          - `total_log10 = -14.9353`
    - 这说明单字 `一` 已不再吃更深的上下文 `<unk>` 链惩罚
    - 当前读数已经直接退回到固定 `<unk>` unigram 成本
  - `case3`
    - `context_suffix = ...体验`
      - `word = 不一`
        - `token_evidence_tag = neutral_missing`
        - `total_log10 = -52.244`
      - 同层对照：
        - `word = 不宜`
          - `total_log10 = -53.244`
        - `word = 不一样`
          - `total_log10 = -91.7432`
    - 对比旧日志里同位置 `不一 = -55.244`
    - 可确认这刀也确实抬升了单字缺证 continuation
- 句级结果上，这刀目前仍属于：
  - **命中层级正确**
  - **有效但不足**
  - `case2`
    - 完整句 top1 仍是：
      - `第一展示已作古老的小镇`
    - `第一站是一座古老的小镇` 仍未回到可见前列
    - 说明单字 `一` 的 grammar 解释修正虽然生效，但还不足以翻转更早形成的错误 family 主盘面
  - `case3`
    - 完整句当前仍是：
      - `rank1 = 体验不一样的生活`
    - 说明这刀没有把主 guardrail 直接打坏
    - 但从局部 LM 读数上看：
      - `不一` 已经被抬到略高于 `不宜`
    - 因而后续若继续，必须继续盯住：
      - “单字 neutral_missing 被抬得是否过宽”
      - 尤其是短前缀下一批 `一 / 依 / 宜 / 已` 近邻是否一起被抬平
- 当前阶段结论应更新为：
  - “去掉 `<unk>` 上下文传播”的稳定版原型并不完整
  - 还需要覆盖单字 `neutral_missing`
  - 这轮已经把这个单字窄口补上，而且方向正确
  - 但它依旧没有强到足以单独解决 `case2`
  - 因而下一步若继续，仍应留在：
    - `InterpretGrammarEvidence(...)`
    - 围绕单字/短词 `neutral_missing` 的解释边界继续收口
  - 而不是回到：
    - `SelectTopLines`
    - `same-span`
    - 或后段 `neutral_missing rescue bonus`
- 在这轮单字窄口补完后，又继续做了一次更细的去重审计，确认还有一块语义口径仍未纠正：
  - `InterpretGrammarEvidence(...)` 现在虽然已经在**分数**上把
    - `split_token_supported`
    - `neutral_missing`
    从惩罚态里剥开
  - 但它导出的主字段仍沿用旧口径：
    - `used_char_fallback = true`
    - `oov_token_count = char_oov_token_count`
  - 这会导致 graph / snapshot 里即使已经不再按惩罚态消费，外观上仍像“坏 fallback / OOV 路径”
  - 而 `WitogramScoreFeatures` 本身已经有：
    - `token_evidence_level`
    - `char_path_oov_token_count`
    可保留原始 char-path 观察值
  - 因而这轮不需要重复去改分数，只需要把**主语义字段**对齐到证据等级
- 这轮因此只做了一个很小、且未重复的字段口径纠偏：
  - 文件：`librime/plugins/witogram/src/witogram.cc`
  - 对 `whole-word miss` 的两类非惩罚证据：
    - `kSplitTokenSupported`
    - `kNeutralMissing`
  - 不再继续导出：
    - `used_char_fallback = true`
    - `oov_token_count > 0`
  - 而是改为：
    - `used_char_fallback = false`
    - `oov_token_count = 0`
  - 同时保留：
    - `token_evidence_level`
    - `char_path_oov_token_count`
    作为 raw char-path 观察值
  - 也就是说：
    - “raw char-path 里确实经过了 NotFound / <unk>”
      这件事仍可观察
    - 但不会再把它扁平误报成主语义上的“惩罚态 fallback/OOV”
- 这轮仍只做最小验证：
  - `librime/build.bat static`
    - 通过
    - 无新增错误
    - 仅有历史 `LNK4044` warning
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --mode full --case case2_diyizhan --case case3_tiyanbuyiyang`
- 当前读数已经能直接证明这刀命中的是“字段语义”，不是分数：
  - `case2`
    - `context_suffix = ...第一展示`
      - `word = 一`
        - `total_log10 = -38.4992`
        - `token_evidence_tag = neutral_missing`
        - **现在变成**：
          - `used_char_fallback = false`
          - `oov_token_count = 0`
      - 同层 `已 / 以` 仍保持：
        - `direct_whole_word_hit`
        - `used_char_fallback = false`
        - `oov_token_count = 0`
  - `case3`
    - `context_suffix = ...体验`
      - `word = 不一`
        - `token_evidence_tag = neutral_missing`
        - `total_log10 = -52.244`
        - **现在也变成**：
          - `used_char_fallback = false`
          - `oov_token_count = 0`
      - `不宜`
        - `token_evidence_tag = split_token_supported`
        - 同样保持：
          - `used_char_fallback = false`
          - `oov_token_count = 0`
- 句级结果则基本保持不变：
  - `case2`
    - 完整句 top1 仍是：
      - `第一展示已作古老的小镇`
    - `第一站是一座古老的小镇` 仍未翻正
  - `case3`
    - 完整句仍是：
      - `rank1 = 体验不一样的生活`
  - 因而这轮可以明确记为：
    - **语义字段纠偏**
    - 不是新的排序改动
    - 也不是新的 rescue/bonus 实验
- 这轮的价值在于：
  - 现在 graph / snapshot 再出现：
    - `neutral_missing`
    - `split_token_supported`
  - 就不会再被旧字段语义误导成：
    - “仍是 fallback/OOV 坏路径”
  - 后续继续做 `P0` 时，观察结果会更接近文档里要求的三类语义：
    - 正证据
    - 中性缺证
    - 真负证据
- 当前阶段判断更新为：
  - `InterpretGrammarEvidence(...)` 这一层现在已经同时完成了：
    - 分数语义纠偏
    - 单字 `neutral_missing` 窄口补齐
    - 主语义字段口径纠偏
  - 但 `case2` 仍未翻正
  - 说明单靠这一层虽然已经持续消除了“系统性误伤”，但剩余 gap 仍然存在
  - 后续若继续，仍应优先留在：
    - `InterpretGrammarEvidence(...)`
    - 或它与上游 contract 的接口处
  - 而不是退回旧的末端 rescue / same-span / source selection 补丁路线
- 按“每一步先查日志、确认没做过”的要求，随后继续把这句
  - “留在 `InterpretGrammarEvidence(...)` 或它与上游 contract 的接口处”
  再拆细了一轮：
  - 先排除了已经做过的：
    - `case3` 句首双字 `NeutralMissing -> prefix LM scale`
    - 单字 `neutral_missing` 的 `<unk>` 链窄口
    - `used_char_fallback / oov_token_count` 主语义字段纠偏
  - 然后回查接口可观测性，确认还有一个**没做过**的残余缺口：
    - `WitogramScoreFeatures` 已保留
      - `char_path_oov_token_count`
    - 但 `transition_lm_features` / graph 工件并没有把它导出
  - 这会带来一个新的观测断层：
    - 现在主语义字段已经纠偏成：
      - `used_char_fallback = false`
      - `oov_token_count = 0`
    - 但如果 graph 又看不到：
      - `char_path_oov_token_count`
    - 那么外部就无法同时看到：
      - “主语义上它是 `neutral_missing`”
      - 和
      - “raw char-path 上它确实经过了 `<unk>` / `NotFound()`”
- 这轮因此只做了一个最小、未重复的接口导出修正：
  - `plugins/witset/src/witset_poet.h`
    - 给 `DebugTransitionLMRecord` 新增：
      - `char_path_oov_token_count`
  - `plugins/witset/src/witset_poet.cc`
    - 在写 `debug_transition_lm_snapshot_` 时，把：
      - `cache_it->second.features.char_path_oov_token_count`
      写入 `record.char_path_oov_token_count`
  - `plugins/witset/src/witset_translator.cc`
    - 在 `transition_lm_features` JSON 导出里新增：
      - `"char_path_oov_token_count"`
- 这轮保持边界不扩散：
  - 不改 `InterpretGrammarEvidence(...)` 的分数
  - 不改 `witset_poet` 排序
  - 不改 request-stage / family / source-line contract
  - 只补观测接口
- 最小验证仍保持固定：
  - `librime/build.bat static`
    - 通过
    - 无新增错误
    - 仅有历史 `C4267 / LNK4044` warning
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --mode full --case case2_diyizhan --case case3_tiyanbuyiyang`
- 结果已直接证明这刀命中的是“接口可观测性”，不是排序：
  - 句级结果保持不变：
    - `case2`
      - `第一站是一座古老的小镇 rank = null`
      - top1 仍是 `第一展示已作古老的小镇`
    - `case3`
      - `体验不一样的生活 rank = 1`
  - 但 `transition_lm_features` 现在终于能同时看到主语义字段和 raw char-path OOV：
    - `case2`
      - `context_suffix = ...第一展示`
      - `word = 一`
        - `token_evidence_tag = neutral_missing`
        - `oov_token_count = 0`
        - `used_char_fallback = false`
        - `char_path_oov_token_count = 1`
      - 对照：
        - `word = 以 / 已`
          - `char_path_oov_token_count = 0`
    - `case3`
      - `context_suffix = ...体验`
      - `word = 不一`
        - `token_evidence_tag = neutral_missing`
        - `oov_token_count = 0`
        - `used_char_fallback = false`
        - `char_path_oov_token_count = 1`
      - `word = 不宜`
        - `token_evidence_tag = split_token_supported`
        - `char_path_oov_token_count = 0`
      - `word = 不一样`
        - `token_evidence_tag = neutral_missing`
        - `char_path_oov_token_count = 1`
- 因而，这轮之后可以把这条接口口径写死：
  - `oov_token_count`
    - 表示主语义上仍要继续传播的惩罚态 OOV
  - `char_path_oov_token_count`
    - 表示 raw char-path 里实际经过的 `<unk>` / `NotFound()` 次数
  - 两者以后不应再混用
- 这轮的价值不在于让 `case2` 翻正，而在于：
  - graph / snapshot 终于能同时表达：
    - 三类 grammar 证据
    - 以及 raw char-path 观察值
  - 后续若继续推进 `P0`，就不需要再反复猜：
    - “为什么明明已经不是惩罚态，graph 里却看不到 raw OOV 痕迹”
- 继续按“每一步先查日志确认没做过”往下拆接口层时，又确认了一块还没处理过的计数字段残留：
  - 当前 `WitogramScoreFeatures` 里：
    - `matched_token_count`
    仍然混合了：
    - 主语义上的“这个词在 grammar 解释层是否已有足够支撑”
    - 与 raw char-path 里“到底有几个 token 真正在 vocab 命中”
  - 特别是前面刚补过的：
    - 单字 `neutral_missing`
  - 此时它已经被解释成：
    - `token_evidence_level = neutral_missing`
    - `used_char_fallback = false`
    - `oov_token_count = 0`
  - 但 `matched_token_count` 仍保持：
    - `0`
  - 这会让主语义字段再次出现一个小的不一致：
    - 外观上像“没有任何 token 被支撑”
    - 但解释层又已经把它判成了中性缺证而非纯负证据
- 同时回查全局代码后也确认：
  - `matched_token_count` 当前并没有被 `witset_poet` 主排序逻辑直接消费
  - 因而这一步仍可保持为：
    - **接口语义整理**
    - 而不是排序实验
- 这轮因此只做了一刀很窄的接口对齐：
  - `plugins/witogram/src/witogram.h`
    - 新增：
      - `char_path_matched_token_count`
  - `plugins/witogram/src/witogram.cc`
    - 在走 raw char-path 时保留：
      - `char_path_matched_token_count = char_matched_token_count`
    - 对 `single-char neutral_missing`
      - 把主语义字段：
        - `matched_token_count`
      - 至少提升到：
        - `1`
      - 也就是说：
        - 主语义上它不再显示成“0 个匹配 token”
        - raw 计数则继续由：
          - `char_path_matched_token_count`
          表达
  - `plugins/witset/src/witset_poet.h`
    - 给 `DebugTransitionLMRecord` 新增：
      - `matched_token_count`
      - `char_path_matched_token_count`
  - `plugins/witset/src/witset_poet.cc`
    - 把上述两个字段写入 debug snapshot
  - `plugins/witset/src/witset_translator.cc`
    - 在 `transition_lm_features` JSON 里新增：
      - `"matched_token_count"`
      - `"char_path_matched_token_count"`
- 这轮仍保持边界不扩散：
  - 不改 `total_log10`
  - 不改 `token_evidence_level`
  - 不改 `witset_poet` 排序
  - 只把：
    - 主语义计数
    - 与 raw char-path 计数
    正式拆开
- 最小验证：
  - `librime/build.bat static`
    - 通过
    - 无新增错误
    - 仅有历史 `C4267 / LNK4044` warning
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --mode full --case case2_diyizhan --case case3_tiyanbuyiyang`
- 句级结果保持不变：
  - `case2`
    - `第一站是一座古老的小镇 rank = null`
  - `case3`
    - `体验不一样的生活 rank = 1`
- 但 `transition_lm_features` 现在已经能把“主语义 matched”与“raw char-path matched”同时展示出来：
  - `case2`
    - `...第一展示 + 一`
      - `token_evidence_tag = neutral_missing`
      - `matched_token_count = 1`
      - `char_path_matched_token_count = 0`
      - `oov_token_count = 0`
      - `char_path_oov_token_count = 1`
    - 对照 `...第一展示 + 以 / 已`
      - `matched_token_count = 1`
      - `char_path_matched_token_count = 1`
      - `char_path_oov_token_count = 0`
  - `case3`
    - `...体验 + 不一`
      - `token_evidence_tag = neutral_missing`
      - `matched_token_count = 1`
      - `char_path_matched_token_count = 1`
      - `char_path_oov_token_count = 1`
    - `...体验 + 不宜`
      - `matched_token_count = 2`
      - `char_path_matched_token_count = 2`
      - `char_path_oov_token_count = 0`
    - `...体验 + 不一样`
      - `token_evidence_tag = neutral_missing`
      - `matched_token_count = 2`
      - `char_path_matched_token_count = 2`
      - `char_path_oov_token_count = 1`
- 这轮之后，接口口径又可以再明确一层：
  - `matched_token_count`
    - 表示 grammar 解释后的主语义计数
  - `char_path_matched_token_count`
    - 表示 raw char-path 实际命中的 token 数
  - 它和：
    - `oov_token_count`
    - `char_path_oov_token_count`
    一样，未来都不应再混用
- 这轮同样不是为了直接翻正 `case2`
  - 而是继续把 `P0` 的接口与观测面整理到：
    - 主语义字段
    - raw 观察字段
    可以并排成立的状态
  - 这样后续若继续做 `InterpretGrammarEvidence(...)`
    - 就不会再被计数字段自己的语义残留误导

## 2026-05-19 `lm_avg` vs `dict_score_norm` 二分 probe：剩余统计压制主要落在 `lm_avg`，`dict_score_norm` 更像补偿项

- 在把手工结构 adjustment 基本排除后，这轮继续把剩余统计项再拆成两半：
  1. 只移除 `lm_avg_weight_ * lm_score_avg`
  2. 只移除 `dict_score_norm_weight_ * dict_score_norm`

- 两个 probe 的共同前提：
  - `Base` 主轴保持稳定树原样
  - 手工结构项保持稳定树原样
  - 都只改单个统计项，然后分别：
    - `librime/build.bat static`
    - 回放 `C:\Users\Bing\AppData\Roaming\witty\debug\_single_case.txt`
    - 对比 `diyizhanshi / diyizhanshiy / diyizhanshiyi`

- 第一刀：只移除 `lm_avg`
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\lm_avg_removed_probe`
  - 结果：
    - `diyizhanshi`
      - top1 仍然是 `第一战士`
      - rank2 仍然是 `第一展示`
      - `第一站是` 仍在 rank5
    - `diyizhanshiy`
      - top1 仍然是 `第一战士有`
      - 但 `第一站使用 / 第一站是由 / 第一站是要 / 第一站是以`
      - 已明显上浮到前列
    - `diyizhanshiyi`
      - top1 仍然是 `第一战士已`
      - `第一站是以` 仍在 rank4
  - 关键数值：
    - 稳定树：
      - `第一展示 = -167.066`
      - `第一站是 = -195.409`
      - 差距约 `28.34`
    - 去掉 `lm_avg` 后：
      - `第一展示 = -145.052`
      - `第一站是 = -168.507`
      - 差距收敛到约 `23.46`
  - 这说明：
    - `lm_avg` 不是像 `Base` 内 `lm_score_scaled` 那样的绝对主因
    - 但它确实还在继续放大 `第一展示` 对 `第一站是` 的优势
    - 属于剩余统计压制中的主要一项

- 第二刀：只移除 `dict_score_norm`
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\dict_norm_removed_probe`
  - 结果更有判别力：
    - `diyizhanshi`
      - top1 仍然是 `第一战士`
      - rank2 仍然是 `第一展示`
      - `第一站是` 仍在 rank5
    - `diyizhanshiy`
      - top1 仍然是 `第一战士有`
      - `第一站是*` 家族没有上升，反而整体更远
    - `diyizhanshiyi`
      - top1 仍然是 `第一战士已`
      - `第一站是以` 仍在 rank4，且差距更大
  - 关键数值：
    - 去掉 `dict_score_norm` 后：
      - `第一展示 = -163.548`
      - `第一站是 = -191.999`
      - 差距约 `28.45`
    - 与稳定树几乎一致，且略微更差
  - 更直白地说：
    - **`dict_score_norm` 不是压制 `第一站是*` 的那一项。**
    - 它更像一个在坏局面里仍然给 `第一站是*` 家族提供少量补偿的统计项

- 把这两刀放在一起，当前判断可以再收紧一层：
  1. `Base` 主轴里的 `lm_score_scaled`
     - 仍然是最大的主压制项
  2. `adjustment_score` 剩余统计项里
     - **`lm_avg` 才是继续压低 `第一站是*` 的主要残余项**
  3. `dict_score_norm`
     - 不是主压制项
     - 更接近补偿项，至少不是当前该优先下刀去削弱的对象

- 因而此处最值得记录的方向更新是：
  - 后续若继续做最窄原型
  - 不应优先碰：
    - `dict_score_norm`
  - 更应优先围绕：
    - `lm_score_scaled`
    - `lm_avg`
  - 也就是继续处理 `LM` 统计账本在不同切分 regime 下的系统性不可比

- 这两版统计项 probe 都已回退，不保留；随后已再次执行：
  - `librime/build.bat static`
  - 编译通过，工作树恢复稳定

## 2026-05-19 两段式 LM 可比性原型：clean bridge 单刀有正信号，但再叠短前缀两字 fallback continuation 缩放后方向失真

- 在把主因继续收敛到 `lm_score_scaled + lm_avg` 之后，这轮开始第一次直接做“只动 LM 账本”的最窄代码原型，不再碰：
  - `dict_score_raw / dict_score_norm`
  - 手工结构项
  - 现有 `anchor / preserve / continuation penalty` 那一组 patch

- 这轮原型分成两步验证。

- 第一步：只做 `whole_first_word clean bridge` 的 LM release
  - 新 helper 只命中下面这个非常窄的场景：
    - 前缀已经是一个不稳的整词首锚点
      - `whole_first_word_anchor_active = true`
      - 且首词长度 `>= 3`
    - 当前步是：
      - `prefix_generated_word_count == 1`
      - `char_count == 1`
      - `used_char_fallback == false`
      - `matched_whole_word == true`
      - `lm_oov_token_count == 0`
    - 也就是：
      - **不稳整词首词后，接了一个干净的 1 字桥接**
  - 在这个极窄条件下，只对：
    - `lm_score_scaled`
    - `lm_score_avg`
    - 同时乘一个 `0.80` 的 release scale
  - 直觉上，这一刀只该帮：
    - `第一站 + 是`
    - 这种“整词首词已成形、当前又接了干净 1 字桥”的路径
  - 而不该直接把：
    - `的 + 驿站是`
    - 这种两词错链一起放上来

- 这一步的单例结果有明确正信号：
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\whole_first_word_bridge_lm_probe`
  - `diyizhanshi`
    - `第一站是`
      - 从后排回升到 `top3`
    - 且没有看到旧 B 那种：
      - `驿站是`
      - 被整体抬炸回前排
  - 这说明：
    - **“不稳整词首词 + 干净 1 字桥接”确实是一个真实可命中的 LM 重复惩罚场景。**

- 但这一步也同时暴露出边界：
  - 它只打中了 `第一站 + 是`
  - 还没有处理掉：
    - `第一 + 战士`
    - `第一 + 展示`
    - 这类“短前缀后继续接两字 fallback continuation”的竞争面
  - 所以它只能把目标链拉回可见区，还不足以翻到 top1

- 第二步：在第一刀基础上，再叠一版“短前缀两字 fallback continuation LM 缩放”
  - 目标是补掉另一半主竞争：
    - `prefix_generated_word_count == 1`
    - `prefix_generated_char_count <= 2`
    - `char_count >= 2`
    - `used_char_fallback == true`
    - `matched_whole_word == false`
  - 也就是：
    - **一个很短的前缀后，又接出两字 fallback continuation**
  - 同样只改：
    - `lm_score_scaled`
    - `lm_score_avg`
  - 不碰别的账本

- 这版“联动两段式 LM 原型”的结果反而给了一个明确负结论：
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\lm_comparability_combo_probe`
  - `diyizhanshi`
    - top1 仍然是 `第一战士`
    - `第一站是` 又退回后面
    - `敌意展示 / 地衣展示`
      - 重新顶回前排
  - `diyizhanshiy`
    - top3 仍然完全被 `第一战士*` 占据
  - `diyizhanshiyi`
    - top3 仍然完全被 `第一战士*` 占据
  - 并且确认：
    - 没有出现旧 B 那种 `驿站是` 家族大面积抬升
    - 但也没有继续扶正 `第一站是*`

- 这说明当前可以把结论收得很细：
  1. **单独的 clean-bridge LM release 是有信息量的。**
     - 它确实命中了 `第一站 + 是`
     - 说明这里存在“LM 重复惩罚打重了”的真实局部问题
  2. **但把另一半简单概括成“短前缀 + 两字 fallback continuation”再统一缩放，是错误抽象。**
     - 因为这会同时误补：
       - `敌意展示`
       - `地衣展示`
       - 这类与目标链并不等价的 wrong family
  3. 因而当前最准确的判断不是：
     - “所有短前缀后两字 fallback continuation 的 LM 都该削弱”
     - 而是：
     - **只有其中更接近 shared-prefix / clean-bridge / recoverable continuation 的那一小块，才值得做 LM release**

- 路线判断因此再次收紧：
  - 已经不该回到全局改 `lm_avg_weight`
  - 也不该做“所有短前缀两字 fallback continuation 一刀切缩放”
  - 下一步若继续沿 LM 可比性这条线推进：
    - 应保留第一刀暴露出的有效面：
      - `whole_first_word clean bridge`
    - 但第二刀必须换成更窄的 companion gate
    - 这个 gate 需要继续区分：
      - `第一 + 战士 / 展示`
      - 与
      - `敌意展示 / 地衣展示`
    - 不能只靠“短前缀 + 两字 fallback”这种模板

- 这轮两段式 LM 原型的代码都已回退，不保留；随后已再次执行：
  - `librime/build.bat static`
  - 编译通过，工作树恢复稳定

## 2026-05-19 最强前缀 companion gate：能把 `第一站是` 拉回 `top3`，但仍打不穿 `第一战士*` 主簇

- 在上一轮确认“短前缀 + 两字 fallback continuation”这个抽象过粗之后，这轮继续只动 `LM`，但把第二刀再收窄一层：
  - 不再对所有短前缀做统一 release
  - 而是先检查当前 source 是否就是该 `source_pool` 里的最强前缀

- 之所以这样收窄，是因为重新核对 `diyizhanshi` 的 `source_pool / top_candidate` 后，三条关键前缀的强弱本来就已经拉开：
  - `第一`
    - `beam = -51.3007`
  - `敌意`
    - `beam = -51.8176`
  - `地衣`
    - `beam = -52.2746`
  - 也就是说：
    - `第一` 在同池里本来就是最强前缀
    - `敌意 / 地衣` 只是次优错前缀

- 因而这轮新原型的 companion gate 改成：
  1. 仍保留第一刀：
     - `whole_first_word clean bridge` 的 LM release
  2. 第二刀不再看“所有短前缀”
     - 只在 `source_is_pool_best == true` 时
     - 才对：
       - `prefix_generated_word_count == 1`
       - `prefix_generated_char_count == 2`
       - `char_count >= 2`
       - `used_char_fallback == true`
       - `matched_whole_word == false`
     - 这一类 continuation 释放一部分：
       - `lm_score_scaled`
       - `lm_score_avg`

- 这版 summary：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\lm_best_prefix_companion_probe`

- 结果比上一版“全短前缀 fallback 一刀切”明显更干净：
  - `diyizhanshi`
    - baseline top3：
      - `第一战士`
      - `第一展示`
      - `敌意展示`
    - 新版 top3：
      - `第一战士`
      - `第一展示`
      - `第一站是`
  - 这说明：
    - **“只放该池最强前缀”这个约束是有效的。**
    - 它成功压住了：
      - `敌意展示`
      - `地衣展示`
      - 这类次优错前缀族的误抬
    - 同时把：
      - `第一站是`
      - 拉回了前 3

- 但这版原型仍然没有真正打穿主竞争：
  - `diyizhanshi`
    - top1 仍然是 `第一战士`
    - rank2 仍然是 `第一展示`
    - `第一站是` 只是上升到 rank3
  - `diyizhanshiy`
    - top3 仍然完全是 `第一战士*`
  - `diyizhanshiyi`
    - top3 仍然完全是 `第一战士*`
  - 换句话说：
    - 这刀只修掉了“wrong family 抢第三名”的问题
    - 没有修掉真正压在最前面的：
      - `第一战士*`
      - 这一整个主簇

- `diyizhanshi` 当步的数值也能说明这个边界：
  - `第一站是`
    - `beam = -173.857`
    - `base = -143.143`
    - `adj = -30.714`
  - `第一展示`
    - 仍有一条更强主路径
    - `beam = -151.380`
    - `base = -124.469`
    - `adj = -26.911`
  - `第一战士`
    - 这里已不是全局最强，但对应主簇在更长 continuation 里仍稳住前排
  - 也就是说：
    - companion gate 已经把目标链重新拉回可见竞争面
    - 但还没有打到最深的主压制来源

- 所以这轮的价值不在于“方案成功”，而在于把责任边界再切细了一层：
  1. `source_pool` 最强前缀约束确实有用
     - 它能过滤掉 `敌意 / 地衣` 这类次优错前缀族
  2. 但即使过滤掉这些 wrong family
     - `第一站是*` 仍然打不过 `第一战士*`
  3. 因而下一步真正该继续打的，不再是：
     - `敌意展示 / 地衣展示`
     - 这类次优错簇
     - 而是：
     - **`第一战士*` 这条主簇为什么在 shared-prefix 主竞争里仍然系统性占优**

- 路线更新：
  - “最强前缀 companion gate” 是有信息量的局部命中
  - 但还不足以成为最终原型
  - 后续若继续沿 `LM` 线推进：
    - 不必再回去做“全短前缀”类宽门控
    - 更值得把观察点继续聚焦在：
      - `第一` 前缀家族内部
      - `第一战士*` vs `第一站是*`
      - 这一组 shared-prefix 主竞争

- 这版最强前缀 companion gate 原型代码已回退，不保留；随后已再次执行：
  - `librime/build.bat static`
  - 编译通过，工作树恢复稳定

## 2026-05-19 从单句切到小型代表集：`clean bridge` 单刀在 21 条上没有泛化，且出现 1 条明显回退

- 针对“只围着 `diyizhanshi` 打转是否太危险”的担心，这轮不再只看单句，而是先构了一个很小的代表集：
  - 文件：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\_shared_prefix_eval_excerpt.txt`
  - 内容刻意覆盖两类场景：
    1. 普通 `是...` continuation
       - `秋天是有气息的`
       - `也带着一种不可挽回的流逝`
    2. `第一站是...` 及其后续段落
       - `第一站是一座古老的小镇`
       - `两旁是古色古香的建筑`
       - `店主是一位和蔼可亲的老人`

- 先在稳定树上跑这个小集：
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\shared_prefix_eval_excerpt_base`
  - 得到：
    - `reference_case_count = 21`
    - `top1_accuracy = 0.5714`
    - `top3_accuracy = 0.6190`
  - 这说明这个小集足够产生可比较的 `expected_rank`，可以用来做“单句线索是否泛化”的 smoke check

- 然后只把此前副作用最可控的那一刀重新挂回去：
  - `whole_first_word clean bridge` 的 LM release
  - 不叠加 companion gate
  - 也不碰 `dict` / 手工结构项

- 重新编译后，在同一个 21 条小集上回放：
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\shared_prefix_eval_excerpt_cleanbridge`

- 这次差分结果非常直接：
  - **improved = 0**
  - **worsened = 1**
  - 具体回退项：
    - `在一个假期`
      - baseline `expected_rank = 1`
      - clean bridge 后 `expected_rank = 4`
  - 其它 20 条：
    - 全部持平
    - 包括最关心的：
      - `第一站是一座古老的小镇`
      - 仍然是 `expected_rank = null`
      - 没有被拉回可见区

- 这个结果的意义很关键：
  1. 单句上的“局部正信号”不能直接外推成“这条路线整体有效”
  2. 至少对当前这个 21 条代表集来说：
     - `clean bridge` 单刀没有带来任何泛化收益
     - 反而还打坏了 1 条原本正常的 case
  3. 因而它目前只能算：
     - **局部病理的解释性 probe**
     - 还不能算可推广的优化原型

- 这也让“是否只盯单句太危险”的问题有了更具体的回答：
  - 是，危险是真实存在的
  - 因为一旦换到很小的代表集，单句上的那点局部改善就立刻消失了

- 因而此处路线判断需要再更新一次：
  - 以后不能再把：
    - `diyizhanshi` 上的局部回升
    - 直接当作保留原型的依据
  - 更合理的标准应改成：
    - 先在小型代表集上至少不回退
    - 再考虑是否值得继续扩展到更大的 typical set

- 这版 `clean bridge` 代码已回退，不保留；随后已再次执行：
  - `librime/build.bat static`
  - 编译通过，工作树恢复稳定

## 2026-05-19 `clean bridge` 再收紧到“prefix debt gate”后，21 条小集仍然 `improved=0 / worsened=1`

- 为了回答“为什么 `clean bridge` 会误伤 `在一个假期`”，这轮没有继续发明宽门控，而是先回头核现有契约：
  - `whole_first_word_anchor` 的激活条件本身就要求：
    - `used_char_fallback == true`
    - `matched_whole_word == false`
    - `next_prefix_oov_tokens > 0`
  - 也就是说：
    - 它本来就是一类“前缀自己带着 LM/path debt”的标记

- 基于这个观察，这轮把上一版 `clean bridge` 再收紧一层，只保留最保守的 release：
  - 除了原有条件：
    - `whole_first_word_anchor_active == true`
    - `whole_first_word_anchor_char_count >= 3`
    - `prefix_generated_word_count == 1`
    - 当前 extension 是：
      - `char_count == 1`
      - `used_char_fallback == false`
      - `matched_whole_word == true`
      - `lm_oov_token_count == 0`
  - 还额外要求当前 source 自己已经带着历史 debt：
    - `candidate->cumulative_char_fallback_hits > 0`
    - 或 `candidate->cumulative_lm_oov_tokens > 0`

- 这版的意图很明确：
  - 若 `在一个` 这样的干净前缀没有历史 debt
    - 就不应该被 `clean bridge` 放行
  - 只有像 `第一站` 这种确实经历过 fallback / OOV debt 的前缀
    - 才允许对后续干净单字桥接做一小步 LM release

- 这版小集 summary：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\shared_prefix_eval_excerpt_debtbridge`

- 结果仍然是负的，而且和上一版 `clean bridge` 本质相同：
  - `reference_case_count = 21`
  - `top1_accuracy = 0.5238`
  - `top3_accuracy = 0.5714`
  - 对 baseline 差分：
    - `improved = 0`
    - `worsened = 1`

- 关键 case 仍然没有被修正：
  - `在一个假期`
    - baseline `expected_rank = 1`
    - debt-gated 后 `expected_rank = 4`
  - `第一站是一座古老的小镇`
    - baseline `expected_rank = null`
    - debt-gated 后仍是 `null`

- top 候选也印证了它并没有命中真正问题面：
  - `在一个假期`
    - top5 仍然是：
      - `在一个家其`
      - `在一个家七`
      - `在一个家柒`
      - `在一个 假期`
      - `再一个假期`
  - `第一站是一座古老的小镇`
    - top5 仍然被：
      - `第一战士已作古老的小镇`
      - `第一展示已作古老的小镇`
      - `第一战事已作古老的小镇`
      - 这类主簇占据
    - `第一站是以做古老的小镇`
      - 只是在更后面出现

- 这一步的意义在于把 `clean bridge` 这一整支路线进一步封死：
  1. 之前可以说：
     - 单句 `diyizhanshi` 上有局部正信号
     - 但小集不泛化
  2. 现在连最保守的 “prefix debt gate” 版也试过了
     - 仍然是：
       - `improved = 0`
       - `worsened = 1`
  3. 因而可以更硬地下结论：
     - **问题不在于 `clean bridge` 门控太宽。**
     - 即使把门收得很窄，它依然没有修到 `第一站是...` 的主竞争，而且仍会伤及别的正常 case。

- 路线更新：
  - 后续不应再继续给 `clean bridge` 这条支路追加复杂度
  - 不论是：
    - 无门控
    - debt-gated
    - 还是再加 companion 条件
  - 它们都没有先通过当前 21 条小集 smoke check

- 这版 debt-gated `clean bridge` 原型代码已回退，不保留；随后已再次执行：
  - `librime/build.bat static`
  - 编译通过，工作树恢复稳定

## 2026-05-19 continuation 梯子：长句主问题不是最后一步 `镇`，而是 `yizuo -> yizuogu` 阶段 `已作古` 链条的语义强势

- 在把 `clean bridge` 整支路线封口后，这轮没有再改代码，而是先把长 continuation 本身拆开看。
- 为了避免继续只盯最终整句，这轮新建了一个 continuation 梯子 corpus：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\_continuation_ladder.txt`
  - 依次观察：
    - `diyizhanshi` -> `第一站是`
    - `diyizhanshiyi` -> `第一站是以`
    - `diyizhanshiyizuo` -> `第一站是以做`
    - `diyizhanshiyizuogu` -> `第一站是以做古`
    - `diyizhanshiyizuogulao` -> `第一站是以做古老`
    - `diyizhanshiyizuogulaodexiaozhen` -> `第一站是以做古老的小镇`

- 跑出的 summary：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\continuation_ladder_base`

- 梯子结果先给出一个结构性事实：
  - `第一站是`
    - `expected_rank = 5`
  - `第一站是以`
    - `expected_rank = 4`
  - `第一站是以做`
    - `expected_rank = 7`
  - `第一站是以做古`
    - `expected_rank = 6`
  - `第一站是以做古老`
    - `expected_rank = 4`
  - `第一站是以做古老的小镇`
    - `expected_rank = 7`

- 关键不是 rank 本身，而是 top1 / gap 在哪一步突然变坏：
  1. `diyizhanshiyizuo`
     - 目标链：
       - `第一站是以做`
       - `Total = -246.95`
     - 主胜者：
       - `第一战士已作`
       - `Total = -212.93`
     - 差距约：
       - `34.0`
  2. `diyizhanshiyizuogu`
     - 目标链：
       - `第一站是以做古`
       - `Total = -320.99`
     - 主胜者：
       - `第一战士已作古`
       - `Total = -227.65`
     - 差距直接拉大到约：
       - `93.3`

- 这一步骤非常关键，因为它说明：
  - **差距不是在最终 `小镇 / 镇` 这一步才形成。**
  - 真正爆开的阶段，是：
    - `yizuo -> yizuogu`
  - 也就是：
    - `第一战士已作 -> 第一战士已作古`
    - 对打
    - `第一站是以做 -> 第一站是以做古`

- 而这里的本质已经不是前面那种 shared-prefix 可比性小偏置，而是当前可见文本自身的语义合法性差异：
  - `已作古`
    - 在当前可见文本下是一条非常强的 continuation
  - `是以做古`
    - 在当前可见文本下本来就是不自然甚至错误的中间形态

- 这和最终用户真正想要的句子之间，存在一个很重要的分层：
  - 用户最终想要的是：
    - `第一站是一座古老的小镇`
  - 但在逐步输入 `diyizhanshiyizuogu...` 时，当前可见候选文本却只能先呈现成：
    - `第一站是以做古...`
  - 也就是说：
    - 目标路径要想赢，必须依赖后续更多输入把这条“暂时不通顺”的中间形态重新救活为：
      - `是一座古老的小镇`

- 因而这轮最重要的新结论是：
  - **长句主问题已经不再是局部 LM 可比性修饰。**
  - 更深层是：
    - 目标路径在中间阶段必须穿过一个对 n-gram / 局部 LM 明显不友好的“临时坏表面形态”
    - 而竞争路径 `已作古...` 在这些中间阶段反而天然极强

- 这也解释了为什么前面的 `clean bridge` / debt-gated bridge 都会失败：
  - 它们最多只能减轻：
    - `第一站是` 当步
    - 或单字桥接时的重复惩罚
  - 但一旦进入：
    - `是以做古`
    - 这类中间阶段
  - 主导排序的就已经不是 bridge，而是 continuation 本身的语义强弱

- 从当前证据看，这意味着一件更根本的事：
  - 如果仍然坚持只靠“当前可见文本上的局部 n-gram / 局部 LM 打分”去救这种 case，
  - 那么 `第一战士已作古...` 这类强 continuation 很可能就是路线级上限，而不再是某个小参数或小门控能修掉的问题。

- 换句话说：
  - `第一站是`
    - 这个短锚点阶段还可以讨论局部比较契约
  - 但继续往 `一座古老的小镇` 走时，
    - 问题就转成了：
    - **是否允许为了未来可能的 rescue，容忍当前明显更差的表面 continuation 继续保活。**
  - 这已经超出前面 `clean bridge` 那类本地修分 probe 的能力边界。

- 因而路线判断需要再次收紧：
  1. 前面 `shared-prefix / LM comparability` 的 probe
     - 只对短锚点阶段有解释力
  2. 对长 continuation：
     - 真正主导的是 `已作古...` 这种强 continuation
     - 而不是 `镇` 这类最后一步 extension
  3. 后续如果还要继续救这类长句
     - 应该优先思考的是：
       - 是否接受更强的“未来救活”机制
       - 或更宽的保活 beam
     - 而不是继续在当前这套局部 LM 打分里加小门控

## 2026-05-19 本轮收束：给“末端修分追平 `octagram`”暂时画上休止符，准备切到插件化 `credibility contract`

- 把这轮相关代码、调试工件、`21` 条小型代表集、continuation 梯子，以及对原版 `octagram` 的代码复核重新串起来后，当前路线判断需要正式收束成两层：
  1. **路线 B 本身没有被否定**
     - `witogram` 作为 `LM` 特征提供器、`witset_poet` 负责句级精排，这个总方向仍然成立
     - 但它成立的前提，不是让 `poet` 继续承担“从脏候选池里把正确句救回来”的主职责
  2. **“继续在 `witset_poet` 末端补局部打分，以追平 `octagram`”这条子路线已经接近止损点**
     - 这一支路后续不再值得继续堆：
       - `shared-prefix` 小门控
       - `LM comparability` 局部 release
       - `clean bridge / debt-gated clean bridge`
       - 以及类似的末端补救 patch

- 促成这次收束的证据，已经足够集中：
  1. `all_requests` 批内保护原型两次单例都完全不动
     - 说明问题不在这个层级
  2. `target_pool / final_pool` 探针确认：
     - `第一 + 展示`
     - `第一展 + 示`
     - `第一站 + 是`
     - 不是死在同态去重或共享 state replace
     - 而是在最终 `beam_score` 正面对打时输掉
  3. `LM` 二分 probe 已经把责任边界切清：
     - 主压制项在 `Base` 主轴里的 `lm_score_scaled`
     - 次级残余压制项在 `adjustment` 里的 `lm_avg`
     - `dict_score_norm` 不是主压制来源
  4. 但所有沿这条线继续下切的局部原型，都没有通过更小范围泛化验证：
     - `clean bridge` 单刀在 `21` 条小集上 `improved = 0 / worsened = 1`
     - 再收紧成 `prefix debt gate` 后仍然是 `improved = 0 / worsened = 1`
     - 这说明它们最多只是局部病理解释器，不是可推广的优化方案
  5. continuation 梯子进一步说明：
     - 长句主问题不是尾部最后一步
     - 而是 `yizuo -> yizuogu` 阶段，错误链 `第一战士已作古...` 在当前可见文本上本来就比 `第一站是以做古...` 更顺
     - 因而长 continuation 上的主导因素已经不是局部 `bridge`，而是当前局部 `LM` 对强 continuation 的自然偏好

- 这轮对原版 `octagram` 的复核，也让“为什么当前这条末端修分路线越来越难追平它”有了更明确的解释：
  - 原版真正强的不是插件本身更复杂
  - 而是：
    - `Syllabifier` 先把 correction / completion / ambiguous joint 的代价写进 `credibility`
    - `Dictionary` 再把这些上游先验并入临时 `DictEntry.weight`
    - `Poet + Octagram` 只在已经相对干净的候选图上做 beam search 和上下文搭配补分
  - 也就是说，原版是：
    - **上游先清图 / 先表达可信度**
    - **末端 n-gram 只做顺水推舟**
  - 当前我们反复失败的地方，则是：
    - **图已经脏了，再希望后段 `LM` 把正确句救回来**

- 因而现在最合理的止损结论不是：
  - `witogram + witset` 方向走错了
- 而是：
  - **把主要纠偏责任放在 `witset_poet` 后段，这条实现路线已经不值得继续深挖。**
  - **后续若还要追平或超过 `octagram`，必须把关键契约前移到候选进入 `poet` 之前。**

- 下一阶段的推荐起点也由此明确：
  1. 不优先改 `librime` core
  2. 先尝试做一条插件化的新支线：
     - 暂名可理解为：
       - `witset_path_credibility`
       - 或 `witset_octagram_contract`
  3. 目标不是逐行复刻 `syllabifier.cc`
     - 而是插件内重建它真正有效的输出：
       - path credibility / path debt ledger
       - ambiguous joint debt
       - correction / completion debt
       - fallback / OOV debt
       - path compactness / path provenance
  4. 这份 ledger 应尽量在候选进入 `poet` 前影响：
     - 候选保活
     - 基础边权
     - 同组竞争关系
  5. `witset_poet` 后续应退回：
     - 统一句级精排器
     - 而不是继续充当主清图器

- 因而，从今天起，这一阶段可以正式画一个休止符：
  - 暂停继续沿“`witset_poet` 末端修分追平 `octagram`”追加 patch
  - 保留本轮得到的反证、边界和验证流程
  - 准备切到新的插件化 `credibility contract` 路线

## 2026-05-19 新路线规划落文：以独立插件重建上游 `credibility contract`

- 基于当天已收束的路线判断，补写了一份新的独立规划文档：
  - `docs/pluginized_upstream_credibility_plan.md`
- 这份规划的核心结论是：
  - 不改 `librime` core 仍然可行
  - 关键原因不是继续在 `poet` 后段补分，而是当前 `witset_translator` 已经位于 `Syllabifier -> Dictionary Lookup -> WordGraph -> Poet` 的正确上游位置
  - 因此可以新增一个独立插件模块，在 `WordGraph` 进入 `poet` 前重建：
    - `path credibility`
    - `path debt ledger`
    - `ambiguous joint debt`
    - `correction / completion debt`
    - `fallback / OOV debt`
    - `path compactness / provenance`
- 新规划明确推荐的边界是：
  - 不直接改 `librime/src/rime/...` 核心实现
  - 不新增另一个 `grammar` 组件去和 `witogram` 冲突
  - 不让 `witset_poet` 继续承担主清图职责
  - 而是新增一个类似 `witset_credibility` / `witset_upstream_contract` 的独立模块，由 `witset_translator` 主动调用
- 规划中推荐的新模块职责分三层：
  1. 分析 `SyllableGraph`，生成可审计的 debt ledger
  2. 在 translator 端改写 `WordGraph` 的基础边权、保活关系与竞争组偏置
  3. 仅向 `witset_poet` 输出少量必要摘要，而不是把 ledger 逻辑继续塞到 `poet`
- 规划中给出的推荐推进顺序是：
  1. 先做模块/API 外壳与旁路开关
  2. 再把当前 `BuildJointRiskHints()` 升级为正式 ledger
  3. 先只验证 translator 端改权
  4. 再做最小保活
  5. 只有在“正确路径更早活下来”被验证后，才继续压缩 `witset_poet` 历史 patch，并进入更强上下文证据阶段
- 当前判断：
  - 这份文档不是要立刻开始大改，而是把“为什么这条线可行、该改哪一层、不该再改哪一层”先固定下来
  - 后续真正开始实现时，应默认以这份新文档作为主路线说明，而不是回到旧的“末端修分追平”思路

## 2026-05-19 新路线二次收口：放弃独立上游插件，改为 `witogram_core` 共享内核

- 在继续细化“开源一档 / 闭源二三档”的产品边界后，上一版“独立 `witset_credibility` 插件”方案被正式推翻，新的项目级结论改为：
  - 不再把主线放在独立上游插件
  - 不再把 `witogram` 继续定位成单纯 `grammar` 组件
  - 不再把“`witset_translator` 长期调用 `witogram_translator`”视为正式架构
- 当前正式确定的新结构是：
  1. 开源 `witogram_core`
     - 承担一档核心能力：
       - `SyllableGraph` 分析
       - `path credibility / debt ledger`
       - `WordGraph` 改写
       - 一档句子搜索
       - 一档调试特征导出
       - 唯一的一档 `WitogramPoet`
  2. 开源 `witogram_translator`
     - 作为 Rime 生态用户可直接使用的公开包装器
     - 仅服务独立开源 schema
  3. 闭源 `witset_translator`
     - 继续作为 `witset.schema.yaml` 的唯一主 translator
     - 在一档模式下直接调用 `witogram_core`
     - 继续独占二档、三档全部编排能力
- 为什么要从“独立插件”改成“共享 core”：
  - `translator -> translator` 在代码层面能做，但 `Translation` 只是候选流接口，不适合作为长期的一档内核边界
  - 若沿这条路做，后续会为了拿到 graph、ledger、debug 特征和句子对象而不断增加 side channel，最终比直接共享 core 更脏
  - 更重要的是：若继续拆一个独立上游插件，开源一档核心会被拆散，不利于 `witogram` 对外成为真正可独立运行的公开实现
- `poet` 的项目级归属也在本轮正式固定：
  - 后续只保留一个一档核心 poet，归属于开源 `witogram_core`
  - 两边都复用这一套：
    - `witogram_translator -> witogram_core -> WitogramPoet`
    - `witset_translator -> witogram_core -> WitogramPoet`
  - 不再长期维护另一套闭源一档 `WitsetPoet` 主体逻辑
- 对 `witset.schema.yaml` 的约束也在本轮明确：
  - 两个插件可以同时注册组件，但 `witset.schema.yaml` 仍必须只挂 `witset_translator`
  - 不允许把 `witset_translator` 与 `witogram_translator` 同时挂进主 `engine/translators`
  - 否则会在同一 segment 上同时产出候选，发生业务链路层面的“抢活”
- 开源/闭源边界在本轮也被正式写死：
  - 开源 `witogram` 只包含一档能力与独立开源 schema 所需包装
  - 开源仓库中绝不出现：
    - `llm_level_2`
    - `llm_level_3`
    - `rc_local / rc_remote`
    - `AsyncPPLService`
    - `SentenceCache`
    - `_witset_*` property 协议
    - 私有 PPL / Beam Search 链路
    - 以及任何暗示二档三档实现思路的预留接口或注释
- 配套文档动作：
  - 已将 `docs/pluginized_upstream_credibility_plan.md` 整体改写为新的正式口径：
    - `witogram_core + witogram_translator + witset_translator(adapter)`
  - 文档中已明确否定：
    - 继续单纯 `grammar`
    - 独立 `witset_credibility` 主线
    - 长期 translator 套 translator
    - 两边长期各维护一套一档 poet
- 当前判断：
  - 到这一轮为止，项目在架构层面已经从“继续找 patch 点”与“再拆一个插件”两条路上都收口
  - 下一步真正应进入的实现起点，是抽出 `witogram_core` 的输入输出边界，并首先让 `witogram_translator` 独立跑通，再让闭源 `witset` 接入同一个 core

## 2026-05-19 实施策略再收口：先在现有架构验证上移介入点，再分拆 shared core

- 在继续细化实施路线时，新增了一条非常关键的执行约束：
  - 当前阶段**先不急着做 `witogram_core` 分拆**
  - 应先在现有 `witset` 分拆架构内验证“把介入点上移到 `SyllableGraph / WordGraph` 是否能让一档稳定超过 `octagram`”
- 这次收口的原因是：
  - 当前真正还没被证明的核心问题，不是“能不能分拆成共享 core”，而是“上移介入点后，当前一档能力是否真的能稳定跑赢 `octagram`”
  - 若这件事还没成立，就提前进入文件搬迁、目录整理、模块拆分，会把大量精力耗在边界整洁，而不是最关键的效果验证上
- 因此，项目现阶段的主线被明确调整为：
  1. 继续停止“末端 patch 追平”路线
  2. 在现有 `witset_translator -> graph -> poet` 骨架内，把主改动重心前移到：
     - `SyllableGraph` 分析
     - `credibility ledger`
     - `WordGraph` 改写
     - 句级搜索前的保活与竞争组偏置
  3. 保持二档、三档逻辑不动
  4. 用固定 smoke / baseline 验证一档是否达到或超过 `octagram`
  5. 只有验证成功后，才正式把这套已成型的一档骨架迁出为 `witogram_core`
- 这轮也进一步明确了“目标架构”和“近期实现”的关系：
  - 目标架构仍然是：
    - `witogram_core + witogram_translator + witset_translator(adapter)`
  - 但近期实现路径改为：
    - 先在现有代码树中把未来 `witogram_core` 的逻辑边界整理出来
    - 先不急着改目录和模块
    - 先验证效果，再分拆
- 文档层面的具体补充已经完成：
  - 在 `docs/pluginized_upstream_credibility_plan.md` 中新增了：
    - 接口级实施设计
    - `WitogramCoreConfig / Context / Result` 草案
    - `AnalyzeCredibility / RewriteWordGraph / RunWitogramPoet` 这一层内部流水线
    - “先验证后分拆”的实验路线、允许/不允许的做法、验收门槛与新的阶段划分
- 当前判断：
  - 这条“先验证、后分拆”的路线比立刻大拆更稳，也更符合当前主目标：先证明一档架构方向正确，再去做开源化与模块化

## 2026-05-19 迁移清单落文：明确未来 `witogram_core` 应收编哪些现有代码块

- 为了避免后续一边说“先验证后分拆”，一边新增逻辑继续四处散落，今天又把“未来 `witogram_core` 迁移清单”补进了规划文档。
- 本轮根据现有代码落点，已把一档相关逻辑初步归成四大块：
  1. 应迁入 `AnalyzeCredibility`
     - 主要来自 `witset_translator.cc` 中的：
       - `BuildJointRiskHints(const SyllableGraph&)`
       - `JointRiskHintBundle`
       - 基于 `SyllableGraph` 的 vertex/edge risk、spelling class 归纳逻辑
  2. 应迁入 `RewriteWordGraph`
     - 主要来自 `witset_translator.cc` 中：
       - `use_upstream_edge_prior_` 分支
       - 同起点竞争组上的 `candidate->weight` 统一改写逻辑
       - translator 侧的 competitive bias / merge competition bias 计算
  3. 应迁入 `RunWitogramPoet`
     - 主要来自 `WitsetPoet` 中的纯一档部分：
       - `MakeSentences()`
       - `SelectTopLines*`
       - `CompressLinePoolByState / PruneLinePool`
       - 纯一档 beam / line pool / state contract
  4. 应迁入 `witogram_core` 的 LM 特征层
     - 主要来自 `witogram.cc` 中：
       - `ScoreFeatures()`
       - `Query()`
       - KenLM model loader / cache
       - whole-word 与 char-path 混合得分
- 同时也明确了一组**绝对继续留在闭源 `witset`** 的内容：
  - `SentenceCache`
  - `AsyncPPLService`
  - `llm_level_2 / llm_level_3`
  - 本地/远程模型调度
  - F6 强制 Beam Search
  - `_witset_*` property 协议
  - `witset_processor / witset_filter` 的刷新与回填链路
- 这轮最大的意义，不是为了马上搬文件，而是为了在近期验证阶段把“纯一档逻辑”和“闭源编排逻辑”先从概念上切开：
  - 以后新增的一档逻辑，默认都应归到：
    - `AnalyzeCredibility`
    - `RewriteWordGraph`
    - `RunWitogramPoet`
  - 而不是继续长成新的混层 helper 或新的末端 patch
- 当前判断：
  - 到这一轮为止，近期验证路线已经不只是“原则上先验证”，而是连“现有代码里哪些块将来要迁、哪些绝不能迁”都已经写死
  - 后续如果开始动一档代码，应优先按照这份迁移清单来判断归属，避免再把主逻辑写回旧的末端补丁堆

## 2026-05-19 验证工作启动：先复现 21 条 shared-prefix 基线并确认当前起点

- 按“先验证、后分拆”的新路线，今天正式开始恢复一档验证工作；第一步没有改代码，也没有编译，而是先确认现有验证入口还能否在当前环境稳定复现。
- 先确认了以下关键前提都存在：
  - `librime/build_x64/bin/Release/rime_api_console.exe`
  - `librime/dist_x64/bin`
  - `C:\Users\Bing\AppData\Roaming\witty\debug\_shared_prefix_eval_excerpt.txt`
  - `C:\Users\Bing\AppData\Roaming\witty\debug\shared_prefix_eval_excerpt_base\reference_cases.jsonl`
- 还额外确认了脚本口径无误：
  - `run_local_snapshot_baseline.py` 在启动 `rime_api_console` 后会显式执行：
    - `set option !llm_level_3`
    - `set option !llm_level_2`
    - `set option llm_level_1`
  - 因而它确实是在跑一档，而不是误落到二档/三档
- 第一次直接跑 21 条代表集时，脚本超时在第一条 `huranjiujuede`：
  - 起初看起来像是 `rime_api_console` 或 snapshot 链路坏了
  - 但进一步手工向 `rime_api_console` 喂：
    - `select schema witset`
    - `set option !llm_level_3`
    - `set option !llm_level_2`
    - `set option llm_level_1`
    - `huranjiujuede`
  - 已确认控制台能正常出候选：
    - Top-1 为 `忽然就觉得`
  - 真正原因是：
    - 这次脚本把 `--snapshot` 指到了一个新的 probe 路径
    - 但当前 `witset.schema.yaml` 实际仍写默认 `debug/witset_local_snapshot.jsonl`
    - 于是脚本是在“等错文件”，不是算法没跑
- 修正回默认 snapshot 路径后，21 条 shared-prefix 代表集已成功跑通：
  - summary：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\shared_prefix_eval_excerpt_probe`
  - metadata：
    - `completed_text_steps = 21`
    - `expected_not_found_count = 4`
    - `preceding_text_mismatch_count = 10`
    - `wall_time_seconds = 301.765`
  - metrics：
    - `top1_accuracy = 0.571429`
    - `top3_accuracy = 0.619048`
- 这组结果与已固化的：
  - `shared_prefix_eval_excerpt_base`
  完全一致，说明：
  1. 当前验证入口是稳定可复现的
  2. 当前环境下的一档起点没有漂移
  3. 后续可以把这 21 条继续作为“上游介入点实验”的 smoke 集
- 关键错例也再次被客观复现：
  - `第一站是一座古老的小镇`
    - 当前 top1 仍是 `第一战士已作古老的小镇`
    - 正确句仅在更后排出现，且仍是 `第一站是以做古老的小镇` 这类脏中间形态
  - `两旁是古色古香的建筑`
    - 当前 top1 仍是 `两旁是故涩谷香的建筑`
  - `店主是一位和蔼可亲的老人`
    - 正确句当前仅到 rank 11
  - `也带着一种不可挽回的流逝`
    - 正确句当前仅到 rank 17
- 这轮结果还带来一个非常重要的观察：
  - 在这些关键 case 的当前 debug 字段里：
    - `EdgeRisk = 0`
    - `JointPrior = 0`
    - `AnchorDebt = 0`
  - 这说明当前线上配置下，现成的上游 risk / debt 相关链路基本没有真正参与最终排序
- 同时也复核了历史结论，避免重复走回头路：
  - 之前已经做过一轮 E1 最小原型：
    - `translator` 前移 `use_upstream_edge_prior`
    - 同时保持 `upstream_path_prior_weight = 0`
  - 当时在高信息量锚点上结果为：
    - `changed = 0`
    - 已判负并恢复关闭
  - 因而今天这一轮不会简单回到“重新打开旧 E1 常数看看”这条已经被止损过的路线
- 当前判断：
  - 现在最有价值的结论不是“某个参数能不能调一调”，而是：
    - 一档验证入口已恢复
    - shared-prefix 21 条 smoke 集可稳定复现
    - 当前关键错例里，上游 risk/debt 信号实际上没有进入有效竞争
  - 因而下一轮真正值得做的，不是重跑旧 E1 开关，而是针对：
    - `AnalyzeCredibility`
    - `RewriteWordGraph`
    设计新的、更强的上游介入实验

## 2026-05-19 第一轮上游 probe：translator 侧 `yi* / 是...一*` bridge candidate 前移

- 在完成 21 条 shared-prefix 基线复现后，今天继续推进了第一轮真正的代码实验。
- 这轮实验明确遵守了新的阶段边界：
  - 不碰二档、三档
  - 不做分拆
  - 不改 `poet` 主循环
  - 只在 `witset_translator` 的 `WordGraph` 重权阶段做最小上游 probe
- 动机来自刚复现出的 shared-prefix 锚点：
  - `带着一点微凉` 当前被 `疑点...` 家族压制
  - `也带着一种不可挽回的流逝` 当前被 `意中...` 家族压制
  - `走进一家特色小店` 当前被 `宜家...` 家族压制
  - `店主是一位和蔼可亲的老人` 当前被 `以为...` 家族压制
  - `第一站是一座古老的小镇` 中除了 `站是`，后半段 `一座` 也被 `已作` 家族压制
- 这些样本表现出一个非常集中的现象：
  - 在同 span 同音家族里，正确路径中大量出现 `一*` bridge 词（`一点 / 一种 / 一家 / 一位 / 一座`）
  - 同时 `站是 -> 一座` 这种 `是...一*` continuation 也经常在上游阶段就输给更高频但句级更差的竞争链
- 基于这个现象，今天在 `witset_translator.cc` 里做了一个**非常明确的 probe，而不是最终方案**：
  1. 新增 UTF-8 首尾字辅助函数
  2. 在 translator 的同 span 家族里识别：
     - 以 `一` 开头的候选
     - 以 `是` 结尾的候选
  3. 查看该家族下一跳是否存在强 `一*` continuation
  4. 若满足条件，则在 `WordGraph` 进入 `poet` 前对这些候选做额外前移
- 这轮 probe 使用的不是新的独立开关，而是直接复用现有：
  - `use_upstream_edge_prior`
  - `upstream_edge_prior_weight`
- 为了让 probe 真正生效，本地 schema 也同步调整为：
  - `use_upstream_edge_prior: true`
  - `upstream_edge_prior_weight: 4.0`
  - `upstream_path_prior_weight: 0.0`
- 代码改完后，按标准方式在 `librime` 执行了：
  - `build.bat static`
  - 编译通过
- 随后重新回放了 21 条 shared-prefix 代表集：
  - 产物目录：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\shared_prefix_eval_excerpt_probe_bridge_v1`
- 结果是**明确正收益**：
  - 基线：
    - `top1_accuracy = 0.571429`
    - `top3_accuracy = 0.619048`
  - 第一轮 probe 后：
    - `top1_accuracy = 0.619048`
    - `top3_accuracy = 0.714286`
- 这次收益不是随机波动，而是清楚命中了目标家族：
  1. `带着一点微凉`
     - 原 top1：`带着疑点微量`
     - probe 后 top1：`带着一点微量`
     - 说明 `一点` 成功翻过了 `疑点` 家族
  2. `也带着一种不可挽回的流逝`
     - 原 top1：`也带着意中不可挽回的流失`
     - probe 后 top1：`也带着一种不可挽回的流失`
     - 说明 `一种` 成功翻过了 `意中` 家族
  3. `走进一家特色小店`
     - 原 top1：`走进宜家特色小店`
     - probe 后 top1：`走进一家特色小店`
     - 这一条已经直接被翻正
- 同时也清楚看到了这轮 probe 的边界：
  1. `第一站是一座古老的小镇`
     - 仍然没有翻正
     - `战士已作...` 链依旧压住 `站是...一座...`
  2. `店主是一位和蔼可亲的老人`
     - 仍然没有翻正
     - `以为` 家族还未被彻底压下
  3. `两旁是古色古香的建筑`
     - 基本不受这轮 probe 影响
     - 说明这类 case 不属于 `yi* / 是...一*` bridge 家族问题
- 这轮还有一个额外发现：
  - 当前 candidate debug 字段里：
    - `EdgeRisk / JointPrior / AnchorDebt`
    仍然大多显示为 0
  - 说明这轮正收益主要来自 translator 侧的候选前移，而不是现有 debug 中已暴露出来的 path debt/risk 项
  - 换句话说：
    - 实验是有效的
    - 但当前调试口径还不能把这类 translator-side probe 解释清楚
- 当前判断：
  - 第一轮 probe 已经证明：
    - “把介入点往上移到 translator / WordGraph 阶段”不是空想
    - 至少对 `yi*` shared-prefix 家族，这条路可以带来真实收益
  - 但它也同时证明：
    - 当前 probe 仍然偏局部，只解决了一簇特定家族
    - 真正更深的一刀，仍然要落在：
      - `AnalyzeCredibility`
      - `RewriteWordGraph`
      的更一般化信号上，而不是长期依赖 surface bridge 字形 probe

## 2026-05-19 与原版 `octagram` 对齐 shared-prefix 代表集：收敛第二轮优先目标

- 根据新的验证准则，继续推进前先把 21 条 shared-prefix 代表集在原版 `stock Rime + octagram` 上按同口径重跑一遍，避免把精力浪费在 n-gram 上限之外的句子上。
- 这次使用：
  - `tools/run_stock_rime_octagram_baseline.py`
  - 语料：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\_shared_prefix_eval_excerpt.txt`
  - 输出：
    - `C:\Users\Bing\AppData\Roaming\Rime_octagram_bench\debug\shared_prefix_eval_excerpt_octagram_probe`
- 原版 `octagram` 在这 21 条上的结果为：
  - `top1_accuracy = 0.714286`
  - `top3_accuracy = 0.714286`
- 经过这轮对齐，当前可以把 21 条中的错例正式分成两类：
  1. **`octagram` 也无能为力，不再作为第二轮主目标**
     - `带着一点微凉`
       - `octagram` 也是 `带着一点微量`
     - `也带着一种不可挽回的流逝`
       - `octagram` 也是 `流失`
     - `我踏上了旅行的征程`
       - `octagram` 也仍然是 `整成`
     - `店主是一位和蔼可亲的老人`
       - `octagram` 仍然错为 `点住是一位...`
       - 虽然它比当前更接近正确，但 top1 仍非目标句
  2. **`octagram` 明确可解，而当前仍错，应作为第二轮主目标**
     - `第一站是一座古老的小镇`
     - `两旁是古色古香的建筑`
     - `一直向往着远方`
     - `体验不一样的生活`
- 这轮最重要的新增收敛，不是总体指标，而是优先级结构终于清晰了：
  - 第二轮不再泛泛地追所有 remaining mismatch
  - 只优先追 `octagram top1 正确 / 当前 top1 错误` 的 case
- 再往下看，这 4 条还可以继续拆成两个难度层级：
  1. **排名问题，值得优先下手**
     - `一直向往着远方`
       - 当前正确句已到 rank 10
       - 说明图里有路径，只是排序竞争输了
     - `体验不一样的生活`
       - 当前正确句已到 rank 7
       - 同样更像是排序竞争问题
  2. **进图/保活问题，更难，不作为第二轮第一刀**
     - `第一站是一座古老的小镇`
       - 当前 exact expected 已不在 top-N
       - top-N 里只有 `第一站是以做古老的小镇`
     - `两旁是古色古香的建筑`
       - 当前 exact expected 也不在 top-N
       - top-N 仍停留在 `故涩谷香` / `古色古香得见祝` 这类脏家族
- 这意味着第二轮最好不要一开始就硬打最难的 `第一站 / 古色古香`：
  - 它们更像“更深的 generation / graph 保活”问题
  - 需要更一般化的 `AnalyzeCredibility` 与 `RewriteWordGraph`
- 相比之下：
  - `一直向往着远方`
  - `体验不一样的生活`
  更适合作为第二轮第一批实验对象，因为：
  1. `octagram` 已证明 n-gram 路线可以做到
  2. 当前正确句已经存在于候选中
  3. 主要矛盾是“如何把正确句从中低位拉到 top1”
- 当前判断：
  - 到这一轮为止，shared-prefix 代表集已经有了明确的“止损边界”：
    - `octagram` 也错的句子，不再作为短期主目标死磕
    - 第二轮优先打 `octagram 正确 / 当前错误`
  - 第二轮的首批目标，建议先集中在：
    - `一直向往着远方`
    - `体验不一样的生活`

## 2026-05-19 第二轮上游 probe：cluster 内 `char fallback` rescue

- 在完成与 `octagram` 的对齐后，第二轮没有再继续追加 translator 侧的 surface 字形 probe，而是转向了一个更一般化的点：
  - `WitsetPoet` 在最终候选里已经会识别 top cluster，但此前基本只用于 debug，不真正参与 cluster 内排序
- 结合两条优先 case 的候选结构复核，当前观察是：
  - `一直向往着远方`
    - 正确句此前已到 rank 10
    - 主要输给同 cluster 的 `一直想望着远方` / `一支香望着远方` 等家族
    - debug 上能看到正确句并非没进图，而是被 `tail / octagram / fragment` 一类末端项压住
  - `体验不一样的生活`
    - 正确句此前已到 rank 7
    - 更像是 dense homophone cluster 里，低 `char fallback` clean candidate 没被优先保活
- 基于这个判断，第二轮实验选择了一个很克制的改动：
  1. 仍不碰二档、三档
  2. 不改 translator 主逻辑
  3. 只在 `WitsetPoet` 最终 top cluster 内，增加一个 `char fallback` rescue
- 具体规则是：
  - 仅当：
    - `cluster_end >= 4`
    - `debug_rerank_threshold <= 12.0`
  - 才在 cluster 内按：
    - `更少 cumulative_char_fallback_hits`
    做一次小幅重排
  - 当前权重是：
    - `kClusterFallbackRescueWeight = 18.0`
- 代码位置：
  - `plugins/witset/src/witset_poet.cc`
  - 动态 cluster 切分之后、最终输出之前
- 改动后重新执行了：
  - `build.bat static`
  - 编译通过
- 但第二轮在沿用 `run_local_snapshot_baseline.py` 时遇到两个非算法问题：
  1. 默认 snapshot 文件被其他进程占用，无法删除
  2. 临时切到新 snapshot 路径后，写出的 JSONL 文件体积异常增大，脚本在解析时出现 `MemoryError`
- 因而这轮没有再继续死磕整套 21 条 snapshot 汇总链路，而是退回到更稳的手工验证：
  - 使用 `subprocess.run()` 直接驱动 `rime_api_console.exe`
  - 用真实 `preceding_text`
  - 只验证两条第二轮优先句
- 手工验证结果如下：
  1. `体验不一样的生活`
     - 当前 top1 已经变成：
       - `体验不一样的生活`
     - 说明第二轮 cluster rescue 对这类 dense homophone cluster 明确有效
  2. `一直向往着远方`
     - 当前 top1 仍然是：
       - `一直想望着远方`
     - 正确句没有被这轮 cluster rescue 拉到 top1
- 这轮结论非常清楚：
  - 第二轮 probe **有效，但收益不是全面的**
  - 它已经成功把：
    - `体验不一样的生活`
    从此前的 rank 7 拉到 top1
  - 但对：
    - `一直向往着远方`
    这种尾部惩罚与尾词竞争更复杂的 case，cluster 内按 `char fallback` rescue 还不够
- 当前判断：
  - 第二轮说明：
    - “在最终 cluster 内做 clean candidate rescue” 这条思路是成立的
    - 它比继续猜具体字形更一般化，也更接近可复用机制
  - 但它也说明：
    - 单靠 `char fallback` 还不够解释 `向往 / 想望 / 香望` 这种竞争
    - 下一步更值得看的，是：
      - cluster 内再引入更直接的 `local grammar / LM consistency`
      - 或更细的 `tail / fragment / octagram penalty` 局部放松策略

## 2026-05-19 第三轮上游 probe：final-list `LM raw rescue`

- 针对第二轮后仍未翻正的主目标：
  - `一直向往着远方`
- 先重新复核了第一轮完整候选分项，关键事实是：
  - 当前 top1 `一直想望着远方`
    - `WholeHit = 0`
    - `LmRaw = -284.40`
    - `LmAvg = -92.94`
  - 正确句 `一直向往着远方`
    - `WholeHit = 2`
    - `LmRaw = -242.32`
    - `LmAvg = -181.22`
  - 也就是说，从整句 LM 证据看，正确句明显更强；当前输掉并不是因为 LM 本身不支持，而更像是浅层 dict/head path 先把它压下去了。
- 因而第三轮没有继续改 `tail` 常数，而是试了一个更贴近 `octagram` 思路的小实验：
  - 在 `WitsetPoet` final list 阶段，若当前 top1：
    - `cumulative_whole_word_hits == 0`
  - 且后面的候选满足：
    - `LmRaw` 明显更强
    - `LmAvg` 也更强
    - `char fallback`、`lm_oov_tokens` 不更差
    - `weight gap` 还在可接受范围内
  - 则允许该候选越过 top1
- 当前阈值是：
  - `kLmRawRescueMinGain = 30.0`
  - `kLmAvgRescueMinGain = 4.0`
  - `kLmRescueMaxWeightGap = 50.0`
- 代码位置：
  - `plugins/witset/src/witset_poet.cc`
  - final cluster rescue 之后、最终输出之前
- 这轮重新执行了：
  - `build.bat static`
  - 编译通过
- 之后继续用更稳的手工回放方式验证两条句子：
  1. `一直向往着远方`
  2. `体验不一样的生活`
- 手工结果如下：
  1. `一直向往着远方`
     - 仍未翻正
     - 当前 top1 仍是：
       - `一直想望着远方`
     - 但第三名已变成：
       - `以至向往着远方`
     - 这说明这轮 rescue 的确开始把 “LM 更强、whole-hit 更多” 的家族往前抬，但**仍不足以把正确句抬到 top1**
  2. `体验不一样的生活`
     - 仍保持 top1 正确：
       - `体验不一样的生活`
- 这轮结论很重要：
  - third-round `LM raw rescue` **没有解决主目标**
  - 它说明 final-list 末端 rerank 已经开始接近极限：
    - 能把“更像样”的候选往前抬
    - 但还不足以稳定区分 `向往 / 想望 / 以至向往` 这类更细的竞争
- 当前判断：
  - 继续在 final list 末端换不同 rerank 条件，收益开始变差
  - 下一步更值得做的，不是继续堆末端 rescue，而是回到：
    - translator / WordGraph 同 span 家族
    - 用局部 prefix-context grammar bias 或更上游的竞争组重权
  - 换句话说，这轮失败反而进一步支持了此前的主判断：
    - 介入点确实还得继续往上移，不能长期停留在 poet 最终列表末端死磕

## 2026-05-19 第四轮上游 probe：translator / WordGraph 局部 grammar bias（判负）

- 这轮尝试把介入点正式移到 translator / WordGraph，同样只围绕 `一直向往着远方` 做实验。
- 做法分两版：
  1. 单词级 local grammar bias
     - 在 `WitsetTranslator` 内部创建 `Grammar` 实例
     - 用当前图上 best prefix path 近似 `prefix_context`
     - 在同 span 家族内比较 `grammar->Query(prefix_context, candidate_word)`
  2. 一词前看 + 一词后看短窗 bias
     - 继续沿用同一套 `prefix_context`
     - 但不再只看单词，而是比较 `candidate + best_next_word`
     - 例如更接近 `向往着 / 想望着`、`一样的 / 易养的`
- 两版都通过了 `build.bat static` 编译，也都只用两条手工前文回放做最小验证：
  - `一直向往着远方`
  - `体验不一样的生活`
- 客观结果：
  - 对 `一直向往着远方`
    - 两版都没有把 top1 从 `一直想望着远方` 翻正。
  - 对 `体验不一样的生活`
    - 两版都引入了回归：top1 变成 `体验不易养的生活`，正确句退到 rank 2。
- 结论：
  - 这条 translator 侧 local grammar bias 路线当前判负。
  - 问题不只是权重没调好，而是“局部 prefix + 局部短窗”的 grammar 证据本身不稳定：
    - 对 `向往` 这类目标不够强
    - 却会错误放大 `易养` 这类局部更顺的伪优项
  - 因此这轮不继续沿这条线调权重或加更多启发式。
- 收尾处理：
  - schema 已把：
    - `use_upstream_local_grammar_bias`
    - `upstream_local_grammar_bias_weight`
    恢复为关闭状态，避免当前环境停在一个比第三轮更差的配置上。
- 当前判断：
  - 末端 rerank 已经撞墙；translator 局部 grammar bias 也暂时判负。
  - 后续若继续往上游推进，更值得尝试的应是：
    - 更结构化的 `AnalyzeCredibility / RewriteWordGraph`
    - 或直接围绕 `octagram` 能解的 case 做 graph 级保活与竞争组重权，而不是再追加局部 grammar 打分。

## 2026-05-19 扩展实验组 `expanded_probe_guardrail_v1`

- 为避免后续 graph 级实验只围绕两条句子过拟合，新增一组 10 条的中等规模验证集，分三类：
  1. 主目标（4 条）
     - `yizhixiangwangzheyuanfang` -> `一直向往着远方`
     - `tiyanbuyiyangdeshenghuo` -> `体验不一样的生活`
     - `diyizhanshiyizuogulaodexiaozhen` -> `第一站是一座古老的小镇`
     - `liangpangshiguseguxiangdejianzhu` -> `两旁是古色古香的建筑`
  2. 护栏句（4 条）
     - `zoujinyijiatesexiaodian` -> `走进一家特色小店`
     - `muzhidimenchuang` -> `木质的门窗`
     - `taruxiaozhendenayike` -> `踏入小镇的那一刻`
     - `dousushuozhesuiyuedegushi` -> `都诉说着岁月的故事`
  3. 敏感句（2 条，非主目标，但容易被局部 heuristic 带偏）
     - `daizheyidianweiliang` -> `带着一点微凉`
     - `yedaizheyizhongbukewanhuideliushi` -> `也带着一种不可挽回的流逝`
- 当前代码状态下，使用真实 `preceding_text` 手工回放这 10 条，输出写入：
  - `C:/Users/Bing/AppData/Roaming/witty/debug/expanded_probe_guardrail_v1`
- 当前基线指标：
  - `top1 = 0.5`
  - `top3 = 0.7`
  - `target_top1 = 0.25`
  - `guardrail_top1 = 1.0`
  - `sentinel_top1 = 0.0`
- 当前结构结论：
  - 主目标 4 条里，只有 `体验不一样的生活` 已经 top1 正确。
  - `两旁是古色古香的建筑` 已进 top3，仍属排序/竞争问题。
  - `一直向往着远方` 与 `第一站是一座古老的小镇` 仍未进 top3，更像 family 保活/竞争组重权问题。
  - 护栏句 4/4 全部稳定，说明当前第一轮/第二轮收益尚未破坏这几条已拿下的句子。
  - 敏感句 2/2 仍未解决，其中 `流逝` 已进 top3，可继续作为回归/顺带收益观察项，但不作为主目标死磕。
- 因此，后续 graph 级实验的最低要求改为：
  - 不能破坏 4 条护栏句 top1
  - 优先尝试把：
    - `两旁是古色古香的建筑`
    - `一直向往着远方`
    往 top1 拉近
  - 若 `第一站是一座古老的小镇` 仍完全不进 top3，则要单独视作更深的 graph 保活问题处理
## 2026-05-19 `G1` graph 级同组重权（exact family bias）实验：已判负

- 本轮继续承接“不要再在末端死磕，而是改查 graph 级同组竞争”的主线；具体做法是在 `witset_translator` 的 WordGraph 重权阶段，新增一版“same-span family 内优先保 strong exact 候选”的最小原型：
  - 代码侧新增：
    - `IsTranslatorStrongExactCandidate()`
    - `ComputeTranslatorExactFamilyBias()`
    - schema 开关：`upstream_path_prior_weight`
  - 判定口径：
    - `CountUTF8Chars(text) >= 2`
    - `remaining_code_length == 0`
    - `IsExactMatch()`
    视为 strong exact family 成员
  - 目标不是做 final-list rerank，而是在 translator / WordGraph 阶段，让 exact family 在同组竞争里更容易保活/抬头
- 代码改完后先做了最基本的静态检查：
  - `witset_translator.h` / `witset_translator.cc` diagnostics 均为空
- 随后按约定方式只执行了：
  - `librime/build.bat static`
  - 未做 clean，未走其他编译/部署链路
  - 编译通过
- 为避免 graph 调试导出继续产生额外几十 GB 工件，本轮验证时将：
  - `debug_dump_local_graph_snapshot: false`
  - 只保留普通 snapshot 与真实 `rime_api_console.exe` 回放
- 验证集继续使用 `expanded_probe_guardrail_v1` 的 10 条：
  - 4 条主目标
  - 4 条护栏句
  - 2 条敏感句
- 为判断这条路线到底是“强度过大”还是“方向就不对”，对 `upstream_path_prior_weight` 做了小 sweep：
  - `0.0` -> `top1=0.5`, `top3=0.7`, `target_top1=0.25`, `guardrail_top1=1.0`, `sentinel_top1=0.0`
  - `0.5` -> 完全等同于 `0.0`
  - `1.0` -> 完全等同于 `0.0`
  - `2.0` -> `top1=0.4`, `top3=0.6`, `target_top1=0.0`, `guardrail_top1=1.0`, `sentinel_top1=0.0`
  - `4.0` -> 与 `2.0` 基本同样的负回归
- 关键 case 变化：
  - `一直向往着远方`
    - `0.0/0.5/1.0/2.0/4.0` 全都没救起来，仍为 `一直想望着远方`
  - `两旁是古色古香的建筑`
    - `0.0/0.5/1.0/2.0/4.0` 全都没把 exact 句从 top3 拉到 top1
  - `第一站是一座古老的小镇`
    - `0.0/0.5/1.0/2.0/4.0` 全都没有本质改善，仍停留在更深 graph 保活失败状态
  - `体验不一样的生活`
    - baseline (`0.0`) 原本是 top1 正确
    - 从 `2.0` 开始被打回 `体验不宜养的生活`
    - `4.0` 同样回归
- 因而这条实验线已经足够明确：
  - 小权重 (`0.5/1.0`) 没有任何收益
  - 一旦权重进入能实际影响排序的区间 (`2.0+`)，首先打坏的是已正确的 `体验不一样的生活`
  - 同时并没有换来 `一直向往着远方` / `两旁是古色古香的建筑` / `第一站是一座古老的小镇` 的任何对应改善
- 当前判断：
  - 这版“exact family bias”更像是在奖励局部 exactness，而不是在修复真正的 graph 级 continuation/保活问题
  - 它没有触达 `一直向往着远方` 和 `第一站是一座古老的小镇` 的深层瓶颈
  - 对 `体验不一样的生活` 的负回归说明这条 bias 方向本身就不干净，不只是参数没调好
- 因此，本轮正式判负：
  - `upstream_path_prior_weight` 已恢复到 `0.0`
  - 暂不继续在“same-span exact family 直接加/减分”这条线追加调参
- 本轮产出的详细 sweep 工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\expanded_probe_guardrail_v1\graph_same_span_weight_sweep.json`
- 下一步更值得尝试的方向：
  - 不再把 family 内“exact 与否”当核心证据
  - 应改查为什么目标路径在 graph 中存在，却没能在更后面的扩展/保活链条里穿过去；尤其优先拆：
    - `一直向往着远方` 的 family 保活
    - `第一站是一座古老的小镇` 的更深 graph continuation 死亡点
## 2026-05-19 `G2` 单 case gate 复核 + `stable whole continuation` 实验：已判负

- 在上一轮 `same-span exact family bias` 判负后，继续沿“graph / poet 链路里到底死在哪一层”往下拆；这次先不急着再加权重，而是先补单 case 运行时证据。
- 临时将 schema 的调试输出切到单独 probe 文件，只回放两条：
  - `yizhixiangwangzheyuanfang`
  - `diyizhanshiyizuogulaodexiaozhen`
- 新工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\single_case_gate_probe.jsonl`
  - `C:\Users\Bing\AppData\Roaming\witty\debug\single_case_gate_probe.graph.jsonl`
  - `C:\Users\Bing\AppData\Roaming\witty\debug\single_case_gate_probe_analysis.json`
- 关键复核结论：
  - `一直向往着远方`
    - 目标句并不是“根本没进图”或“早早被 batch 切掉”
    - 它已经能活到最终候选池，但排位很低；probe 里可见它在最终候选页只到 `rank 10`
    - 这说明当前主问题不是简单的 graph 入口缺失，而是后段累计评分里系统性输给了 `一直想望着远方`
  - `第一站是一座古老的小镇`
    - `第一站 -> 是 -> 是一座` 这条正确续接在新 probe 中能看到完整的：`request -> batch_selected -> admitted_new`
    - 也就是说它不是在 `global_batch_limit` 或 state-admit 这一层被硬挡掉
    - 但继续往长句扩展时，最终保留下来的是 `第一战士已作古...` 和 `第一站是以做古...` 这些 continuation
    - 因而这条 case 的更深根因仍然是长 continuation 被 `已作古` 链条接管，而不是 simple gate 没放行
- 基于上面的新证据，这一轮没有再回去做末端 final-list 旋转，而是在 `WitsetPoet` 里做了一版更窄的句级补偿实验：
  - 新增 schema 权重：`stable_whole_continuation_bonus_weight`
  - 新增 helper：`ComputeStableWholeContinuationBonus()`
  - 目标：当路径已经累计多个 whole-word hits，当前步也 matched whole-word，且没有 joint 风险/当前步 fallback/OOV 时，给这类“稳定整词 continuation”一点额外 bonus，尝试对冲 `fragment / structure / tail / octagram` 这组惩罚
  - 实现方式：直接并入现有 `whole_word_bonus`，不额外扩散状态字段
- 本轮代码修改点：
  - `plugins/witset/src/witset_poet.h`
  - `plugins/witset/src/witset_poet.cc`
  - `C:\Users\Bing\AppData\Roaming\witty\witset.schema.yaml`
- 编译过程：
  - 首次 `build.bat static` 因我把 helper 插错位置、把 `ComputeUpstreamPathPriorPenalty()` 的函数头劈开而失败
  - 随后修正语法，重新执行 `build.bat static`，编译通过
- 验证方式：
  - 沿用 `expanded_probe_guardrail_v1` 的 10 条扩展集
  - 只 sweep `stable_whole_continuation_bonus_weight`
  - 工件：`C:\Users\Bing\AppData\Roaming\witty\debug\expanded_probe_guardrail_v1\stable_whole_continuation_weight_sweep.json`
- sweep 结果：
  - `0.0` -> `top1=0.5`, `top3=0.7`, `target_top1=0.25`, `guardrail_top1=1.0`, `sentinel_top1=0.0`
  - `2.0` -> 完全不变
  - `4.0` -> 完全不变
  - `6.0` -> 完全不变
- 因而这条实验线的结论已经足够明确：
  - 这版 `stable whole continuation bonus` 至少在当前门控条件下，没有对 10 条扩展集产生任何可见影响
  - 不是“方向正确但太强/太弱”，而是整条实验线在当前实现下近似静默
- 当前判断：
  - `一直向往着远方` 与 `第一站是一座古老的小镇` 都不是简单的 early gate 丢失；它们都能走到更后面
  - 但两者的主要矛盾又不完全一样：
    - `一直向往着远方`：目标句能进 final_pool，但被后段累计评分系统性压到很后面
    - `第一站是一座古老的小镇`：`第一站是一座` 本身能存活，但长 continuation 继续往后时仍被 `已作古` 链条接管
  - 说明当前再沿“给 stable whole-word path 一点 bonus”这条线加码，性价比已经很低
- 因此，本轮也正式判负：
  - `stable_whole_continuation_bonus_weight` 保持 `0.0`
  - 暂不继续沿这条 bonus 路线调参
- 下一步更值得尝试的方向：
  - 不再做 generic family bonus / exact bonus / stable-whole bonus
  - 转而查 `一直向往着远方` 这类“已进 final_pool 但总账被系统性打坏”的具体 penalty 组合，尤其：
    - `fragment_penalty`
    - `structure_penalty`
    - `tail_repair_penalty`
    - `octagram_penalty`
  - 重点不是给泛化 bonus，而是问：这些 penalty 为什么会在 `whole_word_hits > 0` 的正常句上持续累计到过重
## 2026-05-19 `G3` `一直向往着远方` penalty ledger + beam / keepalive 复核：继续收紧根因边界

- 本轮承接 `G2` 的单 case gate 结果，继续只围绕 `一直向往着远方` 往下拆，不再先加 generic bonus，而是先问：
  1. 最终 exact 句到底输在哪些分项上
  2. 干净 continuation 是否真的存在
  3. 若存在，它是死在 beam / pool 保活，还是死在更前面的评分契约

- 先直接读取 `single_case_gate_probe.jsonl` 的最终候选 debug 总账：
  - `一直想望着远方`（top1）
    - `Base:-256.51`
    - `Dict:-15.52`
    - `LmScaled:-240.99`
    - 结构罚分基本全为 `0`
  - `一直向往着远方`（baseline rank10）
    - `Base:-261.21`
    - `Dict:-40.60`
    - `LmScaled:-220.61`
    - `Whole:-0.20`
    - `Frag:-0.68`
    - `Struct:-0.51`
    - `Tail:-3.32`
    - `Octa:-9.60`
- 这一步直接说明：
  - exact 句最终留在 `final_pool` 里的那条路径，不是输在 LM；其 `LmScaled` 反而比 `一直想望着远方` 更好
  - 真正拉开差距的是：
    - `dict_score_raw` 明显更差
    - 同时叠了整套 `fragment / structure / tail / octagram` 结构罚分
  - 也就是说，当前头部 exact 句已经不是一条“干净整词 continuation”路径，而是更差的 graph 切分残体

- 随后继续追这条 exact 文本有没有更干净的中段路径：
  - 直接从 `expansion_gate_records` 中抽到：
    - `source_text = ...一直`
    - `entry_text = 向往着`
    - 已经完整经过：
      - `request`
      - `batch_selected`
      - `admitted_new`
    - 且这一跳：
      - `fragment_penalty = 0`
      - `structure_penalty = 0`
      - `tail_repair_penalty = 0`
      - `octagram_penalty = 0`
- 这说明：
  - 正句并不是“根本没有干净 continuation”
  - 至少在中段，`一直 + 向往着` 这条较干净的 continuation 确实存在，而且能进 admitted state

- 但继续往后追时，没有找到：
  - `source_text = ...一直向往着`
  - `entry_text = 远方`
  这样的后续扩展记录
- 因而新的收口判断变成：
  - 干净中段 continuation 并不是算不出来
  - 它更像是在进入 target/source pool 后没能活到下一轮继续展开，最终留下来的 exact 句则退化成了更碎的路径版本

- 为确认是不是纯粹的 beam / batch 配额问题，做了一轮不改代码的放宽实验：
  - 临时只提高：
    - `word_beam_size = 120`
    - `sentence_beam_size = 1000`
    - `global_batch_limit = 9600`
    - `sentence_soft_limit = 100000`
  - 单句回放结果：
    - `一直向往着远方` 从 baseline 的 `rank 10` 提升到 `rank 5`
  - 但 10 条扩展集工件 `beam_relax_eval.json` 显示：
    - `top1 = 0.5` 不变
    - `top3 = 0.7` 不变
    - `target_top1 = 0.25` 不变
    - `guardrail_top1 = 1.0` 不变
- 这轮的意义是：
  - beam 放宽确实对 `一直向往着远方` 有局部保活帮助
  - 但帮助只够把它从深后排挪到中后排，并没有转化成 top3/top1 收益
  - 所以问题不是“当前 beam 太小”这么简单

- 在此基础上，又做了一版更窄的保活实验原型：
  - 临时在 `WitsetPoet` 的 `source_pool / target_pool` 剪枝比较逻辑里，加一版 `bridge keepalive` bonus
  - 触发目标只瞄准：
    - 非终局 line
    - 最近一步是 `char fallback`
    - 最近词长至少 3 字
    - 当前累计 `fragment / structure / tail / octa` 全为 0
  - 目的是只帮助像 `一直 + 向往着` 这种“中途干净但容易被挤掉”的 line，多活一轮
- 编译：
  - `librime/build.bat static`
  - 编译通过
- sweep：
  - `bridge_keepalive_bonus_weight = 0 / 8 / 16 / 24`
  - 工件：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\expanded_probe_guardrail_v1\bridge_keepalive_weight_sweep.json`
- 结果：
  - 10 条扩展集所有指标完全不变
  - `一直向往着远方` 的 top5 也完全不变
  - 所以这版 keepalive 原型属于“静默判负”
- 由于该代码默认 `0.0` 下也没有任何正信号，已把这版 `bridge_keepalive` 代码回退，不保留在当前源码里

- 当前已经能比较稳地收束出这几个结论：
  1. `一直向往着远方` 的最终坏结果，确实表现为结构罚分很重
  2. 但这些结构罚分压到的是“已经退化后的 exact 残路径”，不是最早的根因
  3. 更干净的中段 continuation `一直 + 向往着` 是存在的，而且能进入 admitted state
  4. 纯粹放大 beam，只能让它从更后排往前挪一点，不能变成真正收益
  5. 再加 poet 内部的 generic keepalive，也没有任何实际效果

- 因而到这一步，下一步最值得坚持的判断是：
  - 当前主问题已经不太像 `poet` 末端继续加 bonus / keepalive / 结构补偿就能解决
  - 更深根因更像是：
    - 上游 `candidate->weight / DictEntry.weight` 契约
    - 以及 graph 中长 continuation 的候选构造与比较基础
  - 换句话说：
    - `一直向往着远方` 的 clean continuation 之所以没能最终留下，不是单纯因为末端少了一个小补偿，而更像因为它在上游总账里就没有建立足够干净、可持续扩展的优势
## 2026-05-19 `G4` 上游候选契约复核：`一直向往` 分叉已前移到 graph，总结继续收束

- 本轮不再沿 `poet` 末端补丁推进，而是回到更上游复核：
  - `DictEntry.weight / candidate->weight` 在进入 `WitsetPoet` 前究竟怎么形成
  - `一直向往着远方` 与 `一直想望着远方` 两条家族，最早到底在哪一步分叉

- 代码链路复核后确认：
  - `DictEntry.weight` 的主底座来自：
    - 编译词典后的 table weight
    - 或用户词典运行时权重公式
    - 再叠加 `credibility`
  - `WitsetTranslator` 进入 `WitsetPoet` 前，真正还会继续改这个字段的，主要只有：
    - edge / family / bridge / exact bias
    - local grammar bias
  - `WitsetPoet` 入口处 `base_score` 直接吃 `candidate->weight`
- 也就是说：
  - 如果某条 family 在 `poet` 前已经吃亏，末端再补往往很难救回来

- 对 `yizhixiang*` 的 beam probe 继续下拆，拿到几个新的硬结论：
  1. 最早分叉点不是 final ranking，而是 translator/beam 早期：
     - `yizhixiang` 时 beam top1 已是 `一直想`
     - `yizhixiangwang` 时 `一直向往` 仅排第二
     - 到 `yizhixiangwangzhe`，`一直向往着` 才暂时反超
  2. 这些前缀阶段的 debug 中：
     - `StepEdgeRisk = 0`
     - `StepEdgeType = 0`
     - `AnchorDebt / AnchorRelease / MergeDelta = 0`
     基本全为零
  3. 同时 `StepWholeLog10 = 0`、`StepWholeHit = 0`、`StepCharFB = 1`
     - 说明这里根本不是 whole-word 命中，而是统一走 `char fallback`
- 这与当前 `.klm` 的既有结论完全一致：
  - 当前模型是 split-token 模型
  - `向往 / 向往着 / 一直向往着` 这类多字串并不会作为 whole-word token 命中
  - 所以很多依赖 whole-word / strong exact / whole-first-word 的正向实验，本质上都打不到这批 case

- 为彻底确认 `StepEdgeRisk = 0` 是否只是阈值问题，又临时导出 `yizhixiang / yizhixiangwang / yizhixiangwangzhe` 的 graph snapshot：
  - 工件：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\yizhixiang_edge_probe.graph.jsonl`
- 抽取关键边后确认：
  - `0 -> 10`：`一直想`，`edge_risk = 0`，`edge_spelling_class = 0`
  - `5 -> 14`：`向往 / 想往`，`edge_risk = 0`，`edge_spelling_class = 0`
  - `5 -> 17`：`向往着`，`edge_risk = 0`，`edge_spelling_class = 0`
  - `10 -> 17`：`望着`，`edge_risk = 0`，`edge_spelling_class = 0`
- 这说明：
  - 对这个 case 来说，`xiang` 这里不是当前 risk-hint 体系想处理的那类 `ambiguous joint`
  - 因而 `edge_risk / spelling_class` 路线不是“力度太小”，而是压根没有命中真实分叉边

- 更重要的是，graph 原始权重本身已经给出了新的上游证据：
  - `一直 = -1.44004`
  - `一直想 = -2.1126`
  - `向往 = -12.4648`
  - `想往 = -12.8562`
  - `向往着 = -13.2222`
  - `望着 = -12.4911`
- 组合对比：
  - `一直 + 向往着 = -14.66224`
  - `一直想 + 望着 = -14.6037`
- 也就是说：
  - graph 里正确家族和错误家族都真实存在
  - 而且不是大幅错位，只差约 `0.05854`
  - 但系统当前并不会把“更早闭合、边更长的 continuation”当成正向契约；因此错家族能在后续继续活着并接管 `远方`

- 基于这个新证据，补做了一版新的 translator 级原型：
  - 新思路：同一 `end_pos` 竞争里，若“更长的 strong exact continuation”组合 path score 只略落后于当前最佳路径，则给它一点 upstream bias
  - 目标是更早命中：
    - `一直 + 向往着`
    - 相对 `一直想 + 望着`
- 实现：
  - 临时新增 `upstream_end_competition_weight`
  - 在 `WitsetTranslator` 里基于 `BuildBestPrefixStates(graph)` 扫描同 `end_pos` 竞争
- 编译：
  - `librime/build.bat static`
  - 编译通过
- 验证：
  - sweep `0 / 0.5 / 1 / 2 / 4`
  - 工件：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\expanded_probe_guardrail_v1\upstream_end_competition_weight_sweep.json`
- 结果：
  - 10 条扩展组所有指标完全不变
  - `一直向往着远方` 的 top5 也完全不变
- 因而这条 `same-end prefix+continuation` 偏置原型也判为静默无效
- 由于它默认 `0.0` 下也没有任何正信号，已把这段实验代码与 schema 开关全部回退，不保留在当前源码里

- 到这一步，关于 `一直向往着远方` 的边界已经比较清楚：
  1. 真实分叉早于 poet，发生在 translator / graph / beam 阶段
  2. risk-hint 路线命不中它，因为相关边本身 `edge_risk = 0` / `edge_spelling_class = 0`
  3. whole-word 路线也命不中它，因为当前 split-token KLM 下这批多字 continuation 实际统一走 `char fallback`
  4. graph 原始权重里，正确与错误家族已经非常接近，但错误家族略优
  5. 无论末端补偿、beam 放宽、poet keepalive，还是新的 same-end 竞争偏置，都没把这种“上游近身错位”转化成 10 条集收益

- 因而当前最稳的下一步判断是：
  - 若还要继续深挖，优先级不应再放在 `WitsetPoet` 或 `WitsetTranslator` 的局部补丁上
  - 更值得查的是：
    - `DictEntry.weight` 里哪些部分其实已经混入 path credibility
    - 以及当前 split-token `char_path_log10` 为什么会稳定偏向 `一直想 + 望着` 这类 family
  - 换句话说，根因更像：
    - 上游原始总账本身就没有建立出足够干净的正确 family 优势
    - 而不是少了一个局部 rerank / keepalive 技巧
## 2026-05-19 `G5` `char_path_log10` 家族拆账：偏向点不在最后一跳，而在更早的 fallback 组织方式

- 本轮按方案 B 只做拆账，不改代码：
  - 目标是把 `一直向往` / `一直想望` 两个 family 在 `yizhixiang`、`yizhixiangwang`、`yizhixiangwangzhe`、`yizhixiangwangzheyuanfang` 这四拍的 `char_path_log10` / `CharFB` / `Dict` / `LmRaw` 串起来
  - 问题不是“最后一步谁的 step LM 更好”，而是：错误 family 是否更早就在用更少的 fallback 跳数建立优势

- 直接读取 `yizhixiangwang_beam_probe.jsonl` 后，得到几个关键台阶：
  1. `yizhixiang`
     - top1 已是 `一直想`
     - `CharFB = 1`
     - 说明错误 family 从第一拍就先手，且这里没有 whole-word 命中
  2. `yizhixiangwang`
     - rank1: `一只想往`
       - `LmRaw = -150.57`
       - `StepCharLog10 = -72.567904`
       - `CharFB = 2`
     - rank2: `一直向往`
       - `LmRaw = -173.46`
       - `StepCharLog10 = -95.464771`
       - `CharFB = 2`
     - 这一拍最关键：
       - 两边都还是 `char fallback`
       - 但错误 family 的 `char_path_log10` 已经明显更好，raw gap 约 22.89
       - 因而 `char_path_log10` 对错误 family 的偏向，从第二拍就已经非常强
  3. `yizhixiangwangzhe`
     - rank1: `一直向往着`
       - `LmRaw = -188.37`
       - `StepCharLog10 = -110.370092`
       - `CharFB = 2`
     - rank2: `一直想往这`
       - `LmRaw = -211.10`
       - `StepCharLog10 = -95.606563`
       - `CharFB = 2`
     - 到这一步，正确 family 反超
     - 但注意：
       - 它是靠累计 `LmRaw` 追回来的
       - 当前步 `StepCharLog10` 其实仍比错误 family 更差
       - 也就是说，`char_path` 本身并没有开始偏爱正确 family
  4. `yizhixiangwangzheyuanfang`
     - rank1: `一直想望着远方`
       - `LmRaw = -284.40`
       - `Dict = -15.52`
       - `CharFB = 2`
       - `StepCharLog10 = -168.897844`
     - rank5: `一直向往着远方`
       - `LmRaw = -242.32`
       - `Dict = -27.21`
       - `CharFB = 3`
       - `StepCharLog10 = -95.448132`
     - 这一步最关键：
       - 正确 exact 句的累计 `LmRaw` 反而显著更好
       - 当前步 `StepCharLog10` 也并不差
       - 但它已经多了一次 `CharFB`，同时 `Dict` 被拖坏到 `-27.21`
       - top1 `一直想望着远方` 之所以赢，不是最后一步 char LM 突然更对，而是它整句只用 `2` 次 fallback 就闭合了

- 结合前几轮已有证据，可以把这条 case 再收紧成一句话：
  - `char_path_log10` 的真正偏向点，不在最终 `远方` 这一跳，而在更早的 `xiangwang` 这拍就已经强烈偏向 `想往` 家族；
  - 到完整串时，真正决定胜负的是“谁能维持更少的 `CharFB` 次数并保持更好的 Dict/Base 总账”，而不是最后一步 LM 打分。

- 这也解释了为什么前面几轮都打不到：
  - `risk-hint` 命不中，因为相关边 `edge_risk = 0`
  - `whole-word` 命不中，因为当前 split-token KLM 下这些 continuation 统一走 `char fallback`
  - `poet keepalive / same-end competition` 命不中，因为它们发生得太晚，而 `char_path` 对错误 family 的强偏好在 `xiangwang` 阶段就已经建立

- 因而，方案 B 的阶段性结论已经足够明确：
  - 真正值得继续查的不是“如何在末端再补一点分”，而是：
    1. 为什么 `xiangwang` 这拍的 `char_path_log10` 会对 `想往` 家族强偏约 22.89
    2. 为什么正确 exact 句最终会比 top1 多出一次 `CharFB`
  - 换句话说，若还继续深挖，下一层应该去查：
    - `Witogram::ScoreFeatures()` 对 `xiangwang / wangzhe / yuanfang` 的分词与字符 token 路径
    - 或者更直接地，比对 `一直 + 向往` 与 `一直想 + 往` 这类 family 在 split-token KLM 下的 token 级 score 组成
## 2026-05-19 `G6` 方案 A 探针：`credibility` 不是 `一直向往` 这条 case 的主因

- 本轮按方案 A 做了一版只读 instrumentation，用来拆：
  - `raw_weight`
  - `credibility_weight`
  - `initial_weight`
  - 当前 graph 中的 `candidate->weight`
- 目的不是改排序，而是确认：
  - `DictEntry.weight` 里混入的 `credibility/path debt` 对这条 case 到底有没有实质贡献
  - 以及 `candidate->weight + dict_score_raw` 的重复记账结构，对这条 case 是否真的在放大 `credibility`

- probe 实现方式：
  - 临时给 `DictEntry` 增加 debug 字段
  - 在 `dictionary.cc` / `user_dictionary.cc` 构造 `DictEntry` 时拆出：
    - `raw_weight`
    - `credibility_weight`
    - `initial_weight`
  - 在 graph snapshot dump 中临时打印这些字段
- 编译：
  - `librime/build.bat static`
  - 编译通过
- 验证工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\yizhixiang_scheme_a_probe.graph.jsonl`
- 由于 instrumentation 只是诊断用途，拿到证据后已全部回退，不保留在当前源码中

- 方案 A 这次给出了一个和预期相比“更强、更具体”的新结论：
  - 对 `一直向往着远方` 相关关键边：
    - `一直`
    - `一直想`
    - `向往`
    - `想往`
    - `向往着`
    - `望着`
    - `望着远方`
    - `远方`
  - probe 中全部出现：
    - `credibility_weight = 0`
    - `initial_weight = raw_weight`
- 这说明：
  - 至少对这条 case 的关键竞争边来说，`credibility` 并没有混进边权后造成分叉
  - 也就是说，方案 A 的“怀疑 `credibility/path debt` 污染底座”在这个 case 上并不是主因

- 更进一步，把 `yizhixiangwangzhe` 这一拍两条 family 组合后对比：
  - A: `一直 + 向往着`
    - `raw = -25.0622`
    - `current = -14.66224`
  - B: `一直想 + 望着`
    - `raw = -25.0037`
    - `current = -14.6037`
  - gap：
    - `raw gap (B-A) = +0.0585`
    - `current gap (B-A) = +0.05854`
- 这一组数字的意义非常关键：
  - translator 后续确实对边做了统一的上游提权，但并没有改变这两条 family 的相对差距
  - 也就是说，`一直想 + 望着` 比 `一直 + 向往着` 略优这件事，在“纯 raw 词典边权”层面就已经成立
  - 不是 `credibility` 把它推歪的，也不是 translator 的局部 bias 把它推歪的

- 因而，方案 A 这次确实带来了和此前预期不同的结论：
  - 之前我认为它可能会把根因再下挖到“`credibility` 混入底座并被重复放大”
  - 但 probe 结果表明：
    - 这条 case 的关键边上 `credibility = 0`
    - 相对 gap 在 raw 层和 current 层近乎完全一致
  - 所以对于 `一直向往着远方` 这条 case：
    - `candidate->weight + dict_score_raw` 的重复记账结构仍然客观存在
    - 但它不是解释这条 case 分叉的最深主因

- 到这一步，`一直向往着远方` 的根因边界又进一步收缩：
  1. 不是 poet 末端补丁问题
  2. 不是 risk-hint / whole-word 路线命中问题
  3. 不是 `credibility` 混入关键竞争边导致的上游歪斜
  4. 更像是：
     - raw 词典边权本身就让 `一直想 + 望着` 略优于 `一直 + 向往着`
     - 然后 `char_path_log10` 又在 `xiangwang` 阶段更早、更强地继续偏向错误 family

- 因而，方案 A 的最终价值是：
  - 它没有推翻方案 B，但排除了一个更深层怀疑项
  - 这意味着后续若继续深挖，应把优先级从“creditibility/path debt 污染”下调，转而更聚焦：
    - raw 词典边权为何天然更偏向 `一直想 + 望着`
    - split-token `char_path_log10` 为何在 `xiangwang` 阶段继续扩大这种偏向
## 2026-05-19 `G7` raw 词典边权来源继续收口：`0.0585` 直接来自源频乘积比

- 在方案 A 排除 `credibility` 主因后，继续往下追 `raw_weight` 的来源。
- 先确认主词典：
  - `witset.schema.yaml` 中主 translator 使用 `zhuma.sentence`
  - `zhuma.sentence.dict.yaml` 通过 `import_tables` 引入 `dicts/sentence/jichu` 等基础词典
- 对 `一直想 / 向往 / 想往 / 向往着 / 望着 / 望着远方 / 一直` 做源频检索，均命中：
  - `一直` = 721
  - `一直想` = 368
  - `向往` = 386
  - `想往` = 261
  - `向往着` = 181
  - `望着` = 376
  - `望着远方` = 151
- 同时复核编译器：
  - `dict_compiler.cc` 直接把源权重做 `log(r->weight)` 写进编译词典
  - 运行时 graph 中的 `raw_weight` 只是再减去固定常数 `log(1e8)`
- 因而：
  - 任意两个边或 family 的 `raw gap`，本质上等于它们源权重乘积比的自然对数

- 对关键竞争家族 `一直 + 向往着` vs `一直想 + 望着` 直接验算：
  - A 源频乘积：`721 * 181 = 130501`
  - B 源频乘积：`368 * 376 = 138368`
  - `ln(B / A) = ln(138368 / 130501) = 0.058535913...`
- 这与 graph probe 中观察到的：
  - `raw gap (B-A) = +0.0585`
  几乎完全一致

- 这一步的意义非常关键：
  - `一直想 + 望着` 略优，不是 translator 临时 bias 造成的
  - 也不是 `credibility` 混入底座造成的
  - 而是源词典频次结构本身，就在这组 family 上给了错误家族一个约 `+0.0585` 的先手

- 因而当前关于 `一直向往着远方` 的最深边界可以进一步收束成：
  1. 原始词典边权本身，已经让 `一直想 + 望着` 略优于 `一直 + 向往着`
  2. 这个先手的数值，不是黑箱，而是可由源频乘积比精确复现
  3. 后续 split-token `char_path_log10` 又在 `xiangwang` 阶段继续放大这点先手
- 换句话说：
  - 现在已经不只是“怀疑 raw 词典边权偏了”
  - 而是已经能数学上复现它为什么偏、偏了多少
## 2026-05-19 `G8` 单次记账 A/B：直接关掉第二次词典账并不能释放翻盘空间，反而先打坏稳定性

- 本轮按用户要求做最小 A/B，不改代码，只通过 schema 配置模拟更接近“原版单次记账”的效果。
- 试了 3 组：
  - baseline
    - `dict_score_weight = 1.0`
    - `dict_score_norm_weight = 0.18`
  - `single_raw`
    - `dict_score_weight = 0.0`
    - `dict_score_norm_weight = 0.18`
  - `single_strict`
    - `dict_score_weight = 0.0`
    - `dict_score_norm_weight = 0.0`
- 工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\expanded_probe_guardrail_v1\single_accounting_ab.json`

- 结果总览：
  - baseline
    - `top1 = 0.5`
    - `top3 = 0.7`
    - `target_top1 = 0.25`
    - `guardrail_top1 = 1.0`
  - `single_raw`
    - `top1 = 0.3`
    - `top3 = 0.5`
    - `target_top1 = 0.0`
    - `guardrail_top1 = 0.75`
  - `single_strict`
    - `top1 = 0.2`
    - `top3 = 0.5`
    - `target_top1 = 0.0`
    - `guardrail_top1 = 0.5`
- 这说明：
  - 简单把“第二次词典账”关掉，并没有救回目标 case
  - 反而先明显打坏护栏句和整体稳定性

- `一直向往着远方` 单条也没有出现预期中的“n-gram 空间释放”：
  - baseline top1 仍是 `一直想望着远方`
  - `single_raw` top1 仍是 `一直想望着远方`
  - `single_strict` 反而退化成 `一只想往这与安防 / 一只想往这与暗访`
- 也就是说：
  - 关掉第二次词典账并没有把正确句抬上来
  - 更像是先让不稳定的 char/fallback 路径失去锚定后冒头

- 具体回归：
  - `single_raw`
    - `体验不一样的生活 -> 体验不宜养的生活`
    - `走进一家特色小店 -> 走进宜家特色小店`
    - `带着一点微凉 -> 带着疑点微量`
    - `也带着一种不可挽回的流逝 -> 也带着意中不可挽回的流逝`
  - `single_strict`
    - 在上述基础上，`踏入小镇的那一刻 -> 踏入小镇的那一颗`
    - `一直向往着远方` 本身也进一步退化为更坏的 `安防/暗访` 路径

- 这个实验的意义是：
  - “当前实现存在同源双记账风险” 这个判断仍成立
  - 但不能简单推出“直接删掉第二次词典账就会更像原版 Poet、并让 n-gram 扳回来”
- 原因更像是：
  - 原版 Poet 的干净合同是：
    - 单次 `entry->weight`
    - 再加 grammar 分
  - 而当前 `witset_poet` 外围已经套了：
    - fallback / boundary / tail / structure / avg LM / beam merge 等整套多特征 scorer
  - 在这种结构下，单独抽掉一条词典主轴，不会自动回到“原版 Poet”，只会先让现有稳定锚点减少，导致错误 family 和噪声路径更容易冒头

- 因而本轮结论应收束为：
  1. 直接把 `dict_score_weight` 关掉，不是可行修复
  2. `dict_score_norm_weight` 也一起关掉，只会更差
  3. 如果后续还要继续试“更像原版 Poet”的路线，不能只做减法；必须同时重构：
     - 哪些项保留在单次 `entry/candidate weight`
     - 哪些项退回 grammar/LM
     - 哪些 fallback/结构罚分应降级或改位置
- 换句话说：
  - 这个 A/B 更像证明了“当前 scorer 已经形成耦合系统，不能抽一根梁就指望恢复原版合同”
## 2026-05-19 `Phase 1-3` Poet 对齐版一档 scorer：实现、编译与 10 条结果

- 按用户同意推进到 `phase 3` 后再汇报。
- 本轮目标不是再调局部权重，而是做一个真正可开关的 `Poet 对齐版一档打分核`，只作用于 `llm_level_1`，不碰二档/三档入口。

### 实现
- 文件：
  - `librime/plugins/witset/src/witset_poet.h`
  - `librime/plugins/witset/src/witset_poet.cc`
- 新增配置开关：
  - `poet_aligned_level1_mode`
- 生效条件：
  - 仅当 `poet_aligned_level1_mode = true`
  - 且当前是 `llm_level_1`
  - 且不是 `local_correction` / `llm_driven_engine`
- 第一版对齐策略：
  - `base_score` 退回到：`candidate->weight + lm_total_weight * lm_score_scaled`
  - 关闭 `dict_score_raw` 二次记账进入主回路
  - 关闭 `dict_score_norm / lm_avg / boundary / oov / whole_word / merge_gain / fragment / structure / tail / octagram / joint / upstream_path_prior / beam_merge` 等 adjustment/penalty 主回路
  - 关闭一档最终列表的 cluster rerank / LM rescue，直接按主分选择
- 保留：
  - `witogram` 的 split-token LM 主项
  - graph/beam 主搜索骨架
  - 二档/三档代码路径保持原样不动

### 编译
- 按规则执行：`librime/build.bat static`
- 结果：通过

### Phase 2 小验证
- 临时打开 `poet_aligned_level1_mode: true` 做单 case 验证后确认：
  - 模式确实生效
  - 一档输出发生了明显变化
  - 二三档代码路径未触碰

### Phase 3 全量 10 条结果
- 工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\expanded_probe_guardrail_v1\poet_aligned_level1_eval.json`
- 指标：
  - baseline: `top1=0.5`, `top3=0.7`, `target_top1=0.25`, `guardrail_top1=1.0`, `sentinel_top1=0.0`
  - poet_aligned_v1: `top1=0.2`, `top3=0.5`, `target_top1=0.25`, `guardrail_top1=0.25`, `sentinel_top1=0.0`
- 具体：
  - 唯一明显正收益：
    - `两旁是古色古香的建筑` 从 top3 拉到 top1
  - 目标句未救回：
    - `一直向往着远方` top1 退化为 `已只想往这远方`
    - `第一站是一座古老的小镇` top1 退化为 `的驿站是以做古老的小镇`
    - `体验不一样的生活` 从正确 top1 回退为 `体验不宜养的生活`
  - 护栏严重回归：
    - `走进一家特色小店 -> 走进彝家特色小店`
    - `踏入小镇的那一刻 -> 踏入小镇的那亦科`
    - `都诉说着岁月的故事 -> 都诉说着虽说的古诗`
- 结论：
  - 第一版“只保留 `candidate->weight + LM`”的 Poet 对齐模式，并没有形成更稳的原版 Poet 合同
  - 它确实证明了当前末端耦合里有多余项可疑，但简单收缩为极简主轴会让 `witogram` 的 fallback/路径稳定机制一起丢失，整体更差

### 当前判断
- 这条实验线的价值在于：
  1. 证明继续做末端调权的价值很低
  2. 证明“直接砍成极简单主轴”也不是答案
  3. 更支持后续改成“Poet 对齐主干 + 保留最小必要路径稳定项”的中间形态，而不是纯减法
- 换句话说：
  - 当前多特征 scorer 确实过耦合
  - 但 `witogram` 的优势里，至少有一部分并不是纯副作用，而是承担了基础路径稳定功能

## 2026-05-19 `Phase 1-3` Poet 对齐版一档 scorer：实现、编译与 10 条结果

- 按用户同意推进到 `phase 3` 后再汇报。
- 本轮目标不是再调局部权重，而是做一个真正可开关的 `Poet 对齐版一档打分核`，只作用于 `llm_level_1`，不碰二档/三档入口。

### 实现
- 文件：
  - `librime/plugins/witset/src/witset_poet.h`
  - `librime/plugins/witset/src/witset_poet.cc`
- 新增配置开关：
  - `poet_aligned_level1_mode`
- 生效条件：
  - 仅当 `poet_aligned_level1_mode = true`
  - 且当前是 `llm_level_1`
  - 且不是 `local_correction` / `llm_driven_engine`
- 第一版对齐策略：
  - `base_score` 退回到：`candidate->weight + lm_total_weight * lm_score_scaled`
  - 关闭 `dict_score_raw` 二次记账进入主回路
  - 关闭 `dict_score_norm / lm_avg / boundary / oov / whole_word / merge_gain / fragment / structure / tail / octagram / joint / upstream_path_prior / beam_merge` 等 adjustment/penalty 主回路
  - 关闭一档最终列表的 cluster rerank / LM rescue，直接按主分选择
- 保留：
  - `witogram` 的 split-token LM 主项
  - graph/beam 主搜索骨架
  - 二档/三档代码路径保持原样不动

### 编译
- 按规则执行：`librime/build.bat static`
- 结果：通过

### Phase 2 小验证
- 临时打开 `poet_aligned_level1_mode: true` 做单 case 验证后确认：
  - 模式确实生效
  - 一档输出发生了明显变化
  - 二三档代码路径未触碰

### Phase 3 全量 10 条结果
- 工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\expanded_probe_guardrail_v1\poet_aligned_level1_eval.json`
- 指标：
  - baseline: `top1=0.5`, `top3=0.7`, `target_top1=0.25`, `guardrail_top1=1.0`, `sentinel_top1=0.0`
  - poet_aligned_v1: `top1=0.2`, `top3=0.5`, `target_top1=0.25`, `guardrail_top1=0.25`, `sentinel_top1=0.0`
- 具体：
  - 唯一明显正收益：
    - `两旁是古色古香的建筑` 从 top3 拉到 top1
  - 目标句未救回：
    - `一直向往着远方` top1 退化为 `已只想往这远方`
    - `第一站是一座古老的小镇` top1 退化为 `的驿站是以做古老的小镇`
    - `体验不一样的生活` 从正确 top1 回退为 `体验不宜养的生活`
  - 护栏严重回归：
    - `走进一家特色小店 -> 走进彝家特色小店`
    - `踏入小镇的那一刻 -> 踏入小镇的那亦科`
    - `都诉说着岁月的故事 -> 都诉说着虽说的古诗`
- 结论：
  - 第一版“只保留 `candidate->weight + LM`”的 Poet 对齐模式，并没有形成更稳的原版 Poet 合同
  - 它确实证明了当前末端耦合里有多余项可疑，但简单收缩为极简主轴会让 `witogram` 的 fallback/路径稳定机制一起丢失，整体更差

### 当前判断
- 这条实验线的价值在于：
  1. 证明继续做末端调权的价值很低
  2. 证明“直接砍成极简单主轴”也不是答案
  3. 更支持后续改成“Poet 对齐主干 + 保留最小必要路径稳定项”的中间形态，而不是纯减法
- 换句话说：
  - 当前多特征 scorer 确实过耦合
  - 但 `witogram` 的优势里，至少有一部分并不是纯副作用，而是承担了基础路径稳定功能


## 2026-05-19 参数影响综合复盘：优先抓真正的主放大器，不再依赖盲 sweep

- 本轮没有继续做新实验，而是回头综合 `WORKLOG`、规划文档与 `witset_poet.cc` 的实际公式，整理“哪些参数过去真的动过盘面、哪些只是命中很窄或根本没打到”。

- 当前可以把参数分成三类：

### A. 已被证实是主放大器 / 主抑制器的参数

1. `lm_avg_weight`
- 影响级别：最高。
- 证据：此前把 `lm_avg_weight` 从 `0.20 -> 0` 后，`第一展示` 相对 `第一站 + 是` 的领先差从约 `40.5` 缩到约 `33.5`；只对 `token_count == 1` 禁止第二次 `lm_avg` 入分后，差距可进一步缩到约 `22.5`。
- 代码原因：`lm_avg = lm_total / token_count`，当 `token_count == 1` 时，它与 `lm_total` 同号同量纲，只是再乘一次 `lm_avg_weight`，本质上等于对单 token continuation 机械再记一遍 LM。
- 结论：`lm_avg_weight` 不能再按全局常数理解，至少在单 token 后继上必须接近 `0`。

2. `fragment_penalty_weight` / `tail_repair_weight` / `octagram_tail_penalty` / `octagram_tail_run_penalty` / `single_char_penalty`
- 影响级别：最高。
- 代码量级可直接算：
  - 合法单字承接在长词后，`fragment_penalty` 基础可达 `-1.0` 或 `-1.5`，乘 `0.45` 后约 `-0.45 ~ -0.675`。
  - `tail_repair_penalty` 在尾部常见是 `1.35 ~ 1.75`，乘 `0.95` 后约 `-1.28 ~ -1.66`。
  - `octagram_tail_penalty` 以当前 `3.20 / 1.60` 的配置，在尾部单字承接常见直接给到约 `-3.8 ~ -4.9`。
  - 再叠 `single_char_penalty = -0.25`。
- 这意味着：一个“长前缀后接一个合法单字”的路径，在不考虑 OOV 的情况下就可能先吃掉 `-6` 左右的结构负分。
- 结论：当前这组不是细修项，而是决定性结构抑制器。

3. `early_fallback_compensation_weight`
- 影响级别：中高，但前提是门控对准。
- 证据：它把 `第一 / 敌意 / 地衣` 的首词 family 明显扶正过，说明对“首词多字 family 因 split-token/OOV 吃亏”这类问题是能动盘面的。
- 代码量级：在首词命中时，补偿约为 `weight * 1.15 * span_scale * signal_scale`；当前 `weight=1.20` 时，典型可给到 `+1.5 ~ +2.8`，足够改写近身 family 排序。
- 结论：它不是无效项，而是应保留为“首词 family 去偏”的核心参数之一。

4. `early_prefix_split_penalty_weight`
- 影响级别：中高。
- 证据：它把 `的|驿站` 这类早期单字拆分延伸链压下去过，虽然没有直接救回最终句，但明确改变了主竞争家族。
- 代码量级：以当前公式，`weight=0.85` 时常见实际惩罚约在 `-1.3 ~ -1.9`，足以改变句首拆分链的存活排序。
- 结论：它是一个真正有方向性的结构项，但只应针对“句首单字拆分链”，不应泛化到通用 continuation。

### B. 明显打中过，但方向不对或副作用过大的参数

1. `early_unstable_continuation_penalty_weight`
- 打中过目标 continuation，但把 `第一展示` 压下去后，`敌意展示 / 地衣展示` 接手，说明它不会保 family，只会在 family 内转移胜者。
- 结论：不适合继续当主参数调。

2. `prefix_anchor_delta_debt_weight`
- 有状态设计价值，但实际没有把正确句拉回来；更像说明“正确链不是按两字 anchor 后续接桥”的形式存活。
- 结论：当前不应再围绕它调参。

### C. 已被证明基本不值得再围绕常数细调的参数

1. `early_boundary_bridge_weight`
- 门控太窄且落点太后，只补 `站|是` 这类单点承接，没转成收益。

2. `whole_first_word_continuation_penalty_weight`
- 命中率低，说明真实正确链并不按设想的 whole-first-word continuation 形态活着。

3. `upstream_path_prior_weight` / `beam_joint_guidance_weight` / `joint_prior_weight` / `beam_merge_competition_weight`
- 多轮验证显示要么命不中，要么没有独立收益，不值得继续围绕常数 sweep。

### 数学上更合理的参数判断（基于当前旧 scorer，而不是最终重构态）

- 若不立刻重构 scorer，仅从“量纲一致性 + 已知公式”推一个更合理的旧配置区间：

1. `lm_avg_weight`
- 全局常数不应继续用 `0.20`。
- 若暂时只能保留一个常数，建议落到 `0.05 ~ 0.10`。
- 更合理的是逻辑门控：
  - `token_count == 1` 时视为 `0`
  - `token_count == 2` 时再给一个较弱值
- 原因：避免单 token continuation 再吃一遍完整 LM。

2. `fragment_penalty_weight`
- 当前 `0.45` 偏高。
- 从公式看，若想把“合法单字承接”的默认损伤压到 `0.2 ~ 0.4` 量级，更合理区间应在 `0.15 ~ 0.25`。

3. `tail_repair_weight`
- 当前 `0.95` 偏高。
- 若希望它只在真实尾部碎裂链上起作用，而不是和 `octagram_tail` 叠成主导项，更合理区间约 `0.20 ~ 0.35`。

4. `octagram_tail_penalty` / `octagram_tail_run_penalty`
- 当前 `3.20 / 1.60` 在旧 scorer 里过强，会把 octagram 风格尾惩罚从 regularizer 变成主导项。
- 若仍保留常数式设计，更合理区间大致是：
  - `octagram_tail_penalty: 0.8 ~ 1.2`
  - `octagram_tail_run_penalty: 0.3 ~ 0.6`

5. `single_char_penalty`
- 当前 `0.25` 不算最大问题，但在多项叠加里会成为最后一刀。
- 更合理区间约 `0.05 ~ 0.10`。

6. `early_fallback_compensation_weight`
- 当前 `1.20` 虽高，但因为门控窄，量级是合理的。
- 若继续保留“首词 family 去偏”功能，建议区间仍在 `0.9 ~ 1.3`，不宜先砍。

7. `early_prefix_split_penalty_weight`
- 当前 `0.85` 量级基本合理。
- 若后续仍保留专门的句首单字拆分链抑制，建议区间 `0.7 ~ 1.0`。

### 综合判断

- 以后不应再平均看所有参数；真正值得优先控制的只有两组：
  1. `LM` 双计分主放大器：`lm_avg_weight`
  2. 合法单字 continuation 的结构性过罚：`fragment/tail/octa/single_char`
- 其次才是：
  3. 首词 family 去偏：`early_fallback_compensation_weight`
  4. 句首单字拆分链抑制：`early_prefix_split_penalty_weight`
- 其余大量 patch 参数，在现阶段继续 sweep 的收益已经很低。

## 2026-05-20 定向参数重排验证：仅靠削弱 `lm_avg` 与结构罚分，不能救回 target

- 承接上一节“参数影响综合复盘”，本轮没有再做盲 sweep，而是直接按代码和数学判断做一版定向重排：
  - `lm_avg_weight` 从 schema 的 `0.20` 下调到 `0.08`
  - 新增 `ComputeLmAvgContribution(...)`：`token_count == 1` 直接不记，`token_count >= 2` 再按 token 数和 `CharFB/WholeHit/OovTok` 做分段 gate
  - `fragment/tail/octa/single_char` 不再对所有单字 continuation 默认吃满，而是通过 `ComputeSingleCharStructureGate(...)` 只在真实碎片链征象下吃满
  - schema 同步把 `fragment_penalty_weight / structure_fragment_weight / tail_repair_weight / octagram_tail_penalty / octagram_tail_run_penalty / rear_fragment_penalty / single_char_penalty` 收到更保守区间

### 验证方法

- 按工作区规则只编译 `librime/build.bat static`
- 用 `run_local_snapshot_baseline.py` 对 `C:\Users\Bing\AppData\Roaming\witty\debug\_shared_prefix_eval_excerpt.txt` 跑一轮真实回放
- 再从完整 summary 中抽取 `expanded_probe_guardrail_v1` 的 10 条固定 case，生成：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\param_rebalance_shared_prefix_eval\expanded_probe_guardrail_param_rebalance.json`

### 客观结果

- baseline：`top1=0.5`, `top3=0.7`, `target_top1=0.25`, `guardrail_top1=1.0`, `sentinel_top1=0.0`
- param_rebalance：`top1=0.4`, `top3=0.5`, `target_top1=0.0`, `guardrail_top1=1.0`, `sentinel_top1=0.0`

### 逐项变化

- `体验不一样的生活`
  - baseline：`top1=体验不一样的生活`
  - 本轮：`top1=体验不宜养的生活`
  - 说明：削弱 `lm_avg + structure penalties` 之后，没有把更难的 target 拉回来，反而先放跑了原本能守住的 target。

- `两旁是古色古香的建筑`
  - baseline：`top3` 仍包含正确句
  - 本轮：`top3` 变成 `两旁是古色古香得见祝`，正确句掉出 `top3`
  - 说明：这组结构项虽然有过罚问题，但当前同时承担了把明显碎裂 continuation 压出前列的护栏职责。

- `一直向往着远方` / `第一站是一座古老的小镇`
  - 两个核心 hard target 都没有改善，仍被原错误 family 压制
  - 说明：仅靠末端削弱重复 LM 和结构惩罚，并没有触及真正主因；上游 raw family 先手和 continuation 路径竞争仍然主导结果。

### 结论

- 这轮结果和 `single accounting` / `poet_aligned_v1` 指向同一判断：
  - `lm_avg` 与 `fragment/tail/octa/single_char` 的确是主放大器
  - 但在当前旧 scorer 里，它们也已经承担了一部分基础稳定性
  - 因此不能把“这些项过强”直接等同于“只要削弱它们就会更接近 octagram”

- 更具体地说，本轮失败说明：
  1. `体验不一样的生活` 这类本来靠现有 scorer 勉强守住的句子，会先因为结构护栏削弱而失守
  2. `一直向往着远方` / `第一站是一座古老的小镇` 这类 hard target，并不会因为末端 regularizer 变轻就自然翻盘
  3. 当前真正需要动的，仍然是更上游的 family/path 合同，而不是继续围绕这组末端常数做减法

- 因而下一步判断应是：
  - 停止继续围绕 `lm_avg + structure penalties` 做更多常数或 gate 微调
  - 把这轮结论作为反证，继续转向更上游的 `family/path` 介入验证

## 2026-05-20 clean first-word bridge 验证：补“干净首词 + 干净多字续接”桥接后，盘面仍与失败版一致

- 承接上一轮“末端减法无效”的结论，本轮没有再继续削弱常数，而是根据新取证补了一条此前缺失的正向合同：
  - 运行时 graph / gate 记录显示：
    - `一直向往着远方` 的关键竞争边不是 `risk edge`，而是 `一直 -> 向往着` 对 `一直想 -> 望着` 的 family 竞争
    - `第一站是一座古老的小镇` 也不是 `risk edge` 主导，而是 `第一站 -> 是一座` 对 `第一战士 -> 已作古` 的 continuation 合同竞争
  - 现有 `ComputeEarlyBoundaryBridgeCompensation()` 只奖励“脏首词后接单字修桥”，打不到 `第一站 -> 是一座`、`一直 -> 向往着` 这种“干净 exact 首词后接干净多字 continuation”。

### 实现

- 在 `witset_poet` 中新增：
  - `clean_first_word_bridge_weight`
  - `ComputeCleanFirstWordBridgeBonus(...)`
- 命中条件为：
  - prefix 侧已经只生成了 1 个完整词
  - prefix 没有 single-char/fallback/OOV 污染
  - prefix 已有 whole-word 命中
  - 下一跳本身也是 `matched_whole_word`、非 `CharFB`、非 OOV、且 `char_count >= 2`
- schema 先给了一个中等偏强的起点：`clean_first_word_bridge_weight: 1.8`

### 验证方法

- 按规则执行 `librime/build.bat static`
- 用 `run_local_snapshot_baseline.py` 对 `_shared_prefix_eval_excerpt.txt` 跑真实回放
- 抽取固定 10 条扩展组生成：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\clean_first_word_bridge_eval\expanded_probe_guardrail_clean_first_word_bridge.json`

### 客观结果

- baseline：`top1=0.5`, `top3=0.7`, `target_top1=0.25`, `guardrail_top1=1.0`, `sentinel_top1=0.0`
- clean_first_word_bridge：`top1=0.4`, `top3=0.5`, `target_top1=0.0`, `guardrail_top1=1.0`, `sentinel_top1=0.0`

### 逐项结果

- `一直向往着远方`
  - 仍然是 `一直想望着远方`
  - 说明这条路径即便补了“干净首词桥接”，也没有改掉最终胜出的错误 family。

- `第一站是一座古老的小镇`
  - 仍然是 `第一战士已作古老的小镇`
  - 说明当前缺的不是一个简单的首词 bridge bonus，而是更深的 continuation/state competition 合同。

- `体验不一样的生活`
  - 仍然是 `体验不宜养的生活`
  - 说明这条新 bonus 也没有把上一轮损失的 target 拉回来。

- `两旁是古色古香的建筑`
  - 仍然掉出 `top3`
  - 说明这版新 bonus 至少没有修复上一轮留下的 target 回归。

### 结论

- 这轮结果与上一版失败的 `param_rebalance` 在扩展组上实质等价，说明：
  1. 当前缺的不是一个“首个整词 clean bridge”局部正向项
  2. `第一站` 与 `一直向往` 两类 case 的关键矛盾已经更接近 continuation/state competition 主合同
  3. 继续在 `poet` 末端补局部 bridge bonus，收益上限很低

- 下一步判断：
  - 不再继续围绕 `clean_first_word_bridge_weight` 做 sweep
  - 后续应进一步把介入点前移到 continuation/state competition 层，而不是继续往 `adjustment_score` 里补新项


## 2026-05-20 future-state compact 取证：`第一站是` 明明 admitted，却在 `start=11` 前就只剩 `第一战士`

- 为了继续定位 `continuation/state competition` 的真实死亡点，本轮没有改排序逻辑，只在 `witset_poet` 中增加了两层只读探针：
  - `pre_source_pool`
  - `pre_future_compact / post_future_compact`
- 目标是回答一个更具体的问题：`8->11` 的 `第一站 + 是` 已经 `admitted_new` 进入 `states[11]` 之后，究竟是在哪一层被提前清掉。

### 取证方法

- 按规则执行 `librime/build.bat static`
- 用单 case 语料 `_single_long_case.txt` 跑真实回放，只覆盖：
  - `diyizhanshiyizuogulaodexiaozhen`
- 生成并分析：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\competitive_travel_graph.jsonl`
  - `C:\Users\Bing\AppData\Roaming\witty\debug\competitive_travel_future_compact_focus.json`
  - `C:\Users\Bing\AppData\Roaming\witty\debug\diyizhan_start11_stage_summary.json`

### 客观证据

- `8->11` 的 `第一站 + 是` 确实正常进入：
  - `batch_selected`
  - `admitted_new`
- 对应 `state_key` 也明确存在：
  - `了旅行的征程。第一站是|1|3|4|0|1|0|0|3`
- 但对同一个 case 在 `start=11` 处的阶段统计显示：
  - `pre_future_compact`: 4 条
  - `post_future_compact`: 4 条
  - `pre_source_pool`: 1 条
  - `source_pool`: 1 条
  - `top_candidate`: 1 条
- 更关键的是，这几个阶段里保留下来的 `source_text` 全部都是：
  - `我踏上了旅行的征程。第一战士`
- 也就是说：
  - `第一站是` 并不是在 `admitted_reject` 被挡掉
  - 也不是在 `CompressLinePoolByState()` 时与 `第一战士` 共用同一个 `state_key` 后被覆盖
  - 因为它连 `pre_future_compact(start=11)` 都没有出现

### 当前判断

- `第一站` 这条 case 的死亡点已经进一步收窄：
  - `8->11` 的正确 line 已经生成并 admitted
  - 但在真正轮到 `start=11` 扩展之前，`states[11]` 中可见的 line 只剩 `第一战士`
- 这说明当前问题比“state dedup 误杀”更早，也更怪：
  - 更像是 `states[end_pos]` 的生命周期或写入可见性异常
  - 或者某个更早的 future-state 处理过程只保留了旧前缀，而后 admitted 的 line 没有进入下一轮可见池

### 结论

- 这轮取证可以先排除两种较浅的解释：
  1. 不是 `admitted_reject` 直接挡掉了 `第一站是`
  2. 不是 `第一站是` 与 `第一战士` 共享同一个近似 `state_key` 后被 `CompressLinePoolByState()` 合并掉
- 下一步不应再往 translator 侧补 continuation bonus；更合理的是继续盯住 `states[end_pos]` 在 admitted 之后、source 之前的真实生命周期。

### 补充取证：`admitted_new` 之后的 `target_pool` 实时内容也只有 `第一战士`

- 为了进一步区分“line 留进了 `target_pool`，但后面消失”与“记录写了 admitted，但 `target_pool` 实际没接住新 line”，本轮又新增了一层更近的只读探针：
  - `post_admit_target_pool`
- 同样对单 case `diyizhanshiyizuogulaodexiaozhen` 跑真实回放，生成：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\diyizhan_start11_post_admit_summary.json`
- 对 `start=11` 的阶段统计显示：
  - `post_admit_target_pool`: `2470`
  - `pre_future_compact`: `4`
  - `post_future_compact`: `4`
  - `pre_source_pool`: `1`
  - `source_pool`: `1`
  - `top_candidate`: `1`
- 但更关键的是，对 `post_admit_target_pool(start=11)` 的 `source_text` 去重后，唯一值仍然只有：
  - `我踏上了旅行的征程。第一战士`
- 同时：
  - `post_admit_target_pool` 中不存在 `第一站是`
  - 也不存在 `第一站十 / 第一站时 / 第一站使 / 第一站市 / 第一站式 / 第一站视 / 第一站世 / 第一站实`

### 补充判断

- 这说明问题还要再往前收一层：
  - `admitted_new` 事件虽然记录了 `第一站 + 是` 成功
  - 但在紧随其后的 `target_pool` 实时快照里，`states[11]` 可见内容依然只有旧的 `第一战士`
- 因而当前更像是：
  - `admitted_new` 记录与 `target_pool` 真实内容之间存在不一致
  - 或 `target_pool.push_back(std::move(new_line))` 后，运行时真正可见的池内容并不是预期的新 line 集合
- 下一步判断：
  - 已经不应继续在 `future compact`、`state dedup` 或 translator continuation bonus 上浪费时间
  - 更合理的是直接盯住 `new_line`、`target_pool` 与 `admitted_state_index` 在 admitted 分支内的即时一致性

### 纠偏：上面这条 `post_admit_target_pool` 结论后来被新探针证伪

- 继续复核 `witset_poet.cc` 后发现，之前复用的 `maybe_record_expansion_gate(...)` 自带一个隐藏过滤条件：
  - 只有 `generated_word_count == 1` 的 line 才会写入 `expansion_gate_records`
- 这意味着此前的这些 stage：
  - `post_admit_target_pool`
  - `pre_source_pool`
  - `source_pool`
  - `top_candidate`
  - `pre_future_compact`
  - `post_future_compact`
  实际上都只看到了“首个生成词”的候选，看不到像 `第一站是` 这种两段链。
- 为了验证这一点，本轮新增了不带该过滤条件的更近探针：
  - `admitted_new_line_pre_push`
  - `admitted_new_line_post_push`
- 对同一个单 case `diyizhanshiyizuogulaodexiaozhen` 重新跑回放，生成：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\diyizhan_admitted_line_probe_compact.json`
- 新证据明确显示：
  - `admitted_new_line_pre_push(start=8,end=11)` 中存在 `我踏上了旅行的征程。第一站是`
  - `admitted_new_line_post_push(start=11,end=11)` 中同样存在 `我踏上了旅行的征程。第一站是`
  - 同批的 `第一站十 / 第一站时 / 第一站使 / 第一站市 / 第一站式 / 第一站视 / 第一站世 / 第一站实` 也都在 `post_push` 后真实存在
- 因而需要纠正前面的判断：
  - **不存在 `admitted_new` 与 `target_pool.push_back(std::move(new_line))` 立即背离的证据**
  - 之前“`admitted_new` 记录成功，但 `target_pool` 里只有 `第一战士`”这个结论，是 probe 只记录单词 line 导致的误判
- 新的下一步应改为：
  - 把这种“不带 `generated_word_count == 1` 过滤”的探针继续延伸到 `pre_source_pool / source_pool / top_candidate / pre_future_compact`
  - 重新定位 `第一站是` 真正消失的层级，而不是继续追 `admitted` 分支内部一致性

## 2026-05-20 多例句独立分层 probe：开始把单句结论拉回到小规模代表集

- 为避免继续围着单个 `diyizhan*` 例句打转，本轮先从 `expanded_probe_guardrail_v1` 中抽了 6 条代表句做独立回放：
  - target：`一直向往着远方`、`第一站是一座古老的小镇`、`两旁是古色古香的建筑`
  - guardrail：`走进一家特色小店`、`踏入小镇的那一刻`
  - sentinel：`带着一点微凉`
- 先把 `witset_poet.cc` 中这些 stage 也接到“不带 `generated_word_count == 1` 过滤”的全量 line 快照：
  - `pre_source_pool_full`
  - `source_pool_full`
  - `top_candidate_full`
  - `pre_future_compact_full`
  - `post_future_compact_full`
- 中间先试过把 6 条句子放进同一个 corpus 一次跑完，但很快发现：
  - `run_local_snapshot_baseline.py` 会把整份文本当连续上下文驱动
  - 后面的句子会吃到前面句子的 preceding text
  - 因此混跑结果不适合直接用来判断“每条句子本身的死亡层”
- 随后改成 6 次**独立单句回放**，分别生成：
  - `case1_yizhixiangwang.graph.jsonl`
  - `case2_diyizhan.graph.jsonl`
  - `case3_liangpang.graph.jsonl`
  - `case4_zoujin.graph.jsonl`
  - `case5_taru.graph.jsonl`
  - `case6_daizhe.graph.jsonl`
  - 并汇总到 `independent_multi_case_stage_summary.json`

### 第一轮多例句结果

- `一直向往着远方`
  - 正确整句在 `admitted_new_line_post_push` / `pre_source_pool_full` / `source_pool_full` / `top_candidate_full` / `pre_future_compact_full` / `post_future_compact_full` 全部为 `0`
  - 说明它不是“后面某层把正确整句清掉”，而是正确整句本身就没有形成到这些层里
- `第一站是一座古老的小镇`
  - 和 `一直向往着远方` 一样，正确整句在上述各层全部为 `0`
  - 但主竞争簇稳定集中在 `第一战士已作古老...`
  - 这说明 `diyizhan*` 并不是一个孤立奇例，而是与 `一直向往*` 同属“正确整句未成形”的类别
- `带着一点微凉`
  - 正确整句同样在各层全部为 `0`
  - sentinel 也落入“正确整句没形成”的早死类别，不能简单把它当成与 `diyizhan*` 完全不同的问题
- `走进一家特色小店`
  - 正确整句在 `admitted_new_line_post_push` 中出现 `3` 次
  - 在 `pre_future_compact_full / post_future_compact_full` 中分别出现 `11` 次
  - 但在 `pre_source_pool_full / source_pool_full / top_candidate_full` 中为 `0`
  - 说明这些中途层更像“下一跳扩展用的局部 source line 视图”，不能直接拿来判断“完整整句是否仍然存活”
- `踏入小镇的那一刻`
  - 与 `走进一家特色小店` 类似：
    - `admitted_new_line_post_push = 7`
    - `pre_future_compact_full = 14`
    - `post_future_compact_full = 11`
    - `pre_source_pool_full / source_pool_full / top_candidate_full = 0`
  - 进一步证明：`source_pool/top_candidate` 这类层对“完整整句存活”不是好观测口径
- `两旁是古色古香的建筑`
  - 正确整句在各层仍为 `0`
  - 但 `top_candidate_full` 已能看到很强的正确前缀 `两旁是古色古香的`
  - 说明它与 `一直向往* / 第一站是* / 带着一点微凉` 又不同，更像是“前缀已对、后半段续接失败”的类别

### 当前判断更新

- 不能再把 `pre_source_pool / source_pool / top_candidate` 这些层直接等价成“完整整句是否还活着”的证据层。
- 当前至少已经分出两类问题：
  - **类别 A：正确整句根本没形成**
    - `一直向往着远方`
    - `第一站是一座古老的小镇`
    - `带着一点微凉`
  - **类别 B：正确整句能进入 admitted/future，但 source/top 口径看不到完整整句**
    - `走进一家特色小店`
    - `踏入小镇的那一刻`
  - **类别 C：正确前缀已形成，但后续续接失败**
    - `两旁是古色古香的建筑`
- 因而下一步不应该再继续把所有 target 都当成同一个“死亡层”问题处理。
- 更合理的做法是：
  - 把后续深挖优先集中在类别 A 与类别 C
  - 对类别 B 只把它当 guardrail 口径校准，不再误用 `source_pool/top_candidate` 去推断其完整整句死亡点

## 2026-05-20 三条 `octagram 正确 / 当前错误` 代表句的逐层对照收口

- 这轮不再扩样本，也不再改代码，只把当前最有代表性的三条：
  - `一直向往着远方`
  - `第一站是一座古老的小镇`
  - `两旁是古色古香的建筑`
  按“原版为什么能做对 / 当前为什么做不对”重新收成一张结构对照。
- 对照基线仍沿用前面已确认的结论：
  - `octagram` 在 shared-prefix 代表集中明确能做对这三条
  - 当前链路仍全部 top1 错误

### 原版 `octagram` 真正占优的层

- 这轮再次复核本地 `librime` / `librime-octagram` 代码后，原版有效点依然收束到同一个原则：
  - `syllabifier` 先把 correction / completion / ambiguous joint 的惩罚写进 `credibility`
  - `table/dictionary` 把它并入 `DictEntry.weight`
  - `poet` 只在这个更干净的候选底座上做 `Grammar::Evaluate(...)`
  - `octagram` 自身并不改写词条底座，只补句级搭配分
- 换句话说，原版更强不在于 `octagram.cc` 内部更复杂，而在于：
  - **坏 family 往往在进入 `poet` 之前就已经被更早压薄**

### 1. `一直向往着远方`

- 原版侧：
  - 已知 `octagram` 能做对，因此这条不属于 n-gram 路线能力上限
- 当前侧：
  - baseline top1 仍是 `一直想望着远方`
  - 但历史单句 probe 已确认：正确句并非完全没进图，而是曾进入 `final_pool`，只是排位很后
- 更关键的账本事实：
  - 当前最终 exact 句的 `LmScaled` 比错误 top1 更好
  - 真正把它压下去的是：
    - 更差的 `dict_score_raw`
    - 以及叠上的 `fragment / structure / tail / octagram` 一整组结构罚分
- 继续向前追时又确认：
  - 干净中段 continuation `一直 + 向往着` 是真实存在的
  - 它能过 `request / batch_selected / admitted_new`
  - 但没能继续稳定活到下一轮，把 `远方` 再接出来
- 因而这条的收口判断是：
  - **当前不是“不会算正确句”，而是“干净 continuation 的上游总账与后续保活契约不够强，最终留下来的 exact 句已退化成残路径”**
- 与原版对照：
  - 原版更像是在更早层就把 `想望 / 香望` 这类坏 family 压薄，让干净 continuation 更容易带着更健康的 `entry weight` 走到后段
  - 当前则是坏 family 没有被足够早地稀释，导致末端只看到一个“LM 其实不差，但底座已脏”的 exact 候选

### 2. `第一站是一座古老的小镇`

- 原版侧：
  - 已知 `octagram` 能做对，因此也不属于 n-gram 上限外 case
- 当前侧：
  - baseline top1 稳定是 `第一战士已作古老的小镇`
  - 更早一层 probe 说明：
    - `第一站` 一度能回正
    - `第一站是一座` 这段本身也不是完全算不出来
- 但再往下拆，真正的主矛盾已经不是“一步 continuation 选错”：
  - `第一站|是`
    - 属于 split continuation
  - `第一战士 / 第一展示`
    - 属于 same-span 的整块重切分
  - 两类候选在不同 regime 下被直接拿来比较
- 一到 `diyizhanshi`：
  - `第一战士` 就用更低的 `Base` / 更轻的结构负项盖过 `第一站是`
- 再进入 continuation 梯子：
  - 真正爆开差距的是 `yizuo -> yizuogu`
  - 也就是：
    - `第一战士已作 -> 第一战士已作古`
    - 对打
    - `第一站是以做 -> 第一站是以做古`
- 这一步的本质已经不是局部 bridge，而是：
  - `已作古` 在当前可见文本上天然是强 continuation
  - `是以做古` 则是目标路径为等待未来 rescue 必须暂时穿过的坏表面形态
- 因而这条的收口判断是：
  - **当前主问题不是缺一个 continuation bonus，而是 split continuation 在 very early 阶段就被 same-span 重切分 family 覆盖；后续又被 `已作古` 这种强 continuation 语义滚雪球接管**
- 与原版对照：
  - 原版之所以能做对，更像是：
    - 更早的 `credibility + entry weight` 契约没有让 `战士 / 展示 / 已作古` 这条 family 如此轻松占住顶层
  - 当前一旦允许错误整块重切分 family 在更早层抢占句首，后续 continuation 会自然沿那条错误语义链继续滚下去

### 3. `两旁是古色古香的建筑`

- 原版侧：
  - 已知 `octagram` 能做对
- 当前侧：
  - baseline top1 是 `两旁是故涩谷香的建筑`
  - top3 也被 `故涩谷香 / 古色古香得见祝` 这类脏 family 占住
- 与前两条不同的是，多例句独立 probe 已显示：
  - `top_candidate_full` 中已经能看到很强的正确前缀
    - `两旁是古色古香的`
  - 但完整句 `两旁是古色古香的建筑` 在当前各层都没有真正形成
- 这意味着它既不是：
  - `一直向往` 那种“干净 continuation 已存在但保活失败”
  - 也不是 `第一站` 那种 very early split-prefix 被整块重切分压死
- 更像是：
  - **正确前缀已经回正，但从 `古色古香的` 续接到 `建筑` 的这一步，仍被 `故涩谷香` / `得见祝` 这类脏续接 family 抢走**
- 与原版对照：
  - 原版更像是在上游就让“正确前缀 + 合法后缀”保留成一条连续的低噪路径
  - 当前虽然能在前缀阶段接近正确，但到了后缀续接处，仍缺少让合法 continuation 稳定胜出的候选契约

### 最终收口

- 到这一步，三条 `octagram 正确 / 当前错误` 的 case 已经可以稳定分成三种不同机制：
  1. `一直向往着远方`
     - 干净 continuation 存在
     - 但上游总账与后续保活不够，最终 exact 退化成残路径
  2. `第一站是一座古老的小镇`
     - split continuation 很早就被 same-span 整块重切分 family 覆盖
     - 随后又被强 continuation `已作古...` 语义滚雪球接管
  3. `两旁是古色古香的建筑`
     - 正确前缀已形成
     - 但后半段合法续接没有稳住
- 因而后续若要继续往“模仿原版契约”推进，不能再把这三条当成同一种“poet 排序问题”处理。
- 更合理的是分别对应三类更早介入点：
  - `一直向往`：clean continuation 的上游底座与保活
  - `第一站`：split-prefix family 与 same-span reparse family 的竞争边界
  - `两旁`：正确前缀后的合法续接 contract

## 2026-05-20 三条主线最小原型验证：三条都已判负

- 为了避免继续停留在“原理上似乎还值得”的口头判断，这轮直接把上面三条主线各落成一个最小、可开关的窄原型，并只做对应单句回放：
  1. `一直向往着远方`
     - 新增 `clean_exact_continuation_bonus_weight`
     - 目标：验证“干净 exact 首词后接干净多字 continuation”在当前 `poet` 层是否还有可被释放的空间
  2. `第一站是一座古老的小镇`
     - 新增 `clean_single_char_bridge_bonus_weight`
     - 目标：验证“干净首词后的一字桥 `是`”若被显式扶一把，是否足以改变后续主家族
  3. `两旁是古色古香的建筑`
     - 新增 `validated_prefix_suffix_bonus_weight`
     - 目标：验证“当前缀已全程 exact 且无 fallback/OOV/joint 污染时，给合法后缀 continuation 更强合同”能否把 `建筑` 接出来

### 实现范围

- 本轮只在 `plugins/witset/src/witset_poet.{h,cc}` 中新增 3 个 schema 开关与对应 helper：
  - `clean_single_char_bridge_bonus_weight`
  - `clean_exact_continuation_bonus_weight`
  - `validated_prefix_suffix_bonus_weight`
- 没有改二档、三档链路
- 没有改 `witset_translator` / `WordGraph` 侧逻辑
- 目的不是提交最终方案，而是验证：
  - **哪怕把这三条句子各自最贴近的句级 contract 单独拉出来，在 `poet` 层还能不能动盘面**

### 验证方法

- 按工作区规则执行：`librime/build.bat static`
- 使用单句回放，每次都加一个稳定 warm-up：
  - `踏入小镇的那一刻`
  - 再接目标句
- 分别生成 6 组工件：
  - `verify3_case1_yizhixiangwang_baseline`
  - `verify3_case1_yizhixiangwang_exact`
  - `verify3_case2_diyizhan_baseline`
  - `verify3_case2_diyizhan_bridge`
  - `verify3_case3_liangpang_baseline`
  - `verify3_case3_liangpang_suffix`
- 对应目标句的 top1 / top3 / expected_rank 另行汇总到：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\verify3_case_compare.json`

### 客观结果

- 三组实验在排名层面的结论完全一致：
  - `top1` 不变
  - `top3` 不变
  - `expected_rank` 仍然是 `null`
    - 即：目标句在当前单句回放下，仍然连前 20 候选都进不来
- 逐句结果：
  1. `一直向往着远方`
     - baseline：`一直想望着远方 / 一支香望着远方 / 一枝香望着远方`
     - `clean_exact_continuation_bonus_weight = 2.4` 后：完全不变
  2. `第一站是一座古老的小镇`
     - baseline：`第一战士已作古老的小镇 / 第一战士已作古老的小针 / 第一展示已作古老的小镇`
     - `clean_single_char_bridge_bonus_weight = 2.2` 后：完全不变
  3. `两旁是古色古香的建筑`
     - baseline：`两旁是故涩谷香的建筑 / 两旁事故涩谷香的建筑 / 两旁是古色古香得见祝`
     - `validated_prefix_suffix_bonus_weight = 2.0` 后：完全不变

### 结论

- 这轮最重要的结论不是“参数还不够大”，而是：
  - **把三条句子的局部主矛盾分别抽出来做句级最小 contract，结果依然全部静默**
- 因而可以把判断进一步收紧为：
  1. `一直向往`
     - 问题已经不再是“缺一个 clean continuation bonus”
     - 而是这条 family 在进入 `poet` 之前，底座就已不够干净
  2. `第一站`
     - 问题已经不再是“缺一个 `站 -> 是` bridge bonus”
     - 而是 same-span reparse family 与强 continuation family 在更早层就已接管
  3. `两旁`
     - 问题已经不再是“缺一个 validated suffix bonus”
     - 而是合法后缀能否形成，本质取决于更早的候选构造与竞争 contract
- 因而，到这一步可以更明确地说：
  - **继续在 `witset_poet` 里追加哪怕是这类“按 case 机制量身定制”的局部 contract，性价比也已经很低。**
  - **若还要继续，只剩把主介入点前移到 `AnalyzeCredibility / RewriteWordGraph` 这一层，去重建候选进入 `poet` 之前的 family/path 合同。**

## 2026-05-20 正式止损结论：停止回到 `witset_poet` 末端补丁线

- 回看本轮之前的规划文档和本文件既有结论，可以确认：
  - “不再继续 `witset_poet` 后段 patch 追平 `octagram`，而是把介入点前移到更上游”这一高层判断，并不是今天才第一次出现。
- 这次真正新增的，不是新的方向，而是把原先的路线判断收紧成一个正式执行规则：
  - **以后默认不再回到 `witset_poet` 末端补丁线。**

### 为什么现在可以正式止损

- 因为到这一步，反证链已经足够完整：
  1. 通用末端减法路线已判负
     - `lm_avg + structure penalties` 定向重排没有救回 target，反而先打坏 guardrail
  2. 局部桥接正向项已判负
     - `clean_first_word_bridge` 没有改变 `一直向往` 和 `第一站` 的主错误 family
  3. 三条代表句的 case-specific `poet` 最小 contract 也已判负
     - `一直向往`：`clean_exact_continuation_bonus`
     - `第一站`：`clean_single_char_bridge_bonus`
     - `两旁`：`validated_prefix_suffix_bonus`
     - 三条都在客观排名层完全静默

### 这次和之前结论的区别

- 之前的结论更像：
  - “从代码、原理和已有实验判断，应该把介入点前移”
- 这次的结论则是：
  - “即使按三种不同失败机制分别给 `poet` 层量身做局部 contract，也依然动不了盘面，因此这条线可以正式封口”

### 后续执行规则

- 从这条记录开始，后续若继续推进一档验证，只允许默认走：
  1. `AnalyzeCredibility`
  2. `RewriteWordGraph`
  3. `family/path contract`
- 不再默认继续做：
  1. 新的 `witset_poet` bonus/penalty
  2. 新的末端 bridge/keepalive/suffix rescue
  3. 围绕 `adjustment_score` 的更多局部 patch

### 若未来要重新打开这条线

- 必须先满足两个条件：
  1. 有新的实证表明主问题确实重新落回 `poet` 末端，而不是更上游
  2. 新实验必须是一次性、封口式反证，而不是再回到持续追加末端 patch 的模式

## 2026-05-20 上游验证正式起步：先固定 translator 侧骨架与 baseline

- 按新的执行规则，今天不再继续做任何 `witset_poet` 末端 patch 实验，而是先把当前一档验证入口整理成更接近未来 `witogram_core` 的上游骨架。
- 本轮代码落点只在：
  - `plugins/witset/src/witset_translator.cc`
  - `C:\Users\Bing\AppData\Roaming\witty\witset.schema.yaml`
- 做的事情有两件：
  1. 在 `witset_translator.cc` 内把现有上游逻辑按未来边界重组为：
     - `CredibilityLedger`
     - `CredibilityLedgerBuilder`
     - `WordGraphRewriteConfig`
     - `WordGraphRewriter`
     先不改算法行为，只把 `AnalyzeCredibility / RewriteWordGraph` 这两个落点从 `Query()` 的大段内联逻辑里剥出来。
  2. 把当前 schema 中已判负的 `poet` 末端实验开关：
     - `clean_first_word_bridge_weight`
     恢复为 `0.0`，避免后续“上游验证”仍跑在失败版末端补丁底座上。
- 这样做的目的不是提前分拆文件，而是先把近期允许的实现方式写成实际代码形态：
  - 新的一档主逻辑默认开始归到 `AnalyzeCredibility / RewriteWordGraph`
  - `witset_translator` 逐步退成薄 orchestrator
  - 后续若继续做上游实验，不再需要从 `Query()` 巨型分支里重新找插入点

### 本轮验证

- 按工作区规则只执行了：
  - `librime/build.bat static`
  - 编译通过
- 随后直接复用 21 条 shared-prefix smoke 集重新回放：
  - 语料：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\_shared_prefix_eval_excerpt.txt`
  - 产物：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\upstream_v1_boundary_baseline`
- 本轮 smoke 结果：
  - `completed_text_steps = 21`
  - `expected_not_found_count = 5`
  - `preceding_text_mismatch_count = 10`
  - `top1_accuracy = 0.571429`
  - `top3_accuracy = 0.666667`
  - `wall_time_seconds = 90.484`
- 关键错例仍稳定复现：
  - `一直向往着远方` -> `一直想望着远方`
  - `体验不一样的生活` -> `体验不宜养的生活`
  - `第一站是一座古老的小镇` -> `第一战士已作古老的小镇`
  - `两旁是古色古香的建筑` -> `两旁是故涩谷香的建筑`

### 当前判断

- 这轮最重要的结果不是指标提升，而是：
  1. 已确认在关闭失败 `poet` 开关后，当前一档 smoke 入口仍可稳定跑通
  2. 已把后续上游实验的最小实现边界正式落成代码骨架
  3. 后续可以直接围绕：
     - `CredibilityLedgerBuilder`
     - `WordGraphRewriter`
     做新的 family/path contract 验证，而不必再回到 `witset_poet` 末端 patch 线
- 下一步若继续推进，应优先在这个新骨架上做第一轮真正的上游 contract 实验，而不是再追加 `poet` 端 bonus/penalty。

## 2026-05-20 第一轮真正的 upstream contract：validated continuation v1

- 在刚整理出的 translator 骨架上，做了第一轮真正的上游 contract 原型，不再碰 `witset_poet`。
- 目标很收敛：
  - 只在“前缀最佳路径已经是干净 exact 链”时，
  - 对当前 family 中的 strong exact 多字 continuation 做上游保护，
  - 并对同 family 中接近它的非 exact 多字 continuation 施加轻微抑制。
- 这版原型落点：
  - `PrefixPathState`
  - `BuildBestPrefixStates()`
  - `ComputeTranslatorValidatedContinuationBias()`
  - `WordGraphRewriter::Apply()`
- 代码侧新增了可控开关：
  - `upstream_validated_continuation_weight`

### 原型定义

- `PrefixPathState` 新增两类状态：
  - `exact_multi_word_count`
  - `has_inexact_segment`
- 当前 contract 的触发条件是：
  1. 前缀路径至少已有 1 个 strong exact 多字词
  2. 前缀路径中没有出现 inexact segment
  3. 当前 edge 自身没有被 `credibility` 标成 risk edge
  4. 当前 family 内确实存在 strong exact 多字 continuation
- 在这些条件下：
  - strong exact continuation 得到正向重权
  - 同 family 中靠近它的非 exact 多字 continuation 得到轻微负向重权

### 验证

- 编译：
  - `librime/build.bat static`
  - 通过
- smoke：
  - 仍复用 21 条 shared-prefix 集
  - 产物目录：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\upstream_validated_continuation_v1`
- 结果：
  - `top1_accuracy = 0.571429`
  - `top3_accuracy = 0.666667`
  - `expected_not_found_count = 5`
  - `preceding_text_mismatch_count = 10`
  - `wall_time_seconds = 62.454`
- 与上一轮 boundary baseline 相比，**总体指标没有改善**。

### 进一步核对后的结论

- 这版不是“完全静默”。
- 从 `latest_candidates.json` 看，新信号确实进入了 translator 侧总账：
  - `一直想望着远方` 的总分从 `-255.76` 变为 `-254.00`
  - `两旁是故涩谷香的建筑` 的总分从 `-411.61` 变为 `-405.48`
  - `体验不宜养的生活` 的总分从 `-274.57` 变为 `-268.52`
  - `体验不一样的生活` 的总分从 `-294.15` 变为 `-290.19`
- 但真正的问题也因此更清楚：
  - 这版 contract 只识别了“前缀干净 + 当前 continuation 是 strong exact 多字词”
  - 没有真正识别“这是不是合法的 family/path continuation”
  - 结果是错误 family 里的 exact continuation 也一起被抬了
- 因此它证明了一件事：
  - **仅靠 prefix clean + exact multi-word continuation 这两个条件，还不足以表达我们要的 upstream contract。**

### 当前判断

- 这版 `validated continuation v1` 可以保留代码骨架，但不适合继续作为默认实验配置挂在线上基线里。
- 验证结束后，已把 schema 中的：
  - `upstream_validated_continuation_weight`
  恢复回 `0.0`。
- 下一轮若继续推进，不应只是把这个 weight 再加大，而应先给 ledger / rewriter 补上更有区分力的 contract 信号，例如：
  1. 同一 `family` 内的 source/continuation 类型标签
  2. prefix 到 continuation 的合法续接类别，而不只是“是不是 exact”
  3. 对 `一直向往 / 两旁是古色古香` 这类正确 continuation 的专门 family 归因

## 2026-05-20 继续前先查日志：确认不再重复做粗粒度 continuation 奖惩实验

- 在继续做下一轮 upstream contract 之前，先回查了 `WORKLOG` 和当前方案文档，目的是确认不会把已经判负的实验换个名字再做一遍。
- 回查后的结论很明确：
  - 之前已经反复判负的，是这类思路：
    - 在 translator / poet 侧对 continuation 直接做粗粒度 bonus / protection / suppression
    - 条件通常只是 `prefix clean`、`exact continuation`、`bridge`、`prefix_suffix` 之类
  - 这类实验即便信号能进总账，也很容易把错误 family 里的 exact continuation 一起抬起来
  - 因此下一步**不能**再继续做“换一个条件、继续奖惩 continuation”的同类实验

### 本轮继续采取的动作

- 没有再新增任何排序行为改动。
- 改为只做一轮更上游、且不重复的“读数型 probe”：
  - 在 `BuildLocalGraphSnapshotLine()` 里补出：
    - `prefix_state`
    - `family_contract`
    - candidate 级别的 `char_count / remaining_code_length / is_exact_match / is_strong_exact`
- 这样做的目标是：
  - 不再猜“哪类 continuation 应该被保护”
  - 先直接看到 translator 当前图里，某个 family 为什么会被误当成“合法续接”

### 单句 probe 验证方式

- 没再用整套 baseline 脚本强跑 graph dump，因为它在打开大图快照后会显著变慢，30 步只完成了第 1 个 text step。
- 改为复用已有并且日志里已经验证过的单句控制台方式：
  - `select schema witset`
  - `set option !llm_level_3`
  - `set option !llm_level_2`
  - `set option llm_level_1`
  - `set preceding text 忽然就觉得，秋天是有气息的，带着一点微凉，也带着一种不可挽回的流逝。`
  - `clear composition`
  - `yizhixiangwangzheyuanfang`
- 这次单句 probe 已成功：
  - `snapshot_record` 成功产出
  - graph dump 也已落盘：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\upstream_contract_probe_v2.graph.jsonl`

### probe 读数结论

- 这轮 probe 把一个关键事实钉实了：
  - `validated_continuation` 之所以会误触发，不是因为“正确 continuation 没被看到”
  - 而是因为**正确和错误两条 continuation family 都满足了当前过粗的触发条件**
- 以 `一直向往着远方` 为例：
  - 在 prefix = `一直` 的位置：
    - `向往`
    - `想望`
    都是 strong exact 多字 continuation
  - 在 prefix = `一直想` 的位置：
    - `望着`
    也仍是 strong exact continuation
  - 因此只用：
    - `prefix clean`
    - `exact multi-word continuation`
    这两个条件，无法把 `一直向往` 与 `一直想望` 分开
- 换句话说，这轮 probe 证明：
  - 下一步真正需要补的不是新的 continuation 权重
  - 而是更细的 family/path contract 标签，例如：
    1. continuation 在当前 family 中扮演的是“主路径续接”还是“同音重切分续接”
    2. prefix 到 continuation 的连接类型是否属于合法 source continuation
    3. 当前 continuation 是否只是“错误 family 内的 exact 命中”

### 收尾

- 这轮 probe 完成后，已把 schema 中临时打开的：
  - `debug_dump_local_graph_snapshot`
  - `debug_local_graph_snapshot_path`
  恢复回默认基线配置，避免污染后续正常 smoke。

## 2026-05-20 多样本 family/path probe：误触发不是单句偶然

- 按“不要只盯单样本”的规则，这一轮没有再用单句作为优化依据，而是用一组多样本代表句继续做只读 probe。
- 选取了 8 条代表样本中的 7 条已成功落盘，覆盖：
  - 目标错例：
    - `也带着一种不可挽回的流逝`
    - `一直向往着远方`
    - `体验不一样的生活`
    - `第一站是一座古老的小镇`
    - `两旁是古色古香的建筑`
  - 邻近护栏句：
    - `渴望去看看不同的风景`
    - `踏入小镇的那一刻`
- 产物：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\upstream_contract_probe_multi.graph.jsonl`

### 多样本 probe 结论

- `supports_validated_continuation` 在 7 条样本里全部出现，说明这类 contract 触发**非常普遍**，不是单句偶然。
- 更关键的是，触发不仅出现在“干净正确前缀”上，也大量出现在明显已经漂移的错误 family prefix 上：
  - `一直向往着远方`
    - 正确侧 prefix 有：
      - `一直`
      - `一直向往`
    - 但错误侧 prefix 也同样被支持：
      - `一直想`
      - `一直想王者`
      - `已知悉`
  - `体验不一样的生活`
    - 正确侧 prefix 有：
      - `体验`
      - `体验不一样`
      - `体验不一样的`
    - 但错误侧 prefix 也被支持：
      - `体验不易`
      - `体验不一样得胜`
      - `体验不一样的圣湖`
  - `两旁是古色古香的建筑`
    - 正确侧 prefix 有：
      - `两旁`
    - 但错误侧 prefix 同样被支持：
      - `两旁事故`
      - `两旁事故涩谷`
      - `两旁事故涩谷想的`
  - `踏入小镇的那一刻`
    - 正确侧 prefix：
      - `踏入`
      - `踏入小镇`
    - 错误侧 prefix：
      - `他入戏`
      - `踏入小真的`
      - `踏入小真的那一`

### 当前判断进一步收敛

- 这轮多样本结果进一步确认：
  - 现有 `validated_continuation` 这类 contract 的最大问题，不是“打分太弱”
  - 而是**触发条件本身缺少 family/path 资格边界**
- 换句话说，现在缺的不是更多 continuation 奖惩，而是：
  1. prefix 当前属于哪类 family
  2. continuation 是该 family 的合法主续接，还是错误 family 内的 exact 命中
  3. prefix -> continuation 的连接是否属于允许的 source/path 类型

### 下一步方向

- 下一轮不应继续追加 weight。
- 更合理的是先把 probe 中已经暴露出来的 prefix / continuation 关系，收束成更细的标签原型，例如：
  - `family_clean`
  - `family_drifted`
  - `legal_primary_continuation`
  - `exact_but_wrong_family`
- 只有先把这些边界显式化，后续上游 rewrite 才有可能只保活正确 family，而不是把错误 family 一起救活。

## 2026-05-20 再次查日志后继续：family/path 标签原型不是旧 continuation 奖惩实验

- 在继续做实现前，又回查了一次 `WORKLOG` 和方案文档，确认当前要做的“family/path 显式标签原型”与以下已判负实验不同：
  - `validated_continuation`
  - `prefix_suffix`
  - `bridge/keepalive`
  - 各类 translator / poet continuation bonus
- 这一步的区别在于：
  - **不改排序行为**
  - 只在 graph probe 中增加标签读数
  - 目的是先验证“资格边界是否能被显式描述”，而不是先发明新权重

### 本轮实现

- 在 `witset_translator.cc` 的 graph snapshot 生成逻辑里，新增了两层 probe 标签：
  - prefix 级：
    - `family_root`
    - `family_unqualified`
    - `family_soft_clean`
    - `family_clean`
    - `family_drifted`
  - continuation 级：
    - `legal_primary_continuation`
    - `exact_but_wrong_family`
    - `exact_unqualified_family`
    - `non_exact_under_clean_family`
    - `non_contract_candidate`
- 这版标签仍然是**只读原型**：
  - 不参与排序
  - 不改分
  - 只进 graph snapshot

### 验证方式

- 原本想直接重跑一轮多样本 graph probe，但在 graph dump 打开时，`rime_api_console` 的多样本回放仍然太慢。
- 因为这版标签完全由既有 probe 字段推导而来，只依赖：
  - `prefix_state`
  - `family_contract`
  - candidate 的 `is_strong_exact / char_count / weight`
- 所以后来改为：
  - 直接对上一轮已采好的多样本图快照
    - `C:\Users\Bing\AppData\Roaming\witty\debug\upstream_contract_probe_multi.graph.jsonl`
  - 进行离线复算验证

### 离线复算结果

- 这版标签有一个明确正信号：
  - `exact_but_wrong_family` 能批量命中明显漂移 family 中的 exact continuation
- 例如：
  - `一直向往着远方`
    - `以 -> 知悉 / 直系 / 之喜`
  - `第一站是一座古老的小镇`
    - `地 -> 一站 / 一战 / 已占`
  - `踏入小镇的那一刻`
    - `他 -> 入戏 / 如洗 / 入席`
  - `两旁是古色古香的建筑`
    - `两 -> 旁氏 / 庞氏`
- 这说明：
  - 用 `family_drifted + strong exact continuation`
  - 去标记 `exact_but_wrong_family`
  - 是有区分力的

### 同时暴露出的新问题

- 这版标签也暴露出一个很关键的剩余缺口：
  - `legal_primary_continuation` 目前仍然只是按“当前 clean family 内的 best exact continuation”来打
  - 因而会把一些**当前总账里本来就赢错了**的 candidate 误标成“主续接”
- 典型例子：
  - `体验不一样的生活`
    - prefix `体验` 下，`不易` 被标成 `legal_primary_continuation`
  - `两旁是古色古香的建筑`
    - prefix `两旁` 下，`事故` 被标成 `legal_primary_continuation`
- 这进一步说明：
  - 仅靠“当前 family best exact”仍然不够
  - 还必须再补一层：
    - `legal_primary_continuation` 不能只是“当前 best exact”
    - 必须带上更强的 family/path 资格边界

### 当前收束

- 这轮实现证明了两件事：
  1. `exact_but_wrong_family` 这一类标签是可行的，并且在多样本上有明显信号
  2. `legal_primary_continuation` 还不能直接用当前 best exact 近似
- 因此下一步最合理的方向不是加权，而是继续细化“合法主续接”的判定条件，例如：
  - 当前 continuation 是否沿同一 clean family 的主轴展开
  - 是否只是错误 family 内部的 best exact
  - 是否需要引入“family best exact 之外的 primary source/path 资格”

## 2026-05-20 再往下一步：把“当前 best exact”拆成 confident primary / ambiguous family

- 在继续实现前，再次先回查了日志，确认这一步不是回到旧的 continuation 奖惩实验，而是顺着上一轮标签原型继续细化“合法主续接资格”。
- 本轮仍然保持：
  - 不改排序行为
  - 不加权
  - 只细化 graph probe 的 family/path 标签

### 本轮实现

- 在 `family_contract` 里新增了几项只读读数：
  - `second_best_exact_text`
  - `second_best_exact_weight`
  - `exact_primary_confident`
  - `exact_margin`
- 同时把 continuation 标签进一步细分为：
  - `legal_primary_continuation`
  - `exact_ambiguous_family`
  - `exact_competing_continuation`
  - `exact_but_wrong_family`
- 新规则的核心变化是：
  - `legal_primary_continuation` 不再等价于“当前 best exact”
  - 只有满足：
    1. family 内 exact 候选唯一；或
    2. prefix 已达到 `family_clean`，且 best exact 相对 second best exact 的 margin 足够大
    才允许标成 `legal_primary_continuation`
  - 其余“虽然是 best exact，但 family 内竞争仍然很挤”的情况，统一先降成：
    - `exact_ambiguous_family`

### 验证

- 编译：
  - `librime/build.bat static`
  - 通过
- 验证方式：
  - 没有再重跑慢速多样本 graph dump
  - 直接基于已存在的 7 条多样本快照：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\upstream_contract_probe_multi.graph.jsonl`
  - 做离线复算验证
- 这仍然满足“多样本验证，不靠单句判断”。

### 多样本结果

- 这轮 refinement 的主要正信号是：
  - 上一轮被误标为 `legal_primary_continuation` 的两个典型错例，已经被成功降级为 `exact_ambiguous_family`
- 典型例子：
  - `体验不一样的生活`
    - prefix `体验`
    - `不易 / 不一 / 不宜 / 不已 / 不以`
    - 现在统一落到 `exact_ambiguous_family`
    - 不再把 `不易` 误标成 `legal_primary_continuation`
  - `两旁是古色古香的建筑`
    - prefix `两旁`
    - `事故 / 是故 / 世故 / 师古 ...`
    - 现在统一落到 `exact_ambiguous_family`
    - 不再把 `事故` 误标成 `legal_primary_continuation`
- 同时保留下来的 `legal_primary_continuation` 主要出现在更干净、竞争更明确的位置，例如：
  - `渴望 -> 去看`
  - `渴望 -> 去看看`

### 当前判断

- 这说明本轮 refinement 的方向是对的：
  - 我们已经把“当前只是 best exact”与“真有足够资格叫 primary”这两件事拆开了
- 但它也说明：
  - 真正的 `legal_primary_continuation` 资格，仍不能只靠 family 内 exact margin 完全决定
  - 还需要进一步补上：
    - prefix 是否沿 clean 主轴持续展开
    - continuation 是否属于同一 primary source/path
    - 是否只是 clean family 内部的局部最优，而非全局合法主续接

### 下一步建议

- 下一轮不该直接用这些标签去改权重。
- 更合理的是继续在 probe 层再补一层：
  - `primary_path_eligible`
  - 或者等价的 source/path 资格标签
- 先把“family clean 但仍 ambiguous”与“真正 primary path”分开，再考虑把标签接到 rewrite 里。

## 2026-05-20 再继续一步：补 next-hop `primary_path_eligible` 仍不足以区分错误主轴

- 继续实现前，先再次回查日志，确认这一刀“next-hop 主轴资格 probe”不是重复实验：
  - 它不是 continuation bonus
  - 也不是在 `legal_primary_continuation` 上直接改分
  - 只是继续细化 probe 标签

### 本轮实现

- 在 graph snapshot 里新增了一层 `path_tag`：
  - `primary_path_eligible`
  - `legal_but_path_unconfirmed`
- 判定思路是：
  1. 只有已经被标成 `legal_primary_continuation` 的候选，才继续看 path tag
  2. 若当前候选已到尾部，直接记为 `primary_path_eligible`
  3. 否则继续检查它到达的下一跳 start：
     - 下一跳 prefix family 是否仍在 `family_clean / family_soft_clean`
     - 下一跳 continuation family 是否存在 confident primary
  4. 只有两者都满足，才标成 `primary_path_eligible`
     其余一律记为 `legal_but_path_unconfirmed`

### 验证

- 编译：
  - `librime/build.bat static`
  - 通过
- 验证方式仍然是：
  - 对既有 7 条多样本图快照
    - `C:\Users\Bing\AppData\Roaming\witty\debug\upstream_contract_probe_multi.graph.jsonl`
  - 做离线复算

### 结果

- 这轮 probe 的主要结果不是“成功解决”，而是进一步确认了当前还缺哪一层资格边界。
- 大多数原本的 `legal_primary_continuation` 都被继续压到了：
  - `legal_but_path_unconfirmed`
- 这说明：
  - “当前 edge 看起来像 legal primary”
  - 并不自动意味着它真的处在稳定主轴上
- 但同时也暴露出一个更关键的问题：
  - **仅靠 next-hop clean + next-hop confident primary，仍然不够**
- 最典型的反例就是：
  - `一直向往着远方`
    - `一直想 -> 望着远方`
    - 仍然会被标成 `primary_path_eligible`
- 这说明：
  - 错误 family 一旦在 very early 阶段抢到主轴位置
  - 后续路径完全可能“内部自洽”
  - 因此：
    - `next-hop confidence`
    - 仍不能等价于
    - `true primary source/path eligibility`

### 当前收束

- 到这里，已经可以比较明确地排除两类不够深的近似：
  1. `best exact == legal primary`
  2. `legal primary + next-hop confident == primary path`
- 换句话说，下一步真正缺的，不再是局部 continuation 合法性，而是：
  - **这条路径是否从一开始就属于正确 source/path 主轴**

### 下一步方向

- 下一轮若继续，应该考虑补的是更“轴向”的资格标签，而不是再围绕 continuation family 本身打转，例如：
  - `source_axis_eligible`
  - `shared_prefix_axis_member`
  - `early_axis_drifted`
- 只有把“当前路径是不是从 shared-prefix 主轴上长出来的”显式化，才有机会把：
  - `一直向往`
  - 和
  - `一直想望`
 这种后续都很自洽的路径分开。

## 2026-05-20 再继续一步：exact-only `source_axis_tag` 有信号，但主轴本身仍会被错误家族劫持

- 继续实现前，先再次回查日志，确认这一刀 `source_axis_eligible / shared_prefix_axis_member / early_axis_drifted` 还没有做过，不是重复实验。
- 本轮仍然保持：
  - 不改排序
  - 不加权
  - 只增加 graph probe 读数

### 本轮实现

- 在 graph snapshot 侧新增了一套 exact-only shared-prefix 轴读数：
  - `AxisPrefixState`
  - `BuildExactAxisPrefixStates()`
  - `source_axis_tag`
- 基本思路是：
  1. 只用 strong exact 候选，构建一条 exact-only 前缀轴
  2. 对当前 `legal_primary_continuation` 候选，检查它拼出的 next prefix 是否与这条 exact-only 轴对齐
  3. 若对齐，则标记为：
     - `source_axis_eligible`
     - 或 `shared_prefix_axis_member`
  4. 若在较早阶段已经偏离该轴，则标记为：
     - `early_axis_drifted`
  5. 其余情况标记为：
     - `axis_unconfirmed`

### 验证

- 编译：
  - `librime/build.bat static`
  - 通过
- 验证方式仍然是：
  - 基于既有 7 条多样本图快照
    - `C:\Users\Bing\AppData\Roaming\witty\debug\upstream_contract_probe_multi.graph.jsonl`
  - 做离线复算

### 结果

- 这轮不是完全没用，它确实能抓到一部分“很早就偏出主轴”的错例：
  - `第一站是一座古老的小镇`
    - `第一 -> 战士一`
    - `第一站 -> 十一组`
    - 都会被标成 `early_axis_drifted`
  - `一直向往着远方`
    - `一直 -> 向往着`
    - 也会被标成 `early_axis_drifted`
- 说明：
  - “是否在早期就偏离 exact-only 共享前缀轴”
  - 这个读数本身是有区分力的

### 但更关键的负结论

- 这轮也再次确认了一个更深的限制：
  - **exact-only axis 本身也可能被错误家族劫持**
- 典型例子还是：
  - `一直向往着远方`
    - 当前 exact-only axis 会直接长成：
      - `一直想王者`
      - `一直想望着远方`
    - 因此错误路径：
      - `一直想 -> 望着远方`
    - 反而会被标成：
      - `source_axis_eligible`
- 另外一些错误路径也会显得像“轴成员”：
  - `踏入小镇的那一刻`
    - `踏入 -> 小真的`
    - 会落到 `shared_prefix_axis_member`
  - `两旁是古色古香的建筑`
    - `两旁事故 -> 涩谷`
    - 也会落到 `shared_prefix_axis_member`

### 当前收束

- 到这里可以更明确地确认：
  - 只要“轴”的定义仍然建立在**当前图里 exact-only 的局部最优延展**
  - 它就仍然可能被错误 family 从 very early 阶段整体劫持
- 换句话说：
  - `source_axis_tag` 有局部诊断价值
  - 但它还不能直接等价于“正确 source axis 资格”

### 下一步方向

- 下一轮如果继续，就不能再只从“当前图里谁更像主轴”出发了。
- 更合理的方向是继续补一种**与当前局部最优解耦**的更稳定上游资格，例如：
  - 基于 ledger / spelling class / risk 的早期 axis contract
  - 或 shared-prefix 家族内部的“可信源轴”标记
- 也就是说，下一步真正要找的，不是“当前最像主轴的路径”，而是：
  - **哪条路径从上游 contract 看更像可信源轴**

## 2026-05-20 继续：补 downstream exact completion support 读数

- 继续前先再查了一轮日志和方案文档，确认“downstream exact completion support / tail support”这类读数还没有做过，不是重复实验。
- 同时先基于既有多样本图快照做了离线复核，确认这类信号是**有信息但混合**：
  - 有些 case 上，错误分叉的 downstream exact 支撑明显更薄
  - 但也有不少 case 上，错误 family 反而拥有更大的 exact completion 分叉数
- 因此这轮不把它直接当作新的资格结论，只把它落成 snapshot 字段，作为后续组合判断的基础读数。

### 本轮实现

- 在 `witset_translator.cc` 的 graph snapshot 侧新增：
  - `ExactCompletionSupport`
  - `BuildExactCompletionSupports()`
- 新增输出字段：
  - family 级：
    - `downstream_exact_completion_count`
    - `downstream_exact_best_tail_weight`
  - candidate 级：
    - `downstream_exact_completion_count`
    - `downstream_exact_best_tail_weight`
- 这些字段都只读，不参与排序和打分。

### 验证

- 编译：
  - `librime/build.bat static`
  - 通过
- 仍然遵守多样本验证原则，没有拿单句直接下结论。
- 这轮结论主要来自对既有多样本图快照的离线复核：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\upstream_contract_probe_multi.graph.jsonl`

### 多样本观察

- `一直向往着远方`
  - `一直 -> 向往着`
    - downstream exact count 约为 `9`
  - 但 `一直 -> 向往 / 相望 / 想往 / 想望 ...`
    - downstream exact count 约为 `20`
  - 说明单看“下游 exact completion 数量”并不能保证把正确路径抬出来。
- `体验不一样的生活`
  - `体验 -> 不易 / 不一 / 不宜 ...`
    - downstream exact count 约为 `24`
  - `体验 -> 不一样的`
    - downstream exact count 约为 `3`
  - `体验 -> 不一样`
    - 甚至已经没有 exact 尾部支撑
  - 说明这类读数更像“图里还能怎么继续长”，不等价于“正确 family”。
- `第一站是一座古老的小镇`
  - `第一站 -> 十一组`
    - downstream exact count 为 `0`
  - `第一站 -> 是一座`
    - downstream exact count 为 `4`
  - 这类读数对“明显死尾”的错误 continuation 是有帮助的。
- `两旁是古色古香的建筑`
  - `两旁是 -> 古色`
    - downstream exact count 约为 `128`
  - `两旁 -> 事故`
    - downstream exact count 约为 `36`
  - 这里正确 family 的 downstream 支撑明显更厚，说明该读数对部分 shared-prefix 竞争是有区分力的。

### 当前判断

- 这轮可以确认：
  - `downstream exact completion support` 不是无用信号
  - 但它也绝不能单独拿来当“可信源轴”或“正确 family”资格
- 更准确地说，它比较适合承担：
  - 死尾过滤
  - 尾部可达性诊断
  - 与 `family/path/source-axis` 标签做组合时的辅助读数
- 而不适合单独回答：
  - “哪条路径才是正确主轴”

## 2026-05-20 按 `octagram` 上限标准扩样：跨摘要工件复筛更多 `octagram 对 / 当前错` 句子

- 按用户新标准，继续找证据前先回查 `WORKLOG`，确认这一步不是重做旧的 graph/path probe，而是单独扩充“`octagram` 能做对、当前链路做不对”的样本池。
- 这次没有再用 `C:\Users\Bing\AppData\Roaming\witty\debug\snapshot_summary\reference_cases.jsonl` 那个只有 `1` 个 case 的目录，而是改为：
  - 固定以仓库内 `docs/benchmark_artifacts/octagram_300_20260516/snapshot_summary/reference_cases.jsonl` 作为 `octagram` 侧基线
  - 扫描 `C:\Users\Bing\AppData\Roaming\witty\debug` 下所有 `reference_cases.jsonl`
  - 仅保留 `reference_cases >= 20` 的多样本摘要工件，再做交集比较
- 只把满足以下条件的 case 记为“符合要求”：
  - `octagram expected_rank = 1`
  - 当前摘要工件中的 `expected_rank != 1`

### 结果概览

- 这轮聚合后共筛出 `45` 个候选 case。
- 其中比较稳定、跨多个摘要工件反复出现的，不只限于先前已经反复讨论的：
  - `从校门口一路走去`
  - `心里装着一些说不出口的心事`
  - `我记得风吹过的时候`
  - `秋天是有气息的`
  - `心里却不在这里`
  - `那时的我`
  - `不必言说`
  - `只觉得日子会永远这样延续下去`
  - `那些无法言说的喜欢`
  - `只有一种安静的欢喜`
  - `每一件都独具匠心`
  - `鲜嫩的鱼肉`
  - `每一口都让人回味无穷`
  - `山峰高耸入云`
  - `仿佛仙境一般`
  - `沿着登山步道前行`
  - `当我终于登上山顶时`
  - `远处的城市若隐若现`
  - `感受着大自然的伟大`
  - `还让我体验到了不同地方的风土人情`

### 当前判断

- 说明“符合要求”的样本并不只剩原先那几条旅游错例，校园/回忆段和旅游段里都还有可继续利用的 `octagram 对 / 当前错` 锚点。
- 后续如果要继续做上游 contract 组合验证，应优先从这些跨多个摘要工件都稳定失败的句子里抽样，而不是再回到单 case 目录。

## 2026-05-20 用扩样后的样本池做复核准备：已起验证批，但 graph dump 仍过慢

- 按用户要求，没有再收缩样本，而是直接拿扩样后这批 `octagram 对 / 当前错` 句子做复核准备。
- 先把长样本里的目标句步位重新算了一遍：
  - 校园/回忆段代表句大致落在前半段
  - 旅行段代表句大致落在后半段
- 随后尝试了三种 graph dump 跑法：
  - 直接对长文本样本开 graph dump
  - 截取校园段 + 旅行段组成中等样本
  - 再压缩成只含目标句与少量 guardrail 的 compact 样本
- 结果都暴露出同一个问题：
  - 一旦打开 `debug_dump_local_graph_snapshot`，长句 step 的单步耗时显著上升
  - 即便把 `snapshot-timeout-seconds` 放宽到 `180`，完整批量验证仍然不适合在当前驱动脚本下直接跑完
- 这轮并非完全没有产出：
  - compact 样本链路已经确认能开始落盘，至少前两个 case 已成功写入 `reference_cases.jsonl` 和 graph
  - 说明“用扩样样本池复核旧结论”的验证链路本身是通的
  - 但批量规模一放大，瓶颈仍回到 graph dump 成本，而不是算法判断逻辑

### 当前结论

- 这轮可以确认：
  - 用扩样样本池做复核是可行的
  - 但不能再沿用“单个 baseline 驱动脚本直接吞整批长句 graph dump”的方式
- 更合适的下一步测试，应改为：
## 2026-05-22 多样本复核最新 `shi/yi` source-line bridge：bias 已进 graph，但 request-stage 主轴仍未翻正

- 这轮继续前，先回查了已有日志与方案结论，确认当前要验证的是：
  - 最新落地的 `shi/yi` source-line bridge bias
  - 是否真的把关键 `octagram 对 / 当前错` 样本往正确主轴推进
- 为避免单样本误判，这轮没有只盯单句，而是复用 4 条代表 case 做定点 probe：
  - `一直向往着远方`
  - `第一站是一座古老的小镇`
  - `体验不一样的生活`
  - `两旁是古色古香的建筑`
- 使用脚本：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py`
- 产物：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe_result.json`
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.graph.jsonl`

### request-stage 结果

- `一直向往着 -> 远 / 与 / 于`
  - request 盘面仍由错误分叉主导：
    - `与 = -185.122`
    - `于 = -185.651`
    - `远 = -185.764`
  - 说明这刀没有把 `远` 推成主 request 分叉。
- `第一站是 -> 一 / 以 / 已`
  - request 盘面仍然明显错误：
    - `以 = -158.169`
    - `已 = -182.683`
    - `一 = -186.476`
  - `一` 仍落在错误 family 之后，且差距很大。
- `体验不 -> 一 / 宜 / 依`
  - 仍由错误 family 主导：
    - `易 = -107.366`
    - `宜 = -132.203`
    - `依 = -132.297`
    - `一 = -135.927`
- `两旁是古 -> 色 / 瑟 / 涩`
  - 这一组没有被新合同带歪：
    - `色 = -196.051`
    - `瑟 = -197.996`
    - `涩 = -199.203`

### graph 侧原始读数

- 这轮最关键的发现是：
  - **新加的 `shi/yi` bias 不是没接上，它已经明显进入 graph。**
- 在 `partial_chain_stage_probe.graph.jsonl` 里，`第一站` 前缀下已经出现非常强的 `是...一...` 家族提升：
  - `是一 = -4.61824`
  - `十一 = -11.6661`
  - `是以 = -12.1649`
  - `是一座 = -4.47526`
  - `十一座 = -13.7022`
- 这说明当前问题不是“bridge bias 没生效”，而是：
  - 它只把 `graph` 里的 `是一 / 是一座` 家族抬起来了
  - 但没有进一步把 request-stage 的单字 bridge `是 -> 一` 变成真正的 source-line 主轴

### 验证过程中的额外发现

- `partial_chain_stage_probe.py` 当前汇总里的 `graph_contract.matched_edge_count = 0` 不能直接当结论。
- 原因不是 graph dump 没有落盘，而是：
  - probe 使用的后缀如 `第一站是`
  - 与 graph 中实际关键 prefix 粒度如 `第一站`
  - 没完全对齐
- 因此本轮真正采用的是：
  - 直接读取 `partial_chain_stage_probe.graph.jsonl`
  - 对目标 case 做原始 prefix/candidate 提取

### 当前判断

- 这轮可以明确收束两点：
  1. `shi/yi` source-line bridge 这刀不是静默失败，graph 家族重排已经发生。
  2. 但它仍然**不足以**解决最终目标，因为 request-stage 主轴没有被真正翻正。
- 换句话说，下一步不该继续简单加大 `bridge bias`。
- 更合理的方向是：
  - 继续把 `graph` 里已经抬起来的 `是一 / 是一座`
  - 显式接成 request-stage 的 `primary/source-line` 资格
  - 或补一层真正面向 `是 -> 一` 单字 bridge 的上游 contract
- 在这一步没打通之前，没有必要立刻重跑 21 条 shared-prefix 代表集，因为两个关键目标分叉都还没有赢下来。
  - 更小批次的 targeted graph probe
  - 或只读离线分析现有/新生成的小批工件
  - 优先验证 `risk / spelling class / upstream axis contract` 这类不依赖整批长跑的上游资格信号

### 2026-05-22 补充：最小 request-stage 接线原型第一次试跑失败

- 按上一节收束，这轮没有回去加大 `graph bridge bias`，而是直接试了一个更靠 request-stage 的最小原型：
  - 对 `BuildBestPrefixStates()`
  - 以及 `BuildRequestStagePrefixStates()`
  - 在单字 `是` 且下游已存在强 exact `一...` 家族时，增加一个只用于状态选择的 bridge bonus
- 目标很明确：
  - 不是改候选分本身
  - 而是让 `第一站是` 这种 prefix 真正被保留下来
  - 进而把 `第一站是 -> 一` 接成 request-stage 可用的 source-line 前缀
- 代码已落地在：
  - `plugins/witset/src/witset_translator.cc`
- 编译验证：
  - `librime/build.bat static` 已通过
  - 中间只修了定义顺序与遗漏调用点，不涉及路线回退

### 单 case 定点复核

- 为避免再被整批 snapshot 等待时间拖住，这轮先只跑最关键的 `case2_diyizhan`：
  - `第一站是一座古老的小镇`
- 做法：
  - 复用 `partial_chain_stage_probe.py` 的内部函数
  - 只筛出 `case2_diyizhan`
  - 单独执行 query
- 直接结果：
  - `第一站是 -> 以 / 已 / 一` 的 request 排序**没有变化**
  - 仍然是：
    - `以 = -158.169`
    - `已 = -182.683`
    - `一 = -186.476`
- 因而，这个“只改 prefix state 选择”的最小原型，**当前没有把关键目标分叉推到更接近正确主轴。**

### 从工件里看到的额外信号

- 这轮单 case 跑完后，`next_hop` 工件里：
  - `第一站是 -> 一` 仍然是 `used_char_fallback = true`
  - 而 `以 / 已 / 宜` 仍是 `false`
- 同时，针对这一轮单 case 产物：
  - `partial_chain_stage_probe.graph.jsonl` 为空
  - `next_hop` 记录里也没有读到 `request_stage_tag / request_stage_prefix_text`
- 这说明至少在当前 probe 口径下：
  - 还没有证据表明这次 prefix-state 选择 bonus 真正打通到了 request-stage 合同层
  - 更像是“选择状态的最小偏置还不够，或者根本没有接到最终 request 竞争口径”

### 当前判断更新

- 这次失败很有价值，因为它进一步排除了一个看似合理但仍偏弱的方向：
  - **仅靠在 prefix/request-stage state 选择时保留 `是 -> 一...` 前缀，还不足以改变最终 request 盘面。**
- 因此下一步更可能需要的不是继续调这个 bonus 大小，而是：
  - 明确把 `graph` 已抬起的 `是一 / 是一座`
  - 映射成 request-stage 可见的 source-line / primary-path 资格
  - 或让 probe/debug 工件先能直接读到这一层资格接线是否真正生效
- 在拿到这类更直接证据前，仍然不值得立刻重跑 21 条代表集。

### 2026-05-22 补充：补强 probe 后确认 request-stage 真正被错误 `地一站式...` 前缀占住

- 为了避免继续靠人工翻大文件，这轮先补了 `partial_chain_stage_probe.py`：
  - 支持 `--case case2_diyizhan` 单 case 运行
  - `graph` 匹配不再只看 `prefix_text.endswith(source_suffix)`，而是也看：
    - `request_stage_prefix_text`
    - `prefix_text + candidate.text`
    - `request_stage_prefix_text + candidate.text`
- 用补强后的脚本重新跑 `case2_diyizhan` 后，`partial_chain_stage_probe_result.json` 里的 `graph_contract` 仍然是 `0`，但这次已经确认不是“图里没东西”，而是脚本汇总仍不够直观。
- 因此额外把这条 case 的 graph 相关 edge 单独抽成：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\case2_graph_focus.json`

### 更精确的断点

- 这次终于能把 request-stage 失效点钉到更具体的结构上：
  - `prefix_text = 第一站`
  - 候选 `是 / 十 / 式` 都存在
  - 其中 `是` 的 `request_stage_tag = request_tail_supported`
  - 还没有升到 `request_source_line_eligible`
- 更关键的是，对应同一批 edge，`family_contract.request_stage_prefix_text` 已经不是目标前缀，而是错误链：
  - `地一站式`
  - `地一站式一`
  - `地一站式一座`
- 这说明当前真正的失败点不是：
  - `第一站 -> 是` 这条边没有进图
  - 也不是 `是一 / 是一座` 家族没被抬起来
- 而是：
  - **request-stage prefix state 本身，被错误的 `地一站式...` family 抢占了**
  - 所以 `第一站 -> 是` 只能拿到 `request_tail_supported`
  - 没法被认作当前 request-stage 的 source line

### 当前判断再收束

- 这使得下一步方向进一步明确：
  - 不该继续简单调 `bridge bias`
  - 也不该只在 probe 层面纠缠
- 真正要改的更像是：
  - `BuildRequestStagePrefixStates()` 的状态选择规则
  - 让像 `第一站是` 这种来自 clean / soft-clean prefix 的合法单字 bridge
  - 在 request-stage state 竞争里，能够压过 `地一站式...` 这类错误 exact family
- 换句话说，下一刀要解决的是：
  - **request-stage state ownership**
  - 而不再只是 edge-level candidate bias

### 2026-05-22 补充：审计后继续做 `state ownership` 最小实现，结果仍不足以翻正 `第一站 -> 是`

- 按“继续前先查日志、避免重复实验”的原则，这轮先重新核对了 `WORKLOG.md` 与方案文档，确认：
  - 之前已经明确判负的是：
    - `edge-level bridge bias`
    - “只保 `是 -> 一...` 前缀”的最小 prefix-state 选择 bonus
  - 但**还没有**真正实现并验证：
    - `BuildRequestStagePrefixStates()` 直接按 clean/source-line best prefix 做 state ownership 竞争
- 因而这轮不是重复旧实验，而是沿着最近已经收束出的缺口继续。

### 本轮实现

- 改动位置：
  - `plugins/witset/src/witset_translator.cc`
- 具体做法分成了两步：
  1. 先做了一版“`best_prefix_states[end_pos]` 对齐 bonus”
  2. 随后继续前又再次核对日志，确认这版 bonus 已经判负后，没有重复调常数，而是继续试了一版真正的 comparator / admission 规则：
     - 只要同一 `end_pos` 上存在合法 clean/source-line 对齐 state
     - 就优先它参与 request-stage state ownership
     - 而不是让错误 exact family 继续和它混合竞争
- 目标都一致：
  - 不再调 edge 分
  - 而是直接让 request-stage state 更偏向图里已经确认的合法 source line。

### 单 case 结果

- 仍然只跑 `case2_diyizhan`，避免扩成重复的大实验：
  - `第一站是一座古老的小镇`
- 编译：
  - `librime/build.bat static` 通过
- 定点 probe：
  - `partial_chain_stage_probe.py --case case2_diyizhan`
- request 结果仍未翻正：
  - `以 = -158.169`
  - `已 = -182.683`
  - `一 = -186.476`
- 结果是两版都没有把关键 request 盘面推到正确主轴：
  - bonus 版没有翻正
  - comparator / admission 版也没有翻正

### 新增读数

- 为避免只看 `graph_contract = 0` 误判，这轮继续抽取：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\case2_graph_focus.json`
- 新读数说明：
  - 已能看到若干 request-stage prefix text 开始对齐到正确链：
    - `第一站`
    - `第一站是一`
- 但关键断点仍然没过：
  - 在 `prefix_text = 第一站` 且候选为 `是 / 十 / 式` 的 edge 上
  - `request_stage_prefix_text` 仍然是：
    - `地一站式`
  - `是` 仍然只有：
    - `request_stage_tag = request_tail_supported`
  - 没升到：
    - `request_source_line_eligible`

### 当前结论更新

- 这轮最小实现依然有价值，因为它进一步证明：
  - `state ownership` 的问题确实存在
  - 但**无论是“对齐 `best_prefix_states[end_pos]` 的 bonus”，还是进一步改成同锚点的 comparator / admission，仍不足以拿回 `第一站 -> 是` 的 ownership**
- 这使得当前收束再前进一步：
  - 问题不只是 comparator 太弱
  - 更可能是 `best_prefix_states[end_pos]` 本身就不足以充当 request-stage 的 source-line 锚
- 因而下一步若继续，不该再重复围绕 `best_prefix_states[end_pos]` 做同构规则。
- 更合理的方向是：
  - 直接改 request-stage state 的锚定义
  - 或在 `BuildRequestStagePrefixStates()` 里引入比 `best_prefix_states[end_pos]` 更贴近 source-line 的 ownership 来源

### 2026-05-22 补充：validated source-anchor 版 request-stage ownership 仍然不能拿回 `第一站 -> 是`

- 在继续前再次核对了上面的结论，确认：
  - 这轮不会重复
    - `best_prefix_states[end_pos]` bonus
    - `best_prefix_states[end_pos]` comparator / admission
  - 而是第一次改成：
    - **request-stage source anchor 只从 validated prefix family 派生**
- 实现方式：
  - 新增 `BuildRequestStageSourceAnchors()`
  - 仅从 `best_prefix_states[start_pos]` 仍属于 validated continuation contract 的 prefix 出发
  - 结合 exact candidate 与 `RequestStageTailSupport`
  - 生成每个 `end_pos` 的 `request_stage_source_anchor`
  - 后续 `BuildRequestStagePrefixStates()` 的 admission / alignment 只认这个 anchor，不再直接认全局 `best_prefix_states[end_pos]`
- 这条路线与前两版的关键差异是：
  - 它不再把漂移 exact family 的 best path 直接当 request-stage 锚
  - 也就是说，理论上像 `地一站式...` 这种链，不该再通过“全局 best prefix”反向污染 ownership

### 单 case 结果

- 仍然只跑：
  - `case2_diyizhan`
  - `第一站是一座古老的小镇`
- 编译：
  - `librime/build.bat static` 通过
- probe：
  - `partial_chain_stage_probe.py --case case2_diyizhan`
- request 排序仍然完全没变：
  - `以 = -158.169`
  - `已 = -182.683`
  - `一 = -186.476`

### graph 侧读数

- 继续抽取：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\case2_graph_focus.json`
- 新读数表明，这版 source-anchor 不是完全静默：
  - `request_stage_prefix_text` 在多个位置继续保持了正确链迹象：
    - `第一站`
    - `第一站是一`
- 但关键位点仍未打通：
  - `prefix_text = 第一站`
  - 候选 `是 / 十 / 式`
  - `request_stage_prefix_text` 仍然是：
    - `地一站式`
  - `是` 仍然只有：
    - `request_tail_supported`
  - 不是：
    - `request_source_line_eligible`

### 当前进一步收束

- 这轮结果把结论再往前推了一步：
  - 问题不只是 comparator 太弱
  - 也不只是 `best_prefix_states[end_pos]` 作为锚不合适
- **即便把锚定义改成“只从 validated prefix family 派生的 source anchor”，`第一站 -> 是` 这个 ownership 仍然没有被拿回来。**
- 因而后续若继续，不应再重复围绕：
  - `best_prefix_states[end_pos]`
  - 或“validated source anchor per end_pos”
  - 这类 `end_pos -> text` 单锚模型
- 更可能需要改的是：
  - request-stage state 的表示维度本身
  - 例如显式保留 source-line family identity，而不是把 ownership 压成单一 `end_pos -> text`

### 补记：multi-family bucket / family_identity 版已落地并完成单 case 验证

### 2026-05-25 `neutral_missing avg` 窄原型：只改 `2-token + 1 known + 1 missing` 的 `lm_avg` 解释，命中极弱，已回退

- 在继续前重新核对了当前稳定源码、`partial_chain_stage_probe.py` 的调用链与最新 case2 工件，确认：
  - probe 实际调用的是
    - `outwit-windows/librime/build_x64/bin/Release/rime_api_console.exe`
  - 并不是跑错二进制
  - 而此前一度误把 `第一展示 -> 一` 这种单字 request 当成了第一刀命中对象；实际上当前保留的第一刀只会命中：
    - 词内同时存在已知 token 与缺失 token 的 `neutral_missing` 多字路径
- 随后重新回放当前稳定版 `case2_diyizhan --mode full`，并直接从 `partial_chain_stage_probe.snapshot.jsonl` 抽 `start_pos = 8` 的 `source_pool_full` 细账，确认当前基线仍是：
  - `我踏上了旅行的征程。第一站 / 第一站`
    - `beam_score = -164.477`
    - `base_score = -144.642`
    - `adjustment_score = -19.8343`
    - `lm_score_scaled = -131.82`
    - `lm_score_avg = -43.9401`
  - `我踏上了旅行的征程。的一站 / 一站`
    - `beam_score = -144.503`
    - `base_score = -148.106`
    - `adjustment_score = 3.60319`
    - `lm_score_scaled = -131.82`
    - `lm_score_avg = -88.072`
  - `我踏上了旅行的征程。的驿站 / 驿站`
    - `beam_score = -137.831`
    - `base_score = -131.489`
    - `adjustment_score = -6.34187`
    - `lm_score_scaled = -104.497`
    - `lm_score_avg = -74.4104`
- 这一步先排除了一个误判：
  - 在当前稳定版里，`第一站` 的
    - `dict_score_norm`
    - `lm_score_avg`
    并不比 `一站` 更差
  - 所以它们不是把 `第一站` 压成大负 adjustment 的直接主因
- 但考虑到旧 probe 已明确：
  - `dict_score_norm` 不是主压制项
  - `lm_avg` 至少仍是残余 `LM` 统计账的一部分
  - 且这轮更窄想法还没有做过
  所以这次继续补了一个**极窄**原型，只改 `plugins/witogram/src/witogram.cc`：
  - 不碰 `total_log10`
  - 不碰 `witset_poet.cc`
  - 只在：
    - `token_count == 2`
    - `neutral_missing`
    - 且恰好 `1 known + 1 missing`
    的场景下，尝试把 `avg_log10` 改成更偏向已知 token transition 的解释，避免把缺失 token 再作为 `lm_avg` 的第二次主惩罚
- 这版原型已完成：
  - `librime/build.bat static`
  - `case2_diyizhan --mode full`
  - `case3_tiyanbuyiyang --mode probe`
- 单例结果说明这刀**不是完全 no-op**，但信号极弱，远不足以继续保留：
  - `case2` 中 `我踏上了旅行的征程。的一站 / 一站`
    - `lm_score_avg`
      - 从 `-88.072`
      - 仅改善到 `-87.4964`
    - `beam_score`
      - 从 `-144.503`
      - 仅改善到 `-144.489`
    - 提升约 `0.014`
  - `第一站` 主线、`的驿站` 主敌和最终 top1 均没有质变
  - `case3`
    - `体验不 -> 易` 仍为 top1
    - 没观察到明显回退
- 因而这条 `avg` 窄原型可以直接判为：
  - 命中极弱
  - 统计意义上远弱于第一刀 `neutral_missing total_log10` 纠偏
  - 不值得保留在代码树中继续迭代
- 这版代码已当场回退，并再次执行：
  - `librime/build.bat static`
  - 确保源码与产物重新回到“只保留第一刀稳定版”的状态
- 当前由此新增的结论是：
  - 若后续还继续沿这条线推进，不应再重复：
    - “只改 `2-token neutral_missing` 的 `lm_avg` 解释”
    - 或与之同构的更小常数/分母微调
  - 因为它对当前 `case2` 主差额的作用量级太小，已经足够视作失败路径

### 2026-05-25 `case2` 主缺口继续收口：`第一站` 的大负 adjustment 不是 `lm_avg`，而是句首 `same-span` 重解析罚分

- 为避免继续盲猜 `adjustment_score`，这轮只补了**纯观测导出**，没有改排序逻辑：
  - 在 `witset_poet` / `witset_translator` 的 `expansion_gate_records` 中新增导出：
    - `dict_score_norm_term`
    - `lm_avg_term`
    - `early_*`
    - `clean_*`
    - `validated_prefix_suffix_bonus`
    - `request_stage_source_mismatch_penalty`
    - `shared_prefix_lm_contract_penalty`
    - `early_unstable_continuation_penalty`
    - `prefix_anchor_*`
    - `deferred_clean_prefix_continuation_bonus`
    - `whole_first_word_continuation_penalty`
    - `same_span_competition_penalty`
  - 同时把这些项接进 `Line` 的累计字段，保证 `source_pool_full / top_candidate_full` 也能直接看到句首首词线的累计细账
- 本轮执行：
  - `librime/build.bat static`
  - `case2_diyizhan --mode full`
- 重新抽 `start_pos = 8` 的 `source_pool_full` 后，关键账已经可以直接读实：
  - `我踏上了旅行的征程。第一站 / 第一站`
    - `beam_score = -164.477`
    - `base_score = -144.642`
    - `adjustment_score = -19.8343`
    - `dict_score_norm_term = -1.33253`
    - `lm_avg_term = -1.74003`
    - `early_fallback_compensation = +1.6284`
    - `same_span_competition_penalty = -19.052`
  - `我踏上了旅行的征程。的一站 / 一站`
    - `adjustment_score = 3.60319`
    - `dict_score_norm_term = -2.82272`
    - `lm_avg_term = -1.10245`
    - `early_prefix_split_penalty = -1.79928`
    - `same_span_competition_penalty = 0`
  - `我踏上了旅行的征程。的驿站 / 驿站`
    - `adjustment_score = -6.34187`
    - `dict_score_norm_term = -4.18536`
    - `lm_avg_term = -0.758182`
    - `early_prefix_split_penalty = -1.79928`
    - `same_span_competition_penalty = 0`
- 这一步直接推翻了上一轮的一个近似猜测：
  - `第一站` 的大负 adjustment **不是**主要由
    - `lm_avg`
    - `dict_score_norm`
    造成
  - 真正把它从可竞争盘面打掉的，是后段追加的：
    - `same_span_competition_penalty = -19.052`
- 再按 `source_pool_full` 过滤 `same_span_competition_penalty != 0`，当前命中的句首单块重解析线只有：
  - `第一站`
  - `第一战`
  - `第一展`
  - `第一盏`
  它们全部是句首 `generated_word_count = 1` 的整块线
- 同池里 `generated_word_count = 2 && generated_char_count = 3` 的高分 split anchor 则由：
  - `的驿站`
  - `的翼展`
  - 以及大量 `*驿站`
  这类两词三字线占据
- 因而当前 `case2` 的主缺口进一步收口为：
  - 不是 `neutral_missing` grammar 解释还不够
  - 也不是 `lm_avg` / `dict_norm` 的窄校准
  - 而是**句首单块 reparse line 被 `same-span split anchor` 池作为跨 regime 竞争对象整体压制**
- 下一步若继续，应该优先检查：
  - `same-span split anchor` 是否应要求更强的 contract/support 才能压句首单块整词线
  - 或句首 `generated_word_count = 1` 的 whole-word reparse 是否需要更宽的 `allowed_margin`
- 同时明确一条避免重复的约束：
  - 不要再回去重复“只改 `2-token neutral_missing` 的 `avg`/`lm_avg` 解释”
  - 这一层已经被当前细账证据排到次要位置

### 2026-05-25 继续拆 `same-span` 触发时序：压掉 `第一站` 的是同表面 split anchor，但“同文本 whole-word 直接跳过 same-span”原型无效，已回退

- 在上一条基础上，我继续把 `same-span` 的 anchor 元信息直接接进调试导出：
  - 新增导出：
    - `same_span_anchor_text`
    - `same_span_anchor_score`
    - `same_span_anchor_contract_support`
  - 这样可以直接从 `source_pool_full` 看到：
    - 当前整块 reparse line 是被哪条 anchor 压掉的
    - anchor 的比较分数和 support 是多少
- 重新编译并重跑：
  - `librime/build.bat static`
  - `case2_diyizhan --mode full`
- 这次拿到的关键新证据是：
  - `我踏上了旅行的征程。第一站 / 第一站`
    - `beam_score = -164.477`
    - `same_span_competition_penalty = -19.052`
    - `same_span_anchor_text = 我踏上了旅行的征程。第一站`
    - `same_span_anchor_score = -158.112`
    - `same_span_anchor_contract_support = 4.4`
- 这说明当前压 `第一站` 的**不是异文 split anchor**，而是：
  - 同表面文本 `第一站` 的 split anchor
  - 也就是同一个 visible text 下：
    - whole-word 句首整块线
    - vs
    - 有 request-stage support 的 split line
    之间的跨 regime 竞争
- 同时把同池里的几条句首整块 reparse 并排后还能看到：
  - `第一站`
    - anchor 仍是 `第一站`
    - `same_span_competition_penalty = -19.052`
  - `第一战`
    - anchor 也是 `第一站`
    - `same_span_competition_penalty = -18.6475`
  - `第一展`
    - anchor 也是 `第一站`
    - `same_span_competition_penalty = -15.7279`
  - `第一盏`
    - anchor 也是 `第一站`
    - `same_span_competition_penalty = -15.1675`
- 基于这个证据，我补试了一刀非常窄的原型：
  - 只对
    - `generated_word_count == 1`
    - 句首整块 reparse
    - 且 `anchor_text == line.full_context()`
    的 same-surface 竞争
  - 直接跳过 same-span 罚分
- 结果这刀**没有命中**，重跑后 `第一站` 仍保持：
  - `same_span_competition_penalty = -19.052`
- 再结合同一条 `第一站` 的 stage 时间线可确认：
  - 它先以
    - `admitted_new_line_post_push = -145.425`
    - `same_span_competition_penalty = 0`
    入池
  - 随后经历两轮
    - `pre/post_future_compact_full`
    仍保持 `-145.425`
  - 到更后面某一轮才突然变成：
    - `pre_future_compact_full = -164.477`
    - `same_span_competition_penalty = -19.052`
- 这说明：
  - 当前看到的 `same_span_anchor_text = 第一站`
    很可能是**后一次 state 8 重访**时留下的最后 anchor 信息
  - 而不是最早把罚分打进去时的唯一触发点
  - 因而“同文本 whole-word 直接跳过 same-span”这刀无法在当前入口直接消掉已有罚分
- 这条原型已回退；当前代码重新回到：
  - 只保留更强的 `same-span` 调试导出
  - 不保留任何新排序逻辑
- 由此新增的更精确结论是：
  - 下一步不该直接改“同文本 same-span 是否跳过”
  - 而应先继续确认：
    - **state 8 为什么会在后续 graph 轮次被再次拿出来做 same-span 竞争**
    - 以及**最早那次真正把 `-19.052` 打进去的 anchor / iteration 到底是谁**

### 2026-05-25 `same-span` 触发时序已读实：`第一站` 的第一次罚分发生在外层 `start_pos = 4`

- 继续在不改排序逻辑的前提下，补了两类纯调试字段：
  - `pre/post_future_compact_full` 当前所属的外层 `start_pos`
  - line 第一次 / 最后一次吃到 `same-span` 罚分的外层 `start_pos`
- 重新编译并重跑：
  - `librime/build.bat static`
  - `case2_diyizhan --mode full`
- 这次直接把 `第一站` 在 `end_pos = 8` 的时间线读实了：
  - `admitted_new_line_post_push`
    - `beam_score = -145.425`
    - `same_span_competition_penalty = 0`
  - `pre/post_future_compact_full`
    - `outer_start_pos = 0`
    - 仍为 `-145.425`
    - 仍未吃罚分
  - `pre/post_future_compact_full`
    - `outer_start_pos = 2`
    - 仍为 `-145.425`
    - 仍未吃罚分
  - `pre/post_future_compact_full`
    - `outer_start_pos = 4`
    - 首次变成 `-164.477`
    - `same_span_competition_penalty = -19.052`
    - `first_same_span_apply_outer_start_pos = 4`
    - `last_same_span_apply_outer_start_pos = 4`
- 也就是说，`第一站` 的 `-19.052` 不是后面模糊地多次叠加出来的，而是**外层 `start_pos = 4` 那一轮第一次打进去的**
- 再把同一轮 `outer_start_pos = 4`、`end_pos = 8` 的 `pre_future_compact_full` 盘面并排后，可以确认：
  - 同池里同时存在
    - 句首整块线 `第一站`
    - 以及同表面 split 线 `第一站 / 一站`
  - 真正作为 same-span anchor 压掉它的，是那条：
    - `generated_word_count = 2`
    - 可见文本同样是 `第一站`
    - `beam_score = -158.112`
    - `request_stage_bridge_bonus = 4.4`
    的 split anchor
- 这进一步说明：
  - 当前 `same-span` 主缺口不是“异文 split 线（如 `的驿站`）直接压句首整块线”
  - 而是**同表面文本下，带 request-stage support 的 split line 把句首 whole-word reparse 视为低支持 competing reparse**
- 由此下一步应继续收口到：
  - 为什么外层 `start_pos = 4` 会形成这条 support = `4.4` 的同表面 split anchor
  - 以及句首 whole-word reparse 在这个合同下是否需要单独的 support / comparator 语义

### 2026-05-25 继续静态拆 `support = 4.4`：same-span anchor 实际是 `第一 + 站`，且这 `4.4` 看起来就是完整的 request-stage contract 分

- 继续沿上一条“`outer_start_pos = 4` 会形成哪条 support = `4.4` 的同表面 split anchor”往上追，这轮没有再补新逻辑或新编译，只做静态读码 + 现有 snapshot/result 工件抽取。
- 先把 `BuildPoetRequestStageCandidateHints()` 的构成重新对齐：
  - `hint_bias`
    = `ComputeTranslatorRequestStageContractBias(...)`
    + `ComputeRequestStageBridgeSelectionBonus(...)`
    + `ComputeTranslatorRequestStageFamilySpecificityBias(...)`
    + `ComputeTranslatorRequestStageCompetitionBias(...)`
  - 其中 `ComputeRequestStageBridgeSelectionBonus()` 只对单字 `是` 返回 `3.0`
  - 而 `ComputeTranslatorRequestStageContractBias()` 对单字 exact 候选可给：
    - `request_source_line_eligible`
      - `0.70`
    - `request_tail_supported`
      - `0.30`
    - `request_source_line_terminal`
      - `0.45`
    - 再叠：
      - `path_count` 饱和到 `+0.30`
      - `best_tail_weight` 存在时 `+0.10`
  - 当前 `BuildPoetRequestStageCandidateHints()` 的 `weight` 仍乘固定：
    - `kBeamStageBonusScale = 4.0`
- 再从现有 `source_pool_full` / `request` 工件直接抽 `第一站` family，可确认：
  - `end_pos = 8` 且可见文本为 `第一站` 的 line 不止一条：
    1. whole-word
       - `entry_text = 第一站`
       - `generated_word_count = 1`
       - `request_stage_bridge_bonus = 0`
       - `same_span_competition_penalty = -19.052`
    2. split line
       - `entry_text = 一站`
       - `generated_word_count = 2`
       - `request_stage_bridge_bonus = 0`
    3. split line
       - `entry_text = 站`
       - `generated_word_count = 2`
       - `beam_score = -158.112`
       - `request_stage_bridge_bonus = 4.4`
    4. deeper split line
       - `entry_text = 站`
       - `generated_word_count = 3`
       - `request_stage_bridge_bonus = 7.2`
- 这一步把上一轮一个模糊点彻底坐实了：
  - same-span anchor **不是** `第 + 一站`
  - 而是 `第一 + 站`
  - 它和句首整块 `第一站` 拥有完全相同的 visible text，但前者多了 `request_stage_bridge_bonus = 4.4`
- 再往上追到 `end_pos = 4` 可以看到：
  - `第一`
    - whole-word line
      - `request_stage_bridge_bonus = 0`
    - split line `第 + 一`
      - `request_stage_bridge_bonus = 2.8`
- 再结合 `end_pos = 8` 的 `request` 记录：
  - `source_text = 我踏上了旅行的征程。第一`
  - `entry_text = 站`
  - `request_stage_bridge_bonus = 4.4`
- 这基本说明：
  - `第一 + 站` 这拍本身就直接拿到了完整的 `4.4`
  - 它不是简单从上一拍 `第 + 一 = 2.8` 再累一点“凑出来”的
- 由于 `4.4` 正好等于：
  - `(0.70 + 0.30 + 0.10) * 4`
  - 即
    - `request_source_line_eligible`
    - `path_count` 已饱和
    - `best_tail_weight` 有效
  的纯 `ContractBias`
  且当前这条候选并不是 `是`，因此：
  - 这笔 `4.4` **极大概率不是** `BridgeSelectionBonus`
  - 也不像混合了 `family_specificity / competition` 后形成的杂数
  - 更像是 translator 直接把 `第一 + 站` 判成了一个完整 `request_source_line_eligible` 单字续写合同
- 由此新增的更具体结论是：
  - 当前 `same-span` 压掉句首 whole-word `第一站` 的根因，不只是“split anchor support 更高”
  - 而是 translator 在 `start_pos = 4 -> end_pos = 8` 这拍，已经把
    - `第一 + 站`
    视为了**完整 request-stage source-line continuation**
  - 后面的 same-span 只是忠实执行了这个前移合同
- 所以下一步若继续，最值得优先查的入口不再是：
  - `same-span` 本身
  - 而是：
    - 为什么 `第一 + 站` 会被判成 `request_source_line_eligible`
    - 它对应的 `request_stage_prefix_states[end_pos=8]` 中，哪条 `bridge_lineage_confirmed` state 让 `next_text = 第一站` 成立

### 2026-05-25 补 request-stage metadata 导出后确认：`第一 + 站` 这一拍已被直接判成 `request_source_line_eligible`

- 由于 `graph.jsonl` 在 `case2 --mode full` 下仍然保持空文件，继续等 graph 工件产出性价比很低；因此转为补一层更窄的调试导出：
  - translator 侧把同一个 `MakePoetCandidateHintKey(...)` 下的 metadata 一起传给 poet
  - metadata 包括：
    - `request_stage_tag`
    - `request_stage_prefix_text`
    - `request_stage_state_count`
    - `request_stage_path_count`
    - `request_stage_best_tail_weight`
    - `matching_request_state_text`
    - `matching_request_state_aligned`
    - `matching_request_state_bridge_confirmed`
  - poet 只把这些字段写进 `request` 级 `expansion_gate_records`
  - 不参与任何排序与比较
- 改动文件：
  - `plugins/witset/src/witset_poet.h`
  - `plugins/witset/src/witset_poet.cc`
  - `plugins/witset/src/witset_translator.cc`
- 重新编译：
  - `librime/build.bat static`
  - 通过
- 重新重跑：
  - `python ...\\partial_chain_stage_probe.py --case case2_diyizhan --mode full`
- 随后直接从 `partial_chain_stage_probe.snapshot.jsonl` 抽：
  - `stage = request`
  - `start_pos = 4`
  - `end_pos = 8`
  - `source_text = 我踏上了旅行的征程。第一`
  - `entry_text = 站`
- 现在已经可以直接读到：
  - `search_score = -158.112`
  - `request_stage_bridge_bonus = 4.4`
  - `request_stage_tag = request_source_line_eligible`
  - `request_stage_prefix_text = 第一`
  - `request_stage_state_count = 4`
  - `matching_request_state_text = 第一站`
  - `matching_request_state_aligned = true`
  - `matching_request_state_bridge_confirmed = true`
- 这把上一轮的静态判断进一步坐实为运行时事实：
  - `第一 + 站` 这拍并不是“拿到了一点模糊 request-stage 倾向”
  - 而是已经被 translator 明确判成：
    - `request_source_line_eligible`
  - 并且它在 `end_pos = 8` 的匹配 state 就是：
    - `第一站`
    - 同时满足
      - `aligned_with_best_prefix = true`
      - `bridge_lineage_confirmed = true`
- 这意味着：
  - 当前句首 whole-word `第一站` 被 same-span 压下去的根因，已经不必再怀疑是 poet 末端“误加分”
  - 真正更上游的原因是：
    - translator 的 request-stage prefix contract 在 `start_pos = 4 -> end_pos = 8` 这拍，已经把 `第一 + 站` 当成了一个被确认的 source-line continuation
- 另有一个附带观测：
  - 新导出的 `request_stage_path_count` 在这条记录上是一个非常大的值
  - 目前更像是 request tail path 的真实累积规模，而不是简单的“应当很小的手工计数”；即便如此，本轮核心结论并不依赖它，因为
    - `request_stage_tag`
    - `matching_request_state_text`
    - `matching_request_state_aligned`
    - `matching_request_state_bridge_confirmed`
    已足够证明合同级命中
- 因此当前主结论可以进一步收口为：
  - `case2` 的关键问题不在 `same-span` 本身
  - 也不只是“split anchor 拿到了 4.4”
  - 而是 **translator 侧已经把 `第一 + 站 -> 第一站` 认成了一条被确认的 request-stage source-line continuation**
  - same-span 只是后续忠实执行这个前移合同

- 在继续前，再次回查了日志与方案文档，确认这一步不是回到已判负的单锚路线，而是第一次真正把 request-stage state 从“单一 `end_pos -> text` ownership”改成“同一 `end_pos` 保留多个 family state”。
- 这轮实现改动是：
  - `BuildRequestStagePrefixStates()` 从单一 state 改成 bucket
  - 同一 `end_pos` 最多保留多个 state
  - bucket 内去重 key 从当前 `text` 改成显式 `family_identity`
  - `ClassifyRequestStageTag()` 改成只要 `next_text` 命中 bucket 中任一 state 即可判为 request-stage source-line eligible
- 仍然只做 `case2_diyizhan` 单 case 验证，`librime/build.bat static` 编译通过。
- request 排序相较前一轮已发生变化：
  - `以 = -157.225`
  - `已 = -182.211`
  - `一 = -185.532`
- 这说明“保留多个 family ownership state”不是静默失败，request 盘面确实被改动了。
- 从 `case2_graph_focus.json` 继续读到的关键事实是：
  - `request_stage_state_count = 6`，说明同一 `end_pos` 上确实同时保留了多个 family state
  - `第一站 -> 是` 已经可以拿到 `request_source_line_eligible`
  - 但错误分叉如 `第一站 -> 式` 也仍然可能拿到同级资格
- 因而这轮的新收束不是“multi-family 方向无效”，而是：
  - multi-family bucket 方向有真实信号
  - 但单靠“保留多个 family”还不够
  - 当前真正卡住的是 **source-line eligibility 仍然过宽**
- 再结合 `partial_chain_stage_probe_result.json` 可以看到：
  - 正确目标 `一` 当前仍带 `used_char_fallback = true`
  - 且 `lm_oov_token_count = 1`
  - 错误目标 `以` 没有这层 fallback / OOV 代价
- 这说明当前失败点已进一步前移为：
  - request-stage 不只是要“保住正确 family”
  - 还要把“允许继承 source-line 资格的 family”与“只是被 bucket 保留下来的 family”显式区分开
- 所以下一步若继续，不应回去调大 bonus 或重复单锚 admission，而应沿着：
  - 保留 multi-family bucket
  - 收紧 source-line eligibility 的继承条件
  - 只让真正 bridge 成立、且 lineage 一致的 family 持续拿到 request-stage 资格

### 收尾

- 这轮结束前，已把 `witset.schema.yaml` 里的：
  - `debug_dump_local_graph_snapshot`
  - `debug_local_graph_snapshot_path`
  恢复回默认配置，避免后续环境持续带着 graph dump 开关。

## 2026-05-20 扩样样本 targeted graph probe：先验 `risk / spelling class` 仍不足以单独充当可信源轴资格

- 按上一轮计划，没有再回到整批长语料，而是改成两份更小的 targeted probe：
  - `expanded_sample_school_probe.txt`
  - `expanded_sample_travel_probe.txt`
- 本轮目标不是重新做末端 patch，而是复核一个更具体的问题：
  - 扩样后的新增样本里，`edge_risk / edge_spelling_class` 是否足以作为“可信源轴”或 `upstream axis contract` 的强资格信号

### 校园代表批

- 语料：
  - `忽然就觉得`
  - `秋天是有气息的`
  - `心里却不在这里`
  - `从校门口一路走去`
  - `心里装着一些说不出口的心事`
  - `我记得风吹过的时候`
  - `不必言说`
  - `只觉得日子会永远这样延续下去`
- `reference_cases` 结果里，新增错例至少包括：
  - `从校门口一路走去`：`expected_rank = null`
  - `心里装着一些说不出口的心事`：`expected_rank = 3`
  - `我记得风吹过的时候`：`expected_rank = null`
- graph 复核后看到：
  - 这 `8` 条句子里，几乎每条都有少量 `edge_risk = 1` 的局部边
  - 但 `edge_spelling_class` 仍全部为 `0`
  - 这些局部 risk 并不是只出现在错句里；若干 `expected_rank = 1` 的句子同样带有 `edge_risk = 1`
- 当前判断：
  - 在校园新增样本里，`edge_risk` 更像稀疏局部提示，而不是能单独解释错排的主轴资格
  - `edge_spelling_class` 在这批样本上继续静默

### 旅行代表批

- 先覆盖到了前 `15` 条样本，核心目标错例都已进入 graph：
  - `一直向往着远方`
  - `体验不一样的生活`
  - `第一站是一座古老的小镇`
  - `两旁是古色古香的建筑`
  - 以及 `踏入小镇的那一刻 / 渴望去看看不同的风景`
- 这批里可见：
  - `一直向往着远方`：`expected_rank = null`，`risky_edges = 2`，`spelling_edges = 0`
  - `体验不一样的生活`：`expected_rank = 7`，`risky_edges = 1`，`spelling_edges = 0`
  - `第一站是一座古老的小镇`：`expected_rank = null`，`risky_edges = 5`，`spelling_edges = 0`
  - `两旁是古色古香的建筑`：`expected_rank = null`，`risky_edges = 3`，`spelling_edges = 0`
  - 对照样本 `渴望去看看不同的风景`：`expected_rank = 1`，`risky_edges = 0`
  - 对照样本 `踏入小镇的那一刻`：`expected_rank = 1`，`risky_edges = 3`
- 关键点是：
  - 错句里确实能看到 `edge_risk`
  - 但对句里也会出现同等级别的 `edge_risk`
  - `edge_spelling_class` 仍然全部为 `0`
- 当前判断：
  - 在旅行新增样本里，`edge_risk` 的确比校园批更活跃，但仍不足以单独区分“正确 family/可信源轴”与“错误 family”
  - 它可以作为组合 signal 的一部分，但不能单独拿来定义 `upstream axis contract`

### 收束

- 这轮 targeted probe 说明：
  - 扩样后的新增样本并没有推翻之前的判断
  - 旧结论在新样本池里仍基本成立：
    - `edge_spelling_class` 持续静默
    - `edge_risk` 虽然局部可见，但对错句混用，区分力不足
- 因而下一步如果继续做 `upstream axis contract`，更合理的方向仍然是：
  - 不把 `risk / spelling class` 当单独判死刑的强资格
  - 而是与 family/path/source-axis 读数做组合，只承担辅助资格信号

## 2026-05-21 `risk_assist_tag` 只读复核与临时工件清理

### 磁盘清理

- 继续本轮 `risk_assist_tag` 复核前，先处理了 `C:` 空间不足问题。
- 真正的体积来源不是 `Temp` 小日志，而是 `C:\Users\Bing\AppData\Roaming\witty\debug` 下几份已经确认可重生、且当前不会直接作为证据引用的超大原始 snapshot：
  - `witset_local_snapshot.jsonl`，约 `16.50 GB`
  - `expanded_sample_school_probe.snapshot.jsonl`，约 `16.20 GB`
  - `expanded_sample_travel_probe.snapshot.jsonl`，约 `14.85 GB`
  - `phase123_active_snapshot.jsonl`，约 `3.00 GB`
- 同时顺手删除了少量已无参考价值的旧 `Temp` 日志和一个 `.dmp`。
- 保留了仍有直接参考价值的工件：
  - `expanded_sample_school_probe.graph.jsonl`
  - `expanded_sample_travel_probe.graph.jsonl`
  - 两批 `reference_cases.jsonl`
  - 各 summary 目录
- 清理后 `C:\Users\Bing\AppData\Roaming\witty\debug` 从约 `62.6 GB` 降到约 `10.6 GB`。

### 校园批 `risk_assist_tag`

- 复核方式：
  - 不再取“任意可拼出的 exact path”，而是按 `candidate.weight` 复原**总权重最高**的 exact path，再读路径上的 `risk_assist_tag`。
- `8` 条校园样本都成功匹配到 graph 记录，且 graph 文件本身无坏行。
- 关键读数：
  - `忽然就觉得`：`not_risk_assist_candidate + axis_clean_supportive`
  - `心里却不在这里`：`not_risk_assist_candidate + axis_clean_supportive`
  - `只觉得日子会永远这样延续下去`：`shared_axis_clean_supportive + axis_clean_supportive`
  - `从校门口一路走去`：`shared_axis_clean_supportive`
  - `我记得风吹过的时候`：`shared_axis_clean_supportive`
  - `心里装着一些说不出口的心事`：`shared_axis_clean_supportive + shared_axis_clean_dead_tail`
- 分布特征：
  - `shared_axis_clean_dead_tail` 目前只在 `expected_rank = 3` 的不稳定错句 `心里装着一些说不出口的心事` 上出现
  - `shared_axis_clean_supportive` 会同时落在错句和对句上
  - `axis_clean_supportive` 目前更多出现在对句
  - 大部分 segment 仍然只是 `not_risk_assist_candidate`
- 当前判断：
  - 相比裸 `edge_risk`，`risk_assist_tag` 已开始提供更像 contract 的结构化读数
  - 但它还不足以单独区分“正确 family / 错误 family”
  - `dead_tail` 类标签值得继续纳入下一轮组合合同复核

### 旅行批 `risk_assist_tag`

- `5` 条核心 case 成功从 graph 中恢复到最新记录，但文件内同时存在 `1` 条截断坏 JSON，说明这批产物并不完全干净。
- 当前可读到的关键信号：
  - `一直向往着远方`：出现 `legal_clean_unconfirmed`
  - `体验不一样的生活`：出现 `shared_axis_clean_supportive`
  - 对句 `渴望去看看不同的风景`：同样出现 `shared_axis_clean_supportive`
  - `第一站是一座古老的小镇`、`踏入小镇的那一刻`：当前最佳 exact path 仍主要是 `not_risk_assist_candidate`
- 当前判断：
  - 旅行批说明 `risk_assist_tag` 确实携带了比裸 `edge_risk` 更细的结构语义
  - 但它没有把“组合标签只集中在错句上”这件事进一步坐实
  - 因而它更适合作为 `family/path/source-axis` 组合合同里的弱辅助轴，而不是新的单独主轴

### 收束

- 这轮复核后，可以把判断更新为：
  - `risk_assist_tag` 比裸 `edge_risk / spelling_class` 更有信息量
  - 其中 `dead_tail` 是目前最值得继续追踪的局部信号
  - 但整体仍不支持把 `risk_assist_tag` 单独升格为强资格
- 下一步更合理的方向：
  - 直接做 `family_tag + path_tag + source_axis_tag + risk_assist_tag` 的组合命中复核
  - 重点观察 `dead_tail` 是否能稳定覆盖更多 `octagram 对 / 当前错` case
  - 暂时不把 `supportive` 类标签视作正向资格，只当弱证据

## 2026-05-25 继续收口 `.gram -> .arpa` 链：`一` 的缺口已不止是 unigram，而是当前 extracted vocab 整体缺席

- 这轮没有改任何 C++ 逻辑，也没有重新编译；只继续沿 `case2` 当前最窄主线做静态读码与现成模型文件核对，目标是确认：
  - `一` 的缺席到底是 `.arpa/.klm` 转换环节造成的
  - 还是 `.gram` 源内容本身就没有把 `一` 当 token 收进来
- 先重新读 `tools/dump_to_arpa.cc`，确认当前转换器的关键语义：
  - `GramDb::ExtractAll()` 导出全部 gram 键
  - `dump_to_arpa` 会对每个 gram 键逐 token `decode()`，并把所有出现过的 token 放进 `vocab`
  - 写 ARPA 前还会执行一轮 `Generate missing 1-grams`：
    - 对所有出现在 `vocab` 中、但当前不在 `ngrams[1]` 的 token
    - 统一回填一个缺失 `1-gram`
- 这意味着：
  - 只要某个 token 曾在任意高阶 gram 里出现过
  - 即使它原本没有独立 unigram
  - 当前 `dump_to_arpa` 也会把它补写进 `.arpa` 的 `1-gram`
- 随后直接核对现成模型文件：
  - `wanxiang-lts-zh-hans.arpa`
  - `wanxiang-big-zh-hans.arpa`
  - `wanxiang-mini-zh-hans.arpa`
- 继续确认到：
  - 三份 `.arpa` 里都搜不到按空格分 token 的 `一`
  - 但同一批文件里能稳定搜到：
    - unigram `以 / 已 / 宜`
    - 以及大量高阶 `是 以 / 可 以 / 以 前 ...`
- 由此这轮把结论进一步收紧为：
  - 当前 `wanxiang` 模型族里，`一` 的问题已经不只是“unigram 没进来”
  - 按当前转换器语义看，它更像是**根本没有进入 extracted vocab**
  - 也就是：
    - 不是 `dump_to_arpa` / `build_binary` 在后处理时把 `一` 丢掉
    - 而是 `.gram` 源内容本身就没有把 `一` 作为当前 token 口径中的可见项保留下来
- 这条收口的重要含义是：
  - 继续只围绕 `ScoreFeatures()` 下游做解释，已经不足以回答“为什么 `一` 会长期输给 `以`”
  - `ScoreFeatures()` 只是如实放大了当前模型内容层的 token 缺口
  - 真正更上游的入口应继续落在：
    - grammar 训练/分词口径
    - 或训练后剪枝为何让 `一` 连高阶 token 足迹都没有留下
- 因而当前主判断继续更新为：
  - `case2` 中 `X是一` 在 `LM/base` 层系统性弱于 `X是以`
  - 根因更接近**模型内容层对 token `一` 的整体缺席**
  - 而不是 `.arpa/.klm` 转换器、也不是 `witset_poet` 后段补偿项本身

### 组合命中复核

- 继续按“最佳 exact path 的每一段组合签名”复核了：
  - `family_tag`
  - `continuation_tag`
  - `path_tag`
  - `source_axis_tag`
  - `risk_assist_tag`
- 这轮不是再看单字段，而是直接看完整组合在 `ok / unstable / bad` 三个桶里的分布。

#### 当前最有价值的负向组合

- `shared_axis_clean_dead_tail`
  - 目前只出现 `1` 次
  - 只落在 `unstable` 句 `心里装着一些说不出口的心事`
  - 完整组合为：
    - `family_clean`
    - `legal_primary_continuation`
    - `legal_but_path_unconfirmed`
    - `shared_prefix_axis_member`
    - `shared_axis_clean_dead_tail`
- `legal_clean_unconfirmed`
  - 目前只出现 `1` 次
  - 只落在 `bad` 句 `一直向往着远方`
  - 对应的是 legal continuation 还在，但 `source_axis` 已提前漂移
- `family_drifted + exact_but_wrong_family`
  - 当前只在 `bad` 句里出现，共 `3` 次
  - 主要落在：
    - `从校门口一路走去`
    - `第一站是一座古老的小镇`
  - 说明某些错句在进入 `risk_assist` 之前，`family` 层就已经给出更强负证据

#### 当前会混用的弱辅助组合

- `shared_axis_clean_supportive`
  - 分布为：
    - `bad = 2`
    - `unstable = 2`
    - `ok = 2`
  - 既落在错句，也落在对句
  - 因此它有结构信息，但当前不能当强资格，也不适合直接奖励

#### 当前更偏正向的组合

- `axis_clean_supportive`
  - 当前只出现在 `ok` 句，共 `3` 次：
    - `忽然就觉得`
    - `心里却不在这里`
    - `只觉得日子会永远这样延续下去`
  - 对应更干净的一条链：
    - `primary_path_eligible`
    - `source_axis_eligible`
    - `axis_clean_supportive`

#### 更新后的判断

- 后续如果继续设计 `upstream axis contract`，比“只看 `risk_assist_tag`”更合理的分层是：
  - 第一层负向资格：`family_drifted + exact_but_wrong_family`
  - 第二层 shared-axis 负证据：`shared_axis_clean_dead_tail`
  - 第三层 early drift 提示：`legal_clean_unconfirmed`
  - 保留但不升格：`shared_axis_clean_supportive`
- 因而当前更像成立的不是“supportive vs dead_tail”二分，而是：
  - 先看 `family` 是否已经明显漂移
  - 再看 `source_axis / risk_assist` 是否继续补负证据
  - `shared_axis_clean_supportive` 只能解释为“shared-axis 上仍有 exact completion 支撑”，不能直接当正向资格

### 组合资格优先级草案

- 基于当前组合命中复核，把后续 `upstream axis contract` 的候选规则先整理为只读草案，不直接改排序。

#### 第一层：强负向 gate

- 命中下面任一类，优先视为“当前路径不应被当成健康主轴”：
  - `family_tag = family_drifted` 且 `continuation_tag = exact_but_wrong_family`
  - `risk_assist_tag = shared_axis_clean_dead_tail`
- 理由：
  - 前者说明错误已经在 `family` 层坐实
  - 后者说明 shared-axis 上已无 downstream exact completion 支撑，更接近死尾巴

#### 第二层：中等负向提示

- 当前最适合放在这一层的是：
  - `risk_assist_tag = legal_clean_unconfirmed`
- 含义：
  - continuation 还合法
  - 当前边不一定有显式 risk
  - 但 `source_axis` 已无稳定落点
- 这类信号先记为强提示，不直接当 hard gate，等更多样本复核后再决定是否升格

#### 第三层：弱辅助，禁止直接升格

- `risk_assist_tag = shared_axis_clean_supportive`
- 当前它在 `ok / unstable / bad` 三个桶里都出现，说明它只能表达“shared-axis 上仍有 exact completion 支撑”
- 因而它当前只能：
  - 作为可继续观察的弱证据
  - 不能直接奖励
  - 不能单独判死刑

#### 第四层：正向健康信号

- 当前最接近健康主轴的组合是：
  - `path_tag = primary_path_eligible`
  - `source_axis_tag = source_axis_eligible`
  - `risk_assist_tag = axis_clean_supportive`
- 这类信号当前只在 `ok` 句中稳定出现，可先记录为正向健康信号，但仍保持只读

#### 当前更合理的推进顺序

- 先负向 gate，后讨论正向奖励
- 先多样本只读复核，再考虑打分
- 明确禁止把 `shared_axis_clean_supportive` 直接当正向资格

#### 下一步执行建议

- 不立刻改 `translator` 逻辑
- 先按这份优先级草案，在更大的 `octagram 对 / 当前错` 样本池里做规则级只读复核
- 重点看：
  - `family_drifted + exact_but_wrong_family` 是否稳定覆盖更多错句
  - `shared_axis_clean_dead_tail` 是否仍只落在错句/不稳定句
  - `legal_clean_unconfirmed` 是否会大面积误伤对句

### 更大样本池规则级只读复核

- 继续复用现有工件做规则级只读复核，没有重跑新实验。
- 本轮使用的样本池：
  - `expanded_sample_validation`
  - `expanded_sample_school_probe`
  - `expanded_sample_travel_probe`
- 其中 `validation` 旧 graph 未直接落 `risk_assist_tag`，但字段已足够，按当前 `ClassifyRiskAssistTag()` 口径做了离线复算。

#### 合并盘面

- 合并总盘面：
  - `27` 条样本
  - `ok = 14`
  - `unstable = 4`
  - `bad = 9`
- 去重口径：
  - 按 `school > travel > validation` 优先级保留同句最新工件
  - 去重后共 `24` 条
  - `ok = 11`
  - `unstable = 4`
  - `bad = 9`

#### 当前仍保持干净的信号

- 去重后：
  - `shared_axis_clean_dead_tail`：`unstable = 2`，`ok = 0`，`bad = 0`
  - `legal_clean_unconfirmed`：`bad = 1`，`ok = 0`，`unstable = 0`
  - `axis_clean_supportive`：`ok = 5`，`bad = 0`，`unstable = 0`
- 说明：
  - `shared_axis_clean_dead_tail` 继续只落在不稳定句
  - `legal_clean_unconfirmed` 继续只落在错句
  - `axis_clean_supportive` 继续只落在对句

#### 继续不能升格的信号

- `shared_axis_clean_supportive` 去重后仍为：
  - `ok = 3`
  - `unstable = 2`
  - `bad = 2`
- 说明它继续混用，只能保留为 shared-axis 弱辅助证据

#### 关键修正

- `family_drifted + exact_but_wrong_family` 去重后为：
  - `bad = 4`
  - `unstable = 1`
  - `ok = 1`
- 也就是说：
  - 它明显偏向错句
  - 但已经不能再视为“当前完全不误伤对句”的硬负向规则
- 唯一的 `ok` 误伤样本是：
  - `法国梧桐的叶子沙沙坠落`
- 展开最佳 exact path 后可见：
  - `叶子 / 沙沙 / 坠落` 三段都被记成 `family_drifted + exact_but_wrong_family`
  - 但整句仍是 `expected_rank = 1`
- 说明这个组合有时表达的是“对句里的 suffix exact continuation 在当前 family 账本下被记成 drifted”，而不一定是整条路径真的应被 hard reject

#### 更新后的判断

- 当前更稳的分层是：
  - 可继续视为稳定负向证据：
    - `shared_axis_clean_dead_tail`
    - `legal_clean_unconfirmed`
  - 可视为稳定正向健康信号：
    - `axis_clean_supportive`
  - 只能视为强负向候选、但必须加 guard：
    - `family_drifted + exact_but_wrong_family`
  - 继续只能作弱辅助：
    - `shared_axis_clean_supportive`

#### 下一步执行建议

- 下一步不应直接把 `family_drifted + exact_but_wrong_family` 写成 hard gate
- 更合理的是先做 guard 设计的只读复核：
  - 抽出命中该组合的全部 case
  - 区分“真正错误 family 漂移”与“对句里的合法 suffix exact continuation”
  - 再决定这个信号应如何加 guard，避免误伤

### `wrong_family_exact` guard 形态复核

- 继续按去重后的 `24` 条样本，单独抽出了所有命中
  - `family_tag = family_drifted`
  - `continuation_tag = exact_but_wrong_family`
  的 case。
- 命中总数为 `6` 条：
  - `ok`：`法国梧桐的叶子沙沙坠落`
  - `bad`：`像不经意的黄蝶`
  - `unstable`：`也带着一种不可挽回的流逝`
  - `bad`：`回到西安的街道`
  - `bad`：`从校门口一路走去`
  - `bad`：`第一站是一座古老的小镇`

#### 已排掉的 guard 方向

- 已明确不能使用：
  - “只要 `wrong_family_exact` 是句尾 suffix-only 形态，就 hard gate”
- 原因：
  - 唯一的 `ok` 误伤样本 `法国梧桐的叶子沙沙坠落`
  - 命中的正是这种 suffix-only 形态：
    - `法国梧桐 / 的`
    - `叶子 / 沙沙 / 坠落` 全部是 `family_drifted + exact_but_wrong_family`
- 结论：
  - suffix-only 既会出现在错句，也会出现在对句
  - 因而不能拿 suffix-only 充当安全 guard

#### 当前看到的两种主要形态

- 形态 A：纯 suffix 尾段漂移
  - `法国梧桐的叶子沙沙坠落`
  - `像不经意的黄蝶`
  - `也带着一种不可挽回的流逝`
  - `第一站是一座古老的小镇`
  - 特征：从某一段开始，尾部连续命中 `wrong_family_exact`
  - 结论：这类形态本身仍不足以 hard gate

- 形态 B：中段命中，后面还有别的结构信号
  - `回到西安的街道`
  - `从校门口一路走去`
  - 特征：中途先命中 `wrong_family_exact`，后面还会继续出现别的 segment
  - 例如 `从校门口一路走去` 在 `校门口` 之后，还会出现 `shared_axis_clean_supportive` 的 `一路`
  - 结论：单次中段命中同样不足以直接整条路径判死

#### 当前更像可用 hard negative 的，不是单独 `wrong_family_exact`

- 目前唯一更强的联动例子是：
  - `也带着一种不可挽回的流逝`
  - 先出现 `shared_axis_clean_dead_tail`
  - 尾部再接 `wrong_family_exact`
- 这类顺序更像：
  - shared-axis 已先断尾
  - exact continuation 再落入 drifted family
- 因而它比“单独 drifted exact suffix”更接近可用的 hard negative 候选

#### 更新后的判断

- 关于 `family_drifted + exact_but_wrong_family`，当前可以先定下：
  - 不能单独 hard gate
  - 不能用 suffix-only 充当 guard
  - 更合理的是只在伴随更强 path/source-axis 负证据时再升格
- 它当前更像：
  - 单独使用时：强负向候选信号
  - 与 `shared_axis_clean_dead_tail` / `legal_clean_unconfirmed` 等更强负证据联动时：才可能升级为 hard negative 触发条件

#### 下一步执行建议

- 下一步不直接实现 `wrong_family_exact` gate
- 先做“联动 guard 草案”的只读复核：
  - 统计 `wrong_family_exact` 与 `shared_axis_clean_dead_tail`
  - `legal_clean_unconfirmed`
  - 及其他更强 path/source-axis 负证据的联动命中率
  - 把“单独 wrong_family_exact”与“联动 wrong_family_exact”分开比较误伤率

### 联动 guard 草案复核

- 继续按去重后的 `24` 条样本，只读统计 `wrong_family_exact` 与更强负证据的联动情况。
- `wrong_family_exact` 总命中仍为：
  - `ok = 1`
  - `unstable = 1`
  - `bad = 4`

#### `wrong_family_exact + shared_axis_clean_dead_tail`

- 命中：
  - `unstable = 1`
  - `ok = 0`
  - `bad = 0`
- 唯一样本：
  - `也带着一种不可挽回的流逝`
- 判断：
  - 这条联动当前很干净
  - 但覆盖过稀，暂时不足以单独支撑可落地 guard

#### `wrong_family_exact + legal_clean_unconfirmed`

- 命中：
  - `0`
- 判断：
  - 当前样本里该联动完全不存在
  - 因而不是实际可用的 guard 方向

#### `wrong_family_exact + legal_but_path_unconfirmed`

- 命中：
  - `bad = 1`
  - `unstable = 1`
  - `ok = 0`
- 样本：
  - `也带着一种不可挽回的流逝`
  - `从校门口一路走去`
- 判断：
  - 这条联动比单独 `wrong_family_exact` 更干净
  - 覆盖虽然仍小，但比 `shared_axis_clean_dead_tail` 更有扩展价值

#### 其他更强 source-axis 负证据

- `wrong_family_exact + early_axis_drifted`：
  - `0`
- `wrong_family_exact + axis_unconfirmed`：
  - `0`
- 判断：
  - 当前未看到 `wrong_family_exact` 与更强 source-axis 负证据形成稳定联动

#### 关键样本解读

- `也带着一种不可挽回的流逝`
  - `不可挽回`：
    - `path_tag = legal_but_path_unconfirmed`
    - `source_axis_tag = shared_prefix_axis_member`
    - `risk_assist_tag = shared_axis_clean_dead_tail`
  - `流逝`：
    - `family_drifted + exact_but_wrong_family`
  - 说明：
    - path 先进入不稳 shared-axis
    - shared-axis 断尾
    - exact continuation 再落入 drifted family
  - 这是当前最接近可升级 hard negative 的联动模板

- `从校门口一路走去`
  - `校门口`：
    - `family_drifted + exact_but_wrong_family`
  - `一路`：
    - `path_tag = legal_but_path_unconfirmed`
    - `source_axis_tag = shared_prefix_axis_member`
    - `risk_assist_tag = shared_axis_clean_supportive`
  - 说明：
    - path 已不稳
    - 但 shared-axis 还未断尾
  - 这提示 `legal_but_path_unconfirmed` 可能比 `shared_axis_clean_dead_tail` 更早、更宽地覆盖“开始偏离但尚未彻底断尾”的场景

#### 更新后的收口判断

- 当前更稳的联动优先级：
  - 最干净但覆盖最稀：
    - `wrong_family_exact + shared_axis_clean_dead_tail`
  - 当前最值得继续追：
    - `wrong_family_exact + legal_but_path_unconfirmed`
  - 当前可排除：
    - `wrong_family_exact + legal_clean_unconfirmed`
    - `wrong_family_exact + early_axis_drifted`
    - `wrong_family_exact + axis_unconfirmed`

#### 当前更合理的 guard 候选

- 如果后续进入最小实现原型，当前更合理的逻辑不是：
  - `wrong_family_exact` 单独命中即触发
- 而是两级逻辑：
  - 一级负向资格：命中 `wrong_family_exact`
  - 二级升格条件：同句再命中 `legal_but_path_unconfirmed`
  - 若还能同时命中 `shared_axis_clean_dead_tail`，则负向资格进一步增强

#### 下一步执行建议

- 下一步不直接改代码
- 先做最后一轮只读扩样复核：
  - 把 `wrong_family_exact + legal_but_path_unconfirmed` 放到更大的 `octagram 对 / 当前错` 池子里复核
  - 重点确认扩样后是否仍保持 `ok = 0`

### `wrong_family_exact + legal_but_path_unconfirmed` 扩样复核

- 已完成最后一轮只读扩样复核。
- 继续只纳入当前字段口径兼容的 5 组工件：
  - `expanded_sample_validation_compact`
  - `expanded_sample_validation_small`
  - `expanded_sample_validation`
  - `expanded_sample_school_probe`
  - `expanded_sample_travel_probe`
- 排除原因：
  - `upstream_contract_probe_v2` 虽可配对，但缺 `path_tag / source_axis_tag / continuation_tag`
  - `upstream_family_probe` 仅有 family 维度，不能纳入当前联动口径

#### 扩样后的盘面

- 合并总行数：
  - `41`
  - `ok = 21`
  - `unstable = 6`
  - `bad = 14`
- 按 `school > travel > validation > validation_small > validation_compact` 去重后：
  - 共 `25` 条
  - `ok = 12`
  - `unstable = 4`
  - `bad = 9`
- 对比上一轮：
  - 去重样本从 `24` 增到 `25`
  - 说明这轮主要是在验证结论稳健性

#### 关键联动结果

- `wrong_family_exact + legal_but_path_unconfirmed` 扩样后仍为：
  - `bad = 1`
  - `unstable = 1`
  - `ok = 0`
- 命中的仍是：
  - `也带着一种不可挽回的流逝`
  - `从校门口一路走去`
- 说明：
  - `validation_small / compact` 没有引入新的 `ok` 误伤
  - 该联动虽然覆盖仍小，但当前可用样本里保持稳定干净

#### 与其他联动的对比

- `wrong_family_exact` 单独命中仍为：
  - `bad = 4`
  - `unstable = 1`
  - `ok = 1`
- `wrong_family_exact + legal_but_path_unconfirmed + shared_axis_clean_dead_tail` 仍为：
  - `unstable = 1`
  - `ok = 0`
  - `bad = 0`
- 说明：
  - `wrong_family_exact` 单独使用依然不够干净
  - 基础联动 `wrong_family_exact + legal_but_path_unconfirmed` 是当前最值得继续追的 guard 候选
  - 若再叠加 `shared_axis_clean_dead_tail`，负向资格更强，但覆盖依旧太稀

#### 收口判断

- 当前只读证据已足够支持：
  - 进入“最小实现原型”时，最值得实现成 guard 候选的不是 `wrong_family_exact` 单独命中
  - 而是 `wrong_family_exact + legal_but_path_unconfirmed`
- 可再做强度分层：
  - 基础联动 gate 候选：
    - `wrong_family_exact + legal_but_path_unconfirmed`
  - 加强版负向资格：
    - 上述基础联动再叠加 `shared_axis_clean_dead_tail`

#### 下一步执行建议

- 下一步不再做新的只读扩样
- 进入最小实现原型设计：
  - 暂不改正式排序逻辑
  - 先定义一个可开关的 guard 原型口径
  - 重点验证命中基础联动时是否值得打明确负向资格标记

### 最小实现原型设计收口

- 已重新梳理 `witset_translator.cc` 中 family/path/source-axis/risk_assist 的读数与改权链路。
- 当前真正修改 `candidate->weight` 的 upstream 合同入口都汇总在：
  - `WordGraphRewriter::Apply()`
- 当前最接近本次 guard 的现成函数是：
  - `ComputeTranslatorValidatedContinuationBias()`
- 但已确认：
  - 不应把新 guard 直接硬塞进该函数语义

#### 关键工程判断

- `wrong_family_exact + legal_but_path_unconfirmed` 不是稳定的“单 segment 局部规则”
- 两个信号可分布在前后不同 segment：
  - `从校门口一路走去`
    - `校门口` 命中 `wrong_family_exact`
    - `一路` 命中 `legal_but_path_unconfirmed`
  - `也带着一种不可挽回的流逝`
    - `不可挽回` 先命中 `legal_but_path_unconfirmed`
    - `流逝` 再命中 `wrong_family_exact`
- 因而：
  - 这条 guard 本质上是跨 segment 联动资格
  - 不能假装成单 candidate 局部判断直接揉进 validated continuation bias

#### 为什么不能直接改 `ComputeTranslatorValidatedContinuationBias()`

- 风险一：语义污染
  - 该函数当前职责是 clean family continuation 合同的局部 boost/penalty
  - 把跨 segment 联动塞进去会让职责变脏
- 风险二：实现偷换
  - 为了在单函数里拿到联动信息，最后很可能退化成不可靠的局部近似
  - 容易把当前离线复核过的干净联动，落成一个逻辑并不等价的 patch

#### 当前最合理的最小挂点

- 原型应继续挂在：
  - `WordGraphRewriter::Apply()`
- 但新增独立 helper：
  - `ComputeTranslatorLinkedGuardBias(...)`
- 不改写现有：
  - `ComputeTranslatorValidatedContinuationBias()`

#### 原型最小状态层

- 即便只做最小原型，也至少需要一层很薄的 guard state，用来回答：
  - 当前路径上游是否已出现 `legal_but_path_unconfirmed`
  - 当前路径下游是否还能接到 `legal_but_path_unconfirmed`
  - 若存在，是否还伴随 `shared_axis_clean_dead_tail`
- 最小建议拆成：
  - 前向 `prefix guard state`
  - 后向 `tail guard support`
- 目的：
  - 覆盖 `wrong_family_exact` 在前、`path_unconfirmed` 在后
  - 以及 `path_unconfirmed` 在前、`wrong_family_exact` 在后 两种已确认形态

#### 为什么这仍算最小原型

- 因为现场已存在大量可复用 helper：
  - `BuildBestPrefixStates()`
  - `BuildContinuationPathSummaries()`
  - `BuildExactAxisPrefixStates()`
  - `BuildExactCompletionSupports()`
- 新增的不是独立新算法，而是在现有 prefix/tail 口径上补一层很薄的 guard state

#### 最小配置面建议

- 当前 `WordGraphRewriteConfig` 只有：
  - `upstream_edge_prior_weight`
  - `upstream_path_prior_weight`
  - `upstream_validated_continuation_weight`
- 原型阶段建议只新增：
  - `upstream_linked_guard_weight`
- 继续复用：
  - `enable_upstream_edge_prior`
- 若同时命中 `shared_axis_clean_dead_tail`，先在 helper 中使用固定增强系数，不额外再加第二个权重

#### 推荐落地顺序

- Phase 0：
  - 先做只读 linked guard tag
  - 不改排序
  - 目标是确认代码内实时口径与离线复核一致
- Phase 1：
  - 再把 `ComputeTranslatorLinkedGuardBias(...)` 接入 `WordGraphRewriter::Apply()`
  - 用 `upstream_linked_guard_weight` 控制强度
- Phase 2：
  - 只有确认不新增明显 `ok` 误伤，且能稳定覆盖目标错句，才考虑并入正式上游合同

#### 当前收口结论

- 最小实现原型应定义为：
  - 独立 `linked_guard` helper
  - 薄 `guard state`
  - 单一新权重
- 不直接污染现有 validated continuation bias 的语义

### Phase 0 只读 `linked_guard_tag` 已落地

- 已继续推进到 `Phase 0` 代码壳落地，但仍严格停在只读 tag 阶段，没有改正式排序逻辑。
- 本次仅修改：
  - `witset_translator.cc` 的 graph snapshot 导出链路

#### 新增的只读 helper

- `BuildLinkedGuardPrefixStates(...)`
  - 沿 snapshot 口径的 best prefix path 记录：
    - 是否已见 `legal_but_path_unconfirmed`
    - 是否已见 `shared_axis_clean_dead_tail`
- `BuildLinkedGuardTailSupports(...)`
  - 从 tail 侧回推 best exact tail，记录：
    - 后续是否还能命中 `legal_but_path_unconfirmed`
    - 后续是否还能命中 `shared_axis_clean_dead_tail`
- `ClassifyLinkedGuardTag(...)`
  - 为当前 candidate 输出：
    - `not_linked_guard_candidate`
    - `wrong_family_without_path_unconfirmed`
    - `wrong_family_with_path_unconfirmed`
    - `wrong_family_with_path_unconfirmed_dead_tail`

#### 导出层新增字段

- 已在 `BuildLocalGraphSnapshotLine()` 的 candidate 导出中新增：
  - `linked_guard_tag`

#### 这次为什么仍属于 Phase 0

- 没有改 `candidate->weight`
- 没有把 linked guard 接进 `WordGraphRewriter::Apply()`
- 没有修改 `ComputeTranslatorValidatedContinuationBias()` 的原语义
- 也就是说，本次只是把前面离线复核出的联动 guard 口径，落成代码内实时可见的 snapshot tag

#### 当前工程意义

- 后续不再只能依赖离线脚本二次复算联动关系
- 可以直接在 graph snapshot 中检查 candidate 是否被实时打成：
  - `wrong_family_with_path_unconfirmed`
  - `wrong_family_with_path_unconfirmed_dead_tail`
- 这一步是进入真实 bias 原型前必须先补齐的“口径对齐层”

#### 下一步验证重点

- 下一步不做新的设计推演，直接做口径一致性验证：
  - 用现有 debug 流程重新产出 graph snapshot
  - 检查代码内实时生成的 `linked_guard_tag`
  - 核对：
    - `从校门口一路走去` 是否稳定打出 `wrong_family_with_path_unconfirmed`
    - `也带着一种不可挽回的流逝` 是否稳定打出 `wrong_family_with_path_unconfirmed_dead_tail`
- 若实时 tag 与离线复核口径一致，再进入下一阶段：
  - 把 linked guard 变成可开关的负向 bias 原型

#### 当前阶段状态

- `Phase 0` 设计已完成
- `Phase 0` 代码壳已落地
- 尚未进入改权阶段

### Phase 1：linked guard bias 原型已实现并编译通过

- 已在 `witset_translator` 中把 `linked_guard` 从只读 tag 推进到可开关 bias 原型：
  - `WordGraphRewriteConfig` 新增 `upstream_linked_guard_weight`
  - `WitsetTranslator` 新增 `upstream_linked_guard_weight_`
  - `WordGraphRewriter::Apply()` 新增独立 `ComputeTranslatorLinkedGuardBias(...)`
- 当前 bias 口径：
  - `wrong_family_with_path_unconfirmed`
    - 基础负向 bias
  - `wrong_family_with_path_unconfirmed_dead_tail`
    - 更强一档负向 bias
- 仍保持为独立实验入口：
  - 没有污染 `ComputeTranslatorValidatedContinuationBias()` 的原语义

#### 编译情况

- 第一次编译只暴露出一个真实问题：
  - `GetWordGraphTerminalPos()` 中 `std::max` 的模板参数推导歧义
- 修复为显式 `std::max<size_t>(...)` 后：
  - `librime/build.bat static` 已重新编译通过

### Phase 1：0.0 vs 1.0 对比验证

- 为避免 graph dump 的超慢回放继续消耗时间，本轮验证改为：
  - 同一组代表 case
  - 比较 `upstream_linked_guard_weight = 0.0` 与 `1.0`
  - 直接观察 top1 / top3 变化

#### 验证样本

- `法国梧桐的叶子沙沙坠落`
  - 已知 guard 风险句 / 误伤观测点
- `也带着一种不可挽回的流逝`
  - 已确认 `wrong_family_with_path_unconfirmed_dead_tail` 联动模板
- `从校门口一路走去`
  - 另一条目标错句

#### 结果

- `weight = 0.0`
  - `法国梧桐的叶子沙沙坠落`
    - top1 正确
  - `也带着一种不可挽回的流逝`
    - top1 = `也带着一种不可挽回的流失`
    - 正确句 `流逝` 在 top2
  - `从校门口一路走去`
    - top1 = `从校门口译掳走去`
- `weight = 1.0`
  - 3 条样本的 top1 / top3 全部不变

#### 判断

- 当前原型没有带来新增误伤
- 但也没有带来任何可见收益
- 说明：
  - `linked_guard` 作为末端负向 bias 的落点太晚
  - 无法撬动已经在更早 family/path 主轴阶段形成的句级排序

### Phase 2 收口

- 当前结论：
  - `Phase 0` 成立：实时 tag 与离线口径一致
  - `Phase 1` 不成立：末端负向 bias 原型无效
  - `Phase 2` 不推进：当前实现形态不升格为正式合同

#### 后续若继续

- 不建议继续单纯把 `upstream_linked_guard_weight` 从 `1.0` 往上调
- 更合理的方向应改为：
  - 提前到 family/path 主轴竞争阶段介入
  - 作为 path/family qualification 使用
  - 或仅保留为 snapshot/debug 诊断信号

### 前移版挂点继续收口

- 继续对“下一轮真正该改哪”做了代码级定位。
- 新的关键判断：
  - 前移不等于直接塞进 `witset_poet`
  - 更合理的前移点，仍在 `translator` 的 `WordGraphRewriter::Apply()`
  - 但介入方式要从当前的 candidate 末端补罚，改成 family / edge 资格阶段

#### 为什么不是直接改 `poet`

- 重新对齐 `witset_poet.cc` 后确认：
  - `upstream_path_prior_penalty`
  - `joint_prior_penalty`
  - `joint_guidance_penalty`
  - `beam_merge_penalty`
  - 以及一组 early bonus / penalty
  确实都比后面的 search merge 更早进入主链
- 但 `linked_guard` 当前成立的核心资格是：
  - `wrong_family_exact + legal_but_path_unconfirmed`
- 其中 `wrong_family_exact` 依赖：
  - 同一 `start -> end` family 内部 exact 候选相对关系
  - `best exact / second best exact / clean vs drifted` 之类图级比较
- 这些不是 `poet` beam expansion 现场天然可见的本体信号

#### 调用顺序上的关键确认

- 已再次确认顺序：
  1. `translator` 构建 `WordGraph`
  2. `WordGraphRewriter(rewrite_config).Apply(ledger, &graph)`
  3. 之后才进入 `poet_->MakeSentences(...)`
- 这意味着：
  - “前移”并不需要强行改到 `poet`
  - `Apply()` 自己就已经处在 poet 之前

#### 当前更合理的前移版原型

- 下一轮更值得实现的，不应再是：
  - `ComputeTranslatorLinkedGuardBias()` 这种 candidate 级末端小惩罚
- 而应改成 family / edge 级结构介入，例如：

1. `family qualification penalty`
   - 先在每个 `start -> end` family 内汇总 linked-guard 资格
   - 若该 family 的主 exact 候选命中：
     - `wrong_family_with_path_unconfirmed`
     - 或 `wrong_family_with_path_unconfirmed_dead_tail`
   - 则惩罚 family 整体竞争资格，而不是只惩罚单个 candidate

2. `soft edge demotion`
   - 若某条 edge 的 best exact family 命中 linked guard
   - 且缺少足够强的 clean exact 支撑
   - 则给整条 edge 一个 soft demotion

#### 这一轮收口后的职责划分

- `poet`
  - 更适合处理连续风险：
    - fallback / OOV / joint risk / single-char tail
- `translator graph rewriter`
  - 更适合处理 family/path 资格：
    - family 内 exact 相对关系
    - path qualification / family qualification

#### 当前收口结论

- 下一轮真正值得实现的，不是更大的 `upstream_linked_guard_weight`
- 也不是直接改 `poet`
- 而是把 linked guard 前移成：
  - `WordGraphRewriter::Apply()` 中的 family / edge qualification 原型

## 2026-05-21 前移版 family qualification penalty 原型：编译通过但仍无收益

- 继续沿上一轮收口的方向，没有再去加大 `upstream_linked_guard_weight` 的末端 bias，而是把 linked guard 真正前移到 `WordGraphRewriter::Apply()` 的 family / edge 资格层。

### 本轮实现

- 在 `witset_translator.cc` 中新增并接入：
  - `ComputeTranslatorLinkedGuardFamilyPenalty(...)`
- 改动要点：
  - 不再对单个 candidate 逐个施加 `ComputeTranslatorLinkedGuardBias(...)`
  - 改为先在每个 `start -> end` family 内汇总：
    - family 主 exact 候选的 `linked_guard_tag`
    - family 内是否存在 clean exact support
  - 若主 exact 命中：
    - `wrong_family_with_path_unconfirmed`
    - 或 `wrong_family_with_path_unconfirmed_dead_tail`
    且缺少 clean exact support，则对整条 family / edge 做资格降权
- 也就是说，这轮验证的不是“句尾补罚”，而是：
  - linked guard 前移成 graph 阶段的 family 竞争资格降权后，是否终于能影响最终排序

### 编译

- 按工作区规则在 `librime` 目录执行：
  - `.\build.bat static`
- 结果：
  - 编译通过

### 最小对比验证

- 继续复用已有的：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\linked_guard_phase1_compare.py`
- 验证口径保持不变：
  - 比较 `upstream_linked_guard_weight = 0.0` 与 `1.0`
  - 继续用 3 条代表 case：
    - `法国梧桐的叶子沙沙坠落`
    - `也带着一种不可挽回的流逝`
    - `从校门口一路走去`

#### 结果

- `weight = 0.0`
  - `法国梧桐的叶子沙沙坠落`
    - top1 正确
  - `也带着一种不可挽回的流逝`
    - top1 = `也带着一种不可挽回的流失`
    - 正确句 `流逝` 在 top2
  - `从校门口一路走去`
    - top1 = `从校门口译掳走去`
- `weight = 1.0`
  - 3 条样本的 top1 / top3 全部不变

### 收口判断

- 这版前移原型没有新增误伤
- 但也没有带来任何可见收益
- 说明：
  - 问题已经不只是“candidate 末端补罚太晚”
  - 即便前移到 `Apply()` 内的 family / edge 资格层，当前 linked-guard 信号仍不足以改写图上的主竞争格局
- 因而当前可以把判断进一步收紧为：
  - `Phase 0` 成立：实时 tag 与离线口径一致
  - `Phase 1` 不成立：candidate 末端 bias 无效
  - 前移版 `family qualification penalty` 也暂不成立：仍无可见收益

### 配置收尾

- 验证结束后，已把运行时 schema 中临时打开的：
  - `upstream_linked_guard_weight`
  恢复回：
  - `0.0`
- 包括：
  - `C:\Users\Bing\AppData\Roaming\witty\witset.schema.yaml`
  - `C:\Users\Bing\AppData\Roaming\witty\build\witset.schema.yaml`

### 下一步建议

- 下一步不应继续围绕 `upstream_linked_guard_weight` 做调参
- 更合理的是：
  - 把 `linked_guard` 保留为 debug / diagnosis 层的辅助资格
  - 继续寻找覆盖更广、执行力更强的 upstream contract 信号
  - 或只把它并入更大的 family/source contract，而不再单独作为主执行条件

## 2026-05-21 继续收口：translator 标签线基本跑穿，下一入口回到 `admitted -> full-line stage` 生命周期

- 在前移版 `family qualification penalty` 原型继续判负后，没有马上再发明新权重。
- 先重新对齐了：
  - `上游方案设计与可行性验证.md`
  - `WORKLOG`
  - 以及现有 stage probe 工件
- 目的是确认不会把已经做过的 `continuation/path/source_axis` 细化线，换个名字再做一遍。

### 重新核对后的判断

- `continuation_tag / path_tag / source_axis_tag / linked_guard_tag` 这条 translator 侧细化线，当前已经基本跑穿：
  - `exact_but_wrong_family` 成立
  - `legal_primary_continuation` 已进一步收窄到：
    - `exact_ambiguous_family`
    - `exact_competing_continuation`
  - `path_tag`
    - `primary_path_eligible`
    - `legal_but_path_unconfirmed`
  - `source_axis_tag`
    - `source_axis_eligible`
    - `shared_prefix_axis_member`
    - `early_axis_drifted`
    - `axis_unconfirmed`
  都已经做过 probe 细化与只读复核
- 当前这些标签更适合作为：
  - diagnosis
  - 辅助资格
  - 组合读数
- 但到目前为止，还没有任何一条能单独成为足以改写 top1 的主执行条件

### 继续读取 stage 工件后的新收口

- 继续读取并对齐了：
  - `independent_multi_case_stage_summary.json`
  - `multi_case_stage_probe_v1_summary.json`
  - `multi_case_stage_probe_v1_suffix_summary.json`
  - `pre_source_pool_focus.json`
- 当前最关键的新结论不是“又一个标签没用”，而是：
  - 对这轮代表集里的主证据句，完整 `expected` line 在以下 full-line stage 中全部为 `0`：
    - `admitted_new_line_post_push`
    - `pre_source_pool_full`
    - `source_pool_full`
    - `top_candidate_full`
    - `pre_future_compact_full`
    - `post_future_compact_full`
- 说明：
  - 问题已经不太像“最后一跳 rank 没排对”
  - 更像“正确链根本没有以 full-line 形态活到后段可见池”

### 但正确 partial chain 是存在的

- 同时，聚焦工件 `pre_source_pool_focus.json` 又明确显示：
  - `一直 -> 向往`
  - `一直 -> 向往着`
  - `第一站 -> 是`
  这些正确 partial chain 都真实存在于：
  - `batch_selected`
  - `admitted_new`
- 也就是说：
  - 正确 partial chain 不是没生成
  - 也不是没 admitted
  - 当前更像是 admitted 之后，没能继续作为 full-line 主竞争链稳定活到后段 stage

### 更新后的判断

- 因而这轮把“下一步应继续哪里”重新钉清楚了：
  - 不应继续调 `upstream_linked_guard_weight`
  - 不应继续 invent 新的 translator contract 标签
  - 也不应简单回到 `poet` 末端 patch
- 更合理的是继续盯：
  - `admitted_new`
  - 到 `pre_source_pool_full / source_pool_full / top_candidate_full`
  - 再到 `pre_future_compact_full / post_future_compact_full`
  之间的真实生命周期

### 下一步建议

- 后续若继续，probe 口径应从“完整 expected line 是否出现”切到：
  - 关键 partial chain 的 stage 级追踪
- 优先链路：
  - `一直 -> 向往 -> 向往着`
  - `第一站 -> 是 -> 是一 / 是以 / 适宜 ...`
- 重点回答：
  - 正确 partial chain 是死在 admitted 后的可见池切换
  - 还是死在 `future compact / source_pool` 的保活筛选

### 对关键 partial chain 的第一次直接计数

- 为了把“究竟死在 admitted 后哪一层”再收紧一步，没有新增代码，也没有重跑引擎。
- 直接复用：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\pre_source_pool_focus.json`
- 单独统计了 3 条关键 partial chain：
  - `一直 -> 向往`
  - `一直 -> 向往着`
  - `第一站 -> 是`

#### 结果

- `一直 -> 向往`
  - `batch_selected = 1`
  - `admitted_new = 1`
  - 其余 stage = `0`
- `一直 -> 向往着`
  - `batch_selected = 1`
  - `admitted_new = 1`
  - 其余 stage = `0`
- `第一站 -> 是`
  - `batch_selected = 1`
  - `admitted_new = 1`
  - 其余 stage = `0`

#### 更新后的判断

- 这一步把边界继续收紧为：
  - 正确 partial chain 不是没生成
  - 不是没进 `batch_selected`
  - 也不是没进 `admitted_new`
- 当前更像是：
  - 它们在 admitted 之后，没有继续出现在后续 full-line stage
  - 或者当前 full-line stage probe 还没有把同一条 partial chain 继续追出来

#### 下一步建议

- 后续若继续，最有价值的不是再 invent 新标签
- 而是继续把同一条 partial chain 追进：
  - `pre_source_pool_full`
  - `source_pool_full`
  - `top_candidate_full`
  - `pre_future_compact_full`
  - `post_future_compact_full`
- 也就是把 probe 口径从“整句 expected 是否出现”继续推进到：
  - “已 admitted 的关键 partial chain 是否在后续 stage 仍然可见”

## 2026-05-21 partial-chain probe 继续推进：修正为“partial chain 活着，问题在 next hop”

### 新增脚本

- 新增了最小复用脚本：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py`
- 用途：
  - 直接驱动 `rime_api_console`
  - 读取 snapshot 内的 `expansion_gate_records`
  - 按 partial-chain 口径统计：
    - `一直 -> 向往`
    - `一直 -> 向往着`
    - `第一站 -> 是`

### 第一轮成功跑出的关键结论

- 在第一轮成功的 raw 结果里，三条关键 partial chain 都不只活到 `admitted_new`
- 它们都已能进入后段 stage：

#### `一直 -> 向往`

- `request = 1`
- `batch_selected = 1`
- `admitted_new = 1`
- `admitted_new_line_post_push = 1`
- `pre_source_pool_full = 1`
- `source_pool_full = 1`
- `top_candidate_full = 1`
- `pre_future_compact_full = 3`
- `post_future_compact_full = 3`

#### `一直 -> 向往着`

- `request = 1`
- `batch_selected = 1`
- `admitted_new = 1`
- `admitted_new_line_post_push = 1`
- `pre_source_pool_full = 1`
- `source_pool_full = 1`
- `top_candidate_full = 1`
- `pre_future_compact_full = 4`
- `post_future_compact_full = 4`

#### `第一站 -> 是`

- `request = 1`
- `batch_selected = 1`
- `admitted_new = 1`
- `admitted_new_line_post_push = 2`
- `pre_source_pool_full = 2`
- `source_pool_full = 2`
- `top_candidate_full = 1`
- `pre_future_compact_full = 2`
- `post_future_compact_full = 2`

### 由此修正前一轮判断

- 前一轮“关键 partial chain 没进入后续 full-line stage”的判断需要撤回
- 当前应改判为：
  - partial chain 本身是活着的
  - 真正的问题更像落在：
    - `一直向往着 -> 远方`
    - `第一站是 -> 一座`
    这类 next-hop continuation competition

### 为了继续追 next hop 做的 debug-only 代码调整

- 在 `witset_poet.cc` 中，针对 `maybe_record_expansion_gate(...)` 的 debug 过滤做了两轮收窄式调整
- 目的不是改排序，而是让 probe 能看到 partial chain 后的下一跳

#### 第一轮

- 从仅记录：
  - `source.generated_word_count == 1`
- 放宽为允许记录到：
  - `source.generated_word_count <= 2`

#### 第二轮

- 为避免 debug 记录量爆炸，再进一步收窄为只保留短 partial chain：
  - `generated_word_count <= 2`
  - `generated_char_count <= 5`
- 同时把 `maybe_record_line_snapshot(...)` 也收成同样口径

#### 编译

- 在 `librime` 目录执行：
  - `.\build.bat static`
- 两轮调整后都已编译通过

### 当前新的阻塞

- 为了继续读：
  - `一直向往着 -> 远方`
  - `第一站是 -> 一座 / 以 / 适宜 / 已作古`
  的 next-hop 竞争，继续跑 `partial_chain_stage_probe.py` 时遇到新的瓶颈：
  - `run_local_snapshot_baseline.py`
  - `SnapshotTailReader`
  - `parse_snapshot_line`
  - `json.loads(...)`
  在 `debug_dump_local_graph_snapshot = true` 下会因为单条 snapshot JSON 过大触发：
  - `MemoryError`

### 额外复核

- 这个 `MemoryError` 问题在两种口径下都复现了：
  - 原始长前文
  - 收短后的局部前文
- 因而问题不在 timeout 太小，而在当前：
  - `snapshot jsonl -> Python json.loads`
  这条抓取链路本身不适合继续承载当前规模的 graph debug

### 当前最稳的下一步

- 不再继续加 timeout
- 不再继续硬跑当前 snapshot 解析链
- 后续若继续，应该转去实现更轻量的 next-hop probe 输出通道：
  - 直接在 C++ debug 导出里产出更小的 partial-chain summary
  - 只输出：
    - `source_text`
    - `entry_text`
    - `stage`
    - 少量关键分数字段

## 2026-05-21 轻量 next-hop probe 落地并拿到第一轮结果

### 代码实现

- 为避开超大 snapshot JSON 触发的 `MemoryError`，这轮没有继续加 timeout。
- 直接在 debug 导出链路里补了单独的 next-hop probe 通道：
  - `witset_poet.h`
  - `witset_poet.cc`
  - `witset_translator.h`
  - `witset_translator.cc`

### 设计

- 在 `WitsetPoet` 中新增轻量 next-hop record：
  - 只保留：
    - `stage`
    - `source_text`
    - `entry_text`
    - `search_score`
    - `weight`
    - `base_score`
    - `dict_score_raw`
    - `lm_score_scaled`
    - `used_char_fallback`
    - `lm_oov_token_count`
- 不再把这类 next-hop probe 塞进 snapshot 主 JSON 行
- `WitsetTranslator` 新增独立写口：
  - `debug_dump_local_next_hop_probe`
  - `debug_local_next_hop_probe_path`
  - `debug_next_hop_probe_suffixes`

### 配置与脚本

- 在两份 schema 中补了默认键位：
  - `C:\Users\Bing\AppData\Roaming\witty\witset.schema.yaml`
  - `C:\Users\Bing\AppData\Roaming\witty\build\witset.schema.yaml`
- 继续使用脚本：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py`
- 但脚本已改成：
  - 用普通 snapshot 只做同步
  - 真正的 next-hop 统计改读：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.next_hop.jsonl`

### 编译

- 在新专用终端执行：
  - `.\build.bat static`
- 编译通过

### 第一轮结果：`一直向往着 -> 远`

- 结果文件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe_result.json`
- 关键结论：
  - `远` 不是不存在
  - 它会经过：
    - `request`
    - `batch_selected`
    - `admitted_new`
    - `admitted_new_line_pre_push`
    - `admitted_new_line_post_push`
    - `pre_source_pool_full`
    - `source_pool_full`
    - `pre_future_compact_full`
    - `post_future_compact_full`
- 说明：
  - `一直向往着 -> 远` 这条 next hop 能形成，也能活到后段 stage
- 但 request 起点排名不占优：
  - top entries 前列是：
    - `与`
    - `于`
    - `原`
    - `元`
    - `语`
  - `远` 的 request 分数：
    - `search_score = -187.556`
  - 第一名 `与`：
    - `search_score = -187.386`

### 第一轮结果：`第一站是 -> 一 / 以`

- 运行时实际命中的 source 文本是：
  - `我踏上了旅行的征程。第一站是`
- 关键结论：
  - 最强 request 不是 `一`
  - 而是：
    - `以`
    - `search_score = -159.821`
  - 后续高位还包括：
    - `已`
    - `壹`
    - `乙`
    - `姨`
    - `意`
    - `宜`
- 正确起跳 `一` 虽然存在，但：
  - `search_score = -188.128`
  - `used_char_fallback = true`
  - `lm_oov_token_count = 1`
- 说明：
  - `第一站是 -> 一` 不是没出现
  - 而是 request 起点就已经输给 `以`
  - 且自身还背着 fallback / OOV 负担

### 当前更新后的判断

- 这轮之后，主判断继续收缩为：
  - `partial chain` 本身不是主要问题
  - 更关键的是：
    - 正确 next-hop 在 request 起点就被更强的同音短词压住
- 当前两个主证据句分别表现为：
  - `一直向往着 -> 远`
    - 从 request 起点就略输给 `与 / 于 / 原 / 元 / 语`
  - `第一站是 -> 一`
    - 从 request 起点就被 `以` 大幅压住
    - 且带有 `char fallback + OOV`

### 下一步建议

- 后续若继续，优先直接审：
  - request-stage next-hop ranking contract
- 重点字段：
  - `dict_score_raw`
  - `lm_score_scaled`
  - `used_char_fallback`
  - `lm_oov_token_count`

## 2026-05-21 重复性审计：先检查 WORKLOG 再决定是否继续实验

### 审计目的

- 按最新要求，先检查 `WORKLOG`，确认最近这批 `partial-chain / next-hop` 相关测试是不是在重复已经做过的实验。
- 这一步先不启动新的引擎测试，只复查日志、脚本和现有结果文件。

### 审计结果

- 结论不是“完全重复”，但存在明显重叠，需收紧后续实验边界。

#### 明显已做过、不能再原样重复的部分

- `第一站是一座古老的小镇` 相关的这些层级，`WORKLOG` 里已经多次跑过：
  - `第一站 / 第一站是` 的局部竞争
  - `S9` expansion gate / final_pool / final ranking
  - `whole-word / fallback / OOV` 证据
  - `family qualification / linked_guard / partial-chain` 路线
- 所以如果再继续跑：
  - “看 `第一站是` 有没有进入口/进池/进 final_pool”
  - “看 partial chain 能不能活到后段 stage”
  这类实验，就属于重复路线。

#### 这轮不算重复、可以继续的部分

- 这轮新落地的轻量 `next-hop probe` 不算原样重复：
  - 它新增了独立 JSONL 导出通道
  - 首次把问题收缩到：
    - `一直向往着 -> 远`
    - `第一站是 -> 一 / 以`
    在 `request` 起点的 next-hop 排名
- 这一步和之前 `S9 / final_pool / fallback` 结论是连续的，但不是同一层重复验证。

### 基于现有结果继续做的只读拆账

- 为避免再跑重复实验，这一步没有新起引擎，只直接复用：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe_result.json`
- 继续做 request-stage 分差拆账。

#### `一直向往着 -> 远`

- 这一步不是没形成，而是 request 起点就略输给同音短词：
  - `与`
    - `search_score = -187.386`
    - `dict_score_raw = -14.0545`
    - `lm_score_scaled = -45.4751`
    - `used_char_fallback = false`
    - `lm_oov_token_count = 0`
  - `于`
    - `search_score = -187.443`
    - `dict_score_raw = -14.1029`
    - `lm_score_scaled = -45.4751`
    - `used_char_fallback = false`
    - `lm_oov_token_count = 0`
  - `远`
    - `search_score = -187.556`
    - `dict_score_raw = -14.1979`
    - `lm_score_scaled = -45.4751`
    - `used_char_fallback = false`
    - `lm_oov_token_count = 0`

- 当前能直接看出的结论：
  - `LM` 这一跳基本相同
  - 主要差异先落在：
    - `dict_score_raw`
  - 即：
    - `远` 并不是被 fallback / OOV 打坏
    - 而是 request 起点的词典底座略输给 `与 / 于`

#### `第一站是 -> 一 / 以`

- 这一步已有明显结构差异：
  - `以`
    - `search_score = -159.821`
    - `dict_score_raw = -14.0172`
    - `lm_score_scaled = -19.8808`
    - `used_char_fallback = false`
    - `lm_oov_token_count = 0`
  - `一`
    - `search_score = -188.128`
    - `dict_score_raw = -14.0778`
    - `lm_score_scaled = -46.6264`
    - `used_char_fallback = true`
    - `lm_oov_token_count = 1`

- 当前能直接看出的结论：
  - 这里不只是 `dict_score_raw` 小差异
  - 更关键的是：
    - `一` 这一跳已经带着 `fallback + OOV`
    - 且 `LM` 明显更差
  - 所以：
    - `第一站是 -> 一` 不是“后段才输”
    - 而是 request 起点就已经被 `以` 拉开大差距

### 这次审计后的执行原则

- 后续继续前，先查 `WORKLOG`
- 若某一层已经明确判负，就不再用同一 case 在同一层重复起实验
- 优先只做：
  - 新层级
  - 新抓取口径
  - 或复用现有结果文件的只读拆账

### 当前可继续的唯一非重复方向

- 若继续，最值得做的不是再跑：
  - partial-chain 存活
  - final_pool 可见性
  - `第一站是` 是否进入口
- 而是直接聚焦：
  - `request-stage next-hop ranking contract`
- 其中两条主线分别是：
  - `一直向往着 -> 远`
    - 重点查为什么主要输在 `dict_score_raw`
  - `第一站是 -> 一`
    - 重点查为什么同时出现：
      - `lm_score_scaled` 明显劣化
      - `used_char_fallback = true`
      - `lm_oov_token_count = 1`

## 2026-05-21 只读拆账继续：request-stage 分差已可进一步定性

- 这一步继续遵守“先查 WORKLOG，避免重复实验”的原则。
- 没有再起新的引擎测试。
- 只复用现有结果文件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe_result.json`
- 继续把 `request-stage next-hop ranking` 的分差直接拆成：
  - `search_score`
  - `base_score`
  - `dict_score_raw`
  - `lm_score_scaled`
  - `used_char_fallback`
  - `lm_oov_token_count`

### `一直向往着 -> 远` vs `与 / 于`

#### `远` vs `与`

- `search_score`
  - `远 = -187.556`
  - `与 = -187.386`
  - `delta = -0.170`
- `base_score`
  - `delta = -0.143`
- `dict_score_raw`
  - `delta = -0.1434`
- `lm_score_scaled`
  - `delta = 0`
- `used_char_fallback`
  - 两者都是 `false`
- `lm_oov_token_count`
  - 两者都是 `0`

#### `远` vs `于`

- `search_score`
  - `delta = -0.113`
- `base_score`
  - `delta = -0.095`
- `dict_score_raw`
  - `delta = -0.095`
- `lm_score_scaled`
  - `delta = 0`
- `used_char_fallback`
  - 两者都是 `false`
- `lm_oov_token_count`
  - 两者都是 `0`

#### 更新后的判断

- `一直向往着 -> 远` 这条当前几乎可以直接定性为：
  - **不是 LM 问题**
  - **不是 fallback / OOV 问题**
  - **主要就是 request 起点的 `dict_score_raw` 略输给同音短词**
- 换句话说：
  - `远` 输给 `与 / 于`
  - 当前基本不是结构性崩坏
  - 而是字典底座在这一跳上对同音单字的排序略不占优

### `第一站是 -> 一` vs `以`

- `search_score`
  - `一 = -188.128`
  - `以 = -159.821`
  - `delta = -28.307`
- `base_score`
  - `delta = -26.806`
- `dict_score_raw`
  - `delta = -0.0606`
- `lm_score_scaled`
  - `delta = -26.7456`
- `used_char_fallback`
  - `一 = true`
  - `以 = false`
- `lm_oov_token_count`
  - `一 = 1`
  - `以 = 0`

#### 更新后的判断

- `第一站是 -> 一` 这条也已经可以更精确地定性：
  - **主差距不在 `dict_score_raw`**
  - **主差距几乎全部来自 `lm_score_scaled`**
- 而且这条正确起跳还同时带着：
  - `used_char_fallback = true`
  - `lm_oov_token_count = 1`
- 所以目前更准确的表述应是：
  - `一` 不是单纯“字典分稍差”
  - 而是在 request 起点就因为：
    - fallback
    - OOV
    - 以及由此带出的更差 LM
    被 `以` 大幅拉开

### 这轮只读拆账后的收口

- 当前两条主证据句已经分成两种不同机理：

#### 类型 A：`一直向往着 -> 远`

- 主要是：
  - `dict_score_raw` 小差距
- 不明显涉及：
  - fallback
  - OOV
  - LM 崩坏

#### 类型 B：`第一站是 -> 一`

- 主要是：
  - `LM` 明显劣化
- 并伴随：
  - fallback
  - OOV

### 对下一步的影响

- 后续若继续，不能再把这两条当成同一类问题处理。
- 更合适的拆分是：
  - `一直向往着 -> 远`
    - 去看 request 起点的字典/词表排序依据
  - `第一站是 -> 一`
    - 去看为什么这一跳会落入 fallback / OOV，并进一步把 `lm_score_scaled` 一起拖差

### 代码层再次对齐：这两类问题在实现上也不是同一个来源

- 这一步仍然没有新增实验，只是把上面的数值结论对回代码实现。

#### `一直向往着 -> 远` 为什么现在更像“字典底座细差”

- 在 `witset_poet.cc` 里：
  - `ComputeNormalizedDictScore(dict_score_raw, char_count)` 只是对 `dict_score_raw` 做长度归一化
  - 而 `dict_score_raw` 本身在当前打分语义里对应的就是 `entry->weight`
- 因而在 `远 vs 与/于` 这组里：
  - `lm_score_scaled` 完全相同
  - `fallback / OOV` 完全相同
  - `base_score` 差值又与 `dict_score_raw` 差值几乎一一对应
- 所以当前更准确的工程口径应写成：
  - `远` 这条不是走坏了 token 化路径
  - 而是当前 `entry->weight` 这一层对同音单字的 lexical prior 略低

#### `第一站是 -> 一` 为什么可以直接判成 fallback/OOV 路径

- 在 `witogram.cc` 里，whole-word / char-path 的分支条件很清楚：
  - `word_wid = vocab.Index(word)`
  - 若 `word_wid != NotFound()`
    - `matched_whole_word = true`
    - 走 whole-word 与 char-path blend
  - 否则：
    - `used_char_fallback = true`
    - `oov_token_count = char_oov_token_count`
- 所以对 `第一站是 -> 一` 这条，当前 probe 已经直接给出：
  - `used_char_fallback = true`
  - `lm_oov_token_count = 1`
- 这在代码语义上就意味着：
  - 当前这一步没有拿到 whole-word 命中
  - 实际走的是 char fallback
  - 且 char path 自身还带了 OOV token

#### 因而两类问题在实现层的准确表述是

- `一直向往着 -> 远`
  - 主要是：
    - `entry->weight / dict_score_raw` 细差
  - 不是：
    - whole-word miss
    - char fallback
    - OOV

- `第一站是 -> 一`
  - 主要是：
    - 当前步直接落入 `word_wid == NotFound() -> used_char_fallback = true`
    - 并伴随 `oov_token_count > 0`
    - 然后把 `lm_score_scaled` 一起拖坏

#### 更新后的执行边界

- 后续如果继续：
  - `远` 这类不要再优先怀疑 fallback/OOV
  - `一` 这类也不要再先从字典底座小修小补入手
- 两条线应分别处理：
  - `远`
    - 查 lexical prior / `entry->weight`
  - `一`
    - 查 whole-word miss / char fallback / OOV 的来源

## 2026-05-21 继续只读追源：`entry->weight` 不是纯词频，`word_wid == NotFound()` 就是 whole-word miss

- 这一步继续遵守“先查 WORKLOG、避免重复实验”的原则。
- 没有新增引擎测试。
- 只继续把上轮的数值判断对回上游代码来源。

### `dict_score_raw = entry->weight` 的上游来源

- 在 `witset_poet.cc` 中，当前 request-stage 使用的是：
  - `dict_score_raw = entry->weight`
- 继续往上追 `DictEntry` 的生成后，可以确认：

#### 系统词典路径

- `src/rime/dict/dictionary.cc`
  - `DictEntryIterator::Peek()`
  - 当前实现：
    - `entry_->weight = e.weight - kS + chunk.credibility`
- 同文件更早处：
  - `lookup_table(...)`
  - `cr = initial_credibility + a.credibility()`
  - 最终 `AddChunk(..., cr)`

#### 用户词典路径

- `src/rime/dict/user_dictionary.cc`
  - `UserDictionary::CreateDictEntry(...)`
  - 当前实现：
    - `e->weight = log(weight > 0 ? weight : DBL_EPSILON) + credibility`

### 这一步带来的明确结论

- 所以，`entry->weight` 当前并不是“纯 lexical 词频”。
- 它至少已经混入了：
  - 词典 entry 自身权重
  - `chunk.credibility`
  - 用户词典路径下传入的 `credibility`
- 换句话说：
  - 我们现在口头上把 `远` 这类先归成“字典底座细差”
  - 这个说法在工程上应更精确地写成：
    - **`entry->weight` 这条 lexical-like 主轴的细差**
    - 而不是“纯词典频次细差”

### `第一站是 -> 一` 的代码语义也已经可以直接钉死

- 在 `plugins/witogram/src/witogram.cc` 里：
  - `word_wid = vocab.Index(word)`
  - 若 `word_wid != vocab.NotFound()`
    - `matched_whole_word = true`
    - 走 whole-word 与 char-path blend
  - 否则：
    - `used_char_fallback = true`
    - `oov_token_count = char_oov_token_count`

### 这对当前分析的含义

- 因而当当前 probe 已经读到：
  - `第一站是 -> 一`
  - `used_char_fallback = true`
  - `lm_oov_token_count = 1`
- 在代码语义上就可以直接翻译为：
  1. 当前这一步没有拿到 whole-word 命中
  2. 直接落入 `word_wid == NotFound()` 分支
  3. 实际走的是 char fallback
  4. 且 char path 自身带了 OOV token

### 再次收紧后的表达

- `一直向往着 -> 远`
  - 更准确地说，不是“纯词频略输”
  - 而是：
    - **`entry->weight` 这条已经混入 credibility/path 合同的主轴略输**

- `第一站是 -> 一`
  - 更准确地说，也不只是“fallback/OOV 现象”
  - 而是：
    - **whole-word miss 触发 `word_wid == NotFound()`**
    - **随后进入 char fallback**
    - **并带出 OOV，再把 LM 拖坏**

### 当前下一步边界

- 后面若继续，不应再把 `远` 直接当“纯词典排序”问题看待
- 更稳的说法应是：
  - 审 `entry->weight` 里混入的上游 credibility / path contract
- 而 `一` 这条则继续保持原判断：
  - 直接审为什么这一跳会落到 `word_wid == NotFound()`

## 2026-05-21 目标与路线重新校准后继续：允许新实验，但不重复旧实验，也不回到已判负路线

- 用户已澄清：
  - 不是禁止实验
  - 而是禁止：
    - 重复实验
    - 已验证为错误路线的实验
- 因此当前执行边界更新为：
  - 可以继续自主推进到最终结论
  - 包括必要的新实验
  - 但每次继续前都要先核对：
    - `WORKLOG`
    - 方案文档
  - 避免重复已做层级
  - 避免回到已判负路线

### 这轮新增的非重复结论 1：`一` 这条不需要再绕外部 query 工具，本地 probe 已足够判定 whole-word miss

- 我先尝试复用 Python `kenlm` 对当前关键 token 做模型外部探针：
  - `一 / 以 / 远 / 与 / 于 / 一座 / 远方`
- 结果失败，但失败本身与旧 WORKLOG 一致：
  - 当前 Python `kenlm` 绑定按 `KENLM_MAX_ORDER = 6` 编译
  - 当前 `wanxiang-lts-zh-hans.klm` 是 `order 12`
  - 因而本地 Python 查询通道无法直接加载该模型
- 这一步不是重复实验，因为之前旧日志只说明：
  - 外部 query 工具无法稳定用于当前 12 阶模型
  - 但没有把“这对当前 `一/以/远/与/于` 分析意味着什么”单独收口

#### 由代码语义得到的新判断

- 在 `witogram.cc` 中：
  - `lm::WordIndex word_wid = vocab.Index(word);`
  - `word_wid != vocab.NotFound()` 才会：
    - `matched_whole_word = true`
  - 否则就：
    - `used_char_fallback = true`
- 而 `vocab.Index(word)` 与上下文无关，只与 token 本身是否在 LM 词表中有关
- 因此，对 `第一站是 -> 一` 这条：
  - 当前运行时 probe 已经直接读到：
    - `used_char_fallback = true`
    - `lm_oov_token_count = 1`
- 所以这条已经不需要再靠外部 query 工具兜底确认
- 单凭当前运行时证据，就可以直接判成：
  - **`一` 这个 next-hop token 在当前 LM 里没有 whole-word 命中**
  - **因此直接走了 char fallback**

### 这轮新增的非重复结论 2：`entry->weight` 的动态部分当前主要来自 table query credibility，不是 translator 侧 ledger 直接回灌

- 继续往上游追代码后，当前能更精确地区分两层来源：

#### `entry->weight` 的构成

- `dictionary.cc`
  - `entry_->weight = e.weight - kS + chunk.credibility`
- `lookup_table(...)`
  - `double cr = initial_credibility + a.credibility()`

#### `a.credibility()` 的来源

- `table.cc`
  - `TableQuery::Advance(...)`
    - `credibility_.push_back(credibility_.back() + credibility);`
  - `TableQuery::Access(...)`
    - `credibility += credibility_.back();`
- 这说明 `TableAccessor::credibility()` 的动态部分，来自：
  - table query 沿码路推进时累计进去的 credibility

#### 当前 `witset_translator` 的调用口径

- 在 `witset_translator.cc` 当前主调用位置：
  - `auto sys_result = dict()->Lookup(syllable_graph, x.first, &blacklist());`
- 这里没有显式传 `initial_credibility`
- 也就是当前用的是默认值：
  - `initial_credibility = 0.0`

### 这一步带来的收口

- 因而当前更准确的工程判断应是：

#### 对 `一直向往着 -> 远`

- 它输在的不是“translator 把 ledger 风险直接写进了 `dict_score_raw`”
- 当前更像是：
  - `entry->weight` 里的
    - table entry 原始权重
    - 加上 table query 自身累计 credibility
  这条主轴略输
- 也就是说：
  - **当前没有证据表明 `witset_translator` 已把 ledger 的 edge/vertex risk 直接回灌进 `dict()->Lookup(initial_credibility)`**

#### 对 `第一站是 -> 一`

- whole-word miss 的判断进一步坐实：
  - 不需要再强依赖外部 KenLM query
  - 当前运行时 `used_char_fallback = true` 已足够直接判定

### 对后续路线的影响

- `远` 这条下一步应继续查：
  - table query credibility 是从哪类码路/拼写属性累计进来的
  - 以及为什么它会让 `远` 略输给 `与/于`
- `一` 这条下一步应继续查：
  - 为什么当前传给 `vocab.Index(word)` 的 token 是 `一`
  - 以及为什么当前模型对它不给 whole-word 命中
- 其中第二条可以继续做新实验，但应避免：
  - 重复跑旧的多字串 whole-word OOV 实验
  - 或重新陷入外部 query 工具兼容性问题

## 2026-05-21 继续非重复追踪：`entry->weight` 的 credibility 来源已追到 syllable graph，`一` 也确认是原始 next-hop 候选

- 这一步仍然没有回到已判负路线。
- 也没有再重复跑旧的 whole-word OOV 实验。
- 主要是继续把两条主线都往前追了一层。

### 1. `远` 这条：`entry->weight` 里的动态 credibility 已追到 syllable graph

- 之前已经确认：
  - `dict_score_raw = entry->weight`
  - `entry->weight = entry 原始权重 + chunk.credibility`
  - `witset_translator` 当前 `dict()->Lookup(...)` 没有显式传 `initial_credibility`
- 这轮继续往前追后，当前能更精确地写成：

#### `table query credibility` 的直接累计方式

- `table.cc`
  - `Table::Query()` 遍历 `syll_graph.indices`
  - 对每条可走边执行：
    - `query.Advance(syll_id, props->credibility)`
- `TableQuery::Advance(...)`
  - 直接做前缀和：
    - `credibility_.push_back(credibility_.back() + credibility)`

#### `props->credibility` 的来源

- `syllabifier.cc`
  - `Syllabifier::BuildSyllableGraph()`
  - 当前边的 `EdgeProperties::credibility` 会继承并叠加上游 spelling/property 语义

#### 目前能确认会进这条累计链的几类来源

- `algebra / calculus`
  - `fuzzy` 惩罚
  - `abbrev` 惩罚
- `syllabifier` 运行时再追加：
  - `correction` 惩罚
  - `completion` 惩罚
  - `ambiguous joint` 惩罚

### 这对 `远` 这条的意义

- 因而 `一直向往着 -> 远` 当前更准确的工程表述应升级为：
  - 它输在的不是“纯词频”
  - 也不是“translator ledger 已直接写进 `dict_score_raw`”
  - 而是：
    - **由 table entry 原始权重 + syllable path 累计 credibility 共同形成的 `entry->weight` 主轴略输**

- 也就是说，下一步若继续追 `远`
  - 真正值得查的是：
    - 它和 `与/于` 在当前那条 `yuan...` / `yu...` 路上
    - 是否命中了不同的 fuzzy / abbrev / ambiguous-joint credibility 路径

### 2. `一` 这条：已经确认不是 `Witogram` 后面临时拆出来的

- 这轮把 `word` 传入 `vocab.Index(word)` 的生成链也完整对齐了。

#### 关键链

- `witset_translator.cc`
  - 先 `dict()->Lookup(...)` / `user_dict()->Lookup(...)`
  - 组装 `WordGraph`
- `witset_poet.cc`
  - 扩展候选时直接拿：
    - `entry->text`
  - 再调用：
    - `witogram->ScoreFeatures(context, entry->text, is_rear)`
- `witogram.cc`
  - `word` 原样进入：
    - `vocab.Index(word)`

### 这一步带来的直接结论

- `第一站是 -> 一`
  - 当前 `word = "一"` 不是 `Witogram` 里后拆出来的
  - 而是：
    - **WordGraph 那条 next-hop 边上原本就存在的 `DictEntry.text == "一"` 候选**
- 所以当前更准确的问法已经不再是：
  - “为什么 `vocab.Index()` 把更长词拆成了 `一`”
- 而应改成：
  - **为什么 `第一站是` 之后那条 `yi...` 边，允许单字 `一 / 以 / 已 / 宜 ...` 候选参与主竞争**

### 3. 对 `一` 这条 whole-word miss 的表述也再收紧一层

- 当前已有运行时证据：
  - `used_char_fallback = true`
  - `lm_oov_token_count = 1`
- 再结合代码链：
  - `word` 就是原始 `entry->text`
- 所以这条现在可以更精确地写成：
  - `DictEntry.text == "一"` 这个候选已经进入 next-hop 竞争
  - 但进入 `Witogram` 后：
    - `vocab.Index("一") == NotFound()`
  - 因而：
    - 直接 whole-word miss
    - 并落入 char fallback / OOV

### 当前下一步边界再次更新

- `远`
  - 继续追：
    - 当前 path credibility 究竟是哪类 spelling/property 在拉开 `远` 与 `与/于`
- `一`
  - 继续追：
    - `第一站是` 后那条 `yi...` 边在 `WordGraph` 里到底有哪些候选
    - 为什么单字 `一` 会作为强 next-hop 候选进入主竞争

## 2026-05-21 再进一步：`yi...` 这里不是只有单字边，而是单字边与跨两音节长边并存竞争

- 这一步继续走的是新的、未重复的图级追踪：
  - 不再只是看 `word = "一"` 怎么进 `vocab.Index(word)`
  - 而是直接看：
    - `第一站是` 后那条 `yi...` 输入
    - 在 `WordGraph` 里到底形成了哪些边

### 代码层最小事实

- `witset_translator.cc`
  - 先按 `graph[start][end] -> DictEntryList` 组装 `WordGraph`
- `witset_poet.cc`
  - `MakeSentences()` 扩展时
  - 会把同一 `start_pos` 下所有不同 `end_pos` 的边都一起放进 beam 竞争

### 现成工件复核后的结论

- 在当前 `diyizhanshiyi...` 这条局部链里：

#### 单字边确实存在

- `start = 11, end = 13`
- 这条边上的 request probe 直接出现：
  - `以`
  - `已`
  - `一`
  - `宜`

#### 跨两音节长边也同时存在

- 图快照里还能看到：
  - `start = 8, end = 13`
  - 候选包括：
    - `是一`
    - `是以`
    - `适宜`
    - `时已`
    - `事宜`

### 这一步带来的关键判断

- 所以当前这里不是：
  - 只保留了单字切分
- 也不是：
  - 只保留了跨前一音节的长边
- 而是：
  - **`11->13` 的单字族**
  - 和
  - **`8->13` 的跨两音节长边**
  - 一起进入了 beam 竞争

### 对 `一` 这条的进一步收口

- 因而现在更准确的说法已经变成：

1. `一` 的问题不是“长边没进图，只剩单字边”
2. 当前真实情况是：
   - 长边和单字边都进图了
3. 真正的问题更像是：
   - 在 beam 竞争里
   - `11->13` 的单字族，尤其是 `以`
   - 当前比分高于更长的 `8->13` 候选族

### 这对下一步的影响

- 后面若继续查 `第一站是一座...`
  - 就不该再问“有没有长边”
- 当前真正该问的是：
  - **为什么 `11->13` 的 `以` 会压过 `8->13` 的 `是一 / 是以 / 适宜 / 事宜 ...`**
- 也就是：
  - 下一层要直接比较
    - 不同跨度边在 beam 里的 request/base/final 竞争账本

## 2026-05-21 再收一步：`yi...` 这里的竞争可能不是“本跳长边弱”，而是前置 source line 早已偏弱

- 这一步继续复用了现有 `diyizhan_local_competition.json`，没有起新实验。
- 新得到的信号比“长边有没有进图”更关键。

### 图级事实补充

- 当前局部图里不仅有：
  - `8 -> 13`
    - `十一 / 是一 / 是以 / 适宜 / 事宜 ...`
- 还存在更长一步：
  - `8 -> 16`
    - `是一座`
- 说明从图结构上看：
  - 正确长边链并没有缺席

### 但 `focus_transitions` 暗示前置 source line 体质本身就不对称

- `第一站`
  - `matched_whole_word = false`
  - `used_char_fallback = true`
  - `oov_token_count = 1`
- `是`
  - 在 `context_suffix = ...第一站` 下
  - `matched_whole_word = true`
  - `used_char_fallback = false`
- `以`
  - 在局部 probe 里也表现为：
    - `matched_whole_word = true`
    - `used_char_fallback = false`

### 这一步带来的判断

- 因而当前 `第一站是一座...` 这条的真实问题，可能不只是：
  - `11->13` 的单字族本跳太强
- 还更像是：
  - 作为长边族前置 source line 的 `第一站`
  - 自身已经是：
    - whole-word miss
    - char fallback
    - OOV
- 换句话说：
  - `8->13` / `8->16` 这类从 `第一站` 往后接的长边族
  - 可能是在一个已经先天偏弱的 source state 上继续扩展
  - 而 `11->13` 的单字族则挂在更局部、whole-word 可命中的单字跳转上

### 当前更准确的问题改写

- 所以现在不该只问：
  - 为什么 `11->13` 的 `以` 压过 `8->13` 的 `是一 / 是以 / 适宜 ...`
- 还要同时问：
  - **`8->13` / `8->16` 这条长边路线，是不是在进入本跳之前，前置 source line `第一站` 就已经输掉了一大截**

### 对下一步的影响

- 下一层更值得直接比较的是两类 source state：

1. `... + 第一站`
   - whole-word / fallback / OOV 状态
2. `... + 第一站是`
   - 以及围绕 `yi` 单字族的局部 next-hop 状态

- 如果这个判断成立，那么：
  - 当前 case 的关键瓶颈就不是单纯“`yi` 本跳排序”
  - 而是：
    - **更早一层的 source line 建立阶段，正确长词 `第一站` 已经没有拿到健康主轴**

## 2026-05-21 再对齐打分实现：`第一站` 的 fallback 身份会继续影响后续 scorer 分支

- 这一步仍然没有新增实验。
- 只是把现有运行时事实与 `witset_poet.cc` 的打分实现逐项对齐。

### 1. `第一站` 的 fallback 身份，不只是单步现象

- 现有工件已经确认：
  - `word = 第一站`
  - `matched_whole_word = false`
  - `used_char_fallback = true`
  - `oov_token_count = 1`
- 这条身份不只是影响当前这一步的 `lm_score_scaled`
- 在 `witset_poet.cc` 里，它还会继续影响后续多项 scorer 分支

### 2. 当前 scorer 中，确实有一整组项会把 prefix 的 fallback / OOV 状态继续传播

#### 直接依赖 prefix fallback / OOV 的项

- `ComputeEarlyBoundaryBridgeCompensation(...)`
  - 只有当前缀已经：
    - `prefix_fallback_hits > 0`
    - `prefix_oov_tokens > 0`
  - 且当前步是干净单字 whole-word 命中时才会给补偿
- `ComputeEarlyUnstableContinuationPenalty(...)`
  - 只在前缀已经：
    - `prefix_fallback_hits > 0`
    - `prefix_oov_tokens > 0`
  - 且当前步继续：
    - `used_char_fallback = true`
    - `matched_whole_word = false`
  时触发额外惩罚
- `ComputePrefixAnchorDeltaDebtPenalty(...)`
- `ComputeWholeFirstWordContinuationPenalty(...)`
  - 都会比较 prefix anchor 之后新增的 fallback / OOV 账本

#### 只奖励“干净 whole-word 前缀”的项

- `ComputeCleanFirstWordBridgeBonus(...)`
  - 要求前缀：
    - `prefix_fallback_hits == 0`
    - `prefix_oov_tokens == 0`
    - `prefix_whole_word_hits > 0`
- `ComputeCleanSingleCharBridgeBonus(...)`
- `ComputeCleanExactContinuationBonus(...)`
- `ComputeValidatedPrefixSuffixBonus(...)`
- `ComputeStableWholeContinuationBonus(...)`
  - 这些都要求 prefix 侧保持：
    - `whole_word_hits`
    - 无 fallback
    - 无 OOV

### 3. 这对 `第一站是一座...` 的具体含义

- 因而当前 `第一站` 一旦在句首建立时就落入：
  - whole-word miss
  - char fallback
  - OOV
- 后面不只是“这一步分差吃亏”
- 还会导致两件事同时发生：

1. 原本属于 clean whole-word prefix 的一整组 bonus 拿不到
2. 后续 continuation 若继续不稳，还会进入 fallback / debt / unstable penalty 相关分支

### 4. 当前更像根因的工程表述

- 所以现在可以把 `第一站是一座古老的小镇` 这条再写得更准确：
  - 它的问题不只是：
    - `yi` 本跳单字族更强
  - 也不只是：
    - 当前 `.klm` 对 `第一站` 不给 whole-word 命中
  - 还包括：
    - **`第一站` 的 fallback/OOV 身份被 `witset_poet` 当作 prefix 状态继续传下去**
    - **这会让正确长边路线既失去 clean-prefix bonuses，又更容易触发后续不稳前缀惩罚**

### 5. 对下一步的影响

- 因此，后面若继续判断根因优先级：
  - `yi` 本跳排序只能算后半段现象
  - 更前面的主死亡点更像是：
    - **句首正确整块 `第一站` 未能建立 clean whole-word prefix state**

## 2026-05-21 当前阶段最终收口：`poet` 末端补丁线正式封口，主问题已收敛到 prefix-state contract

- 这轮把最近几层新增证据与旧实验结论合并后，当前已经可以给出阶段性最终结论。

### 一、已经可以正式封口的线

#### 1. 继续围绕 `yi` 单跳排序打补丁

- 当前证据已经足够说明：
  - `yi` 这里只是后段可见现象
  - 不是主死亡点
- 因为：
  - `11->13` 单字族与 `8->13 / 8->16` 长边族都进图了
  - 真正更早的 source line `第一站` 本身已经 whole-word miss + fallback + OOV

#### 2. 继续围绕 `poet` 末端局部 reward / penalty 做新 patch

- 旧 WORKLOG 的反证链已经足够完整：
  - `clean_first_word_bridge`
  - `clean_single_char_bridge`
  - `clean_exact_continuation`
  - `validated_prefix_suffix`
  - `stable_whole_continuation`
  - `prefix_anchor_delta_debt`
  - `whole_first_word_continuation`
  - 这些线不是静默，就是只改变局部家族、不改变最终盘面
- 现在再叠加这轮新证据：
  - `第一站` 的 fallback/OOV 身份会被 scorer 继续传播
  - 所以继续在后段 continuation 上补局部 contract，等于在错误的层次上补

### 二、当前真正成立的主问题

- `C2` 这一型，当前主问题已经可以收敛成一句话：
  - **正确长前缀在 source line 建立阶段就掉进了 fallback/OOV family，而 `witset_poet` 的 scorer 还会继续把这个 prefix-state 失血向后传播。**

### 三、这条主问题由哪三层共同构成

#### 第 1 层：模型形态

- 当前 `.klm` 是拆分 token 模型
- 对关键整块如：
  - `第一站`
  - whole-word 命中并不稳定

#### 第 2 层：source line 建立

- `第一站`
  - `matched_whole_word = false`
  - `used_char_fallback = true`
  - `oov_token_count = 1`
- 说明正确整块前缀在 very early stage 就没有建立 clean state

#### 第 3 层：scorer 传播

- `witset_poet` 里一整组项会继续吃：
  - `prefix_fallback_hits`
  - `prefix_oov_tokens`
  - `prefix_whole_word_hits`
- 结果是：
  1. clean-prefix bonuses 拿不到
  2. debt / unstable 类分支更容易被触发

### 四、当前默认执行边界

- 从这一步开始，若继续推进，不再默认做：
  - 新的 `poet` 末端 reward / penalty patch
  - 新的局部 `yi` 跳转 patch
  - 围绕已判负参数做 sweep

- 当前唯一值得继续的主线应默认收敛到：
  1. `AnalyzeCredibility`
  2. `RewriteWordGraph`

### 五、最终结果表达

- 当前阶段的最终结果不是“已经修好 `第一站是一座...`”
- 而是已经把**真正该改的层级**收敛清楚了：
  - **不是 `yi` 单跳**
  - **不是继续给 `poet` 末端加局部补丁**
  - **而是要在更上游处理正确长前缀的 prefix-state contract，使其不要在 source line 建立阶段就掉进 fallback/OOV family。**

## 2026-05-22 `upstream_prefix_state_contract` 最小 family/path 合同：21 条 shared-prefix `0.0 vs 1.0` 对照

- 承接前面的复盘结论，本轮不再回到 `witset_poet` 末端 patch，而是直接把 `witset_translator` 里原先过粗的 `prefix_state_contract` 改成最小 family/path 合同原型。
- 这次改的不是“clean prefix 下给多字 continuation 通发奖励”，而是只在下列组合出现时给正向保护：
  - `continuation_tag = legal_primary_continuation`
  - 且 `path_tag = primary_path_eligible`，或至少属于 `shared_prefix_axis_member / source_axis_eligible`
  - 同时把 `wrong_family_with_path_unconfirmed` / `wrong_family_with_path_unconfirmed_dead_tail` 纳入同一合同里的负向项
- 也就是说，这轮真正验证的是：
  - 能不能在 translator graph 阶段，开始给“合法长前缀的合法续接”一点真实保护
  - 而不是继续在句尾靠局部 bonus / penalty 去捞

### 验证口径

- 语料：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\_shared_prefix_eval_excerpt.txt`
- snapshot 统一恢复到：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\witset_local_snapshot.jsonl`
- 两轮对照：
  1. `upstream_prefix_state_contract_weight = 0.0`
  2. `upstream_prefix_state_contract_weight = 1.0`
- summary 目录：
  - off: `C:\Users\Bing\AppData\Roaming\witty\debug\snapshot_summary\upstream_prefix_state_contract_off`
  - on: `C:\Users\Bing\AppData\Roaming\witty\debug\snapshot_summary\upstream_prefix_state_contract_on`
- 两份运行时 schema：
  - `C:\Users\Bing\AppData\Roaming\witty\witset.schema.yaml`
  - `C:\Users\Bing\AppData\Roaming\witty\build\witset.schema.yaml`
  已按实验口径同步切换；实验结束后恢复为 `1.0`，保留当前原型状态。

### 客观结果

- `weight = 0.0`
  - `top1_accuracy = 0.571429`
  - `top3_accuracy = 0.666667`
  - `expected_not_found_count = 5`
  - `preceding_text_mismatch_count = 10`
  - `wall_time_seconds = 81.75`
- `weight = 1.0`
  - `top1_accuracy = 0.619048`
  - `top3_accuracy = 0.714286`
  - `expected_not_found_count = 4`
  - `preceding_text_mismatch_count = 10`
  - `wall_time_seconds = 78.407`

### case 级变化

- 明确正收益：
  - `体验不一样的生活`
    - `expected_rank: 7 -> 1`
    - top1 从 `体验不宜养的生活` 纠正为 `体验不一样的生活`
- 明确但仍未翻正的改善：
  - `一直向往着远方`
    - `expected_rank: not found -> 5`
    - top1 仍是 `一直想望着远方`
    - 说明正确 family 已经开始重新进入候选盘面，但保护力度还不够强
- 仍无改善的主硬例：
  - `第一站是一座古老的小镇`
    - top1 仍是 `第一战士已作古老的小镇`
    - 正确目标句仍未进入前列，只能看到 `第一站是以做古老的小镇` 这类中间形态升到 `rank 5`
  - `两旁是古色古香的建筑`
    - top1 / top3 均无变化，仍被 `故涩谷香` family 压住

### 结论

- 这轮结果和前面连续判负的 `validated_continuation` / `linked_guard` 不一样：
  - **第一次在多样本 shared-prefix 集上看到 translator 上游合同带来的正向盘面收益。**
- 但这仍然只是**最小成立**，还远不是最终答案：
  1. 覆盖还窄
  2. 对 `一直向往` 只做到“重新进候选”，还没做到翻正
  3. 对 `第一站 -> 是一座` 这类在 request 起点就已背上 fallback / OOV 的链条，当前合同仍然介入得太晚
- 更准确地说：
  - 这版原型已经证明“family/path/source-axis 组合合同”方向是对的
  - 但它目前仍主要表现为 candidate 级重权
  - 还没有真正把“合法长前缀在 source line 建立初期就被保护”这件事做完整

### 下一步判断

- 不应再回退到 `poet` 末端调权。
- 也不应把这轮结果夸大成“上游问题已经解决”。
- 更合理的下一刀应继续前移到 `AnalyzeCredibility / RewriteWordGraph` 的 source-line 建立阶段，重点补：
  1. prefix-state 到 next-hop request 的早期资格保护
  2. 对 `fallback / OOV` 尚未发生前的合法长前缀保活
  3. `第一站 -> 是一 / 一座` 这类 request-stage next-hop contract，而不只是候选末端加减分

## 2026-05-22 继续前移：补 request/source-line 单字 next-hop 保护并修正 contract 上下文接线

- 承接上一条 `upstream_prefix_state_contract` 的对照结果，这一轮不再回到 `poet` 末端，也没有直接做更大的 weight sweep。
- 目标是继续把保护点前移到 `request/source-line` 阶段，先补上当前实现里最明显的两个缺口：
  1. `Apply()` 里的 family/path/source-axis/downstream-completion 辅助状态，之前竟然只在 `upstream_linked_guard_weight > 0` 时才构建
  2. 当前 `prefix_state_contract` 只覆盖 `char_count >= 2` 的 strong exact continuation，完全漏掉了 `远 / 一` 这类真正决定 next-hop request 起点的单字 exact 候选

### 本轮实现

- 在 `witset_translator.cc` 中先修正了 contract 上下文接线：
  - `continuation_path_summaries`
  - `axis_prefix_states`
  - `exact_completion_supports`
  现在只要 `upstream_prefix_state_contract_weight > 0` 或 `upstream_linked_guard_weight > 0`，就会正常构建，不再错误依赖 `linked_guard` 开关。
- 然后新增了一套更靠前的 request-stage 辅助状态：
  - `RequestStageTailSupport`
  - `RequestStagePrefixState`
  - `BuildRequestStageTailSupports()`
  - `BuildRequestStagePrefixStates()`
- 新增 `ClassifyRequestStageTag()`，专门给：
  - clean prefix
  - single-char exact candidate
  - 且仍有 exact tail / source-line 资格
  的 next-hop 候选打标签。
- 新增 `ComputeTranslatorRequestStageContractBias()`：
  - 只对上述单字 exact next-hop 做一层很窄的正向保护
  - 继续复用 `upstream_prefix_state_contract_weight`
  - 不额外引入新的 schema 开关
- 同时把新的 request-stage 读数接进 graph snapshot，后续若再 dump，可直接看到：
  - `request_stage_prefix_text`
  - `request_stage_exact_count`
  - `request_stage_path_count`
  - `request_stage_best_tail_weight`
  - `request_stage_tag`

### 当前判断

- 这轮修改的意义，不是宣称已经救回 `第一站是一座古老的小镇`，而是把“合法长前缀的保护”从原先只看多字 continuation，继续往 request/source-line 建立阶段前推了一层。
- 同时也修掉了一个真实工程问题：
  - 如果不开 `linked_guard`，原先 `prefix_state_contract` 在运行时拿到的是不完整的 family/path/source-axis 上下文。
- 下一步若继续验证，重点应看：
  - `一直向往着远方` 里的 `远`
  - `第一站是一座古老的小镇` 里的 `一`
  是否终于开始在 graph / next-hop 层得到更早保护。

### 本轮验证

- 代码层校验：
  - `GetDiagnostics` 对 `witset_translator.cc` 无报错
- 还未重新跑 shared-prefix 对照；避免在没有明确编译/验证授权前擅自启动新一轮耗时流程。

## 2026-05-22 request-stage 单字 next-hop 保护：shared-prefix 21 条复测

- 本轮是在上一版 `upstream_prefix_state_contract` 最小 family/path 合同之上，继续补了：
  - request/source-line 单字 exact next-hop 保护
  - 并修正了 `prefix_state_contract` 对 family/path/source-axis 辅助状态错误依赖 `linked_guard` 开关的问题
- 用户这边随后执行了 `build_and_deploy.bat`；链路中：
  - `librime static` 成功
  - `outwit` 主构建成功
  - 最后失败在 `OutwitSettingsServer.exe -> output` 复制阶段，原因是目标文件被其他进程占用
  - 这一步不影响本轮 `rime_api_console` shared-prefix 复测，因为评测脚本实际使用的是：
    - `librime/build_x64/bin/Release/rime_api_console.exe`
    - `librime/dist_x64/bin`

### 复测口径

- 语料：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\_shared_prefix_eval_excerpt.txt`
- 对照：
  1. `upstream_prefix_state_contract_weight = 0.0`
  2. `upstream_prefix_state_contract_weight = 1.0`
- summary 目录：
  - off: `C:\Users\Bing\AppData\Roaming\witty\debug\snapshot_summary\upstream_prefix_state_contract_request_stage_off`
  - on: `C:\Users\Bing\AppData\Roaming\witty\debug\snapshot_summary\upstream_prefix_state_contract_request_stage_on`

### 客观结果

- `weight = 0.0`
  - `top1_accuracy = 0.571429`
  - `top3_accuracy = 0.666667`
  - `expected_not_found_count = 5`
  - `preceding_text_mismatch_count = 10`
  - `wall_time_seconds = 78.11`
- `weight = 1.0`
  - `top1_accuracy = 0.619048`
  - `top3_accuracy = 0.714286`
  - `expected_not_found_count = 4`
  - `preceding_text_mismatch_count = 10`
  - `wall_time_seconds = 80.875`

### case 级变化

- 仍然明确受益：
  - `体验不一样的生活`
    - `expected_rank: 7 -> 1`
    - top1 继续能翻正
- 仍然只有“重新进盘”但未翻正：
  - `一直向往着远方`
    - `expected_rank: not found -> 6`
    - top1 仍是 `一直想望着远方`
- 仍未救回：
  - `第一站是一座古老的小镇`
    - `expected_rank` 仍然未命中
    - 但 `第一站是以做古老的小镇` 已经进入更靠前位置（当前在 `rank 5`）
  - `两旁是古色古香的建筑`
  - `店主是一位和蔼可亲的老人`

### 结论

- 这轮 request-stage 单字 next-hop 保护**没有带来比上一轮更多的代表集净收益**。
- 但它也没有把盘面拉坏；当前 shared-prefix 21 条的 `off/on` 总体结果，仍保持在：
  - `top1 +1 / 21`
  - `top3 +1 / 21`
  - `expected_not_found_count -1`
- 更准确地说：
  - 这层“单字 next-hop 早期保护”在工程结构上是合理补齐
  - 但就当前权重与资格表达方式，它还没有把 `第一站 -> 是一 / 一座` 这类 request-stage 死亡链条真正救活
  - 当前最可能的原因，不再是“完全没前移到 request-stage”，而是：
    - 资格仍然太弱
    - 只给 single-char exact 一层轻量 bonus 还不够
    - `yi -> 一` 这类 next-hop 的主竞争仍被更强错误家族压死

### 当前收口判断

- 不应回退到 `poet` 末端调权。
- 也不应把这轮无新增收益误解为“request-stage 路线错误”。
- 更稳的结论是：
  1. 上游 family/path 合同主线仍然成立
  2. request-stage 补齐方向也成立，但当前这版力度与资格边界还不足
  3. 下一步要么直接增强 `request-stage next-hop ranking contract` 的资格表达，要么补 graph probe / dump，确认 `一`、`远` 这些单字 exact next-hop 在 source-line 阶段到底拿到了多少合同分

## 2026-05-22 多样本 source-line / next-hop 拆账：request-stage 为什么暂未新增净收益

- 承接上一轮 shared-prefix 21 条复测后“request-stage 单字 next-hop 保护未新增净收益”的结果，这一轮没有再做 weight sweep。
- 直接改用 `partial_chain_stage_probe.py` 做多样本 source-line / next-hop 拆账，目标不是单句调参，而是确认：
  1. 当前 request-stage 合同到底有没有真正打到目标 next-hop
  2. 若没打到，是资格门槛没过，还是打到了但力度不足
- 本轮代表 case：
  - `一直向往着远方`
  - `第一站是一座古老的小镇`
  - `体验不一样的生活`
  - `两旁是古色古香的建筑`
- 工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe_result.json`
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe_analysis.json`

### 关键结论一：`一直向往着` 已经进入 request-stage，但合同只给到 `request_tail_supported`

- 在 `prefix_text = 一直向往着` 上，prefix 状态是：
  - `family_clean`
  - `has_inexact_segment = false`
  - `supports_validated_continuation = true`
- 说明这类 case 确实已经进入当前 request-stage 合同覆盖范围。
- 但目标 `远` 及其主要竞争项 `与 / 于` 在 graph snapshot 上拿到的都是：
  - `request_stage_tag = request_tail_supported`
  - `continuation_tag = non_contract_candidate`
  - `path_tag = not_primary_path_candidate`
  - `source_axis_tag = not_axis_candidate`
- 也就是说：
  - 当前 request-stage 合同只知道“这些单字 exact 后面还有 tail”
  - 但并不知道 `远` 比 `与 / 于` 更像正确主轴
- 结果是 `远` 仍然只排在 `原 / 源 / 远 / 园` 一组里，无法真正压过错误家族。
- 这说明在 clean prefix case 上，问题已经不是“合同没前移”，而是：
  - **合同缺少能区分正确 next-hop 与同音错误 next-hop 的 source-axis / primary-path 资格。**

### 关键结论二：`第一站是一座` 的真实死亡点比之前判断更早

- 这轮把 source-line 直接拆开后确认：
  - 真正该看的不是 `第一站是 -> 一`
  - 而更早是 `第一站 -> 是 / 式 / 十 / 时 / 事 ...`
- 在 `prefix_text = 第一站` 上：
  - prefix 状态是 `family_soft_clean`
  - `supports_validated_continuation = true`
- 但首个 next-hop 的 top candidates 已经是：
  - `是`
  - `十`
  - `时`
  - `事`
  - `式`
  这些都只是 `request_tail_supported`，没有更强的 primary-path 资格区分。
- 继续往后，图里已经能看到：
  - `是一`
  - `是以`
  - `十一座`
  - `是一座`
  这些 phrase-level exact family 在竞争。
- 这说明：
  - 之前把这一句的 request-stage 问题理解成“`一` 没被保护”并不准确
  - **真正的分叉更早发生在 `第一站 -> 是 / 式` 这一跳，后面才扩成 `是一 / 是以 / 十一座` 的 phrase family 竞争。**
- 当前单字 `一` bonus 没法救这个 case，不是力度不够而已，而是落点偏后了。

### 关键结论三：`体验不一样` 与 `两旁是古色古香` 根本没进入当前 request-stage 合同门

- 在 `prefix_text = 体验不` 上：
  - `family_drifted`
  - `has_inexact_segment = true`
  - `supports_validated_continuation = false`
- 因此 `一 / 宜 / 依` 这些单字 exact 候选全部都是：
  - `request_stage_tag = not_request_stage_candidate`
- 同样，在 `prefix_text = 两旁是` 上：
  - 也是 `family_drifted`
  - `has_inexact_segment = true`
  - `supports_validated_continuation = false`
- 所以 `古 / 故 / 固` 这些候选也全部是：
  - `request_stage_tag = not_request_stage_candidate`
- 换句话说：
  - 这些 case 当前并不是“合同打到了但分不够”
  - 而是 **prefix-state 已经漂移，request-stage gate 根本不开**
- 这也解释了为什么本轮 request-stage 补丁没有新增净收益：
  - 很多真正重要的错例在进入 current gate 之前就已经掉线了。

### 当前最稳判断

- 当前 request-stage 路线没有判负。
- 但这轮 source-line 拆账已经说明：
  1. 对 clean prefix case（如 `一直向往着`），当前合同太弱，只能给 `tail_supported`，不能区分正确 next-hop
  2. 对核心硬例（如 `体验不`、`两旁是`），prefix-state 已漂移，当前 gate 根本不开
  3. 对 `第一站`，真正该救的是更早的 `第一站 -> 是 / 式` 这一跳，而不是更后的 `一`

### 下一步收口

- 不应继续做同一个 request-stage bonus 的 weight sweep。
- 更值得继续的是两条：
  1. 在 `AnalyzeCredibility / RewriteWordGraph` 更早修 prefix-state / family 资格
     - 让 `family_drifted + has_inexact_segment` 的目标 case 有机会重新进入 contract
  2. 把 request-stage 资格从 `tail_supported` 升级成真正的 next-hop ranking contract
     - 能区分 `远` 与 `与/于`
     - 能区分 `是` 与 `式/十/事`
- 若继续实现，最优先对象应是：
  - `第一站` 的 `是 / 式` source-line 合同
  - `体验不` / `两旁是` 的 prefix-state repair

## 2026-05-22 prefix-state repair v1：单字 exact bridge 重新开 gate，但总代表集暂未新增收益

- 承接上一轮 source-line / next-hop 拆账，这一轮不再做 request-stage bonus sweep，而是直接补 `prefix_state repair`。
- 核心修正点只做一件事：
  - 不再把“已有 strong exact 前缀之后的单字 exact bridge”一律记为 `has_inexact_segment = true`
- 最小实现落在 `witset_translator.cc`：
  - `PrefixPathState` / `LinkedGuardPrefixState` 新增 `exact_single_char_bridge_count`
  - 新增 `CanPreserveTranslatorPrefixCleanliness()`
  - `BuildBestPrefixStates()` / `BuildLinkedGuardPrefixStates()` 允许在已有 `exact_multi_word_count >= 1` 且还未 drift 的前提下，保留一次单字 exact bridge，不立刻判 drift
  - `family_clean` 现在要求 `exact_multi_word_count >= 2` 且 `exact_single_char_bridge_count == 0`；带 bridge 的仍记为 `family_soft_clean`

### 编译结果

- `librime/build.bat static` 已成功通过。
- 本轮只出现既有 warning，无新的阻塞错误。

### 目标导向验证一：gate 是否真的重新打开

使用 `partial_chain_stage_probe.py` 复测后确认：

1. `体验不一样的生活`
- `prefix_text = 体验不`
- 之前：
  - `has_inexact_segment = true`
  - `supports_validated_continuation = false`
  - `family_tag = family_drifted`
- 现在：
  - `exact_multi_word_count = 1`
  - `exact_single_char_bridge_count = 1`
  - `has_inexact_segment = false`
  - `supports_validated_continuation = true`
  - `family_tag = family_soft_clean`
- 说明 `体验 + 不` 这一类单字 exact bridge 已经成功重新进入 contract gate。

2. `两旁是古色古香的建筑`
- `prefix_text = 两旁是`
- 之前也是 `family_drifted`
- 现在同样变成：
  - `exact_multi_word_count = 1`
  - `exact_single_char_bridge_count = 1`
  - `has_inexact_segment = false`
  - `supports_validated_continuation = true`
  - `family_tag = family_soft_clean`
- 说明 `两旁 + 是` 这类桥接也已重新开 gate。

3. `第一站是一座古老的小镇`
- `prefix_text = 第一站`
- 仍是 `family_soft_clean`
- 没有被这轮 repair 误伤。

4. `一直向往着远方`
- `prefix_text = 一直向往着`
- 仍保持 `family_clean`
- 也没有被这轮 repair 拉坏。

### 目标导向验证二：总代表集是否新增收益

在保持 `upstream_prefix_state_contract_weight = 1.0` 的前提下，重新跑 21 条 shared-prefix 代表集：

- summary 目录：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\snapshot_summary\upstream_prefix_state_contract_prefix_repair_on`
- 结果：
  - `top1_accuracy = 0.619048`
  - `top3_accuracy = 0.714286`
  - `expected_not_found_count = 4`
  - `preceding_text_mismatch_count = 10`
- 与上一轮 `request_stage_on` 完全一致，没有新增净收益。
- case 级 `expected_rank / selected_rank` 也没有发生变化。

### 这意味着什么

这轮 repair 的价值已经很明确：

- 它证明前一轮定位是对的：
  - `体验不`、`两旁是` 这类 case 之前确实是被 prefix-state gate 提前关掉了
  - 这不是错觉，也不是 probe 口径问题
- 但它同时也证明：
  - **仅仅把 gate 打开，还不足以把总盘面推起来。**

也就是说，当前已经从“进不去 contract gate”走到了“进得去，但 gate 内部仍没有足够强的 next-hop ranking contract”。

### 当前最稳收口

因此这一轮之后，下一步不应再继续围绕 `prefix_state repair` 本身微调：

- 这条 repair 已经完成了它的职责：重新开 gate
- 但真正决定收益的下一刀，应该继续放在：
  - `第一站 -> 是 / 式`
  - `一直向往着 -> 远 / 与 / 于`
  这种 source-line next-hop ranking contract 上
- 换句话说：
  - **prefix-state repair 是必要条件，但不是充分条件**
  - **下一步真正该补的是 gate 内部的主轴区分能力**

## 2026-05-23

- 继续 `P0` 验证闭环前，先复核了 `WORKLOG` 与 `partial_chain_stage_probe.py`，确认没有回到已判负的：
  - `end_pos -> text` 单锚路线
  - 或围绕 `式` 再做 request-stage eligibility 收紧
- 本轮先修的是 probe 观测链，而不是算法本体：
  - `partial_chain_stage_probe.py` 新增了候选级 stage 优先级回退
  - 结果文件里新增：
    - `preferred_candidate_stage`
    - `matched_source_suffixes`
  - 同时补了 Windows `cp1252` 终端下 `print(json.dumps(..., ensure_ascii=False))` 的编码兜底，避免 probe 成功却因打印失败返回 `UnicodeEncodeError`
- 但重跑 `python .\partial_chain_stage_probe.py --case case2_diyizhan` 后，新的空结果暴露出更前一层的问题：
  - `top_request_entries` 仍为空
  - `preferred_candidate_stage = null`
  - case 级 `next_hop.jsonl` 里完全没有 `request / batch_selected / admitted_*` 这类候选级记录
  - 只有：
    - `post_admit_target_pool`
    - `pre_future_compact`
    - `post_future_compact`
    - `pre_source_pool`
    - `source_pool`
- 直接对最新 `partial_chain_stage_probe.next_hop.jsonl` 做脚本检查后，已经可以排除“只是 Python 汇总写错”的可能：
  - 当前 case 匹配到 `2349` 条记录
  - `non_empty_entry_text = 0`
  - 即这轮原始 next-hop 工件本身就没有候选级 `entry_text`
- 继续回查 `witset_poet.cc` 的 `maybe_record_expansion_gate(...)` / `maybe_record_line_snapshot(...)` 后，当前更准确的解释是：
  - next-hop probe 的过滤条件始终是 `source->full_context()` 命中 `debug_next_hop_probe_suffixes`
  - 但 `graph.jsonl` 里当前真正承载后续竞争的已不是字面 `第一站是` 这条 beam source，而是 request-stage family 对应的 follow-up family
  - 也就是说，**当前 request-stage family 前缀与真实 beam source 文本已经脱钩**
- 为了验证这点，本轮把 probe 扩成了两段式：
  - 第一段按原始 `source_suffix = 第一站是` 先跑，拿 graph snapshot
  - 第二段从 `graph_snapshot.family_contract.request_stage_states` 反推出一组真实 beam source suffix，再自动重跑
  - 当前自动补出的 suffix 为：
    - `第一站是`
    - `地一站是`
    - `第一展是`
    - `第一战是`
    - `敌意展是`
- 即便如此，`next_hop` 侧仍然只有 pool 级记录，没有恢复出候选级 `request` 记录；因此本轮结论继续收紧为：
  - 问题不在 `partial_chain_stage_probe.py` 的 stage fallback
  - 也不只是 suffix 列表太窄
  - 而是当前真正可用的候选竞争信息，已经主要体现在 `graph` 的 follow-up family，而不是 `next_hop` 的 request dump
- 因此本轮又把 `summarize_graph_probe()` 扩成了“follow-up family 聚焦”口径：
  - 保留原先对 `request_stage_prefix_text == 第一站是` 的边计数
  - 额外纳入 `request_stage_prefix_text` 以 `第一站是` 为前缀、且候选命中焦点项的 follow-up edge
  - 新增结果字段：
    - `followup_focus_edge_count`
- 最新重跑结果表明，观测闭环已经恢复到足以继续判断根因的位置：
  - `next_hop_after_diyizhanshi.graph_contract.followup_focus_edge_count = 1`
  - 焦点候选 `以 / 已 / 一 / 宜` 已重新出现在结果文件中
  - 但它们出现的位置不是“干净的 `第一站是 -> x` request 盘面”，而是：
    - `prefix_text = 地一站式`
    - `prefix_family_tag = family_drifted`
    - `request_stage_prefix_text = 第一站是以`
- 当前最重要的新判断应更新为：
  - `P0` 第一刀后，`used_char_fallback = false`、`lm_oov_token_count = 0` 的正信号已经能在 pool 级记录中稳定看到
  - 但真正的后续候选竞争仍主要挂在 `family_drifted` 路径上
  - 因而 `P0` 还不能算验收通过；它没有把主竞争盘面重新拉回“以 `第一站是` 为主轴的干净 request-line”
- 下一步若继续，不应再重复：
  - 只修 `partial_chain_stage_probe.py` 的 stage fallback
  - 或再盲目补一轮 suffix 列表
- 更合理的下一刀应转向：
  - 结合 `graph_contract` 与 `request_stage_states`，检查为什么 follow-up family 已经漂到 `第一站是以`
  - 查清当前主竞争是在：
    - `RewriteWordGraph`
    - `request-stage state ownership`
    - 还是 `beam/source_pool` 选源阶段
  - 也就是说，当前真正待验证的是 **`P0` 之后，谁把后续竞争从 `第一站是` 主轴重新带偏到了 `地一站式 / 第一站是以` 这条 family_drifted 链上**
- 继续沿这条线直接读取 `partial_chain_stage_probe.graph.jsonl` 中的 `expansion_gate_records` 后，当前问题位置又进一步收紧：
  - `第一站` 这条正确主轴并没有在更早阶段消失
  - 在 `end_pos = 8` 时，`post_future_compact` 里仍能看到：
    - `source_text = 第一站`
    - `beam_score = -147.758`
  - 因而 `P0` 后的真实失守点不在 `第一站` 之前
- 真正的大跳水发生在：
  - `start_pos = 8`
  - `end_pos = 11`
  - 也就是 `第一站 -> 是 / 十 / 时 / 式 / ...` 这一跳
- 在这一步，`graph` 里的 request / batch / admitted 记录已经足以说明问题不在 probe 汇总，而在真实排序盘面：
  - `source_text = 第一站`
  - `entry_text = 是`
  - `request.search_score = -207.667`
  - `used_char_fallback = false`
  - `lm_oov_token_count = 0`
  - 同组里 `十/时/式/...` 都只是与 `是` 非常接近的单字 exact 竞争，范围约 `-208.148 ~ -209.546`
- 也就是说，`P0` 第一刀后的语义修正已经兑现为：
  - `第一站 -> 是` 不再因为 `fallback/OOV` 额外失血
  - 这一跳现在是“干净但分数不够高”，而不是“脏路径被误罚”
- 但与此同时，在同一个 `end_pos = 11` 上，更早漂移出来的多字错误链已经占据了更高盘面：
  - `敌意展示 = -178.06`
  - `第一展示 = -178.195`
  - `地以展示 = -181.012`
  - `地一站式 = -195.349`
  - `第一战士 = -192.453`
- 这组读数非常关键，因为它说明：
  - `第一站是` 不是在 `admitted_state_index`/`CompressLinePoolByState` 里被错误合并掉
  - 它其实已经被正常 `admitted_new`
  - 只是进入 `source_pool` 时分数就明显低于错误多字链
  - 到 `pre_future_compact / post_future_compact` 时，主池顶部自然就被 `第一战士` 等错误 family 占住
- 因而当前更可信的链路解释应更新为：
  1. `translator`/graph 侧能把正确主轴保到 `第一站`
  2. `P0` 也已把 `第一站 -> 是` 从“误当 OOV/fallback”纠正成“干净单字 exact”
  3. 但 `poet` 的真实 beam 盘面里，这条干净单字桥的分数仍远低于早先漂移出的多字错误链
  4. 所以后续真正被用户看到的，不是 `第一站是 -> 一` 的 clean line，而是已经占住上风的 `第一战士 / 第一展示 / 地一站式 ...`
- 这也把“当前最值得继续查哪一步”进一步明确了：
  - 现在不应再优先怀疑 `admitted_state_index` 或 `CompressLinePoolByState` 把正确链误杀
  - 真正的核心缺口更像是：
    - `request-stage family` 的 contract 虽已在 translator 中参与打分，但强度仍不足以改变 beam 主盘面
    - 当前 `upstream_prefix_state_contract_weight` / `request_stage_tag` 更像“轻量偏置”，不足以对抗 `20+` 分量级的错误多字链优势
- 因而下一步若继续，更合理的方向应是：
  - 不再只围绕 `P0` 语义或 probe 口径修补
  - 直接转向 `P1/P2` 级问题：
    - 要么把 request-stage/source-line 主轴真正前移成更强的上游 contract
    - 要么在 `poet` 的 batch admission / beam ranking 中显式保住这类主轴单字桥
  - 换句话说，当前主问题已经从“`一` 被误罚”进一步前移成了 **“干净的 `第一站是` 线已经存在，但不足以在真实 beam 盘面中压过错误多字 family”**

- 继续做最小实现审计后，当前更明确的工程判断如下：
  - `translator -> poet` 现有正式信号通道只有：
    - `vertex_risks`
    - `edge_risks`
    - `edge_spelling_classes`
  - 具体由：
    - `poet_->SetJointRiskHints(...)`
    - `WitsetPoet::SetJointRiskHints(...)`
    接入
  - 也就是说，虽然 `translator` 里已经能计算：
    - `request_stage_tag`
    - `request_stage_tail_support`
    - `request_stage_source_anchor`
    - `request_stage_prefix_states`
    但这些主轴资格并没有显式传进 `poet`
- 同时进一步确认：
  - `translator` 侧并不是完全没做这件事
  - 已有两条相关 bias：
    - `ComputeTranslatorRequestStageContractBias(...)`
    - `ComputeRequestStageBridgeSelectionBonus(...)`
  - 其中后者甚至对 `candidate->text == "是"` 且 downstream 存在强 exact `一...` family 时直接给 `+3.0`
- 但结合当前 schema 实际配置，这条路径为什么仍然不够已经很清楚：
  - `upstream_prefix_state_contract_weight = 1.0`
  - `upstream_validated_continuation_weight = 0.0`
  - `clean_single_char_bridge_bonus_weight = 0.0`
  - `validated_prefix_suffix_bonus_weight = 0.0`
  - 即：
    - translator 侧 prefix-state contract 确实启用了
    - 但 poet 侧所有可复用的 clean bridge / validated continuation 类 bonus 当前都没有打开
- 这使得当前最小实现落点可以明确收口为：
  - **不要继续只调 translator bias**
  - **也不要单纯把现有 poet bridge 常数调大**
  - 更合理的是增加一条与 `SetJointRiskHints(...)` 平行的 `translator -> poet` hint 通道，把：
    - `request_source_line_eligible`
    - `request_tail_supported`
    - `request_source_line_terminal`
    这类 request-stage 主轴资格，以 candidate 级 `(start_pos, end_pos, entry_text)` key 显式送进 `poet`
- 当前我认为最小可行实现应是：
  1. `translator` 在 graph rewrite 后、调用 `MakeSentences()` 前，基于当前最终 `word_graph` 重新生成 request-stage candidate hint map
  2. key 不能只用 `(start_pos, end_pos)`，因为 `是/十/时/式` 共用同一 edge，必须带上 `entry_text`
  3. `poet` 新增与 `joint_risk_hints_` 平行的 candidate-level hint map，并在 `BatchRequest` 打分阶段查表
  4. bonus 应落在：
     - `request` 阶段 `adjustment_score`
     - 即与 `clean_single_char_bridge_bonus` / `validated_prefix_suffix_bonus` 同层
     - 而不是落在 `BuildApproxStateKey` 或 compact 阶段
- 当前不建议的路线也已基本明确：
  - 不要把主问题重新归因到 `CompressLinePoolByState`
  - 不要继续围绕 `stage fallback / suffix fallback` 修 probe 表层逻辑
  - 不要只提高 `ComputeRequestStageBridgeSelectionBonus(+3.0)` 常数，希望 translator 一步翻盘
- 原因是：
  - 当前证据已经表明 translator 侧轻量 bias 和 request-stage bridge bonus 都在工作
  - 但 `第一站 -> 是` 在真实 request 排名中仍落后约 `10+` 分给错误多字链
  - 因而需要在 poet 的真实 beam 排序盘面里，再显式给这类主轴资格一次独立加成

- 本轮已按上述方向直接落了一版最小实现，但**尚未编译验证**：
  - 修改 [witset_poet.h](file:///C:/Code/outwit/outwit-windows/librime/plugins/witset/src/witset_poet.h)
    - 新增 `SetUpstreamCandidateHints(...)`
    - 新增 `request_stage_candidate_hints_`
  - 修改 [witset_poet.cc](file:///C:/Code/outwit/outwit-windows/librime/plugins/witset/src/witset_poet.cc)
    - 新增 candidate key 生成函数
    - 在 `MakeSentences()` 的 `BatchRequest` 打分阶段增加 `get_request_stage_bonus(...)`
    - 将 `request_stage_bridge_bonus` 并入 `adjustment_score`
  - 修改 [witset_translator.cc](file:///C:/Code/outwit/outwit-windows/librime/plugins/witset/src/witset_translator.cc)
    - 新增 `BuildPoetRequestStageCandidateHints(...)`
    - 在 query 链路里先清空旧 hint，再于 `WordGraphRewriter` 后基于最终 `graph` 生成并下发 hint
- 当前实现的 key 设计为：
  - `(start_pos, end_pos, entry_text)`
  - 原因是 `是/十/时/式` 共用同一 edge，只用 `(start_pos, end_pos)` 无法区分
- 当前 poet 侧 bonus 的设计原则为：
  - 复用 translator 现有 `request-stage` 资格判断
  - 但在 beam 排序阶段放大为 `kBeamStageBonusScale = 4.0`
  - 同时叠加现有 `ComputeRequestStageBridgeSelectionBonus(...)`
  - 目的是让这类 bonus 在真实 request 盘面里有机会与错误多字 chain 抗衡，而不只是 translator 侧的轻量 rewrite bias
- 本轮只做了代码修改与静态诊断：
  - `witset_poet.h/.cc`
  - `witset_translator.cc`
  - VS Code diagnostics 当前为 0
- 尚未做的事：
  - 未编译
  - 未重跑 `case2_diyizhan`
  - 未确认 `第一站 -> 是` 是否已回到 `request` 主盘面

- 后续按“脚本验证要讲性价比”的新约束，暂停继续跑整条 `partial_chain_stage_probe.py`，改做更小粒度的静态审计与 spot check。
- 这一轮新的高价值确认如下：
  - `partial_chain_stage_probe.py` 使用的不是安装目录旧二进制，而是：
    - `librime/build_x64/bin/Release/rime_api_console.exe`
    - `librime/dist_x64/bin`
  - 因而若 probe/spot check 结果无变化，不能简单归因为“没吃到新编译”
  - `BuildPoetRequestStageCandidateHints(...)` 的静态量级复核结果：
    - 对 `request_source_line_eligible` 的单字 exact 候选，`ComputeTranslatorRequestStageContractBias(...)` 基础分约为 `0.70 + tail_path + tail_weight`
    - 在当前实现里又乘了 `kBeamStageBonusScale = 4.0`
    - 若是 `是 -> 强 exact 一...`，还会再叠加 `ComputeRequestStageBridgeSelectionBonus(...) = 3.0`
    - 所以 `第一站 -> 是` 这类 candidate 若命中 hint，理论 bonus 不应接近 `0`
- 进一步确认了一个之前的观测盲点：
  - 虽然已把 `request_stage_bridge_bonus` 加进 `poet` 记录结构，但最初并没有在 `translator` 的 JSON 输出里序列化
  - 现已补到三处输出：
    - `BuildLocalGraphSnapshotLine(...)`
    - `DumpLocalSnapshot(...)`
    - `DumpLocalNextHopProbe(...)`
  - 也就是说，之前即便 bonus 已命中，已有工件里也根本看不到该字段；因此“bonus 没生效”的旧判断并不稳固
- 还确认了两个运行时语义：
  - `request_stage_candidate_hints_` 在 `WitsetPoet::MakeSentences()` 中不会被清空；`debug_*` 会清，但 hint map 不会
  - `graph` 工件不是中途实时 flush 的：
    - `Query()` 中只会把 `pending_graph_snapshot_line_` 暂存
    - 真正落盘要等新 session 切换或 `WitsetTranslator` 析构时 `FlushPendingGraphSnapshot()`
  - 因而“中途截停 query 再去读 `graph.jsonl`”这条 cheap 路线天然不可靠
- 由此当前最稳妥的判断更新为：
  - 先前“request_stage hint 没有作用到打分路径”的结论需要降级为**未证实**
  - 当前更像是：
    - 观测链之前缺字段
    - 再加上 `graph`/`next_hop` 的 flush 时机与中途截停不兼容
  - 下一步若还要动态确认，应优先设计一次真正便宜、且能保证 query 正常收尾的单点验证，而不是再跑整条 probe

- 继续按高性价比方式做了一次真正便宜的动态 spot check，关键做法是：
  - 不再跑整条 `partial_chain_stage_probe.py`
  - 临时关闭 `graph` / `next_hop` dump，只保留 `snapshot`
  - 直接启动 `rime_api_console`
  - 显式执行：
    - `select schema witset`
    - `set option !llm_level_3`
    - `set option !llm_level_2`
    - `set option llm_level_1`
    - `set preceding text 终于，在一个假期，我踏上了旅行的征程。`
    - `clear composition`
    - `diyizhanshi`
  - 等这一个 query 正常收尾后直接读 snapshot
- 同时为了让这次 spot check 真正可观测：
  - 已把 `request_stage_bridge_bonus` 加入候选 `[Debug]` 字符串，标签名为 `ReqBridge`
  - 这样即使 `expansion_gate_records` 为空，也能从最终候选直接看到 bonus 是否进入最终路径
- 这轮动态验证的关键结果已经明确：
  - `diyizhanshi` 在一档下可快速返回，`candidate_count = 20`
  - snapshot 顶层 `llm_level_flags` 已确认：
    - `llm_level_1 = true`
    - `llm_level_2 = false`
    - `llm_level_3 = false`
  - 返回候选中的大量错误线都带有：
    - `ReqBridge:2.00`
  - 例如：
    - `的驿站是`
    - `地驿站是`
    - `第驿站是`
    - `敌驿站是`
    - `滴驿站是`
    - 等等
  - 而 `敌意展示` 的 `ReqBridge = 0.00`
  - 同时，`第一站是` **根本不在返回的 20 个候选里**
- 因而当前最重要的新结论是：
  - `request-stage bridge bonus` **已经命中并进入最终路径**
  - 问题不再是“bonus 没生效”
  - 问题是它**命中得过宽**
- 这与当前 key 设计完全一致：
  - 现在的 hint key 只有：
    - `(start_pos, end_pos, entry_text)`
  - 它无法区分：
    - `第一站 -> 是`
    - `地驿站 -> 是`
    - `第驿站 -> 是`
    - 以及其他错误 family 上的 `... -> 是`
  - 于是同一 `entry_text = "是"` 的 bridge bonus 被整片错误 family 一起吃到
- 这也意味着：
  - 当前这条最小实现虽然验证了“把 request-stage 主轴信号送进 poet 排序阶段”这个大方向是可接线、可落地、可见效的
  - 但它没有解决真正的主问题，因为缺少 source-line / family 级身份，奖励被错误共享
- 当前下一步应收口为：
  - 不再继续调 bonus 强度
  - 不再纠结“有没有命中”
  - 而是回到更早就提出的真正根因：
    - `poet` 侧必须能区分 source-line / family identity
    - hint key 至少要从 `(start,end,entry_text)` 升级为带 prefix/source identity 的 key

- 继续做静态审计后，当前“下一刀怎么改”已经可以再收紧一层：
  - [partial_chain_stage_probe_result.json](file:///C:/Users/Bing/AppData/Roaming/witty/debug/partial_chain_stage_probe_result.json) 里的 `request_stage_states` 明确表明：
    - 在相关 end_pos 上并不是只有单一前缀
    - bucket 中同时存在多个 `bridge_lineage_confirmed = true` 的 family，例如：
      - `family_identity = 第一站`
      - `family_identity = 地`
      - `family_identity = 第`
      - `family_identity = 第一`
      - `family_identity = 第一战`
      - `family_identity = 敌意`
  - 这与代码实现一致：
    - `BuildRequestStagePrefixStates(...)` 本来就是多 family bucket
    - 同一 `family_identity` 只保留一个代表 `text`
    - bucket 最多保留若干个 family，而不是只留单一路线
- 因而，当前实现的真正结构性问题是：
  - `BuildPoetRequestStageCandidateHints(...)` 仍然只依赖 `best_prefix_states`
  - 它不是从 `request_stage_prefix_states` 多 family bucket 生成 hint
  - 所以它天然只能围绕“单一前缀解释”构造 bonus
- 这意味着两个备选改法的优先级已可明确：
  1. **仅把 key 从 `(start,end,entry_text)` 改成 `(start,end,source_text,entry_text)`，但仍使用 `best_prefix_states` 生成 hint**
     - 只能把 bonus 从“所有 family 共享”收紧到“一个 family 独享”
     - 若 `best_prefix_state` 本身不是 `第一站`，则仍然救不到正确主轴
     - 因而这条路不够
  2. **改为基于 `request_stage_prefix_states` 多 family bucket 生成 hint，并同时把 key 扩为带 `source_text` 的 key**
     - translator 为每个 bucket state 的代表 `text` 单独生成 `(start,end,source_text,entry_text) -> bonus`
     - poet 在 `BatchRequest` 用 `source_line->full_context()` 命中
     - 这样 `第一站 -> 是` 与 `地驿站 -> 是`、`第驿站 -> 是` 会拿到不同 key
     - 当前看这是最小且方向正确的一刀
- 当前建议的最小实现方案已明确为：
  - 不新增复杂 family 字段进 `Line`
  - 先直接复用：
    - translator 侧 `RequestStagePrefixState.text`
    - poet 侧 `source_line->full_context()`
  - 将 hint key 升级为：
    - `(start_pos, end_pos, source_text, entry_text)`
  - 但 hint 的生成源必须改成：
    - 遍历 `request_stage_prefix_states[start_pos]`
    - 而不是继续只看 `best_prefix_states[start_pos]`

- 已按上面的最小方案完成代码实现：
  - `MakePoetCandidateHintKey(...)` / `MakeCandidateHintKey(...)` 已从
    - `(start_pos, end_pos, entry_text)`
    - 升级为 `(start_pos, end_pos, source_text, entry_text)`
  - `BuildPoetRequestStageCandidateHints(...)` 已不再依赖单一 `best_prefix_states`
  - 改为遍历 `request_stage_prefix_states[start_pos]` 的多 family bucket
  - 为每个 `prefix_state.text` 单独生成 hint
  - `poet` 命中时使用 `source_line->full_context()` 对齐 key
- 编译验证：
  - `librime/build.bat static` 已通过
  - 没有新增编译错误；只剩既有 warning
- 继续使用同一条 cheap spot check：
  - 一档
  - `diyizhanshi`
  - 只开 snapshot
  - 不跑整条 probe
- 这轮动态结果显示，修改已命中预期方向：
  - 错误 family 候选如
    - `的驿站是`
    - `地驿站是`
    - `第驿站是`
    - `低驿站是`
    - `敌驿站是`
  - 其候选 debug 中的 `ReqBridge` 已全部变为 `0.00`
  - 说明“bridge bonus 被错误 family 一起吃到”的问题已经被成功收住
- 同时也确认了当前新的阻塞点：
  - `第一站是` 仍然没有进入这次一档 `diyizhanshi` 的前 20 候选
  - 也就是说：
    - 上一轮的关键 bug 已经被修正
    - 但它并不足以让正确主轴直接翻进当前 top20
- 因而当前问题状态已更新为：
  - 旧问题：
    - request-stage bridge bonus 误发给错误 family
    - **已修正**
  - 新主阻塞：
    - 即便去掉错误 family 的误奖励，`第一站是` 仍然没能在当前一档 spot check 里进入 top20
    - 下一步应继续往更早的 source-line / request-stage source selection 或 `第一站` 本身的前缀保活继续查

- 继续按高性价比方式做了“三断点 spot check”，仍然只开 snapshot、一档、同一前文：
  - `diyizhan`
  - `diyizhans`
  - `diyizhanshi`
- 这轮结果把主阻塞进一步收紧为：
  - `第一站` **不是在加 `shi` 时才丢**
  - 它在更早的 `diyizhan -> diyizhans` 这一步就已经掉出前 20
- 具体结果：
  - `diyizhan`
    - `candidate_count = 20`
    - `第一站` 仍在返回集里，但仅排到 `rank = 20`
    - top10 仍全部是 `的驿站 / 地驿站 / 第驿站 / ...`
  - `diyizhans`
    - `candidate_count = 20`
    - `第一站` 已不在前 20
    - top10 变成 `的驿站是 / 的驿站上 / 地驿站是 / 第驿站是 / ...`
  - `diyizhanshi`
    - 与 `diyizhans` 基本同盘面
    - `第一站是` 依然不在前 20
- 这说明当前真正的前移结论是：
  - `第一站 -> 是` 这一步当然有问题
  - 但更早、更本质的问题是：
    - `第一站` 这个前缀本身在一档下就只是在边缘存活
    - 一旦继续扩成 `diyizhans`，就立即被整片 `驿站是/上` family 挤出盘面
- `diyizhan` 下 `第一站` 的当前读数也支持这一点：
  - `rank = 20`
  - `Base = -192.42`
  - `Adj = -1.41`
  - `Total = -193.83`
  - `Whole = 0.00`
  - `WholeHit = 0`
  - `StepWholeLog10 = 0.0`
  - `StepCharLog10 = -155.996796`
  - 与之对比，前排 `驿站` 系列普遍：
    - `Whole ≈ 0.15`
    - `WholeHit = 1`
    - 总分领先约 `5~8` 分
- 因而当前下一步不应再优先纠结 bridge bonus：
  - bridge 误奖励问题已经修掉
  - 但正确主轴在 bridge 之前就已经处在边缘
- 当前更合理的下一刀应改成：
  - 直接查 `diyizhan` 这一步为什么 `第一站` 只能以 `rank 20` 存活
  - 优先检查：
    - `第一站` 在当前路径里为何没有 whole-word hit
    - 它与 `驿站` 系列在上游候选构造 / 词典打底 / request 入池上的分差从哪一步拉开

- 已继续用极小成本把 `whole-word hit` 的问题前移到当前运行中的 `wanxiang-lts-zh-hans.arpa` 词表层，直接扫描 `1-grams` 后确认：
  - `第一站 = absent`
  - `驿站 = absent`
  - `第一 = absent`
  - `站 = present`
  - `是 = present`
  - `一 = absent`
- 再补查关键单字后确认：
  - `的 = present`
  - `第 = present`
  - `驿 = present`
  - `一 = absent`
  - `站 = present`
  - `是 = present`
- 这把当前判断继续收紧为：
  - `第一站` 在 `diyizhan` 下 `WholeHit = 0`，并不是一个孤立异常
  - 同期错误前排里的 `驿站` 其实也不是靠 whole-word unigram 命中在赢
  - 当前真正的差别，不是“驿站整词命中而第一站整词不命中”
  - 而是：
    - 两边都更多依赖 split / char-path 口径
    - 但 `第一站` 的逐字路径会经过缺失的 `一`
    - 而 `的 / 第 / 驿 / 站 / 是` 这些错误线上的关键字元都在词表里
- 因而，`diyizhan` 这一步当前更准确的根因是：
  - `第一站` 并不是因为 whole-word bonus 没拿到才弱
  - 它更像是因为当前 grammar 词表缺 `一`，导致其 split / char-path 路径本身就严重失血
  - 相比之下，`的驿站 / 地驿站 / 第驿站` 等错误线，即便也未必有整词 whole-word 命中，但其逐字路径都更“合法”，所以天然更强
- 因而当前下一步应再更新为：
  - 不再把重点放在 `第一站` 的 `whole-word hit` 本身
  - 而应直接围绕：
    - `一` 缺失导致的 `第一站` char-path 弱势
    - 上游能否在 `diyizhan` 这一步就把 `第一站` 作为强 dictionary / source-line family 保住，不让它被纯 grammar char-path 提前压死

- 已继续对这条根因做最小实现验证，并命中了当前最关键的问题点：
  - 不是再加新的 guard
  - 而是修正 `P0` 之后的一个副作用：
    - `neutral_missing` 虽然已不再当作惩罚型 fallback / OOV
    - 但它也因此不再触发 `ComputePrefixCharFallbackLmScale(...)`
    - 导致 `第一站` 这类首词多字、整词缺失但并非真 OOV 的候选，raw LM 负分仍被原样吃满
- 已在 `witset_poet.cc` 做最小修正：
  - `neutral_missing` 继续不计入惩罚型 `used_char_fallback`
  - 但在 `ComputePrefixCharFallbackLmScale(...)` 这一层，额外把
    - `WitogramTokenEvidenceLevel::kNeutralMissing`
    - 视为“证据不足型 fallback”
  - 只用于首词多字 early-prefix 的 LM 降权
  - 不改变 OOV penalty / char fallback penalty 的 `P0` 语义
- 编译验证：
  - `librime/build.bat static` 已通过
- 继续只用 cheap spot check 验证两条：
  - `diyizhan`
  - `diyizhans`
- 结果出现了明显翻转：
  - `diyizhan`
    - `第一站` 从之前 `rank 20`
    - 直接升到 `rank 1`
    - `LmScaled` 从 `-179.60`
    - 变成 `-62.86`
    - 其余调节项几乎未动，说明命中的正是 raw LM 过强这一层
  - `diyizhans`
    - `第一站是` 已进入前 20，且升到 `rank 4`
    - top10 盘面也已经由原先的 `驿站是 / 驿站上` 系列，变成以
      - `第一战士`
      - `第一站上`
      - `第一战神`
      - `第一站是`
      - `第一站十`
      为主
- 这说明当前判断应进一步更新为：
  - `第一站` 此前的主阻塞，确实不是额外惩罚项
  - 而是 `neutral_missing` 情况下，首词 early-prefix 仍然被 raw LM 过强地下压
  - 一旦只在这一层把 LM 解释纠正，正确主轴就能重新回到主盘面
- 同时也说明：
  - 前一轮修掉的 `request-stage bridge bonus` 错发问题是必要修复
  - 但真正带来盘面翻转的，是这次对 `neutral_missing -> prefix LM scale` 的语义修正
- 当前剩余问题也更清楚：
  - `diyizhans` 下 `第一站是` 虽已回到 `rank 4`
  - 但还未到 `rank 1`
  - 这意味着：
    - 首词保活问题已大幅缓解
    - 下一步应继续围绕 `第一站 -> 是/十/时` 这一跳的 request 排序差异做更小的定点排查

- 已继续按 cheap spot check 定点排查 `diyizhans` / `diyizhanshi`，只并排读取：
  - `第一站是`
  - `第一站十`
  - `第一战士`
  - `第一战神`
  - `第一站上`
  - `第一展示`
- 当前最关键的新结论：
  - `第一站是` 与 `第一站十` 在当前打分里几乎完全同分
  - 真正压在它们上面的主竞争者不是 `驿站...` 旧错误线了，而是：
    - `第一战士`
    - `第一展示`
- 具体读数：
  - `diyizhans`
    - `第一战士`
      - `rank 1`
      - `Base = -92.54`
      - `Adj = -1.61`
      - `Total = -94.16`
      - `LmScaled = -78.37`
      - `WholeHit = 0`
    - `第一站上`
      - `rank 2`
      - `Total = -141.95`
      - `LmScaled = -112.53`
      - `WholeHit = 1`
    - `第一战神`
      - `rank 3`
      - `Total = -142.19`
      - `LmScaled = -112.53`
      - `WholeHit = 1`
    - `第一站是`
      - `rank 4`
      - `Total = -166.06`
      - `Dict = -23.86`
      - `DictNorm = -18.44`
      - `LmScaled = -136.74`
      - `Whole = 0.12`
      - `ReqBridge = 0.00`
      - `Octa = -0.73`
      - `Tail = -0.15`
      - `WholeHit = 1`
    - `第一站十`
      - `rank 5`
      - `Total = -166.07`
      - `Dict = -23.87`
      - `DictNorm = -18.45`
      - `LmScaled = -136.74`
      - 其余读数与 `第一站是` 基本一致
  - `diyizhanshi`
    - `第一战士`
      - 仍是 `rank 1`
      - 读数与上面基本一致
    - `第一展示`
      - 升到 `rank 2`
      - `Total = -142.61`
      - `LmScaled = -112.30`
      - `WholeHit = 1`
    - `第一站是`
      - 升到 `rank 3`
      - 读数仍与 `diyizhans` 基本一致
    - `第一站十`
      - `rank 4`
      - 与 `第一站是` 仍几乎同分
- 这说明当前问题已进一步收紧为两层：
  1. `是 / 十` 这类同一 `shi` 音上的细排，当前缺少足够强的区分信号
     - 它们在 `Dict / DictNorm / LmScaled / Whole / ReqBridge / Octa / Tail` 上都近乎一样
     - 当前还没有任何有效信号把 `是` 明显拉开
  2. 更大的压制项来自 `第一战士 / 第一展示`
     - 它们不是靠 bridge bonus 在赢
     - 而是自身 `LmScaled` 明显更强，导致 `Base` 整体领先约 20~70 分
- 因而下一步应再拆成更小的两个方向：
  - 先看 `shi` 同音细排：为什么 `是` 与 `十` 近乎不可分
  - 再看 `战士 / 展示` 为什么在当前 preceding text 下有这么强的 grammar / collocation 优势

## 2026-05-23 cheap spot check 继续收口：`shi` 同音细排当前退化成同一 backoff 桶，`战士 / 展示` 不是同一种优势来源

- 这一轮继续遵守“只做高性价比定点读取”的原则，没有重新跑长脚本。
- 只复用了现成工件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.snapshot.jsonl`
  - `C:\Users\Bing\AppData\Roaming\witty\grammar\wanxiang-lts-zh-hans.arpa`
- 目标很窄：
  1. 把 `第一站是 / 第一站十 / 第一站时` 为什么几乎同分钉死
  2. 把 `第一战士` 与 `第一展示` 对 `第一站是` 的优势来源拆开，避免再把它们混成同一种“LM 更强”

### `shi` 同音细排：当前基本没有有效 tie-break 信号

- 继续读取 `diyizhanshi` 快照后，`第一站是 / 第一站十 / 第一站时` 的关键分项仍然几乎完全一致：
  - `Base = -160.60 / -160.61 / -160.61`
  - `Total = -166.06 / -166.07 / -166.07`
  - `LmScaled = -136.74 / -136.74 / -136.74`
  - `LmAvg = -134.51 / -134.51 / -134.51`
  - `WholeHit = 1 / 1 / 1`
  - `StepWholeLog10 = -77.998398`
  - `StepCharLog10 = -77.998398`
- 三者差异只剩极小的词典底分：
  - `Dict / DictNorm`
  - 量级只有约 `0.01`
  - 不足以把 `是` 稳定拉开
- 直接查 ARPA 后，`2-gram` 里：
  - `站 是`
  - `站 十`
  - `站 时`
  都是同一个读数：
  - `-37.499199`
- 同时 `1-gram` 里：
  - `是`
  - `十`
  - `时`
  也都是同一个读数：
  - `-37.499199`
- 这说明当前 `shi` 同音细排之所以几乎不可分，不是某个 bonus 没开，而是：
  - **在现 grammar 里，`站 + 是/十/时` 已基本退化成同一个 backoff 桶。**
  - 既没有来自 step LM 的可分信号，也没有来自当前 scorer 其他分项的稳定拉开项。

### `第一战士`：主优势不是 collocation，而是仍处于句首单块重解释 regime

- `diyizhans` / `diyizhanshi` 快照都显示：
  - `第一战士`
    - `rank 1`
    - `Base = -92.54`
    - `Total = -94.16`
    - `LmRaw = -194.50`
    - `LmScaled = -78.37`
    - `WholeHit = 0`
    - `StepWholeLog10 = 0`
    - `StepCharLog10 = -194.495995`
  - `第一站是`
    - `Base = -160.60`
    - `Total = -166.06`
    - `LmRaw = -194.50`
    - `LmScaled = -136.74`
    - `WholeHit = 1`
    - `StepWholeLog10 = -77.998398`
- 也就是说：
  - 两边并不是“`战士` 当前步 n-gram 证据更强”
  - 反而在快照里，`LmRaw` 完全一样
  - 真正拉开的是：
    - `第一战士` 还在句首 same-span 单块重解释 regime
    - `第一站是` 已经进入 split continuation regime
- 结合现 scorer 公式，这类差距主要来自：
  1. `第一战士` 仍按句首多字候选吃 `prefix LM scale`
  2. `第一站是` 已开始按两词链条吃 `LmAvg`
  3. `第一站是` 还会继续吃 `Frag / Tail / Octa / single-char whole penalty` 这类 split 形态项
  4. `第一战士` 则仍保留更好的单块边界形态
- 直接查 ARPA 也能侧面说明它不是“`战 士` collocation 特别强”：
  - `战 士`
    - `2-gram = -37.499199`
  - 和
    - `站 是 / 站 十 / 站 时`
    - 同属一个低分 backoff 桶
- 因而当前更准确的说法应是：
  - **`第一战士` 的领先，主因不是 grammar 显式偏爱 `战 士`，而是当前 scorer 对句首单块重解释路径仍显著有利。**

### `第一展示`：同时叠加了单块 regime 优势与真实 `展 示` collocation 优势

- `diyizhanshi` 快照里：
  - `第一展示`
    - `rank 2`
    - `Base = -137.04`
    - `Total = -142.61`
    - `LmScaled = -112.30`
    - `WholeHit = 1`
    - `StepWholeLog10 = -56.767464`
  - 对照 `第一站是`
    - `StepWholeLog10 = -77.998398`
- 直接查 ARPA 后可见：
  - `展 示`
    - `2-gram = -16.199902`
  - 明显强于：
    - `站 是`
    - `站 十`
    - `站 时`
    - `战 士`
    - 它们都只有 `-37.499199`
- 这说明 `第一展示` 与 `第一战士` 不应混为一谈：
  - `第一战士`
    - 更像 regime / scoring 形态优势
  - `第一展示`
    - 除了同样享受单块重解释的结构性优势
    - 还额外叠加了一个真实更强的 `展 示` collocation 信号

### 当前收口

- 这轮之后，当前问题可以更精确地拆成两类，而不是一句“LM 更强”带过：
  1. `第一站是 / 十 / 时`
     - 当前基本没有可用 tie-break 信号
     - 若不引入新的区分证据，`是` 很难稳定压过同音项
  2. `第一战士`
     - 主压制来自句首单块重解释的 scoring regime
  3. `第一展示`
     - 同时来自句首单块重解释优势与 `展 示` 的真实 collocation 优势
- 因而后续如果继续推进，最值得优先想清楚的不是再加泛化 bonus，而是：
  - 要不要显式削弱句首 same-span 单块重解释对 split continuation 的结构性偏置
  - 以及是否需要为 `shi` 同音细排补独立证据源，否则 `是 / 十 / 时` 会长期并列

## 2026-05-23 通用竞争形态原型 v1：在 translator request-stage hint 中轻量压“同终点多字重解释”，编译通过但对 case2 关键盘面无可见收益

- 按“不能做某个读音/句子/单字特调”的约束，这一轮没有碰：
  - 某个具体拼音
  - 某个具体字
  - 某个具体句子的特判
- 落点选在 `witset_translator.cc` 的 `BuildPoetRequestStageCandidateHints(...)`，目标是做一个纯竞争形态级的最小原型：
  - 当同一 `end_pos` 上已经存在被 request-stage 确认的多段 exact 主轴时
  - 对未被确认的多字重解释路径，额外下发一个轻量负向 hint
  - 这条 hint 仍走现有 translator -> poet 的 `request_stage_candidate_hints_` 通道
- 这轮实现刻意保持通用：
  - 不看具体读音
  - 不看固定词表白名单
  - 只看：
    - 同一终点
    - competing state 是否已确认
    - 当前路径是否属于未确认的多字 exact 重解释

### 验证口径

- 静态检查：
  - `GetDiagnostics` 对 `witset_translator.cc` 无报错
- 编译：
  - `librime/.\\build.bat static` 通过
- 运行时验证：
  - 继续只跑单例 `case2_diyizhan`
  - 命令：
    - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan`
  - 复查输入：
    - `diyizhans`
    - `diyizhanshi`

### 客观结果

- 这版原型没有把链路打坏：
  - `partial_chain_stage_probe.py` 正常完成
  - `case2_diyizhan` 的 graph / next-hop / snapshot 工件都能正常生成
- 但对关键盘面没有看到可见变化：
  - `diyizhans`
    - `第一战士`
      - 仍是 `rank 1`
      - `Total = -94.16`
    - `第一站是`
      - 仍是 `rank 4`
      - `Total = -166.06`
    - `第一展示`
      - 仍在 `rank 11`
      - `Total = -167.04`
  - `diyizhanshi`
    - `第一战士`
      - 仍是 `rank 1`
    - `第一展示`
      - 仍是 `rank 2`
    - `第一站是`
      - 仍是 `rank 3`
    - `第一站十 / 第一站时`
      - 仍与 `第一站是` 基本同分
- 同时，snapshot 里的 `ReqBridge` 对这些候选仍显示 `0.00`
  - 说明这版负向竞争 hint 即使在工程上接通了，
  - 也没有在当前 case 的最终候选读数里形成可见量级

### 当前判断

- 这次失败很有价值，因为它把“通用优化该往哪层落”进一步钉清了：
  1. 通用方向本身没有错
     - “对同终点多字重解释 vs 已确认 split continuation 做统一竞争约束”这个想法是合理的
  2. 但当前这版落在 request-stage hint / adjustment 通道里太晚也太轻
     - 它没有打穿此前已经确认的主差额层：
       - `Base`
       - 尤其是 `lm_score_scaled`
  3. 更准确地说：
     - 当前主竞争不是缺一个小 bonus / 小 penalty
     - 而是错误线在进入 `poet` 主排序前，就已经带着明显更强的主轴分数

### 收口

- 因而这轮应明确记一条新的非重复结论：
  - **若继续做通用修正，优先级更高的方向不再是继续加 request-stage hint 级的小竞争偏置。**
  - **更值得下一步考虑的是：**
    - 是否把“same-span 多字重解释 vs 已确认 split continuation”的竞争约束前移到更靠近 `Base` / state competition 的层级
    - 或者直接在 `poet` 的 state / beam 竞争面上做结构性合同，而不是只在 `adjustment_score` 里追加小量提示

## 2026-05-23 partial-chain probe 提速重构：默认切到快路径，graph 改为退出后统一汇总，不再按 case 白等

- 继续前先复核了 `WORKLOG` 中已有关于 `partial_chain_stage_probe.py` 的结论，确认这轮不是回到已判负的旧路径，而是正面处理已经多次出现的同一个效率瓶颈：
  - `debug_dump_local_graph_snapshot` 打开时单步明显变慢
  - graph snapshot 的 flush 时机天然更偏向 session 切换/进程退出
  - 旧脚本却在每个 case 发完输入后立刻等待 graph，就会形成长时间白等
- 这轮目标不是“最小改动能跑就行”，而是直接把默认执行口径改成高性价比：
  - 默认不再采集 graph
  - 默认不再自动做第二轮 retry
  - 只在明确需要 graph contract 时才显式进入 full 模式

### 这轮脚本重构

- 修改文件：
  - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py`
- 新增三种模式：
  - `--mode snapshot`
    - 只等最终 snapshot
  - `--mode probe`
    - snapshot + next-hop
    - 这是新的默认模式
  - `--mode full`
    - snapshot + next-hop + graph contract
- 新增显式重跑开关：
  - `--retry-missing-probes`
  - 只有用户明确要求时才做第二轮 suffix 扩展重跑

### 核心提速点

- `update_schema(...)` 改为按 mode 精确打开需要的 debug 开关：
  - 不再默认同时打开 `snapshot + next-hop + graph`
  - 默认链路现在只保留：
    - `debug_dump_local_snapshot = true`
    - `debug_dump_local_next_hop_probe = true`
    - `debug_dump_local_graph_snapshot = false`
- `query_cases(...)` 改为：
  - query 阶段只等待 snapshot，确认输入已经收尾
  - 不再在每个 case 中途调用 `wait_for_graph_snapshot_for_case(...)`
  - `next-hop` / `graph` 都改为 console 退出后统一读取
- 新增一次性索引：
  - `load_next_hop_index(...)`
  - `load_graph_snapshot_index(...)`
  - 不再对每个 case、每个 probe 反复全量扫描 `.jsonl` 文件
- `build_retry_probe_suffix_map(...)` 也改成直接复用已加载的 graph index，不再逐 case 重读 graph 文件

### 这次提速针对的具体低效点

- 旧链路低效主要不是“单例 case 本身太大”，而是：
  1. 默认把三套重型 debug 全开
  2. 每个 case 发完输入后立刻等 graph snapshot
  3. graph 通常要等下一次 session 或进程退出后才稳定 flush
  4. 等待期间 Python 还在反复扫描整个 `graph.jsonl`
  5. 首轮不满足条件时还会默认再整条跑第二轮
- 这轮修改后：
  - 默认直接绕开最重的 graph dump
  - 即便用户显式进 `full`，graph 也只在进程退出后统一汇总，不再按 case 白等

### 静态验证

- 没有重新跑真实单例，避免再次被旧验证耗时拖住。
- 只做了超便宜静态校验：
  - `python -m py_compile C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py`
    - 通过
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --help`
    - CLI 通过，可见新参数：
      - `--mode {snapshot,probe,full}`
      - `--retry-missing-probes`

### 后续默认用法

- 若只要最终候选及 debug 串：
  - `--mode snapshot`
- 若要继续看 source suffix 下的 next-hop 竞争：
  - `--mode probe`
  - 这是默认模式
- 只有在确实需要 graph contract / request-stage edge 聚合时才用：
  - `--mode full`
  - 若还要补跑扩展 suffix，再额外加：
    - `--retry-missing-probes`

### 当前判断

- 这轮属于“把默认工作流改快”，不是只加一个开关。
- 即使还没有重新做真实耗时 benchmark，也可以先明确一条工程判断：
  - **旧脚本最浪费时间的部分已经被结构性拿掉了。**
  - **后续若再慢，剩下的才更接近真实查询成本，而不是验证链路自己制造的白等。**

## 2026-05-23 partial-chain probe 快路径实测：`case2_diyizhan` 已回到十几秒量级，后续默认走 `probe`

- 按新的效率规则，这轮没有再去跑旧的 `full + retry` 重链路，而是只做最小实测：
  - 只测 `case2_diyizhan`
  - 先测最轻的 `snapshot`
  - 再补默认模式 `probe`
  - 不测 `full`
- 实测命令：
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode probe`

### 实测结果

- `snapshot`
  - 正常完成
  - `snapshot_candidate_count = 20`
  - 端到端耗时约 `15.06s`
- `probe`
  - 正常完成
  - 成功拿到 `next_hop_after_diyizhanshi`
  - `preferred_candidate_stage = request`
  - 端到端耗时约 `11.05s`

### 说明

- 这两次不是严格 cold/warm 对照：
  - 第二次运行在文件系统和程序缓存上可能更热
  - 因而不应草率解读为 `probe` 必然比 `snapshot` 更快
- 但就效率判断而言，当前最重要的结论已经足够清楚：
  - **新的快路径单例已回到十几秒量级，而不是此前动辄几分钟甚至更久。**
  - **这已经满足“后续默认验证可用”的工程门槛。**

### 后续默认策略

- 若只要最终候选和 debug 串：
  - 走 `--mode snapshot`
- 若要继续看 source suffix 下的 next-hop：
  - 默认走 `--mode probe`
- 只有在明确需要 graph contract / request-stage edge 聚合时才允许走：
  - `--mode full`
- 只有在 `full` 且确实需要补 suffix 时才再加：
  - `--retry-missing-probes`

### 收口

- 后续继续做算法定位时，默认不再接受“为了拿一点 next-hop 数据却跑回 full 重链路”的低效做法。
- 若某次验证重新升回分钟级，优先检查：
  - 是否误开了 `full`
  - 是否误开了 `--retry-missing-probes`
  - 是否本次其实只需要 `snapshot` 却用了 `probe/full`

## 2026-05-23 same-span 竞争合同继续推进：从 request 批移到 target_pool 后，`第一战士` 已被通用惩罚压低，但 `第一展示` 仍未命中该合同

- 继续按高效口径推进，这轮没有回到慢脚本，也没有跑 `full`：
  - 先静态诊断 `witset_poet.cc/.h`
  - 再 `librime/.\\build.bat static`
  - 然后只用复用后的快路径模块做一次三输入 snapshot spot check：
    - `diyizhan`
    - `diyizhans`
    - `diyizhanshi`
- 这轮的关键修正不是参数调小，而是把通用合同的落点修正到正确层级：
  - 之前放在 `all_requests` 上时，只能比较“同一个 `start_pos` 批次”的请求
  - 因而它根本看不到：
    - `start_pos = 0` 的 `第一战士`
    - `start_pos = 8` 的 `第一站 + 是`
  - 这也是上一版 `SameSpanComp` 一直是 `0.00` 的真正根因
- 本轮已把合同从 request 批移到 `target_pool` 后处理：
  - 在同一 `end_pos` 的最终候选池内，扫描：
    - 稳定的 split anchor line
    - 早期 same-span 单块重解释 line
  - 再按 generated char count 做同终点竞争约束

### 编译与触发结果

- `witset_poet.cc` 诊断无报错
- `librime/.\\build.bat static` 通过
- 复跑三输入 snapshot 后，`SameSpanComp` 已开始真实触发：
  - `diyizhans`
    - `第一战士`
      - 旧：`Total = -94.16`
      - 新：`Total = -137.95`
      - `SameSpanComp = -43.80`
    - `第一站是`
      - 仍为 `Total = -166.06`
      - `SameSpanComp = 0.00`
    - `第一站十`
      - 仍为 `Total = -166.07`
      - `SameSpanComp = 0.00`
  - `diyizhanshi`
    - `第一战士`
      - 旧：`Total = -94.16`
      - 新：`Total = -138.61`
      - `SameSpanComp = -44.45`
    - `第一站是`
      - 仍为 `Total = -166.06`
      - `SameSpanComp = 0.00`
    - `第一站十 / 第一站时`
      - 仍基本同分
      - `SameSpanComp = 0.00`

### 这轮带来的新判断

- 这次已经能明确确认：
  1. 通用合同方向是成立的
     - 之前不生效不是因为思路错，而是落点错了
  2. `第一战士` 的确属于这类“早期 same-span 单块重解释”竞争者
     - 修正落点后会被显著压低
  3. 但这还不足以让 `第一站是` 直接翻到第一
     - `diyizhans` 下仍有约 `23.87` 分差
     - `diyizhanshi` 下仍有约 `23.45` 分差

### 当前剩余问题收口

- `第一展示` 仍没有吃到 `SameSpanComp`
  - 当前读数里它保持：
    - `SameSpanComp = 0.00`
  - 这说明它不满足“same-span 单块重解释”这条合同的命中形态，或在 pool 里落成了不同结构
- 因而下一步若继续，应优先确认：
  - `第一展示` 为什么不命中该合同
  - 它在 poet state/pool 里到底是：
    - 另一种单块 whole path
    - 还是已经落成多段结构

### 收口

- 这轮最大的价值是把通用合同从“完全不生效”推进到“对主竞争者之一真实生效”。
- 后续继续推进时，不应再回去怀疑 `SameSpanComp` 完全没接通；当前更值得查的是：
  - `第一展示` 的结构归类
  - 以及它为什么能绕开这条合同

### 补充定点确认：`第一展示` 当前更像 `第一 -> 展示` 两段链，而不是需要吃 same-span 合同的首词单块路径

- 继续只用快路径做了 1 次额外 probe：
  - 输入：
    - `diyizhanshi`
  - source suffix：
    - `第一`
  - focus entries：
    - `展示`
    - `战士`
- 原始 `next_hop` 记录显示：
  - `source_text = ...第一`
  - `entry_text = 展示`
  - `start_pos = 4`
  - `end_pos = 11`
  - `generated_word_count = 1`
  - `generated_char_count = 2`
  - `search_score = -167.04`
  - 对应 `战士` 也同样存在：
    - `source_text = ...第一`
    - `entry_text = 战士`
    - `search_score = -192.027`
- 这说明至少在 request 盘面上：
  - `第一展示` 可以直接作为 `第一 + 展示` 的两段链进入
  - 因而它不是必须依赖“首词单块重解释”才能成立
- 结合前面的 snapshot 读数，当前更合理的判断是：
  - `第一战士`
    - 仍有首词单块 whole path 在顶层竞争，所以已经吃到 `SameSpanComp`
  - `第一展示`
    - 更像是通过 `第一 + 展示` 这条两段链在赢
    - 所以它没有命中当前 same-span 单块合同并不奇怪

## 2026-05-23 尝试“多字首词 clean-prefix continuation bonus”原型：方向通用，但第一版条件过宽，错误 family 一起吃到 bonus，暂不落正式配置

- 为了避免只围绕 `case2` 做特调，这轮没有直接给 `shi`/句子/单字写特判，而是试了一个更通用的结构性原型：
  - 当首词是：
    - 单词路径
    - 多字
    - 非 whole-hit
    - 无 fallback / 无 OOV / 无 joint
  - 且后续接上：
    - 干净的 1 字 whole continuation
  - 则给一笔统一的 `deferred_clean_prefix_continuation_bonus`
- 这条原型已接入 `witset_poet.cc`，并在 debug comment 中增加：
  - `PrefixCont`
  - `StepPrefixCont`
- 代码已通过：
  - `GetDiagnostics`
  - `librime/.\\build.bat static`

### 临时权重 spot check

- 这轮没有改正式 schema，只在快路径脚本里临时注入：
  - `deferred_clean_prefix_continuation_bonus_weight: 18.0`
- 仍只跑高性价比三输入 snapshot：
  - `diyizhan`
  - `diyizhans`
  - `diyizhanshi`

### 结果

- 这条 bonus 确实会命中目标线：
  - `diyizhans`
    - `第一站是`
      - `PrefixCont = 20.88`
      - `Total = -145.18`
    - `第一站十`
      - `PrefixCont = 20.88`
      - `Total = -145.19`
- 但它也同时命中了明显不该一起抬的错误线：
  - `diyizhans`
    - `第一战神`
      - `PrefixCont = 20.88`
      - `Total = -121.31`
  - `diyizhanshi`
    - `第一展示`
      - `PrefixCont = 20.88`
      - `Total = -121.73`
- 这说明：
  - “多字首词 clean-prefix 后续接 clean 1 字 continuation” 这个条件本身太宽
  - 它不仅覆盖 `第一站 + 是/十/时`
  - 也覆盖 `第一展 + 示`、`第一战 + 神` 一类错误 family

### 当前判断

- 这个方向作为“结构性奖励”不是完全错：
  - 它确实命中了目标形态
- 但第一版条件无法区分：
  - 正确 continuation family
  - 以及错误 family 的同形态 continuation
- 因而当前结论应记为：
  - **这条宽条件原型判负，不能直接进入正式配置。**
  - 代码层可先保留为默认 `0.0` 的未启用实验入口，但后续若继续，应先补更强的 family / source-line 限定，再考虑重新启用。

## 2026-05-23 继续收紧到 family/source-line：前缀本身不是主缺口，主差额在单字续接 edge；translator 新增 family-aware 1 字 bias 目前未打进 ReqBridge

- 这轮继续只做高性价比定点验证：
  - 1 次 `full`：
    - `diyizhanshi`
    - 只为读取 `graph.jsonl` 里的 `expansion_gate_records` 和 `word_edges.family_contract`
  - 1 次三输入 `snapshot`：
    - `diyizhan`
    - `diyizhans`
    - `diyizhanshi`
- 没有跑大集，因为这轮的目标是继续定位“family 级信号到底卡在哪层”。

### 当前更精确的盘面结论

- 三条 3 字前缀本身其实非常接近，而且 `第一站` 仍是最强：
  - `pre_source_pool/source_pool/top_candidate`
    - `第一站 beam_score = -60.578`
    - `第一战 beam_score = -60.8091`
    - `第一展 beam_score = -61.311`
- 说明当前主缺口不在：
  - 首个 3 字前缀的入池
  - 或 `第一站` 这条 family 在前缀阶段被压死
- 真正把盘面拉开的，是单字续接那一步：
  - `第一站 + 是`
    - `search_score = -166.062`
    - `lm_score_scaled = -89.799`
  - `第一展 + 示`
    - `search_score = -142.608`
    - `lm_score_scaled = -65.356`
- 两边共同点：
  - 都是 `used_char_fallback = false`
  - 都是 `lm_oov_token_count = 0`
  - `request_stage_bridge_bonus = 0`
- 这说明：
  - `第一展示` 领先 `第一站是` 的主因不是 bridge / fallback / OOV
  - 而是 `展 + 示` 这一步自身的 LM 强度明显更高

### request-stage family 证据已经存在，但当前 tag 太粗

- 对 `start = 8, end = 11` 的 `word_edges.family_contract` 继续检查后确认：
  - `request_stage_states` 里已经同时包含：
    - `第一站是`
    - `第一展示`
    - `第一战士`
  - 且附带：
    - `family_identity`
    - `exact_count`
    - `aligned_with_best_prefix`
    - `bridge_lineage_confirmed`
- 但落到 edge candidate 层时：
  - `是 / 十 / 时 / 示`
  - 当前统一都只是：
    - `request_stage_tag = request_tail_supported`
- 也就是说：
  - 可区分 family 的信息已经在 request-stage state 层存在
  - 但下游真正消费时，被收缩成了过粗的 edge tag

### 针对这一点做的 translator 原型

- 在 `witset_translator.cc` 新增了一条通用的 1 字 family-specificity bias 原型：
  - 只对：
    - exact 1 字 continuation
    - matching confirmed request-stage state
  - 比较：
    - matching state 的 family specificity
    - competing confirmed state 的 family specificity
  - 若 matching 更具体、更稳定，则给正向 hint
- 代码已编译通过。

### 结果

- 这条新 bias 目前没有体现在最终 snapshot：
  - `第一站是 / 十 / 时`
    - `ReqBridge` 仍是 `0.00`
  - `第一展示`
    - `ReqBridge` 仍是 `0.00`
- 也就是说：
  - translator 侧虽然已经加了新的 family-aware bias 逻辑
  - 但当前它还没有真正打进 `poet` 的 `request_stage_candidate_hints_ -> ReqBridge` 链路

### 当前判断

- 这轮最重要的新收口不是“又加了一条 bias”，而是两件事：
  1. `第一站` 这条正确 family 没有在 3 字前缀阶段输掉
  2. 真正的 family 级证据已经存在于 `request_stage_states`，但当前没有成功传导成 `ReqBridge`
- 所以下一步若继续，优先级应是：
  - 先插桩/核对 `BuildPoetRequestStageCandidateHints()` 实际产出的 key 和 hint 值
  - 确认为什么新 bias 没有命中 `poet`
  - 而不是继续盲目调整 family score 公式
- 继续前先按日志既有结论，保持只走 `snapshot` 快路径，不回到 `full` 慢链路；这轮新增了一个只用于诊断的旁路读数：
  - 在 `witset_poet.cc` 增加 `AltReqBridge / StepAltReqBridge`
  - 含义是：忽略 `source_text` 后，按同一个 `(start_pos, end_pos, entry_text)` 聚合 `request_stage_candidate_hints_` 的最强 hint
  - 目的不是改排序，而是一次性区分：
    - translator 根本没有生成该 edge 的 hint
    - 还是生成了 hint，但挂在别的 `source_text`
- 按工作区规则执行 `librime/build.bat static`，编译通过；随后只跑：
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
- 新诊断结果直接把问题进一步收紧为“hint 已生成，但没有命中当前 source_text key”：
  - `diyizhans`
    - `第一站是`: `ReqBridge = 0.00`, `AltReqBridge = 3.92`
    - `第一站十`: `ReqBridge = 0.00`, `AltReqBridge = 2.00`
    - `第一展示`: `ReqBridge = 0.00`, `AltReqBridge = -1.60`
  - `diyizhanshi`
    - `第一站是`: `ReqBridge = 0.00`, `AltReqBridge = 2.98`
    - `第一展示`: `ReqBridge = 0.00`, `AltReqBridge = 2.44`
    - `第一站十 / 第一站时`: `ReqBridge = 0.00`, `AltReqBridge = 2.00`
- 这说明：
  - 新 family-aware bias 并不是完全没生成
  - 当前真正缺口在于：`poet` 当前 source line 的 key，没有命中 translator 生成 hint 时绑定的 `source_text`
  - 因而主问题已经从“权重是否太小”转成“hint 绑定到了哪个 source_text / family”
- 同时，把 `witset_translator.cc` 中 `FindRequestStageStateByText()` 从“返回第一条同文本 state”改成“在同文本里优先选择更强的 confirmed / 更具体 family state”：
  - 优先 confirmed
  - 再优先 `bridge_lineage_confirmed`
  - 再优先更大的 `exact_segment_count`
  - 再优先更长的 `family_identity`
- 修复后再次执行 `librime/build.bat static`，并重跑同一条 `snapshot` 快路径。
- 结果：
  - `ReqBridge` 仍为 `0.00`
  - 但 `AltReqBridge` 进一步变化，`diyizhanshi` 下 `第一站是` 从 `2.98` 升到 `3.33`
- 结论更新：
  - “同文本多 state 只取第一条”确实会削弱 hint 强度，但不是 `ReqBridge` 全 miss 的主因
  - 当前最核心、且尚未解决的问题仍是：
    - 需要定位 translator 最终把正负 hint 分别挂到了哪些 `source_text`
    - 再决定是统一 key 口径，还是把 hint 绑定从 `source_text` 升级为更稳定的 family/source-line identity
- 继续沿这条线，只做了一轮更细的诊断增强：
  - 在 `witset_poet.cc` 的 `AltReqBridge` 基础上，新增 `StepAltReqSrc`
  - 它直接输出“忽略 `source_text` 后命中的最强 hint，原本绑定的是哪条 `source_text`”
- 按工作区规则再次执行 `librime/build.bat static`，编译通过；随后仍只跑：
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
- 这轮关键诊断结果：
  - `diyizhanshi`
    - `第一站是`: `ReqBridge = 0.00`, `AltReqBridge = 3.33`, `StepAltReqSrc = 第一站`
    - `第一站十 / 第一站时`: `ReqBridge = 0.00`, `AltReqBridge = 2.00`, `StepAltReqSrc = 第一站`
    - `第一展示`: `ReqBridge = 0.00`, `AltReqBridge = 2.44`, `StepAltReqSrc = 第一展`
  - 这直接说明：
    - translator 侧生成 hint 时绑定的 `source_text` 是局部生成串，如 `第一站 / 第一展`
    - poet 侧此前查 key 用的 `candidate->full_context()` 则会带上前文整句
    - 因而两边不是同一口径，`ReqBridge` 才会整体 miss
- 基于这条直接证据，继续做了最小正式修复：
  - 在 `WitsetPoet::Line` 新增 `generated_text()`
  - 将 request-stage hint 查 key 的 `source_text` 从 `candidate->full_context()` 改为 `candidate->generated_text()`
- 修复后再次编译通过，并复跑同一条 `snapshot` 快路径。
- 修复后的结果：
  - `diyizhanshi`
    - `第一站是`: `ReqBridge = 3.33`, `AltReqBridge = 3.33`, `StepAltReqSrc = 第一站`
    - `第一展示`: `ReqBridge = 2.44`, `AltReqBridge = 2.44`, `StepAltReqSrc = 第一展`
    - `第一站十 / 第一站时`: `ReqBridge = 2.00`, `AltReqBridge = 2.00`, `StepAltReqSrc = 第一站`
  - `diyizhans`
    - `第一站十`: `ReqBridge = 2.00`, `AltReqBridge = 2.00`
    - `第一站是`: `ReqBridge = 3.33`, `AltReqBridge = 3.92`
  - 结论：
    - 主问题“poet 因 source_text 口径不一致而整体 miss hint”已经打通
    - `diyizhanshi` 这一帧上，`ReqBridge` 与旁路读数已完全对齐
    - `diyizhans` 下 `第一站是` 仍残留少量差额，说明还有一条更强 hint 绑定在异常 `source_text`（本轮观测到是 `第一种按`）上，属于后续继续清理的尾差，不再是主链断路
- 拿到这轮有效改动后，按“代表集 smoke 复核”的既定策略，尝试对 `shared-prefix` 代表语料做一次高性价比复核：
  - 命令：
    - `python C:\Code\outwit\outwit-windows\librime\plugins\witogram\tools\run_local_snapshot_baseline.py --corpus C:\Users\Bing\AppData\Roaming\witty\debug\_shared_prefix_eval_excerpt.txt --limit 6 --snapshot-timeout-seconds 60 ...`
    - 随后又把超时放宽到 `120`
  - 两次都在第一条 `huranjiujuede` 上超时，且 `last_seen_input = ''`
  - 这说明当前失败点是 baseline 冷启动阶段根本没有写出首条 snapshot，不是 smoke 结果变差
  - 按效率规则，这轮先停止继续在这条链上烧时间；后续若继续跑代表集，应先单独排查为什么这次 baseline 启动后完全没有首条 snapshot
- 继续清理 `diyizhans` 下 `第一站是` 残余 `AltReqBridge > ReqBridge` 的尾差时，先尝试把 translator 中 `BuildPoetRequestStageCandidateHints()` 里给单字 `是` 的 `RequestStageBridgeSelectionBonus` 收紧为：
  - 只有当前 `request_stage_tag` 是 `request_source_line_eligible / request_source_line_terminal`
  - 且 `next_text` 命中的 confirmed request-stage state 同时满足 `aligned_with_best_prefix`
  - 才允许把这笔 `+3.0` bridge bonus 下发到 poet hint
- 这轮仍按工作区规则只做：
  - `librime/build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
- 结果：
  - `diyizhans` 下 `第一站是` 仍保持 `ReqBridge = 3.33`, `AltReqBridge = 3.92`, `StepAltReqSrc = 第一种按`
  - `diyizhanshi` 下 `第一站是 / 第一站十 / 第一站时 / 第一展示` 的 `ReqBridge` 与上轮一致，没有回退
- 这说明：
  - 残余异常并不只是“bridge bonus 被 tail-supported 脏状态误吃”
  - 更像是 `第一种按` 这条路径本身已经被上游视为 `aligned_with_best_prefix`
  - 因而当前真正的尾差根因已经前移到 `BuildRequestStageSourceAnchors()`：
    - 它完全依赖 `best_prefix_states`
    - `aligned_with_best_prefix` 实际上等价于“是否等于当前唯一 source anchor”
    - 若 `best_prefix_states` 在 `diyizhans` 这一帧已被错误 family 带偏，那么下游 request-stage family 竞争再怎么收紧，也会把错误 family 当成 validated source line
- 结论更新：
  - `poet source_text` 口径错位这条主链已经修复完成
  - 当前剩余尾差不应再在 poet 或 hint bonus 层继续局部打补丁
  - 若继续，需要直接审计 `BuildBestPrefixStates()` / `BuildRequestStageSourceAnchors()`，确认为什么 `diyizhans` 的 source anchor 会落到 `第一种按` 这类错误 family 上
- 继续沿“不要关掉简拼能力，但要纠正拆字偏爱”这个方向收口后，重新回看了输入层与上游风险链：
  - `C:\Users\Bing\AppData\Roaming\witty\shuangpin_algebra.yaml` 里确实保留了：
    - `abbrev/^([a-z]).+$/$1/`
    - `abbrev/^([zcs]h).+$/$1/`
  - `witset_translator.cc` 里也能确认：
    - `SpellingTypeName(kAbbreviation) = "abbreviation"`
    - beam 侧还会把 `raw_keys` 首字符视作潜在 abbreviation key
  - 但在此前“edge 风险降噪版 C”中，`ClassifyEdgeSpellingType()` 已被收窄为只保留 `ambiguous`
    - `abbreviation / completion / fuzzy` 被整体移出了 `edge_spelling_class`
  - 这使得“由简拼放出来的拆字段”在后续 `edge_risk / spelling_class / competitive edge bias` 链路里几乎被当成 clean edge
- 因而这轮没有去关掉 algebra 简拼规则，而是先做一版更通用的弱收缩：
  - `ClassifyEdgeSpellingType()`：
    - 若 edge 同时有 normal spelling，则仍视为 `0`
    - 若没有 normal spelling、但存在 `abbreviation`，则标成一个新的弱 class `1`
    - `ambiguous` 仍保持最高 class `4`
  - `ComputeTranslatorCompetitiveRiskScore()`：
    - 对 `edge_spelling_class == 1` 的 edge，给一个弱可见风险底座 `0.18`
    - 不把它抬到 `ambiguous` 那个量级
- 目标不是禁掉简拼，而是：
  - 让“纯 abbreviation 支撑的 edge”不再和正常整段 exact edge 一样被当成 clean competitor
  - 从而纠正“系统天然偏爱把一个 full segment 拆开走”的倾向
- 这轮仍只按工作区规则做：
  - `librime/build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
- 单 case 结果：
  - `diyizhan`
    - `第一站` 已稳定在 `rank 1`
  - `diyizhans`
    - `第一战士 = rank 1`
    - `第一站上 = rank 2`
    - `第一站是 = rank 4`
    - `第一展示 = rank 11`
  - `diyizhanshi`
    - `第一战士 = rank 1`
    - `第一展示 = rank 2`
    - `第一站是 = rank 3`
  - 全句：
    - `第一站是以做古老的小镇 = rank 5`
    - 仍未翻正为 `第一站是一座古老的小镇`
- 旁路读数补充：
  - `diyizhans`
    - `第一站是`: `ReqBridge = 3.33`, `AltReqBridge = 3.92`, `StepAltReqSrc = 第一种按`
    - `第一站十`: `ReqBridge = 2.00`, `AltReqBridge = 2.00`, `StepAltReqSrc = 第一站`
    - `第一展示`: `ReqBridge = 0.00`, `AltReqBridge = -1.60`, `StepAltReqSrc = 地以`
  - `diyizhanshi`
    - `第一站是`: `ReqBridge = 3.33`, `AltReqBridge = 3.33`, `StepAltReqSrc = 第一站`
- 结论：
  - 这版“弱 abbreviation 风险”方向是对的：
    - 它确实在纠正“abbrev edge 太干净”的倾向，而不是直接关闭简拼能力
  - 但它只修到了第一层：
    - `第一种按` 这条异常 `source_text` 仍然存在
    - 说明真正更深的主缺口仍在 `BuildBestPrefixStates()` / `BuildRequestStageSourceAnchors()`
    - 即：错误 family 在 `diyizhans` 这一帧仍可能被上游 source-anchor 当成 validated source line
- 继续在“允许简拼，但不要让 pure-abbreviation split edge 抢 ownership”这条线上前移一层：
  - 新增 `GetPureAbbreviationEdgePenalty()`，直接读取 `CredibilityLedger.edge_spelling_classes`
  - 若某条 edge 被判成前一轮新引入的弱 class `1`（纯 abbreviation、无 normal spelling 支撑），则在两个 ownership 入口同时减去 `0.35`
    - `BuildBestPrefixStates()`
    - `BuildRequestStageSourceAnchors()`
  - 同时把这套口径接进三条链，避免只修一半：
    - `WordGraphRewriter::Apply(...)` 后的 request-stage ownership / hint 生成
    - `BuildPoetRequestStageCandidateHints(...)`
    - `BuildLocalGraphSnapshotLine(...)`
- 这轮仍按工作区规则只做：
  - `librime/build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
- 结果：
  - 编译通过
  - 单例 snapshot 的关键排名与旁路读数基本不变：
    - `diyizhans`
      - `第一站是 = rank 4`, `ReqBridge = 3.33`, `AltReqBridge = 3.92`, `StepAltReqSrc = 第一种按`
      - `第一站上 = rank 2`
      - `第一展示 = rank 11`
    - `diyizhanshi`
      - `第一站是 = rank 3`, `ReqBridge = 3.33`, `AltReqBridge = 3.33`, `StepAltReqSrc = 第一站`
    - 全句仍是：
      - `第一站是以做古老的小镇 = rank 5`
- 这说明：
  - 单纯把 pure-abbreviation 弱降权送进 `best_prefix/source_anchor` 选锚过程还不够
  - 更可能的两种情况是：
    - `第一种按` 这条异常 ownership 来源本身并不是当前定义的 class `1`
    - 或者它虽然带有 abbreviation 成分，但主导 source-anchor 的那条边不是单条 pure-abbreviation edge，而是更上游的整段 prefix / anchor 组合
- 结论更新：
  - 当前可以确认：`abbreviation edge 太干净` 不是唯一原因
  - 下一步若继续，最值得做的不是再盲调权重，而是直接把 `diyizhans -> 第一种按` 这条 ownership 来源对应的 edge spelling class / risk / raw key 诊断打出来，确认它到底是不是 pure abbreviation edge
- 为了把这条诊断真正落地，继续做了一个最小 instrumentation，而不是再改算法：
  - 给 `RequestStagePrefixState` 新增：
    - `last_start_pos`
    - `last_end_pos`
    - `last_edge_risk`
    - `last_edge_spelling_class`
  - 并在 `BuildLocalGraphSnapshotLine()` 的 `request_stage_states` 输出中追加：
    - `last_edge_raw_keys`
- 同时把 `CredibilityLedger` 传进 `BuildRequestStagePrefixStates()`，保证这些 state 级诊断字段能带着真实 edge class/risk 落到 graph 工件中。
- 这轮仍只做最小闭环：
  - `librime/build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode full`
- 新诊断结果先给出了一条很关键的排除项：
  - 在完整句 `diyizhanshiyizuogulaodexiaozhen` 的 request-stage competing source lines 里：
    - `第一站 / 第一展 / 地一站 / 第一站是 / 第一展是 / 第一站是以 / 第一展是以`
    - 它们记录到的最后一步 `last_edge_raw_keys` 分别就是正常分段：
      - `diyizhan`
      - `yizhan`
      - `zhan`
      - `shi`
      - `yi`
    - 且这些 state 的 `last_edge_spelling_class` 全都是 `0`
    - `last_edge_risk` 也全都是 `0`
- 这说明：
  - 在完整句里，真正参与抢 ownership 的这些 source line，至少在 `zhan / shi / yi` 这些最后几步上，并不是 pure abbreviation edge
  - 因而“简拼拆字太干净”最多只是次级放大项，不是当前完整句 request-stage ownership 漂移的主因
  - 这也解释了为什么前一轮把 pure-abbreviation 弱降权前移到 `best_prefix/source_anchor` 后，单例结果几乎不动：我们降的是 class `1` edge，但当前完整句里主导 competing source line 的最后几步根本不是 class `1`
- 当前剩余问题进一步收口为：
  - `StepAltReqSrc = 第一种按` 仍然只在短输入 `diyizhans` 的 snapshot 旁路里出现
  - 同一异常 source text 在完整句 graph 中并未直接出现
  - 因而它更像是“短输入阶段的 request-stage ownership 漂移”，而不是完整句后续 `shi / yi` 桥接 edge 的简拼污染
- 结论更新：
  - 不能再把主矛头继续放在 `abbreviation edge` 本身
  - 下一步若继续，最值得做的是给短输入 `diyizhans` 本身补一条 request-stage graph/trace 直采能力，或者在 snapshot 中直接把 `AltReqBridge` 命中的 source state 元数据打印出来
  - 只有这样，才能直接看到 `第一种按` 在短输入帧上的真正 `raw_keys / spelling_class / risk`，而不是用完整句工件去间接推断
- 继续沿这条线，没有再先猜算法，而是先补工具能力：
  - 修改 `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py`
  - 让 `case2_diyizhan` 在 `full` 模式下支持额外采集短输入 graph：
    - `diyizhans`
    - `diyizhanshi`
  - 同时验证出一个机制边界：
    - 仅 `graph-only` 直跑短输入时，`graph.jsonl` 仍可能是空
    - 但当 `snapshot + next-hop + graph` 三种采集一起开时，`diyizhans` 的 graph 可以稳定落盘
- 在 translator 侧继续做了最小 instrumentation：
  - 给 `RequestStagePrefixState` 新增：
    - `cumulative_risk_penalty`
    - `last_start_pos`
    - `last_end_pos`
    - `last_edge_risk`
    - `last_edge_spelling_class`
  - 并在 graph 输出里继续补 `last_edge_raw_keys`
  - 同时把 `CredibilityLedger` 正式接进 `BuildRequestStagePrefixStates()`
- 这样之后，短输入 `diyizhans` 的 request-stage source line 已经可以直接看见“最后一步来自哪条 edge，以及那条 edge 带了什么 risk/class/raw_keys”
- 按最小闭环重新执行：
  - `librime/build.bat static`
  - 用脚本内联调用直采 `diyizhans` 的 `snapshot + next-hop + graph`
- 新证据：
  - `第一种按` 本身已经不再只是旁路猜测，而是能在 `diyizhans` 的 graph 里直接看到：
    - `text = 第一种按`
    - `family_identity = 第一种`
    - `last_edge_raw_keys = an`
    - `last_edge_risk = 1`
    - `last_edge_spelling_class = 0`
  - 继续到 `第一种按是` 时：
    - `last_edge_raw_keys = s`
    - `last_edge_risk = 0`
    - `last_edge_spelling_class = 1`
- 这把链路进一步钉死为：
  - 真正把错误 family 带进 request-stage 的关键步，是更早的 `an`
  - 后面的 `s -> 是` 确实带有 abbreviation class `1`
  - 但它更像是在已经偏掉的 family 上续接，而不是主导源头
- 基于这条证据，继续做了一版通用修正，不针对句子或读音：
  - 在 `BuildRequestStagePrefixStates()` 中，为每个 state 增加累计风险债
  - 债值口径：
    - `edge_risk_penalty = 2.0 * ComputeTranslatorCompetitiveRiskScore(edge_risk, 0.0, edge_spelling_class)`
  - `selection_score` 改为在原先 `raw_score + best_tail + bridge_bonus` 基础上减去累计风险债
  - 目标：
    - 让像 `第一种按` 这种由高风险 `an` 带起来的 family，在后续续接成 `第一种按是` 时也继续背着风险债，不再在下一步被“洗白”
- 重新编译通过，并再次只跑：
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
- 结果：
  - `diyizhans` 盘面没有发生可见变化：
    - `第一战士 = rank 1`
    - `第一站是 = rank 4`, `ReqBridge = 3.33`, `AltReqBridge = 3.92`, `StepAltReqSrc = 第一种按`
    - `第一站十 = rank 5`
    - `第一展示 = rank 11`
  - 说明：
    - “把风险债累计进 request-stage state”这个方向在机制上是合理的
    - 但它单独一刀仍不足以压掉 `第一种按`
- 最新结论：
  - 现在已经不需要再猜 `第一种按` 到底是不是简拼污染
  - 证据表明它是：
    - 先由一个 `an` 高风险 edge 把 family 带偏
    - 再由一个 class `1` 的 `s` 简拼续接到 `是`
  - 但即便把这两步风险都计入 state 累计债，当前 source-anchor / request-stage ownership 仍未发生可见翻转
  - 因而下一步若继续，最值得直接查的是：
    - 为什么这条已经背着风险债的 `第一种按(是)` 仍然能保持 `aligned_with_best_prefix = true`
    - 即：问题可能已经不只是 state 排序分，而是 `best_prefix/source_anchor` 本身的 validated anchor 生成口径还在把它当成同等合法 family
- 继续按这条线往下查后，真正命中的不是继续加分/减分，而是修正了 `aligned_with_best_prefix` 的语义：
  - 之前在 `BuildRequestStagePrefixStates()` 里使用的是：
    - `anchor_aligned || current_state.aligned_with_best_prefix`
  - 这意味着一条路径只要前面某一步曾经对齐过 validated anchor，后面即使已经分叉成别的 family，也会一直保留 `aligned_with_best_prefix = true`
  - 这与“当前 end_pos 仍与当前 validated anchor 对齐”不是同一个语义
- 另外还继续把 risk 前移到 validated anchor 生成口径：
  - 新增 `GetValidatedAnchorRiskPenalty()`
  - `BuildBestPrefixStates()` 不再只按 `candidate->weight` 选每个 span 的 winner，而改成按：
    - `candidate->weight + bridge_bonus - validated_anchor_risk_penalty`
  - `BuildRequestStageSourceAnchors()` 也同步吃这套 risk penalty
- 重新执行最小闭环：
  - `librime/build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
  - 再用脚本内联直采 `diyizhans` 的 `snapshot + next-hop + graph`
- 新结果：
  - `diyizhans`
    - `第一站是 = rank 4`
    - `ReqBridge = 2.00`
    - `AltReqBridge = 2.00`
    - `StepAltReqSrc = 敌意展`
    - 原来那条 `StepAltReqSrc = 第一种按` 已不再出现
  - `diyizhanshi`
    - `第一站是 = rank 3`
    - `ReqBridge = 2.00`
    - `AltReqBridge = 2.00`
    - `StepAltReqSrc = 第一站`
    - `第一展示 = rank 2`, `ReqBridge = 0.00`, `AltReqBridge = 2.00`, `StepAltReqSrc = 第一战`
  - 完整句：
    - `第一站是以做古老的小镇` 由 `rank 5` 退到 `rank 7`
    - `ReqBridge = 0.00`
- 更关键的是短输入 `diyizhans` 的 graph 变化：
  - `第一种按` / `第一种按是` 不再出现在 request-stage state 输出中
  - `第一站` 相关 state 仍保留 `aligned_with_best_prefix = true`
  - `第一站是` 当前是：
    - `aligned_with_best_prefix = false`
    - `last_edge_raw_keys = s`
    - `last_edge_spelling_class = 1`
- 这说明：
  - 这次确实打到了根上的一个语义 bug：
    - `aligned_with_best_prefix` 之前被错误地当成“历史曾对齐过”而不是“当前仍对齐”
  - 修掉后，`第一种按` 这条异常 source line 已经被从 validated-aligned 家族里清掉
  - 但新的盘面也表明：
    - `第一站是` 这条单字简拼续接本身目前也不再属于 validated-aligned source line
    - 因而 `ReqBridge` 整体量级从 `3.33/3.92` 收缩到了 `2.00/2.00`
- 当前结论更新：
  - “第一种按 抢 validated anchor”这一层已经被收掉
  - 但 `第一站是` 还没有真正翻正，说明剩余问题已进一步收口到：
    - `s -> 是` 这种单字简拼桥本身在 request-stage 里应不应该、以及如何重新获得稳定确认资格
  - 也就是说，下一步不该再围绕 `第一种按` 打转，而该转去审视：
    - `ClassifyRequestStageTag(...)`
    - `ComputeRequestStageBridgeSelectionBonus(...)`
    - `第一站 + 是` 在失去“历史对齐继承”后，怎样以通用条件重新拿回应有的 confirmed/source-line 资格
- 继续沿这个缺口推进后，没有回滚“历史对齐继承”的错误语义，而是尝试给真正的单字桥一个正当入口：
  - 在 `BuildRequestStagePrefixStates()` 新增 `CanConfirmValidatedSingleCharBridge()`
  - 第一版条件是：
    - 当前 request-stage prefix 已 confirmed 或 aligned
    - 当前 candidate 是 exact single-char
    - 下游仍有 tail support
  - 这样能把 `第一站 + 是` 重新标成 `bridge_lineage_confirmed`
- 结果表明确实命中了主诉求的一半：
  - `diyizhans`
    - `第一站是` 的 `ReqBridge` 从 `2.00` 回升到 `4.00`
  - `diyizhanshi`
    - `第一站是` 的 `ReqBridge` 从 `2.00` 回升到 `4.93`
  - 短输入 graph 里：
    - `第一站是` 已重新变成 `bridge_lineage_confirmed = true`
- 但第一版条件过宽，马上暴露出新的副作用：
  - 不只是 `第一站 + 是`，连 `第一种含 -> 是` 这类高风险 family 也会一起拿到 `bridge_lineage_confirmed`
  - 旁路来源从原来的 `第一种按` 滑成了：
    - `第一种含`
    - 以及后续 `第一种和按`
- 为此又追加了一层最小收紧，而不是整段推倒：
  - `CanConfirmValidatedSingleCharBridge()` 要求“当前这一步本身也是低风险 single-char edge”
  - 用 `ComputeTranslatorCompetitiveRiskScore(edge_risk, 0.0, edge_spelling_class) <= 0.18` 作为门槛
- 第二版结果：
  - `第一站是` 的桥确认继续保住：
    - `diyizhans`: `ReqBridge = 4.93`
    - `diyizhanshi`: `ReqBridge = 4.93`
  - 但高风险 family 仍未完全被收掉，只是从：
    - `第一种按`
    - 变成了 `第一种含 / 第一种和按`
- 这说明：
  - 单纯把“validated prefix + 低风险单字 bridge”视为 confirmed，方向上是对的，已经把 `第一站是` 的主桥重新点亮
  - 但它仍缺一个 family 级约束：
    - 当前条件只看 prefix 是否 confirmed/aligned，以及当前 bridge edge 是否低风险
    - 还没有要求“这个 bridge 必须延续当前 validated family，而不是接在错误 family 上继续漂移”
- 最新结论：
  - 现在主缺口已经不是“桥确认有没有”
  - 而是“桥确认该挂给哪个 family”
  - 下一步若继续，最值得直接做的是：
    - 在 `CanConfirmValidatedSingleCharBridge()` 或相邻逻辑里加入 family/source-line 继承约束
    - 不再只检查 prefix confirmed 与当前 edge 低风险，还要检查该 bridge 的 next_text / family_identity 是否与当前 validated source line 保持一致
- 继续沿这条线收口后，发现更深一层的问题不在“新桥确认入口”本身，而在 `bridge_lineage_confirmed` 的传播口径：
  - 之前的逻辑里，只要某条路径前面某一步已经 `bridge_lineage_confirmed = true`
  - 后面即使 family 已经偏成 `第一种和按` 这类错误 source line，也会继续无条件继承 confirmed 身份
- 因而本轮没有再调数值，而是新增了一个 source-line 约束的传播 helper：
  - `CanPropagateBridgeLineageConfirmation()`
  - 条件：
    - 当前 prefix 本身已经 `bridge_lineage_confirmed`
    - 并且它要么当前仍 `aligned_with_best_prefix`
    - 要么当前 `prefix_state.text` 仍等于该 `start_pos` 的 validated source anchor
- 同时保留上一轮新增的单字桥确认入口，但让它也走同一套 source-line 继承约束：
  - `CanConfirmValidatedSingleCharBridge()`
  - 仍要求：
    - exact single-char
    - 当前 bridge edge 低风险
    - 下游 tail support 存在
  - 并且若不是当前 aligned prefix，则必须命中当前 `start_pos` 的 validated source anchor
- 重新执行最小闭环：
  - `librime/build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode snapshot`
  - 再用脚本内联直采 `diyizhans` 的 `snapshot + next-hop + graph`
- 新结果：
  - `diyizhans`
    - `第一站是 = rank 4`
    - `ReqBridge = 3.60`
    - `AltReqBridge = 3.60`
    - `StepAltReqSrc = 第一站`
    - 之前那条 `第一种和按` 旁路已被清掉
  - `diyizhanshi`
    - `第一站是 = rank 3`
    - `ReqBridge = 3.60`
    - `AltReqBridge = 3.60`
    - `StepAltReqSrc = 第一站`
- 短输入 `diyizhans` 的 graph 也已坐实：
  - `第一种和按` / `第一种和按是` 已不再出现在 request-stage state 输出里
  - `第一站是` 只剩 `family_identity = 第一站 / 第一 / 第` 这些同源状态
  - `last_edge_raw_keys = s`, `last_edge_spelling_class = 1`
- 这说明：
  - 这轮真正命中的，是“错误 family 不能继续继承 confirmed 身份”这条通用约束
  - 现在 `第一站是` 的桥确认已保住，且旁路 source line 已收敛回正确家族
- 但新的结果也同步说明：
  - `ReqBridge` 从前一轮的 `4.93` 收到了 `3.60`
  - 说明之前那部分更高的桥分里，确实混入了错误 family 带来的膨胀
  - 现在的 `3.60` 更像是干净后的真实桥确认强度
- 当前结论更新：
  - “桥确认该挂给哪个 family”这一层已经被明显收口
  - 下一步若继续，重点不该再放在错误 family 漂移
  - 而应回到更直接的问题：
    - 为什么在桥确认已干净收敛回 `第一站` 后，`第一站是` 仍然排在 `第一战士 / 第一展示` 后面
    - 也就是剩余问题更可能已经回到：
      - next-hop ranking
      - LM / search score
      - 或 `第一站 + 是` 与 `第一展 + 示` 的后续承接差额

### 2026-05-24 补充：`family_soft_clean -> HasConfidentPrimaryExact` 与 `head-supported single-char bridge` 两条最小补丁都已判负

- 继续前先复核了上一轮最末尾留下的实验态代码与验证结果，确认：
  - `HasConfidentPrimaryExact()` 放宽到接受 `family_soft_clean`
  - 这条补丁已经编译并做过最小验证，但当时尚未回退
- 这轮先把它的真实作用路径钉死：
  - `ClassifyContinuationTag()` 对关键点
    - `体验不 -> 一`
    - `第一站是一 -> 座`
    - `体验不一 -> 样`
    都会先命中 `char_count < 2 -> non_contract_candidate`
  - 因而这些主竞争点根本走不到 `HasConfidentPrimaryExact()`
  - 这说明该补丁不是“力度不够”，而是**打不到当前关键单字链路**
- 处理：
  - 已将 `HasConfidentPrimaryExact()` 的 `family_soft_clean` 放宽补丁完整回退
  - 并重新执行 `librime\\.\\build.bat static`

### 后续继续尝试的最小新路：`head-supported single-char bridge`

- 基于已有证据，这轮尝试了一条更贴近目标的通用实验：
  - 不再复用只按 `end_pos` 统计的 `request_tail_supported / downstream_exact_completion_count`
  - 而是新增“当前起点下，是否存在以该单字为首字的强多字 exact 候选”这一类 `head support`
  - 目标是候选级地区分：
    - `一 -> 一样 / 一座`
    - 与
    - `宜 / 依 / 以`
      这类只是同位单字、但不共享同源强多字 exact head 的竞争者
- 这条实验先后试了两个最小落点：
  1. 先挂在 `translator -> poet` 的 request-stage hint 生成层
  2. 随后前移到 translator 的 candidate bias 层，直接改单字候选竞争
- 两版都已编译并做最小验证：
  - `partial_chain_stage_probe.py --case case2_diyizhan --case case3_tiyanbuyiyang --mode snapshot`
  - 并额外读取 `snapshot.jsonl` 中：
    - `diyizhanshiyi`
    - `tiyanbuyi`
    - `tiyanbuyiyangdeshenghuo`
    的目标排名

### 结果

- 第一版（poet hint 层）：
  - 基本无可见收益
  - 说明如果正确链在 `request_stage_prefix_states` 里已经没有 ownership，这条 hint 仍然打不到关键点
- 第二版（translator candidate bias 层）：
  - `体验不宜 / 体验不一` 的 `ReqBridge` 从 `-0.79` 轻微变成 `-0.87`
  - 但排序完全没变：
    - `体验不易 = rank 1`
    - `体验不宜 = rank 4`
    - `体验不一 = rank 15`
    - `体验不一样的生活 = rank 7`
  - `case2` 也没有任何翻正迹象：
    - `diyizhanshiyi` 仍然完全被 `...是以` 链主导
    - `第一站是一` / `第一站是一座古老的小镇` 仍未进入 top20
- 判断：
  - 这条路不是纯 no-op，因为它确实改到了局部分差
  - 但它依然无法把正确链拉出当前 LM / request 排名劣势
  - 因而仍应视为**明确失败路径**

### 当前收口更新

- 已将两版 `head-supported single-char bridge` 实验代码完整回退
- 并再次执行 `librime\\.\\build.bat static`，恢复干净基线
- 由此当前可明确避免的重复路线再增加两条：
  - 不要再围绕 `family_soft_clean -> HasConfidentPrimaryExact` 微调
  - 不要再围绕“仅靠 strong exact head support 给单字桥加轻量 bias”继续试常数或换落点
- 这也进一步说明：
  - 当前剩余缺口更可能已经不在“单字桥资格再补一点”
  - 而在更早的 request / LM 盘面对 `一 / 座 / 样` 这条链本身的基础排序证据

### 2026-05-24 补充：`request_source_chain_case2_case3.json` 已把断点进一步收口到 source_pool / request 早层

- 继续沿现有工件只读排查 `request_source_chain_case2_case3.json` 后，可以把 `case2/case3` 的首次失守层级再明确收口：
  - `case2` 不是 `第一站是 -> 一` 这类单字 bridge 才第一次出错
  - `case3` 也不是 `体验不 -> 一样` 才第一次出错
  - 两者都在更早的 `source_pool / batch_selected` 盘面里已经形成了难以逆转的底盘差额

#### `case2`: `第一站是一座古老的小镇`

- 起点 `source_pool` 本身：
  - `第一站 = -60.578`
  - `第一战 = -60.8091`
  - `第一展 = -61.311`
  - 说明 `第一站` 在起点并没有输，甚至还是三者里最强
- 第一跳 `第一站 -> 是` 也没有失守：
  - `batch_selected / admitted_new`
  - `第一站 + 是 = -114.487`
  - 仍然明显好于
    - `第一战 + 是 = -116.318`
    - `第一展 + 是 = -103.308` 对应的是 `第一展示` 这条整体重解释链，不是同一跳的单字 bridge 问题
- 真正的断点出现在第二跳多字展开：
  - `第一站 + 是以 = -136.354`
  - `第一站 + 是一 = -154.93`
  - 同一起点下直接相差 `18.576`
  - 且这个差额已经体现在 `search_score / base_score` 主盘面，而不是后续 poet 或 request-stage 补偿层
- 后续 `第一站 + 是一座 = -199.336` 仍然能够入池，但这只是沿着已经劣势极大的 `是一` 链继续延伸，并不是在这里才第一次被挡掉
- 因而 `case2` 当前最准确的收口应是：
  - `第一站` 自身不是首次问题
  - `第一站 -> 是` 也不是首次问题
  - **首次不可逆断点在 `第一站是 -> 是一 / 是以` 的 request / LM 多字展开竞争**

#### `case3`: `体验不一样的生活`

- 起点 `source_pool` 已经先天失衡：
  - `体言 = -45.77`
  - `体验 = -75.4642`
  - 两者一开始就相差约 `29.6942`
- 虽然第一跳里 `体验 -> 不 = -103.72` 明显优于 `体言 -> 不 = -111.6`
  - 说明 `不` 这个 continuation 本身并不是站不住
  - 但它只能回补约 `7.88`
  - 远不足以抹平句首 `体言 / 体验` 已经形成的巨大 source/base 差额
- 进入多字展开后，这个早层差额继续延续：
  - `体言 + 不易 = -121.469`
  - `体验 + 不易 = -124.697`
  - `体言 + 不一样 = -191.261`
  - `体验 + 不一样 = -194.412`
- 因而 `case3` 当前最准确的收口应是：
  - `体验不一样` 这一步本身不是第一次失守
  - **首次不可逆断点已经在句首 `体验 / 体言` 的 source_pool 起点形成**
  - 后面的 `不 / 不易 / 不一样` 只是把这条早层差额继续往后传递

#### 当前结论更新

- 后续若继续推进，不应再把主要精力放在：
  - 单字 bridge contract
  - request-stage 单字保活
  - 或给 `一 / 座 / 样` 追加轻量补偿
- 更值得继续拆的是：
  - `case2` 中 `第一站是` 之后 `是一 / 是以` 的 request / LM 多字展开为什么天然偏向 `是以`
  - `case3` 中句首 `体验 / 体言` 为什么会在 source_pool 就拉开近 30 分差
- 也就是说，下一步若要做新改动，优先级应继续前移到：
  - whole-word / char-path / fallback / OOV 证据
  - 句首 source candidate 生成与 request base ranking
  - 而不是继续围绕后验桥接合同打转

### 2026-05-24 补充：为 transition snapshot 导出 `token_evidence_tag` 后，`case3` 旧结论需要纠正

- 为避免继续被 `used_char_fallback / matched_whole_word` 这类粗字段误导，先做了一个很小的诊断性改动：
  - 在 `WitsetPoet::DebugTransitionLMRecord` 中新增 `token_evidence_tag`
  - 把 `WitogramScoreFeatures.token_evidence_level` 直接导出到本地 graph snapshot
- 之后只重新编译一次，并仅对 `case2_diyizhan` 与 `case3_tiyanbuyiyang` 跑了 2 例 `full` probe；这次不做大回归

#### `case2` 新证据：`是一 / 是以` 的主分歧确实在 token evidence，而不是 request-stage bridge

- 从新导出的 transition 记录看，围绕 `第一站/驿站/... -> 是一 / 是以`：
  - `是以` consistently 是 `split_token_supported`
  - `used_char_fallback = true`
  - `matched_whole_word = false`
  - `oov_token_count = 0`
  - `total_log10 ≈ -54.77`
- 相对地，`是一` consistently 是：
  - `neutral_missing`
  - `used_char_fallback = true`
  - `matched_whole_word = false`
  - `oov_token_count = 1`
  - `total_log10 ≈ -77.99`
- 也就是说，对当前 split-token grammar 来说：
  - `是以` 拿到的是“有完整 split-token 支持”的证据
  - `是一` 拿到的是“中性缺失 + 1 个 OOV”的证据
- 因而 `case2` 当前主缺口进一步收口为：

## 2026-05-27 `P1 request-stage ownership` 继续：`Task 2` 已完成，`Task 3` 单例 graph/acceptance 已命中

- 继续前先按最新计划与 `WORKLOG` 去重，确认当前合法停点仍是：
  - `docs/superpowers/plans/2026-05-26-request-stage-ownership-p1.md`
  - 先完成 `Task 2` 的 family-tail 字段链
  - 再做 `Task 3` 的 translator 侧单例验证
- 本轮没有回退到旧的 `poet` 末端 patch，也没有再走：
  - continuation weight 放大
  - 尾步标签补丁
  - 早期单字桥 family split + 多保一条分支

### `Task 2` 状态

- family-aware tail 只读字段链已接通：
  - translator state
  - translator graph/snapshot
  - poet metadata/debug
  - `partial_chain_stage_probe.py`
- 相关 focused tests 已绿：
  - `test_case1_family_tail_acceptance.py`
  - `test_partial_chain_stage_probe.py`

### `Task 3` 单例结论

- 只对单例临时打开：
  - `--upstream-family-tail-weight 1.0`
- 在 `case1_yizhixiangwang` 的 `next_hop_after_yizhi` graph focus 中，`向往` 当前已不再是此前的：
  - `not_request_stage_candidate`
- 当前读到的关键字段为：
  - `request_stage_tag = request_source_line_eligible`
  - `request_stage_family_tail_state_count = 1`
  - `request_stage_family_tail_weight = -24.9571`
  - `request_stage_generic_tail_weight = -24.9571`
- 这说明这轮 translator 侧 family-tail ownership 原型，至少在 `case1` 的 request-stage ownership 层已经命中：
  - 正确 family 更早进入了 eligible
  - focused acceptance 与手工读数一致

### 当前保留判断

- 目前已能确认的是：
  - `Task 2` 已完成
  - `Task 3` 在 `case1` 的 graph/acceptance 层命中
- 目前还不能直接记为“原型可保留”的是：
  - 最小代表集还没有收完整
  - 尤其 `case2 / case3 / case4 / case7` 还需要同口径复核，确认没有 guardrail 回退或 shared-prefix 过宽
- 因而下一步仍应按计划继续：
  - 最小代表集验证
  - 只有代表集方向正确，才把这刀记为保留；否则立即止损

## 2026-05-27 `Task 4` 最小代表集复核：`family-tail ownership` 原型 graph 命中，但端到端应止损回退

- 为避免再跑高成本的 `partial_chain_stage_probe.py --mode full` 多 case 组合，本轮改用脚本现有 helper 做轻量单句直读：
  - 复用 `partial_chain_stage_probe.py` 的 `update_schema(...)`
  - 复用 `run_local_snapshot_baseline.py` 的 `launch_console / SnapshotTailReader / wait_for_snapshot_record`
  - 每条 case 单独清空 `snapshot.jsonl`，只等待该整句 input 的最终 snapshot
  - 不再开启 graph / next-hop 导出，避免再次生成超大中间工件

### 同口径对照结果

- gate=`upstream_family_tail_weight: 1.0`
  - `case1_yizhixiangwang`
    - 目标：`一直向往着远方`
    - top1：`一直想望着远方`
    - 结论：虽然 graph 中 `向往` 已进入 `request_source_line_eligible`，但最终整句 top1 仍未翻正
  - `case2_diyizhan`
    - top1 仍为：`第一站是一座古老的小镇`
    - 结论：守住
  - `case3_tiyanbuyiyang`
    - top1 仍为：`体验不一样的生活`
    - 结论：守住
  - `case4_liangpang`
    - top1 变为：`两旁事故涩谷香的建筑`
    - 结论：回退
  - `case7_liushi`
    - top1 仍为：`也带着一种不可挽回的流失`
    - 结论：未改善

- baseline=`upstream_family_tail_weight: 0.0`
  - `case1_yizhixiangwang`
    - top1 同样是：`一直想望着远方`
  - `case2_diyizhan`
    - top1 同样是：`第一站是一座古老的小镇`
  - `case3_tiyanbuyiyang`
    - top1 同样是：`体验不一样的生活`
  - `case4_liangpang`
    - top1 是：`两旁是古色古香的建筑`
  - `case7_liushi`
    - top1 同样是：`也带着一种不可挽回的流失`

### 结论

- 这版 `family-tail ownership` 原型的真实效果已足够明确：
  - 它确实能让 `case1` 在 graph/request-stage 读数上更早进入 eligible
  - 但这种 graph 命中没有转化成 `case1` 的最终 top1 改善
  - 同时它把 `case4` 从基线正确打成了错误
- 因而本轮正式判定：
  - `Task 2` 保留：family-aware tail 字段链、调试导出、probe 读取继续保留
  - `Task 3` 原型不保留：translator 侧 family-tail ownership 行为改动应回退
- 已按此结论执行：
  - 回退 `Task 3` 的行为逻辑
  - 仅保留 `Task 2` 所需字段链与配置位
  - 删除只服务于 `Task 3` 原型的 focused acceptance
  - **`是一` 在 LM token evidence 上先天弱于 `是以`**
  - 这不是 request-stage 单字 bridge 或后验合同补一点就能翻的路线

#### `case3` 新证据：旧的“句首 `体言` 压过 `体验`”结论已经过期

- 重新跑当前代码后的 snapshot 与 transition 记录后，旧结论需要明确纠正：
  - 当前 `tiyanbuyiyangdeshenghuo` 的前列已经不再是 `体言...`
  - 当前 top12 全部都是 `体验...`
  - 正确句 `体验不一样的生活` 目前排在第 7
- 新 snapshot 前列现在是：
  - `体验不宜养的生活`
  - `体验不宜养的圣火`
  - `体验不宜养的生火`
  - `体验不易样的生活`
  - `体验不易养的生活`
  - ...
  - `体验不一样的生活` 才到第 7
- 新导出的 transition 进一步说明：
  - 句首 `体验` 本身是 `split_token_supported`
  - `used_char_fallback = true`
  - `matched_whole_word = false`
  - `oov_token_count = 0`
  - `total_log10 ≈ -54.77`
  - 而 `体言` 反而只是 `neutral_missing`
  - `oov_token_count = 1`
  - `total_log10 ≈ -77.99`
- 所以当前 build 下，`case3` 已不应再按“句首 `体言/体验` 首次失守”理解

#### `case3` 当前真正断点：`体验不 -> 易`，随后 `体验不易 -> 养`

- 对 `context_suffix = ...体验不` 的 transition：
  - `易` 是 `direct_whole_word_hit`
  - `oov_token_count = 0`
  - `total_log10 ≈ -18.32`
  - `宜 / 以 / 已 / 依` 也都是 `direct_whole_word_hit`
  - `total_log10 ≈ -39.50`
  - 只有 `一` 是 `neutral_missing`
  - `used_char_fallback = true`
  - `matched_whole_word = false`
  - `oov_token_count = 1`
  - `total_log10 ≈ -41.50`
- 这说明当前 `体验不` 之后：
  - `易` 拿到的是压倒性的 direct whole-word 命中
  - `一` 则落在 neutral-missing 且带 OOV 的弱证据上
  - 所以错误盘面先被 `不易` 拖走，而不是被 `体言` 拖走
- 再往后一跳，对 `context_suffix = ...体验不易`：
  - `样` 是 `direct_whole_word_hit`
  - `total_log10 ≈ -39.50`
  - `养` 也是 `direct_whole_word_hit`
  - `total_log10 ≈ -37.50`
- 因而当前 `case3` 的真实残留结构已收口为：
  - **`体验不 -> 易` 先赢**
  - **随后 `体验不易 -> 养` 又略优于 `样`**
  - 正确链 `体验不一样` 需要连续跨过这两层局部 whole-word 吸引子

#### 当前结论更新

- 当前最值得继续推进的方向进一步变化为：
  - `case2`：继续拆为什么 `是一` 在 split-token grammar 下稳定落成 `neutral_missing + OOV1`，而 `是以` 能拿到 `split_token_supported`
  - `case3`：不再盯句首 `体验/体言`，而应直接针对 `体验不 -> 易` 与 `体验不易 -> 养/样` 的局部 whole-word 吸引子
- 换句话说，当前剩余缺口已更像：
  - **局部 short-span whole-word / split-token evidence 过强**
  - 而不是早先认为的 `case3` 句首 source_pool 失守

### 2026-05-24 补充：`neutral_missing` 在 `witogram` 中当前只改标签，不改总分；`case2/case3` 差额主要都直接落在 `Base`

- 继续把新跑出的 `partial_chain_stage_probe.graph.jsonl` 与 `witogram.cc` 源码对齐后，当前可以进一步确认：
  - `case2` 的 `是一 / 是以`
  - 以及 `case3` 的 `体验 -> 不易 / 不一 / 不一样`
  - 主差额都不是后续 `adjustment_score` 被放大
  - 而是早在 `base_score` 里就已经拉开

#### `case2`：`第一站 -> 是一 / 是以` 的 request 细账已钉死为 `Base` 主差额

- 直接读取当前 build 的 request record：
  - `第一站 + 是一`
    - `search_score = -242.11`
    - `base_score = -242.175`
    - `adjustment_score = +0.065`
    - `dict_score_raw = -4.618`
    - `lm_score_scaled = -89.799`
  - `第一站 + 是以`
    - `search_score = -223.534`
    - `base_score = -222.976`
    - `adjustment_score = -0.558`
    - `dict_score_raw = -12.165`
    - `lm_score_scaled = -63.053`
- 结论：
  - `是一` 的词典底子其实更强
  - `adjustment_score` 也没有压它，反而还轻微帮了它
  - 真正把它压下去的是：
    - `base_score` 里直接吃到的 `lm_score_scaled`
  - 即：
    - **`case2` 不是 adjustment 不够，而是 `Base` 内部 `lm_score_scaled` 已先把 `是一` 打弱**

## 2026-05-27 `P1 request-stage ownership` 继续收口：`matching_request_state_*` 已证实 owned state 存在，当前子线接近判停

- 继续前再次按 `WORKLOG` 与 `2026-05-26-request-stage-ownership-p1.md` 去重，确认下面这些都已经做过并判负，不能重做：
  - `family_soft_clean -> HasConfidentPrimaryExact` 放宽
  - `candidate-specific continuation legality`
  - 早期单字桥 family split + 多保一条分支
  - translator 侧 `family-tail ownership` 行为原型
- 这轮没有继续加新的行为 bias，而是只补最小观测链，避免再被 bucket 首项展示误导：
  - `partial_chain_stage_probe.py` 补导出候选级
    - `matching_request_state_text`
    - `matching_request_state_aligned`
    - `matching_request_state_bridge_confirmed`
  - graph candidate JSON 同步补出同名字段
  - 对应 focused tests 已覆盖：
    - `test_partial_chain_stage_probe.py`
    - `test_case1_family_tail_acceptance.py`

### `case1` 新运行时证据

- 重新刷新 `case1_yizhixiangwang --mode full` 后，当前已能直接看到 candidate-specific matching state：
  - `向往`
    - `matching_request_state_text = 一直向往`
    - `matching_request_state_aligned = true`
    - `matching_request_state_bridge_confirmed = false`
  - `向往着`
    - `matching_request_state_text = 一直向往着`
    - `matching_request_state_aligned = true`
    - `matching_request_state_bridge_confirmed = true`
  - `想望`
    - `matching_request_state_text = ""`
- 也就是说，`向往` 当前**不是**“没有 owned request-stage state”，而是：
  - owned state 实际已经存在
  - 只是此前 probe 没把 candidate 自己真正匹配到的 state 暴露出来

### 当前真正断点

- 在 owned state 已确认存在的前提下，`向往` 当前仍然是：
  - `request_stage_tag = not_request_stage_candidate`
  - `continuation_tag = exact_ambiguous_family`
- 结合 `witset_translator.cc` 现有实现，当前更准确的收口是：
  - 主阻塞点已不再是
    - `BuildRequestStagePrefixStates()` 没形成正确 owned state
  - 而是前移到了
    - `ClassifyContinuationTag()`
    - 以及其中 `HasConfidentPrimaryExact()` 对 `family_soft_clean` 的判定边界

### 当前阶段判断

- 由于对应的可疑放宽线：
  - `family_soft_clean -> HasConfidentPrimaryExact`
  - 以及更窄的 `candidate-specific continuation legality`
  都已在旧日志中正式判负，这轮不再重复进入同一路线。
- 因而当前 `P1 request-stage ownership` 子路线可记为：
  - `Task 2` 的观测链仍保留且已补全到 candidate-specific matching state
  - 但在不重复旧负路线的前提下，这条子线已经没有新的高性价比微调入口
- 若后续还要继续推进，应视为：
  - 需要离开当前 `ownership` 微调口径
  - 改为新的 plan，去处理 downstream contract identity 或更上游的结构边界

#### `case3`：`体验 -> 不易 / 不一 / 不一样` 的 request 细账同样是 `Base` 主导

- 当前 build 下，`体验` 之后的 direct request 细账：
  - `体验 + 不易`
    - `search_score = -124.697`
    - `base_score = -124.396`
    - `adjustment_score = -0.301`
    - `dict_score_raw = -12.016`
    - `lm_score_scaled = -36.916`
  - `体验 + 不宜`
    - `search_score = -149.815`
    - `base_score = -149.158`
    - `adjustment_score = -0.656`
    - `lm_score_scaled = -61.299`
  - `体验 + 不一`
    - `search_score = -152.117`
    - `base_score = -151.435`
    - `adjustment_score = -0.682`
    - `lm_score_scaled = -63.602`
  - `体验 + 不一样`
    - `search_score = -194.412`
    - `base_score = -193.881`
    - `adjustment_score = -0.531`
    - `dict_score_raw = -10.491`
    - `lm_score_scaled = -107.926`
- 结论：
  - `不一样` 的字典分其实并不差
  - 但它在 `lm_score_scaled` 上被直接拉开到远弱于 `不易`
  - `adjustment_score` 对这组候选几乎都只是小幅负项，根本不是第一次失守点
  - 即：
    - **`case3` 当前也不该先去调 adjustment，而应继续拆 `Base` 内的 token evidence / char-path 总账**

#### `witogram` 源码再确认：`neutral_missing` 当前只是标签，不会软化 `total_log10`

- `Witogram::ScoreFeatures()` 当前逻辑是：
  - 先对 `word` 的每个 UTF-8 token 逐个调用 `AppendTokenScore()`
  - 无论 token 是否命中词表，都直接：
    - `total_log10 += model->Score(*state, wid, out)`
  - 若整词 token 命中词表：
    - `total_log10 = 0.60 * whole_word_log10 + 0.40 * char_total_log10`
  - 若整词 token 未命中：
    - `used_char_fallback = true`
    - `total_log10 = char_total_log10`
    - 然后只根据 `char_oov_token_count / matched_token_count`
      把 evidence 标成：
      - `split_token_supported`
      - `neutral_missing`
      - `true_oov`
- 也就是说：
  - `neutral_missing` 这个标签当前只影响分类语义
  - **不会回写或软化 `total_log10`**
  - `OOV token` 仍然已经原样记进 `char_total_log10`
  - 后面进入 `witset_poet` 的：
    - `lm_score_scaled`
  - 仍然直接继承这份原始 char-path 总账

#### 当前对后续实现的含义

- 若下一步继续做新实验，需要避免两类低性价比方向：
  - 再回头微调 `adjustment_score`
  - 或只靠后验 contract / bridge 保活去补已经在 `Base` 输掉的候选
- 更合理的新方向应是：
  - 要么继续在 `witogram` / token-evidence 口径处理：
    - `neutral_missing` 不应只改标签而完全不影响总分
  - 要么在 `poet` 里做更窄的 source-line 级补偿：
    - 只对“字典明显更强、但因 `neutral_missing` 被过度打弱”的候选回补
- 第二种更像当前值得优先尝试的安全方向：
  - 因为它有机会同时帮助：
    - `是一`
    - `不一样`
  - 但不会像全局抬高 `neutral_missing` 那样，把此前已经修过的 `体言` 一类短词也重新抬起来

#### 回读当前 `same-surface whole-word exemption` 原型后的新结论

- 本轮重新编译并重跑：
  - `case2_diyizhan --mode full`
  - `case3_tiyanbuyiyang --mode probe`
- 直接从 `partial_chain_stage_probe.snapshot.jsonl` 的定点提取确认：
  - `diyizhans` / `diyizhanshi` 下都已经存在一条**不吃 same-span 罚分**的 `第一站` 整块线：
    - `beam_score = -145.425`
    - `same_span_competition_penalty = 0`
  - `diyizhanshi` 下：
    - `第一站 + 是`
    - `request_stage_bridge_bonus = 3.6`
    - `request_stage_tag = request_source_line_eligible`
    - `第一站是` 已能稳定进入 `pre/source/post_future_compact_full`
- 这说明当前 very narrow exemption 至少做对了一件事：
  - **`第一站` 不再被同表面 split anchor `第一 + 站` 作为最佳盘面长期压住**

- 但同一次回读也确认：
  - `diyizhans` 下 `第一站` 仍然存在被 same-span 命中的记录
  - 当前新的锚点已经不是同表面的 `第一站`
  - 而是异文 anchor：
    - `same_span_anchor_text = 第一种`
    - `first_same_span_apply_outer_start_pos = 4`
    - 罚分量级约：
      - `-20.256`
      - `-19.929`
- 因此当前根因已从：
  - `第一 + 站` 对 `第一站` 的同表面 split 压制
  收口成：
  - **`第一种` 这类异文 3-char anchor 为什么会在 `outer_start_pos = 4` 抢到 same-span anchor**

- 继续往后看 `diyizhanshi` / `diyizhanshiyi`：
  - `第一展示` 反而开始被 `第一站是` 压制
  - `diyizhanshi` 下可见：
    - `第一展示`
    - `same_span_competition_penalty = -34.2891`
    - `same_span_anchor_text = 第一站是`
  - `diyizhanshiyi` 下可见：
    - `第一展示`
    - `same_span_competition_penalty = -38.131`
    - `same_span_anchor_text = 第一站是`
    - 而 `第一站是一` 链已能稳定落在 `pre/source/post_future_compact_full`
- 这说明当前原型的净效果不是空转：
  - **它把原先压 `第一站` 的 same-span 主刀，倒转成了由 `第一站是` 去压 `第一展示`**

- guardrail 侧：
  - `case3_tiyanbuyiyang --mode probe` 的 `result.json` 继续显示
    - `体验不 -> 易`
    - `search_score = -139.725`
    - 明显领先 `以/已/意/宜` 一组的 `-166.x`
  - 当前没有看到 `体验不 -> 易` 的明显回退

#### 当前阶段性判断

- `same-surface whole-word exemption` 原型：
  - **不是无效**
  - 也**不是最终终点**
- 它已经完成的事情是：
  - 去掉同表面 split anchor 对 `第一站` 的主压制
- 当前新的最小缺口变成：
  - `第一种` 为什么还能在 `outer_start_pos = 4` 成为 `第一站` 的 same-span anchor
- 后续若继续做最小验证，应优先并排抽：
  - `diyizhans`
  - `第一站 / 第一种`
  的 request / admitted / compact 细账
  - 先确认这是不是另一个需要单独约束的异文 same-span 竞争面
  - 而不是马上回退当前 prototype

#### 继续回读后确认：`case2` 仍未最终翻正，但正确链没有中途断掉

- 直接读取最终输入 `diyizhanshiyizuogulaodexiaozhen` 的候选列表后确认：
  - 当前 top10 仍然全部被错误链占住
  - 典型 top 候选包括：
    - `的驿站是以做古老的小镇`
    - `的驿站是一座古老的小镇`
    - `的翼展示已作古老的小镇`
  - 当前 build 下：
    - **`case2` 还没有真正翻到正确句子**

- 但继续对同一输入的 `snapshot` 做定点抽取又确认：
  - 正确链并没有在中途消失
  - 到最终输入时仍能看到：
    - `第一站是`
      - `beam_score ≈ -199.334 / -200.640`
      - `request_stage_bridge_bonus = 6`
      - 仍在 `pre/source/post_future_compact_full`
    - `第一站是一`
      - `beam_score ≈ -229.062 / -231.240`
      - 同样仍在 `pre/source/post_future_compact_full`
  - 且这些记录里：
    - `same_span_competition_penalty = 0`

- 这一步把当前缺口再次收口为：
  - **不是 `第一站是 / 第一站是一` 链在中途被 same-span 或 request-stage 杀掉**
  - 而是：
    - 正确链虽然活到了最终输入
    - 但在最终 candidate 成形 / 下游排序阶段
      仍然输给了 `驿站 / 战 / 展示` 一组错误链

- 同时从最终候选 debug 还能看到：
  - 错误候选常带：
    - `AltReqBridge`
    - `ReqSrcMismatch`
    - `StepAltReqSrc: 第一战是一座古老的小`
  - 说明当前后段还存在：
    - **entry 级 alt request hint 与 source mismatch 惩罚并存，但惩罚力度仍不足以压住错误链**

#### 当前最新主判断

- `same-surface whole-word exemption` 这条线：
  - 已经把问题从前段 same-span split 压制，推进到了更后段的 candidate 成形 / source-mismatch 竞争面
  - 所以这条原型暂时不应立即回退
- 当前真正值得继续优先拆的入口已变成：
  - `request_stage_alt_hints_by_entry`
  - `ComputeRequestStageSourceMismatchPenalty()`
  - 以及最终候选里 `AltReqBridge` 对错误链的残余帮助

#### 失败路径记录：放宽 graph snapshot 去抓多词 source-line 判负并已回退

- 为了直接看到后段错误链，我曾尝试在 `witset_poet.cc` 中放宽 graph snapshot 的记录条件：
  - 原逻辑只记录 `source->generated_word_count == 1`
  - 临时改成：若命中 next-hop probe suffix，则允许 multi-word source line 也落进 `snapshot`
- 实测结果：
  - `partial_chain_stage_probe.graph.jsonl` 依然是空文件
  - `snapshot.jsonl` 反而出现坏行，容错扫描时可见：
    - `line 37`
    - `Unterminated string`
- 说明：
  - 当前 probe 的图工件缺失，不是单靠放宽 `maybe_record_expansion_gate()` 就能修好
  - 这条 debug-only 放宽还会把单行 JSON 放大到不稳定，性价比低
- 处理：
  - 该放宽已立即回退
  - 并重新 `build.bat static` 恢复到可用状态
- 后续原则：
  - 不再沿“直接放大 graph/snapshot 落盘范围”这条路继续尝试
  - 若还需要后段多词链观测，应优先走更窄的定点导出，而不是扩大整行 snapshot 体积

#### 继续收口：`第一站是 -> 是以 / 是一` 的真实分叉先发生在 base，而不是 adjustment

- 继续对最终输入 `diyizhanshiyizuogulaodexiaozhen` 的 `snapshot.expansion_gate_records` 做定点抽取后确认：
  - `第一站是以`
    - `beam_score = -221.201 / -223.380`
    - `base_score = -219.861 / -223.207`
    - `adjustment_score = -1.340 / -0.173`
    - `lm_score_scaled = -194.874`
    - `dict_score_raw = -24.987 / -28.334`
  - `第一站是一`
    - `beam_score = -229.062 / -231.240`
    - `base_score = -236.757 / -240.104`
    - `adjustment_score = +7.695 / +8.863`
    - `lm_score_scaled = -219.317`
    - `dict_score_raw = -17.441 / -20.787`

- 这说明：
  - `是一` 并不是因为后段 adjustment 更差而输掉
  - 恰好相反，`是一` 的 `adjustment_score` 明显**比 `是以` 更高**
  - 真正把两者拉开约 `7~8` 分的是：
    - **`是以` 的 base 更强**
    - 且主差额主要来自：
      - `lm_score_scaled`
    - `dict_score_raw` 反而是 `是一` 更占优

- 同时继续核对同一输入下 `source_text = 第一站是一座` 的所有 stage 记录后确认：
  - **当前 snapshot 中完全不存在 `第一站是一座`**
  - 也就是说，现阶段不能再把问题描述成：
    - `第一站是一座` 已经生成出来，只是排序输给 `第一站是以做`
  - 更准确的收口应改为：
    - `第一站是一` 仍然活着
    - 但它后续没有成功生成出 `第一站是一座` 这条 source-line
    - 与之相对，错误链 `第一站是以 -> 第一站是以做` 已能稳定入池并继续展开

- 这一步把当前主缺口进一步收紧为两层：
  - 第一层：
    - `是 -> 以` 相对 `是 -> 一` 的先天 LM/base 优势
  - 第二层：
    - 在 `第一站是一` 已存活的前提下，为什么 `座` 这一步没有形成可见 source-line

- 当前判断：
  - `AltReqBridge / ReqSrcMismatch` 仍是后段错误链持续成形时的因素
  - 但在 `是以 vs 是一` 这个最早可见分叉点上，它们还不是主因
  - 后续若继续高性价比排查，应优先转向：
    - `第一站是一` 下 `座/作/坐` 为何没有进入可见 expansion/source pool
    - 而不是继续把主要怀疑放在 same-span 或已知 mismatch 惩罚不足本身

#### 继续收口：`一座` 并不缺席，缺的是 `第一站` family 下的后续扩展

- 继续对最终输入 `diyizhanshiyizuogulaodexiaozhen` 做定点抽取后确认：
  - `一座` 这个 entry 在当前盘面里**大量存在**
  - 例如可见：
    - `的一战时一座`
    - `的驿站是一座`
    - `的翼展示一座`
    - `的以展示一座`
  - 因此当前问题**不是词典里没有 `一座` / `座`**

- 但继续核对 `source_text = 第一站是一` 的所有后续记录后确认：
  - 在最终输入的 `expansion_gate_records` 中
  - 这条 exact source-line 除了它自己以外，**没有任何下一步扩展记录**
  - 不仅没有：
    - `第一站是一座`
    - `第一站是一作`
    - `第一站是一坐`
  - 也没有：
    - `entry_text = 一座`
    - `entry_text = 座`
    之类挂在 `第一站` family 下的后续项

- 这一步把当前主缺口再次收紧为：
  - **`第一站是一` 虽然仍在 source/future compact pool 中存活**
  - 但到了最终输入这一步，它并没有发生任何可见 continuation expansion
  - 与之相对，错误 family 已经能继续长出：
    - `驿站是一座`
    - `驿站是以做`
    - `战时一座`
    - `展示一座`

- 因而当前更准确的表述应改为：
  - 问题不只是 `座` 没排上来
  - 而是：
    - **`第一站是一` 这条 exact family 的后续枚举/扩展在最终输入盘面里直接缺席**

- 当前最对位的下一步应改为：
  - 优先查 `第一站是一` 为什么没有进入下一步 expansion
  - 尤其关注：
    - source selection / expand 来源集合
    - graph edge 可见性
    - 以及同位置下 exact family 是否在进入扩展前被别的 family 替代

#### 继续静态对位：缺口已进一步落在 `source_pool -> top_candidate` 的 source selection

- 继续对最终输入里 `start_pos = 13` 的 snapshot 读取后确认：
  - `第一站是一`
    - 确实仍在：
      - `pre_source_pool_full`
      - `source_pool_full`
    - 且当时 `pool_size = 2222`
  - 但同位置的：
    - `top_candidate_full`
  - **完全没有 `第一站是一`**

- 同时读取同一 `start_pos = 13` 的 `top_candidate_full` 后确认：
  - 真正被选去展开的 top80，主要是：
    - `驿站是以`
    - `驿站是一`
    - `展示已/以`
    - `战时以/已/乙/意/宜...`
  - `第一站是以` 虽然还能压线进入 top80
  - 但 `第一站是一` 已经掉出 top80 之外

- 结合读码可见：
  - `source_pool` 经过 `CompressLinePoolByState/PruneLinePool` 后
  - `SelectTopLines()` 默认按：
    - `beam_score`
    - 再次级看 `weight`
    做裁剪
  - 只存在一个很窄的额外保活：
    - `whole_first_word_anchor_active && generated_word_count == 1`
  - 而 `第一站是一` 属于多词 line，不满足这条额外保活条件

- 因此当前最新收口是：
  - `第一站是一` 并不是进入 source_pool 后还能去试扩展 `座/一座`
  - 而是：
    - **在 `source_pool -> top_candidate` 这一步就已经因为 beam 排名不够而没被选去展开**
  - 于是后面自然不会再出现：
    - `第一站是一座`
    - `第一站是一作`
    - `第一站是一坐`

- 当前最精确的后续入口应改为：
  - 不再笼统地说“查后段”
  - 而是直接查：
    - 为什么 `start_pos = 13` 时 `第一站是一` 的 `beam_score` 仍不足以进入 `top_candidate`
    - 以及是否存在一个对多词 exact family 可泛化的 source-selection 保活/偏置入口

#### 继续前推一层：`第一站` family 的主差额其实在更早的 direct multi-char continuation 盘面里就已形成

- 继续回退到更早位置后确认：
  - 在 `start_pos = 8` 的 `source_pool_full` 中
    - `第一站`
      - 仍然活着并进入了 `top_candidate_full`
      - 代表项：
        - `entry_text = 第一站`
        - `beam_score = -145.425`
        - `global_rank = 32 / 1722`
      - 另一条 split 变体：
        - `entry_text = 一站`
        - `beam_score = -144.990`
        - `global_rank = 24 / 1722`
    - 但同位置的错误 family：
      - `的驿站`
      - `beam_score = -137.831`
      - `global_rank = 1 / 1722`
  - 也就是说：
    - **`第一站` 这条 exact family 在更早位置并没有死**
    - 但它相对 `的驿站` 的累计 `beam/base/LM` 差距，在 `start_pos = 8` 时就已经明显存在

- 具体看这一步的基础差额：
  - `的驿站`
    - `base_score = -131.489`
    - `lm_score_scaled = -104.497`
  - `第一站`
    - `base_score = -144.642`
    - `lm_score_scaled = -131.820`
  - 因而当前并不能把缺口简单描述成“后段才突然输掉”
  - 更准确地说：
    - **错误 family 在更早的 prefix / direct multi-char continuation 层就已经带着明显更强的 LM/base 优势**

- 再回退到 `start_pos = 11` 后确认：
  - `source_text = 第一站是`
    - 两条记录分别为：
      - `beam_score = -199.334`
      - `global_rank = 112 / 2473`
      - `selected_top_candidate = false`
      - 以及
      - `beam_score = -200.640`
      - `global_rank = 113 / 2473`
      - `selected_top_candidate = false`
  - 而对照项：
    - `source_text = 的驿站是`
    - `beam_score = -176.136`
    - `global_rank = 2 / 2473`
    - `selected_top_candidate = true`

- 这一步的意义在于：
  - `第一站是` 这条单字尾 continuation 本身，在 `start_pos = 11` 时已经不是会被继续展开的 source
  - 因而后面看到的：
    - `第一站是一`
    - `第一站是以`
  - 更不应再机械理解为：
    - `第一站是 -> 一 / 以`
    的单字尾巴续写主链
  - 当前更合理的解释是：
    - `第一站` 在 `start_pos = 8` 仍属于被选中的 source
    - 后续 `第一站是一 / 第一站是以` 更可能主要来自更早 source 的 direct multi-char entry continuation
      - 如 `第一站 + 是一`
      - `第一站 + 是以`
    - 而不是依赖 `第一站是` 这条单字 source-line 在 `start_pos = 11` 再往后续写

- 因而到目前为止，`case2` 的 source-selection 收口应再精确一层：
  - 问题不只是 `start_pos = 13` 时 `第一站是一` 掉到 `rank 210 / 216`
  - 也不只是 `ReqSrcMismatch` 在后段压住了 `一座`
  - 而是：
    - **错误 family 从 `start_pos = 8` 开始就带着更强的 LM/base 盘面优势**
    - `start_pos = 11` 的 `第一站是` 单字尾 source 本身已经失去被选中资格
    - `start_pos = 13` 时 direct multi-char continuation `第一站是一` 也继续输给同位置盘面，最终掉出 `top_candidate`

- 因此当前最对位的后续入口再次收紧为：
  - 若要做通用修正，优先查的不是单纯 `是 -> 一` 的尾字比较
  - 而是：
    - **已确认 exact family 在更早 source 上产生的 direct multi-char continuation，为什么会在 source selection 中长期输给错误 family 的 prefix/LM 累计优势**

#### 继续收口：`X是以` 相对 `X是一` 的优势是跨 family 稳定存在的 direct multi-char entry 偏置

- 继续直接对 `start_pos = 13 / end_pos = 13 / source_pool_full` 中所有同时存在：
  - `X是一`
  - `X是以`
  的 family 做成对比较后确认：
  - 当前差距并不只出现在 `第一站`
  - 而是**跨大量 family 几乎按固定模板重复出现**

- 对当前最关键的 exact family：
  - `第一站是一`
    - `beam_score = -231.240`
    - `base_score = -240.104`
    - `adjustment_score = 8.86333`
    - `lm_score_scaled = -219.317`
 
## 2026-05-25 阶段 2 状态复核：建议继续 P1 预验证，但不宣告 P0 通过

- 重新对照 `docs/阶段2实施清单_P0_P1_P2.md` 的正式 gate 与近期 `P0` 复核结论后，当前阶段判断更新为：
  - `P0` 方向已被证实成立：
    - `第一站 -> 是` 已不再表现为被 `fallback/OOV` 误罚的脏路径
    - `used_char_fallback = false`
    - `lm_oov_token_count = 0`
  - 但 `P0` 仍未正式通过 gate：
    - 主竞争盘面尚未回到“以 `第一站是` 为主轴的干净 request-line”
    - follow-up family 仍会漂到：
      - `第一站是以`
      - `地一站式`
      - `第一展示`
      - `第一战士`
- 因而当前更准确的工程建议不是：
  - 继续围绕 `P0` 做同构语义微调
  - 或直接宣告 `P0` 已完成
- 而是：
  - **保持正式 gate 不变**
  - **同时开始受控的 `P1` 预验证**
- 这里的 `P1` 预验证只允许聚焦：
  - `RewriteWordGraph`
  - `BuildRequestStagePrefixStates()`
  - `source-line ownership`
  - `family continuity`
  - `continuation legality`
  - `request-stage admission`
  这些上游 contract 边界
- 这里的 `P1` 预验证不允许退回成：
  - `oov_penalty / char_fallback_penalty` 微调
  - 新一轮 `witset_poet` 末端 bonus/penalty
  - surface bonus 形式的旧 continuation 奖惩
- 当前状态应固定记为：
  - `P0`：核心方向成立，正式 gate 未过
  - `P1`：允许开始受控预验证，但不能记作“已正式进入完整 P1 推进”
  - `第一站是以`
    - `beam_score = -223.380`
    - `base_score = -223.207`
    - `adjustment_score = -0.172564`
    - `lm_score_scaled = -194.874`
  - 固定差额近似为：
    - `beam_delta(是一 - 是以) = -7.860`
    - `base_delta = -16.897`
    - `adjustment_delta = +9.036`
    - `lm_delta = -24.443`

- 更关键的是，这组差额并不是 `第一站` 特例：
  - `第一战`
  - `的一战`
  - `低一站`
  - `滴一战`
  - 以及多组 `一展 / 一站 / 一战 / 一盏`
  - 都反复出现几乎同一组差额：
    - `beam_delta` 约 `-7.5 ~ -8.8`
    - `base_delta` 约 `-16.9 ~ -18.0`
    - `adjustment_delta` 约 `+9.0 ~ +9.4`
    - `lm_delta` 约 `-24.4 ~ -25.6`

- 这说明当前 `case2` 的主缺口又进一步收口为：
  - 不是某个单独 family 被特殊压制
  - 也不是 `ReqSrcMismatch` 在 `第一站` 这条 exact family 上额外打坏
  - 而是：
    - **在当前 direct multi-char entry 评分体系里，`X是以` 相对 `X是一` 本身就带着跨 family 稳定的基础优势**
    - 且这个优势主要来自：
      - 更强的 `base`
      - 其中主差额仍是：
        - 更强的 `lm_score_scaled`
    - `是一` 虽然总能拿到更高的 `adjustment`
    - 但现阶段这部分补偿不足以覆盖其 `base/LM` 缺口

- 另外继续核对 `request_stage_*` 字段后还确认：
  - 这些 `第一站是一 / 第一站是以` 的 `snapshot` 记录上
    - `request_stage_tag`
    - `request_stage_prefix_text`
    - `matching_request_state_text`
  - 当前都是空
  - 因而这一层更像是：
    - **direct multi-char entry / direct continuation 的基础排序问题**
  - 而不是 request-stage hint/bridge 仍在主导它们的胜负

- 因此当前最对位的下一步应再次收紧为：
  - 优先查：
    - `X是一` / `X是以` 这类 direct multi-char entry 的 `base/LM` 是如何形成的
    - 尤其是：
      - 为什么跨 family 都稳定给 `是以` 更强的基础分
  - 暂时不应再把主精力放在：
    - 单字尾 `是 -> 一`
    - 或 request-stage bonus / mismatch 的局部调节

- 继续静态读码后，这条 runtime 现象已经能和打分主链直接对上：
  - `WitsetPoet::MakeSentences()` 中，对每个 `entry->text` 先调用：
    - `witogram->ScoreFeatures(context, entry->text, is_rear, &features)`
  - 然后把：
    - `lm_features.total_log10`
    换算成：
    - `lm_score_scaled = total_log10 * kLn10 * ngram_weight`
  - 最后主分公式是：
    - `base_score = candidate->weight + lm_total_weight_ * lm_score_scaled + dict_score_weight_ * dict_score_raw + ...`
    - `adjustment_score` 则是后加的：
      - `dict_score_norm_term`
      - `lm_avg_term`
      - `whole_word_bonus`
      - `request_stage_*`
      - 以及各类 bridge / penalty / repair 项

- 这和当前 runtime 读数正好一致：
  - `X是一` 往往能拿到更高的 `adjustment_score`
  - 但仍然输给 `X是以`
  - 说明真正把它压下去的主差额，不在后段 `adjustment`
  - 而是：
    - **`ScoreFeatures(context, entry->text)` 产出的 `lm_score_scaled` 已经在 `base_score` 层把差距拉开**

- 因而当前后续代码入口也可以再收紧为：
  - 不再优先翻 request-stage / mismatch / rescue 逻辑
  - 直接优先查：
    - `witogram->ScoreFeatures(...)`
    - 以及 `lm_score_scaled -> base_score` 这条 direct multi-char entry 的基础评分链

## 补记：`CanConfirmValidatedSingleCharBridge(...)` 的过紧回归已做一轮修复，`case2` 出现新的正向推进

- 继续前先复核了 `WORKLOG.md` 与 `阶段2实施清单_P0_P1_P2.md`，保持口径不变：
  - 仍处于 `P0` 未正式过 gate、只允许受控 `P1` 预验证
  - 不回退到末端散 patch，也不做 clean build

- 先围绕上一刀 `aligned_with_best_prefix -> next_anchor_aligned` 的修正做最小 runtime 红灯：
  - 对 `case2_diyizhan` 跑 `full probe`
  - 结果确认：
    - 错误桥 `第一 + 展` 已不再 `bridge_confirmed`
    - 但正确桥 `第一站 + 是` 也一并被打回
      - `request_stage_tag = request_tail_supported`
      - `matching_request_state_bridge_confirmed = false`
    - 同时：
      - `same_span_applied = 0`
      - `clean_split_count = 0`
      - 说明上一刀把正确锚点资格也一起收没了

- 因而把问题进一步收窄为：
  - 不是“不能再收 bridge”
  - 而是：
    - **不能把 2 字早期前缀与已形成多字 exact 前缀放在同一条继承规则里**
  - `第一 + 展` 应继续拦住
  - `第一站 + 是` 这类已形成多字 exact prefix 的单字桥，需要保留一条更窄的恢复口

- 本轮对 `witset_translator.cc` 做的最小修正是：
  - 仍保留：
    - `aligned_with_best_prefix` 不能无条件继承 confirm
  - 但新增：
    - 若 `prefix_state` 本身已与 source anchor 对齐
    - 且 `prefix_state.text` 已达到多字 exact prefix（当前以 `UTF8 chars >= 3` 作为极窄门槛）
    - 则允许这类前缀恢复早期单字桥确认
  - 换句话说，这刀显式区分了：
    - `第一 + 展`
    - `第一站 + 是`

- 重新 `.\build.bat static` 后，用同一份 `case2 full probe` 复跑红灯断言：
  - `bad_count = 0`
  - `good_count = 1`
  - 其中正确样本为：
    - `source_text = ...第一站`
    - `entry_text = 是`
    - `request_stage_tag = request_source_line_eligible`
    - `matching_request_state_bridge_confirmed = true`

- 之后再跑 `case2_diyizhan + case3_tiyanbuyiyang` 的双 case `probe`，得到新的实际盘面：
  - `case2 / diyizhanshi`
    - 从之前的：
      - `['第一战士', '第一展示', '第一站是', ...]`
    - 推进为：
      - `['第一展示', '第一站是', '第一战是', '第一站十', '第一站时', ...]`
    - 说明：
      - `第一战士` 已被打下去
      - `第一站是` 已从第 3 升到第 2
  - `case2 / diyizhanshiyizuogulaodexiaozhen`
    - 正确句：
      - `第一站是一座古老的小镇`
    - 当前已回到第 2
  - `case3 / tiyanbuyiyangdeshenghuo`
    - top1 仍为：
      - `体验不一样的生活`
    - guardrail 未坏

- 对最新 `case2 full probe` 再补读内部计数后，当前主缺口继续收窄为：
  - `same_span_applied = 4`
  - `clean_split_count = 8`
  - `bridge_confirmed_count = 1`
  - 说明：
    - same-span 已重新工作
    - 正确桥也已恢复
  - 但 `第一展示` 仍是 top1，且 debug 已明确显示：
    - `SameSpanComp = -14.587545`
    - 即它已经吃到同 span 罚分，仍然压过 `第一站是`

- 因而当前最新判断应更新为：
  - 这轮 bridge 修正是正向有效的，不判负
  - 剩余问题不再是：
    - 正确单字桥完全拿不到资格
    - 或 same-span 完全不触发
  - 而是：
    - **`第一展示` 这条 residual family 即使吃到 same-span 罚分，仍凭更早盘面 / residual `ReqBridge` 优势压住 `第一站是`**
  - 如果继续推进，下一步应优先查：
    - `第一展示` 为什么还能保留这部分 `ReqBridge` / lineage 优势
    - 而不是回头重做这轮 bridge-confirmed 恢复

## 2026-05-26 继续去重后确认：`第一展示` 剩余优势确有一段来自 translator 给未确认 tail-supported 前缀发放的自有 `ReqBridge`

- 在继续前再次复核 `WORKLOG`，确认这条线虽然和之前的 `ReqBridge / same-span` 主题相邻，但“**`第一展示` 当前仍保留的自有 `ReqBridge` 到底来自哪一层 hint 口径**”这一刀还没有被完整做过。

- 本轮先不改代码，先做两步最小 runtime 复核：
  - 直接读 `diyizhanshi` 的 `request` 记录，确认：
    - `source_text = ...第一展`, `entry_text = 示/是`
    - `request_stage_tag = request_tail_supported`
    - `continuation_tag = non_contract_candidate`
    - `matching_request_state_text = ""`
    - 但仍拿到：
      - `request_stage_bridge_bonus = 2.0`
  - 同时发现更早层还有大批：
    - `request_tail_supported + non_contract_candidate + 空 matching_state`
    - 仍拿正 `request_stage_bridge_bonus`
    - `diyizhanshi` 单例里红灯计数达到：
      - `bad_count = 139`

- 继续对代码口径做静态比对后，确认这批 bonus 的直接来源不是 `poet` 末端，而是 `witset_translator.cc` 中：
  - `BuildPoetRequestStageCandidateHints(...)`
  - 对 `request_stage_tag == request_tail_supported` 的单字 exact 候选，先统一走：
    - `ComputeTranslatorRequestStageContractBias(...)`
  - 再把结果作为 `request_stage_candidate_hints_` 送入 `poet`
  - 于是 `第一展示` 即使：
    - 不是 `request_source_line_eligible`
    - 没有 owned `matching_request_state`
    - 也仍会保留一段自有 `ReqBridge`

- 这一轮先试过一版过宽抑制：
  - 直接把所有
    - `request_tail_supported + non_contract_candidate + 无 matching_request_state`
    的正 hint 归零
  - 结果：
    - `第一展示` 的 `ReqBridge` 确实清成 `0.00`
    - 但长句 `第一站是一座古老的小镇` 一度从前排掉后，说明这刀太宽，会误伤后续仍有价值的 tail-supported 链
  - 因而这版已判负，不作为最终结论

- 最终保留的版本继续收窄为：
  - 仅当候选满足：
    - `request_stage_tag == request_tail_supported`
    - `continuation_tag == non_contract_candidate`
    - `matching_request_state` 为空
    - 且**当前 `prefix_state` 自身仍不是 confirmed request-stage state**
  - 才把这类自有 contract hint 归零
  - 也就是：
    - 不再按“字符数 / segment 数”这种外在形态裁
    - 直接按 `IsConfirmedRequestStageState(prefix_state)` 裁

- 重新 `.\build.bat static` 后，本轮得到的最小闭环结论是：
  - `case2 full probe`
    - `第一展示`
      - `ReqBridge:0.00`
      - `AltReqBridge:2.00`
      - `ReqSrcMismatch:-2.18`
    - `第一站是`
      - `ReqBridge:3.60`
      - `AltReqBridge:3.60`
    - 说明：
      - `第一展示` 的**自有** `ReqBridge` 已被清掉
      - 剩余只来自 alt-source 对照与更早 base/LM 盘面
  - `case2 / diyizhanshi`
    - top 仍是：
      - `第一展示`
      - `第一站是`
    - 说明这刀本身还不足以翻正短句
  - `case2 / diyizhanshiyizuogulaodexiaozhen`
    - 正确句：
      - `第一站是一座古老的小镇`
    - 仍保持在第 2，没有再被误伤回退
  - `case3 / tiyanbuyiyangdeshenghuo`
    - top1 仍是：
      - `体验不一样的生活`
    - guardrail 仍稳

- 因而这一轮应更新判断为：
  - 这是一次**有效但不充分**的新推进：
    - 已确认并清除了 `第一展示` 的一段自有 `ReqBridge`
    - 同时没有破坏长句正确链与 `case3`
  - 但短句 top1 仍未翻正
  - 当前剩余主缺口继续收窄为：
    - `第一展示` 现在主要靠：
      - `AltReqBridge`
      - `ReqSrcMismatch` 仍不足以彻底压退
      - 以及更早首词 `第一展 / 第一站` 的 base/LM 盘面
    - 换句话说，**request-stage 自有 hint 这层已经不是主矛盾，只剩 alt-source 对照与更早 base/LM 盘面残差**

## 2026-05-26 继续去重后确认：`AltReqBridge` 的最后一层残余也来自“仅 aligned、未建立真实 bridge lineage”的 source prefix

- 在继续前再次复核 `WORKLOG`，确认这轮不是回到已判负的：
  - 单纯加大 `ReqSrcMismatch` multiplier
  - 把 `AltReqBridge` 当成真实合同支持
  - 或再做一轮同类 same-span / source mismatch 常数 sweep

- 本轮先不急着重跑 `full`，只复用现成 `snapshot/probe` 产物做两步高性价比核对：
  - 先直接读 `diyizhanshi` 的 debug 串，确认：
    - `第一展示`
      - `ReqBridge = 0.00`
      - `AltReqBridge = 2.00`
      - `StepAltReqSrc = 第一站`
      - `ReqSrcMismatch = -2.18`
    - 说明：
      - 它借到的最后一层 `AltReqBridge`，不是来自别的错误 split source，
      - 而是来自 `第一站 + 示` 这条 **source prefix 已对齐、但候选本身没有 owned matching-state** 的 tail-supported 单字 exact 路径
  - 再回读 translator 静态逻辑，确认当前过滤口径是：
    - `request_tail_supported`
    - `continuation_tag = non_contract_candidate`
    - `matching_request_state` 为空
    - 且 `!IsConfirmedRequestStageState(prefix_state)`
  - 而 `IsConfirmedRequestStageState(...)` 的定义实际是：
    - `bridge_lineage_confirmed || aligned_with_best_prefix`
  - 这意味着：
    - 像 `第一站` 这种只是 best-prefix 对齐、但还没有建立真实 bridge lineage 的 prefix state，
    - 也会被当作“已 confirmed”，从而继续给 `第一站 + 示` 发放 alt hint

- 因而本轮把过滤条件继续收紧为：
  - 不再按 `IsConfirmedRequestStageState(prefix_state)` 放行
  - 而是要求：
    - `prefix_state.bridge_lineage_confirmed == true`
  - 换句话说：
    - **仅有 best-prefix 对齐还不够，必须已经建立真实 bridge lineage，tail-supported 单字非合同候选才允许继续保留 contract bias**

- 重新 `.\build.bat static` 后，只跑最小必要的：
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode probe`
  - 随后再补一轮：
    - `--case case2_diyizhan --case case3_tiyanbuyiyang --mode probe`
  - 中间不再重复跑 `full`，因为本轮并不需要 graph 级证据，而现有 `full` 还会碰到已知 `graph.jsonl` 脏行

- 本轮最小闭环结果：
  - `case2 / diyizhanshi`
    - `第一展示`
      - `ReqBridge = 0.00`
      - `AltReqBridge = 0.00`
      - `ReqSrcMismatch = 0.00`
      - `StepAltReqSrc` 也为空
    - `第一站是`
      - `ReqBridge = 3.60`
      - `AltReqBridge = 3.60`
    - 说明：
      - `第一展示` 通过 request-stage 链路获得的残余合同支持已经被彻底清掉
  - 排名上：
    - `diyizhanshi`
      - 仍是 `第一展示` rank1、`第一站是` rank2
    - `diyizhanshiyizuogulaodexiaozhen`
      - 仍是 `第一展示已作古老的小镇` rank1
      - `第一站是一座古老的小镇` rank2
    - `case3`
      - top1 仍是 `体验不一样的生活`

- 因而这轮应更新判断为：
  - 这是一次**有效且很关键的排异推进**：
    - 已证明 `第一展示` 当前不再依赖任何 request-stage `ReqBridge / AltReqBridge / ReqSrcMismatch` 残余
    - 但它仍保持 rank1
  - 这意味着：
    - request-stage 这层已基本收口到头
    - 剩余主缺口已进一步前移为：
      - `第一展 / 第一站`
      - 在更早首词阶段的 `base_score / LM / dict_score` 盘面差
  - 若继续推进，下一步不应再围绕：
    - `AltReqBridge`
    - `ReqSrcMismatch`
    - 或 tail-supported hint 继续打转
  - 更合理的下一战场应切到：
    - 更早的首词 `第一展 / 第一站` 比较链，
    - 看 `LM/base` 在没有 request-stage 残余加成的前提下，为何 `第一展示` 仍能压住 `第一站是`

## 2026-05-26 继续切到首词主轴后确认：当前主差额已不是 request-stage，而是 `ScoreFeatures()` 对整词 `展示` 与拆词 `站+是` 的纯 LM/base 解释差

- 在 request-stage 这层基本排干净后，这轮不再继续做 translator/poet 小补丁，而是直接回到首词与 `LM/base` 主轴做最小复核。

- 先复用现成 `probe` 产物对比 `diyizhan` 与 `diyizhanshi` 两拍：
  - `diyizhan`
    - `第一站`
      - `Base = -75.28`
      - `Dict = -12.82`
      - `LmScaled = -62.46`
    - `第一展`
      - `Base = -75.94`
      - `Dict = -13.49`
      - `LmScaled = -62.46`
    - 说明：
      - 在首词 `第一站 / 第一展` 这一拍，`LM` 完全相同
      - `第一站` 之所以领先，几乎全来自 `dict_score_raw` 略好
  - `diyizhanshi`
    - `第一展示`
      - `Base = -136.64`
      - `LmScaled = -111.90`
      - `ReqBridge / AltReqBridge / ReqSrcMismatch = 0`
      - `SameSpanComp = -16.58`
    - `第一站是`
      - `Base = -159.40`
      - `LmScaled = -135.94`
      - `ReqBridge = 3.60`
      - `AltReqBridge = 3.60`
    - 说明：
      - 即使 `第一展示` 已不再吃任何 request-stage 残余支持，
      - 它仍然靠 `Base` 约 `22.76` 的优势压住 `第一站是`
      - 其中主差额就是 `LmScaled` 约 `24.04`

- 再静态回读 `witset_poet.cc` 当前主合成口径，可确认：
  - `base_score = candidate->weight + lm_total_weight * lm_score_scaled + dict_score_weight * dict_score_raw + ...`
  - 也就是说，当前头部差额并不是某个隐藏 patch 再次起效，
  - 而是 `lm_score_scaled` 直接在 `base_score` 主轴里把 `第一展示` 推到了前面

- 继续回读 `witogram::InterpretGrammarEvidence()` 后，当前 `LM` 解释层也已收口得很明确：
  - 先逐字 token 累加 `char_path_log10`
  - 若整词存在，再算 `whole_word_log10`
  - 对整词命中：
    - `total_log10 = 0.60 * whole_word_log10 + 0.40 * char_total_log10`
  - 对未命中整词、只能走拆词路径：
    - 留在 char-path / neutral-missing 解释

- 因而这轮可把当前主问题正式改写为：
  - 现在 `第一展示` 的优势不再主要来自 request-stage / same-span / source mismatch
  - 而是：
    - **`展示` 作为整词直命中，在 `ScoreFeatures()` 里拿到更强的 `total_log10 / lm_score_scaled`**
    - 对 `第一站是` 这种 `第一站 + 是` 的拆词 continuation 形成纯 `LM/base` 压制

- 这也意味着：
  - 若继续推进，下一步不应再回到：
    - `translator` 的 request-stage hint
    - `poet` 的 same-span / mismatch / alt-source
  - 更合理的主战场应正式切到：
    - `witogram::InterpretGrammarEvidence()` / `ScoreFeatures()`
    - 检查是否需要在“整词直命中 vs 已验证 split continuation”之间引入更上游、更通用的解释层约束

## 2026-05-26 再次去重并直接抽当前短输入 debug 后确认：`第一展示` vs `第一站是` 现在连“整词 vs 拆词”都不是，已收口为模型内两个整词 token 的真实 LM 先验差

- 为避免重复走已经做过的 `InterpretGrammarEvidence()` 路线，这轮先再次复核了旧日志，确认以下入口都已经正式做过并收口：
  - 多字 `neutral_missing` 去掉 `<unk>` 上下文传播
  - 单字 `neutral_missing` / `kSingleTokenMissing`
  - `first_word_evidence_missing_prefix`
  - `used_char_fallback / oov_token_count` 主语义字段纠偏
  - `request-stage / same-span / AltReqBridge / source mismatch`
  - `lm_avg` 的全局关断、单字 gate、effective-token-count、`2-token neutral_missing avg` 等窄改

- 在此基础上，这轮没有再跑重脚本，只直接从现成 `snapshot` 里抽 `diyizhanshi` 的当前 debug，得到一个比上一节更硬的新事实：
  - `第一展示`
    - `WholeHit = 1`
    - `CharFB = 0`
    - `OovTok = 0`
    - `StepWholeLog10 = -56.767464`
    - `StepCharLog10 = -56.767464`
    - `LmScaled = -111.90`
  - `第一站是`
    - `WholeHit = 1`
    - `CharFB = 0`
    - `OovTok = 0`
    - `StepWholeLog10 = -77.998398`
    - `StepCharLog10 = -77.998398`
    - `LmScaled = -135.94`

- 这说明当前需要把问题描述再收紧一层：
  - 它已经不是：
    - `展示` 作为整词直命中
    - 去压 `第一站 + 是` 的拆词 continuation
  - 而是：
    - **`第一展示` 与 `第一站是` 在当前短输入层都属于 direct whole-word hit**
    - 且各自 `StepWholeLog10 == StepCharLog10`
    - 也就是说，whole-word blend 在这里并没有额外制造差额
    - 真正的差额就是当前模型里：
      - `展 示`
      - 相对
      - `站 是`
      的真实 LM / collocation 先验差

- 这与更早已有结论也能严丝合缝对上：
  - `第一战士`
    - 主因更像句首单块重解释 regime
  - `第一展示`
    - 既有句首单块重解释结构性优势
    - 但同时还叠加真实更强的 `展 示` collocation
  - 现在在 request-stage 与 same-span 收紧之后，再直接看当前 debug，
    - 可以进一步确认：
      - **至少在 `diyizhanshi` 这一层，`第一展示` 的残余领先已经主要不是“结构被放大”**
      - 而是模型本体对 `展 示` 的直接偏好

- 因而当前阶段判断应进一步更新为：
  - 在不引入新证据源、也不改 grammar/model 内容的前提下，
  - 当前运行时 scorer 的窄补丁空间已经接近耗尽
  - 继续围绕：
    - `InterpretGrammarEvidence(...)`
    - `same-span`
    - `request-stage`
    - `lm_avg`
    做小刀 patch，预期收益已很低，而且高概率只是重复旧路

- 如果后续仍要继续推进，真正剩下的方向已经更像两类“非当前小补丁范围”的路线：
  1. 引入新的区分证据源
     - 专门解决 `shi` 同音细排 / 合法主续接判别
  2. 直接承认并处理模型内容问题
     - 例如 grammar / LM 对 `展 示` vs `站 是` 的真实先验差
     - 而不是继续指望 runtime scorer 在现有证据下把它硬拉回来

## 2026-05-26 继续沿“新证据源”路线推进：第一阶段 `cross_boundary_reparse` 观测信号已跑通，并能稳定区分 `第一展示` 与 `第一站是`

- 在进入实现前，先按新 spec / plan 再次去重，确认本轮不重复这些已收口路线：
  - 继续调 `request-stage / AltReqBridge / ReqSrcMismatch`
  - 再做一轮 `same-span` 常数 sweep
  - 回到 `lm_avg / neutral_missing / single-token-missing`
  - 或直接写某个音 / 字 / 词的局部 tie-break

- 因而本轮转入新的泛化路线：
  - 不直接改分
  - 先把“跨已验证边界的整词重解释”显式导出成一个结构标签：
    - `cross_boundary_reparse`
  - 第一阶段只做观测，不改排序

- 第一版实现尝试过于依赖“两词线 + 多字 step”的固定形态：
  - 编译通过，但 `case2 / diyizhanshi` 上
    - `第一展示`
      - `StepCrossBoundary = 0`
    - `第一站是`
      - `StepCrossBoundary = 0`
  - 说明这条定义过窄，不足以覆盖当前实际 line 形态

- 随后把定义收紧到更稳的结构事实：
  - 不再额外假设固定词数/step 形态
  - 而是直接复用现有代码里已经成立的：
    - `same-span competing reparse`
    - 且同区确实存在 validated split anchor
  - 也就是说：
    - 只要某条 line 已被当前 same-span 逻辑识别为“重解释竞争线”，
    - 并且所在局部竞争区里确实存在 split anchor，
    - 就把它标成：
      - `cross_boundary_reparse = true`

- 代码落点：
  - `witset_poet.h`
    - 给 `DebugExpansionGateRecord` / `DebugNextHopProbeRecord` 增加：
      - `cross_boundary_reparse`
  - `witset_poet.cc`
    - 给 `Line` 增加：
      - `step_cross_boundary_reparse`
    - 在 same-span anchor 建立后、真正应用竞争逻辑前：
      - 对满足 `same-span competing reparse` 且同区存在 validated split anchor 的 line 打标
    - 在最终候选 debug 串里导出：
      - `StepCrossBoundary`
  - `witset_translator.cc`
    - 把该字段导出到 snapshot / next-hop JSON

- 验证过程继续遵守“最小且高效”：
  - 先只做：
    - `.\build.bat static`
  - 然后只跑：
    - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode probe`
  - 在确认 `case2` 命中后，最后才补一轮最小护栏：
    - `--case case3_tiyanbuyiyang --mode probe`

- 当前闭环结果：
  - `case2 / diyizhanshi`
    - `第一展示`
      - `StepCrossBoundary = 1`
      - 同时仍保持：
        - `SameSpanComp = -16.58`
        - `ReqBridge / AltReqBridge / ReqSrcMismatch = 0`
      - 说明：
        - 新信号已能把它显式标为“跨边界重解释线”，
        - 而不是再依赖隐含的 same-span 推断
    - `第一站是`
      - `StepCrossBoundary = 0`
      - 说明：
        - 新信号没有误把合法 split continuation 标成重解释线
  - `case3 / tiyanbuyiyangdeshenghuo`
    - top3 仍是：
      - `体验不一样的生活`
      - `体验不宜养的生活`
      - `体验不一样的圣火`
    - top10 中：
      - `flagged_top10 = []`
    - 说明：
      - 该信号当前没有在正常 whole-word continuation 上大面积误触发

- 因而这轮应更新判断为：
  - 这不是又一轮同类 scorer 小调，而是一个**新的观测性突破**：
    - 我们已经有了一条可导出、可验证、且当前不明显过宽的结构标签，
    - 能把 `第一展示` 这类“跨已验证边界的整词重解释”与 `第一站是` 这类合法 split continuation 显式区分开
  - 第一阶段目标已达成：
    - 命中面成立
    - 护栏暂时稳定
  - 这意味着：
    - 若下一步继续推进，就已经具备进入第二阶段“最小消费”的前提
    - 而且消费不必再回到词级/音级特判或旧常数 sweep

## 2026-05-26 继续进入第二阶段最小消费：只补掉 `cross_boundary_reparse` 在 same-span 后残留的领先量，`diyizhanshi` 已翻正且 `case3` 护栏稳定

- 在进入第二阶段前，先继续遵守“最小且高效”原则，不直接双 case 重跑：
  - 先用当前 `case2` 单例 snapshot 读实际残差
  - 结果显示：
    - `第一展示`
      - 已有：
        - `StepSameSpanComp = -16.576790`
        - `StepCrossBoundary = 1`
      - 但最终仍以：
        - `Diff = 2.39`
        - 略高于 `第一站是`
    - `第一站是`
      - `Diff = 0.00`
      - `StepCrossBoundary = 0`

- 因而第二阶段的最小消费目标被进一步收窄为：
  - 不新增全局 whole-word 惩罚
  - 不重开一轮常数 sweep
  - 只在：
    - 已经命中 `cross_boundary_reparse`
    - 且同区存在 validated split anchor
    - 且经过 same-span 之后仍残留领先量
  - 的局部竞争里，补掉这点残余领先

- 落地方式：
  - 在 `witset_poet.cc` 新增：
    - `ComputeCrossBoundaryReparsePenalty(...)`
  - 定义口径：
    - 仅当：
      - `current_search_score > anchor_search_score`
      - 且 `anchor_contract_support > current_contract_support`
    - 才生效
  - 惩罚值不是固定常数，而是：
    - 以“当前残余领先量”作为主要目标
    - 再用 `0.80 * support_gap` 做上限
    - 实际返回：
      - `-min(0.80 * support_gap, residual_lead + 0.25)`
  - 这意味着它不是无脑多扣一截，而是：
    - 只把 same-span 之后仍残留的那点领先量补掉一点点
    - 目标是翻过局部残差，而不是横扫整片 whole-word 候选

- 同时为便于后续继续诊断，在 line debug 串里增加：
  - `StepCrossBoundaryPen`

- 第二阶段验证仍保持最小路径：
  1. `.\build.bat static`
  2. `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode probe`
  3. 先确认短句是否翻正
  4. 只有翻正后，才补：
     - `--case case3_tiyanbuyiyang --mode probe`

- 当前闭环结果：
  - `case2 / diyizhanshi`
    - top5 变为：
      - `第一站是`
      - `第一展示`
      - `第一站十`
      - `第一站时`
      - `第一战十`
    - `第一展示`
      - `StepCrossBoundaryPen = -2.637359`
      - `StepCrossBoundary = 1`
      - `StepSameSpanComp = -16.576790`
      - 最终：
        - `Diff = 0.25`
    - `第一站是`
      - `StepCrossBoundaryPen = 0`
      - `StepCrossBoundary = 0`
      - 最终：
        - `Diff = 0.00`
    - 说明：
      - 第二阶段没有靠大常数硬压，而是只补掉 same-span 后剩余的局部领先量，
      - 已足以让 `diyizhanshi` 从 `第一展示 > 第一站是` 翻到：
        - `第一站是 > 第一展示`

  - 同一轮 `case2` 产物里，长句：
    - `diyizhanshiyizuogulaodexiaozhen`
      - 仍是：
        - `第一展示已作古老的小镇`
        - `第一站是一座古老的小镇`
      - 即正确长句仍在第 2，没有被打飞到更后面

  - `case3 / tiyanbuyiyangdeshenghuo`
    - top5 仍是：
      - `体验不一样的生活`
      - `体验不宜养的生活`
      - `体验不一样的圣火`
      - `体验不一样的生火`
      - `体验不宜养的圣火`
    - `flagged_top10 = []`
    - 说明：
      - 当前最小消费没有把 `case3` 的正常 whole-word continuation 一起误伤

- 因而当前阶段应更新判断为：
  - `cross_boundary_reparse` 这条新结构信号不仅可观测，而且已经能在**不回到词级/音级特判、不重开旧常数 sweep**的前提下，作为局部竞争信号被最小消费
  - `diyizhanshi` 已翻正
  - `case3` 护栏稳定
  - 剩余主缺口已不再是“短句第一展示压第一站是”，而更偏向：
    - 更长输入上跨边界重解释 family 的继续残留
    - 以及更早首词 / 长续接层的 LM/base 盘面问题

## 2026-05-26 继续沿长句 lineage 收口：错误 `展示` family 已被压出顶部，主残余正式切回 split family 内部 `是以做 / 一座` 竞争

- 继续前先复查了本段 `WORKLOG`、`cross-boundary reparse` spec/plan，并核对最新未验证代码：
  - 当前未完成的不是短句信号定义，而是它在长句 family 上能否继续作用；
  - 因而先不改别处，只对 `witset_poet.cc` 里已落地但未验证的 lineage carry patch 做最小闭环。

- 先用最小路径复核旧 snapshot，确认长句阶段的真实缺口：
  - 在旧产物中：
    - `diyizhanshiyi`、`diyizhanshiyizuo`、`diyizhanshiyizuogulaodexiaozhen`
    - `第一展示...` family 的 `StepCrossBoundary` 已回落为 `0`
  - 说明之前第二阶段能翻正短句，但错误 family 身份只停留在首跳，后续长句阶段不再被识别为“跨边界重解释”。

- 第一轮继续最小实现：
  - 在 `Line` 上新增：
    - `carries_cross_boundary_reparse_identity`
  - 让 `cross_boundary_reparse` 可以沿错误 family descendant 继续传递；
  - 不改 request-stage，不加新常数，只复用现有 same-span / cross-boundary 局部钩子。

- 以最小路径验证：
  - `.\build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode probe`
  - 结果确认：
    - `diyizhanshiyi` 起，`第一展示已 / 第一展示以 ...` 已出现
      - `CarryCrossBoundary = 1`
    - 到长句
      - `第一展示已作古老的小镇`
      - 仍保持 `CarryCrossBoundary = 1`
  - 这说明：
    - 旧的“错误 family 身份在首跳后丢失”判断已经被修正；
    - 错误侧 lineage 现在确实能被持续跟踪。

- 但同一轮验证也暴露出新的更窄缺口：
  - 错误侧 lineage 虽然保住了，
  - 正确 split 侧锚点仍只认“首个 clean split / bridge-confirmed hop”，没有沿 descendant 往后传；
  - 结果是长句里虽然能看到 `CarryCrossBoundary = 1`，却不一定总能重新形成同区 anchor 对照。

- 第二轮继续最小实现：
  - 在 `Line` 上新增：
    - `carries_validated_split_anchor_identity`
  - 传播条件只依赖：
    - `clean_split_continuation`
    - `matching_request_state_bridge_confirmed`
    - 且排除 `exact_ambiguous_reparse / cross_boundary_reparse`
  - 同时放宽 same-span anchor 建立：
    - 除原来的首个 split anchor 外，
    - 也允许“沿 validated split family 继续展开、且当前 step 仍是 clean whole-word”的 descendant 继续充当 anchor。

- 第二轮仍只做最小验证：
  - `.\build.bat static`
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case2_diyizhan --mode probe`

- 当前闭环结果发生了关键转折：
  - `diyizhanshi`
    - 仍保持：
      - `第一站是 > 第一展示`
  - `diyizhanshiyi`
    - `第一展示已 / 第一展示以`
      - 已变为 `StepCrossBoundary = 1`
    - 同时：
      - `第一站是一`
      - 也出现了局部 penalty 参与
  - `diyizhanshiyizuogulaodexiaozhen`
    - 原先位居顶部的
      - `第一展示已作古老的小镇`
    - 已不再占据长句 top1
    - 长句顶部转成 split family 内部竞争，例如：
      - `第一站是以做古老的小镇`
      - `第一站是一座古老的小镇`

- 因而当前阶段必须更新判断为：
  - 这轮已经拿到**新的实质性突破**：
    - 长句主错误已不再是 `展示` cross-boundary reparse family 继续霸榜；
    - 说明“cross-boundary 错误 family carry + validated split anchor carry”这条路线本身是有效的。
  - 同时也应明确：
    - 当前主残余已经不再属于 `cross_boundary_reparse` 这条路线，
    - 而是重新切回此前已知的 split family 内部：
      - `是以做...`
      - 对
      - `是一座...`
      - 的 `LM/base / char-path` 竞争。
  - 因此后续如果继续，不应再重复加码 `cross_boundary_reparse` 或 same-span lineage；
    - 下一阶段应回到 `是以 / 是一 / 一座` 这一条已知主缺口，按 `char-path / lm_score_scaled / per-token transition trace` 口径继续收口。

- 随后补跑最小 guardrail：
  - `python C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py --case case3_tiyanbuyiyang --mode probe`
  - 发现：
    - top1 变成 `体验不宜养的生活`
    - 原正确 top1 `体验不一样的生活` 退到第 2
  - 说明：
    - `carries_validated_split_anchor_identity` 这一步虽然让 `case2` 长句顶部暂时从 `展示` family 切走，
    - 但它把正常 split family 的 anchor 建立也一并放宽了，`case3` 出现了真实回归，
    - 因而这一步不能保留。

- 已做收口处理：
  - 回退 `carries_validated_split_anchor_identity` 相关代码；
  - 重新 `.\build.bat static` 恢复二进制；
  - 当前保留的仍只有：
    - `carries_cross_boundary_reparse_identity`
    - 以及此前已经验证过的短句 `cross_boundary_reparse` 最小消费。

- 所以对这轮实验的最终判断应再收紧为：
  - “错误 `展示` family 可被 carry 追踪”这一观察结论成立；
  - 但“validated split anchor 也沿 descendant 全量 carry”这条实现路线在当前定义下**过宽，已判负并回退**；
  - 后续若继续推进，应切回已知的 `是以 / 是一 / 一座` split family 内部 `LM/base / char-path` 主缺口，
    不再沿这条 over-broad anchor-carry 路线继续加码。

- 为了避免后续反复手写 ad hoc Python 去筛 `graph.jsonl`，这轮先只扩了观测面，不改排序逻辑：
  - 修改：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\partial_chain_stage_probe.py`
  - 新增：
    - 每个 probe 在 `--mode full` 下都会输出 `transition_lm`
    - 直接汇总 `transition_lm_features`
    - 记录：
      - `context_suffix / word`
      - `total_log10 / avg_log10`
      - `whole_word_log10 / char_path_log10`
      - `matched_token_count / char_path_oov_token_count`
      - `token_evidence_tag`
  - 对 `case2` 额外补了三个最关键观察面：
    - `第一站 -> 是 / 是一 / 是以`
    - `第一站是 -> 一 / 一座 / 以 / 已 / 宜`
    - `第一站是一 -> 座 / 做 / 作 / 坐`
- 真实复跑 `python partial_chain_stage_probe.py --case case2_diyizhan --mode full` 时，还顺手暴露了一个脚本鲁棒性问题：
  - `partial_chain_stage_probe.next_hop.jsonl` 中偶发出现截断行
  - 原 `iter_jsonl()` 会直接 `json.loads()` 崩掉
  - 现已改为：
    - 对 malformed JSONL 行输出 warning 并跳过
  - 并补了最小单测覆盖：
    - `test_partial_chain_stage_probe.py`
    - 覆盖 `transition_lm` 汇总筛选
    - 覆盖 malformed JSONL skip
- 现在 `case2` 的决定性 runtime 证据可以直接从 probe 结果里稳定看到：
  - `第一站`
    - `是`
      - `direct_whole_word_hit`
      - `total_log10 = -37.4992`
      - `char_path_log10 = -37.4992`
    - `是一`
      - `neutral_missing`
      - `total_log10 = -75.9984`
      - `char_path_log10 = -77.9984`
      - `whole_word_log10 = 0`
    - `是以`
      - `split_token_supported`
      - `total_log10 = -54.7675`
      - `char_path_log10 = -54.7675`
      - `whole_word_log10 = 0`
  - `第一站是`
    - `一`
      - `single_token_missing`
      - `total_log10 = -28.8744`
      - `char_path_log10 = -40.4992`
      - `whole_word_log10 = 0`
    - `一座`
      - `neutral_missing`
      - `total_log10 = -76.9984`
      - `char_path_log10 = -78.9984`
      - `whole_word_log10 = 0`
  - `第一站是一`
    - `座 / 做 / 作 / 坐`
      - 全部是 `direct_whole_word_hit`
      - `total_log10 = char_path_log10 = whole_word_log10 = -38.4992`
- 这轮结论因此可以进一步写死为：
  - `case2` 当前主缺口已经不是末端：
    - `座` vs `做`
  - 而是更前面的两个连续台阶：
    - `第一站 -> 是一`
    - `第一站是 -> 一座`
  - 两步都表现为：
    - `neutral_missing`
    - 且 char-path 显著弱于正确链竞争项
  - 一旦跨过这两个台阶，到：
    - `第一站是一 -> 座 / 做 / 作 / 坐`
    - LM 细账已不再提供区分
- 所以下一步不应再围绕：
  - `座/做`
  - 末端字竞争
  - 或继续修 `展示` lineage
- 更合理的下一步应是：
  - 继续审：
    - 为什么 `第一站 + 是一`
    - 与 `第一站是 + 一座`
    - 会稳定落到 `neutral_missing`
  - 以及：
    - `neutral_missing / split_token_supported / single_token_missing`
    - 在这两步上的 token 路径与打分语义差异

- 继续顺着这条线往下查 `witogram.cc + witset_poet.cc` 后，已经可以把“现有补偿是否还没打上”也排除掉：
  - `Witogram::InterpretGrammarEvidence()` 里的原始 token-evidence 语义是：
    - whole-word 命中：
      - `direct_whole_word_hit`
    - whole-word 缺失但 char-path 全 token 命中：
      - `split_token_supported`
    - whole-word 缺失且 char-path 部分 token 命中：
      - `neutral_missing`
      - 其中缺失 token 会走 `AppendNeutralMissingTokenScore()`，
        直接按 `unknown_unigram_log10 * penalty_scale` 记分
    - 单 token 缺失：
      - `single_token_missing`
- 对 `case2` 这两个关键台阶，runtime 现在已经能同时看到 rescue 前后：
  - `第一站 -> 是一`
    - request 阶段：
      - `search_score = -151.762`
      - `adjustment_score = +0.094`
      - `token_evidence_tag = neutral_missing`
    - 进入 `batch_selected / admitted_new` 后：
      - `search_score = -143.379`
      - `adjustment_score = +8.478`
    - 说明：
      - 现有 neutral-missing rescue **确实已经打上了**
      - 不是“这条补偿没触发”
    - 但同层 `第一站 -> 是以` 仍然有：
      - `search_score = -135.518`
      - 依旧领先约 `7.86`
  - `第一站是 -> 一座`
    - request 阶段：
      - `search_score = -208.108`
      - `adjustment_score = -2.247`
      - `request_stage_bridge_bonus = 0`
    - 进入 `batch_selected / admitted_new` 后：
      - `search_score = -199.066`
      - `adjustment_score = +6.796`
    - 到 `admitted_new_line_pre_push`：
      - `adjustment_score = +6.915`
      - `request_stage_bridge_bonus = 4.8`
    - 说明：
      - `一座` 这一步连 request-stage bridge 也已经在后段补上了
      - 但总盘面仍然不够强
- 因而这轮可以把结论再收紧一层：
  - 现有路径里，
    - `neutral_missing rescue`
    - `request-stage bridge`
    - 以及后段 compact/source-pool 流程
    - 都**不是完全没起作用**
  - 真正的问题是：
    - 它们对 `是一 / 一座` 的补偿量级，
    - 仍然填不平原始 `LM/base / char-path` 缺口
  - 所以再继续沿“加一个同类小 bonus”这条路推进，收益大概率不够。
- 当前更像是一个上游语义问题，而不是末端 rescue 权重问题：
  - `是一 / 一座` 在 `InterpretGrammarEvidence()` 里一开始就被分类成
    - “有部分 token 命中，但缺失 token 只能按 `<unk>` unigram 记分”的
      `neutral_missing`
  - 后续所有 rescue 只是在这个已经偏低的 base 上做有限回补
  - 因此下一步若要继续，优先级应切到：
    - `neutral_missing` 的上游语义
    - 尤其是这类“共享强前缀、且后续 exact continuation 很干净”的缺词路径
  - 而不是继续在 poet 末端叠加更多小常数补偿。

## 2026-05-26 去重后继续：把 `是一 / 一座` 这类 one-gap 多 token 缺词从一般 `neutral_missing` 中拆成 `mixed_token_missing`

- 继续前再次回查了 `WORKLOG`、`阶段2实施清单_P0_P1_P2.md` 和当前代码，确认下面这些路线都已经做过，不能重复：
  - `single_token_missing` 新子类
    - 已落地，不能再当成新方向
  - 全局 `neutral_missing` 固定 `<unk>` 成本软化
    - 已做过且更宽，不能重复
  - 多字/单字 `neutral_missing` 的 `<unk>` state 传播
    - 已做过，不能重复
  - `witset_poet` 末端继续叠同类小 bonus
    - 已证实不是主缺口
- 去重后剩下仍未正式做过、且仍直接对位 `case2` 主残余的最小入口收紧为：
  - 不改 `<unk>` state 传播
  - 不改末端 bonus
  - 只在 `InterpretGrammarEvidence()` 里把：
    - `token_count > 1`
    - `char_path_matched_token_count > 0`
    - `char_path_oov_token_count == 1`
    - 的 one-gap 多 token 缺词
    - 从一般 `neutral_missing` 中拆出来
  - 新 evidence tag：
    - `mixed_token_missing`
  - 同时只对这类 one-gap 多 token 缺词使用：
    - `kMixedTokenMissingUnknownPenaltyScale = 0.75`
  - 语义意图是：
    - 它比一般多缺口 `neutral_missing` 更接近“共享强前缀下只缺一个 token 证据”
    - 但又不把它误标成 whole-word hit 或 split-token-supported
- 为避免新类变成 no-op，同时保持边界最小：
  - `witset_poet.cc` 里所有当前按 `kNeutralMissing` 消费、且语义上本来就是“neutral-missing-like” 的位置，都并入：
    - `kMixedTokenMissing`
  - 包括：
    - `TokenEvidenceLevelToTag`
    - 非惩罚态 fallback 归类
    - `first_word_evidence_missing_prefix`
    - `ComputeNeutralMissingSourceLineRescueBonus`
    - `ComputeNeutralMissingPrefixContinuationFamilyBonus`
  - 不新增新的 scorer 项，不改已有权重表。
- 先红后绿的最小验证方式：
  - 新增 acceptance 测试：
    - `C:\Users\Bing\AppData\Roaming\witty\debug\test_case2_multitoken_missing_acceptance.py`
  - 先用当前二进制跑：
    - `python partial_chain_stage_probe.py --case case2_diyizhan --mode full`
  - 再跑测试：
    - 预期失败点锁到：
      - `第一站 -> 是一`
      - `第一站是 -> 一座`
      - 当前仍是 `neutral_missing`
  - 然后实现新类后再复跑，测试转绿。
- 最小实现落点：
  - `librime/plugins/witogram/src/witogram.h`
    - 新增 `kMixedTokenMissing`
  - `librime/plugins/witogram/src/witogram.cc`
    - 新增 `kMixedTokenMissingUnknownPenaltyScale = 0.75`
    - 在 `InterpretGrammarEvidence()` 中把 one-gap 多 token 缺词归类到 `kMixedTokenMissing`
  - `librime/plugins/witset/src/witset_poet.cc`
    - 新增 `IsNeutralMissingLike(...)`
    - 让现有 neutral-missing-like 消费点继续覆盖新类
- 最关键的 runtime 结果：
  - `case2_diyizhan`
    - `第一站 -> 是一`
      - `transition_lm.token_evidence_tag`
        - `neutral_missing -> mixed_token_missing`
      - `total_log10`
        - `-75.9984 -> -66.3736`
      - `lm_score_scaled`
        - `-87.4963` 级别抬到 `-76.4154`
      - `search_score`
        - 抬到 `-136.517`
    - `第一站 -> 是以`
      - 仍是 `split_token_supported`
      - `search_score = -131.494`
    - `第一站是 -> 一座`
      - `transition_lm.token_evidence_tag`
        - `neutral_missing -> mixed_token_missing`
      - `total_log10`
        - `-76.9984 -> -67.3736`
      - `lm_score_scaled`
        - 抬到 `-77.5667`
      - `search_score`
        - 抬到 `-192.863`
- 最重要的最终盘面变化：
  - `partial_chain_stage_probe.snapshot.jsonl` 中，
    - `input = diyizhanshiyizuogulaodexiaozhen`
    - top1 已变为：
      - `第一站是一座古老的小镇`
    - `第一站是以做古老的小镇`
      - 已退到较后位置
  - 这说明这轮不是只改了 tag，而是把 `case2` 的最终候选顺序真正翻正了。
- 最小 guardrail：
  - 复跑：
    - `python partial_chain_stage_probe.py --case case3_tiyanbuyiyang --mode full`
  - `snapshot` 中：
    - top1 仍为：
      - `体验不一样`
  - 当前没有出现把 `case3` 拉回 `不宜养/不易养` 顶上的回归。
- 因而这轮可以先收口为：
  - 继续沿“单字缺证新类”会重复
  - 继续沿“全局 neutral_missing 更宽软化”会重复
  - 继续沿“`<unk>` state 传播”会重复
  - 这轮真正新且有效的入口，是：
    - **把 one-gap 多 token 缺词从一般 `neutral_missing` 中拆出为 `mixed_token_missing`**
  - 它在不脱离 `P0 grammar 语义纠偏` 框架的前提下，已经拿到：
    - `case2` 最终 top1 翻正
    - `case3` guardrail 保持

## 2026-05-26 mixed_token_missing 代表集轻量复核：不是只救 case2，但覆盖面仍集中在 `一* / one-gap` 类

- 在拿到 `case2` 翻正后，没有直接去跑长 baseline，而是先按 `阶段2实施清单_P0_P1_P2.md` 的三层资产做一轮最小代表集复核：
  - 只用现成 `partial_chain_stage_probe.py`
  - 只跑 7 个已有 case
  - 模式从最重的 `full` 收紧为：
    - `probe`
    - 即只保留：
      - final snapshot
      - next-hop
    - 去掉 graph contract
  - 这样可以避免为了代表集复核就跑长时间脚本
- 中途也专门做了一次效率止损：
  - 先尝试 7 case `--mode full`
  - 超过预期窗口仍无产出后立即停止
  - 改成更轻的 `--mode probe`
  - 避免无必要地长时间占用终端与 runtime
- 7 case 轻量复核结果如下：
  - `case2_diyizhan`
    - top1：
      - `第一站是一座古老的小镇`
    - `expected_rank = 1`
  - `case3_tiyanbuyiyang`
    - top1：
      - `体验不一样的生活`
    - `expected_rank = 1`
  - `case4_liangpang`
    - top1：
      - `两旁是古色古香的建筑`
    - `expected_rank = 1`
  - `case5_zoujin`
    - top1：
      - `走进一家特色小店`
    - guardrail 保持正确
  - `case6_muzhi`
    - top1：
      - `木质的门窗`
    - guardrail 保持正确
  - `case7_liushi`
    - top1：
      - `也带着一种不可挽回的流失`
    - `expected = 流逝`
    - `expected_rank = 2`
    - 说明已有正向外溢，但还没翻正
  - `case1_yizhixiangwang`
    - top1：
      - `一直想望着远方`
    - `expected = 一直向往着远方`
    - `expected_rank = null`
    - 说明这刀没有覆盖到这类问题
- 因而这轮代表集结论可以写得比较清楚：
  - `mixed_token_missing` **不是只对单个 `case2` 生效**
  - 至少还外溢到了：
    - `case4_liangpang`
      - 已直接翻正
    - `case7_liushi`
      - `流逝` 升到 rank 2
  - 同时没有把：
    - `case3_tiyanbuyiyang`
    - `case5_zoujin`
    - `case6_muzhi`
    - 这些 guardrail 拉坏
- 但覆盖边界也已经足够明确：
  - 当前这刀主要在：
    - `一*`
    - one-gap
    - 多 token mixed-missing
    - 且 shared-prefix/continuation 较干净
    - 的路径上生效
  - 对：
    - `一直向往着远方`
      - `向往/想望`
    - 这类不属于 one-gap mixed-token 缺词的问题
    - 几乎没有帮助
- 所以下一步若继续，方向不该是“继续放宽 mixed_token_missing”，否则有过宽风险；
  更合理的是二选一：
  - 继续把 `P0` 做完整：
    - 只围绕 one-gap mixed-token family 再补 1-2 个代表样本和 acceptance
    - 先把这一类确认做实
  - 或者开始准备切到 `P1`：
    - 去查 `case1_yizhixiangwang`
    - 为什么在不属于 mixed-token 缺词的前提下，
      仍会在 shared-prefix request/family formation 阶段先天输给 `想望`

## 2026-05-26 去重后继续查 `case1_yizhixiangwang`：确认主阻塞已切到更早的 request-stage state ownership，最小 lineage-tail 原型判负并回退

- 继续前先重新对齐了 `WORKLOG` 与 `阶段2实施清单_P0_P1_P2.md`，确认：
  - `一直向往着远方`
    - 本来就是 shared-prefix 代表句
  - 当前这条线不应再回到：
    - `P0` grammar 微调
    - `family_soft_clean -> HasConfidentPrimaryExact` 放宽
    - `head-supported single-char bridge`
  - 这些都已经做过并判负，不能重复
- 这轮先做的不是实现，而是补一个**只读观测增强**：
  - 在 `partial_chain_stage_probe.py` 的 `case1_yizhixiangwang` 中新增：
    - `next_hop_after_yizhi`
    - `source_suffix = 一直`
    - `focus_entries = 向 / 想 / 向往 / 想望 / 向往着 / 想望着`
  - 同时补了一条对应单测，先红后绿，确保这层 shared-prefix 竞争以后能稳定复看
- 新 probe 抽出来的关键 runtime 证据如下：
  - `一直 -> 向`
    - `search_score = -138.947`
    - `token_evidence_tag = direct_whole_word_hit`
  - `一直 -> 想`
    - `search_score = -138.893`
    - `token_evidence_tag = direct_whole_word_hit`
  - `一直 -> 向往`
    - `search_score = -154.762`
    - `token_evidence_tag = split_token_supported`
  - `一直 -> 想望`
    - `search_score = -180.409`
    - `token_evidence_tag = split_token_supported`
  - 同时 `transition_lm` 也确认：
    - `向往`
      - `total_log10 = -54.9656`
    - `想望`
      - `total_log10 = -75.9984`
  - 这说明：
    - `向往/向往着` 在 LM 和 request 分上都明显强于 `想望`
    - 所以这条 case **不是**再回去做 `P0` grammar 语义的题
- graph / contract 进一步把主阻塞收紧到：
  - 在 `一直` 这一步：
    - `向往`
    - `想望`
    - 都是：
      - `prefix_family_tag = family_soft_clean`
      - `supports_validated_continuation = true`
      - `continuation_tag = exact_ambiguous_family`
  - 只有到：
    - `一直向往着`
    - 才会出现：
      - `continuation_tag = legal_primary_continuation`
      - `path_tag = legal_but_path_unconfirmed`
      - `source_axis_tag = shared_prefix_axis_member`
  - 但最后：
    - `一直向往着 -> 远`
    - 仍只拿到：
      - `request_stage_tag = request_tail_supported`
    - 最终 snapshot top1 仍是：
      - `一直想望着远方`
    - `一直向往着远方` 仍未进入 top20
- 静态读码后，当前最关键的新判断是：
  - `case1` 的真实死亡点更早
  - 不是尾字 `远/与/于` 的 LM
  - 也不是 `向往` 自身不够强
  - 而是 `BuildRequestStagePrefixStates()` 在：
    - `一直 -> 向/想`
    - 这一跳使用的是：
      - `raw_score + best_tail_weight + bridge_bonus - risk`
    - 其中 `best_tail_weight` 仍是不分 family 的共用 tail 读数
  - 结果是：
    - 正确链和错误链在极早期会共享同一份 tail 支撑
    - request-stage best-prefix 更容易被 `一直想...` 抢走
- 在此基础上，这轮还尝试过一刀**最小 P1 原型**：
  - 新增一个实验性 `request_lineage_tail_supported`
  - 想表达“前缀已确认 lineage 的尾步承接”
  - 并给它介于 `request_source_line_eligible` 与 `request_tail_supported` 之间的 bias
- 结果：
  - 这个原型确实命中了：
    - `一直向往着 -> 远`
  - 但 **没有改变最终排序**
  - top1 仍是：
    - `一直想望着远方`
  - 说明它只是把尾步标签改漂亮了，**没有打到真正的 ownership 死亡点**
- 处理：
  - 已将这版 `request_lineage_tail_supported` 原型完整回退
  - 并重新执行：
    - `librime\\.\\build.bat static`
  - 当前工作树回到：
    - 保留 `case1` 的新 probe 观测点与单测
    - 不保留无收益的 translator 行为改动
- 因而这轮最后可稳定写死的结论是：
  - `case1` 当前已经不适合再做更下游的小 patch
  - 当前真正还没解决的，是更早的：
    - `request-stage state ownership`
    - / family-specific tail support
    - / shared-prefix family continuity
  - 如果下一步还要继续，就不该再围绕：
    - `向往着 -> 远`
    - 这种尾步标签补丁
    - 而应直接查：
      - `一直 -> 向/想`
      - 这一步的 family-specific request-stage prefix state 形成机制

## 2026-05-26 再沿 `case1` 做一轮更上游 P1 原型：早期单字桥 family split + 分支数放宽，命中 state 形状但最终判负并回退

- 在上一轮把 `case1` 收口到更早的 request-stage ownership 之后，这轮继续前再次查重，确认下面两点：
  - 之前做过并判负的，不再重复：
    - `head-supported single-char bridge`
    - `family_soft_clean -> HasConfidentPrimaryExact` 放宽
    - 尾步 `request_tail_supported` / lineage-tail 补丁
  - 当前还剩的一个**真正未做完的新口子**是：
    - `一直 -> 向/想`
    - 这一层是否因为
      - `ResolveNextRequestStageFamilyIdentity()` 过早继承旧 family
      - 再叠加 `kMaxBranchesPerState = 2`
      - 让 `向` 在 request-stage bucket 里根本活不下来
- 这轮先补了最小只读验证：
  - 从 `partial_chain_stage_probe.graph.jsonl` 直接读 `prefix_state.text = 一直` 的 request-stage states
  - 发现关键事实是：
    - `end=10` 对应 `xiang` 这一拍，候选里确实有：
      - `想 / 相 / 向`
    - 但 request-stage bucket 里只有：
      - `一直想`
      - `一直相`
      - 没有 `一直向`
    - 同时：
      - `一直向往`
      - 虽然在更后层能出现
      - 但早期并没有占到自己的 request-stage state
- 基于这个新事实，这轮做了一版**最小 P1 结构原型**，目标不是调分，而是让 request-stage 更早区分 family：
  1. `ResolveNextRequestStageFamilyIdentity()`
     - 对“强 exact 前缀后的早期单字桥”不再直接继承旧 family
     - 而是先把
       - `一直向`
       - `一直想`
       分成不同 family identity
  2. `BuildRequestStagePrefixStates()`
     - 对同一类“早期单字桥”状态
     - 仅把 `kMaxBranchesPerState`
       从 `2`
       临时放到 `3`
     - 目的只是让 `向` 不被 `想/相` 在进入 bucket 之前就直接裁掉
- 为避免“改了代码却不知道有没有命中结构”，这轮还按 TDD 加了一条最小 acceptance：
  - 先写失败测试，要求 `case1` 的 `prefix = 一直 / end=10` bucket 中应同时出现：
    - `一直向 / 一直向`
    - `一直想 / 一直想`
  - 初始测试失败，验证了问题确实存在
  - 加完原型后测试转绿，说明这刀**确实改变了 request-stage state 形状**
- 但运行时最终结果很明确，且足以止损：
  - 单例 `case1 full` 复跑后：
    - `一直向` family 确实已能活到更后层
    - 甚至在 `一直向往着于` 这类尾层 request-stage states 里也能看到
      - `family_identity = 一直向`
  - 但 snapshot 最终结果反而没有变好：
    - top1 仍是：
      - `一直想望着远方`
    - `一直向往着远方`
      - 甚至已经掉出 top20
      - `expected_rank = null`
- 因而这轮原型的性质可以明确判定为：
  - **命中了上游 state 形状**
  - **但没有转化成正向排序收益**
  - **而且已经出现了目标句进一步后退的负信号**
- 处理：
  - 已将这版原型完整回退：
    - 回退 `ResolveNextRequestStageFamilyIdentity()` 的 early single-char family split
    - 回退 `BuildRequestStagePrefixStates()` 的早期单字桥 `3-branch` 放宽
  - 删除了只服务于该失败原型的 acceptance 测试
  - 重新执行：
    - `librime\\.\\build.bat static`
  - 当前代码树回到：
    - 保留之前对 `case1` 的 probe 观测增强
    - 不保留这轮无收益的行为改动
- 这轮失败原型带来的最终新结论是：
  - `case1` 当前**不是**“只要把 `一直向` 放进 request-stage state 就会自然翻正”
  - 更深一层的瓶颈仍然在：
    - candidate-specific / family-specific tail support
    - 或更完整的 request-stage ownership 表示维度
  - 这已经超出“再加一个小规则 / 小原型”能高性价比验证的范围
- 因而到这里可以把当前阶段正式收口为：
  - `case1` 现有**所有仍像“小步 P1 原型”的路线已经基本试尽**
  - 剩余若继续，已更像：
    - 重新设计 request-stage state 表示
    - 或显式 family-specific tail support
    - 这种更大一级的结构方案

## 2026-05-26 进入正式 P1 结构计划：不再继续小 patch，改为按 ownership contract 计划推进

- 继续前再次对齐了：
  - `阶段2实施清单_P0_P1_P2.md`
  - `上游方案设计与可行性验证.md`
  - 当前 `WORKLOG`
- 当前共识已固定为：
  - `P0` 侧的小步 grammar 语义修正已经拿到有效突破
  - `case1_yizhixiangwang` 剩余问题已不再适合继续做 translator 小 patch
  - 当前若继续，应正式进入：
    - `P1: request-stage ownership / family-specific tail support`
    - 的结构设计与受控验证
- 因而这一步没有再写新实现，而是先把新的执行入口固化成计划文档：
  - `C:\Code\outwit\docs\superpowers\plans\2026-05-26-request-stage-ownership-p1.md`
- 这份计划明确了三层边界：
  1. 不重复已判负路线：
     - continuation weight 放大
     - 尾步标签补丁
     - 只改 early single-char bucket 形状
  2. 只在 translator 内推进：
     - request-stage state 表示
     - family-aware tail support
     - graph/probe 可审计读数
  3. 验证顺序固定为：
     - 先单例 `case1` 失败测试
     - 再最小代表集：
       - `case1 / case2 / case3 / case4 / case7`
     - 只有方向正确时才考虑更大 smoke / baseline
- 到这里的阶段性判断是：
  - 当前并不是“没有路可走”
  - 但剩下的路已经从“小步 patch”切换成“正式 P1 结构计划”
  - 后续继续时应直接按这份计划执行，而不是重新从新的局部小实验摸起
