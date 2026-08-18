# 工程实现参考（Atom 端 C + 后端）

**纯 C（C11）+ 纯软件光栅，零外部依赖，无 SVG/字体库需求** —— 设备只要给一块
466×466 ARGB8888 帧缓冲即可。桌面 `make && ./demo` 直接出关键帧 BMP 验证。

> 交互规格以可交互 Demo（[`../docs-and-demo/demo.html`](../docs-and-demo/demo.html)）为源——本参考实现已对齐 **2026-08-18 决议版**
> （即时开跳/三分钟出框/15 分钟上限/破纪录必庆祝/★每 50/60 格刻度环/火柴人最新规格/命名 JumpRope Star）,
> 细节以 Demo 与 [PRD](../docs-and-demo/jump-rope-mini-prd.md) 为准。后端可运行参考见 [`backend/`](backend/)。

## 目录

```
include/atom_hal.h    HAL 抽象（5 个函数指针：显示/语音/存储读写/时钟）→ 接设备 SDK
include/atom_app.h    应用公共 API（骨骼喂点/计数/站位/触屏/档案/事件出向）
src/atom_render.c     软光栅：粗圆头线/进度环弧/椭圆环/五角星/七段数字/5x7 标签字体（带 alpha 混合）
src/atom_app.c        状态机 + 各屏渲染 + 训练逻辑（限时/停表/星星/卡路里/体测评级/持久化）
src/atom_pose_sim.c   内置演示动作（无 AI 数据时自检用）
src/main_demo.c       桌面 harness（真机不编译此文件）
backend/              后端参考实现（FastAPI + Redis 单文件可跑,三榜/脱敏/校验/日卡,详见其 README）
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
| `atom_app_on_jump()` / `AtomApp.onJump()` | AI 判定一次有效跳跃 → 计数 +1，同帧触发数字打击动画/涟漪/发光/音效。识别口径：并脚跳（双脚同起同落），标称 ≤3 跳/秒；正对/侧对均支持、引导正对 |
| `atom_app_form_hint(kind)` / `AtomApp.formHint(kind)` | 动作纠错事件（2026-08-17 评审会新增）：`alternating` 交替跳 / `single_foot` 单脚 / `low_jump` 幅度不足 → 教练语音提醒（"双脚并拢跳"）；交替/单脚不计数；同类提醒 ≥20s 间隔 |
| `atom_app_set_presence(b)` / `AtomApp.setPresence(bool)` | **仅用于训练中**：`false` 立即暂停（停表），`true` 恢复；不作为开跳门槛（开跳为 3·2·1·BodyPark 即时流程） |
| 事件出向 `atom_callbacks_t.on_event` / `AtomBridge.onEvent` | `app_ready / session_start / jump{count} / session_end{count, ms, goal, testMin, kcal, newBest}` → 上报后端（字段说明见 [后端实现说明](backend-spec.md)） |

WebView 备选方案：设备 WebView（Chromium ≥90，Canvas 2D）全屏加载 `docs-and-demo/demo.html?app=1`
（`app=1` 隐藏手册页签外壳，只剩纯 Demo）即可运行——用 `webview.evaluateJavascript("AtomApp.onJump()")` 等注入上表 JS 接口,
`AtomBridge.speak/saveProfile/onEvent` 由固件在页面加载前注入（缺失时页面自动回落浏览器能力）。
桌面调试加 `?debug`。

## 已完整实现 / 工程接力清单

✅ 状态机与训练全套（2026-08-18 决议版）：**3·2·1·BodyPark 即时开跳**（无站位校验，
presence 仅训练中）、出框/切后台**超 3 分钟自动结算**、自由跳 **15 分钟上限**（前 1 分钟
语音预告）、**空轮保护**（0 下不落档直接回首页）、限时 1'/2'（**60 格刻度环**，逢五长刻度，
最后 10s 转暖橙）、**破纪录追逐**（差 ≤10 播报/越线即时反馈）与**必庆祝规则**（破纪录/达标/
时间到）、★固定每 50（>4 收敛 ★×N）、货币星整 20+2 达标+10、计数 ≥1000 降档 96→68、
火柴人**最新规格**（躯干 40=2×腿宽/臂 18/头 r26 上移 17/手白 r9/脚白 r14/肩锚 颈±11/
髋锚 髋中±10/底边锚定 0.94）、金闪涟漪、`formHint` 纠错接口（4 种 + 20s 频控）、
结算自我对比行（超越/差距/首纪录）、今日累计/今日最高/轮数/最长连胜落档、卡路里（MET）、
体测评级（示意阈值，上线换国标分表）、档案持久化与档位归一（目标 50/100/200）。

🔧 接力项（框架已留位）：首页四卡横滑与二级页组（排行榜三榜/打卡记录分页/最佳战绩 7 维度/
设置 6 行）、日卡渲染与云相册上报、动作要领幻灯片、虚拟绳与逢十金光、BGM 合成与闪避、
开场白拼装、双语文案切换；连胜跨天判定以服务端为准（`atom_app_new_day()` 钩子 +
backend/ 已实现云侧逻辑）；中文字库（现用 5x7 ASCII，语音文本已是 UTF-8 中文）；
性能优化（软光栅逐像素法在 MCU 上请换硬件 2D/脏矩形）。
