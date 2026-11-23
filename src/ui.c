/* ArchiveTV GBA-style UI. Pure drawing code (no SDL), so it can also be built on a PC to render previews. */
#ifdef HOST
#include <string.h>
#include <stdio.h>
#else
#include "sys.h"
#endif
#include "ui.h"
#include "font_m5x7.h"

static int RW = 640, RH = 480;
int UW = 320, UH = 240, UVIS = 9;
#define OY ((UH - 240) / 2)   /* centres the fixed-layout screens on taller displays */
#define C(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

/* ---------------- themes ---------------- */
typedef struct {
    const char *name;
    uint32_t outline, text, ink, dim, win, panel, rim, rowalt, hilite, hiedge, hitext;
    uint32_t accent, accentd, accentl, yellow, toast, sky, skyl, teal, lav, mint, shadow, bg1, bg2, good, warn, track, scrtv, pilltext;
    uint32_t bars[7];
    int anim;
    uint32_t limb;      /* antennas; 0 = outline colour */
    uint32_t feet;      /* feet fill with an outline stroke; 0 = plain outline-coloured feet */
    uint32_t more;      /* colour of action rows ("Search videos...", "More..."); 0 = accentd */
    uint32_t tab;       /* selected tab pill; 0 = accent */
} Theme;
static const Theme THEMES[] = {
    { "Cute",
      C(44,48,92), C(44,48,92), C(60,58,104), C(150,140,170), C(255,246,226), C(255,252,244), C(255,252,244), C(250,244,236), C(255,214,92), C(236,186,60), C(44,48,92),
      C(244,104,136), C(206,70,108), C(255,170,186), C(255,214,92), C(255,236,160), C(150,210,246), C(206,236,255), C(76,190,172), C(182,160,232), C(150,226,170), C(214,178,150), C(250,226,200), C(244,212,182), C(40,160,110), C(226,140,40), C(236,228,220), C(150,210,246), C(255,255,255),
      { C(236,236,236), C(255,222,110), C(130,218,224), C(140,220,140), C(234,140,214), C(244,112,120), C(120,140,226) }, 1 },
    { "Night",
      C(10,10,24), C(232,232,250), C(200,200,230), C(130,130,170), C(40,42,78), C(30,32,62), C(70,74,120), C(36,38,72), C(120,90,220), C(80,60,170), C(255,255,255),
      C(255,110,160), C(200,70,130), C(255,170,210), C(255,214,92), C(70,60,120), C(110,200,255), C(180,230,255), C(80,220,200), C(150,130,230), C(120,255,180), C(8,8,20), C(22,22,44), C(28,28,54), C(110,230,160), C(255,190,90), C(60,62,100), C(110,200,255), C(255,255,255),
      { C(90,90,120), C(160,140,60), C(60,140,150), C(60,140,80), C(150,70,140), C(160,60,70), C(60,70,160) }, 1, C(160,160,214), C(150,44,96), C(140,205,255), C(120,90,220) },
    { "Game Boy",
      C(15,56,15), C(15,56,15), C(15,56,15), C(48,98,48), C(155,188,15), C(155,188,15), C(139,172,15), C(145,180,15), C(48,98,48), C(15,56,15), C(155,188,15),
      C(48,98,48), C(15,56,15), C(139,172,15), C(139,172,15), C(139,172,15), C(139,172,15), C(155,188,15), C(48,98,48), C(48,98,48), C(155,188,15), C(15,56,15), C(139,172,15), C(130,162,15), C(15,56,15), C(15,56,15), C(139,172,15), C(139,172,15), C(155,188,15),
      { C(155,188,15), C(139,172,15), C(48,98,48), C(15,56,15), C(48,98,48), C(139,172,15), C(155,188,15) }, 0 },
    { "Plain",
      C(70,70,80), C(30,30,36), C(60,60,70), C(130,130,140), C(240,240,244), C(255,255,255), C(255,255,255), C(246,246,250), C(60,120,230), C(40,90,190), C(255,255,255),
      C(60,120,230), C(40,90,190), C(160,190,240), C(230,190,60), C(255,244,200), C(170,200,240), C(220,234,255), C(40,160,150), C(140,140,200), C(120,230,150), C(200,200,206), C(214,214,220), C(214,214,220), C(30,140,80), C(200,110,20), C(224,224,230), C(170,200,240), C(255,255,255),
      { C(214,214,220), C(214,214,220), C(214,214,220), C(214,214,220), C(214,214,220), C(214,214,220), C(214,214,220) }, 0 },
};
#define NTHEMES ((int)(sizeof THEMES / sizeof THEMES[0]))
static const Theme *TH = &THEMES[0];
int ui_theme_count(void) { return NTHEMES; }
const char *ui_theme_name(int i) { return THEMES[(i % NTHEMES + NTHEMES) % NTHEMES].name; }
void ui_set_theme(int i) { TH = &THEMES[(i % NTHEMES + NTHEMES) % NTHEMES]; }

#define NAVY    (TH->outline)
#define TEXT    (TH->text)
#define INK     (TH->ink)
#define DIM     (TH->dim)
#define CREAM   (TH->win)
#define WHITE   (TH->panel)
#define RIM     (TH->rim)
#define ROWALT  (TH->rowalt)
#define HILITE  (TH->hilite)
#define HIEDGE  (TH->hiedge)
#define HITEXT  (TH->hitext)
#define PINK    (TH->accent)
#define PINKD   (TH->accentd)
#define BLUSH   (TH->accentl)
#define YELLOW  (TH->yellow)
#define YELLOWL (TH->toast)
#define SKY     (TH->sky)
#define SKYL    (TH->skyl)
#define TEAL    (TH->teal)
#define LAV     (TH->lav)
#define MINT    (TH->mint)
#define SHADOW  (TH->shadow)
#define BG1     (TH->bg1)
#define BG2     (TH->bg2)
#define GOOD    (TH->good)
#define WARN    (TH->warn)
#define TRACK   (TH->track)
#define PILLTX  (TH->pilltext)
#define LIMB    (TH->limb ? TH->limb : TH->outline)

