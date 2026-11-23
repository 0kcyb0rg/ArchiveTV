package com.okcyborg.archivetv;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.media.SoundPool;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/** ArchiveTV for Android handhelds: the Miyoo app's screens, drawn by the same C code. */
public class MainActivity extends Activity implements android.opengl.GLSurfaceView.Renderer {
    static MainActivity self;
    android.opengl.GLSurfaceView view; android.widget.FrameLayout root; Tv tv; final Crt crt = new Crt(); int fx = 1; boolean curve = false;
    final Rect drawMenu = new Rect(), drawTv = new Rect(); boolean drawIdle;
    /* screen split: where the menus and the TV go (screen pixels); mode 0 = menus only, 1 = TV left, 2 = TV on top */
    final Rect menuR = new Rect(), tvR = new Rect(); int mode = 0, sw = 1, sh = 1; boolean full = false;
    int[] tvPx; Bitmap tvBmp; static final int TVW = 640, TVH = 480;
    Thread loop; volatile boolean running;
    int W, H; int[] px; Bitmap bmp; final Paint paint = new Paint();
    final Object lock = new Object();
    final ArrayDeque<Screen> stack = new ArrayDeque<>();
    final ArrayDeque<Integer> keyq = new ArrayDeque<>();
    final ArrayDeque<int[]> touchq = new ArrayDeque<>();   /* {kind, vx, vy}: 0 down, 1 drag, 2 tap, 3 swipe left, 4 swipe right */
    final Rect dst = new Rect(); float downX, downY; boolean dragging;
    final double t0 = System.nanoTime() / 1e9;
    SharedPreferences prefs;
    int theme = 0; boolean music = true, sounds = true, group = true;
    SoundPool pool; int sMove, sSelect, sBack, sTab, sError, sDone; final MusicLoop bgm = new MusicLoop();

