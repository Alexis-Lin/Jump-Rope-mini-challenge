#include "atom_render.h"
#include <math.h>
#include <string.h>

static uint32_t *FB;

void rd_bind(uint32_t *fb) { FB = fb; }

static inline void px(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= RW || y >= RH) return;
    uint32_t a = c >> 24;
    if (a == 0) return;
    if (a == 255) { FB[y * RW + x] = c | 0xFF000000u; return; }
    uint32_t d = FB[y * RW + x];
    uint32_t rb = ((((c & 0xFF00FF) * a) + ((d & 0xFF00FF) * (255 - a))) >> 8) & 0xFF00FF;
    uint32_t g  = ((((c & 0x00FF00) * a) + ((d & 0x00FF00) * (255 - a))) >> 8) & 0x00FF00;
    FB[y * RW + x] = 0xFF000000u | rb | g;
}

void rd_clear(uint32_t c) { for (int i = 0; i < RW * RH; i++) FB[i] = c | 0xFF000000u; }

void rd_fill_circle(float cx, float cy, float r, uint32_t c) {
    int x0 = (int)(cx - r - 1), x1 = (int)(cx + r + 1);
    int y0 = (int)(cy - r - 1), y1 = (int)(cy + r + 1);
    float r2 = r * r;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= r2) px(x, y, c);
        }
}

void rd_thick_line(float x0, float y0, float x1, float y1, float w, uint32_t c) {
    float hw = w * 0.5f;
    float minx = fminf(x0, x1) - hw - 1, maxx = fmaxf(x0, x1) + hw + 1;
    float miny = fminf(y0, y1) - hw - 1, maxy = fmaxf(y0, y1) + hw + 1;
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    for (int y = (int)miny; y <= (int)maxy; y++)
        for (int x = (int)minx; x <= (int)maxx; x++) {
            float t = len2 > 0 ? ((x - x0) * dx + (y - y0) * dy) / len2 : 0;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            float ex = x0 + t * dx - x, ey = y0 + t * dy - y;
            if (ex * ex + ey * ey <= hw * hw) px(x, y, c);
        }
}

void rd_ring_arc(float cx, float cy, float r, float w, float frac, uint32_t c) {
    if (frac <= 0) return;
    if (frac > 1) frac = 1;
    float ro = r + w * 0.5f, ri = r - w * 0.5f;
    float ro2 = ro * ro, ri2 = ri * ri;
    float a1 = frac * 6.2831853f;
    for (int y = (int)(cy - ro - 1); y <= (int)(cy + ro + 1); y++)
        for (int x = (int)(cx - ro - 1); x <= (int)(cx + ro + 1); x++) {
            float dx = x - cx, dy = y - cy;
            float d2 = dx * dx + dy * dy;
            if (d2 < ri2 || d2 > ro2) continue;
            float ang = atan2f(dx, -dy);            /* 12点=0，顺时针 */
            if (ang < 0) ang += 6.2831853f;
            if (ang <= a1) px(x, y, c);
        }
}

void rd_ellipse_ring(float cx, float cy, float rx, float ry, float w, uint32_t c) {
    int n = (int)(rx * 1.2f) + 48;
    for (int i = 0; i < n; i++) {
        float a = 6.2831853f * i / n;
        rd_fill_circle(cx + rx * cosf(a), cy + ry * sinf(a), w * 0.5f, c);
    }
}

void rd_fill_ellipse(float cx, float cy, float rx, float ry, uint32_t c) {
    for (int y = (int)(cy - ry - 1); y <= (int)(cy + ry + 1); y++)
        for (int x = (int)(cx - rx - 1); x <= (int)(cx + rx + 1); x++) {
            float dx = (x - cx) / rx, dy = (y - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) px(x, y, c);
        }
}

void rd_star(float cx, float cy, float r, uint32_t c) {
    /* 10 顶点星形多边形，射线法填充 */
    float vx[10], vy[10];
    for (int i = 0; i < 10; i++) {
        float rr = (i % 2 == 0) ? r : r * 0.42f;
        float a = -1.5708f + i * 0.62832f;
        vx[i] = cx + rr * cosf(a);
        vy[i] = cy + rr * sinf(a);
    }
    for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
        for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
            int in = 0;
            for (int i = 0, j = 9; i < 10; j = i++)
                if ((vy[i] > y) != (vy[j] > y) &&
                    x < (vx[j] - vx[i]) * (y - vy[i]) / (vy[j] - vy[i]) + vx[i])
                    in = !in;
            if (in) px(x, y, c);
        }
}

