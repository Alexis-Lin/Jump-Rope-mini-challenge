# Atom 嵌入式版本（proto-v0.5-embed）

单文件 `index.html`，在设备的嵌入式 WebView（Chromium 内核 ≥ 90，需支持 Canvas 2D；WebAudio/SpeechSynthesis 可选）中全屏加载即可运行。UI 按 466×466 圆屏 @15fps 设计，自动铺满 `100vmin`。

**真机模式下计数/骨骼/站位完全由后端 AI 驱动**，页面内不做任何判定；原型的模拟输入（点按跳/空格/自动演示/控制条）默认封死，URL 追加 `?debug` 可恢复（桌面调试用）。

## 接口总览

### 入向：固件/AI 管线 → 页面（`window.AtomApp`）

| 方法 | 说明 |
|---|---|
| `AtomApp.feedPose(pts)` | 33 点骨骼流（MediaPipe Pose 拓扑，`[{x,y,v?}]` 归一化 0–1、原点左上、喂入前完成镜像），建议 ≥15Hz；500ms 无数据自动回落内置演示动作 |
| `AtomApp.onJump()` | AI 判定一次有效跳跃 → 计数 +1，同步触发数字打击动画/涟漪/音效 |
| `AtomApp.setPresence(bool)` | 站位判定（固件侧去抖后调用）：准备页 `true` 触发倒计时开跳；跳绳中 `false` 暂停、`true` 恢复 |
| `AtomApp.setProfile(json)` | 注入用户档案（开机时用设备存储的档案恢复） |
| `AtomApp.setBoard(scope, list)` | 注入排行榜数据，`scope: 'global'\|'local'`，`list: [{name, count}]` 降序；未注入时用内置示意数据 |
| `AtomApp.start(mode)` | 编程式开跳：`'timed'`（限时）/ 其它（每日任务），供语音助手/物理按键调用 |
| `AtomApp.home()` / `AtomApp.end()` | 回首页 / 立即结算 |

调用方式：原生侧 `webview.evaluateJavascript("AtomApp.onJump()")`（Android）或等价机制（QtWebEngine `runJavaScript` / iOS `evaluateJavaScript`）。

### 出向：页面 → 固件（`window.AtomBridge`，固件在页面加载前注入）

| 方法 | 说明 | 缺失时回落 |
|---|---|---|
| `AtomBridge.speak(text)` | 教练语音 → 设备 TTS / 预生成语音包 | 浏览器 SpeechSynthesis |
| `AtomBridge.saveProfile(json)` | 档案持久化到设备存储 | localStorage |
| `AtomBridge.onEvent(evt)` | 事件流：`app_ready` / `session_start` / `jump{count}` / `session_end{count, ms, goal, testMin, kcal, newBest}` —— 用于上报后端（排行榜/打卡云同步） | 静默 |

所有出向调用都有 try/catch 与回落，桥不完整不会影响页面运行。

## 联调建议

1. 桌面浏览器打开 `index.html?debug`，控制台手动 `AtomApp.feedPose(...)` / `AtomApp.onJump()` 验证坐标系与计数链路；
2. 真机首验三项：骨骼渲染延迟与计数反馈延迟对齐（均 <100ms）、15fps 下动画无掉帧、TTS 桥语音时机正确；
3. 排行榜/档案云同步就绪前，可先只接 `onEvent` 埋点收数。
