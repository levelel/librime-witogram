<p align="center">
  <img src="https://raw.githubusercontent.com/levelel/outwit-project-index/main/resources/logo-circling.svg" alt="Outwit Logo" width="120" />
</p>

<h1 align="center">几维输入法 librime-Witogram 插件</h1>

基于 RIME 引擎、以 `KenLM` 为底层实现的 N-gram 语法模型插件。它的定位不是对 `librime-octagram` 的逐行等价复刻，而是在保留 Rime `grammar` 接口兼容性的前提下，逐步演进为更现代、可维护、可扩展的上位方案。

当前仓库的状态：

- 工程基础已经完成：`KenLM` 模型加载、`mmap`、UTF-8 纠正、线程安全、运行时去掉 Darts 依赖等已经落地。
- 评分体系仍在持续校准：当前 `.gram -> .arpa -> .klm` 转换链路是**兼容性优先**的实现，句级打分契约也仍在迭代中。
- 因此，`witogram` 现阶段更适合被理解为“正在演进中的新架构基础”，而不是已经完全证明优于 `octagram` 的终态版本。

## 几维项目 

- [几维开源项目首页](https://github.com/levelel/outwit-project-index)

## 当前已实现的能力

- **标准 UTF-8 查询链路**：运行时查询不再依赖旧的自定义变长编码，避免了 `<unk>` 词汇大量丢失的问题。离线转换工具会先将旧编码解回 UTF-8，再交给后续流程处理。
- **KenLM + mmap 加载**：运行时通过 `KenLM` 的二进制模型配合 `mmap` 加载，具备更现代的模型访问方式。模型文件越大，对磁盘随机读取性能要求越高，推荐搭配高速 NVMe SSD 使用。
- **线程安全的模型缓存**：模型加载器已经引入 `std::mutex`，在多线程初始化和多 schema 场景下比旧实现更安全。
- **运行时架构更简洁**：用户端运行时已经不再依赖旧的 `gram_db` / `Darts::DoubleArray` 查询链，维护成本更低，也更适合后续继续演进。
- **一档模式特征融合已开始落地**：`witogram` 运行时现在不再只提供单个 LM 分值，`witset_poet` 也已开始按显式特征项做句级融合，为追平或超过原版 `octagram` 的准确度做底层替换准备。

## 当前仍在校准的部分

- **模型转换链路**：当前 `.gram -> .arpa -> .klm` 流程是以兼容旧模型为目标的过渡实现，还不能简单等价理解为“已经严格恢复出标准概率分布”。
- **句级评分契约**：`witogram` 与 `witset_poet` 的分数融合已开始从旧的 `Dict + LmScaled` 混分迁移到显式特征契约，但参数仍在校准中，现阶段不应将其视为已经完成的对数线性模型。
- **相对 `octagram` 的排序优势**：这是当前正在推进和验证的方向，而不是已经完成验收的事实。

## 项目路线

目前项目正在推进的方向是：

1. 保留 `KenLM + mmap + UTF-8 + 线程安全` 这些已经落地的工程优势；
2. 在 `witset` 自己的 `Poet` 中建立更清晰、可解释、可验证的句级评分契约；
3. 重做模型转换与验证体系，逐步让 `witogram` 真正具备“上位替代”所需要的排序质量。

路线说明文档见：

- [route_b_implementation_plan.md](./docs/route_b_implementation_plan.md)
- [validation_baseline.md](./docs/validation_baseline.md)

## 本地验证快照

为验证 `witogram + witset` 的本地候选排序链路，当前已经补上了可累计的本地快照导出能力：

- `witset_translator` 会将每次本地造句结果追加写入 JSONL 快照文件，而不是只保留最后一次覆盖结果。
- 每条记录包含当前输入、前文、候选列表以及 `Dict / DictNorm / LmRaw / LmScaled / LmAvg / Boundary / OOV / Len / Whole / Base / Pen / Adj / Total` 调试分项。
- 仓库内提供了离线汇总脚本 `tools/summarize_local_snapshot.py`，可将累计快照整理成 `metrics.json`、`latest_candidates.json` 和 `regression_report.md`。
- 这条链路已经通过真实运行验证：从 `rime_api_console` 实际驱动 `witset` 输入后，能够在用户目录成功产出 `witset_local_snapshot.jsonl` 和汇总报告。

当前已确认的验证注意事项：

- 如果只是单独打开 `llm_level_1`，而没有先关闭 `llm_level_3 / llm_level_2`，实际跑到的仍可能不是纯本地 N-gram 链路。
- 做 `witogram` 本地排序验证时，应显式关闭 `llm_level_3` 和 `llm_level_2`，再开启 `llm_level_1`。

推荐配置示例：

```yaml
translator:
  debug_dump_local_snapshot: true
  debug_local_snapshot_path: "C:/Users/Bing/AppData/Roaming/witty/debug/witset_local_snapshot.jsonl"
  debug_dump_local_graph_snapshot: false
```

## 配置文件说明

### 启用插件

当前版本仍然通过标准 Rime `grammar` 组件接入。要启用 `witogram`，需要在对应的 schema 配置文件（例如 `witty.schema.yaml` 等）中定义 `grammar` 节点，并在 `translator` 配置中开启 `contextual_suggestions`：

```yaml
# 在文件顶层定义 grammar 节点
grammar:
  language: "grammar/wanxiang-lts-zh-hans"  # 指向用户目录中 grammar/wanxiang-lts-zh-hans.klm 文件
  weight: 1.0                               # 当前为 grammar 查询权重，实际最优值仍需结合验证结果校准

# 在你的拼音/整句翻译器中启用上下文建议
translator:
  # ... 其他配置 ...
  contextual_suggestions: true              # 必须开启此项，Poet 组件才会去加载和调用 grammar
```

> **注意：** `witogram` 的资源加载器会自动寻找 `.klm` 后缀，因此配置中的 `language` 字段无需写出 `.klm` 扩展名。如果原本使用的是 `.gram` 模型，配置路径通常不需要修改。

## ⚠️ 兼容性与共存问题

**Witogram 的目标是成为原版 Octagram 的上位替代品，但当前仍处于持续校准阶段。**

在底层的 C++ 实现中，`witogram` 注册的 Rime 组件名（`grammar`）以及读取的配置文件节点名（`grammar/language`、`grammar/weight`）与原版的 `octagram` **完全相同**。

因此：
1. **不能共存**：你**绝对不能**在同一个 `librime` 编译体系中同时加载 `librime-octagram` 和 `librime-witogram`。同时编译这两个插件会导致组件注册名冲突和链接错误。
2. **平滑迁移**：如果你想在其他标准的 Rime 发行版（如小狼毫、鼠须管等）中试用本插件，只需在编译 `librime` 时，**将源码中的 `plugins/octagram` 文件夹彻底删除**，并替换为本项目的 `plugins/witogram`。配置节点保持兼容。

## 编译方法

依赖：
- librime
- KenLM (内置于 third_party)

在 librime 中，将本插件放入 `plugins/witogram` 目录中。
执行标准编译：
```bash
./build.bat
```

## 模型格式转换指南

为了让旧 `.gram` 模型可以迁移到 `KenLM` 路线，仓库中提供了一套转换工具链。需要特别说明的是：当前转换链路的目标首先是**兼容旧模型并打通新架构**，而不是宣称已经无损恢复为标准语言模型。

### 为什么需要转换？

原本的 `.gram` 文件建立在旧的双数组 Trie 存储之上，和当前 `KenLM` 查询链路并不直接兼容。转换为 `.klm` 的现实意义主要有两点：

1. **打通新的运行时架构**：使模型能够进入 `KenLM + mmap` 的加载与查询链路。
2. **为后续评分体系重构做准备**：让 `witogram` 可以在统一的模型接口上继续演进，而不是长期绑定旧格式实现。

当前请不要把转换结果理解为：

- 已经完整、无损恢复原 `.gram` 的全部概率语义；
- 已经天然优于原 `octagram`；
- 已经完成标准对数线性模型的建模。

这些能力仍在后续路线中持续推进。

### 转换步骤

> **💡**：我们已经在 [Releases 页面](../../releases) 提供了预先转换好的万象中文 N-gram 模型。普通用户无需自行编译和执行下方转换步骤，直接下载预制模型文件放入用户目录下 `grammar` 文件夹即可。后续随着转换链路和评分体系演进，README 会同步更新模型状态与推荐版本。

1. 编译成功后，在 `librime/dist_x64/bin` 目录下会生成 `dump_to_arpa.exe` 和 `build_binary.exe`。
2. 提取原始模型至 ARPA 格式（当前实现包含兼容性数据推导与归一化处理）：
```bash
dump_to_arpa.exe wanxiang-lts-zh-hans.gram wanxiang-lts-zh-hans.arpa
```
3. 量化构建 `.klm` 二进制文件：
```bash
build_binary.exe -q 8 -b 8 trie wanxiang-lts-zh-hans.arpa wanxiang-lts-zh-hans.klm
```
4. 将生成的 `.klm` 放入配置目录即可，无需修改文件名后缀配置。

## 鸣谢 (Credits)

本项目起步于对原始 `librime-octagram` 插件的重构与现代化尝试。特别感谢原作者及社区：

- [librime-octagram](https://github.com/lotem/librime-octagram) 提供了基础的 N-gram 挂载机制。
- [KenLM](https://github.com/kpu/kenlm) 提供了极其强大的语言模型推理和量化框架。
- [万象拼音](https://github.com/amzxyz/RIME-LMDG) 提供了高质量的中文 N-gram 模型，为本插件的优化和测试提供了坚实基础。
