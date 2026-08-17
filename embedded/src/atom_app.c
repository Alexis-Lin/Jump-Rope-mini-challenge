/* ============================================================
 * Atom 跳绳小挑战 · 应用核心（C 参考实现 proto-v0.5-c）
 *
 * 完整实现：状态机 / 训练屏（贴边环·计时·打击数字·骨骼火柴人·金闪涟漪）/
 *           限时测试 / 计时停表 / 星星圈数 / 卡路里 / 体测评级 / 档案持久化
 * 简化占位（工程接力，可用设备 SVG 栈实现）：
 *           首页三卡滑动、排行榜/勋章/记录/设置二级页、火焰放射光、语音文案细化
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

typedef enum { SCR_LAUNCH, SCR_IDLE, SCR_MODE, SCR_READY, SCR_JUMP, SCR_CELEB, SCR_RESULT } screen_t;

#define TEST_GOAL (-1)
#define MAX_RIPPLE 6

static const atom_hal_t *H;
static atom_callbacks_t CB;
static uint32_t FBUF[RW * RH];

static struct {
    screen_t scr;
    atom_profile_t pf;
    int goal, count, countdown, earned;
    uint32_t cd_next, now, run_since, elapsed, pause_since, celeb_until;
    bool paused, goal_hit, new_best, presence;
    int spoke30, spoke10;                       /* 限时语音节点 */
    int punch;                                  /* 打击动画剩余帧 */
    bool punch_big;
    struct { float x, y, s; uint32_t t0; bool live; } rip[MAX_RIPPLE];
    atom_kp_t pose[ATOM_KP_COUNT];
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
    A.pf.goal_num = 100; A.pf.test_min = 1; A.pf.weight_kg = 40; A.pf.age_band = 1;
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
    if (A.pf.goal_num <= 0 && A.pf.goal_num != 0) A.pf.goal_num = 100;
    if (A.pf.test_min != 2) A.pf.test_min = 1;
    if (A.pf.weight_kg <= 0) A.pf.weight_kg = 40;
}
atom_profile_t *atom_app_profile(void) { return &A.pf; }
void atom_app_new_day(void) { A.pf.today_count = 0; atom_app_save(); }

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
/* 体测评级（示意阈值，上线前替换国标分表）：返回 0待及格..4满分 */
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
    bool goal_met = A.goal_hit;
    if (A.goal == TEST_GOAL) {
        int32_t *best = A.pf.test_min == 2 ? &A.pf.best_2min : &A.pf.best_1min;
        A.new_best = A.count > *best;
        if (A.new_best) *best = A.count;
    }
    A.pf.today_count += A.count;
    A.pf.total += A.count;
    if (A.count > A.pf.best_session) A.pf.best_session = A.count;
    A.pf.stars += A.earned;
    /* 打卡：达标或当日累计 >= 50（连胜跨天判断由工程侧 new_day 流程完善，此处简化自增） */
    if (goal_met || A.pf.today_count >= 50) { /* TODO: 按 lastDone 日期判定连胜 */ }
    atom_app_save();
    emit(ATOM_EV_SESSION_END, A.count, (int)A.elapsed, (int)(kcal() * 10), A.new_best);
    if (goal_met || A.new_best) {
        A.scr = SCR_CELEB;
        A.celeb_until = ms() + 1900;
        speak(A.new_best ? "新纪录！" : "目标达成！");
    } else {
        A.scr = SCR_RESULT;
        speak("本轮结束，辛苦啦");
    }
}

static void session_ready(int goal) {
    A.goal = goal; A.count = 0; A.earned = 0;
    A.goal_hit = false; A.new_best = false;
    A.elapsed = 0; A.run_since = 0; A.countdown = 0; A.paused = false;
    A.spoke30 = A.spoke10 = 0;
    memset(A.rip, 0, sizeof(A.rip));
    A.scr = SCR_READY;
    emit(ATOM_EV_SESSION_START, goal, A.pf.test_min, 0, 0);
    speak(goal == TEST_GOAL ? "限时测试，请站到镜头前" : "请站到镜头前");
}

