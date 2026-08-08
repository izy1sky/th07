# Changelog

## [0.2.1] - 2026-08-09

### Debug 语义跳转重构

- `midboss` / `boss` 不再依赖“timeline 首指令是 opcode 2/3”的旧启发式：改为扫描 ECL
  子程序中真正的 boss 标记（指令 99），按 boss 首次出场定位；3~8 关此前完全找不到 boss
  的问题已修复；
- `stage` / `timeline` / `wave` / `spell` 等跳转统一为“重建当前关卡 + 快进所有
  timeline 到目标帧”，不再在旧场景里强插指令，避免画面生硬；
- `spell <n>` 在无 boss 在场时会先跳到最终 boss 再释放符卡；
- 每条跳转命令会在控制台打印目标 timeline 与帧号，方便核对。

## [0.2.0] - 2026-08-09

### Web 包体压缩（BGM 转 MP3 流式播放）

- 原版 `thbgm.dat`（约 444MB 原始 PCM）不再整体打包进浏览器资源；
- 新增 `tools/convert_bgm_web.py`：从原版数据提取 20 首 BGM，转成 192kbps MP3（约 60MB），
  并生成新的 `assets/bgm/thbgm.fmt` 与 16 字节占位 `assets/thbgm.dat`；
- `SoundPlayer` 在 Emscripten 构建下改用 miniaudio 解码 MP3，并按原版 intro/loop 循环点
  流式播放；桌面端仍走原始 PCM 路径；
- Web 资源包从约 455MB 降到约 93MB，满足 B站 Toy 140MB 上限。

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
- **FPS 显示**：改为显示固定 60Hz 逻辑帧率，而不是显示器渲染帧率；
  高刷屏下不再显示 120/160，游戏速度不受影响。

### 构建产物

- `th07.html` / `th07.js` / `th07.wasm` / `th07.data`（资源包，约 455MB，
  主要是 `thbgm.dat`；浏览器加载较大文件可能较慢，后续可做压缩）。

### 已知限制

- 文本渲染与原版仍有差异（上游 known issue）。
- 16bit 色、MIDI 等原版功能未实现（上游 known issue）。
- 大端平台无法读入关卡数据（上游 known issue）。
