/* ============================================================
 * Atom 跳绳小挑战 · 应用核心（C 参考实现，行为基准 = demo.html 2026-08-18）
 *
 * 完整实现：状态机（即时开跳 3·2·1·BodyPark）/ 训练屏（贴边环·60 格刻度环·
 *           计时·打击数字·骨骼火柴人最新规格·金闪涟漪）/ 限时 1'/2'(默认 1' 主推) /
 *           自由跳 30 分钟上限(8-18) / 出框 3 分钟自动结算 / 空轮保护 /
 *           破纪录追逐与必庆祝 / ★固定每 50 / 卡路里 / 体测评级 / 档案持久化
 * 简化占位（工程接力，见 code/README.md）：
 *           首页四卡滑动、二级页组、日卡、动作要领幻灯片、虚拟绳与金光、
 *           BGM 合成(跳频自适应 96–128BPM)、开场白完整拼装与 TTS 预取、
 *           放射光风火轮细化(5 档条纹/中心遮罩)、庆祝模块动效(蓄力爆发/撒花)、
 *           二级页组(返回=右滑手势+左下 ‹ 按钮,无点按空白返回;课中零浮层弹窗)
 * ============================================================ */
#include "atom_app.h"
#include "atom_render.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

void atom_pose_sim(float t_sec, atom_kp_t out[ATOM_KP_COUNT]);

/* ---- 品牌色 ---- */
#define C_BG      0xFF0A0C0B
#define C_INK     0xFFF2F4F2
#define C_SOFT    0xFF8D9591
#define C_GREEN   0xFF23C766
#define C_GOLD    0xFFFFDB80
#define C_AMBER   0xFFFFB020
#define C_STAR    0xFFFFC24D     /* 荣誉星金黄(会后修订) */
#define C_LIMB    0xFFEDF2EF     /* 火柴人四肢/躯干 */
#define C_WHITE   0xFFFFFFFF     /* 头 / 手 / 脚 */

typedef enum { SCR_LAUNCH, SCR_IDLE, SCR_MODE, SCR_READY, SCR_JUMP, SCR_CELEB, SCR_RESULT } screen_t;

#define TEST_GOAL     (-1)
#define MAX_RIPPLE    6
#define FREE_CAP_MS   (30u * 60000u)   /* 自由跳单轮上限（8-18 定稿:30 分钟强制终止,用户不可改,可配） */
#define PAUSE_END_MS  (3u * 60000u)    /* 出框/切后台超 3 分钟自动结算（对齐课程出框策略） */
#define FORM_HINT_GAP 20000u           /* 同类纠错提醒最小间隔 */

static const atom_hal_t *H;
static atom_callbacks_t CB;
static uint32_t FBUF[RW * RH];

static struct {
    screen_t scr;
    atom_profile_t pf;
    int goal, count, countdown, earned;
    uint32_t cd_next, brand_until, now, run_since, elapsed, pause_since, celeb_until;
    bool paused, goal_hit, new_best, presence, pause_by_tap;
    bool celeb_pending;                         /* 8-18:结果页点「完成」后待弹的恭喜帧 */
    int spoke30, spoke10;                       /* 限时语音节点 */
    bool cap_warned;                            /* 30 分钟上限前 1 分钟预告（一次） */
    int pb_at_start;                            /* 起跳时 PB 基准（按模式取，自我对比/追逐用） */
    bool pb_near_said, pb_broken;               /* 破纪录追逐状态 */
    uint32_t form_last[4];                      /* 纠错提醒频控（按 kind） */
    int punch;                                  /* 打击动画剩余帧 */
    bool punch_big;
    struct { float x, y, s; uint32_t t0; bool live; } rip[MAX_RIPPLE];
    atom_kp_t pose[ATOM_KP_COUNT];
    uint32_t kp_ms[ATOM_KP_COUNT];              /* 逐点最近有效时刻(缺失补间用) */
    uint32_t pose_ms;
    float ws;                                   /* 肩宽标尺 */
} A;

static void emit(atom_event_t e, int p1, int p2, int p3, int p4) {
    if (CB.on_event) CB.on_event(e, p1, p2, p3, p4);
}
static void speak(const char *s) { if (H && H->speak) H->speak(s); }
static uint32_t ms(void) { return H && H->millis ? H->millis() : 0; }

/* ---- 档案 ---- */
static void pf_default(void) {
    memset(&A.pf, 0, sizeof(A.pf));
    A.pf.goal_num = 200; A.pf.test_min = 1; A.pf.weight_kg = 40; A.pf.age_band = 1;   /* freestyle goal 默认 200(8-18) */
}
void atom_app_save(void) {
    if (H && H->storage_write) H->storage_write(&A.pf, sizeof(A.pf));
}
static void pf_load(void) {
    pf_default();
    if (H && H->storage_read) {
        atom_profile_t t;
        if (H->storage_read(&t, sizeof(t)) == (int)sizeof(t)) A.pf = t;
    }
    /* 目标档位收敛 50/100/200（会后定稿，无自由档） */
    if (A.pf.goal_num != 50 && A.pf.goal_num != 100 && A.pf.goal_num != 200) A.pf.goal_num = 200;
    if (A.pf.test_min != 2) A.pf.test_min = 1;   /* 限时 1'/2',默认 1' 主推(会后修订恢复 2') */
    if (A.pf.weight_kg <= 0) A.pf.weight_kg = 40;
}
atom_profile_t *atom_app_profile(void) { return &A.pf; }
void atom_app_new_day(void) {
    A.pf.today_count = 0; A.pf.today_best = 0; A.pf.today_rounds = 0;
    atom_app_save();
}

