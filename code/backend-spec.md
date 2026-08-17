# 后端字段与服务实现说明

> 对象:服务端工程。配合 [PRD](../docs-and-demo/jump-rope-mini-prd.md) §8 使用;事件由端上经 `on_event`/`AtomBridge.onEvent` 上报(接口见同目录 [README](README.md))。

## 1. 用户档案字段(单账号;端上本地持久化 + 云同步)

| 字段 | 类型 | 含义 | 更新时机 |
|---|---|---|---|
| `nick` | string | 昵称(开场白称呼) | 手机 App/云端设置(设备端无键盘);空时端上用默认"小跳将/Jumper" |
| `sessions` | int | 累计跳绳轮次(开场白"第 N 次") | 每次结算 +1 |
| `streak` | int | 连续打卡天数 | 结算时:昨日已打卡 +1;断签重置为 1 |
| `lastDone` | date | 最近打卡日期 | 当日首次达成打卡条件时 |
| `history` | map<date, {count, best, n, ms, done}> | 每日聚合:累计次数 / **单轮最高 best(榜单口径)** / 轮数 n(仅数据,不上屏) / 累计净时长 ms / 是否打卡 | 每次结算累加(**单轮从 0 计,当日跨轮累加**;best 取 max);按 session 存明细、按天聚合展示;打卡条件 = 当日 ≥ 目标 或 ≥50 |
| `total` | int | 历史累计跳数 | 每跳 +1(结算落档) |
| `stars` | int | 星星余额(软货币) | 结算时 + 本轮 earned(整 20+2/彩蛋 ×2/隐藏数字+5/达标+10);未来消耗于换装/场景/补签卡 |
| `bestSession` | int | 单轮最多(PB) | 结算时取 max |
| `bests` | map<min, count> | 限时测试最佳(key=1/2 分钟) | 限时结算破纪录时 |
| `goalNum` | int | 每日目标(100/200/300;0=自由跳) | 设置页 |
| `testMin` | int | 限时时长(1/2) | 设置页 |
| `age` | enum | 年龄段('6-8'/'9-11'/'12-14'/'15-17'/'成人') | 设置页;体测评级用 |
| `weight` | int kg | 体重 | 设置页;卡路里用 |
| `remind` | enum | 每日提醒('关'/19:00/20:00/21:00) | 设置页;推送服务读取 |
| `luckyHits` | int | 踩中隐藏数字次数 | 课中踩中时 |
| `scenes` | int | 已解锁场景数(初始 3,上限 6) | 达标结算时 +1 |
| `outfit` | int | 当前装扮索引(按 total 解锁:100/300/500/1000/2000) | 入口卡点按切换 |
| `lastGreet` | date | 开场白当日去重标记 | 进入首页播完整开场白时 |

## 2. 端上事件(上报最小集)

| 事件 | 载荷 | 说明 |
|---|---|---|
| `session_start` | `{mode}` | mode: timed / free |
| `jump` | `{count}` | 每跳;弱网可 10 条聚合 |
| `session_end` | `{count, ms, goal, testMin, kcal, newBest}` | **结算即上报**(排行榜/云档案入口);ms 为净时长(暂停不计)。**数据模型(评审会定稿):一次 session = 一节单组课(对齐 oneset/workout),落一条训练记录;打卡记录按 session 存储、按天聚合展示** |
| `form_hint` | `{kind}` | 动作纠错事件(评审会新增):alternating 交替跳 / single_foot 单脚 / low_jump 幅度不足;端上触发教练语音,上报做规则调优 |

埋点扩展:`app_open, jump_10x, goal_hit, streak_day{n}, board_view, overtake{rank}, share_flip, setting_change{key}`。

## 3. 排行榜服务(2026-08-17 评审会定稿)

- 结算上报 `{userId, date, count, ms, mode, testMin}`;
- **三榜**,全员默认参与、不设退出项(同城/local 取消):

