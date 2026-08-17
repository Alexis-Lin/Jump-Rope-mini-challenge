# 嵌入式实现参考（Atom 端前端）

**纯 C（C11）+ 纯软件光栅，零外部依赖，无 SVG/字体库需求** —— 设备只要给一块
466×466 ARGB8888 帧缓冲即可。桌面 `make && ./demo` 直接出关键帧 BMP 验证。

> ⚠ 交互规格以可交互 Demo（[`../docs-and-demo/demo.html`](../docs-and-demo/demo.html)）为源——本参考实现基于 proto-v0.5 行为,
> 后续界面演进(双语/新首页/BodyPark 即时开跳/刻度环/发光等)以 Demo 与 [PRD](../docs-and-demo/jump-rope-mini-prd.md) 为准接力。

## 目录

```
include/atom_hal.h    HAL 抽象（5 个函数指针：显示/语音/存储读写/时钟）→ 接设备 SDK
include/atom_app.h    应用公共 API（骨骼喂点/计数/站位/触屏/档案/事件出向）
src/atom_render.c     软光栅：粗圆头线/进度环弧/椭圆环/五角星/七段数字/5x7 标签字体（带 alpha 混合）
src/atom_app.c        状态机 + 各屏渲染 + 训练逻辑（限时/停表/星星/卡路里/体测评级/持久化）
src/atom_pose_sim.c   内置演示动作（无 AI 数据时自检用）
src/main_demo.c       桌面 harness（真机不编译此文件）
```

## 移植三步

1. 实现 `atom_hal_t` 五个函数（display_flush 把帧缓冲刷 LCD；speak 接 TTS/语音包；
   storage 读写档案 blob；millis 单调毫秒）；
2. 主循环 15Hz 调 `atom_app_tick()`；触摸按下调 `atom_app_touch(x, y)`；
3. AI 管线接三个入口（接口契约见下节）。

## AI 管线接口契约（C 与 WebView 方案通用）

| 接口（C / JS） | 说明 |
|---|---|
| `atom_app_feed_pose(pts)` / `AtomApp.feedPose(pts)` | 33 点骨骼流（MediaPipe Pose 拓扑，`[{x,y,v?}]` 归一化 0–1、原点左上、喂入前完成镜像），建议 ≥15Hz；500ms 无数据自动回落内置演示动作。渲染细节见 [火柴人渲染说明](../docs-and-demo/figure-rendering.md) |
| `atom_app_on_jump()` / `AtomApp.onJump()` | AI 判定一次有效跳跃 → 计数 +1，同帧触发数字打击动画/涟漪/发光/音效 |
| `atom_app_set_presence(b)` / `AtomApp.setPresence(bool)` | **仅用于训练中**：`false` 立即暂停（停表），`true` 恢复；不作为开跳门槛（开跳为 3·2·1·BodyPark 即时流程） |
| 事件出向 `atom_callbacks_t.on_event` / `AtomBridge.onEvent` | `app_ready / session_start / jump{count} / session_end{count, ms, goal, testMin, kcal, newBest}` → 上报后端（字段说明见 [后端实现说明](backend-spec.md)） |

WebView 备选方案：设备 WebView（Chromium ≥90，Canvas 2D）全屏加载 `docs-and-demo/demo.html?app=1`
（`app=1` 隐藏手册页签外壳，只剩纯 Demo）即可运行——用 `webview.evaluateJavascript("AtomApp.onJump()")` 等注入上表 JS 接口,
`AtomBridge.speak/saveProfile/onEvent` 由固件在页面加载前注入（缺失时页面自动回落浏览器能力）。
桌面调试加 `?debug`。

## 已完整实现 / 工程接力清单

✅ 训练屏全套：贴边进度环、顶点计时、格斗式打击数字（逢十重击帧序）、2/3 视口骨骼
火柴人（肩宽标尺缩放）、金闪触地涟漪（爆发扩张/压缩回弹）、限时测试（读秒/语音节点/
自动结算）、卡路里（MET）、体测评级（示意阈值，上线换国标分表）、档案持久化、庆祝/结算屏。

🔧 接力项（框架已留位）：对齐最新 Demo 的首页三卡横滑与二级页（排行榜/记录/最佳战绩/设置）、
3·2·1·BodyPark 即时开跳、限时 60 格刻度环、逢十发光、绳色池、双语文案；连胜跨天判定
（`atom_app_new_day()` 已留钩子）；中文字库（现用 5x7 ASCII，语音文本已是 UTF-8 中文）；
性能优化（软光栅逐像素法在 MCU 上请换硬件 2D/脏矩形）。
