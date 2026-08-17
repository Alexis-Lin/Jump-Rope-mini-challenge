/* ============================================================
 * Atom 跳绳小挑战 · 应用公共 API（与 Web 版 AtomApp/AtomBridge 契约一致）
 *
 * 调用模型：
 *   atom_app_init(&hal, &callbacks);
 *   循环 @15Hz: atom_app_tick();            // 状态推进 + 渲染 + display_flush
 *   AI 管线随时调用: feed_pose / on_jump / set_presence
 *   触屏事件: atom_app_touch(x, y)
 * ============================================================ */
#ifndef ATOM_APP_H
#define ATOM_APP_H

#include <stdbool.h>
#include "atom_hal.h"

/* ---- 骨骼关键点（MediaPipe Pose 33 点拓扑；归一化 0-1，原点左上，已镜像） ---- */
typedef struct { float x, y, v; } atom_kp_t;
#define ATOM_KP_COUNT 33
/* 关键索引：0 鼻 7/8 耳 11/12 肩 13/14 肘 15/16 腕 23/24 髋 25/26 膝 27/28 踝 */

/* ---- 事件出向（上报后端：排行榜 / 云打卡 / 埋点） ---- */
typedef enum {
    ATOM_EV_APP_READY,
    ATOM_EV_SESSION_START,   /* p1 = goal(-1 限时), p2 = test_min */
    ATOM_EV_JUMP,            /* p1 = count */
    ATOM_EV_SESSION_END,     /* p1 = count, p2 = elapsed_ms, p3 = kcal x10, p4 = new_best */
    ATOM_EV_CHECKIN,         /* p1 = streak */
} atom_event_t;

typedef struct {
    void (*on_event)(atom_event_t ev, int p1, int p2, int p3, int p4);
} atom_callbacks_t;

/* ---- 用户档案（工程侧可整体注入 / 读取做云同步） ---- */
typedef struct {
    int32_t goal_num;        /* 每日目标（0=自由） */
    int32_t test_min;        /* 限时时长（分钟：1/2） */
    int32_t streak;          /* 连胜天数 */
    int32_t stars;           /* 星星积分 */
    int32_t total;           /* 累计跳数 */
    int32_t best_session;    /* 单轮 PB */
    int32_t best_1min;       /* 1 分钟最佳 */
    int32_t best_2min;       /* 2 分钟最佳 */
    int32_t today_count;     /* 今日累计（跨天清零由工程侧驱动 atom_app_new_day） */
    int32_t weight_kg;       /* 体重（卡路里估算用） */
    int32_t age_band;        /* 0:6-8 1:9-11 2:12-14 3:15-17 4:成人（体测评级用） */
} atom_profile_t;

/* ---- 生命周期 ---- */
void atom_app_init(const atom_hal_t *hal, const atom_callbacks_t *cb);
void atom_app_tick(void);                       /* 以 15Hz 调用 */

/* ---- AI 管线入向（与 Web 版 AtomApp.* 一一对应） ---- */
void atom_app_feed_pose(const atom_kp_t pts[ATOM_KP_COUNT]);  /* >=15Hz */
void atom_app_on_jump(void);                    /* AI 判定一次有效跳跃 */
void atom_app_set_presence(bool present);       /* 站位判定（固件侧去抖） */

/* ---- 导航控制（触屏 / 语音助手 / 物理按键） ---- */
void atom_app_touch(int x, int y);              /* 按下事件坐标 */
void atom_app_start(bool timed);                /* 编程式开跳 */
void atom_app_home(void);
void atom_app_end_session(void);

/* ---- 数据 ---- */
atom_profile_t *atom_app_profile(void);         /* 可读写；写后调 atom_app_save() */
void atom_app_save(void);
void atom_app_new_day(void);                    /* 工程侧在跨天时调用：清今日/结算连胜 */

#endif