/* ---- 计时/数据 ---- */
static uint32_t elapsed_ms(void) {
    return A.elapsed + (A.run_since ? ms() - A.run_since : 0);
}
static uint32_t test_ms(void) { return (uint32_t)A.pf.test_min * 60000u; }
static float kcal(void) {
    float min = A.elapsed / 60000.0f;
    if (min <= 0 || A.count <= 0) return 0;
    float freq = A.count / min;
    float met = freq < 100 ? 8.8f : freq <= 120 ? 11.8f : 12.3f;
    return met * A.pf.weight_kg * (A.elapsed / 3600000.0f);
}
/* 体测评级（⚠ 示意阈值，上线前替换《国家学生体质健康标准》分表）：0待及格..4满分 */
static const int RATE_T[5][4] = {
    {60,90,110,130},{80,110,130,150},{100,130,150,170},{110,140,160,180},{90,120,140,160}};
static const char *RATE_S[5] = {"KEEP ON","PASS","GOOD","GREAT","FULL"};
static int rate_idx(int cnt) {
    const int *t = RATE_T[A.pf.age_band >= 0 && A.pf.age_band < 5 ? A.pf.age_band : 1];
    return cnt >= t[3] ? 4 : cnt >= t[2] ? 3 : cnt >= t[1] ? 2 : cnt >= t[0] ? 1 : 0;
}

/* ---- 会话 ---- */
static void spawn_ripple(void);

static void session_end(void) {
    if (A.scr != SCR_JUMP) return;
    if (A.run_since) { A.elapsed += ms() - A.run_since; A.run_since = 0; }
    A.paused = false;
    /* 空轮保护：一下没跳 → 不落档、不发结算事件、不进结算页 */
    if (A.count == 0) {
        A.scr = SCR_IDLE;
        speak("这轮没有跳哦，准备好随时再来！");
        return;
    }
    bool goal_met = A.goal_hit;
    if (A.goal == TEST_GOAL) {
        int32_t *best = A.pf.test_min == 2 ? &A.pf.best_2min : &A.pf.best_1min;
        A.new_best = A.count > *best;
        if (A.new_best) *best = A.count;
    } else {
        A.new_best = A.count > A.pf.best_session;   /* 自由跳破单轮 PB 也算新纪录 */
    }
    A.pf.today_count += A.count;
    A.pf.today_rounds += 1;
    if (A.count > A.pf.today_best) A.pf.today_best = A.count;   /* 榜单口径 */
    A.pf.total += A.count;
    if (A.count > A.pf.best_session) A.pf.best_session = A.count;
    A.pf.stars += A.earned;
    /* 打卡/连胜：当日 ≥ 目标或 ≥50 记打卡；连胜以服务端时间为准（云端回写 streak/max_streak），
       端上仅在离线时按 new_day 契约本地推进 */
    if ((goal_met || A.pf.today_count >= 50) && A.pf.streak > A.pf.max_streak)
        A.pf.max_streak = A.pf.streak;
    atom_app_save();
    emit(ATOM_EV_SESSION_END, A.count, (int)A.elapsed, (int)(kcal() * 10), A.new_best);
    /* 8-18 决议：训练完成默认直接进结果页；新纪录的恭喜帧改在用户点「完成」后单独弹出
       （前后端逻辑解耦；课中达标/破纪录瞬间已有即时反馈）。
       结算同时由端上渲染 466×466 结果卡上报（result_card,接力项——见 backend-spec §2） */
    A.celeb_pending = A.new_best;
    A.scr = SCR_RESULT;
    speak(A.goal == TEST_GOAL ? "时间到！辛苦啦" : goal_met ? "目标达成！" : "本轮结束，辛苦啦");
}

static void session_ready(int goal) {
    A.goal = goal; A.count = 0; A.earned = 0;
    A.goal_hit = false; A.new_best = false; A.celeb_pending = false;
    A.elapsed = 0; A.run_since = 0; A.paused = false;
    A.spoke30 = A.spoke10 = 0; A.cap_warned = false;
    /* PB 基准按模式取（结算自我对比 + 课中追逐） */
    A.pb_at_start = goal == TEST_GOAL
        ? (A.pf.test_min == 2 ? A.pf.best_2min : A.pf.best_1min)
        : A.pf.best_session;
    A.pb_near_said = A.pb_broken = false;
    memset(A.rip, 0, sizeof(A.rip));
    /* 即时开跳：无站位校验，选模式立刻 3·2·1·BodyPark（presence 只在训练中生效） */
    A.scr = SCR_READY;
    A.countdown = 3; A.cd_next = ms() + 900; A.brand_until = 0;
    emit(ATOM_EV_SESSION_START, goal, A.pf.test_min, 0, 0);
    speak("三");
}

