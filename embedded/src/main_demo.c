/* 桌面演示 harness：模拟 15Hz 主循环 + AI 喂点/计数，输出关键帧 BMP
   真机上请删除本文件，把 HAL 接到设备 SDK，主循环 15Hz 调 atom_app_tick() */
#include "atom_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void atom_pose_sim(float t_sec, atom_kp_t out[ATOM_KP_COUNT]);

static uint32_t sim_ms = 0;
static uint32_t hal_millis(void) { return sim_ms; }
static void hal_speak(const char *t) { printf("[TTS] %s\n", t); }
static uint8_t store[256]; static int store_len = 0;
static int hal_read(void *b, int n) { if (!store_len) return -1; if (n > store_len) n = store_len; memcpy(b, store, n); return n; }
static int hal_write(const void *b, int n) { if (n > (int)sizeof(store)) n = sizeof(store); memcpy(store, b, n); store_len = n; return n; }

static const uint32_t *cur_fb;
static void hal_flush(const uint32_t *fb, int w, int h) { cur_fb = fb; (void)w; (void)h; }

static void save_bmp(const char *name) {
    int w = ATOM_SCREEN_W, h = ATOM_SCREEN_H;
    FILE *f = fopen(name, "wb");
    if (!f) return;
    int rowsz = w * 3, pad = (4 - rowsz % 4) % 4, datasz = (rowsz + pad) * h;
    uint8_t hd[54] = {'B','M'};
    *(uint32_t *)(hd + 2) = 54 + datasz; *(uint32_t *)(hd + 10) = 54;
    *(uint32_t *)(hd + 14) = 40; *(int32_t *)(hd + 18) = w; *(int32_t *)(hd + 22) = h;
    *(uint16_t *)(hd + 26) = 1; *(uint16_t *)(hd + 28) = 24; *(uint32_t *)(hd + 34) = datasz;
    fwrite(hd, 1, 54, f);
    uint8_t z[4] = {0};
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t p = cur_fb[y * w + x];
            uint8_t bgr[3] = { (uint8_t)p, (uint8_t)(p >> 8), (uint8_t)(p >> 16) };
            fwrite(bgr, 1, 3, f);
        }
        fwrite(z, 1, pad, f);
    }
    fclose(f);
    printf("[BMP] %s\n", name);
}

static void on_event(atom_event_t e, int p1, int p2, int p3, int p4) {
    printf("[EVT] %d  p1=%d p2=%d p3=%d p4=%d\n", e, p1, p2, p3, p4);
}

int main(void) {
    atom_hal_t hal = { hal_flush, hal_speak, hal_read, hal_write, hal_millis };
    atom_callbacks_t cb = { on_event };
    atom_app_init(&hal, &cb);
    atom_app_profile()->streak = 12;
    atom_app_profile()->best_session = 231;
    atom_app_profile()->best_1min = 154;
    atom_app_profile()->today_count = 221;

    atom_kp_t pose[ATOM_KP_COUNT];
    uint32_t next_jump = 0;
    int frame = 0;
    system("mkdir -p out");
    for (int tick = 0; tick < 15 * 30; tick++) {          /* 30 秒 @15Hz */
        sim_ms += 66;
        /* 剧本：0.5s 点入 APP → 1s 点 START → 1.5s 选不限时 → 站位 → 连跳 */
        if (tick == 8)  atom_app_touch(233, 400);
        if (tick == 16) atom_app_touch(233, 350);
        if (tick == 24) atom_app_touch(233, 320);
        if (tick == 30) atom_app_set_presence(true);
        atom_pose_sim(sim_ms / 1000.0f, pose);
        atom_app_feed_pose(pose);
        if (tick > 75 && sim_ms >= next_jump) {           /* ~2.2 跳/秒 */
            atom_app_on_jump();
            next_jump = sim_ms + 450;
        }
        if (tick == 15 * 28) atom_app_end_session();
        atom_app_tick();
        if (tick == 12 || tick == 20 || tick == 40 || tick == 90 ||
            tick == 200 || tick == 300 || tick == 15 * 28 + 2 || tick == 15 * 29)
            { char n[64]; snprintf(n, sizeof(n), "out/frame_%03d.bmp", frame++); save_bmp(n); }
    }
    puts("demo done");
    return 0;
}
