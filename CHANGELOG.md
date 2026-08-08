# Changelog

## [0.1.0] - 2026-08-08

### Web port（浏览器移植）

基于上游 `reallyportable` 分支（SDL3 + OpenGL ES + Emscripten）的浏览器版本，
目标是“游玩体验与原版一致”，并针对 Web 输入延迟做了增强。

### 输入延迟优化

- **按键边沿缓冲**：`SDL_EVENT_KEY_DOWN` 立即记录按下事件，合并进下一个
  60Hz 逻辑 tick，快速点按不再因为两次 tick 之间按下又松开而丢失。
- **浏览器按键接管**：shell 页面聚焦画布，并对方向键、空格、z/x/shift、
  Escape、小键盘等游戏键调用 `preventDefault()`，避免页面滚动和浏览器
  快捷键抢输入。
- **触摸控制**：沿用事件驱动的触摸路径（上游已有），手指位移增量每个
  逻辑 tick 精确消费一次。

### 构建产物

- `th07.html` / `th07.js` / `th07.wasm` / `th07.data`（资源包，约 455MB，
  主要是 `thbgm.dat`；浏览器加载较大文件可能较慢，后续可做压缩）。

### 已知限制

- 文本渲染与原版仍有差异（上游 known issue）。
- 16bit 色、MIDI 等原版功能未实现（上游 known issue）。
- 大端平台无法读入关卡数据（上游 known issue）。
