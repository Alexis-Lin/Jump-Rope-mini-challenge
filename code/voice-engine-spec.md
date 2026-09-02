# 跳绳小程序 · 语音引擎工程实现方案

> 版本 v1.0（2026-08-31）· 依据《audio-voice-strategy.md》v0.4 与《voice-script-list.md》v1.0
> 读者：设备端（ATOM）与服务端工程师。Demo 参考实现见 `docs-and-demo/demo.html`（JS，逻辑同构）。

---

## 0. 职责划分（一句话版）

- **设备端**：事件产生 → 候选生成 → 仲裁 → 混音播放。全部实时逻辑在端上，弱网/离线不影响课中语音。
- **服务端**：训练开始前下发「**记忆快照**」（昨日成绩、PB、连胜、断练天数…）+ 预生成动态 TTS（开场白/结算句）+ 固定语音包分发与版本管理。

> **8-31 拍板：播报逻辑全量端上。** 服务端不参与任何实时决策——课中每一句「说不说、说哪句、要不要打断」都由端上仲裁引擎离线可算。云端只做三件慢事：① 开轮前记忆快照 ② 动态整句 TTS 预生成 ③ 语音包/话术表 OTA。三件全部失败时的降级链：记忆类静默 → 动态句回落无变量兜底句 → 固定包按出厂版本播——课中体验永不因网络掉档。

## 1. 总体架构

```
[计数管线/状态机/计时器/档案]                 设备端
        │  产生 VoiceEvent(intent, params)
        ▼
[候选生成器]  查话术表(voice-script-list) → Candidate{...}
        ▼
[仲裁引擎]   冷却/限次/过期过滤 → priority 最高者 → 抢占判定
        ▼
[播放器]     L1音效(独立通道,永不打断) / L2人声(单通道) / L3 BGM(duck -50%)
        ▲
[资产层]     固定opus包(本地) + 模板拼接(数字+句段) + 云TTS缓存(开场白/结算)
        ▲
[服务端]     /voice/memory-snapshot  /voice/tts-pregen  语音包 OTA
```

### 1.1 决策逻辑树（抽象版，每个语音时机走同一棵树）

```
触发源(每一跳 / 250ms 看门狗 / 轮次生命周期)
 └→ 候选生成器:查话术表 voice_table.json
     (每条候选带 重要性/紧迫度/有效期/冷却键/每轮限次键;四大类 🛡️⏱️🎯❤️)
     ├─ 安静模式开 且 非安全类 ──────────────→ 弃
     ├─ 过期? 冷却中? 本轮限次满? ──是──────→ 弃
     └─ 否 → 优先级 = 重要性 × 紧迫系数(NOW1.5/SOON1.0/随时0.6) → 取最高 top
              ├─ 无句在播 ───────────────→ ▶ 播 top
              ├─ top 高出在播 >20 分 ─────→ ⏹ 掐断 → 改播 top(抢占)
              └─ 否则 ──────────────────→ 丢弃,不排队(= 默认沉默)
 播出后登记: 同类冷却计时 · 每轮限次 +1 · 记忆去重日志(48h)
 横切约束: 记忆对比类每轮总预算 ≤3 条
```

同一棵树的图形版见 Demo 手册「语音播报策略」页签。

## 2. 核心数据结构（C 风格示意）

```c
typedef enum { URG_NOW, URG_SOON, URG_WHENEVER } urgency_t;   /* 系数 1.5 / 1.0 / 0.6 */

typedef struct {
    const char *intent;        /* 事件意图,如 "yd_pass" "combo_star",对应话术表行号 */
    int   importance;          /* 0–100,查表 */
    urgency_t urgency;
    int   ttl_ms;              /* 有效期:过期即丢(默认 2000;计数类 800) */
    const char *cooldown_key;  /* 同类冷却键,如 "count10" "cheer" */
    int   cooldown_ms;
    const char *quota_key;     /* 每轮限次键,NULL=不限;如 "steady"(≤2) "yd_pass"(≤1) */
    int   params[4];           /* 槽位参数(计数/差值/天数…) */
    int64_t born_ms;
} candidate_t;

typedef struct {
    /* 冷却表 + 每轮限次计数器,session_start 时 quota 清零 */
    map_t cooldown_until;      /* key → 到期时间戳 */
    map_t quota_used;          /* key → 已用次数 */
    candidate_t playing;       /* 当前在播(无则 intent=NULL) */
    int   preempt_threshold;   /* 抢占阈值,默认 20 */
} arbiter_t;
```

**话术表本身是数据不是代码**：`voice_table.json` 由 `voice-script-list.md` 生成（intent → imp/urg/ttl/冷却/限次/变体列表/生成方式），OTA 可更新，端上代码只认 intent。