    double now() { return System.nanoTime() / 1e9 - t0; }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        self = this;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        root = new android.widget.FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        view = new android.opengl.GLSurfaceView(this);
        view.setEGLContextClientVersion(2);
        view.setRenderer(this);
        view.setRenderMode(android.opengl.GLSurfaceView.RENDERMODE_WHEN_DIRTY);
        view.setFocusable(true);
        root.addView(view, new android.widget.FrameLayout.LayoutParams(-1, -1));
        view.setPreserveEGLContextOnPause(true);
        tv = new Tv(this);
        setContentView(root);
        prefs = getSharedPreferences("atv", MODE_PRIVATE);
        theme = prefs.getInt("theme", 0); music = prefs.getBoolean("music", true); sounds = prefs.getBoolean("sounds", true);
        group = prefs.getBoolean("group", true); Keys.swapAB = prefs.getBoolean("swapab", false); fx = prefs.getInt("fx2", 1); curve = prefs.getBoolean("curve", false);
        pool = new SoundPool.Builder().setMaxStreams(4).setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).build()).build();
        try {
            sMove = pool.load(getAssets().openFd("sfx/move.wav"), 1); sSelect = pool.load(getAssets().openFd("sfx/select.wav"), 1);
            sBack = pool.load(getAssets().openFd("sfx/back.wav"), 1); sTab = pool.load(getAssets().openFd("sfx/tab.wav"), 1);
            sError = pool.load(getAssets().openFd("sfx/error.wav"), 1); sDone = pool.load(getAssets().openFd("sfx/done.wav"), 1);
        } catch (Exception ignored) { }
        Store.init(this); Downloader.load(); Downloader.kick();
        stack.push(new Hello());
    }

    void save() { prefs.edit().putInt("theme", theme).putBoolean("music", music).putBoolean("sounds", sounds).putBoolean("group", group).putBoolean("swapab", Keys.swapAB).putInt("fx2", fx).putBoolean("curve", curve).apply(); }
    void sfx(int id) { if (sounds && id != 0) pool.play(id, 0.55f, 0.55f, 1, 0, 1f); }

    void musicOn(boolean on) { if (on && music && (tv == null || !tv.active())) bgm.start(this); else bgm.pause(); }

    @Override protected void onResume() { super.onResume(); view.onResume(); hideBars(); musicOn(true); }
    @Override protected void onPause() { super.onPause(); view.onPause(); stopLoop(); musicOn(false); tv.saveSpot(); tv.pause(); }
    @Override protected void onDestroy() { super.onDestroy(); bgm.stop(); }
    void hideBars() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    @Override public boolean onKeyDown(int code, KeyEvent e) {
        if (e.getDevice() != null && e.getDevice().getKeyboardType() == android.view.InputDevice.KEYBOARD_TYPE_ALPHABETIC) {
            int ch = code == KeyEvent.KEYCODE_DEL ? '\b' : code == KeyEvent.KEYCODE_ENTER ? '\n' : e.getUnicodeChar();
            synchronized (lock) { Screen s = stack.peek(); if (s instanceof Keyboard && ch > 0 && ((Keyboard) s).typed(ch)) return true; }
        }
        int k = Keys.map(code);
        if (k == Keys.NONE) return super.onKeyDown(code, e);
        if (tv.focus) { tv.key(k); return true; }
        synchronized (lock) { keyq.add(k); }
        return true;
    }
    /* touch: screen position -> the UI's virtual pixels (half the drawn picture) */
    int[] virt(float x, float y) {
        synchronized (dst) {
            if (dst.width() <= 0 || !dst.contains((int) x, (int) y)) return new int[] { -1, -1 };
            return new int[] { (int) ((x - dst.left) * W / dst.width() / 2), (int) ((y - dst.top) * H / dst.height() / 2) };
        }
    }
    @Override public boolean onTouchEvent(android.view.MotionEvent e) {
        if (tv.active() && tvArea().contains((int) e.getX(), (int) e.getY())) { tv.touch(e); return true; }
        int[] v = virt(e.getX(), e.getY());
        float slop = 12 * getResources().getDisplayMetrics().density;
        switch (e.getActionMasked()) {
            case android.view.MotionEvent.ACTION_DOWN:
                downX = e.getX(); downY = e.getY(); dragging = false;
                synchronized (lock) { touchq.add(new int[] { 0, v[0], v[1] }); }
                return true;
            case android.view.MotionEvent.ACTION_MOVE:
                if (!dragging && Math.abs(e.getY() - downY) > slop && Math.abs(e.getY() - downY) > Math.abs(e.getX() - downX)) dragging = true;
                if (dragging) synchronized (lock) { touchq.add(new int[] { 1, v[0], v[1] }); }
                return true;
            case android.view.MotionEvent.ACTION_UP:
                float dx = e.getX() - downX, dy = e.getY() - downY;
                synchronized (lock) {
                    if (!dragging && Math.abs(dx) > slop * 5 && Math.abs(dx) > Math.abs(dy) * 2) touchq.add(new int[] { dx < 0 ? 3 : 4, v[0], v[1] });
                    else if (!dragging && Math.abs(dx) < slop && Math.abs(dy) < slop) touchq.add(new int[] { 2, v[0], v[1] });
                }
                return true;
        }
        return super.onTouchEvent(e);
    }
    static int footerKey(String k) {
        if (k == null) return Keys.NONE;
        switch (k) {
            case "A": return Keys.A; case "B": return Keys.B; case "X": return Keys.X; case "Y": return Keys.Y;
            case "START": return Keys.START; case "SELECT": return Keys.SELECT; case "MENU": return Keys.MENU;
            case "L/R": return Keys.R1;
            default: return Keys.NONE;
        }
    }
    @Override public boolean onKeyUp(int code, KeyEvent e) { return Keys.map(code) != Keys.NONE || super.onKeyUp(code, e); }

    @Override public void onSurfaceCreated(javax.microedition.khronos.opengles.GL10 g, javax.microedition.khronos.egl.EGLConfig c) { crt.init(); tv.glReady(crt.videoTexture()); }
    @Override public void onSurfaceChanged(javax.microedition.khronos.opengles.GL10 g, int w, int hh) {
        android.opengl.GLES20.glViewport(0, 0, w, hh);
        stopLoop();
        sw = w; sh = hh;
        synchronized (lock) { layout(); Native.setTheme(theme); }
        running = true;
        loop = new Thread(this::run, "draw"); loop.start();
    }
    /** wide screens: TV on the left half, menus on the right. Portrait: TV on top, menus below.
     *  Squarer screens: menus only, and videos play full screen. */
    void layout() {
        double a = sw / (double) sh;
        mode = a >= 1.5 ? 1 : a < 0.9 ? 2 : 0;
        if (mode == 1) { tvR.set(0, 0, sw / 2, sh); menuR.set(sw / 2, 0, sw, sh); }
        else if (mode == 2) { int th = sw * 3 / 4; tvR.set(0, 0, sw, th); menuR.set(0, th, sw, sh); }
        else { tvR.set(0, 0, sw, sh); menuR.set(0, 0, sw, sh); }
        int mw = menuR.width(), mh = menuR.height();
        /* the menus are drawn small and scaled up with crisp pixels: 640 wide for tall panels, ~480-540 lines otherwise */
        if (mw < mh * 1.2) { W = 640; H = (int) Math.round(640.0 * mh / mw) & ~1; }
        else { H = mh >= 1000 ? 540 : 480; W = Math.min(1280, (int) Math.round(H * (double) mw / mh)) & ~1; }
        px = new int[W * H]; bmp = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888);
        Native.select(0, W, H);
        placeTv();
    }
    /** where the TV is on screen right now: its half, or the whole screen when full screen (or no room beside it) */
    Rect tvArea() { return full || mode == 0 ? new Rect(0, 0, sw, sh) : new Rect(tvR); }
    void placeTv() { view.requestRender(); }
    void setFull(boolean f) { full = f; placeTv(); }
    /** the GL thread: everything goes through the screen effect; only the TV gets the curved glass */
    @Override public void onDrawFrame(javax.microedition.khronos.opengles.GL10 g) {
        android.opengl.GLES20.glClearColor(18 / 255f, 10 / 255f, 30 / 255f, 1f);
        android.opengl.GLES20.glClear(android.opengl.GLES20.GL_COLOR_BUFFER_BIT);
        boolean hasFrame = tv.latch();
        synchronized (lock) {
            if (bmp == null) return;
            boolean tvFull = tv.active() && (full || mode == 0);
            if (!tvFull) crt.draw(0, bmp, drawMenu, sw, sh, fx, false, false);
            Rect area = tvArea();
            if (tv.active()) {
                /* black glass, then the picture fitted inside it, then the time bar */
                android.opengl.GLES20.glEnable(android.opengl.GLES20.GL_SCISSOR_TEST);
                android.opengl.GLES20.glScissor(area.left, sh - area.bottom, area.width(), area.height());
                android.opengl.GLES20.glClearColor(0, 0, 0, 1); android.opengl.GLES20.glClear(android.opengl.GLES20.GL_COLOR_BUFFER_BIT);
                android.opengl.GLES20.glDisable(android.opengl.GLES20.GL_SCISSOR_TEST);
                if (hasFrame && tv.ready) { Rect vr = new Rect(); fit(area, tv.vw, tv.vh, vr); crt.drawVideo(vr, sw, sh, fx, curve, tv.stm, 270f * vr.height() / Math.max(1, area.height())); }
                if (tv.barShow && tv.barBmp != null) crt.draw(1, tv.barBmp, area, sw, sh, fx == 0 ? 0 : 1, curve, true);
            } else if (drawIdle && tvBmp != null) crt.draw(1, tvBmp, drawTv, sw, sh, fx, curve, false);
        }
    }
    void stopLoop() { running = false; if (loop != null) try { loop.join(); } catch (InterruptedException ignored) { } loop = null; }

    void run() {
        paint.setFilterBitmap(false);
        while (running) {
            long start = System.nanoTime();
            synchronized (lock) {
                while (!keyq.isEmpty()) {
                    int k = keyq.poll();
                    Screen s = stack.peek();
                    if (s != null) s.key(k);
                }
                while (!touchq.isEmpty()) {
                    int[] ev = touchq.poll();
                    Screen s = stack.peek();
                    if (s != null) s.touch(ev[0], ev[1], ev[2]);
                }
                Screen s = stack.peek();
                if (s == null) { runOnUiThread(this::finish); running = false; break; }
                double t = now();
                Native.select(0, W, H);
                s.draw(t);
                Native.frame(px);
                bmp.setPixels(px, 0, W, 0, 0, W, H);
                boolean idleTv = mode != 0 && !tv.active() && !(s instanceof Hello);
                if (idleTv) {   /* an untuned TV with the little guy hopping on it, until something plays */
                    int th = Math.max(240, (int) Math.round(640.0 * tvR.height() / Math.max(1, tvR.width()))) & ~1;
                    if (tvBmp == null || tvBmp.getHeight() != th) { tvPx = new int[640 * th]; tvBmp = Bitmap.createBitmap(640, th, Bitmap.Config.ARGB_8888); }
                    Native.select(1, 640, th);
                    Native.tvIdle("Tuning in...", t);
                    Native.frame(tvPx);
                    tvBmp.setPixels(tvPx, 0, 640, 0, 0, 640, th);
                }
                if (tv.active()) tv.drawBar(t, tvArea());
                Native.select(0, W, H);
                Rect mr = s instanceof Hello ? new Rect(0, 0, sw, sh) : menuR;
                fit(mr, W, H, dst);
                synchronized (dst) { drawMenu.set(dst); }
                drawIdle = idleTv;
                if (idleTv) drawTv.set(tvR);
            }
            view.requestRender();
            long el = (System.nanoTime() - start) / 1000000;
            if (el < 33) try { Thread.sleep(33 - el); } catch (InterruptedException ignored) { }
        }
    }

    /** fits a w x h picture inside r, centred, keeping its shape */
    void fit(Rect r, int w, int h, Rect out) {
        double sc = Math.min(r.width() / (double) w, r.height() / (double) h);
        int dw = (int) (w * sc), dh = (int) (h * sc);
        synchronized (dst) { out.set(r.left + (r.width() - dw) / 2, r.top + (r.height() - dh) / 2, r.left + (r.width() + dw) / 2, r.top + (r.height() + dh) / 2); }
    }
    String hello = SAY[(int) (System.currentTimeMillis() % SAY.length)];
    void push(Screen s) { stack.push(s); }
    void pop() { stack.pop(); }
    void runBg(Runnable r) { new Thread(r, "net").start(); }

    /** plays the downloaded copy if there is one, otherwise streams from archive.org */
    void play(String id, String name, String title) {
        java.io.File f = Store.local(id, name);
        playSrc(f.exists() ? f.getPath() : Archive.downloadUrl(id, name), title, id + "/" + name);
    }
    void playSrc(String src, String title, String key) {
        runOnUiThread(() -> { tv.play(src, title, key); musicOn(false); placeTv(); });
        view.setRenderMode(android.opengl.GLSurfaceView.RENDERMODE_WHEN_DIRTY);
    }
    /** the TV stopped: back to the little guy, and the music */
    void tvStopped() { full = false; placeTv(); runOnUiThread(() -> musicOn(true)); }
    /** the download % in the footer, like the Miyoo's arrow badge */
    void badge() { String b = Downloader.badge(); if (b != null) Native.footerBadge(b); }

    // ---------------- screens ----------------
    abstract class Screen {
        abstract void draw(double t); abstract void key(int k);
        /** taps on footer hints press that button; a tap on the little TV in the corner goes back */
        void touch(int kind, int x, int y) {
            if (kind != 2) return;
            int k = footerKey(Native.hitFooter(x, y));
            if (k != Keys.NONE) { key(k); return; }
            if (x < 36 && y < 36) key(Keys.B);
            else tapElse(x, y);
        }
        void tapElse(int x, int y) { }
    }

    static final String[] SAY = { "Let's watch old TV!", "Old TV was better, right?", "Hey, let's go back in time!", "I hate new anime, blehhh", "Let's go back to the 90s!", "Eat my shorts!" };

    class Hello extends Screen {
        final String say = SAY[(int) (System.currentTimeMillis() % SAY.length)]; double start = -1;
        void draw(double t) {
            if (start < 0) start = t;
            int shown = (int) ((t - start) * 20);
            Native.hello(say, shown, shown > say.length() + 6 ? "A:Start" : null, t);
        }
        void key(int k) { if (k == Keys.A || k == Keys.START) { sfx(sSelect); stack.pop(); push(new Tabs()); } else if (k == Keys.B) { stack.pop(); } }
        void touch(int kind, int x, int y) { if (kind == 2) key(Keys.A); }
    }

    /** a list of rows the user can move through; subclasses fill rows and react to buttons */
    abstract class ListScreen extends Screen {
        List<String> m = new ArrayList<>(), s = new ArrayList<>(); List<Integer> tone = new ArrayList<>();
        int sel = 0, top = 0; double selT = 0; String status = ""; double statusT = -10;
        String empty = "Nothing here.";
        void row(String a, String b, int tn) { m.add(a); s.add(b == null ? "" : b); tone.add(tn); }
        void clear() { m.clear(); s.clear(); tone.clear(); }
        void say(String msg) { status = msg; statusT = now(); }
        void drawList(double t) {
            int n = m.size(), vis = Native.visibleRows();
            if (sel >= n) sel = Math.max(0, n - 1);
            if (sel < top) top = sel; if (sel >= top + vis) top = sel - vis + 1;
            int[] tn = new int[n]; for (int i = 0; i < n; i++) tn[i] = tone.get(i);
            Native.list(m.toArray(new String[0]), s.toArray(new String[0]), tn, sel, top, (int) ((t - selT) * 1000), t, empty);
            if (t - statusT < 2.5 && !status.isEmpty()) Native.toast(status);
        }
        int dragBase, dragY0;
        void touch(int kind, int x, int y) {
            if (kind == 0) { dragBase = sel; dragY0 = y; return; }
            if (kind == 1) {   /* drag: the list follows the finger, one row per row height */
                int n = m.size(); if (n == 0) return;
                int to = Math.max(0, Math.min(n - 1, dragBase + (dragY0 - y) / 18));
                if (to != sel) { sel = to; selT = now(); sfx(sMove); }
                return;
            }
            if (kind == 3 || kind == 4) { swipe(kind == 3); return; }
            if (kind != 2) return;
            int r = Native.hitRow(x, y);
            if (r >= 0 && top + r < m.size() && Native.hitFooter(x, y) == null) {
                if (top + r == sel) key(Keys.A); else { sel = top + r; selT = now(); sfx(sMove); }
                return;
            }
            super.touch(kind, x, y);
        }
        void swipe(boolean left) { }
        boolean move(int k) {
            int n = m.size(), vis = Native.visibleRows(), o = sel;
            if (n == 0) return false;
            if (k == Keys.UP) sel = (sel + n - 1) % n;
            else if (k == Keys.DOWN) sel = (sel + 1) % n;
            else if (k == Keys.LEFT) sel = Math.max(0, sel - vis);
            else if (k == Keys.RIGHT) sel = Math.min(n - 1, sel + vis);
            else return false;
            if (o != sel) { selT = now(); sfx(sMove); }
            return true;
        }
    }

    // --- the home tabs ---
    static final String[] TABS = { "Search", "Guide", "Saved", "Lists", "Files", "\u0001" };
    class Tabs extends ListScreen {
        int tab = 1; Guide.Node node;
        List<String[]> saved = new ArrayList<>(); List<String> pls = new ArrayList<>();
        List<Downloader.Job> jobs = new ArrayList<>(); List<java.io.File> vids = new ArrayList<>(), parts = new ArrayList<>();
        double lastFill = 0;
        Tabs() { node = Guide.load(MainActivity.this); fill(); }
        void fill() {
            clear(); lastFill = now();
            switch (tab) {
                case 0: row("Search videos...", "", 4); row("Search collections...", "", 4); empty = ""; break;
                case 1: for (Guide.Node c : node.kids) row(c.name, c.isFolder() ? "" + c.kids.size() : "", c.isFolder() ? 4 : 0); empty = "No guide."; break;
                case 2:
                    saved = Store.saved();
                    for (String[] r : saved) row(r.length > 2 ? r[2] : r[1], r[0].equals("item") ? "" : r[0].endsWith("search") ? "search" : "folder", r[0].equals("item") ? 0 : 4);
                    empty = "Nothing saved yet.\nPress Y on a show to save it."; break;
                case 3:
                    pls = Store.playlistNames(); row("+ New playlist", "", 4);
                    for (String n : pls) row(n, "" + Store.playlist(n).size(), 0);
                    empty = ""; break;
                case 4: fillFiles(); empty = "Nothing yet.\nDownloads show up here."; break;
                default:
                    row("Music", music ? "On" : "Off", 0); row("Menu sounds", sounds ? "On" : "Off", 0);
                    row("Theme", Native.themeName(theme), 0); row("Group episodes of a show", group ? "On" : "Off", 0);
                    row("Swap A and B", Keys.swapAB ? "On" : "Off", 0); row("Screen effect", Crt.NAMES[fx], 0); row("Curved screen", curve ? "On" : "Off", 0); row("About ArchiveTV", "", 4); empty = "";
            }
        }
        void fillFiles() {
            synchronized (Downloader.class) { jobs = new ArrayList<>(Downloader.queue); }
            vids.clear(); parts.clear();
            java.io.File[] fs = Store.videos.listFiles();
            if (fs != null) {
                java.util.Arrays.sort(fs, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
                for (java.io.File f : fs) {
                    String n = f.getName();
                    if (n.endsWith(".part")) { String d = f.getPath().substring(0, f.getPath().length() - 5); if (Downloader.find(d) == null) parts.add(f); }
                    else if (Archive.isVideo(n)) vids.add(f);
                }
            }
            for (Downloader.Job jb : jobs) {
                String st;
                if (Downloader.active == jb) st = jb.total > 0 ? (100 * jb.got / jb.total) + "%" : "starting";
                else st = jb.state == 1 ? "failed" : jb.state == 2 ? "paused " + Archive.size(jb.got) : "queued";
                row(jb.disp, st, Downloader.active == jb ? 1 : jb.state == 1 ? 2 : 3);
            }
            for (java.io.File f : parts) row(Store.disp(new java.io.File(f.getPath().substring(0, f.getPath().length() - 5))), "paused " + Archive.size(f.length()), 3);
            for (java.io.File f : vids) row(Store.disp(f), Archive.size(f.length()), 0);
        }
        void draw(double t) {
            if (tab == 4 && t - lastFill > 1) { int o = sel; fill(); sel = o; }
            Native.bg(t); Native.headerTabs(TABS, tab, null, t); drawList(t);
            String[] F = { "A:Open  L/R:Tab", "A:Open  L/R:Tab", "A:Open  X:Remove  L/R:Tab", "A:Open  X:Delete  L/R:Tab", "A:Menu  START:Play  L/R:Tab", "A:Change  L/R:Tab" };
            Native.footer(F[tab]); badge();
        }
        void key(int k) {
            if (move(k)) return;
            if (k == Keys.L1 || k == Keys.L2 || k == Keys.R1 || k == Keys.R2) {
                tab = (tab + ((k == Keys.L1 || k == Keys.L2) ? TABS.length - 1 : 1)) % TABS.length; sel = 0; top = 0; selT = now(); sfx(sTab); fill(); return;
            }
            if (k == Keys.SELECT) { theme = (theme + 1) % Native.themeCount(); Native.setTheme(theme); save(); say("Theme: " + Native.themeName(theme)); fill(); return; }
            if (k == Keys.B && tv.active()) { tv.takeFocus(); return; }
            if (k == Keys.MENU || k == Keys.B) { sfx(sBack); push(new Confirm()); return; }
            if (k == Keys.X) { removeAt(); return; }
            if (k != Keys.A && k != Keys.START) return;
            if (m.isEmpty()) return;
            sfx(sSelect);
            switch (tab) {
                case 0: push(new Keyboard(sel == 0 ? "Search videos" : "Search collections", "SEARCH", q -> { pop(); push(new Results(sel == 0 ? "search" : "csearch", q, q)); })); break;
                case 1: { Guide.Node c = node.kids.get(sel); if (c.isFolder()) push(new Folder(c)); else open(c.kind, c.arg, c.name); break; }
                case 2: { String[] r = saved.get(sel); open(r[0], r[1], r.length > 2 ? r[2] : r[1]); break; }
                case 3:
                    if (sel == 0) push(new Keyboard("New playlist", "CREATE", n -> { pop(); java.io.File f = Store.plFile(n); try { f.createNewFile(); } catch (Exception ignored) { } fill(); }));
                    else push(new PlaylistScreen(pls.get(sel - 1)));
                    break;
                case 4: filesAct(k); break;
                default:
                    switch (sel) {
                        case 0: music = !music; musicOn(true); break;
                        case 1: sounds = !sounds; break;
                        case 2: theme = (theme + 1) % Native.themeCount(); Native.setTheme(theme); break;
                        case 3: group = !group; break;
                        case 4: Keys.swapAB = !Keys.swapAB; break;
                        case 5: fx = (fx + 1) % Crt.NAMES.length; break;
                        case 6: curve = !curve; break;
                        default: push(new About()); return;
                    }
                    save(); fill();
            }
        }
        void tapElse(int x, int y) { }
        void touch(int kind, int x, int y) {
            if (kind == 2) { int tb = Native.hitTab(x, y); if (tb >= 0) { if (tb != tab) { tab = tb; sel = 0; top = 0; selT = now(); sfx(sTab); fill(); } return; } }
            super.touch(kind, x, y);
        }
        void swipe(boolean left) { key(left ? Keys.R1 : Keys.L1); }
        void removeAt() {
            if (m.isEmpty()) return;
            if (tab == 2) { String name = m.get(sel); push(new Ask("Remove?", "Remove from Saved?", name, "A:Remove  B:Keep", () -> { Store.unsave(sel); fill(); say("Removed."); })); }
            else if (tab == 3 && sel > 0) { String n = pls.get(sel - 1); push(new Ask("Delete?", "Delete this playlist?", n, "A:Delete  B:Keep", () -> { Store.plFile(n).delete(); fill(); say("Playlist deleted."); })); }
            else if (tab == 4) filesAct(Keys.X);
        }
        void filesAct(int k) {
            int i = sel;
            if (i < jobs.size()) {
                Downloader.Job jb = jobs.get(i);
                String id = jb.dst.substring(jb.dst.lastIndexOf('/') + 1);
                if (k == Keys.START) { playSrc(jb.url, jb.disp, keyFor(jb.dst)); return; }
                if (k == Keys.X) { push(new Ask("Delete?", "Stop and delete this download?", jb.disp, "A:Delete  B:Keep", () -> { Downloader.remove(jb, true); fill(); say("Download deleted."); })); return; }
                String[] o = { "Watch now", jb.state == 1 ? "Try again" : jb.state == 2 ? "Resume download" : "Pause download", "Delete download", "Back" };
                push(new Menu(jb.disp, o, c -> {
                    if (c == 0) playSrc(jb.url, jb.disp, keyFor(jb.dst));
                    else if (c == 1) { Downloader.pause(jb, jb.state == 0); say(jb.state == 2 ? "Paused. Resume it any time." : "Resuming..."); }
                    else if (c == 2) Downloader.remove(jb, true);
                    fill();
                }));
                return;
            }
            i -= jobs.size();
            if (i < parts.size()) {
                java.io.File pf = parts.get(i);
                String[] o = { "Delete download", "Back" };
                push(new Menu(Store.disp(pf), o, c -> { if (c == 0) { pf.delete(); fill(); } }));
                return;
            }
            i -= parts.size();
            if (i < vids.size()) {
                java.io.File f = vids.get(i);
                if (k == Keys.START) { playSrc(f.getPath(), Store.disp(f), keyFor(f.getPath())); return; }
                if (k == Keys.X) { push(new Ask("Delete?", "Delete this video?", Store.disp(f), "A:Delete  B:Keep", () -> { f.delete(); fill(); say("Deleted."); })); return; }
                String[] o = { "Play", "Add to playlist...", "Delete", "Back" };
                push(new Menu(Store.disp(f), o, c -> {
                    String n = f.getName(); int sp = n.indexOf("__");
                    if (c == 0) playSrc(f.getPath(), Store.disp(f), keyFor(f.getPath()));
                    else if (c == 1 && sp > 0) choosePlaylist(n.substring(0, sp), n.substring(sp + 2), Store.disp(f));
                    else if (c == 2) { f.delete(); fill(); }
                }));
            }
        }
    }
    /** the resume key for a downloaded file: "<id>/<file name>", the same as when streaming */
    static String keyFor(String path) { String n = new java.io.File(path).getName(); int i = n.indexOf("__"); return i > 0 ? n.substring(0, i) + "/" + n.substring(i + 2) : n; }

    void choosePlaylist(String id, String name, String title) {
        List<String> pls = Store.playlistNames();
        String[] o = new String[Math.min(pls.size(), 12) + 2];
        o[0] = "+ New playlist"; for (int i = 0; i < o.length - 2; i++) o[i + 1] = pls.get(i); o[o.length - 1] = "Back";
        push(new Menu("Add to playlist", o, c -> {
            if (c == 0) push(new Keyboard("New playlist", "CREATE", n -> { pop(); Store.plAdd(n, id, name, title); toastLater("Added to " + n + "!"); }));
            else if (c < o.length - 1) { boolean ok = Store.plAdd(o[c], id, name, title); toastLater(ok ? "Added to " + o[c] + "!" : "Already in " + o[c]); }
        }));
    }
    void toastLater(String msg) { Screen s = stack.peek(); if (s instanceof ListScreen) ((ListScreen) s).say(msg); }

    /** the A-button menu on a video of an item */
    void fileMenu(String id, Archive.VFile v, int k) {
        java.io.File f = Store.local(id, v.name);
        if (k == Keys.START) { play(id, v.name, v.disp); return; }
        Downloader.Job jb = Downloader.find(f.getPath());
        String[] o = f.exists() ? new String[] { "Watch", "Add to playlist...", "Delete from device", "Back" }
                : new String[] { "Watch now", jb != null ? "Already in queue" : "Download", "Add to playlist...", "Back" };
        push(new Menu(v.disp, o, c -> {
            String a = o[c];
            if (a.startsWith("Watch")) play(id, v.name, v.disp);
            else if (a.equals("Download")) { int r = Downloader.add(Archive.downloadUrl(id, v.name), f, v.disp); toastLater(r == 0 ? "Added to the queue (see Files tab)" : "Already in the queue"); }
            else if (a.startsWith("Add to playlist")) choosePlaylist(id, v.name, v.disp);
            else if (a.startsWith("Delete")) { f.delete(); toastLater("Deleted."); }
        }));
    }

    class PlaylistScreen extends ListScreen {
        final String name; List<String[]> items;
        PlaylistScreen(String n) { name = n; empty = "Empty playlist.\nAdd videos from any show."; fill(); }
        void fill() { clear(); items = Store.playlist(name); for (String[] r : items) row(r.length > 2 ? r[2] : r[1], Store.local(r[0], r[1]).exists() ? "on device" : "", Store.local(r[0], r[1]).exists() ? 1 : 0); }
        void draw(double t) { Native.bg(t); Native.header(name, null, t, Native.HAPPY); drawList(t); Native.footer("A:Play  X:Remove  B:Back"); badge(); }
        void key(int k) {
            if (move(k)) return;
            if (k == Keys.B) { sfx(sBack); pop(); return; }
            if (m.isEmpty()) return;
            String[] r = items.get(sel);
            if (k == Keys.X) { Store.plRemove(name, sel); fill(); say("Removed."); return; }
            if (k == Keys.A || k == Keys.START) { sfx(sSelect); play(r[0], r[1], r.length > 2 ? r[2] : r[1]); }
        }
    }

    interface Pick { void picked(int i); }
    interface Text { void done(String s); }
    class Menu extends Screen {
        final String title; final String[] items; final Pick cb; int sel = 0;
        Menu(String title, String[] items, Pick cb) { this.title = title; this.items = items; this.cb = cb; }
        void draw(double t) { Screen under = stack.size() > 1 ? new ArrayList<>(stack).get(1) : null; if (under != null) under.draw(t); Native.menu(title, items, sel, t); }
        void touch(int kind, int x, int y) {
            if (kind != 2) return;
            int i = Native.hitMenu(x, y);
            if (i == -2) { sfx(sBack); pop(); }
            else if (i >= 0 && i < items.length) { sel = i; key(Keys.A); }
        }
        void key(int k) {
            if (k == Keys.UP) { sel = (sel + items.length - 1) % items.length; sfx(sMove); }
            else if (k == Keys.DOWN) { sel = (sel + 1) % items.length; sfx(sMove); }
            else if (k == Keys.B) { sfx(sBack); pop(); }
            else if (k == Keys.A || k == Keys.START) { sfx(sSelect); pop(); if (!items[sel].equals("Back")) cb.picked(sel); }
        }
    }
    class Ask extends Screen {
        final String title, l1, l2, hints; final Runnable yes;
        Ask(String title, String l1, String l2, String hints, Runnable yes) { this.title = title; this.l1 = l1; this.l2 = l2; this.hints = hints; this.yes = yes; }
        void draw(double t) { Native.message(title, l1, l2, Native.SAD, hints, t); }
        void key(int k) { if (k == Keys.A) { pop(); yes.run(); } else if (k == Keys.B) { sfx(sBack); pop(); } }
    }

    void open(String kind, String arg, String title) { if (kind.equals("item")) push(new ItemScreen(arg, title)); else push(new Results(kind, arg, title)); }

    class Confirm extends Screen {
        void draw(double t) { Native.message("ArchiveTV", "Turn off ArchiveTV?", null, Native.SLEEP, "A:Yes  B:No", t); }
        void key(int k) { if (k == Keys.A) { stack.clear(); } else if (k == Keys.B) { sfx(sBack); pop(); } }
    }
    class About extends Screen {
        void draw(double t) { Native.about(t); }
        void key(int k) { if (k == Keys.B || k == Keys.A) { sfx(sBack); pop(); } }
    }

    class Folder extends ListScreen {
        final Guide.Node node;
        Folder(Guide.Node n) { node = n; for (Guide.Node c : n.kids) row(c.name, c.isFolder() ? "" + c.kids.size() : "", c.isFolder() ? 4 : 0); }
        void draw(double t) { Native.bg(t); Native.header(node.name, null, t, Native.HAPPY); drawList(t); Native.footer("A:Open  B:Back"); }
        void key(int k) {
            if (move(k)) return;
            if (k == Keys.B) { sfx(sBack); pop(); return; }
            if ((k == Keys.A || k == Keys.START) && !m.isEmpty()) { sfx(sSelect); Guide.Node c = node.kids.get(sel); if (c.isFolder()) push(new Folder(c)); else open(c.kind, c.arg, c.name); }
        }
    }

    /** "Tuning in..." while something loads in the background */
    abstract class Loading extends ListScreen {
        volatile boolean loading = true; volatile String error;
        String title;
        void load() { loading = true; error = null; runBg(() -> { try { fetch(); } catch (Exception e) { error = e.getMessage(); } loading = false; }); }
        abstract void fetch() throws Exception;
        boolean drawLoading(double t) {
            if (loading) { Native.message("Tuning in...", "Tuning in...", title, Native.STATIC, "B:Cancel", t); return true; }
            if (error != null) { Native.message("Oh no!", "Couldn't reach archive.org", "Check Wi-Fi and try again", Native.SAD, "A:Retry  B:Back", t); return true; }
            return false;
        }
        boolean keyLoading(int k) {
            if (loading) { if (k == Keys.B) { sfx(sBack); pop(); } return true; }
            if (error != null) { if (k == Keys.A) load(); else if (k == Keys.B) { sfx(sBack); pop(); } return true; }
            return false;
        }
    }

    class Results extends Loading {
        final String kind, arg; int sort = 0, page = 1, found = 0;
        final List<Archive.Item> items = new ArrayList<>(); Series.Fold fold;
        Results(String kind, String arg, String title) { this.kind = kind; this.arg = arg; this.title = title; empty = "Nothing found. Try other words?"; load(); }
        void fetch() throws Exception {
            Archive.Page p = Archive.search(kind, arg, page, sort);
            synchronized (lock) { if (page == 1) items.clear(); items.addAll(p.items); found = p.found; rebuild(); }
        }
        void rebuild() {
            List<String> names = new ArrayList<>(); for (Archive.Item it : items) names.add(it.title);
            fold = Series.fold(names, group);
            clear();
            for (int r : fold.rows) {
                if (r >= 0) { Archive.Item it = items.get(r); row(it.title, it.coll ? "folder" : "", it.coll ? 4 : 0); }
                else { int g = -r - 1; row(fold.names.get(g), "" + fold.groups.get(g).size(), 4); }
            }
            if (items.size() < found && items.size() < 2000) row("More...", "", 4);
        }
        void draw(double t) {
            if (drawLoading(t)) return;
            Native.bg(t); Native.header(title, null, t, Native.HAPPY); drawList(t); Native.footer("A:Open  Y:Save  X:Sort  B:Back"); badge();
        }
        void key(int k) {
            if (keyLoading(k) || move(k)) return;
            if (k == Keys.B) { sfx(sBack); pop(); return; }
            if (k == Keys.X) { sort = (sort + 1) % Archive.SORTS.length; page = 1; sel = 0; top = 0; say(Archive.SORT_NAMES[sort]); load(); return; }
            if (k == Keys.Y) {
                if (sel < fold.rows.size() && fold.rows.get(sel) >= 0) { Archive.Item it = items.get(fold.rows.get(sel)); say(Store.save(it.coll ? "collection" : "item", it.id, it.title) ? "Saved!" : "Already saved"); }
                else say(Store.save(kind, arg, title) ? "Saved!" : "Already saved");
                return;
            }
            if ((k != Keys.A && k != Keys.START) || m.isEmpty()) return;
            sfx(sSelect);
            if (sel >= fold.rows.size()) { page++; load(); return; }
            int r = fold.rows.get(sel);
            if (r >= 0) openItem(items.get(r));
            else { int g = -r - 1; push(new Group(fold.names.get(g), fold.groups.get(g), items)); }
        }
    }
    void openItem(Archive.Item it) { if (it.coll) push(new Results("collection", it.id, it.title)); else push(new ItemScreen(it.id, it.title)); }

    class Group extends ListScreen {
        final String title; final List<Integer> mem; final List<Archive.Item> items;
        Group(String title, List<Integer> mem, List<Archive.Item> items) { this.title = title; this.mem = mem; this.items = items; for (int i : mem) row(items.get(i).title, items.get(i).coll ? "folder" : "", items.get(i).coll ? 4 : 0); }
        void draw(double t) { Native.bg(t); Native.header(title, null, t, Native.HAPPY); drawList(t); Native.footer("A:Open  B:Back"); }
        void key(int k) {
            if (move(k)) return;
            if (k == Keys.B) { sfx(sBack); pop(); return; }
            if ((k == Keys.A || k == Keys.START) && !m.isEmpty()) { sfx(sSelect); openItem(items.get(mem.get(sel))); }
        }
    }

    class ItemScreen extends Loading {
        final String id; List<Archive.VFile> files = new ArrayList<>(); Series.Fold fold; boolean az = false;
        ItemScreen(String id, String title) { this.id = id; this.title = title; empty = "No video files in this item."; load(); }
        void fetch() throws Exception {
            String[] tt = new String[1];
            List<Archive.VFile> f = Archive.files(id, tt);
            synchronized (lock) { files = f; if (tt[0] != null) title = tt[0]; rebuild(); }
        }
        void rebuild() {
            if (az) files.sort((a, b) -> Series.natcmp(a.disp, b.disp));
            else files.sort((a, b) -> a.rank != b.rank ? a.rank - b.rank : Series.natcmp(a.disp, b.disp));
            List<String> names = new ArrayList<>(); for (Archive.VFile v : files) names.add(v.disp);
            fold = Series.fold(names, group);
            clear();
            for (int r : fold.rows) {
                if (r >= 0) { Archive.VFile v = files.get(r); row(v.disp, Archive.size(v.size), v.rank == 0 ? 1 : 0); }
                else { int g = -r - 1; row(fold.names.get(g), fold.groups.get(g).size() + " eps", 4); }
            }
        }
        void draw(double t) {
            if (drawLoading(t)) return;
            Native.bg(t); Native.header(title, null, t, Native.HAPPY); drawList(t); Native.footer("A:Menu  X:Sort  Y:Save  B:Back"); badge();
        }
        void key(int k) {
            if (keyLoading(k) || move(k)) return;
            if (k == Keys.B) { sfx(sBack); pop(); return; }
            if (k == Keys.X) { az = !az; rebuild(); sel = 0; top = 0; say(az ? "Sorted: A to Z (episode order)" : "Sorted: best quality first"); return; }
            if (k == Keys.Y) { say(Store.save("item", id, title) ? "Saved!" : "Already saved"); return; }
            if ((k != Keys.A && k != Keys.START) || m.isEmpty()) return;
            sfx(sSelect);
            int r = fold.rows.get(sel);
            if (r >= 0) fileMenu(id, files.get(r), k);
            else { int g = -r - 1; push(new Episodes(this, fold.names.get(g), fold.groups.get(g))); }
        }
    }
    class Episodes extends ListScreen {
        final ItemScreen it; final String title; final List<Integer> mem;
        Episodes(ItemScreen it, String title, List<Integer> mem) { this.it = it; this.title = title; this.mem = mem; for (int i : mem) { Archive.VFile v = it.files.get(i); row(v.disp, Archive.size(v.size), v.rank == 0 ? 1 : 0); } }
        void draw(double t) { Native.bg(t); Native.header(title, null, t, Native.HAPPY); drawList(t); Native.footer("A:Menu  START:Watch  B:Back"); badge(); }
        void key(int k) {
            if (move(k)) return;
            if (k == Keys.B) { sfx(sBack); pop(); return; }
            if ((k == Keys.A || k == Keys.START) && !m.isEmpty()) { sfx(sSelect); fileMenu(it.id, it.files.get(mem.get(sel)), k); }
        }
    }

    /** the on-screen keyboard from the Miyoo app */
    class Keyboard extends Screen {
        final String title, action; final Text cb; StringBuilder text = new StringBuilder(); int kx = 0, ky = 0; boolean caps = false;
        Keyboard(String title, String action, Text cb) { this.title = title; this.action = action; this.cb = cb; }
        void draw(double t) { Native.keyboard(title, text.toString(), kx, ky, t, action, caps ? 1 : 0); }
        void key(int k) {
            /* ui.c lays the keys out 10 wide by 4 rows, plus a row of special keys (ky == 4) */
            switch (k) {
                case Keys.UP: ky = (ky + 4) % 5; sfx(sMove); return;
                case Keys.DOWN: ky = (ky + 1) % 5; sfx(sMove); return;
                case Keys.LEFT: kx = (kx + 9) % 10; sfx(sMove); return;
                case Keys.RIGHT: kx = (kx + 1) % 10; sfx(sMove); return;
                case Keys.B: if (text.length() > 0) text.setLength(text.length() - 1); else { sfx(sBack); pop(); } return;
                case Keys.X: if (text.length() > 0) text.setLength(text.length() - 1); return;
                case Keys.Y: text.append(' '); return;
                case Keys.SELECT: caps = !caps; return;
                case Keys.START: submit(); return;
                case Keys.A:
                    if (ky == 4) {
                        int sp = Native.kbSpecial(kx);
                        if (sp == 0) caps = !caps; else if (sp == 1) text.append(' '); else if (sp == 2) { if (text.length() > 0) text.setLength(text.length() - 1); } else submit();
                        return;
                    }
                    int ch = Native.kbChar(kx, ky);
                    if (ch > 0 && text.length() < 60) { char c = (char) ch; text.append(caps ? Character.toUpperCase(c) : c); sfx(sSelect); }
            }
        }
        void submit() { String q = text.toString().trim(); if (q.isEmpty()) return; sfx(sSelect); cb.done(q); }
        void tapElse(int x, int y) { int h = Native.hitKb(x, y); if (h >= 0) { kx = h % 16; ky = h / 16; key(Keys.A); } }
        /* a real keyboard (USB or Bluetooth) types straight in */
        boolean typed(int ch) {
            if (ch == '\n') { submit(); return true; }
            if (ch == '\b') { if (text.length() > 0) text.setLength(text.length() - 1); return true; }
            if (ch >= 32 && ch < 256 && text.length() < 60) { text.append((char) ch); return true; }
            return false;
        }
    }

    static final class Guide {
        static final class Node {
            String name, kind, arg; final List<Node> kids = new ArrayList<>();
            boolean isFolder() { return kind == null; }
            Node child(String n) { for (Node c : kids) if (c.isFolder() && c.name.equals(n)) return c; Node c = new Node(); c.name = n; kids.add(c); return c; }
        }
        static Node load(Activity a) {
            Node root = new Node(); root.name = "Guide"; Node cur = root;
            try (BufferedReader r = new BufferedReader(new InputStreamReader(a.getAssets().open("guide.txt"), "UTF-8"))) {
                String line;
                while ((line = r.readLine()) != null) {
                    line = line.trim();
                    if (line.isEmpty() || line.startsWith("#")) continue;
                    if (line.startsWith("[") && line.endsWith("]")) {
                        cur = root;
                        for (String part : line.substring(1, line.length() - 1).split(">")) cur = cur.child(part.trim());
                        continue;
                    }
                    int bar = line.indexOf('|');
                    String left = (bar >= 0 ? line.substring(0, bar) : line).trim(), name = bar >= 0 ? line.substring(bar + 1).trim() : left;
                    Node n = new Node(); n.name = name;
                    if (left.startsWith("search ")) { n.kind = "search"; n.arg = left.substring(7).trim(); }
                    else if (left.startsWith("csearch ")) { n.kind = "csearch"; n.arg = left.substring(8).trim(); }
                    else if (left.startsWith("item ")) { n.kind = "item"; n.arg = left.substring(5).trim(); }
                    else { n.kind = "collection"; n.arg = left; }
                    cur.kids.add(n);
                }
            } catch (Exception ignored) { }
            return root;
        }
    }
}