static uint32_t *T;   /* 640x480 target */

void ui_init(uint32_t *target, int rw, int rh) {
    T = target; RW = rw; RH = rh; UW = rw / 2; UH = rh / 2;
    UVIS = (UH - 80) / UROWH; if (UVIS > 16) UVIS = 16; if (UVIS < 4) UVIS = 4;
}

/* real-pixel rect */
static void rr(uint32_t *b, int x, int y, int w, int h, uint32_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > RW) w = RW - x;
    if (y + h > RH) h = RH - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) { uint32_t *p = b + (y + j) * RW + x; for (int i = 0; i < w; i++) p[i] = c; }
}
/* virtual-pixel rect */
static uint32_t *B;
static void R(int x, int y, int w, int h, uint32_t c) { rr(B, x * 2, y * 2, w * 2, h * 2, c); }
void ui_rect(int x, int y, int w, int h, uint32_t c) { B = T; R(x, y, w, h, c); }
static void P(int x, int y, uint32_t c) { R(x, y, 1, 1, c); }

/* text: the 8x16 font at 1 virtual px = 2 real px */
/* text: m5x7, each font pixel = 3 real pixels (GBA-like scale). x/y are virtual; widths are returned in virtual px.
   maxch keeps the old meaning of "room for maxch 8-px cells" (maxch * 16 real px). */
#define TS 3
static void rglyph(int rx, int ry, unsigned char ch, uint32_t c) {
    if (ch < 32) return;
    const unsigned short *g = M5_ROWS[ch - 32];
    for (int j = 0; j < M5_ROWS_N; j++) {
        unsigned short row = g[j];
        if (!row) continue;
        int i = 0;
        while (i < 16) {
            if (row & (0x8000 >> i)) { int st = i; while (i < 16 && (row & (0x8000 >> i))) i++; rr(B, rx + st * TS, ry + j * TS, (i - st) * TS, TS, c); }
            else i++;
        }
    }
}
static int adv(unsigned char ch) { return ch < 32 ? 0 : M5_ADV[ch - 32] * TS; }
/* one character centred in an 8-px cell (button letters, keyboard keys) */
static void glyph(int x, int y, unsigned char ch, uint32_t c) { rglyph(x * 2 + (16 - adv(ch) + TS) / 2, y * 2 - 6, ch, c); }
static int txt_real(int rx, int ry, const char *s, uint32_t c, int maxw) {
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        int a = adv(*p);
        if (w + a - TS > maxw) break;
        rglyph(rx + w, ry, *p, c);
        w += a;
    }
    return w;
}
static int txt(int x, int y, const char *s, uint32_t c, int maxch) {
    int w = txt_real(x * 2, y * 2 - 6, s, c, maxch * 16);
    return (w + 1) / 2;
}
static int txtw(const char *s, int maxch) {
    int w = 0, maxw = maxch * 16;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) { int a = adv(*p); if (w + a - TS > maxw) break; w += a; }
    return (w > 0 ? w - TS + 1 : 0) / 2 + 1;
}
/* bold-ish text with a soft drop shadow */
static int txt_pop(int x, int y, const char *s, uint32_t c, uint32_t sh, int maxch) {
    txt(x + 1, y + 1, s, sh, maxch);
    return txt(x, y, s, c, maxch);
}
static void txt_center(int cx, int y, const char *s, uint32_t c, int maxch) {
    txt(cx - txtw(s, maxch) / 2, y, s, c, maxch);
}

/* GBA window: rounded navy border, white inner rim, fill, drop shadow */
static void win(int x, int y, int w, int h, uint32_t fill) {
    R(x + 2, y + h, w - 1, 2, SHADOW);
    R(x + w, y + 2, 2, h - 1, SHADOW);
    R(x + 2, y, w - 4, 1, NAVY); R(x + 2, y + h - 1, w - 4, 1, NAVY);
    R(x, y + 2, 1, h - 4, NAVY); R(x + w - 1, y + 2, 1, h - 4, NAVY);
    P(x + 1, y + 1, NAVY); P(x + w - 2, y + 1, NAVY); P(x + 1, y + h - 2, NAVY); P(x + w - 2, y + h - 2, NAVY);
    R(x + 2, y + 1, w - 4, 1, RIM); R(x + 2, y + h - 2, w - 4, 1, RIM);
    R(x + 1, y + 2, 1, h - 4, RIM); R(x + w - 2, y + 2, 1, h - 4, RIM);
    R(x + 2, y + 2, w - 4, h - 4, fill);
}
static void pill(int x, int y, int w, int h, uint32_t fill) {
    R(x + 1, y, w - 2, h, fill); R(x, y + 1, w, h - 2, fill);
}

/* ---------------- the mascot: a little TV ---------------- */
static int MX, MY, MS;
static void M(int x, int y, int w, int h, uint32_t c) { R(MX + x * MS, MY + y * MS, w * MS, h * MS, c); }
static unsigned rng = 12345;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }

