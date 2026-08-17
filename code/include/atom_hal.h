/* ============================================================
 * Atom 跳绳小挑战 · HAL 抽象层
 * 工程师对接点：把这些函数指针接到设备 SDK 上即可。
 * 全部可为 NULL（应用会跳过该能力），便于分步移植。
 * ============================================================ */
#ifndef ATOM_HAL_H
#define ATOM_HAL_H

#include <stdint.h>

#define ATOM_SCREEN_W 466
#define ATOM_SCREEN_H 466
#define ATOM_FPS      15

typedef struct {
    /* 显示：把 ARGB8888 帧缓冲刷到 LCD（每 tick 调用一次） */
    void (*display_flush)(const uint32_t *fb, int w, int h);
    /* 语音：教练播报（接设备 TTS / 预生成语音包；文本为 UTF-8） */
    void (*speak)(const char *utf8_text);
    /* 存储：档案持久化（flash/文件系统），返回实际读写字节数，失败返回 <0 */
    int  (*storage_read)(void *buf, int max_len);
    int  (*storage_write)(const void *buf, int len);
    /* 时钟：开机以来毫秒数（用于计时与动画；单调递增） */
    uint32_t (*millis)(void);
} atom_hal_t;

#endif
