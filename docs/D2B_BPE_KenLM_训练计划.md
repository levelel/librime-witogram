# D2B：BPE + KenLM 独立训练计划

## 1. 目标

训练一个基于 BPE subword tokenization 的中文 KenLM n-gram 模型，
替换或补充当前的字级 split-token `wanxiang-lts-zh-hans.klm`。

核心收益：高频词组合（如"向往"）作为单一 BPE token 进入 LM，
不再被拆成两个独立的字，消除字级 LM 对整词的无差别低估。

## 2. 环境准备

### 2.1 Python 依赖

```bash
pip install sentencepiece
```

### 2.2 现有工具

项目中的 KenLM 工具链位于：
`C:\Code\outwit\outwit-windows\librime\plugins\witogram\third_party\kenlm\bin\Release\`
- `lmplz.exe` — 训练 n-gram LM
- `build_binary.exe` — 转为 .klm 二进制格式

## 3. 步骤一：获取语料（第 1 天）

### 3.1 中文维基百科（推荐首选）

```powershell
# 下载（约 2GB 压缩包）
Invoke-WebRequest -Uri "https://dumps.wikimedia.org/zhwiki/latest/zhwiki-latest-pages-articles.xml.bz2" -OutFile "zhwiki.xml.bz2"

# 安装 WikiExtractor
git clone https://github.com/attardi/wikiextractor.git
cd wikiextractor

# 提取纯文本
python WikiExtractor.py -o ..\wiki_text --json ..\zhwiki.xml.bz2

# 合并
Get-Content ..\wiki_text\*\* | Out-File -Encoding UTF8 ..\raw_corpus.txt
```

### 3.2 清洗

```python
# clean.py
import re
with open('raw_corpus.txt', 'r', encoding='utf-8') as f:
    text = f.read()
# 保留中文、标点、换行
text = re.sub(r'[^\u4e00-\u9fff\u3000-\u303f\uff00-\uffef。，！？；：、\n]', '', text)
# 过滤过短行
lines = [l for l in text.split('\n') if len(l.strip()) > 10]
with open('clean_corpus.txt', 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines))
```

预期产出：~1.5GB 纯净中文文本，~200 万行。

### 3.3 可选增强语料

| 来源 | 说明 |
|---|---|
| 搜狗实验室 SogouCA | 全网新闻数据，约 2GB |
| 人民日报 2014 | 新闻领域语料 |
| 现代小说文本 | 文学领域语料 |

建议至少覆盖新闻 + 百科两个领域。

## 4. 步骤二：训练 SentencePiece BPE（第 1 天，几分钟）

```bash
spm_train \
  --input=clean_corpus.txt \
  --model_prefix=zh_bpe_8k \
  --vocab_size=8000 \
  --character_coverage=0.9995 \
  --model_type=bpe \
  --max_sentence_length=4192 \
  --num_threads=8 \
  --input_sentence_size=5000000
```

参数说明：
- `vocab_size=8000`：8K subword token，平衡压缩率与模型大小
- `character_coverage=0.9995`：覆盖 99.95% 汉字，罕见字回退到字节
- `input_sentence_size=5000000`：采样 500 万句训练（语料够大时可全量）

产出：
- `zh_bpe_8k.model`（~4MB）
- `zh_bpe_8k.vocab`（~200KB）

### 验证 BPE 效果

```bash
echo "一直向往着远方" | spm_encode --model=zh_bpe_8k.model
# 期望输出类似: ▁一直 ▁向往 ▁着 ▁远方
```

关键检查：`向往` 是否作为单一 token 出现。如果没有，增大 `vocab_size` 到 16000 或 32000。

## 5. 步骤三：BPE tokenize 语料（第 1 天，几分钟）

```bash
spm_encode --model=zh_bpe_8k.model --output_format=piece \
  < clean_corpus.txt > bpe_corpus.txt
```

产出：`bpe_corpus.txt`，每行是 BPE token 空格分隔序列，如：
```
▁一直 ▁向往 ▁着 ▁远方
```

## 6. 步骤四：训练 KenLM（第 1-2 天，1-4 小时）

```powershell
$kenlm = "C:\Code\outwit\outwit-windows\librime\plugins\witogram\third_party\kenlm\bin\Release"

# 训练 6-gram LM
& "$kenlm\lmplz.exe" -o 6 --discount_fallback `
  < bpe_corpus.txt `
  > zh_bpe_6gram.arpa

# 转为二进制
& "$kenlm\build_binary.exe" trie zh_bpe_6gram.arpa zh_bpe_6gram.klm
```

硬件需求：
| order | RAM | 磁盘 | 时间 |
|---|---|---|---|
| 6-gram | 16-32 GB | ~1GB | 1-2h |
| 8-gram | 32-64 GB | ~3GB | 2-4h |

## 7. 步骤五：集成到 witogram（第 2-3 天）

### 7.1 双模型共存策略（推荐）

不为替换当前模型，而是**并行查询**：

```cpp
// 伪代码：ScoreFeatures() 中
double current_lm_score = QueryKenLM(wanxiang_klm, context, word);  // 现有
double bpe_lm_score = QueryKenLM(bpe_klm, context_bpe, word_bpe);   // 新增
double combined_lm = alpha * bpe_lm_score + (1-alpha) * current_lm_score;
```

### 7.2 修改文件清单

| 文件 | 改动 |
|---|---|
| `witogram.h` | 新增 `bpe_klm_` 成员 + `BpeTokenizer` 成员 |
| `witogram.cc` | `ScoreFeatures()` 中新增 BPE 路径；`Load()` 中加载 BPE 模型 |
| `witogram_component.cc` | 配置参数：`bpe_model_path`, `bpe_klm_path`, `bpe_lm_weight` |
| `witset.schema.yaml` | 新增配置项 |

### 7.3 配置示例

```yaml
witogram:
  bpe_model_path: "C:/path/to/zh_bpe_8k.model"
  bpe_klm_path: "C:/path/to/zh_bpe_6gram.klm"
  bpe_lm_weight: 0.5
  bpe_lm_cache_size: 4096