void atom_app_start(bool timed) { session_ready(timed ? TEST_GOAL : A.pf.goal_num); }
void atom_app_home(void) { A.scr = SCR_IDLE; A.countdown = 0; }
void atom_app_end_session(void) { session_end(); }

void atom_app_set_presence(bool p) {
    A.presence = p;
    if (p && A.scr == SCR_READY && A.countdown == 0) {
        A.countdown = 3; A.cd_next = ms() + 900; speak("三");
    } else if (!p && A.scr == SCR_JUMP && !A.paused) {
        A.paused = true; A.pause_since = ms();
        if (A.run_since) { A.elapsed += ms() - A.run_since; A.run_since = 0; }
        speak("已暂停");
    } else if (p && A.scr == SCR_JUMP && A.paused) {
        A.paused = false; A.run_since = ms();
    }
}

void atom_app_on_jump(void) {
    if (A.scr != SCR_JUMP || A.paused) return;
    A.count++;
    A.punch = 4; A.punch_big = (A.count % 10 == 0);
    if (A.punch_big) A.punch = 6;
    spawn_ripple();
    int unit = A.goal > 0 ? A.goal : 50;
    if (A.count % unit == 0) { A.earned += 2; }
    if (A.goal > 0 && A.count == A.goal && !A.goal_hit) {
        A.goal_hit = true; A.earned += 10;
        speak("目标达成！继续保持！");
    }
    emit(ATOM_EV_JUMP, A.count, 0, 0, 0);
}

