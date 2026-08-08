# Changelog

本仓库是 [some100/th07](https://github.com/some100/th07)（CC0 公有领域）的个人 fork，
用于学习与本地游玩。所有相对上游的改动都记录在这里。

## [1.0.0] - 2026-08-08

### 初版总结（Initial summary release）

首个整理好的版本。以“游玩体验与原版一致”为目标，在上游 th07 逆向工程的基础上
继续提升了逐字节匹配精度，并整理出可复现的构建/比对环境。

### 匹配率

- 主程序有效匹配率：**99.38% -> 99.51%**（reccmp 统计，678 个函数）
- 未达 100% 的函数：**75 -> 11 个**
- 另有 **16 个函数**经逐字节验证与原版完全一致，但因 reccmp 对巨型全局对象
  地址范围的标签误报而被列入 `ignore_functions`（按 100% 计入时总量约 99.52%）

### 主要修复

- **浮点常量/宽字符串标签**：新增 `resources/csv/widechars.csv`，统一原版中
  “浮点 0.0 与 CRT 全零宽字符串被链接器合并”的标签，修复 24 个假性不匹配。
- **Player 结构体布局**：`startPos` 从 `PlayerBombInfo` 移到 `Player`，使
  `PlayerBombInfo` 步长恢复为原版的 `0xA142C`，修复 `BombData::BombReimuACalc`。
- **渲染分支方向**：修正 `AnmManager::SetRenderStateForVm` 的 `noVertexBuffers`
  分支方向，使其与原版二进制一致。
- **RegisterChain 栈帧**：修正 `MainMenu::RegisterChain`（移除多余 8 字节占位）
  与 `MusicRoom::RegisterChain`（补齐 24 字节栈对齐）。
- **静态局部变量符号**：在 `globals.csv` 中补充原版 `$S1`/`$S3` 符号，
  修复 `Supervisor::DrawFpsCounter` 与 `MusicRoom::RegisterChain` 的符号不匹配。

### 工具与构建

- `pyproject.toml` 加入 `ninja` 依赖，保证 `uv run` 环境可直接构建。
- 保存 reccmp 0.1.1 的本地补丁到 `patches/reccmp-parse.py`：
  - 间接调用（`call dword ptr [reg + disp]`）的位移归一化；
  - 乘法/位运算立即数不再被误标为巨型全局偏移。
- 新增 `resources/csv/widechars.csv` 数据源并在 `reccmp-project.yml` 注册。
- `.gitignore` 排除本地试玩数据、构建日志与 diff 工作文件。

### 分支说明

- `main`：v1.0.0，包含本 fork 的全部改动。
- `upstream`：上游 `some100/th07` 的 `main` 原样快照，便于对比差异。

### 已知剩余（11 个未 100% 函数）

- 5 个构造函数：`MusicRoom::MusicRoom`、`GuiMsgVm::GuiMsgVm`、`Enemy::Enemy`、
  `AsciiManager::AsciiManager`、`Effect::Effect`
- `EclManager::RunEcl`
- `ResultScreen::RegisterChain`
- `BulletTypeSprites::BulletTypeSprites`
- `Ending::ReadEndFileParameter`（atol 导入槽位差异）
- `GameWindow::Render`（编译器栈槽分配）
- `AnmManager::DrawInner`（行内汇编常量）

这些属于编译器行为复刻、结构体布局推导与导入表对齐问题，后续版本继续处理。
