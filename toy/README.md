# Toy 集成（B站）

把 th07 的 Emscripten 构建发布到 B站 Toy，并接入社区功能。

## 已就绪的部分

- `toy-bridge.js`：Toy JS SDK 桥接层，随 web 构建输出到 `build-web/toy-bridge.js`。
- `resources/shell.html`：加载 `toy-sdk.js` 与 `toy-bridge.js`；非 Toy 环境自动降级。
- `src/ResultScreen.cpp`：结算入口 `ResultScreen::RegisterChain` 通过 `EM_ASM`
  调用 `ToyBridge.onRunEnded(type, score, stage, difficulty, character, shotType)`。

## 榜单设计（board 1/2/3）

`submitScore` 分数范围仅 -16777216 ~ 16777215，而 th07 原始分数会超出上限，
所以必须用派生指标：

- board 1（分数榜）：`score / 1000` 取整。规则在页面注明“分数按千分位缩放入榜”。
- board 2（通关榜）：`stage * 100000 + clearType * 1000 + score / 100000`
  （clearType 语义由实现定义，如 0=未通关 / 1=普通通关 / 2=无续关）。
- board 3：预留（如 time attack / 最大擦弹）。

成绩提交必须由用户点击触发（首次会弹平台确认框），不要在结算页自动调用。
桥接层已提供 `ToyBridge.submitLastScore(board, scale)` 供按钮调用。

## 历史记录（history）

- 云存储 key：`history`，值为 JSON 数组，最多保留 20 条。
- 单条结构：`{ d: "YYYY-MM-DD", t: 结算类型, s: 原始分数, st: 到达关卡,
  df: 难度, c: 角色, stp: 机体 }`，控制在 1KB 内。
- 读取：`ToyBridge.getHistory()`。

## 录像（replay）

- 录像 `.rpy` 二进制大于云存储 1KB 上限，**不存云端**。
- 录像继续保存在本地 IDBFS（`/savesth07`，浏览器内持久化）。
- 分享建议：结算页用 canvas 生成战绩图 → App 内 `saveImageToAlbum`，
  Web 端 `<a download>`。桥接层已提供 `ToyBridge.shareImage(dataUrl)`。

## 待做

- 结算页 UI：登录/昵称显示、成绩上传按钮、榜单展示、历史列表、战绩图生成。
- 复现用完整录像的云端分享（需要自建服务，Toy SDK 不提供文件上传）。
- 确认 `ResultScreen::RegisterChain(type)` 中 type 的语义（1=通关结算，2=游戏结束）。

## 本地开发

- 本地 localhost 打开时 `window.toy` 不存在，桥接全部返回 null/[]，游戏不受影响。
- 真机验证需发布到 Toy 后，在 B站 App/Web 内打开。