```

## 8. 评估策略

1. 先跑 case1 单点验证：`一直向往着远方` 是否翻正
2. 再跑 300 条基线：对比基线 Top-1/Top-3
3. 调 `bpe_lm_weight`（0.3-0.7 范围内）
4. 必要时增大 BPE vocab（16000、32000）重新训练

## 9. 时间估算

| 步骤 | 时间 |
|---|---|
| 语料下载（维基） | 1-2h（取决于网速） |
| 语料清洗 | 10min |
| BPE 训练 | 5-10min |
| BPE tokenize | 5min |
| KenLM 6-gram 训练 | 1-2h |
| 代码集成 | 2-4h |
| 测试验证 | 1-2h |
| **总计** | **约 1-2 天** |

## 10. 实验结果（2026-05-30 实际验证）

### 10.1 实际语料

- 中文维基百科 + 新闻（news2016zh）+ 搜狗新闻（Sohu）+ 中文小说（ChineseBook）
- 总计 **4.08 亿行 / 32GB** 纯净中文
- 训练平台：远程 Linux 服务器（31GB RAM, 8 vCPU）

### 10.2 实际模型对比

| 模型 | 参数 | 文件大小 | 训练时间 |
|---|---|---|---|
| BPE 32K 4-gram | `--prune 0 0 1 2` + `build_binary -q 8 -b 8` | **7.4 GB** | ~44 min |
| BPE 32K 3-gram | `--prune 0 0 5` + `build_binary -q 8 -b 8` | **1.5 GB** | ~26 min |

### 10.3 C++ 集成改动清单

| 文件 | 改动 |
|---|---|
| `witogram.h` | 新增 `BpeTokenize()`、`use_bpe_`、`bpe_processor_` |
| `witogram.cc` | 构造函数加载 BPE 模型；`SplitUtf8Tokens()` 替换为 `Tokenize()` |
| `CMakeLists.txt` | `add_subdirectory(third_party/sentencepiece)`；链接 `sentencepiece-static` |
| `bpe_bench.schema.yaml` | 新增 schema：`grammar/language: zh_bpe_4gram_q8`, `grammar/bpe_model: zh_bpe_32k` |

### 10.4 300 条基线结果

| 模型 | Top-1 | Top-3 | Wall Time | 模型大小 |
|---|---|---|---|---|
| Octagram（wanxiang-lts 原版） | **68.33%** | 70.00% | — | 234 MB |
| Witogram（wanxiang-lts，当前） | ~53% | ~68% | — | 234 MB |
| **BPE 32K + 4-gram q8** | **52.82%** | 64.12% | 292 s | **7.4 GB** |
| **BPE 32K + 3-gram q8** | **53.00%** | 64.00% | 288 s | **1.5 GB** |

### 10.5 关键验证

- **"向往"在 BPE 32K 模型中成功合并为单一 token** ✅
  - 上下文 `一直向往着远方` → BPE tokens: `▁一直 向往 着 远方`
  - KenLM 分数：`-11.73`（正确）vs `想望着` `-14.05`（错误）
- **BPE LM 在 case1 上首次实现了"向往"优于"想望"** ✅
  - gap = 2.32（更大负数 = 更大惩罚）

### 10.6 结论

1. BPE 3-gram 与 4-gram 准确率几乎相同（53.0% vs 52.8%），3-gram 模型小 5x
2. BPE 模型与当前 witogram wanxiang-lts 基准基本持平，技术路线可行
3. 但模型体积（1.5GB+）远大于 wanxiang-lts（234MB），投入产出比偏低
4. `expected_not_found_count ≈ 97` 说明核心瓶颈在词典/翻译器层面
5. 方案 D（让模型本身提供更强分词证据）建议作为长期路线保留

### 10.7 Stock pipeline 验证（2026-05-30）

切换到 stock pipeline (ScriptTranslator + Poet w=7) 重新测试：

| 模型 | 管线 | Top-1 | 结论 |
|---|---|---|---|
| BPE 3-gram 1.5GB | **Stock (Poet w=7)** | **61.67%** | = wanxiang-lts 同管线 |
| wanxiang-lts 234MB | **Stock (Poet w=7)** | **61.67%** | — |
| BPE 3-gram 1.5GB | Witset (Poet w=80) | 53.00% | 管线瓶颈确认 |

### 10.8 最终结论

- **BPE 实验正式结项：BPE 子词模型对中文拼音输入没有独立价值。**
- 原因：字符 n-gram (order≥5) 已经编码了整词信息 (P(往|向) ≈ P(向往))，BPE token 合并不提供新的区分信号
- Stock pipeline 的 +8.67% 提升证明问题在搜索架构（Poet beam width），不在 n-gram 粒度
