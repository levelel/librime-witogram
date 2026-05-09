# librime-witogram

基于 RIME 引擎的高效 N-gram 语法模型插件（原 `librime-octagram`）。

本项目将底层的字典树结构由 `Darts::DoubleArray` 升级为高压缩率的 `marisa-trie`，在维持极速查询的同时，将动辄几百 MB 的语言模型体积大幅压缩（**通常可缩小至原体积的 1/3**）。

## 核心特性

- **极致压缩**：使用 `marisa-trie` (Succinct Data Structure) 存储海量 N-gram 数据，内存和磁盘占用大幅下降。
- **向下兼容**：自动识别模型文件头。若模型文件为旧版的 `Rime::Grammar/1.0` (基于 Darts)，引擎依然可以完美加载并运行；若为 `Rime::MarisaGrammar/1.0`，则启用新引擎。
- **无缝集成**：编译后即作为 `witogram` 插件挂载至 RIME，可与 `witset` 协同提供强大的整句/智能拼音输入体验。

## 编译方法

依赖：
- librime
- marisa-trie

在 librime 中，将本插件放入 `plugins/witogram` 目录中。
执行标准编译：
```bash
./build.bat static
```

## 模型格式转换指南

为了让您能够享受新格式带来的性能红利，我们提供了一个转换工具 `convert_grammar`。

### 为什么需要转换？
原本的 `.gram` 文件采用了双数组 Trie 树（Darts）。当词条达到千万级时，这种结构存在大量空洞，导致文件异常臃肿。
通过转换为 Marisa 格式：
1. **节省内存**：模型体积从 ~630MB 锐减至 ~240MB，后台占用更小。
2. **加载更快**：较小的文件尺寸带来了更快的 IO 读取和启动速度。
3. **精度无损**：转换过程中不丢失任何一条 N-gram 词频数据，打分结果 100% 对齐。

### 转换步骤

1. 编译成功后，工具会生成在 `librime/dist_x64/bin/convert_grammar.exe` (或对应的系统 bin 目录下)。
2. 将你需要转换的旧模型文件（如 `wanxiang-lts-zh-hans.gram`）准备好。
3. 执行转换命令：
```bash
convert_grammar.exe path/to/your/model.gram
```
4. 转换完成后，该模型文件会被**原地覆盖**更新为 Marisa 格式，体积会肉眼可见地变小。
5. 正常配置并在输入法中使用即可，无需任何代码修改。