## 3. 仲裁引擎（照抄可用）

```c
void arbiter_offer(arbiter_t *a, candidate_t *cands, int n) {
    candidate_t *top = NULL;
    float top_p = 0;
    for (int i = 0; i < n; i++) {
        candidate_t *c = &cands[i];
        if (now_ms() - c->born_ms > c->ttl_ms) continue;                  /* R5 过期丢 */
        if (map_get(a->cooldown_until, c->cooldown_key) > now_ms()) continue;  /* R6 冷却 */
        if (c->quota_key && map_get(a->quota_used, c->quota_key) >= quota_max(c->quota_key)) continue;
        float p = c->importance * COEF[c->urgency];
        if (p > top_p) { top_p = p; top = c; }                            /* R1/R2 单句互斥取最高 */
    }
    if (!top) return;
    if (a->playing.intent == NULL) { play(a, top, top_p); return; }
    float cur_p = a->playing.importance * COEF[a->playing.urgency];
    if (top_p - cur_p > a->preempt_threshold) {                           /* R3/R4 高出阈值才打断 */
        tts_stop(); play(a, top, top_p);
    }
    /* 否则丢弃,不排队(R4 后半:排队会导致"事件过去了才说") — R7 默认沉默 */
}
```

- **安全类豁免**：`intent` 前缀 `safety_` 的候选绕过安静模式开关（quiet mode 只关 L2/L3，安全句仍播）。
- **计数让位**：整十报数 ttl 800ms 且 importance 55——任何 60+ 事件同帧自然压过它，无需专门互斥代码（Demo 里的 `comboCele` 标志即此规则的手写版，正式实现交给仲裁）。
- 每次 `play()` 记冷却 + 限次 + `said_log`（记忆去重用，见 §5）。

## 4. 事件源接入点（对照 Demo）

| 触发点 | 产生的 intent | Demo 参考 |
|---|---|---|
| `registerJump()` 每跳 | count10 / count30 / yd_pass / goal_near / pb_near / pb_break / steady / combo_star / blood2 / goal_hit / lap_again / lucky / overtake | demo.html `registerJump` |
| 250ms 看门狗 | combo_break_comfort / test_30s / test_10s / cap_soon / long_run | `setInterval(…,250)` |
| `pauseSession()` | safety_away（出框）/ paused_tap（手动） | `pauseSession` |
| `endSession()` | summary（收尾+对比句拼接） | `endSession` |
| 发令后 | opening（M1/M2/M3 三选一） | `standLocked → buildOpening` |
| 恭喜页 | new_record_span | `celebrationShow.speak` |

## 5. 记忆系统（「它记得住我」的数据底座）

### 5.1 需要的用户历史数据（→ 回答"是否需要历史记录"：**是，必须**）

服务端按账号持久化（与 backend-spec.md 档案模型合并，勿另建库）：

| 字段 | 用途 | 已有/新增 |
|---|---|---|
| `history[date] = {count, best, n, ms, manual}` | 昨日对比 M4/M5、周小结、回归判定 | 已有 |
| `best_session` + **`best_at`（日期）** | PB 追逐 M7/M8、**纪录跨度 M6** | `best_at` 新增 |
| `bests{1,2}` + `bests_at{1,2}` | 限时 PB 与其跨度 | `bests_at` 新增 |
| `streak / max_streak / last_done` | 开场亮点、连胜 | 已有 |
| `sessions / last_greet` | 第 N 次、当日首轮判定 | 已有 |
| `last_active_date`（最后一次 count>0 的日期） | **回归关怀 M2 的断练天数** | 由 history 推导即可 |
| `combo_max_alltime` | 后续"最高连击纪录"类记忆 | 新增（可选） |
| `memory_said_log[{intent, date}]` | 48h 同类记忆不重复 | 新增（端上即可，7 天滚动） |

**补记（manual）口径建议**：越昨对比 M4/M5 使用 `count - manual`（只比实测），避免"补记 500 下"导致第二天怎么跳都"没超过昨天"的挫败；结算页累计仍含补记。⚠️ 待产品确认后写死。

### 5.2 快照下发

`session_start` 前设备拉取（或服务端随启动配置推送）：

```json
GET /voice/memory-snapshot →
{ "nick": "小雨", "sessions": 17, "streak": 6,
  "yesterday_count": 40, "best_session": 187, "best_at": "2026-08-21",
  "days_since_active": 0, "total": 4120,
  "opening_tts": { "full": "cache_key_a", "short": "cache_key_b", "ready": true } }
```

离线：端上用本地缓存的上次快照；快照缺失 → 记忆类事件全部静默（策略"说不出就沉默"）。

### 5.3 呈现槽位与选择

