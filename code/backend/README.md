# 后端参考实现（FastAPI + Redis，单文件可跑）

> 规格来源：[`../backend-spec.md`](../backend-spec.md)（字段/口径/伪代码），本目录是其**可运行参考**。
> 拿过去"能用多少用多少"——鉴权、对象存储、推送通道、App 云相册接口按团队现有设施替换 TODO。

## 运行

```bash
pip install -r requirements.txt
redis-server &                 # 或 REDIS_URL=redis://host:port/db
uvicorn app:app --reload       # http://localhost:8000/docs 看 OpenAPI
python app.py smoke            # 无需真 Redis 的内置冒烟自测（fakeredis）
```

## 接口一览（全部要求 `X-Account-Edition: cn|intl` 头，双区数据永不合并）

| 接口 | 说明 |
|---|---|
| `POST /v1/jump/session_end` | 结算入口：校验（空轮拒收 / free ≤30min / timed 1'·2' 且 ≤时长 / >8 跳每秒剔除）→ 日聚合(count·best·n·ms·done·manual) + session 明细（一条 mini-workout，独立打标 `jump_rope`，对齐 oneset）→ 月度榜写入 `ZADD GT` → 打卡/连胜（服务端时间）；跨零点按 session 开始时间归天；单日 3000 封顶后入档但不上榜/不给星 |
| `GET /v1/jump/board/{free·timed1·timed2·streak}` | 三榜（8-18 定稿:自由单轮 / 1 分钟 / 连续打卡，按月度刷新；2' 月榜就绪、入口 P1）：Top3 + 我的名次 + 上一名（供"再跳 N 下超过 XX"）；同分按显示名首字母（读侧二次排序） |
| `POST /v1/jump/display_name` | 显示名登记：昵称 ≤10 截断；未填 → 邮箱/手机前缀 3 位 + `***` |
| `POST /v1/jump/form_hint` | 纠错事件落库（alternating / single_foot / low_jump / low_light）；8-18:V1 暂不启用常规纠错，接口保留做 AI 规则调优数据 |
| `POST /v1/jump/daily_card` | 日卡 PNG 上传：同日按内容 hash 去重 → 对象存储 → **自动写入手机 App 云相册**（App 侧接口，TODO） |
| `POST /v1/jump/result_card` | **结果卡自动同步（8-18）**：每次训练完成端上渲染 466×466 结果卡上传 → App 训练记录 + 云相册；按 session hash 去重 |
| `POST /v1/jump/record_backfill` | **追加记录（8-18）**：手动补记近 7 天（单次 ≤1000，数值待复核）；仅累加 count/manual/total，不写 best、不上榜、不追溯连胜 |
| `POST /v1/jump/opening_tts` | **今日开场白 TTS 预生成**（音频策略 §4.5）：进 APP 时预取、按 日期+语言+文本 hash 缓存；TTS 供应商接入 TODO |
| `push_streak_reminders()` | 连胜提醒定时任务（每分钟扫到点用户，当日未打卡则推送；通道待定 PRD #3） |

## Key 布局

```
{edition}:jump:profile:{uid}      档案 JSON（history 按天聚合）
{edition}:jump:log:{uid}          session 明细 List（profile_log_jump_rope_*）
{edition}:jump:free:{yyyymm}      自由单轮当月最高榜（ZSET GT，按月建 key = 月度刷新，TTL 62d）
{edition}:jump:timed{1|2}:{yyyymm} 限时当月最高榜（1' 主推上屏;2' 就绪、入口 P1）
{edition}:jump:streak:{yyyymm}    连续打卡榜（结算任务维护，按月刷新）
{edition}:jump:rcard:{uid}:{ts}   结果卡去重 hash
{edition}:jump:names              uid → 脱敏显示名
```

## 待接力（TODO in code）

鉴权中间件（账号服务 token）· PNG 落对象存储 + App 云相册写入 · 推送通道 ·
影子封禁与置信度分 · 体测国标分表（评级在端上，服务端复算校验时同步替换）。
