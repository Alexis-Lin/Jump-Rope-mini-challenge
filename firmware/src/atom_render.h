/* 软件光栅渲染器（参考实现；工程侧可整体替换为 GPU/2D 加速接口） */
#ifndef ATOM_RENDER_H
#define ATOM_RENDER_H
#include <stdint.h>

#define RW 466
#define RH 466

void rd_bind(uint32_t *fb);
void rd_clear(uint32_t argb);
void rd_fill_circle(float cx, float cy, float r, uint32_t argb);
void rd_thick_line(float x0, float y0, float x1, float y1, float w, uint32_t argb);
void rd_ring_arc(float cx, float cy, float r, float w, float frac, uint32_t argb); /* 从12点顺时针 */
void rd_ellipse_ring(float cx, float cy, float rx, float ry, float w, uint32_t argb);
void rd_fill_ellipse(float cx, float cy, float rx, float ry, uint32_t argb);
void rd_star(float cx, float cy, float r, uint32_t argb);   /* 五角星（纯 C，无 SVG 依赖） */
/* 七段数字：返回整串宽度；digits 仅 0-9，':' 画为两点，负 h 不绘制只测宽 */
float rd_7seg_str(float x, float y, float h, float sw, const char *s, uint32_t argb);
/* 5x7 标签字体（A-Z 0-9 与少量符号），scale 为像素倍率；返回宽度 */
float rd_text(float x, float y, float scale, const char *s, uint32_t argb);
float rd_text_w(float scale, const char *s);

#endif