| 榜 | 口径 | Key(前缀含分区) | 重置 |
|---|---|---|---|
| 今日单轮 | 当日**单轮最高**(当日累计不上榜,防刷) | `{edition}:jump:round:{date}` | 时区滚动建 key + TTL,天然每日 0 点重置 |
| 1 分钟 | 1' 测试**历史最佳**;限时时长各自成榜(2' 用 `timed2` 单独 key) | `{edition}:jump:timed1:best` | 不重置 |
| 连胜 | 连续打卡天数 | `{edition}:jump:streak` | 断签回落(每日结算任务刷新) |

- **Redis Sorted Set**:写入用 `ZADD GT`(只升不降=天然取最高,替代 ZINCRBY);`ZREVRANK` 查名次、`ZREVRANGE` 取 Top3/邻近区间;
- **同分并列**:按显示名首字母升序(读侧二次排序;或 member 编码 `score|name` 归一);
- **显示名(脱敏,评审会定稿)**:昵称 ≤10 字符截断;未填昵称 → 邮箱前 2–3 位小写 + `***`(手机号同理;不露域名/后段);未填昵称者语音不点名;
- 榜单读接口:Top3 + 我的名次 + 上一名(供"再跳 N 下超过 XX");
- **防刷**:单轮口径本身杜绝当日累计刷榜;另有端上计数置信度分 + 单日封顶 3000 + 频率异常剔除(>8 跳/秒;识别标称上限 ≈3 跳/秒,超出即可疑)+ 影子封禁;
- 弱网:端上缓存上次榜单展示,结算后异步补报。

## 4. 计算公式(端上实现,后端复算校验)

- **卡路里(MET 法)**:跳频 <100/分 → MET 8.8;100–120 → 11.8;>120 → 12.3;`kcal = MET × 体重kg × 净时长h`;
- **体测评级**(仅 1 分钟测试):按年龄段阈值出 继续加油/及格/良好/优秀/满分。**⚠ 当前阈值为示意,上线前替换《国家学生体质健康标准》官方分表(年级 × 性别),表结构同现有 5 档**;
- **星星**:荣誉星(屏显)固定每 50 一颗,不入账;货币星见档案 `stars` 行;数值需运营复核(PRD §12)。

## 5. 推送(连胜提醒)

