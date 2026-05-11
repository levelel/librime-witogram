## 版本更新日志

### v1.1.0 架构与性能深度优化
* **修复底层编码隐患**：彻底移除了原版中自定义的 `0x4000-0xA000` 变长编码压缩逻辑。原编码在对接纯 UTF-8 的 KenLM 时会导致严重的词汇丢失（全部命中 `<unk>`）。现已重构 `dump_to_arpa` 与 `witogram.cc`，全面拥抱标准的 UTF-8 编码。
* **引入动态分词降级机制 (Word/Char Fallback)**：在 `Query` 接口中实现了智能匹配策略。优先尝试 Word-level（词组级别）查询；若命中 `<unk>`，则自动 fallback 为 Character-level（单字）逐字累加计分。这使得插件既能完美兼容万象等传统的单字 N-gram 模型，又能直接支持未来的词组级模型。
* **补全符合标准概率分布的 Backoff 权重**：原版的 `.gram` 模型为了极端压缩体积，剥离了 N-gram 的 Backoff（回退）概率。新版工具链在转换为 `.klm` 时，利用 KenLM 算法**完整补全并保留了所有层级的回退数据**。虽然模型物理体积因此变得比 `.gram` 稍大，但得益于 mmap 技术，**带来了极低的驻留内存 (RSS) 占用，物理内存消耗不再受模型文件大小限制，仅取决于实际打字时的活跃词频率**。
* **增强多线程安全性**：在 `WitogramComponent::GetModel` 中引入了 `std::lock_guard<std::mutex>`，彻底解决了在 Rime 多个 Schema 同时并发初始化时可能引发的竞态崩溃风险。
* **清理遗留死代码**：彻底移除了运行时不必要的 `gram_db.cc/h` 和 Darts 双数组相关逻辑，将旧有工具链完全隔离在离线构建流程中，大幅缩减了插件运行时的二进制体积。

---

# 万象拼音量化模型 (Witogram KLM)

这里提供了由 `witogram` 的转换工具链将**万象拼音**的 `.gram` 原始模型转换为 KenLM 高效 `.klm` 格式的模型文件。

## 包含的模型

- `wanxiang-mini-zh-hans.klm`: 迷你版，内存占用极低。
- `wanxiang-lts-zh-hans.klm`: 长期支持版，平衡了准确率与体积。
- `wanxiang-big-zh-hans.klm`: 完整版，提供最佳的预测效果。

## 使用方法

1. 确保你正在使用的 `librime` 已经集成了 `witogram` 插件。
2. 下载本 Release 提供的 `.klm` 模型文件（可根据你的内存和性能需求选择 mini、lts 或 big 版本）。
3. 将解压出的 `.klm` 模型文件放置在你的 Rime 用户目录（或共享目录）中。
4. 在你的 `*.schema.yaml` 或 `grammar.yaml` 配置中，将原本指向 `.gram` 模型的名称保持不变，`witogram` 插件会自动识别并优先加载同名的 `.klm` 模型。

## ⚠️ 兼容性与共存说明

`witogram` 是一个完全**兼容标准 Rime 引擎架构**的底层语言模型打分组件。
- **与原版 Octagram 冲突**：`witogram` 是对原版 `octagram` 的深度重构与上位替代，两者在底层注册了相同的组件名（`grammar`）和配置项。因此，**绝对不能在同一个 librime 编译体系中同时加载两者**，否则会导致冲突！在编译前，请务必从源码中彻底删除原有的 `plugins/octagram` 目录。
- **与 Rime 前端的兼容性**：它**完全兼容**所有标准的 Rime 前端（例如 Weasel 小狼毫、Squirrel 鼠须管、Fcitx5-rime、同文输入法等）。
- **如何使用**：只要你将 `librime-witogram` 作为一个插件编译进该前端所使用的 `librime` 核心库中，并在配置中启用 grammar，它就可以直接工作，无需对前端进行任何特殊改造。

## 致谢

特别感谢 [万象拼音](https://github.com/mirtlecn/rime-wanxiang) 项目制作并开源了如此高质量的中文 N-gram 模型数据！