/* 22 x 26 units: a 4:3 TV with one short and one long antenna */
static void mascot(int x, int y, int s, int mood, double t) {
    MX = x; MY = y; MS = s;
    int tick = (int)(t * 6);
    int blink = (tick % 15) == 0;
    int wiggle = (tick % 40) < 3;   /* long antenna sways now and then */
    /* antennas: short on the left, long on the right */
    for (int i = 0; i < 4; i++) M(10 - i, 7 - i, 1, 1, LIMB);
    for (int i = 0; i < 7; i++) M(12 + i + (wiggle && i > 3 ? (tick % 2 ? 1 : -1) * (i - 3) / 2 : 0), 7 - i, 1, 1, LIMB);
    /* body */
    M(1, 8, 20, 16, NAVY); M(0, 9, 22, 14, NAVY);
    M(2, 9, 18, 14, PINK); M(1, 10, 20, 12, PINK);
    M(2, 9, 18, 1, BLUSH);                       /* highlight */
    M(2, 22, 18, 1, PINKD);                      /* shade */
    /* 4:3 screen */
    M(2, 10, 14, 11, NAVY);
    uint32_t scr = mood == MOOD_SLEEP ? DIM : SKY;
    M(3, 11, 12, 9, scr);
    M(3, 11, 12, 1, SKYL);
    if (mood == MOOD_STATIC) {
        rng = (unsigned)((int)(t * 12) * 7919);   /* new static 12 times a second */
        for (int j = 0; j < 9; j++) for (int i = 0; i < 12; i++) {
            unsigned v = rnd() & 3;
            M(3 + i, 11 + j, 1, 1, v == 0 ? NAVY : v == 1 ? DIM : v == 2 ? SKYL : PILLTX);
        }
    } else {
        /* eyes */
        if (mood == MOOD_SLEEP || (blink && (mood == MOOD_HAPPY || mood == MOOD_TALK))) { M(5, 14, 2, 1, NAVY); M(11, 14, 2, 1, NAVY); }
        else { M(5, 13, 2, 2, NAVY); M(11, 13, 2, 2, NAVY); M(5, 13, 1, 1, PILLTX); M(11, 13, 1, 1, PILLTX); }
        /* mouth, two rows above the bottom of the screen */
        if (mood == MOOD_SAD) { M(8, 17, 2, 1, NAVY); M(7, 18, 1, 1, NAVY); M(10, 18, 1, 1, NAVY); }
        else if (mood == MOOD_WOW) M(8, 17, 2, 2, NAVY);
        else if (mood == MOOD_TALK) { if ((int)(t * 10) & 1) { M(8, 17, 2, 2, NAVY); } else { M(7, 17, 1, 1, NAVY); M(8, 18, 2, 1, NAVY); M(10, 17, 1, 1, NAVY); } }
        else if (mood == MOOD_SLEEP) M(8, 18, 2, 1, NAVY);
        else { M(7, 17, 1, 1, NAVY); M(8, 18, 2, 1, NAVY); M(10, 17, 1, 1, NAVY); }
        if (mood != MOOD_SLEEP) { M(3, 16, 2, 1, BLUSH); M(13, 16, 2, 1, BLUSH); }
    }
    /* knob + speaker */
    M(17, 11, 3, 3, NAVY); M(18, 12, 1, 1, YELLOW);
    M(17, 16, 3, 1, NAVY); M(17, 18, 3, 1, NAVY);
    /* feet */
    if (TH->feet) {   /* darker pink feet with a dark stroke */
        M(2, 23, 5, 4, NAVY); M(15, 23, 5, 4, NAVY);
        M(3, 24, 3, 2, TH->feet); M(16, 24, 3, 2, TH->feet);
    } else { M(3, 24, 3, 2, NAVY); M(16, 24, 3, 2, NAVY); }
}

/* ---------------- screen furniture ---------------- */
void ui_bg(double t) {
    B = T;
    int off = TH->anim ? (int)(t * 10) % 32 : 0;
    for (int y = -32; y < UH; y += 16)
        for (int x = -32; x < UW; x += 16) {
            int cx = x + off, cy = y + off;
            int odd = ((x + 32) / 16 + (y + 32) / 16) & 1;
            R(cx, cy, 16, 16, odd ? BG1 : BG2);
            if (!odd) { P(cx + 7, cy + 7, BG1); P(cx + 8, cy + 8, BG1); }
        }
}
static int header_still = 0;
void ui_header(const char *title, const char *right, double t, int mood) {
    B = T;
    for (int i = 0; i < 7; i++) { int x0 = i * UW / 7, x1 = (i + 1) * UW / 7; R(x0, 0, x1 - x0, 4, TH->bars[i]); }
    R(0, 4, UW, 1, NAVY);
    win(4, 9, UW - 8, 26, CREAM);
    if (header_still) mascot(10, 7, 1, mood, 1.0);   /* still on the download screen (fixed time: no blink, no bounce) */
    else mascot(10, 7 + ((int)(t * 3) & 1), 1, mood, t);   /* dances on the menus */
    int rw = right && *right ? txtw(right, 16) + 10 : 0;
    txt_pop(40, 14, title, TEXT, YELLOWL, (UW - 52 - rw - 8) / 8);
    if (rw) {
        int px = UW - 12 - rw;
        pill(px, 14, rw, 16, NAVY);
        txt(px + 5, 14, right, MINT, 16);
    }
}
static void button_icon(int x, int y, const char *k, int *w) {
    uint32_t c = LAV, tc = PILLTX;
    if (!strcmp(k, "A")) c = PINK;
    if (TH->anim == 0 && TH->outline == C(15,56,15)) tc = C(155,188,15);
    else if (!strcmp(k, "B")) { c = YELLOW; tc = NAVY; }
    else if (!strcmp(k, "X")) c = SKY;
    else if (!strcmp(k, "Y")) c = TEAL;
    int n = (int)strlen(k);
    if (n == 1) {
        R(x + 3, y, 8, 14, NAVY); R(x + 1, y + 1, 12, 12, NAVY); R(x, y + 3, 14, 8, NAVY);
        R(x + 3, y + 1, 8, 12, c); R(x + 1, y + 3, 12, 8, c); R(x + 2, y + 2, 10, 10, c);
        R(x + 4, y + 2, 4, 1, WHITE);
        glyph(x + 3, y - 1, (unsigned char)k[0], tc);
        *w = 14;
    } else {
        int ww = txtw(k, 12) + 6;
        pill(x, y, ww, 14, NAVY); pill(x + 1, y + 1, ww - 2, 12, c);
        txt(x + 3, y - 1, k, tc, 12);
        *w = ww;
    }
}
/* hints like "A:Open  B:Back  MENU:Quit" */
static char foot_badge[24];
void ui_footer_badge(const char *b) { snprintf(foot_badge, sizeof foot_badge, "%s", b ? b : ""); }
/* touch: where things were last drawn, in virtual pixels */
static int tab_x0[16], tab_x1[16], ntab_hit;
static int foot_x0[10], foot_x1[10], nfoot; static char foot_k[10][8];
static int menu_x, menu_y, menu_w, menu_h, menu_n;
void ui_footer(const char *hints) {
    B = T;
    win(4, UH - 26, UW - 8, 22, CREAM);
    int arrow = foot_badge[0] == 2;
    const char *bt = foot_badge + arrow;
    int bw = foot_badge[0] ? txtw(bt, 12) + 8 + (arrow ? 8 : 0) : 0;
    if (bw) {
        int bx = UW - 10 - bw;
        pill(bx, UH - 22, bw, 15, NAVY);
        if (arrow) {   /* small down arrow */
            int ax = bx + 4, ay = UH - 19;
            R(ax + 2, ay, 2, 5, MINT); R(ax, ay + 4, 6, 1, MINT); R(ax + 1, ay + 5, 4, 1, MINT); R(ax + 2, ay + 6, 2, 1, MINT);
        }
        txt(bx + 4 + (arrow ? 8 : 0), UH - 23, bt, MINT, 12);
    }
    nfoot = 0;
    if (!hints) return;
    int x = 12, y = UH - 22;
    char tok[64];
    const char *p = hints;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        int n = 0;
        while (p[n] && !(p[n] == ' ' && p[n + 1] == ' ') && n < 63) n++;
        memcpy(tok, p, (size_t)n); tok[n] = 0; p += n;
        char *colon = strchr(tok, ':');
        if (colon) {
            *colon = 0; int w = 0;
            int fx = x;
            button_icon(x, y + 1, tok, &w);
            x += w + 4;
            x += txt(x, y, colon + 1, INK, 20) + 12;
            if (nfoot < 10) { foot_x0[nfoot] = fx - 4; foot_x1[nfoot] = x - 6; snprintf(foot_k[nfoot], sizeof foot_k[0], "%s", tok); nfoot++; }
        } else x += txt(x, y, tok, INK, 30) + 12;
        if (x > UW - 20 - bw) break;
    }
}
void ui_toast(const char *msg) {
    B = T;
    if (!msg || !*msg) return;
    int w = txtw(msg, 34) + 20;
    int x = (UW - w) / 2, y = UH - 52;
    win(x, y, w, 22, YELLOWL);
    txt(x + 10, y + 3, msg, TEXT, 34);
}

