package com.okcyborg.archivetv;

/** The Miyoo app's drawing code (src/ui.c), shared through JNI. Draw a screen, then copy it out with frame(). */
final class Native {
    static { System.loadLibrary("atvui"); }
    static final int HAPPY = 0, SAD = 1, STATIC = 2, WOW = 3, SLEEP = 4, TALK = 5;
    static native void init(int w, int h);
    static native void setTheme(int t);
    static native int themeCount();
    static native String themeName(int i);
    static native int visibleRows();
    static native void bg(double t);
    static native void header(String title, String right, double t, int mood);
    static native void headerTabs(String[] tabs, int sel, String right, double t);
    static native void list(String[] mains, String[] sides, int[] tones, int sel, int top, int marquee, double t, String empty);
    static native void footer(String hints);
    static native void footerBadge(String badge);
    static native void toast(String msg);
    static native void message(String title, String l1, String l2, int mood, String hints, double t);
    static native void hello(String say, int shown, String hints, double t);
    static native void menu(String title, String[] items, int sel, double t);
    static native void keyboard(String title, String text, int kx, int ky, double t, String action, int caps);
    static native int kbChar(int kx, int ky);
    static native int kbSpecial(int kx);
    static native void about(double t);
    static native void frame(int[] out);
}