`remind` 非关时,每日到点检查:当日未打卡 → 推送("连胜还剩 N 小时");通道待定(设备端/手机 App/微信服务号,PRD 开放问题 #3)。

## 6. 与 ATOM 用户档案模型的字段对齐

> 账号级字段以 [ATOM-UserGoalPreference-and-OnBoarding](https://github.com/Alexis-Lin/ATOM-UserGoalPreference-and-OnBoarding) 的档案模型为唯一事实源(IDENTITY / GOAL / PREF 已定稿);本应用的设备端档案是其**局部缓存 + 应用私有数据**。对齐关系:

| 本应用字段 | 档案模型归属 | 说明 |
|---|---|---|
| `nick` | `identity.account.username`(昵称) | 云端为源,设备端只读缓存;开场白称呼用 |
| `age`(5 档年龄段) | 派生自 `identity.personal.birth_date` | 设备端不独立采集生日,由云端下发年龄段;体测评级用 |
| `weight` | `identity.personal.weight` | 云端为源(F 信封,含 history);设备端缓存,本地修改需回写 |
| `goalNum / testMin` | `goal.goals[]`(跳绳目标条目)| 每日跳绳目标建议登记为一条 goal(code=jump_rope_daily),端上值为其执行参数 |
| `remind` | PREF(影响教练行为 → 归 PREFERENCE,非 SETTINGS) | 按模型"分界两问"判定 |
| `voiceOn / 语言` | `app_settings` / `device_config` | 纯设备行为配置 |
| `streak / history / total / stars / bests / bestSession / sessions / luckyHits / scenes / outfit` | STATUS / LOG 域(🚧 模型草案中) | **暂存本应用私有命名空间**,待 STATUS/LOG 定稿后迁移;工程资产命名遵守模型的机械前缀规则:`profile_log_jump_rope_*` |
| 排行榜分区 | `account_edition`(cn/intl) | ~~同城榜~~已取消(评审会决议);榜单只按分区隔离,不再用 city |

**账号铁律的两条直接影响**(源自档案模型 v5.28):① 任何按邮箱查用户必须带 `account_edition` 条件(cn/intl 双区数据不互通);② 排行榜/云档案的 key 前缀含分区(如 `cn:jump:2026-08-14:global`),跨区永不合并。

## 7. 实现伪代码参考

### 7.1 结算处理(收到 `session_end`)

```
handle_session_end(user, ev):            # ev = {count, ms, goal, testMin, kcal, newBest, mode, city, date}
    assert ev.count <= 3000 and rate_ok(ev)          # 防刷:单日封顶 + 频率校验(>8跳/秒剔除)
    rec = profile_log.day(user, ev.date) or {count:0, ms:0, done:false}
    rec.count += ev.count                            # 今日累计跨轮累加(单轮从 0 起算)
    rec.ms    += ev.ms
    if not rec.done and (rec.count >= user.goalNum or rec.count >= 50):
        rec.done = true
        user.streak = user.streak + 1 if user.lastDone == ev.date - 1d else 1
        user.lastDone = ev.date
    user.total    += ev.count
    user.stars    += ev.earned                       # 星星经济:整20+2 / 彩蛋×2 / 隐藏+5 / 达标+10(端上算,服务端复核)
    user.sessions += 1
    user.bestSession = max(user.bestSession, ev.count)
    if ev.mode == timed: user.bests[ev.testMin] = max(user.bests[ev.testMin], ev.count)
    kcal_check(ev)                                   # 服务端复算 MET 公式,偏差 >10% 打标
    leaderboard_incr(user, ev)                       # 见 7.2
    save(rec, user)
```

### 7.2 排行榜(Redis)

```
# 三榜 key(评审会定稿;edition 分区,跨区永不合并)
round_key(date, edition)  = f"{edition}:jump:round:{date}"    # 今日单轮最高,TTL 48h
timed_key(min, edition)   = f"{edition}:jump:timed{min}:best" # 限时历史最佳,永久
streak_key(edition)       = f"{edition}:jump:streak"          # 连胜天数,结算任务维护

on_session_end(user, ev):
    ZADD round_key(ev.date), GT, ev.count, user.id   # GT=只升不降 → 天然"当日单轮最高"
    EXPIRE round_key(ev.date), 48h                   # TTL 覆盖时区跨日,天然每日重置
    if ev.mode == timed:
        ZADD timed_key(ev.testMin), GT, ev.count, user.id
    ZADD streak_key(), user.streak, user.id          # 结算后连胜值直接覆盖

board_view(user, board):                             # 返回 Top3 + 邻近区间
    rank  = ZREVRANK key(board), user.id
    top3  = ZREVRANGE key(board), 0, 2, WITHSCORES
    near  = ZREVRANGE key(board), max(0, rank-1), rank+1, WITHSCORES
    above = near[0] if rank > 0
    # 同分并列:读侧按 display_name 首字母二次排序
    return {top3, rank, me: score, chase: above.score - score + 1, chase_name: display_name(above)}

display_name(user):                                  # 脱敏(评审会定稿)
    if user.nickname: return user.nickname[:10]      # ≤10 字符,超出截断
    src = user.email or user.phone                   # 至少有其一
    return lower(src[:3]) + "***"                    # 街机三字母风;不露域名/手机后段
```

### 7.3 连胜提醒推送(每日定时)

```
push_streak_reminder():                              # 每分钟扫描到点用户(remind 时区化)
    for user where user.remind == now.hhmm:
        rec = profile_log.day(user, today)
        if rec == null or not rec.done:
            hours_left = midnight(user.tz) - now
            push(user, f"连胜 {user.streak} 天保卫战:还剩 {hours_left} 小时")
```

### 7.4 端上口径(对齐用)

- 星星:**屏显荣誉星 = floor(count/50),不入账**;入账的是 earned(货币星);
- 净时长 ms 不含暂停;自由跳 15 分钟上限由端上执行,服务端校验 free 的 ms ≤ 15min、timed 的 ms ≤ testMin;
- `newBest` 仅限时模式有意义(按 testMin 分别记录)。
