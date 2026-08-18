/* 内置演示动作：连跳(62%腾空+落地屈膝) + 手腕摆绳 + 远近/左右/高低漫游
   BP-28 拓扑（与 Web 原型 simPose() 同一套数学），供无 AI 数据时演示/自检 */
#include "atom_app.h"
#include <math.h>

void atom_pose_sim(float t_sec, atom_kp_t out[ATOM_KP_COUNT]) {
    for (int i = 0; i < ATOM_KP_COUNT; i++) out[i].v = 0;
    const float f = 2.3f;
    float ph = fmodf(t_sec * f, 1.0f);
    float air = ph < 0.62f ? sinf(3.14159f * ph / 0.62f) : 0;
    float crouch = ph >= 0.62f ? 0.018f * sinf(3.14159f * (ph - 0.62f) / 0.38f) : 0;
    float dy = -0.055f * air + crouch;
    float swing = sinf(6.2832f * t_sec * f) * 0.012f;
    struct { int i; float x, y; } P[] = {
        {9, 0.5f, 0.4385f + dy},                                        /* 头(圆心) */
        {8, 0.5f, 0.515f + dy},                                         /* 颈 */
        {7, 0.5f, 0.545f + dy},                                         /* 肩中 */
        {12, 0.435f, 0.545f + dy}, {13, 0.565f, 0.545f + dy},           /* 右肩/左肩 */
        {11, 0.385f, 0.615f + dy}, {14, 0.615f, 0.615f + dy},           /* 右肘/左肘 */
        {10, 0.36f + swing, 0.665f + dy * 0.7f},                        /* 右腕 */
        {15, 0.64f - swing, 0.665f + dy * 0.7f},                        /* 左腕 */
        {6, 0.5f, 0.665f + dy},                                         /* 髋中 */
        {2, 0.462f, 0.665f + dy}, {3, 0.538f, 0.665f + dy},             /* 右髋/左髋 */
        {1, 0.455f, 0.775f + dy * 0.75f + crouch}, {4, 0.545f, 0.775f + dy * 0.75f + crouch},
        {0, 0.45f, 0.878f + dy * 0.5f}, {5, 0.55f, 0.878f + dy * 0.5f}, /* 右踝/左踝 */
        {21, 0.443f, 0.888f + dy * 0.5f}, {18, 0.557f, 0.888f + dy * 0.5f}, /* 脚跟 */
    };
    /* 漫游：远近 ±26% / 左右 ±13% / 高低 ±4% */
    float d = 1 + 0.26f * sinf(t_sec * 0.55f + 1.3f) * sinf(t_sec * 0.13f + 0.4f);
    float ox = 0.09f * sinf(t_sec * 0.31f + 0.7f) + 0.04f * sinf(t_sec * 0.09f);
    float oy = 0.04f * sinf(t_sec * 0.22f + 2.1f);
    for (unsigned k = 0; k < sizeof(P) / sizeof(P[0]); k++) {
        out[P[k].i].x = 0.5f + (P[k].x - 0.5f) * d + ox;
        out[P[k].i].y = 0.878f + (P[k].y - 0.878f) * d + oy;
        out[P[k].i].v = 1;
    }
}
