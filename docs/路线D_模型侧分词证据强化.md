# 路线 D：模型侧分词证据强化

## 1. 问题定义

当前 `wanxiang-lts-zh-hans.klm` 是字级 split-token KenLM 模型。
对 case1 的关键差距：`向往` (LmRaw=-173.46) vs `想往` (LmRaw=-150.57)，gap = 22.89。

模型不知道"向往"是一个整词——它在训练语料被切成字后，丢失了整词级别的先验频率信息。

## 2. 三个子方向

### D1：词级 KenLM（最根本）

训练一个以词为单位的 KenLM 模型。

**训练方法：**
1. 获取中文文本语料（维基百科、新闻、小说等）
2. 用分词器（jieba/pkuseg/THULAC）将文本切成词序列
3. 将词序列写入 ARPA 格式文本
4. 用 KenLM `lmplz` 训练 n-gram 模型（order 6-12）
5. 用 `build_binary` 转为 .klm 文件

**训练素材建议：**
- 中文维基百科 dump（~1.5GB 文本）
- 新闻语料（人民日报、新华社）
- 文学语料（现代小说、散文）
- 总规模建议 5-20GB 纯净中文文本
- 分词器推荐 jieba（速度快、精度可接受）或 pkuseg（精度更高）

**硬件要求：**
- KenLM 训练主要消耗内存
- 5-gram on 10GB 语料：~16GB RAM
- 8-gram on 20GB 语料：~64GB RAM
- 训练时间：1-4 小时（取决于语料大小和 n-gram order）
- 不需要 GPU

**集成改动：**
- `witogram.cc` 中 `ScoreFeatures()` 的查询逻辑：从按字切分改为按词切分
- token_evidence_tag 的判断逻辑需要适配（词级不再有 char_fallback 概念）

### D2：字词混合 / BPE（中等改动）

用 BPE/SentencePiece 训练一个 subword tokenizer，然后在 subword tokens 上训练 KenLM。

**BPE 原理：**
- 从字级开始，反复合并最高频的相邻 token pair
- 高频组合如"向往"会自然成为独立 token
- 低频组合保持字级，避免 OOV

**训练方法：**
1. 准备中文文本语料（不需要分词，原始字序列即可）
2. 用 SentencePiece 训练 BPE 模型（vocab_size 建议 8000-32000）
3. 将语料用 BPE tokenizer 编码为 subword token 序列
4. 在 subword token 序列上训练 KenLM

**SentencePiece 训练命令示例：**
```bash
spm_train --input=corpus.txt --model_prefix=zh_bpe \
  --vocab_size=16000 --character_coverage=0.9995 \
  --model_type=bpe
```

**硬件要求：**
- SentencePiece 训练：极低（CPU 单线程，几分钟）
- KenLM 训练：同 D1
- 推理时 BPE tokenization 开销：可忽略

**与 n-gram 的计算成本对比：**
- 训练成本：基本相同（KenLM 复杂度由 n-gram 数量决定，不由 vocab 大小决定）
- 推理成本：BPE 多一步 tokenization（微秒级），KenLM query 不变
- 内存：模型大小由 n-gram 计数决定。BPE 倾向于减少 n-gram 数量（高频组合合并），实际可能比字级更小

**与 witset-worker 的关系：**
项目中已有 witset-worker 使用 Qwen3-0.6B 的 tokenizer（SentencePiece 格式）做 PPL 计算。
但那是用于 transformer LLM 推理，不是用于 KenLM 训练。BPE for KenLM 需要独立的训练管线。

### D3：运行时伪词打分（最小改动，算法层）

不改模型文件，不改字典文件。仅在 `witogram.cc` 的 `ScoreFeatures()` 或 `witset_poet.cc` 的 scoring 中增加：当候选文本的每个字符都属于某个已知整词时，不把它当成纯 fallback 路径。

**具体实现：**
1. 在 `Witogram::ScoreFeatures()` 中增加 `whole_word_prior_score`
2. 对每个候选 entry，查询词典：这个 entry 是否是一个已知的整词？
3. 如果是整词，对比它被当成 char-fallback 时的评分，计算"整词证据增量"
4. 将此增量加入 `base_score` 或作为独立的 `adjustment_score` 项

**对 case1 的效果：**
- `向往`：词典中存在的整词 → +whole_word_prior
- `想往`：词典中也存在，但频率/权重不同 → +whole_word_prior 但可能更小
- 如果词典中 `向往` 的权重优于 `想往`，这个信号就能缩小 gap

**为何之前说"不适用"：**
之前的判断有误——以为需要修改词典文件来标注哪些是整词。实际上，词典本身就已经区分了整词条目和单字条目。不需要修改任何文件，只需要在运行时查询现有词典即可判断 candidate 是否构成一个整词。

**风险：**
- 不能过度依赖（过度奖励整词可能会压制正确的单字补全）
- 需要一个适度的 weight，通过实验确定

## 3. 推荐实施顺序

1. **D3 先试**：零成本、纯算法改动，可以快速验证"额外分词证据"方向是否有效
2. **D2 再试**：如果 D3 有正向信号，训练 BPE tokenizer + 新 KenLM，成本中等
3. **D1 最后**：如果 D2 不够，全面切换到词级模型，成本最高
