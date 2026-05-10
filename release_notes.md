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

## 兼容性说明

`witogram` 是一个完全**兼容标准 Rime 引擎架构**的后端插件（`Processor` 和 `Filter`），它负责在 Rime 的 `Context` 内计算词句的语言模型概率。
- **与 Rime 前端的兼容性**：它**完全兼容**所有标准的 Rime 前端（例如 Weasel 小狼毫、Squirrel 鼠须管、Fcitx5-rime、同文输入法等）。
- **如何使用**：只要你将 `librime-witogram` 作为一个插件编译进该前端所使用的 `librime` 核心库中，并在配置中启用 grammar，它就可以直接工作，**不需要**像 `witplace` 那样对前端进行任何特殊改造。

## 致谢

特别感谢 [万象拼音](https://github.com/mirtlecn/rime-wanxiang) 项目制作并开源了如此高质量的中文 N-gram 模型数据！
