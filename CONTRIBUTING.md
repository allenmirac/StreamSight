# 贡献指南

感谢你对 StreamSight 的关注！本项目欢迎任何形式的贡献，包括 bug 修复、功能实现、文档改进与测试补充。

## 报告 Bug

- 请先在 [Issues](../../issues) 中搜索，确认是否已有相同问题。
- 提供清晰的重现信息：运行环境（OS、编译器、OpenCV/FFmpeg 版本）、启动命令、期望结果与实际结果、相关日志。

## 提交代码

1. Fork 本仓库并 clone 到本地。
2. 基于 `main` 分支创建功能分支（如 `feat/xxx`、`fix/xxx`）。
3. 完成修改，确保通过编译与测试。
4. 提交并推送，发起 Pull Request。

## 开发环境

### 依赖

```bash
# 运行时 + AI
sudo apt install libopencv-dev ffmpeg
# FFmpeg C API 管线
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
```

### 构建

```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

### 运行测试

```bash
cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
./bin/test_smoke
```

构建选项：

| 选项 | 默认 | 说明 |
|------|------|------|
| `BUILD_TESTS` | OFF | 构建所有测试目标（`test_smoke` 统一入口 + 各模块单测） |
| `BUILD_LEGACY_TARGETS` | OFF | 构建旧版目标（`rtsp_analysis_server` 等） |

## 代码风格

- 语言标准：**C++11**。
- 格式遵循仓库根目录的 `.clang-format`，提交前运行 `clang-format -i <file>`。
- 命名与现有代码保持一致：类名 `CamelCase`，成员变量以下划线结尾（如 `mutex_`），命名空间小写。
- 注释使用中文。

## 提交规范

提交信息遵循 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/)：

```
<type>(<scope>): <subject>
```

`type` 常用取值：`feat`（新功能）、`fix`（修复）、`docs`（文档）、`refactor`（重构）、`perf`（性能）、`test`（测试）、`build`（构建）、`chore`（杂项）。

示例：`feat(ffmpeg): add EncoderPool to replace per-stream encode threads`

## Pull Request 流程

- 一个 PR 聚焦一件事，避免混杂无关改动。
- 描述改动动机、实现思路、影响范围，涉及性能时附上压测数据。
- 确保测试通过。
- 维护者 review 通过后合并。

## 许可

贡献代码默认遵循本项目的 [Apache License 2.0](LICENSE)。
