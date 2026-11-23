/* Bridge from Java to the Miyoo app's drawing code (ui.c). Java asks for a screen, then copies the pixels. */
#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

static uint32_t *buf; static int W, H;

/* Java strings -> Latin-1 (the pixel font has the 224 Latin-1 glyphs) */
static char *l1(JNIEnv *e, jstring s) {
    if (!s) return 0;
    const jchar *c = (*e)->GetStringChars(e, s, 0);
    jsize n = (*e)->GetStringLength(e, s);
    char *o = (char *)malloc((size_t)n + 1);
    for (jsize i = 0; i < n; i++) o[i] = c[i] < 256 ? (char)c[i] : '?';
    o[n] = 0;
    (*e)->ReleaseStringChars(e, s, c);
    return o;
}
#define FN(name) JNICALL Java_com_okcyborg_archivetv_Native_##name

/* a few pictures of different sizes (0 = menus, 1 = the TV panel, 2 = the time bar); select() makes one current */
static uint32_t *bufs[4]; static int bw[4], bh[4];
JNIEXPORT void FN(select)(JNIEnv *e, jclass c, jint which, jint w, jint h) {
    if (which < 0 || which > 3) return;
    if (!bufs[which] || bw[which] != w || bh[which] != h) {
        free(bufs[which]); bufs[which] = (uint32_t *)calloc((size_t)w * h, 4); bw[which] = w; bh[which] = h;
    }
    buf = bufs[which]; W = w; H = h; ui_init(buf, w, h);
}
JNIEXPORT void FN(init)(JNIEnv *e, jclass c, jint w, jint h) { Java_com_okcyborg_archivetv_Native_select(e, c, 0, w, h); }
JNIEXPORT void FN(setTheme)(JNIEnv *e, jclass c, jint t) { ui_set_theme(t); }
JNIEXPORT jint FN(themeCount)(JNIEnv *e, jclass c) { return ui_theme_count(); }
JNIEXPORT jstring FN(themeName)(JNIEnv *e, jclass c, jint i) { return (*e)->NewStringUTF(e, ui_theme_name(i)); }
JNIEXPORT jint FN(visibleRows)(JNIEnv *e, jclass c) { return UVIS; }
JNIEXPORT void FN(bg)(JNIEnv *e, jclass c, jdouble t) { ui_bg(t); }
JNIEXPORT void FN(header)(JNIEnv *e, jclass c, jstring title, jstring right, jdouble t, jint mood) {
    char *a = l1(e, title), *b = l1(e, right); ui_header(a ? a : "", b ? b : "", t, mood); free(a); free(b);
}
JNIEXPORT void FN(headerTabs)(JNIEnv *e, jclass c, jobjectArray tabs, jint sel, jstring right, jdouble t) {
    int n = (*e)->GetArrayLength(e, tabs); char *v[16]; if (n > 16) n = 16;
    for (int i = 0; i < n; i++) v[i] = l1(e, (jstring)(*e)->GetObjectArrayElement(e, tabs, i));
    char *r = l1(e, right);
    ui_header_tabs((const char *const *)v, n, sel, r, t);
    for (int i = 0; i < n; i++) free(v[i]);
    free(r);
}
JNIEXPORT void FN(list)(JNIEnv *e, jclass c, jobjectArray mains, jobjectArray sides, jintArray tones, jint sel, jint top, jint marquee, jdouble t, jstring empty) {
    int n = mains ? (*e)->GetArrayLength(e, mains) : 0;
    URow *r = (URow *)calloc((size_t)(n ? n : 1), sizeof(URow));
    jint *tn = n ? (*e)->GetIntArrayElements(e, tones, 0) : 0;
    for (int i = 0; i < n; i++) {
        r[i].main = l1(e, (jstring)(*e)->GetObjectArrayElement(e, mains, i));
        r[i].side = l1(e, (jstring)(*e)->GetObjectArrayElement(e, sides, i));
        if (!r[i].side) r[i].side = (const char *)calloc(1, 1);
        r[i].tone = tn[i];
    }
    char *em = l1(e, empty);
    ui_list(r, n, sel, top, marquee, t, em);
    for (int i = 0; i < n; i++) { free((void *)r[i].main); free((void *)r[i].side); }
    if (tn) (*e)->ReleaseIntArrayElements(e, tones, tn, JNI_ABORT);
    free(r); free(em);
}
JNIEXPORT void FN(footer)(JNIEnv *e, jclass c, jstring h) { char *a = l1(e, h); ui_footer(a ? a : ""); free(a); }
JNIEXPORT void FN(footerBadge)(JNIEnv *e, jclass c, jstring h) { char *a = l1(e, h); ui_footer_badge(a); free(a); }
JNIEXPORT void FN(toast)(JNIEnv *e, jclass c, jstring h) { char *a = l1(e, h); if (a) ui_toast(a); free(a); }
JNIEXPORT void FN(message)(JNIEnv *e, jclass c, jstring title, jstring a1, jstring a2, jint mood, jstring hints, jdouble t) {
    char *a = l1(e, title), *b = l1(e, a1), *d = l1(e, a2), *h = l1(e, hints);
    ui_message(a ? a : "", b ? b : "", d, mood, h, t); free(a); free(b); free(d); free(h);
}
JNIEXPORT void FN(hello)(JNIEnv *e, jclass c, jstring say, jint shown, jstring hints, jdouble t) {
    char *a = l1(e, say), *h = l1(e, hints); ui_hello(a ? a : "", shown, h, t); free(a); free(h);
}
JNIEXPORT void FN(menu)(JNIEnv *e, jclass c, jstring title, jobjectArray items, jint sel, jdouble t) {
    int n = (*e)->GetArrayLength(e, items); char *v[16]; if (n > 16) n = 16;
    for (int i = 0; i < n; i++) v[i] = l1(e, (jstring)(*e)->GetObjectArrayElement(e, items, i));
    char *ti = l1(e, title);
    ui_menu(ti ? ti : "", (const char *const *)v, n, sel, t);
    for (int i = 0; i < n; i++) free(v[i]);
    free(ti);
}
JNIEXPORT void FN(keyboard)(JNIEnv *e, jclass c, jstring title, jstring text, jint kx, jint ky, jdouble t, jstring action, jint caps) {
    char *a = l1(e, title), *b = l1(e, text), *d = l1(e, action);
    ui_keyboard(a ? a : "", b ? b : "", kx, ky, t, d ? d : "Search", caps); free(a); free(b); free(d);
}
JNIEXPORT jint FN(kbChar)(JNIEnv *e, jclass c, jint kx, jint ky) { return ui_kb_char(kx, ky); }
JNIEXPORT jint FN(kbSpecial)(JNIEnv *e, jclass c, jint kx) { return ui_kb_special(kx); }
JNIEXPORT void FN(about)(JNIEnv *e, jclass c, jdouble t) { ui_about(t); }
/* copies the finished picture out, opaque (ui.c writes 0x00RRGGBB) */
JNIEXPORT void FN(frame)(JNIEnv *e, jclass c, jintArray out) {
    jint *o = (*e)->GetIntArrayElements(e, out, 0);
    int n = W * H;
    for (int i = 0; i < n; i++) o[i] = (jint)(buf[i] | 0xFF000000u);
    (*e)->ReleaseIntArrayElements(e, out, o, 0);
}

