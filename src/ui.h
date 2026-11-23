/* ArchiveTV GBA-style UI: everything is drawn on a 320x240 "virtual" canvas, each virtual pixel = 2x2 real pixels. */
#pragma once
#include <stdint.h>

extern int UW, UH, UVIS;   /* virtual screen size (real / 2) and list rows; set by ui_init */
#define UROWH 18

typedef struct { const char *main, *side; int tone; } URow;   /* tone: 0 normal, 1 good, 2 warn, 3 dim, 4 "more" */

void ui_init(uint32_t *target, int real_w, int real_h);
void ui_bg(double t);
void ui_header(const char *title, const char *right, double t, int mood);
void ui_footer(const char *hints);
void ui_toast(const char *msg);
void ui_list(const URow *rows, int n, int sel, int top, int marquee_off, double t, const char *empty);
void ui_message(const char *title, const char *l1, const char *l2, int mood, const char *hints, double t);
extern double ui_play_at;
extern int ui_marquee_extra;
extern double ui_osd_avail;
void ui_download(const char *name, long long got, long long total, double rate, const char *eta, int cancelling, double t, const char *hints);
void ui_osd(uint32_t *buf, const char *title, double pos, double dur, int paused, const char *clock, double t);
void ui_rect(int x, int y, int w, int h, uint32_t c);

enum { MOOD_HAPPY, MOOD_SAD, MOOD_STATIC, MOOD_WOW, MOOD_SLEEP, MOOD_TALK };
void ui_keyboard(const char *title, const char *text, int kx, int ky, double t, const char *action, int caps);
int ui_kb_char(int kx, int ky);
int ui_kb_special(int kx);
void ui_badge(const char *msg, double t);
void ui_header_tabs(const char *const *tabs, int n, int sel, const char *right, double t);
void ui_menu(const char *title, const char *const *items, int n, int sel, double t);
int ui_theme_count(void);
const char *ui_theme_name(int i);
void ui_set_theme(int i);
void ui_footer_badge(const char *b);
void ui_hello(const char *say, int shown, const char *hints, double t);   /* shown = letters typed so far */
void ui_about(double t);
int ui_hit_tab(int vx, int vy);
int ui_hit_row(int vx, int vy);
const char *ui_hit_footer(int vx, int vy);
int ui_hit_menu(int vx, int vy);
int ui_hit_kb(int vx, int vy, int *kx, int *ky);