void atom_app_start(bool timed) { session_ready(timed ? TEST_GOAL : A.pf.goal_num); }
void atom_app_home(void) { A.scr = SCR_IDLE; A.countdown = 0; A.brand_until = 0; }
void atom_app_end_session(void) { session_end(); }

void atom_app_set_presence(bool p) {
    A.presence = p;
    /* 仅训练中：出框/切后台立即暂停停表；回到画面恢复（开跳不设门槛） */
    if (!p && A.scr == SCR_JUMP && !A.paused) {
        A.paused = true; A.pause_by_tap = false; A.pause_since = ms();
        if (A.run_since) { A.elapsed += ms() - A.run_since; A.run_since = 0; }
        speak("已暂停");
    } else if (p && A.scr == SCR_JUMP && A.paused && !A.pause_by_tap) {
        A.paused = false; A.run_since = ms();   /* 手动暂停不被回画面自动恢复 */
    }
}

void atom_app_form_hint(atom_form_kind_t kind) {
    if (kind > ATOM_FORM_LOW_LIGHT) return;
    uint32_t now = ms();
    if (A.form_last[kind] && now - A.form_last[kind] < FORM_HINT_GAP) return;   /* ≥20s 频控 */
    A.form_last[kind] = now;
    static const char *TXT[4] = {
        "双脚并拢跳，交替跳不计数哦",
        "双脚一起跳，单脚跳不计数哦",
        "跳高一点点，让我看清你",
        "光线暗一点，往亮处站一站",
    };
    speak(TXT[kind]);
    emit(ATOM_EV_FORM_HINT, (int)kind, 0, 0, 0);
}

void atom_app_on_jump(void) {
    if (A.scr != SCR_JUMP || A.paused) return;
    A.count++;
    A.punch = 4; A.punch_big = (A.count % 10 == 0);
    if (A.punch_big) A.punch = 6;
    spawn_ripple();
    /* 星星：荣誉星固定每 50（屏显 lap = count/50）；货币星整 20 +2；达标 +10 */
    if (A.count % 20 == 0) A.earned += 2;
    if (A.goal > 0 && A.count == A.goal && !A.goal_hit) {
        A.goal_hit = true; A.earned += 10;
        speak("目标达成！继续保持！");
    }
    /* 破纪录追逐（自由跳）：只差 10 下内播报一次；越线即时反馈（不打断计数） */
    if (A.goal != TEST_GOAL && A.pb_at_start >= 30) {
        int gap = A.pb_at_start - A.count;
        if (!A.pb_near_said && gap > 0 && gap <= 10) {
            A.pb_near_said = true;
            char b[64]; snprintf(b, sizeof(b), "离你的最高纪录只差 %d 下，冲！", gap);
            speak(b);
        }
        if (!A.pb_broken && A.count > A.pb_at_start) {
            A.pb_broken = true;
            A.punch = 6; A.punch_big = true;   /* 全身金光/彩带为工程接力，先用重击帧代偿 */
            speak("新纪录诞生！现在每一下都在刷新它！");
        }
    }
    emit(ATOM_EV_JUMP, A.count, 0, 0, 0);
}

void atom_app_feed_pose(const atom_kp_t pts[ATOM_KP_COUNT]) {
    /* 缺失补间(AI 团队确认):单点缺失保持最近有效值 ≤400ms,超时该部件隐藏 */
    uint32_t now = ms();
    for (int i = 0; i < ATOM_KP_COUNT; i++) {
        if (pts[i].v != 0) { A.pose[i] = pts[i]; A.kp_ms[i] = now; }
        else if (A.pose[i].v != 0 && now - A.kp_ms[i] > 400) A.pose[i].v = 0;
    }
    A.pose_ms = now;
}

/* ---- 触屏（简化热区；完整卡片交互工程接力） ---- */
void atom_app_touch(int x, int y) {
    switch (A.scr) {
    case SCR_LAUNCH: A.scr = SCR_IDLE; break;
    case SCR_IDLE:
        if (y > 300) A.scr = SCR_MODE;            /* 底部 START */
        break;
    case SCR_MODE:
        if (y < 140) A.scr = SCR_IDLE;            /* 顶部返回 */
        else if (y < 260) atom_app_start(true);   /* 限时 */
        else atom_app_start(false);               /* 不限时 */
        break;
    case SCR_READY: break;                        /* 倒计时/发令中不响应 */
    case SCR_JUMP:
        /* 训练中单击屏幕 = 暂停并弹控制面板;暂停中按纵向分区选择 */
        if (!A.paused) {
            A.paused = true; A.pause_by_tap = true; A.pause_since = ms();
            if (A.run_since) { A.elapsed += ms() - A.run_since; A.run_since = 0; }
            speak("已暂停");
        } else if (y < 240) {                     /* 上区:继续 */
            A.paused = false; A.pause_by_tap = false; A.run_since = ms();
        } else if (y < 300) {                     /* 中区:立即结算(保存) */
            session_end();
        } else {                                  /* 下区:放弃本轮(不落档,已结算历史不可删) */
            A.paused = false; A.pause_by_tap = false;
            A.scr = SCR_IDLE;
            speak("本轮已放弃，不计入记录");
        }
        break;
    case SCR_RESULT:
        if (y < 233) A.scr = SCR_MODE;            /* 上半：再跳 */
        else if (A.celeb_pending) {               /* 下半：完成 → 有新纪录先弹恭喜帧(8-18) */
            A.celeb_pending = false;
            A.scr = SCR_CELEB;
            A.celeb_until = ms() + 1900;
            speak("新纪录！");
        } else A.scr = SCR_IDLE;                  /* 下半：完成 */
        break;
    default: break;
    }
    (void)x;
}