/* the player's time bar over the video: drawn on an empty picture, empty pixels see-through,
   and the bottom gets the soft dark fade the Miyoo version paints over the video */
JNIEXPORT void FN(osd)(JNIEnv *e, jclass c, jintArray out, jstring title, jdouble pos, jdouble dur, jint paused, jstring clock, jdouble avail, jdouble t) {
    uint32_t *tmp = (uint32_t *)calloc((size_t)W * H, 4);
    if (!tmp) return;
    char *a = l1(e, title), *ck = l1(e, clock);
    ui_osd_avail = avail;
    ui_osd(tmp, a ? a : "", pos, dur, paused, ck, t);
    ui_osd_avail = -1;
    free(a); free(ck);
    jint *o = (*e)->GetIntArrayElements(e, out, 0);
    int fade0 = (H / 2 - 30) * 2;
    for (int y = 0; y < H; y++) {
        uint32_t al = 0;
        if (y >= fade0) { int k = y - fade0; al = (uint32_t)((k < 24 ? k * 6 : 150) * 255 / 256); }
        for (int x = 0; x < W; x++) {
            uint32_t v = tmp[y * W + x];
            o[y * W + x] = (jint)(v ? (v | 0xFF000000u) : (al << 24));
        }
    }
    (*e)->ReleaseIntArrayElements(e, out, o, 0);
    free(tmp);
}
JNIEXPORT jint FN(width)(JNIEnv *e, jclass c) { return W; }
JNIEXPORT jint FN(height)(JNIEnv *e, jclass c) { return H; }

/* touch hit-tests (virtual pixels) */
JNIEXPORT jint FN(hitTab)(JNIEnv *e, jclass c, jint x, jint y) { return ui_hit_tab(x, y); }
JNIEXPORT jint FN(hitRow)(JNIEnv *e, jclass c, jint x, jint y) { return ui_hit_row(x, y); }
JNIEXPORT jstring FN(hitFooter)(JNIEnv *e, jclass c, jint x, jint y) { const char *k = ui_hit_footer(x, y); return k ? (*e)->NewStringUTF(e, k) : 0; }
JNIEXPORT jint FN(hitMenu)(JNIEnv *e, jclass c, jint x, jint y) { return ui_hit_menu(x, y); }
JNIEXPORT jint FN(hitKb)(JNIEnv *e, jclass c, jint x, jint y) { int kx, ky; return ui_hit_kb(x, y, &kx, &ky) ? kx + ky * 16 : -1; }
