# Atom 跳绳小挑战 · C 参考实现（proto-v0.5-c）

**纯 C（C11）+ 纯软件光栅，零外部依赖，无 SVG/字体库需求** —— 设备只要给一块
466×466 ARGB8888 帧缓冲即可。桌面 `make && ./demo` 直接出关键帧 BMP 验证。

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
   storage 读写一个 44 字节档案 blob；millis 单调毫秒）；
2. 主循环 15Hz 调 `atom_app_tick()`；触摸按下调 `atom_app_touch(x, y)`；
3. AI 管线接三个入口（与 Web 版 `AtomApp.*` 契约一致）：
   - `atom_app_feed_pose(pts)` — 33 点骨骼（MediaPipe 拓扑，归一化 0-1，已镜像，≥15Hz）
   - `atom_app_on_jump()` — AI 判定一次有效跳跃（驱动计数/打击动画/涟漪）
   - `atom_app_set_presence(b)` — 站位判定（固件侧去抖；驱动开跳/暂停/恢复）
   事件上报走 `atom_callbacks_t.on_event`（session_start/jump/session_end 含卡路里）。

## 已完整实现 / 工程接力清单

✅ 训练屏全套：贴边进度环、顶点计时、格斗式打击数字（逢十重击帧序）、2/3 视口骨骼
火柴人（肩宽标尺缩放）、金闪触地涟漪（爆发扩张/压缩回弹）、限时测试（读秒/语音节点/
自动结算）、卡路里（MET）、体测评级（示意阈值，上线换国标分表）、档案持久化、庆祝/结算屏。

🔧 接力项（框架已留位）：首页三卡横滑与二级页（排行榜/勋章/记录/设置）目前是简化单屏；
连胜跨天判定（`atom_app_new_day()` 已留钩子）；火焰放射光（现为底部光晕）；中文文案
渲染（现用 5x7 ASCII，请接设备中文字库；语音文本已是 UTF-8 中文）；性能优化（软光栅
逐像素法在 MCU 上请换硬件 2D/脏矩形）。

⚠️ 本实现与 Web 原型（`../prototype/`）行为对齐，以 Web 版为交互规格源。