/* ============================================================
 * 渲染
 * ============================================================ */
static void spawn_ripple(void) {
    for (int i = 0; i < MAX_RIPPLE; i++)
        if (!A.rip[i].live) {
            /* 落点优先脚跟(18/21,真实触地),缺省回落双踝(0/5) —— BP-28 */
            int li = A.pose[18].v ? 18 : 5, ri = A.pose[21].v ? 21 : 0;
            float lx = A.pose[li].v ? A.pose[li].x : 0.45f;
            float rx = A.pose[ri].v ? A.pose[ri].x : 0.55f;
            float ly = A.pose[li].v ? A.pose[li].y : 0.878f;
            float ry_ = A.pose[ri].v ? A.pose[ri].y : 0.878f;
            /* 2/3 视口变换（同渲染） */
            float x = 0.5f + (((lx + rx) * 0.5f) - 0.5f) * (2.0f / 3.0f);
            float y = 0.94f - (1 - fmaxf(ly, ry_)) * (2.0f / 3.0f) + 0.02f;
            A.rip[i].x = x * RW;
            A.rip[i].y = y * RH;
            A.rip[i].s = A.ws > 0 ? A.ws : 1;
            A.rip[i].t0 = ms();
            A.rip[i].live = true;
            return;
        }
}

static uint32_t lerp_c(uint32_t a, uint32_t b, float t, float alpha) {
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    int r = (int)(ar + (br - ar) * t), g = (int)(ag + (bg - ag) * t), bl = (int)(ab + (bb - ab) * t);
    int al = (int)(alpha * 255); if (al < 0) al = 0; if (al > 255) al = 255;
    return ((uint32_t)al << 24) | (r << 16) | (g << 8) | bl;
}

static void draw_ripples(void) {
    for (int i = 0; i < MAX_RIPPLE; i++) {
        if (!A.rip[i].live) continue;
        float age = (A.now - A.rip[i].t0) / 700.0f;
        if (age >= 1) { A.rip[i].live = false; continue; }
        float sc = A.rip[i].s;
        if (age < 0.22f) {                                  /* 落点金闪 */
            float f = age / 0.22f;
            float fr = (34 + 42 * f) * sc;
            rd_fill_ellipse(A.rip[i].x, A.rip[i].y, fr, fr * 0.3f,
                            lerp_c(C_GOLD, C_GOLD, 0, 0.5f * (1 - f)));
        }
        float p = 1 - powf(1 - age, 3);                     /* 爆发扩张 */
        float rx = (36 + 137 * p) * sc;
        float w = age < 0.35f ? 9 - 6.5f * (age / 0.35f)
                : age < 0.7f ? 2.5f + 1.5f * ((age - 0.35f) / 0.35f)
                : 4 - 3 * ((age - 0.7f) / 0.3f);
        uint32_t col = lerp_c(C_GOLD, C_INK, fminf(1, age / 0.4f), 0.95f * (1 - age));
        rd_ellipse_ring(A.rip[i].x, A.rip[i].y, rx, rx * 0.3f, w, col);
    }
}

/* 火柴人（规格见 docs-and-demo/figure-rendering.md，2026-08-18 定稿）：
 * 躯干宽 40=2×腿宽 · 臂 18 · 腿 20 · 头 r26 双耳中点上移 17 · 手白 r9 · 脚白 r14
 * 肩锚点 x=颈点±11（y 取真实肩点）· 髋锚点 髋中±10 · 躯干顶自颈点下移 12 补偿圆帽 */