- 开场（M1/M2/M3 三选一，互斥）→ 课中瞬时（M4 昨日、M7 PB，各每轮 1 次）→ 结算（M5「当天最特别」：越昨 > 最高连击 > 沉默）→ 恭喜页（M6 跨度）。
- 每轮记忆类总预算 **≤3 条**；同一 intent 48h 内不重复（查 `memory_said_log`）。

## 6. 内容生产管线

1. **固定 opus 包**（~190 条/语言）：构建脚本从 `voice_table.json` 拉出所有无槽位变体 + 数字 99 条 + 池 A/B/C → 提交录音/批量 TTS → 打包 OTA。命名 `{intent}_{variant}_{lang}.opus`。
2. **模板拼接**（端上实时）：`数字干声 + 句段` 级拼接仅用于计数类；其余模板句一律走 3。
3. **云 TTS 预取**：开场白（M1/M2/M3 三变体 × 模式三变体）与结算对比句在**进入 APP 时**即请求，`cache_key = hash(date, lang, snapshot)`；发令时未就绪 → 本轮静默跳过不补播。结算句在课中 20 分钟点或倒计时 30s 点预取一次草稿，`endSession` 用实际数字二次校正（差异大则回落无变量兜底句）。

## 7. 播放层

- L1 音效独立通道，永不被打断，不可关；L2 人声单通道（同时最多 1 句）；L3 BGM 人声期间 duck -50%。
- `tts_stop()` 立即掐断当前句（抢占用）；正常句间不排队。
- 安静模式 = L2+L3 关，`safety_*` 例外；参数见设置第 7 行。

## 8. 里程碑建议

> **9-2 精简提案生效时走 V1-Lite 路径**（见 audio-voice-strategy.md §10）：§2/§3 的打分仲裁引擎、话术表 OTA、48h 去重**整体后置到 V2**；V1 用三档固定优先级（高:出框/破纪录/越昨/达标/时间到 · 中:开场白/结算/报时/时间节点 · 低:整十报数）+ 每 intent 一个每轮布尔位实现，语音包 ~130 条纯离线，记忆快照缩到 4 字段（yesterday_count / best_session / best_at / streak）。9-2 补充：自由模式**每满 1 分钟报时**（数字拼接；第 20 分钟让位长轮关怀、第 29 分钟起让位上限预告）；**结算小结 ≤2 句、端上拼接**——§6-3 的结算句云 TTS 预取在 V1 一并不做（云端动态生成耗时，短小结用固定句+数字干声足够）；开场白**保留昵称点名**——称呼音频在设置/修改昵称时一次性云端生成缓存（`POST /voice/nick-clip`，改名重生成），未写/失败回落无称呼版，云端**每日**预生成管线仍然不做。另三条 9-2 拍板：**出框引导复用 ATOM 系统级出框检测与提示**（V1 不在小程序内重做，提示语可配则换教练句）；**目标达成庆祝确认保留**（首环＋每满环 fanfare＋播报，高档）；**停跳升级链**（9-2 修订）：10s 鼓励（≤3/轮）→ 30s 再提示＋结算预告 → 60s 自动结算落档；主动暂停/出框暂停不进本链（走 3 分钟暂停生命周期，恢复时停跳计时清零），切后台静默暂停、超 3 分钟静默结算，进程被杀靠课中每 10 下/10s 的本地快照补结算——完整边界矩阵见 audio-voice-strategy.md §10.4。下表 M-A/M-C 对应并简化为 M-A′/M-B′ 两步。


| 阶段 | 内容 | 依赖 |
|---|---|---|
| M-A | 仲裁引擎 + voice_table.json + 固定包播放（P1–P12、A1–A2、E1–E7 无槽位变体） | 无 |
| M-B | 记忆快照接口 + `best_at` 落库 + M4/M5/M6/M7 上线 | 服务端档案改造 |
| M-C | 云 TTS 预取管线（开场白/结算）+ 缓存与回落 | TTS 供应商选型（策略 Q5，待定） |
| M-D | 彩蛋池扩容、多 Persona、纠错类（识别能力就绪后） | AI V2 |

## 9. 验收清单（QA 口径）

- [ ] 整十报数与任一 60+ 事件同帧：只播高优，数字音效照常。
- [ ] 抢占：开场白播放中破 PB（70×1.5=105 vs 40×1.0=40，差 >20）→ 掐断改播。
- [ ] 断连安慰每轮最多 2 次、第 3 次断连静默。
- [ ] 断网整轮：课中固定语音全部正常，记忆类全静默、无卡顿无补播。
- [ ] 安静模式跳绳出框：仍播出框引导句。
- [ ] 同一句变体连续两轮不重复；越昨对比 48h 内同用户不复读同 intent。