static void cursor(int x, int y, double t) {
    int dx = ((int)(t * 4) & 1);
    x += dx;
    for (int i = 0; i < 5; i++) R(x + i, y + i, 1, 11 - 2 * i, NAVY);
    for (int i = 0; i < 3; i++) R(x + 1 + i, y + 2 + i, 1, 7 - 2 * i, PINK);
}
int ui_marquee_extra = 0;   /* how many characters the selected title scrolls (set while drawing) */
void ui_list(const URow *rows, int n, int sel, int top, int moff, double t, const char *empty) {
    B = T;
    int x = 4, y = 38, w = UW - 8, h = UVIS * UROWH + 12;
    win(x, y, w, h, WHITE);
    if (!n) {
        /* sleepy TV with one or two lines under it ("\n" splits), centred in the box */
        const char *msg = empty ? empty : "Nothing here.";
        char l1b[80]; const char *nl = strchr(msg, '\n'), *l2 = 0;
        if (nl) { size_t k = (size_t)(nl - msg); if (k >= sizeof l1b) k = sizeof l1b - 1; memcpy(l1b, msg, k); l1b[k] = 0; l2 = nl + 1; msg = l1b; }
        int block = 81 + 6 + (l2 ? 26 : 13);
        int my = y + (h - block) / 2 - 2;
        mascot(UW / 2 - 33, my, 3, MOOD_SLEEP, t);
        txt_center(UW / 2, my + 85, msg, DIM, 36);
        if (l2) txt_center(UW / 2, my + 98, l2, DIM, 36);
        return;
    }
    for (int i = 0; i < UVIS && top + i < n; i++) {
        int pos = top + i, ry = y + 6 + i * UROWH;
        const URow *r = &rows[i];   /* rows[i] is item top+i */
        int hi = pos == sel;
        if (!hi && (pos & 1)) R(x + 4, ry, w - 16, UROWH, ROWALT);
        if (hi) {
            R(x + 5, ry, w - 18, UROWH, HILITE); R(x + 4, ry + 1, w - 16, UROWH - 2, HILITE);
            R(x + 5, ry + UROWH - 2, w - 18, 1, HIEDGE);
            cursor(x + 7, ry + 3, t);
        }
        int sidew = r->side && *r->side ? txtw(r->side, 14) : 0;
        int mainw = (w - 40) - (sidew ? sidew + 8 : 0);          /* virtual px for the title */
        uint32_t c = r->tone == 1 ? GOOD : r->tone == 2 ? WARN : r->tone == 3 ? DIM : r->tone == 4 ? (TH->more ? TH->more : PINKD) : TEXT;
        if (hi) c = HITEXT;
        const char *m = r->main ? r->main : "";
        if (hi) {   /* moff = ms since this row was selected: scroll long titles */
            int L = (int)strlen(m), extra = 0;
            while (extra < L && txtw(m + extra, 100) > mainw) extra++;
            ui_marquee_extra = extra;
            if (extra > 0 && moff > 1000) { int step = (moff - 1000) / 200 % (extra + 10); m += step > extra ? extra : step; }
        }
        txt_real((x + 18) * 2, (ry + 1) * 2 - 6, m, c, mainw * 2);
        if (sidew) txt(x + w - 12 - sidew, ry + 1, r->side, hi ? HITEXT : DIM, 14);
    }
    /* scrollbar */
    if (n > UVIS) {
        int tx = x + w - 8, ty = y + 6, th = h - 12;
        R(tx, ty, 3, th, TRACK);
        int bh = th * UVIS / n; if (bh < 8) bh = 8;
        int by = ty + (th - bh) * top / (n - UVIS);
        R(tx, by, 3, bh, PINK);
    }
}
void ui_message(const char *title, const char *l1, const char *l2, int mood, const char *hints, double t) {
    ui_bg(t);
    ui_header(title, "", t, mood == MOOD_SAD ? MOOD_SAD : MOOD_HAPPY);
    B = T;
    win(24, 50 + OY, UW - 48, 150, WHITE);
    int bob = mood == MOOD_STATIC ? 0 : ((int)(t * 3) & 1);
    int has1 = l1 && *l1, has2 = l2 && *l2;
    int block = 78 + (has1 ? 22 : 0) + (has2 ? 22 : 0);
    int top = 50 + OY + (150 - block) / 2;
    mascot(UW / 2 - 33, top + bob, 3, mood, t);
    if (has1) txt_center(UW / 2, top + 84, l1, TEXT, 34);
    if (has2) txt_center(UW / 2, top + 106, l2, DIM, 34);
    ui_footer(hints);
}
static void fmt_sz(char *d, int cap, long long b) {
    if (b <= 0) snprintf(d, (size_t)cap, "?");
    else if (b < 1024 * 1024) snprintf(d, (size_t)cap, "%dK", (int)(b / 1024));
    else snprintf(d, (size_t)cap, "%d.%dM", (int)(b >> 20), (int)((b & 0xFFFFF) * 10 >> 20));
}
double ui_play_at = 0;   /* watch-now: fraction of the file needed before it starts playing (0 = none) */
void ui_download(const char *name, long long got, long long total, double rate, const char *eta, int cancelling, double t, const char *hints) {
    ui_bg(t);
    header_still = 1; ui_header("Downloading", "", t, MOOD_WOW); header_still = 0;
    B = T;
    win(16, 44 + OY, UW - 32, 160, WHITE);
    mascot(36, 58 + OY + ((int)(t * 4) & 1), 2, total > 0 && got >= total ? MOOD_HAPPY : MOOD_WOW, t);
    txt(96, 60 + OY, name, TEXT, 25);
    char a[24], b[24], line[64];
    fmt_sz(a, sizeof a, got); fmt_sz(b, sizeof b, total);
    if (total > 0) snprintf(line, sizeof line, "%s / %s", a, b); else snprintf(line, sizeof line, "%s", a);
    txt(96, 82 + OY, line, INK, 25);
    if (rate > 0) { snprintf(line, sizeof line, "%dK/s  %s left", (int)(rate / 1024), eta ? eta : ""); txt(96, 102 + OY, line, DIM, 25); }
    else txt(96, 102 + OY, "Tuning in...", DIM, 25);
    /* progress bar with moving candy stripes */
    int bx = 30, by = 138 + OY, bw = UW - 60, bh = 16;
    R(bx + 1, by, bw - 2, bh, NAVY); R(bx, by + 1, bw, bh - 2, NAVY);
    R(bx + 1, by + 1, bw - 2, bh - 2, TRACK);
    int fw = total > 0 ? (int)((bw - 2) * (double)got / (double)total) : 0;
    if (total <= 0) { fw = 40; }
    int fx = bx + 1;
    if (total <= 0) fx = bx + 1 + ((int)(t * 80) % (bw - 42));
    int so = (int)(t * 20) % 8;
    for (int i = 0; i < fw; i++) {
        int stripe = ((i + so) / 4) & 1;
        R(fx + i, by + 1, 1, bh - 2, stripe ? PINK : BLUSH);
    }
    if (fw > 2) R(fx, by + 2, fw, 1, BLUSH);
    if (total > 0 && ui_play_at > 0 && ui_play_at < 1) {   /* a little marker where playback kicks in */
        int mx = bx + 1 + (int)((bw - 2) * ui_play_at);
        R(mx, by - 2, 1, bh + 4, YELLOW); R(mx - 2, by - 5, 5, 1, YELLOW); R(mx - 1, by - 4, 3, 1, YELLOW);
        int pa = (int)(ui_play_at * 100 + 0.5); if (pa < 1) pa = 1;
        snprintf(line, sizeof line, "%d%%  -  plays at %d%%", (int)(100.0 * got / total), pa);
        txt_center(UW / 2, by + 22, line, TEXT, 24);
    } else if (total > 0) { snprintf(line, sizeof line, "%d%%", (int)(100.0 * got / total)); txt_center(UW / 2, by + 22, line, TEXT, 6); }
    ui_footer(cancelling ? "Stopping..." : hints ? hints : "B:Cancel");
}
/* player overlay on top of a video frame (buf is 640x480) */
double ui_osd_avail = -1;   /* streaming: fraction of the video downloaded so far (-1 = all of it) */
void ui_osd(uint32_t *buf, const char *title, double pos, double dur, int paused, const char *clock, double t) {
    B = buf;
    win(4, 4, txtw(title, 26) + 44, 26, CREAM);
    mascot(11, 3, 1, paused ? MOOD_SLEEP : MOOD_HAPPY, 1.0);   /* still, like the header one */
    txt(40, 9, title, TEXT, 26);
    if (clock) { pill(UW - 54, 8, 48, 16, NAVY); txt(UW - 50, 8, clock, MINT, 5); }
    /* bottom: a soft dark fade with a thin bar, like a normal video player */
    for (int y = (UH - 30) * 2; y < RH; y++) {
        int k = y - (UH - 30) * 2;              /* 0..59 */
        int keep = 256 - (k < 24 ? k * 6 : 150); /* fade in, then ~40% brightness */
        uint32_t *row = buf + y * RW;
        for (int x = 0; x < RW; x++) {
            uint32_t v = row[x];
            row[x] = ((((v >> 16) & 255) * (uint32_t)keep >> 8) << 16) | ((((v >> 8) & 255) * (uint32_t)keep >> 8) << 8) | ((v & 255) * (uint32_t)keep >> 8);
        }
    }
    char a[24], b[24];
    int s = (int)pos; if (s < 0) s = 0;
    if (s >= 3600) snprintf(a, sizeof a, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60); else snprintf(a, sizeof a, "%d:%02d", s / 60, s % 60);
    b[0] = 0;
    if (dur > 0) { int d = (int)dur; if (d >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", d / 3600, d / 60 % 60, d % 60); else snprintf(b, sizeof b, "%d:%02d", d / 60, d % 60); }
    uint32_t wt = C(240, 240, 248), gr = C(170, 170, 185);
    int ty = UH - 22;
    txt(10, ty - 2, a, wt, 10);
    int bl = txtw(b, 10);
    if (bl) txt(UW - 10 - bl, ty - 2, b, gr, 10);
    if (paused) txt((UW - txtw("PAUSED", 6)) / 2, ty - 2, "PAUSED", wt, 6);
    int bx = 10, by = UH - 5, bw = UW - 20;
    if (ui_osd_avail >= 0 && ui_osd_avail < 1) {   /* downloaded part = light track, the rest = dark */
        int aw = (int)(bw * 2 * ui_osd_avail);
        rr(buf, bx * 2, by * 2, bw * 2, 2, C(55, 55, 70));
        if (aw > 0) rr(buf, bx * 2, by * 2, aw, 2, C(130, 130, 150));
    } else rr(buf, bx * 2, by * 2, bw * 2, 2, C(110, 110, 125));
    int fw = dur > 0 ? (int)(bw * 2 * pos / dur) : 0;
    if (fw > bw * 2) fw = bw * 2;
    if (fw > 0) rr(buf, bx * 2, by * 2, fw, 2, PINK);
    rr(buf, bx * 2 + fw - 3, by * 2 - 2, 6, 6, wt);   /* small dot */
}

/* ---------------- on-screen keyboard ---------------- */
static const char *KROWS[4] = { "abcdefghij", "klmnopqrst", "uvwxyz0123", "456789-'.&" };
int ui_kb_char(int kx, int ky) { return ky < 4 ? KROWS[ky][kx] : 0; }
/* bottom row: 0 = SHIFT (cols 0-1), 1 = SPACE (2-5), 2 = DEL (6-7), 3 = action (8-9) */
int ui_kb_special(int kx) { return kx < 2 ? 0 : kx < 6 ? 1 : kx < 8 ? 2 : 3; }
void ui_keyboard(const char *title, const char *text, int kx, int ky, double t, const char *action, int caps) {
    ui_bg(t);
    ui_header(title, "", t, MOOD_WOW);
    B = T;
    /* text field */
    win(4, 38, UW - 8, 28, WHITE);
    R(12, 44, UW - 24, 17, ROWALT);
    int n = (int)strlen(text), maxch = (UW - 40) / 8;
    const char *shown = n > maxch ? text + n - maxch : text;
    int w = txt(16, 45, shown, TEXT, maxch);
    if (((int)(t * 2.5)) & 1) R(16 + w + 1, 47, 2, 12, PINK);
    if (!n) txt(16 + 6, 45, action && !strcmp(action, "SEARCH") ? "type a show, a topic..." : "type a name...", DIM, 30);
    /* keys */
    win(4, 72 + OY / 2, UW - 8, 136, CREAM);
    int kw = 28, kh = 22, x0 = (UW - (10 * (kw + 2) - 2)) / 2, y0 = 80 + OY / 2;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 10; c++) {
            int x = x0 + c * (kw + 2), y = y0 + r * (kh + 3);
            int hi = (r == ky && c == kx);
            uint32_t face = hi ? HILITE : WHITE;
            R(x + 1, y + kh, kw - 1, 2, hi ? HIEDGE : SHADOW);
            R(x + 1, y, kw - 2, kh, NAVY); R(x, y + 1, kw, kh - 2, NAVY);
            R(x + 1, y + 1, kw - 2, kh - 2, face);
            R(x + 2, y + 1, kw - 4, 1, hi ? HIEDGE : RIM);
            char ch = KROWS[r][c];
            glyph(x + (kw - 8) / 2, y + 3, (unsigned char)(caps && ch >= 'a' && ch <= 'z' ? ch - 32 : ch), hi ? HITEXT : INK);
        }
    /* bottom row */
    const char *lab[4] = { caps ? "ABC" : "abc", "SPACE", "DEL", action && *action ? action : "OK" };
    static const int cs[4] = { 0, 2, 6, 8 }, ce[4] = { 1, 5, 7, 9 };
    for (int i = 0; i < 4; i++) {
        int x = x0 + cs[i] * (kw + 2), y = y0 + 4 * (kh + 3);
        int ww = (ce[i] - cs[i] + 1) * (kw + 2) - 2;
        int hi = ky == 4 && ui_kb_special(kx) == i;
        uint32_t face = i == 3 ? (hi ? PINKD : PINK) : (i == 0 && caps) ? (hi ? HIEDGE : YELLOWL) : (hi ? HILITE : WHITE);
        R(x + 1, y + kh, ww - 1, 2, SHADOW);
        R(x + 1, y, ww - 2, kh, NAVY); R(x, y + 1, ww, kh - 2, NAVY);
        R(x + 1, y + 1, ww - 2, kh - 2, face);
        int L = (int)strlen(lab[i]);
        (void)L;
        txt(x + (ww - txtw(lab[i], 12)) / 2, y + 3, lab[i], i == 3 ? PILLTX : (hi ? HITEXT : INK), 12);
    }
    ui_footer("A:Type  B:Del  SELECT:Shift");
}

/* small centred bubble with a static-y mascot, e.g. "Buffering..." over the video */
void ui_badge(const char *msg, double t) {
    B = T;
    int w = txtw(msg, 30) + 56, h = 34;
    int x = (UW - w) / 2, y = (UH - h) / 2;
    win(x, y, w, h, CREAM);
    mascot(x + 8, y + 4, 1, MOOD_STATIC, t);
    txt(x + 42, y + 9, msg, TEXT, 30);
}

/* header with tabs instead of a title (home screen) */
void ui_header_tabs(const char *const *tabs, int n, int sel, const char *right, double t) {
    (void)right;
    ui_header("", "", t, MOOD_HAPPY);
    B = T;
    int x = 37;
    int avail = UW - 10 - x;
    int total = 0;
#define TABW(i) (tabs[i][0] == 1 ? 14 : txtw(tabs[i], 8) + 5)
    for (int i = 0; i < n; i++) total += TABW(i);
    int gap = n > 1 ? (avail - total) / (n - 1) : 0;
    if (gap > 8) gap = 8;
    if (gap < 0) gap = 0;
    ntab_hit = n > 16 ? 16 : n;
    for (int i = 0; i < n; i++) {
        int w = TABW(i);
        if (i < 16) { tab_x0[i] = x; tab_x1[i] = x + w; }
        if (i == sel) pill(x, 13, w, 18, NAVY), pill(x + 1, 14, w - 2, 16, TH->tab ? TH->tab : PINK);
        uint32_t c = i == sel ? PILLTX : DIM;
        if (tabs[i][0] == 1) {   /* little pixel gear */
            static const char *G[10] = { "...##...", ".#.##.#.", "########", ".##..##.", "##....##", "##....##", ".##..##.", "########", ".#.##.#.", "...##..." };
            int gx = x + (w - 8) / 2, gy = 17;
            for (int j = 0; j < 10; j++) for (int k = 0; k < 8; k++) if (G[j][k] == '#') R(gx + k, gy + j, 1, 1, c);
        } else txt(x + 3, 14, tabs[i], c, 8);
        x += w + gap;
    }
}
/* popup menu over whatever is on screen */
void ui_menu(const char *title, const char *const *items, int n, int sel, double t) {
    B = T;
    int maxl = txtw(title, 30);
    for (int i = 0; i < n; i++) { int l = txtw(items[i], 30) + 10; if (l > maxl) maxl = l; }
    int w = maxl + 40, h = n * UROWH + 34;
    if (w < 160) w = 160;
    int x = (UW - w) / 2, y = (UH - h) / 2;
    menu_x = x; menu_y = y; menu_w = w; menu_h = h; menu_n = n;
    win(x, y, w, h, WHITE);
    R(x + 2, y + 2, w - 4, 20, CREAM);
    R(x + 2, y + 22, w - 4, 1, NAVY);
    txt(x + 10, y + 4, title, TEXT, 30);
    for (int i = 0; i < n; i++) {
        int ry = y + 27 + i * UROWH;
        if (i == sel) {
            R(x + 5, ry, w - 10, UROWH, HILITE); R(x + 4, ry + 1, w - 8, UROWH - 2, HILITE);
            cursor(x + 7, ry + 3, t);
        }
        txt(x + 20, ry + 1, items[i], i == sel ? HITEXT : TEXT, 30);
    }
}

/* start screen: the mascot says something in a speech bubble */
void ui_hello(const char *say, int shown, const char *hints, double t) {
    ui_bg(t);
    ui_header("ArchiveTV", "okcyborg.com", t, MOOD_HAPPY);
    B = T;
    win(24, 44 + OY, UW - 48, 162, WHITE);
    static const int HOP[12] = { 0, 2, 3, 4, 4, 3, 2, 0, 0, 0, 0, 0 };   /* a little hop every ~1.5 s */
    int hop = HOP[(int)(t * 8) % 12];
    int talking = shown < (int)strlen(say);
    mascot(UW / 2 - 33, 52 + OY - hop, 3, talking ? MOOD_TALK : MOOD_HAPPY, t);
    int tw = txtw(say, 32), bw = tw + 20, bx = (UW - bw) / 2, by = 140 + OY, cx = UW / 2;
    R(bx + 2, by, bw - 4, 1, NAVY); R(bx + 2, by + 23, bw - 4, 1, NAVY);
    R(bx, by + 2, 1, 20, NAVY); R(bx + bw - 1, by + 2, 1, 20, NAVY);
    P(bx + 1, by + 1, NAVY); P(bx + bw - 2, by + 1, NAVY); P(bx + 1, by + 22, NAVY); P(bx + bw - 2, by + 22, NAVY);
    R(bx + 1, by + 2, bw - 2, 20, CREAM); R(bx + 2, by + 1, bw - 4, 22, CREAM);
    /* tail pointing up at the TV */
    for (int i = 0; i < 6; i++) {
        R(cx - i - 1, by - 6 + i, 2 * i + 3, 1, NAVY);
        if (i) R(cx - i, by - 6 + i, 2 * i + 1, 1, CREAM);
    }
    R(cx - 5, by, 11, 1, CREAM);
    { char part[96]; int n = shown < 0 ? 0 : shown; if (n > 95) n = 95; snprintf(part, sizeof part, "%.*s", n, say); txt(bx + 10, by + 4, part, TEXT, 32); }
    txt_center(UW / 2, 172 + OY, "Videos from Internet Archive", DIM, 36);
    txt_center(UW / 2, 184 + OY, "Donate: archive.org/donate", PINK, 36);
    ui_footer_badge("");
    ui_footer(hints);
}

const char *ui_app_version = "";
void ui_about(double t) {
    ui_bg(t);
    { char v[24] = ""; if (ui_app_version && ui_app_version[0]) snprintf(v, sizeof v, "v%s", ui_app_version); ui_header("About", v, t, MOOD_HAPPY); }
    B = T;
    int y0 = 40 + OY;
    win(12, y0, UW - 24, 168, WHITE);
    int x = 26, vx = 100;
    txt(x, y0 + 8, "ArchiveTV", TEXT, 24);
    txt(x, y0 + 24, "Old TV for your Miyoo.", DIM, 30);
    int y = y0 + 44;
    txt(x, y, "Made by", DIM, 10);   txt(vx, y, "@0kCyb0rg", PINK, 26); y += 15;
    txt(x, y, "Web", DIM, 10);       txt(vx, y, "okcyborg.com", PINK, 26); y += 15;
    txt(x, y, "Videos", DIM, 10);    txt(vx, y, "Internet Archive", TEXT, 26); y += 15;
    txt(x, y, "Donate", DIM, 10);    txt(vx, y, "archive.org/donate", PINK, 26); y += 15;
    txt(x, y, "Music", DIM, 10);     txt(vx, y, "Select a Level", TEXT, 26); y += 15;
    txt(x, y, "Font", DIM, 10);      txt(vx, y, "m5x7 by Daniel Linssen", TEXT, 26); y += 15;
    txt(x, y, "Effect", DIM, 10);    txt(vx, y, "Onion OS scanlines", TEXT, 26); y += 15;
    ui_footer_badge("");
    ui_footer("B:Back");
}

/* ---------- touch screens (Android): what's under a finger, in virtual pixels (real / 2) ---------- */
int ui_hit_tab(int vx, int vy) {
    if (vy < 9 || vy > 35) return -1;
    for (int i = 0; i < ntab_hit; i++) if (vx >= tab_x0[i] - 4 && vx < tab_x1[i] + 4) return i;
    return -1;
}
int ui_hit_row(int vx, int vy) {   /* which visible list row (0 = the top one shown), -1 = none */
    if (vx < 4 || vx > UW - 4 || vy < 44) return -1;
    int i = (vy - 44) / UROWH;
    return i < UVIS ? i : -1;
}
const char *ui_hit_footer(int vx, int vy) {   /* the button name of the footer hint under the finger */
    if (vy < UH - 28) return 0;
    for (int i = 0; i < nfoot; i++) if (vx >= foot_x0[i] && vx < foot_x1[i]) return foot_k[i];
    return 0;
}
int ui_hit_menu(int vx, int vy) {   /* item index, -1 = inside the box but no item, -2 = outside */
    if (vx < menu_x || vx >= menu_x + menu_w || vy < menu_y || vy >= menu_y + menu_h) return -2;
    if (vy < menu_y + 27) return -1;
    int i = (vy - menu_y - 27) / UROWH;
    return i < menu_n ? i : -1;
}
int ui_hit_kb(int vx, int vy, int *kx, int *ky) {   /* 1 if a key of ui_keyboard is under the finger */
    int kw = 28, kh = 22, x0 = (UW - (10 * (kw + 2) - 2)) / 2, y0 = 80 + OY / 2;
    if (vx < x0 || vy < y0) return 0;
    int c = (vx - x0) / (kw + 2), r = (vy - y0) / (kh + 3);
    if (c > 9 || r > 4) return 0;
    *kx = c; *ky = r;
    return 1;
}

/* The TV panel when nothing is on (Android): an untuned channel - snow - with the little guy hopping in
   front of it, saying "Tuning in...". The whole picture is the TV's screen. */
void ui_tv_idle(const char *say, double t) {
    B = T;
    unsigned seed = (unsigned)((int)(t * 15) * 2654435761u);
    rng = seed ? seed : 1;
    for (int y = 0; y < RH; y += 2) {
        uint32_t *row = T + y * RW;
        for (int x = 0; x < RW; x += 2) {
            unsigned v = rnd() & 255; v = 8 + (v * v * v >> 18);     /* dark snow, a few brighter flecks */
            uint32_t c = (v << 16) | (v << 8) | (v < 250 ? v + 4 : v);
            row[x] = row[x + 1] = c;
            if (y + 1 < RH) row[RW + x] = row[RW + x + 1] = c;
        }
    }
    for (int k = 0; k < 3; k++) {   /* a few rolling bright bars, like a lost signal */
        int by = (int)((t * 23 + k * 97) * 1.0) % UH;
        for (int y = by; y < by + 2 && y < UH; y++) for (int x = 0; x < UW; x++) {
            uint32_t *p = T + (y * 2) * RW + x * 2; uint32_t c = *p, r = ((c >> 16) & 255) + 22; if (r > 255) r = 255;
            p[0] = p[1] = p[RW] = p[RW + 1] = (r << 16) | (r << 8) | r;
        }
    }
    static const int HOP[12] = { 0, 2, 3, 4, 4, 3, 2, 0, 0, 0, 0, 0 };
    int hop = HOP[(int)(t * 8) % 12];
    int my = UH / 2 - 52;
    mascot(UW / 2 - 33, my - hop, 3, MOOD_HAPPY, t);
    const char *msg = say && *say ? say : "Tuning in...";
    int tw = txtw(msg, 32), bw = tw + 20, bx = (UW - bw) / 2, by = my + 88, cx = UW / 2;
    R(bx + 2, by, bw - 4, 1, NAVY); R(bx + 2, by + 23, bw - 4, 1, NAVY);
    R(bx, by + 2, 1, 20, NAVY); R(bx + bw - 1, by + 2, 1, 20, NAVY);
    P(bx + 1, by + 1, NAVY); P(bx + bw - 2, by + 1, NAVY); P(bx + 1, by + 22, NAVY); P(bx + bw - 2, by + 22, NAVY);
    R(bx + 1, by + 2, bw - 2, 20, CREAM); R(bx + 2, by + 1, bw - 4, 22, CREAM);
    for (int i = 0; i < 6; i++) { R(cx - i - 1, by - 6 + i, 2 * i + 3, 1, NAVY); if (i) R(cx - i, by - 6 + i, 2 * i + 1, 1, CREAM); }
    R(cx - 5, by, 11, 1, CREAM);
    txt(bx + 10, by + 4, msg, TEXT, 32);
}