static void draw_figure(void) {
    atom_kp_t P[ATOM_KP_COUNT];
    if (A.pose_ms && A.now - A.pose_ms < 500) memcpy(P, A.pose, sizeof(P));
    else atom_pose_sim(A.now / 1000.0f, P);
    /* 2/3 视口，底边锚定 0.94（顶部 1/3 永久让给数字） */
    for (int i = 0; i < ATOM_KP_COUNT; i++) {
        if (!P[i].v) continue;
        P[i].x = (0.5f + (P[i].x - 0.5f) * (2.0f / 3.0f)) * RW;
        P[i].y = (0.94f - (1 - P[i].y) * (2.0f / 3.0f)) * RH;
    }
    float span = P[12].v && P[13].v ? hypotf(P[13].x - P[12].x, P[13].y - P[12].y) : 40;
    float ws = span / 40; if (ws < 0.55f) ws = 0.55f; if (ws > 1.5f) ws = 1.5f;
    A.ws = ws;
    /* BP-28:肩中(7)/髋中(6)后端直出,缺省回落左右点中点 */
    float nx = P[7].v ? P[7].x : (P[12].x + P[13].x) * 0.5f;
    float ny = P[7].v ? P[7].y : (P[12].y + P[13].y) * 0.5f;
    float hx = P[6].v ? P[6].x : (P[2].x + P[3].x) * 0.5f;
    float hy = P[6].v ? P[6].y : (P[2].y + P[3].y) * 0.5f;
    /* 腿：髋锚点 x=髋中 ±10、y 取各自真实髋点(2/3)，膝(1/4)/踝(0/5)真实点 */
    rd_thick_line(hx - 10 * ws, P[2].y, P[1].x, P[1].y, 20 * ws, C_LIMB);
    rd_thick_line(P[1].x, P[1].y, P[0].x, P[0].y, 20 * ws, C_LIMB);
    rd_thick_line(hx + 10 * ws, P[3].y, P[4].x, P[4].y, 20 * ws, C_LIMB);
    rd_thick_line(P[4].x, P[4].y, P[5].x, P[5].y, 20 * ws, C_LIMB);
    /* 臂：肩锚点 x=肩中 ±11、y 取各自真实肩点(12/13)，肘(11/14)/腕(10/15)真实点 */
    rd_thick_line(nx - 11 * ws, P[12].y, P[11].x, P[11].y, 18 * ws, C_LIMB);
    rd_thick_line(P[11].x, P[11].y, P[10].x, P[10].y, 18 * ws, C_LIMB);
    rd_thick_line(nx + 11 * ws, P[13].y, P[14].x, P[14].y, 18 * ws, C_LIMB);
    rd_thick_line(P[14].x, P[14].y, P[15].x, P[15].y, 18 * ws, C_LIMB);
    /* 躯干：等宽直柱 40（顶自肩中下移 12 补偿圆帽） */
    rd_thick_line(nx, ny + 12 * ws, hx, hy, 40 * ws, C_LIMB);
    /* 手（白 r9=臂宽/2 不外凸）/ 脚（白 r14，全身唯一的"凸"） */
    rd_fill_circle(P[10].x, P[10].y, 9 * ws, C_WHITE);
    rd_fill_circle(P[15].x, P[15].y, 9 * ws, C_WHITE);
    rd_fill_circle(P[0].x, P[0].y, 14 * ws, C_WHITE);
    rd_fill_circle(P[5].x, P[5].y, 14 * ws, C_WHITE);
    /* 头：纯白圆 r26 —— BP-28 头部点(9)直接作圆心;缺省回落肩中上移 43ws */
    float hcx = P[9].v ? P[9].x : nx;
    float hcy = P[9].v ? P[9].y : ny - 43 * ws;
    rd_fill_circle(hcx, hcy, 26 * ws, C_WHITE);
}

/* 限时模式 60 格时间刻度环（每格=总时长/60；逢五长刻度；剩余亮格从尾端熄灭） */
static void draw_tick_ring(float rem_frac, bool urgent) {
    int lit = (int)ceilf(rem_frac * 60);
    for (int i = 0; i < 60; i++) {
        bool major = i % 5 == 0;
        float a = (float)i / 60 * 6.2831853f - 1.5707963f;   /* 12 点起顺时针 */
        float r0 = major ? 218 : 222, r1 = major ? 234 : 231;
        float ca = cosf(a), sa = sinf(a);
        uint32_t col = i < lit ? (urgent ? C_AMBER : C_GREEN) : 0x2AFFFFFF;
        rd_thick_line(233 + ca * r0, 233 + sa * r0, 233 + ca * r1, 233 + sa * r1,
                      major ? 5 : 3.5f, col);
    }
}