/* ---------------- 七段数字 ----------------
   段布局: A 上横 B 右上 C 右下 D 下横 E 左下 F 左上 G 中横 */
static const uint8_t SEG[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};
static void seg_digit(float x, float y, float h, float sw, int d, uint32_t c) {
    float w = h * 0.52f, m = h * 0.5f;
    uint8_t s = SEG[d];
    if (s & 0x01) rd_thick_line(x, y,     x + w, y,     sw, c);          /* A */
    if (s & 0x02) rd_thick_line(x + w, y, x + w, y + m, sw, c);          /* B */
    if (s & 0x04) rd_thick_line(x + w, y + m, x + w, y + h, sw, c);      /* C */
    if (s & 0x08) rd_thick_line(x, y + h, x + w, y + h, sw, c);          /* D */
    if (s & 0x10) rd_thick_line(x, y + m, x, y + h, sw, c);              /* E */
    if (s & 0x20) rd_thick_line(x, y,     x, y + m, sw, c);              /* F */
    if (s & 0x40) rd_thick_line(x, y + m, x + w, y + m, sw, c);          /* G */
}
float rd_7seg_str(float x, float y, float h, float sw, const char *s, uint32_t c) {
    float w = h * 0.52f, gap = h * 0.22f, cx = x;
    int draw = h > 0;
    float ah = fabsf(h);
    float aw = ah * 0.52f, agap = ah * 0.22f;
    for (const char *p = s; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            if (draw) seg_digit(cx, y, h, sw, *p - '0', c);
            cx += aw + agap;
        } else if (*p == ':') {
            if (draw) {
                rd_fill_circle(cx + agap * 0.6f, y + ah * 0.3f, sw * 0.55f, c);
                rd_fill_circle(cx + agap * 0.6f, y + ah * 0.7f, sw * 0.55f, c);
            }
            cx += agap * 2.2f;
        } else if (*p == ' ') cx += aw * 0.6f;
    }
    (void)w; (void)gap;
    return cx - x;
}

/* ---------------- 5x7 标签字体（子集） ---------------- */
typedef struct { char ch; uint8_t r[7]; } glyph_t;
static const glyph_t G[] = {
    {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D',{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G',{0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}}, {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I',{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J',{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N',{0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}}, {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}}, {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}}, {'W',{0x11,0x11,0x11,0x15,0x15,0x1B,0x11}},
    {'Y',{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'X',{0x11,0x0A,0x04,0x04,0x04,0x0A,0x11}},
    {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3',{0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
    {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5',{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6',{0x0E,0x10,0x1E,0x11,0x11,0x11,0x0E}}, {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9',{0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}},
    {'\'',{0x04,0x04,0x08,0x00,0x00,0x00,0x00}},{':',{0x00,0x04,0x00,0x00,0x04,0x00,0x00}},
    {'.',{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}}, {'/',{0x01,0x02,0x02,0x04,0x08,0x08,0x10}},
    {'+',{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}}, {'-',{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {'!',{0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
};
static const glyph_t *find_glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    for (unsigned i = 0; i < sizeof(G) / sizeof(G[0]); i++)
        if (G[i].ch == ch) return &G[i];
    return 0;
}
float rd_text(float x, float y, float sc, const char *s, uint32_t c) {
    float cx = x;
    for (const char *p = s; *p; p++) {
        if (*p == ' ') { cx += 4 * sc; continue; }
        const glyph_t *g = find_glyph(*p);
        if (!g) { cx += 6 * sc; continue; }
        for (int r = 0; r < 7; r++)
            for (int b = 0; b < 5; b++)
                if (g->r[r] & (0x10 >> b))
                    for (int yy = 0; yy < (int)sc; yy++)
                        for (int xx = 0; xx < (int)sc; xx++)
                            px((int)(cx + b * sc + xx), (int)(y + r * sc + yy), c);
        cx += 6 * sc;
    }
    return cx - x;
}
float rd_text_w(float sc, const char *s) {
    float w = 0;
    for (const char *p = s; *p; p++) w += (*p == ' ' ? 4 : 6) * sc;
    return w;
}
