# witogram — 项目全景总结

## 目标

将 Rime IME 的 grammar 组件从 Weasel 的 octagram + GramDb 升级为现代化的 witogram + KenLM，**在不损失精度**的前提下实现双格式支持，并为未来数据升级铺路。

---

## 架构决策与最终方案

```
                    ┌─ .gram ─→ GramDb 直读 → 68.00%  ←─ 本实验
grammar/language ───┤
                    └─ .klm  ─→ KenLM 加载 → 可选模式
                                    ├─ backoff (ScoreFeatures) → 61.67%
                                    └─ collocation_mode → 63.67%
```

### 核心文件

| 文件 | 说明 |
|---|---|
| `witogram.cc` | 主实现：KenLM 评分 + Collocation + GramDb 直读 |
| `octagram_gram_db.h/cc` | 从 librime-octagram 移植的 GramDb (Darts Trie + MappedFile) |
| `octagram_encoding.h/cc` | 从 librime-octagram 移植的 gram 编码函数 |
| `snapshot_script_translator.cc` | 继承 ScriptTranslator，输出现有候选用于评估 |
| `rime_dumpd.cc` | daemon 模式 rime dump，支持 stdin 命令协议 |

---

## 实验序列

### 阶段 1: KenLM 基础（已废弃）

witogram + wanxiang.klm（GramDb→dump_to_arpa→build_binary 转换）

| 评分模式 | Top-1 | 问题 |
|---|---|---|
| backoff (ngram_weight=1) | 61.67% | KenLM backoff 平滑稀释了区分度 |
| collocation_mode (ngram_length) | 63.67% | 人工 n-gram 假阳性（-4.66%） |

**根因：** GramDb→KenLM 转换为满足 ARPA backoff 一致性，必须插入缺失前缀 n-gram。这些人工 n-gram 无法在运行时被区分。

### 阶段 2: 原生 KenLM 训练（已废弃）

在远程服务器用 32GB 中文语料训练字符 6-gram KenLM

| 模型 | 体积 | Top-1 |
|---|---|---|
| wanxiang-lts (RIME-LMDG) | 234MB | 61.67% |
| 32GB 原生 6-gram | 2.3GB | 60.67% |

**结论：** 数据质量 > 数据量。RIME-LMDG 精选语料 > 32GB 通用网络语料。

### 阶段 3: GramDb 直读（最终方案 ✅）

绕开转换，直接加载 .gram 文件

| 评分 | Top-1 | 说明 |
|---|---|---|
| GramDb 直读 | **68.00%** | 追平 octagram 原版 68.33% |
| octagram 原版 | 68.33% | Weasel 环境基线 |

**结论：** 现代化后可无损替代 octagram。

---

## BPE 实验（已结项）

### 为什么失败
BPE 子词合并（"向往"→单 token）理论上正确，但字符 n-gram (order≥5) 已经编码了整词信息 (P(往|向) ≈ P(向往))，BPE 不增加额外区分信号。

### 实验矩阵

| 模型 | 体积 | 管线 | Top-1 | 结论 |
|---|---|---|---|---|
| BPE 4-gram | 7.4GB | witset | 52.82% | 太大太慢 |
| BPE 3-gram | 1.5GB | witset | 53.00% | = wanxiang 同管线 |
| BPE 3-gram | 1.5GB | **stock** | **61.67%** | = wanxiang 同管线 |

**最终结论：BPE 无独立价值。正式结项。**

---

## 管线对比

| 管线 | Grammar | Top-1 | Top-3 | avg candidates |
|---|---|---|---|---|
| **Stock + GramDb** | wanxiang (RIME-LMDG) | **68.00%** | 69.67% | — |
| Stock + KenLM collocation | wanxiang (转换版) | 63.67% | 65.33% | — |
| Stock + KenLM backoff | wanxiang (转换版) | 61.67% | 63.33% | — |
| Stock + KenLM backoff | BPE 3-gram | 61.67% | 63.33% | — |
| Stock + KenLM collocation | 原生 6-gram | 60.67% | 62.33% | — |
| Witset + KenLM backoff | BPE 3-gram | 53.00% | 64.00% | 19.64 |
| Witset + dict only | 无 | 29.20% | 38.90% | 4.99 |
| **Octagram (Weasel, 原版)** | wanxiang (GramDb) | **68.33%** | 70.00% | — |

---

## 关键发现

1. **数据质量 > 数据量。** 21M 精选 n-gram（RIME-LMDG）> 500M 网络 n-gram。拼音输入法需要口语化、日常化的语料，学术/新闻内容稀释了搭配信号。
2. **GramDb 不能无损转 KenLM。** ARPA 的 backoff 一致性要求迫使插入人工前缀 n-gram，这些条目在运行时无法与真实条目区分。
3. **Collocation 检测 > Backoff 平滑。** 固定惩罚（±12）的碰撞检测比 KenLM 概率 backoff 更适合"搭配合理/不合理"的二元判断。
4. **Stock pipeline (Poet w=7) > Witset (Poet w=80)。** 相同的 grammar 在 stock 中比 witset 高 8.67%。问题在搜索架构，不在 LM。

---

## 遗留问题与未来方向

### 短期
- witogram 可以同时加载 .gram 和 .klm，当前优先使用 .gram
- collocation_mode 保留为 KenLM 原生数据的入口（当有更好数据时可启用）

### 中期
- 要超越 68.33%，需要 RIME-LMDG 同类语料 + 更高阶 n-gram（8+）
- 现有 32GB 语料可做"拼音输入风格过滤"后重训

### 已放弃的路线
- ❌ BPE 子词模型（无独立价值）
- ❌ GramDb→dump_to_arpa→KenLM 转换（人工 n-gram 不可解）
- ❌ 通用网络语料原生 KenLM（数据不匹配场景）
