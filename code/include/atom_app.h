/* ============================================================
 * Atom 跳绳小挑战 · 应用公共 API（与 Web 版 AtomApp/AtomBridge 契约一致）
 * 行为基准：docs-and-demo/demo.html（2026-08-18，含 08-17 评审会及会后决议）
 *
 * 调用模型：
 *   atom_app_init(&hal, &callbacks);
 *   循环 @15Hz: atom_app_tick();            // 状态推进 + 渲染 + display_flush
 *   AI 管线随时调用: feed_pose / on_jump / set_presence / form_hint
 *   触屏事件: atom_app_touch(x, y)
 * ============================================================ */
#ifndef ATOM_APP_H
#define ATOM_APP_H

#include <stdbool.h>
#include "atom_hal.h"

/* ---- 骨骼关键点（MediaPipe Pose 33 点拓扑；归一化 0-1，原点左上，已镜像） ---- */
typedef struct { float x, y, v; } atom_kp_t;
#define ATOM_KP_COUNT 33
/* 关键索引：0 鼻 7/8 耳 11/12 肩 13/14 肘 15/16 腕 23/24 髋 25/26 膝 27/28 踝
 * 手指(17-22)/面部(除双耳)/脚趾(29-32)一律忽略，不渲染 */

/* ---- 动作纠错事件种类（AI 管线 → 应用；2026-08-17 评审会新增） ---- */
typedef enum {
    ATOM_FORM_ALTERNATING,   /* 交替跳（不计数）→ "双脚并拢跳" */
    ATOM_FORM_SINGLE_FOOT,   /* 单脚跳（不计数） */
    ATOM_FORM_LOW_JUMP,      /* 幅度不足 */
    ATOM_FORM_LOW_LIGHT,     /* 识别置信度低/光线不足 */
} atom_form_kind_t;

/* ---- 事件出向（上报后端：排行榜 / 云打卡 / 埋点） ---- */
typedef enum {
    ATOM_EV_APP_READY,
    ATOM_EV_SESSION_START,   /* p1 = goal(-1 限时), p2 = test_min */
    ATOM_EV_JUMP,            /* p1 = count */
    ATOM_EV_SESSION_END,     /* p1 = count, p2 = elapsed_ms, p3 = kcal x10, p4 = new_best
                                （count=0 的空轮不发结算事件，端上直接回首页） */
    ATOM_EV_CHECKIN,         /* p1 = streak */
    ATOM_EV_FORM_HINT,       /* p1 = atom_form_kind_t（上报做规则调优） */
} atom_event_t;

typedef struct {
    void (*on_event)(atom_event_t ev, int p1, int p2, int p3, int p4);
} atom_callbacks_t;

/* ---- 用户档案（工程侧可整体注入 / 读取做云同步）
 * 身高/体重/性别/年龄从账号 profile（identity.personal）带入，设备端不再设置。 ---- */
typedef struct {
    int32_t goal_num;        /* 每日目标：50 / 100 / 200（会后定稿，无自由档） */
    int32_t test_min;        /* 限时时长（分钟：1 / 2，默认 1 主推） */
    int32_t streak;          /* 连胜天数（服务端时间口径，云端为准） */
    int32_t max_streak;      /* 最长连胜（最佳战绩维度） */
    int32_t stars;           /* 星星积分（货币星） */
    int32_t total;           /* 累计跳数 */
    int32_t best_session;    /* 单轮 PB */
    int32_t best_1min;       /* 1 分钟最佳 */
    int32_t best_2min;       /* 2 分钟最佳 */
    int32_t today_count;     /* 今日累计（跨轮累加；跨天清零由工程侧 atom_app_new_day） */
    int32_t today_best;      /* 今日单轮最高（榜单口径，防刷） */
    int32_t today_rounds;    /* 今日轮数（仅数据，不上屏） */
    int32_t weight_kg;       /* 体重（卡路里估算；来源 identity.personal.weight） */
    int32_t age_band;        /* 0:6-8 1:9-11 2:12-14 3:15-17 4:成人（派生自 birth_date） */
} atom_profile_t;

/* ---- 生命周期 ---- */
void atom_app_init(const atom_hal_t *hal, const atom_callbacks_t *cb);
void atom_app_tick(void);                       /* 以 15Hz 调用 */

/* ---- AI 管线入向（与 Web 版 AtomApp.* 一一对应） ---- */
void atom_app_feed_pose(const atom_kp_t pts[ATOM_KP_COUNT]);  /* >=15Hz */
void atom_app_on_jump(void);                    /* AI 判定一次有效跳跃（并脚口径，≤3 跳/秒） */
void atom_app_set_presence(bool present);       /* 仅训练中：false 立即暂停停表；
                                                   超 3 分钟未回自动结算（不作为开跳门槛） */
void atom_app_form_hint(atom_form_kind_t kind); /* 动作纠错：教练语音 + 事件上报（≥20s 频控） */

/* ---- 导航控制（触屏 / 语音助手 / 物理按键） ---- */
void atom_app_touch(int x, int y);              /* 按下事件坐标 */
void atom_app_start(bool timed);                /* 编程式开跳（选模式即 3·2·1·BodyPark） */
void atom_app_home(void);
void atom_app_end_session(void);

/* ---- 数据 ---- */
atom_profile_t *atom_app_profile(void);         /* 可读写；写后调 atom_app_save() */
void atom_app_save(void);
void atom_app_new_day(void);                    /* 工程侧在跨天时调用：清今日字段（按 session 开始时间归天） */

#endif
