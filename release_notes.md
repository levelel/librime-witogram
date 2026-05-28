## 进展记录

## 2026-05-16

### 原版 `octagram` 对照基线已固化

- 已将首次原版 `Rime + octagram` 的 `300` 条对照结果保存到仓库：
  - `docs/benchmark_artifacts/octagram_300_20260516/stock_rime_octagram_snapshot.jsonl`
  - `docs/benchmark_artifacts/octagram_300_20260516/snapshot_summary/metrics.json`
  - `docs/benchmark_artifacts/octagram_300_20260516/snapshot_summary/baseline_run_metadata.json`
  - `docs/benchmark_artifacts/octagram_300_20260516/snapshot_summary/regression_report.md`
- 这份副本作为路线 B 后续每一轮句级排序改造的稳定对照组，后续不应直接覆盖。

### 首次正面对比结论

- 在相同语料、相同句库、相同 `grammar`、相同 `shuangpin_algebra` 对齐条件下：
  - 原版 `octagram`：`Top-1 = 0.683333`，`Top-3 = 0.700000`，`expected_not_found_count = 90`
  - 当前 `witogram + witset_poet`：`Top-1 = 0.534884`，`Top-3 = 0.661130`，`expected_not_found_count = 83`
- 这说明当前版本的主要短板不是候选召回，而是最终排序。
- 也说明原版 `octagram` 对歧义拼音路径的抑制虽然粗暴，但在真实语料上是有效的。

### 对当前实现的新判断

- 当前 `witogram::ScoreFeatures()` 已经能同时给出整词路径与逐字路径证据，但 `witset_poet` 还没有把这种差异真正转成强句级排序信号。
- 现阶段只靠：
  - `whole_word_bonus`
  - `char_fallback_penalty`
  - `boundary / length / oov`
  仍不足以稳定压制 `di'e / qi'an` 一类碎裂路径。
- 因此，下一步主线不再是继续微调常数，而是把“原版有效的歧义抑制能力”升级为更可解释的证据化特征。

### 下一步算法方向

- 下一阶段准备在 `witset_poet` 中引入三类新特征：
  - `merge_gain`
    - 用整词路径与逐字路径的 LM 分数差，判断“合并解释是否明显更自然”
  - `fragment_penalty`
    - 打击 `皇帝 + 俄`、`牵起 + 按` 一类结构碎裂路径
  - `ambiguity_margin`
    - 不看路径来源标签，而看该候选是否只能依赖弱解释才能存活
- 目标不是复制原版的硬惩罚，而是在统一候选池里，用更精准、更可解释的句级证据超过它。

## 2026-05-15

### 路线 B 阶段性结论更新

- 已将“拼音歧义消解与句级排序是一体能力”正式写入 `docs/route_b_implementation_plan.md`，后续不再把歧义路径简单视为特殊坏候选一票否决。
- 基于当前 `300` 条自动化基线，阶段性结果已写入计划文档：
  - `Top-1 Accuracy` 从 `0.358804` 提升到 `0.534884`
  - `Top-3 Accuracy` 从 `0.435216` 提升到 `0.661130`
- 已把剩余问题归纳为三类：
  - 高频错词仍可能压过正确表达
  - OOV/近 OOV 区分力不足
  - 拼音歧义切分产生的坏路径仍可活到最终候选
- 已在计划文档中补充 `Rime + octagram` 对照基准测试方案，并明确：
  - 当前 `witogram` 基线脚本不能直接原样用于原版小狼毫
  - 更推荐复用评测框架、替换驱动层，并建立隔离的 `octagram benchmark` 环境
  - 若当前原版小狼毫环境无法确认已真实启用 `octagram`，则大概率需要重新编译与部署匹配版本的 `librime-octagram`

### 一档模式底层算法重构

- `witogram` 不再只暴露单一 `Query()` 分值，运行时新增了更细的语言模型特征输出能力，包括：
  - `total_log10`
  - `avg_log10`
  - `token_count`
  - `oov_token_count`
  - `matched_whole_word`
  - `used_char_fallback`
- 空上下文初始化改为句首状态（BOS），不再沿用更松散的 `NullContext` 语义。
- `witset_poet` 的一档本地评分从“`Dict + LmScaled` 启发式混分”升级为“基础分 + 显式特征项”：
  - `Dict`
  - `DictNorm`
  - `LmScaled`
  - `LmAvg`
  - `Boundary`
  - `OOV`
  - `Len`
  - `Whole`
- 为避免长句和高同音分支在句级排序前被过早砍掉，原先固定写死的全局批量裁剪 `100` 已改为可配置的 `global_batch_limit`，默认按 beam 大小动态放宽。
- `witset.schema.yaml` 已同步加入一档模式的新评分参数，默认值直接偏向准确度优先，而不是旧的激进性能裁剪。

### 本地排序验证链路补齐

- `witset_translator` 的本地快照导出已从“单文件覆盖”改为“JSONL 追加写入”。
- 默认快照文件名调整为 `witset_local_snapshot.jsonl`，用于支持整份 `test_sentence.txt` 的连续跑批。
- 当用户仍配置旧的 `.json` 路径时，运行时会自动规范化为 `.jsonl`，避免覆盖式文件格式和累计模式冲突。
- 单条快照记录继续保留输入、前文、候选列表和 `Dict / DictNorm / LmRaw / LmScaled / LmAvg / Boundary / OOV / Len / Whole / Base / Pen / Adj / Total` 调试分项，便于追踪一档本地排序异常。

### 离线汇总脚本新增

- 新增 `tools/summarize_local_snapshot.py`，用于读取累计的本地快照 JSONL 文件。
- 脚本会输出：
  - `metrics.json`
  - `latest_candidates.json`
  - `regression_report.md`
- 当前脚本默认汇总：
  - 总记录数
  - 唯一输入数
  - 候选为空的输入
  - Top-1 输出分布
  - Top-1 分项分数均值
  - 多次运行中 Top-1 不稳定的输入
- 如果后续提供带 `input -> expected_text` 映射的参考文件，脚本也可以继续输出 Top-1 / Top-3 命中率。

### 当前状态口径

- `witogram` 已具备 `KenLM + mmap + UTF-8 + 线程安全` 的工程基础。
- 当前主线仍然不是宣布“已经全面优于 `octagram`”，而是先把本地排序验证体系做实。
- 现阶段最重要的目标是：
  - 让本地候选排序结果可累计采集
  - 让每次改动都能基于全量语料回看结果
  - 为后续继续调整 `witogram + witset_poet` 的分数契约提供事实依据

### 编译部署与真实链路验证

- 已遵守工作区规则，在 `outwit-windows` 目录下执行 `build_and_deploy.bat`，未使用 `clean`，构建与部署成功。
- 为生成 `rime_api_console.exe` 等验证工具，又在 `librime` 目录下执行了标准 `build.bat static`。
- 之后从 `C:/Users/Bing/AppData/Roaming/witty` 用户目录实际运行 `rime_api_console.exe`，成功触发 `witset` 输入链路并写出：
  - `C:/Users/Bing/AppData/Roaming/witty/debug/witset_local_snapshot.jsonl`
  - `C:/Users/Bing/AppData/Roaming/witty/debug/snapshot_summary/metrics.json`
  - `C:/Users/Bing/AppData/Roaming/witty/debug/snapshot_summary/regression_report.md`
- 验证过程中确认了一点重要行为：
  - 如果只执行 `set option llm_level_1`，默认的 `llm_level_3` 仍可能保持开启状态。
  - 要验证纯本地 `witogram` 排序，必须先显式关闭 `llm_level_3` 和 `llm_level_2`，再开启 `llm_level_1`。
