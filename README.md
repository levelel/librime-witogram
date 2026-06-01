<p align="center">
  <img src="https://raw.githubusercontent.com/levelel/outwit-project-index/main/resources/logo-circling.svg" alt="Outwit Logo" width="120" />
</p>

<h1 align="center">librime-witogram</h1>

<p align="center">
基于 RIME 引擎、以 KenLM 为底层实现的 N-gram 语法模型插件，定位为 librime-octagram 的演进替代方案。
</p>

## 项目愿景

针对原 octagram 插件的三个痛点提出改进方案：

| 痛点 | octagram | witogram 方案 |
|---|---|---|
| 模型格式 | .gram（Darts Trie + 自定义编码） | .klm（KenLM mmap + 量化），同时保留 .gram 直读能力 |
| 扩展性 | 专有工具链，重建困难 | KenLM lmplz 标准流程，可基于语料重训 |
| 运行时架构 | 独立插件，代码与 librime 紧耦合 | 统一插件，组件化 |

## 实际验证结果

### 测试方法

- **基线管线**：Rime stock pipeline（ScriptTranslator + Poet beam search w=7）
- **测试集**：基于 72 字句子，按拼音映射拆分，每条看 Top-1/Top-3 命中率
- **快照工具**：`SnapshotScriptTranslator` 导出候选列表，`rime_dumpd` daemon 模式批量执行
- 详尽的验证过程和完整数据见 [PROJECT_SUMMARY.md](./docs/PROJECT_SUMMARY.md)

### 管线对比

| 管线 | Grammar | 评分方式 | Top-1 | 相对 octagram |
|---|---|---|---|---|
| **Stock + GramDb 直读** | wanxiang-lts.gram | GramDb::Lookup | **68.00%** | **≈ 追平** |
| Stock + KenLM collocation | wanxiang-lts.klm | ngram_length 检测 | 63.67% | -4.33% |
| Stock + KenLM backoff | wanxiang-lts.klm | KenLM 概率 | 61.67% | -6.66% |
| Stock + KenLM collocation | 32GB 原生 6-gram.klm | ngram_length 检测 | 60.67% | -7.66% |
| Octagram 原版 (Weasel) | wanxiang-lts.gram | GramDb::Lookup | 68.33% | 基线 |

### 关键发现

#### 1. GramDb 直读可无损替代 octagram（68.00% ≈ 68.33%）
通过移植 librime-octagram 的 `GramDb` 和 `gram_encoding` 代码到 witogram，直接加载 .gram 文件，评分逻辑与原版完全一致。配置 `grammar/language` 时，witogram 会自动查找对应的 .gram 和 .klm 文件，优先使用 .gram。

#### 2. GramDb → KenLM 转换存在精度损失（-4.33%）
dump_to_arpa 转换需要为 ARPA 格式填充缺失前缀 n-gram（得分 -10.0，`backoff=1.0`）。这些人工条目在 KenLM 的 `ngram_length` 检查和分数空间中与真实条目重叠，无法在运行时精确区分。尝试两种修复均未成功：
- **标记方案**：给人工条目赋极低得分（-99.0）→ build_binary 通过但运行时 FormatLoadException
- **分数门槛方案**：人工条目（~-37.5）与真实低频条目（可低至 -126）分数区间重叠，无法选择唯一阈值

#### 3. 数据质量比数据量更重要
用 32GB 网络语料（维基百科 + 新闻 + 搜狐）训练的 6-gram KenLM（2.3GB）仅 60.67%，低于 RIME-LMDG 精选的 21M 条目 wanxiang-lts（61.67% 同管线基础）。拼音输入法需要口语化、日常化的搭配数据，学术/新闻内容稀释了有效信号。

#### 4. BPE 子词模型无独立价值
BPE 分词 + KenLM 训练与字符 n-gram 结果持平（61.67% 同管线），实验已结项。原因：字符 n-gram（order≥5）已经编码了整词信息（P(往|向) ≈ P(向往)），BPE 合并不增加额外区分信号。

#### 5. Stock pipeline > Witset pipeline
相同 grammar 在 stock（Poet w=7）中比 witset（Poet w=80）高 8.67%。说明搜索架构（beam width 和候选管理）是主要瓶颈。

## 配置

```yaml
grammar:
  language: wanxiang-lts-zh-hans       # 指向 grammar/{language}.klm 或 grammar/{language}.gram
  # 以下为 KenLM collocation 模式参数（仅 .klm 有效）
  collocation_mode: false              # true 启用 ngram_length 碰撞检测
  collocation_penalty: -8
  non_collocation_penalty: -12
  rear_penalty: -18
```

## 待验证的技术路线

1. **RIME-LMDG 同等级语料 + collocation_mode**：如果有与原版训练数据同等质量的口语化语料，KenLM collocation 模式能否追平 GramDb 直读（68.00%）？
2. **更高阶 n-gram**：当前 wanxiang 数据最高 8-gram。如果有更高阶（12-16）精选语料，collocation 模式能否超越 68%？
3. **OOV 召回改善**：当前 ~110/300 条候选未命中目标，问题不在 grammar 而在 translator 上游（词典覆盖）。如何结合 grammar 信号做上游候选扩展？
4. **数据过滤方法**：通用语料中如何自动筛选出适合拼音输入法的 n-gram（短句、口语化、日常表达）？

## 编译

依赖：librime、KenLM（third_party/kenlm）、SentencePiece（third_party/sentencepiece，仅 BPE 实验需要）

```bash
# 放入 plugins/witogram 后编译
# Windows: ./build.bat
# Linux/macOS: 标准 cmake 流程
```

## 鸣谢

- [librime-octagram](https://github.com/lotem/librime-octagram) — 提供了基础的 GramDb 实现和搭配检测机制
- [KenLM](https://github.com/kpu/kenlm) — 提供了高性能语言模型推理框架
- [万象拼音 / RIME-LMDG](https://github.com/amzxyz/RIME-LMDG) — 提供了高质量中文 N-gram 训练数据