static void draw_jump(void) {
    rd_clear(C_BG);
    int lap = A.count / 50;                       /* ★ 固定每 50 一颗（全模式统一） */
    /* 燃起来·风火轮(会后修订:5 档与 freestyle goal 挂钩——满50=档1橙,每完成一目标升档,
       ≥4 目标=档5白热;限时无目标沿用每 50 一档至档3)。本参考以底部光晕示意档位颜色/强度;
       完整放射光为工程接力:条纹 15/18/20/24/24 · 转速 18/14/11/8/5.5s · 中心径向遮罩淡出 ·
       圆心=火柴人渲染中心(50%,62%,屏幕偏下) */
    int goal_laps = A.goal > 0 ? A.count / A.goal : 0;
    int fire = A.goal == TEST_GOAL ? (lap > 3 ? 3 : lap)
             : A.count < 50 ? 0 : (1 + goal_laps > 5 ? 5 : 1 + goal_laps);
    if (fire > 0) {
        static const uint32_t FIRE_C[6] =
            { 0, 0xFFFFA62C, 0xFFFFBA32, 0xFFFFCC36, 0xFFFFDC6E, 0xFFFFF6DC };
        for (int i = 0; i < 3; i++)
            rd_fill_ellipse(RW / 2.0f, RH + 40, 300 - i * 60, 130 - i * 25,
                            lerp_c(FIRE_C[fire], FIRE_C[fire], 0, 0.024f * fire + 0.014f * i));
    }
    /* 进度环：自由=目标圈平滑弧；限时=60 格刻度环 */
    if (A.goal == TEST_GOAL) {
        float rem = 1 - (float)elapsed_ms() / test_ms();
        if (rem < 0) rem = 0;
        draw_tick_ring(rem, test_ms() - elapsed_ms() <= 10000);
    } else {
        int unit = A.goal > 0 ? A.goal : 50;
        float frac = (float)(A.count % unit) / unit;
        rd_ring_arc(233, 233, 226, 13, 1, 0x17FFFFFF);
        rd_ring_arc(233, 233, 226, 13, frac, C_GREEN);
    }
    /* 顶点计时 */
    char buf[32];
    uint32_t t = A.goal == TEST_GOAL
        ? (elapsed_ms() > test_ms() ? 0 : test_ms() - elapsed_ms()) : elapsed_ms();
    snprintf(buf, sizeof(buf), "%u:%02u", t / 60000, (t / 1000) % 60);
    float tw = rd_7seg_str(0, 0, -20, 3, buf, 0);
    uint32_t tcol = (A.goal == TEST_GOAL && t <= 10000) ? C_AMBER : C_SOFT;
    rd_7seg_str((RW - tw) / 2.0f, 14, 20, 3, buf, tcol);
    /* 打击数字（≥1000 降档 96→68 防溢出） */
    float h = A.count > 999 ? 68 : 96;
    static const float PS[7]  = {1, 1, 1.05f, 0.9f, 1.5f, 1, 1};
    static const float PB[7]  = {1, 1, 1.02f, 0.96f, 1.15f, 0.85f, 1.9f};
    if (A.punch > 0) h *= (A.punch_big ? PB : PS)[A.punch];
    snprintf(buf, sizeof(buf), "%d", A.count);
    float cw = rd_7seg_str(0, 0, -h, h * 0.14f, buf, 0);
    uint32_t ccol = A.goal_hit ? C_AMBER : C_INK;
    if (A.punch >= (A.punch_big ? 6 : 4)) ccol = C_GOLD;             /* 第0帧金闪 */
    rd_7seg_str((RW - cw) / 2.0f, 155 - h, h, h * 0.14f, buf, ccol);
    /* 荣誉星（每 50 一颗；>4 颗工程接力收敛为 ★×N） */
    for (int i = 0; i < lap && i < 4; i++)
        rd_star((RW + cw) / 2.0f + 24 + (i % 2) * 26, 155 - h + 14 + (i / 2) * 26, 11, C_STAR);
    if (lap > 4) {
        snprintf(buf, sizeof(buf), "X%d", lap);
        rd_text((RW + cw) / 2.0f + 50, 155 - h + 44, 2, buf, C_STAR);
    }
    draw_figure();
    draw_ripples();
    if (A.paused) {
        rd_fill_circle(233, 233, 466, 0xB00A0C0B);
        rd_text((RW - rd_text_w(4, "PAUSED")) / 2, 145, 4, "PAUSED", C_INK);
        rd_text((RW - rd_text_w(3, "RESUME")) / 2, 215, 3, "RESUME", C_GREEN);
        rd_text((RW - rd_text_w(3, "FINISH")) / 2, 262, 3, "FINISH", C_INK);
        rd_text((RW - rd_text_w(2, "DISCARD")) / 2, 318, 2, "DISCARD", C_SOFT);
    }
}

static void draw_simple_figure(float cx, float cy, float s) {   /* 静态吉祥物 ATOM（无脸极简版;原工作名 Bono） */
    rd_fill_circle(cx, cy - 31 * s, 15.5f * s, C_WHITE);
    rd_thick_line(cx, cy - 9 * s, cx, cy + 23 * s, 13 * s, C_LIMB);
    rd_thick_line(cx, cy - 3 * s, cx - 17 * s, cy + 11 * s, 9 * s, C_LIMB);
    rd_thick_line(cx, cy - 3 * s, cx + 17 * s, cy + 11 * s, 9 * s, C_LIMB);
    rd_thick_line(cx, cy + 23 * s, cx - 12 * s, cy + 45 * s, 9 * s, C_LIMB);
    rd_thick_line(cx, cy + 23 * s, cx + 12 * s, cy + 45 * s, 9 * s, C_LIMB);
}

/* 今日开场白(会后定稿,策略见 docs-and-demo/audio-voice-strategy.md §4.5):
   进入 APP 时由工程侧按当日数据预生成 TTS 缓存;3·2·1·BodyPark 发令后边跳边听,
   未就绪则本轮静默跳过不补播;当日首轮完整版(≤3 句)、同日再轮 1 句短版、「再跳一轮」不播。
   本参考只播模式召唤句示意;完整拼装(称呼+第N次/数据亮点/模式变体)与预取为工程接力 */
