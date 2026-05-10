
<p align="center">
  <img src="https://raw.githubusercontent.com/levelel/outwit-project-index/main/resources/logo-circling.svg" alt="Outwit Logo" width="120" />
</p>

<h1 align="center">几维输入法 librime-Witogram 插件</h1>

基于 RIME 引擎的高效 N-gram 语法模型插件（原 `librime-octagram`）。

本项目将底层的字典树结构由 `Darts::DoubleArray` 彻底升级为业界标准的 `KenLM`，通过 `-q 8 -b 8` 双重 8-bit 量化，在维持极速查询的同时，有效压缩中大型语言模型的体积（**最高可缩减 60%**），并且废除了各种启发式的拍脑袋打分惩罚，全面转向了更科学的**对数线性模型 (Log-Linear Model)** 架构。

## 几维项目 

- [几维开源项目首页](https://github.com/levelel/outwit-project-index)

## 核心特性

- **极致压缩**：使用 `KenLM` 量化技术存储海量 N-gram 数据，将原本 520MB 的大型模型压缩至 205MB，磁盘占用和 IO 开销大幅下降。
- **内存映射加载**：在运行时通过 `mmap` 零延迟加载 `.klm` 模型，无需堆内预分配内存，物理内存占用逼近于 0。
- **纯粹的科学打分**：废弃了原有的各种生硬惩罚（`collocation_penalty` 等），直接采用 `KenLM` 返回的平滑对数概率 $\log_{10}(P)$。
- **Log-Linear 架构**：暴露出唯一一个配置参数 `weight`，用于线性插值，用户可自主调节 N-gram 模型的干预权重。

## 编译方法

依赖：
- librime
- KenLM (内置于 third_party)

在 librime 中，将本插件放入 `plugins/witogram` 目录中。
执行标准编译：
```bash
./build.bat static
```

## 模型格式转换指南

为了让您能够享受 KenLM 带来的体积和性能红利，我们提供了一套转换工具链。

### 为什么需要转换？
原本的 `.gram` 文件采用了双数组 Trie 树（Darts）。这会导致巨大的内存浪费，且不包含回退路径。转换为 `.klm` 格式可以带来以下优势：
1. **节省内存**：对于中大型模型，体积可缩减 40%~60%。
2. **完整回退算法**：KenLM 内部包含了隐马尔可夫模型的回退权重，比 Rime 原本的暴力短句截断科学得多。
3. **零延迟加载**：通过内存映射机制，启动输入法时无需等待。

### 转换步骤

1. 编译成功后，在 `librime/dist_x64/bin` 目录下会生成 `dump_to_arpa.exe` 和 `build_binary.exe`。
2. 提取原始模型至 ARPA 格式（包含自动数据推导与概率归一化）：
```bash
dump_to_arpa.exe wanxiang-lts-zh-hans.gram wanxiang-lts-zh-hans.arpa
```
3. 量化构建 `.klm` 二进制文件：
```bash
build_binary.exe -q 8 -b 8 trie wanxiang-lts-zh-hans.arpa wanxiang-lts-zh-hans.klm
```
4. 将生成的 `.klm` 放入配置目录即可，无需修改文件名后缀配置。

## 鸣谢 (Credits)

本项目是对原始 `librime-octagram` 插件的重构与优化。特别感谢原作者及社区：

- [librime-octagram](https://github.com/lotem/librime-octagram) 提供了基础的 N-gram 挂载机制。
- [KenLM](https://github.com/kpu/kenlm) 提供了极其强大的语言模型推理和量化框架。