void atom_app_feed_pose(const atom_kp_t pts[ATOM_KP_COUNT]) {
    memcpy(A.pose, pts, sizeof(A.pose));
    A.pose_ms = ms();
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
    case SCR_READY: A.scr = SCR_IDLE; break;
    case SCR_RESULT:
        if (y < 233) A.scr = SCR_MODE;            /* 上半：再跳 */
        else A.scr = SCR_IDLE;                    /* 下半：完成 */
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
            float lx = A.pose[27].v ? A.pose[27].x : 0.45f;
            float rx = A.pose[28].v ? A.pose[28].x : 0.55f;
            float ly = A.pose[27].v ? A.pose[27].y : 0.878f;
            float ry_ = A.pose[28].v ? A.pose[28].y : 0.878f;
            /* 2/3 视口变换（同渲染） */
            float x = 0.5f + (((lx + rx) * 0.5f) - 0.5f) * (2.0f / 3.0f);
            float y = 1 - (1 - fmaxf(ly, ry_)) * (2.0f / 3.0f) + 0.02f;
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

static void draw_figure(void) {
    atom_kp_t P[ATOM_KP_COUNT];
    if (A.pose_ms && A.now - A.pose_ms < 500) memcpy(P, A.pose, sizeof(P));
    else atom_pose_sim(A.now / 1000.0f, P);
    /* 2/3 视口，底边锚定 */
    for (int i = 0; i < ATOM_KP_COUNT; i++) {
        if (!P[i].v) continue;
        P[i].x = (0.5f + (P[i].x - 0.5f) * (2.0f / 3.0f)) * RW;
        P[i].y = (1 - (1 - P[i].y) * (2.0f / 3.0f)) * RH;
    }
    float span = P[11].v && P[12].v ? hypotf(P[12].x - P[11].x, P[12].y - P[11].y) : 40;
    float ws = span / 40; if (ws < 0.55f) ws = 0.55f; if (ws > 1.5f) ws = 1.5f;
    A.ws = ws;
    float nx = (P[11].x + P[12].x) * 0.5f, ny = (P[11].y + P[12].y) * 0.5f;
    float hx = (P[23].x + P[24].x) * 0.5f, hy = (P[23].y + P[24].y) * 0.5f;
    rd_thick_line(nx, ny, hx, hy, 26 * ws, C_INK);                   /* 等宽直柱躯干 */
    rd_thick_line(nx, ny, P[13].x, P[13].y, 18 * ws, C_INK);         /* 臂自颈点 */
    rd_thick_line(P[13].x, P[13].y, P[15].x, P[15].y, 18 * ws, C_INK);
    rd_thick_line(nx, ny, P[14].x, P[14].y, 18 * ws, C_INK);
    rd_thick_line(P[14].x, P[14].y, P[16].x, P[16].y, 18 * ws, C_INK);
    rd_thick_line(P[23].x, P[23].y, P[25].x, P[25].y, 20 * ws, C_INK);
    rd_thick_line(P[25].x, P[25].y, P[27].x, P[27].y, 20 * ws, C_INK);
    rd_thick_line(P[24].x, P[24].y, P[26].x, P[26].y, 20 * ws, C_INK);
    rd_thick_line(P[26].x, P[26].y, P[28].x, P[28].y, 20 * ws, C_INK);
    rd_fill_circle(P[15].x, P[15].y, 13 * ws, C_INK);                /* 手脚圆头 */
    rd_fill_circle(P[16].x, P[16].y, 13 * ws, C_INK);
    rd_fill_circle(P[27].x, P[27].y, 14 * ws, C_INK);
    rd_fill_circle(P[28].x, P[28].y, 14 * ws, C_INK);
    float hcx = (P[7].x + P[8].x) * 0.5f, hcy = (P[7].y + P[8].y) * 0.5f - 17 * ws;
    rd_fill_circle(hcx, hcy, 26 * ws, C_INK);                        /* 白色实心圆头 */
}

static void draw_jump(void) {
    rd_clear(0xFF0A0C0B);
    int unit = A.goal > 0 ? A.goal : 50;
    int lap = A.count / unit;
    /* 燃起来（简化为底部暖色光晕，放射光工程接力）：每满一圈升一级 */
    int fire = lap > 3 ? 3 : lap;
    if (fire > 0)
        for (int i = 0; i < 3; i++)
            rd_fill_ellipse(RW / 2.0f, RH + 40, 300 - i * 60, 130 - i * 25,
                            lerp_c(0xFFFF7814, 0xFFFF7814, 0, 0.05f * fire + 0.02f * i));
    /* 贴边进度环 */
    float frac;
    if (A.goal == TEST_GOAL) frac = 1 - (float)elapsed_ms() / test_ms();
    else frac = (float)(A.count % unit) / unit;
    rd_ring_arc(233, 233, 226, 13, 1, 0x17FFFFFF);
    rd_ring_arc(233, 233, 226, 13, frac < 0 ? 0 : frac, C_GREEN);
    /* 顶点计时 */
    char buf[32];
    uint32_t t = A.goal == TEST_GOAL
        ? (elapsed_ms() > test_ms() ? 0 : test_ms() - elapsed_ms()) : elapsed_ms();
    snprintf(buf, sizeof(buf), "%u:%02u", t / 60000, (t / 1000) % 60);
    float tw = rd_7seg_str(0, 0, -20, 3, buf, 0);
    uint32_t tcol = (A.goal == TEST_GOAL && t <= 10000) ? C_AMBER : C_SOFT;
    rd_7seg_str((RW - tw) / 2.0f, 14, 20, 3, buf, tcol);
    /* 打击数字（格斗式：砸入/压缩/回弹 由 punch 帧驱动） */
    float h = 96;
    /* 帧序（punch 递减索引）：小击 4→1.5 3→0.9 2→1.05 1→1；重击 6→1.9 ... */
    static const float PS[7]  = {1, 1, 1.05f, 0.9f, 1.5f, 1, 1};
    static const float PB[7]  = {1, 1, 1.02f, 0.96f, 1.15f, 0.85f, 1.9f};
    if (A.punch > 0) h *= (A.punch_big ? PB : PS)[A.punch];
    snprintf(buf, sizeof(buf), "%d", A.count);
    float cw = rd_7seg_str(0, 0, -h, h * 0.14f, buf, 0);
    uint32_t ccol = A.goal_hit ? C_AMBER : C_INK;
    if (A.punch >= (A.punch_big ? 6 : 4)) ccol = C_GOLD;             /* 第0帧金闪 */
    rd_7seg_str((RW - cw) / 2.0f, 155 - h, h, h * 0.14f, buf, ccol);
    /* 目标倍数星（C 绘制五角星，设备无 SVG 栈） */
    for (int i = 0; i < lap && i < 6; i++)
        rd_star((RW + cw) / 2.0f + 24 + (i % 3) * 26, 155 - h + 14 + (i / 3) * 26, 11, C_GREEN);
    draw_figure();
    draw_ripples();
    if (A.paused) {
        rd_fill_circle(233, 233, 466, 0xB00A0C0B);
        rd_text((RW - rd_text_w(4, "PAUSED")) / 2, 210, 4, "PAUSED", C_INK);
    }
}

static void draw_simple_figure(float cx, float cy, float s) {   /* 静态 Bono（无脸极简版） */
    rd_fill_circle(cx, cy - 31 * s, 15.5f * s, C_INK);
    rd_thick_line(cx, cy - 9 * s, cx, cy + 23 * s, 13 * s, C_INK);
    rd_thick_line(cx, cy - 3 * s, cx - 17 * s, cy + 11 * s, 9 * s, C_INK);
    rd_thick_line(cx, cy - 3 * s, cx + 17 * s, cy + 11 * s, 9 * s, C_INK);
    rd_thick_line(cx, cy + 23 * s, cx - 12 * s, cy + 45 * s, 9 * s, C_INK);
    rd_thick_line(cx, cy + 23 * s, cx + 12 * s, cy + 45 * s, 9 * s, C_INK);
}

void atom_app_tick(void) {
    A.now = ms();
    /* 倒计时推进 */
    if (A.scr == SCR_READY && A.countdown > 0 && A.now >= A.cd_next) {
        A.countdown--;
        if (A.countdown > 0) { A.cd_next = A.now + 900; speak(A.countdown == 2 ? "二" : "一"); }
        else { A.scr = SCR_JUMP; A.run_since = A.now; speak("跳！"); }
    }
    /* 限时到点 / 暂停超时自动结算 / 语音节点 */
    if (A.scr == SCR_JUMP && !A.paused && A.goal == TEST_GOAL) {
        uint32_t e = elapsed_ms();
        if (e >= test_ms()) session_end();
        else {
            uint32_t rem = test_ms() - e;
            if (rem <= 30000 && !A.spoke30) { A.spoke30 = 1; speak("还有三十秒"); }
            if (rem <= 10000 && !A.spoke10) { A.spoke10 = 1; speak("最后十秒，冲刺！"); }
        }
    }
    if (A.scr == SCR_JUMP && A.paused && A.now - A.pause_since > 5000) session_end();
    if (A.scr == SCR_CELEB && A.now >= A.celeb_until) A.scr = SCR_RESULT;
    if (A.punch > 0) A.punch--;

    /* ---- 渲染 ---- */
    char buf[48];
    switch (A.scr) {
    case SCR_LAUNCH:
        rd_clear(0xFF0A0C0B);
        draw_simple_figure(233, 180, 1.4f);
        rd_text((RW - rd_text_w(3, "DAILY JUMP")) / 2, 290, 3, "DAILY JUMP", C_INK);
        snprintf(buf, sizeof(buf), "STREAK %d", A.pf.streak);
        rd_text((RW - rd_text_w(2, buf)) / 2, 330, 2, buf, C_SOFT);
        rd_text((RW - rd_text_w(2, "TAP TO ENTER")) / 2, 390, 2, "TAP TO ENTER", C_SOFT);
        break;
    case SCR_IDLE: {
        rd_clear(0xFF0A0C0B);
        snprintf(buf, sizeof(buf), "%d", A.pf.streak);
        float w = rd_7seg_str(0, 0, -64, 9, buf, 0);
        rd_7seg_str((RW - w) / 2, 60, 64, 9, buf, C_INK);
        rd_text((RW - rd_text_w(2, "DAY STREAK")) / 2, 145, 2, "DAY STREAK", C_SOFT);
        snprintf(buf, sizeof(buf), "TODAY %d", A.pf.today_count);
        rd_text((RW - rd_text_w(3, buf)) / 2, 185, 3, buf, C_INK);
        snprintf(buf, sizeof(buf), "PB %d  1' %d", A.pf.best_session, A.pf.best_1min);
        rd_text((RW - rd_text_w(2, buf)) / 2, 225, 2, buf, C_SOFT);
        /* START 按钮 */
        rd_thick_line(163, 330, 303, 330, 64, C_GREEN);
        rd_text((RW - rd_text_w(3, "START")) / 2, 320, 3, "START", C_INK);
        rd_text((RW - rd_text_w(2, "RECORD   SET")) / 2, 395, 2, "RECORD   SET", C_SOFT);
        break; }
    case SCR_MODE:
        rd_clear(0xFF0A0C0B);
        rd_text((RW - rd_text_w(3, "MODE")) / 2, 80, 3, "MODE", C_INK);
        rd_thick_line(133, 200, 333, 200, 76, 0xFF1B201D);
        snprintf(buf, sizeof(buf), "TIMED %d'", A.pf.test_min);
        rd_text((RW - rd_text_w(3, buf)) / 2, 190, 3, buf, C_GREEN);
        rd_thick_line(133, 310, 333, 310, 76, 0xFF1B201D);
        snprintf(buf, sizeof(buf), "FREE %d", A.pf.goal_num);
        rd_text((RW - rd_text_w(3, buf)) / 2, 300, 3, buf, C_INK);
        break;
    case SCR_READY:
        rd_clear(0xFF0A0C0B);
        if (A.countdown > 0) {
            snprintf(buf, sizeof(buf), "%d", A.countdown);
            float w = rd_7seg_str(0, 0, -160, 20, buf, 0);
            rd_7seg_str((RW - w) / 2, 150, 160, 20, buf, C_INK);
        } else {
            rd_text((RW - rd_text_w(3, "STAND BY")) / 2, 200, 3, "STAND BY", C_INK);
            rd_text((RW - rd_text_w(2, "STEP IN VIEW")) / 2, 250, 2, "STEP IN VIEW", C_SOFT);
        }
        break;
    case SCR_JUMP: draw_jump(); break;
    case SCR_CELEB:
        draw_jump();
        rd_fill_circle(233, 233, 466, 0x900A0C0B);
        rd_text((RW - rd_text_w(5, A.new_best ? "NEW BEST!" : "DONE!")) / 2, 210, 5,
                A.new_best ? "NEW BEST!" : "DONE!", C_GOLD);
        break;
    case SCR_RESULT: {
        rd_clear(0xFF0A0C0B);
        snprintf(buf, sizeof(buf), "%d", A.count);
        float w = rd_7seg_str(0, 0, -80, 11, buf, 0);
        rd_7seg_str((RW - w) / 2, 70, 80, 11, buf, A.new_best ? C_AMBER : C_INK);
        snprintf(buf, sizeof(buf), "TIME %u:%02u  KCAL %.1f",
                 A.elapsed / 60000, (A.elapsed / 1000) % 60, (double)kcal());
        rd_text((RW - rd_text_w(2, buf)) / 2, 190, 2, buf, C_SOFT);
        if (A.goal == TEST_GOAL && A.pf.test_min == 1) {
            snprintf(buf, sizeof(buf), "RATE %s", RATE_S[rate_idx(A.count)]);
            rd_text((RW - rd_text_w(2, buf)) / 2, 225, 2, buf, C_GREEN);
        }
        snprintf(buf, sizeof(buf), "+%d STARS", A.earned);
        rd_text((RW - rd_text_w(2, buf)) / 2, 260, 2, buf, C_GREEN);
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