static void speak_opening(void) {
    char buf[96];
    if (A.goal == TEST_GOAL)
        snprintf(buf, sizeof(buf), "%s",
                 A.pf.test_min == 2 ? "两分钟限时，稳住节奏冲到底！" : "一分钟限时，全力冲刺！");
    else
        snprintf(buf, sizeof(buf), "目标 %d 下，出发！", A.pf.goal_num);
    speak(buf);
}

void atom_app_tick(void) {
    A.now = ms();
    /* 倒计时 3·2·1 → BodyPark 发令帧 0.75s → 训练 */
    if (A.scr == SCR_READY) {
        if (A.countdown > 0 && A.now >= A.cd_next) {
            A.countdown--;
            if (A.countdown > 0) { A.cd_next = A.now + 900; speak(A.countdown == 2 ? "二" : "一"); }
            else { A.brand_until = A.now + 750; speak("Body Park!"); }
        } else if (A.countdown == 0 && A.brand_until && A.now >= A.brand_until) {
            A.brand_until = 0;
            A.scr = SCR_JUMP; A.run_since = A.now;
            speak_opening();   /* 开场白:发令后播(预生成就绪才播,原型等价于总是就绪) */
        }
    }
    if (A.scr == SCR_JUMP && !A.paused) {
        uint32_t e = elapsed_ms();
        if (A.goal == TEST_GOAL) {
            /* 限时到点 / 语音节点 */
            if (e >= test_ms()) session_end();
            else {
                uint32_t rem = test_ms() - e;
                if (rem <= 30000 && !A.spoke30) { A.spoke30 = 1; speak("还有三十秒"); }
                if (rem <= 10000 && !A.spoke10) { A.spoke10 = 1; speak("最后十秒，冲刺！"); }
            }
        } else {
            /* 自由跳 30 分钟上限(8-18 定稿)：最后 1 分钟语音预告，届满自动结算（成绩照常落档） */
            if (e >= FREE_CAP_MS) session_end();
            else if (e >= FREE_CAP_MS - 60000 && !A.cap_warned) {
                A.cap_warned = true;
                speak("还有一分钟到十五分钟上限，冲刺！");
            }
        }
    }
    /* 出框/切后台超 3 分钟自动结算（对齐课程出框策略，可配 3-5min） */
    if (A.scr == SCR_JUMP && A.paused && A.now - A.pause_since > PAUSE_END_MS) session_end();
    if (A.scr == SCR_CELEB && A.now >= A.celeb_until) A.scr = SCR_IDLE;   /* 恭喜帧在结果页之后,结束回首页(8-18) */
    if (A.punch > 0) A.punch--;

    /* ---- 渲染 ---- */
    char buf[64];
    switch (A.scr) {
    case SCR_LAUNCH:
        rd_clear(C_BG);
        draw_simple_figure(233, 180, 1.4f);
        rd_text((RW - rd_text_w(3, "JUMPROPE STAR")) / 2, 290, 3, "JUMPROPE STAR", C_INK);
        snprintf(buf, sizeof(buf), "STREAK %d", A.pf.streak);
        rd_text((RW - rd_text_w(2, buf)) / 2, 335, 2, buf, C_SOFT);
        break;
    case SCR_IDLE: {
        /* 首页四行（会后定稿）：连胜 / 今日(未跳=邀请) / 累计+★ / 最佳速览 */
        rd_clear(C_BG);
        snprintf(buf, sizeof(buf), "%d", A.pf.streak);
        float w = rd_7seg_str(0, 0, -64, 9, buf, 0);
        rd_7seg_str((RW - w) / 2, 55, 64, 9, buf, C_INK);
        rd_text((RW - rd_text_w(2, "DAY STREAK")) / 2, 140, 2, "DAY STREAK", C_SOFT);
        if (A.pf.today_count > 0)
            snprintf(buf, sizeof(buf), "TODAY TOTAL %d", A.pf.today_count);
        else
            snprintf(buf, sizeof(buf), "NO JUMPS YET - GO!");
        rd_text((RW - rd_text_w(3, buf)) / 2, 180, 3, buf, C_INK);
        snprintf(buf, sizeof(buf), "TOTAL %d  *%d", A.pf.total, A.pf.stars);
        rd_text((RW - rd_text_w(2, buf)) / 2, 222, 2, buf, C_SOFT);
        snprintf(buf, sizeof(buf), "BEST %d  1' %d", A.pf.best_session, A.pf.best_1min);
        rd_text((RW - rd_text_w(2, buf)) / 2, 254, 2, buf, C_SOFT);
        /* START 按钮 */
        rd_thick_line(163, 330, 303, 330, 64, C_GREEN);
        rd_text((RW - rd_text_w(3, "START")) / 2, 320, 3, "START", C_INK);
        rd_text((RW - rd_text_w(2, "RECORD  RANK  BEST  SET")) / 2, 395, 2,
                "RECORD  RANK  BEST  SET", C_SOFT);
        break; }
    case SCR_MODE:
        rd_clear(C_BG);
        rd_text((RW - rd_text_w(3, "MODE")) / 2, 80, 3, "MODE", C_INK);
        rd_thick_line(133, 200, 333, 200, 76, 0xFF1B201D);
        snprintf(buf, sizeof(buf), "TIMED %d'", A.pf.test_min);
        rd_text((RW - rd_text_w(3, buf)) / 2, 190, 3, buf, C_GREEN);
        rd_thick_line(133, 310, 333, 310, 76, 0xFF1B201D);
        snprintf(buf, sizeof(buf), "FREE %d", A.pf.goal_num);
        rd_text((RW - rd_text_w(3, buf)) / 2, 300, 3, buf, C_INK);
        rd_text((RW - rd_text_w(2, "HOW TO JUMP")) / 2, 390, 2, "HOW TO JUMP", C_SOFT);
        break;
    case SCR_READY:
        rd_clear(C_BG);
        if (A.countdown > 0) {
            snprintf(buf, sizeof(buf), "%d", A.countdown);
            float w = rd_7seg_str(0, 0, -160, 20, buf, 0);
            rd_7seg_str((RW - w) / 2, 150, 160, 20, buf, C_INK);
        } else {
            /* 发令帧：品牌口号 BODYPARK（品牌绿，0.75s） */
            rd_text((RW - rd_text_w(4, "BODYPARK")) / 2, 210, 4, "BODYPARK", C_GREEN);
        }
        break;
    case SCR_JUMP: draw_jump(); break;
    case SCR_CELEB:
        /* 恭喜页 = 自包含庆祝模块(吸纳 Quick-Voice-Tips set-celebration,契约同其 celebration.h):
           三行堆叠 范围(小·上)/状态(最大·中)/金徽章(下);仅新纪录时进入(点「完成」后)。
           蓄力→爆发/撒花 flutter/彩纸炮/圆边金光环/点按跳过 为工程接力 */
        rd_clear(C_BG);
        rd_text((RW - rd_text_w(2, "ROUND")) / 2, 150, 2, "ROUND", C_SOFT);
        rd_text((RW - rd_text_w(6, "COMPLETE")) / 2, 200, 6, "COMPLETE", C_INK);
        rd_text((RW - rd_text_w(3, "* NEW PB")) / 2, 290, 3, "* NEW PB", C_GOLD);
        break;
    case SCR_RESULT: {
        /* 结算（会后减字）：大数字 / 状态行 / 评级(仅1') / ★+自我对比 / 今日累计 */
        rd_clear(C_BG);
        snprintf(buf, sizeof(buf), "%d", A.count);
        float w = rd_7seg_str(0, 0, -80, 11, buf, 0);
        rd_7seg_str((RW - w) / 2, 70, 80, 11, buf, A.new_best ? C_AMBER : C_INK);
        snprintf(buf, sizeof(buf), "%s %u:%02u",
                 A.new_best ? "NEW BEST" : A.goal_hit ? "GOAL" : A.goal == TEST_GOAL ? "TIME UP" : "DONE",
                 A.elapsed / 60000, (A.elapsed / 1000) % 60);
        rd_text((RW - rd_text_w(2, buf)) / 2, 190, 2, buf, C_INK);
        if (A.goal == TEST_GOAL && A.pf.test_min == 1) {
            snprintf(buf, sizeof(buf), "RATE %s", RATE_S[rate_idx(A.count)]);
            rd_text((RW - rd_text_w(2, buf)) / 2, 222, 2, buf, C_GREEN);
        }
        /* 只和自己比（排行榜后端后算，不在结算页）：超越/差距/首纪录 */
        int diff = A.count - A.pb_at_start;
        if (A.pb_at_start <= 0) snprintf(buf, sizeof(buf), "+%d*  FIRST RECORD", A.earned);
        else if (diff > 0)      snprintf(buf, sizeof(buf), "+%d*  BEAT BEST +%d", A.earned, diff);
        else                    snprintf(buf, sizeof(buf), "+%d*  %d TO RECORD", A.earned, A.pb_at_start - A.count + 1);
        rd_text((RW - rd_text_w(2, buf)) / 2, 258, 2, buf, C_GREEN);
        if (A.pf.today_count > A.count) {
            snprintf(buf, sizeof(buf), "TODAY TOTAL %d", A.pf.today_count);
            rd_text((RW - rd_text_w(2, buf)) / 2, 292, 2, buf, C_SOFT);
        }
        rd_text((RW - rd_text_w(2, "TAP TOP AGAIN  BOTTOM HOME")) / 2, 380, 2,
                "TAP TOP AGAIN  BOTTOM HOME", C_SOFT);
        break; }
    }
    if (H && H->display_flush) H->display_flush(FBUF, RW, RH);
}

void atom_app_init(const atom_hal_t *hal, const atom_callbacks_t *cb) {
    H = hal;
    if (cb) CB = *cb;
    memset(&A, 0, sizeof(A));
    A.ws = 1;
    pf_load();
    rd_bind(FBUF);
    A.scr = SCR_LAUNCH;
    emit(ATOM_EV_APP_READY, 0, 0, 0, 0);
}
