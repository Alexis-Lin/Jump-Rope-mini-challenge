/* ============================================================
 * Atom 跳绳小挑战 · 应用公共 API（与 Web 版 AtomApp/AtomBridge 契约一致）
 * 行为基准：docs-and-demo/demo.html（2026-08-18，含 08-17 / 08-18 两次评审会决议）
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

/* ---- 骨骼关键点（BP-28 自研拓扑 = AI 后台 KeypointNameEnum；归一化 0-1，原点左上）
 *  镜像 = 照镜子（已确认）：用户举右手 → 画面右侧手抬起（r* 的 x 大于 l*，喂入前已翻转）
 *  推流 15-20Hz；单点短暂缺失由 feed 层补间保持 ≤400ms，超时该部件隐藏 ---- */
typedef struct { float x, y, v; } atom_kp_t;
#define ATOM_KP_COUNT 28
/* 索引：0 右踝 1 右膝 2 右髋 3 左髋 4 左膝 5 左踝 / 6 髋中 7 肩中 8 颈 9 头 /
 *       10 右腕 11 右肘 12 右肩 13 左肩 14 左肘 15 左腕 /
 *       16-21 脚·趾·跟(仅 18/21 脚跟作涟漪落点) 22 腰椎 23-26 指尖 27 鼻(不渲染)
 * 渲染使用 15 点：头 9 · 肩中 7 · 髋中 6 · 肩 12/13 · 肘 11/14 · 腕 10/15 ·
 *                髋 2/3 · 膝 1/4 · 踝 0/5（脚跟 18/21 可选） */

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
    int32_t goal_num;        /* freestyle goal 目标环：50 / 100 / 200，默认 200（8-18 由每日目标更名定稿） */
    int32_t test_min;        /* 限时时长（8-18 定稿:固定 1 分钟；2 为历史档，best_2min 仅存档） */
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
