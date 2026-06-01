<p align="center">
  <img src="https://raw.githubusercontent.com/levelel/outwit-project-index/main/resources/logo-circling.svg" alt="Outwit Logo" width="120" />
</p>

<h1 align="center">librime-witogram</h1>

RIME grammar 插件，支持 KenLM (.klm) 和 GramDb (.gram) 双格式加载。

## 现状

- **GramDb 直读（.gram）**：通过移植的 `Darts::DoubleArray` + `MappedFile` 加载，精度与原版 octagram 一致（基线：68.00% vs 68.33%）
- **KenLM 加载（.klm）**：通过 `QuantTrieModel` + mmap 加载，支持 8-bit 量化。提供两种评分模式：
  - `backoff`（默认）：KenLM 标准 backoff 概率（基线：61.67%）
  - `collocation_mode`：ngram_length 碰撞检测（基线：63.67%）
- 组件自动选择：配置 `grammar/language` 指向的 .klm 或 .gram 文件，运行时优先使用 .gram（若有）

## 已知局限

- **dump_to_arpa 转换会引入人工 n-gram**：转换需要填充缺失前缀以满足 ARPA backoff 一致性，这些人工条目（得分 -10.0）无法在运行时与真实条目区分。Collocation mode 因此比 GramDb 直读低 4.33%。
- **通用语料不如精选语料**：用 32GB 网络语料（维基百科 + 新闻）训练的 6-gram KenLM 仅 60.67%，低于 RIME-LMDG 精选的 21M n-gram。拼音输入需要日常化、口语化的训练数据。
- **BPE 子词未带来提升**：BPE 分词 + KenLM 训练与字符 n-gram 结果持平（61.67%），实验已结项。

## 配置

```yaml
grammar:
  language: wanxiang-lts-zh-hans       # 指向 grammar/{language}.klm 或 grammar/{language}.gram
  collocation_mode: false              # true 启用 kenlm ngram_length 碰撞检测
  collocation_penalty: -8
  non_collocation_penalty: -12
  rear_penalty: -18
```

## 待验证

1. 是否有 RIME-LMDG 同等级的训练语料能证明 KenLM + collocation_mode 可追平 GramDb 直读
2. 更高阶 n-gram (8+) 在 collocation_mode 下能否超越 68.00%
3. 如何在精选语料基础上做拼音输入场景的针对性过滤和剪枝

## 编译

依赖：librime、KenLM（third_party/kenlm）

```bash
# 放入 plugins/witogram 后编译
# Windows: ./build.bat
# Linux/macOS: 标准 cmake 流程
```

## 鸣谢

- [librime-octagram](https://github.com/lotem/librime-octagram)
- [KenLM](https://github.com/kpu/kenlm)
- [万象拼音 / RIME-LMDG](https://github.com/amzxyz/RIME-LMDG)
