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
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/** ArchiveTV for Android handhelds: the Miyoo app's screens, drawn by the same C code. */
public class MainActivity extends Activity implements SurfaceHolder.Callback {
    static MainActivity self;
    SurfaceView view;
    Thread loop; volatile boolean running;
    int W, H; int[] px; Bitmap bmp; final Paint paint = new Paint();
    final Object lock = new Object();
    final ArrayDeque<Screen> stack = new ArrayDeque<>();
    final ArrayDeque<Integer> keyq = new ArrayDeque<>();
    final double t0 = System.nanoTime() / 1e9;
    SharedPreferences prefs;
    int theme = 0; boolean music = true, sounds = true, group = true;
    SoundPool pool; int sMove, sSelect, sBack, sTab, sError; MediaPlayer bgm;

    double now() { return System.nanoTime() / 1e9 - t0; }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        self = this;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        view = new SurfaceView(this);
        view.getHolder().addCallback(this);
        view.setFocusable(true);
        setContentView(view);
        prefs = getSharedPreferences("atv", MODE_PRIVATE);
        theme = prefs.getInt("theme", 0); music = prefs.getBoolean("music", true); sounds = prefs.getBoolean("sounds", true);
        group = prefs.getBoolean("group", true); Keys.swapAB = prefs.getBoolean("swapab", false);
        pool = new SoundPool.Builder().setMaxStreams(4).setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).build()).build();
        try {
            sMove = pool.load(getAssets().openFd("sfx/move.wav"), 1); sSelect = pool.load(getAssets().openFd("sfx/select.wav"), 1);
            sBack = pool.load(getAssets().openFd("sfx/back.wav"), 1); sTab = pool.load(getAssets().openFd("sfx/tab.wav"), 1);
            sError = pool.load(getAssets().openFd("sfx/error.wav"), 1);
        } catch (Exception ignored) { }
        stack.push(new Hello());
    }

    void save() { prefs.edit().putInt("theme", theme).putBoolean("music", music).putBoolean("sounds", sounds).putBoolean("group", group).putBoolean("swapab", Keys.swapAB).apply(); }
    void sfx(int id) { if (sounds && id != 0) pool.play(id, 0.55f, 0.55f, 1, 0, 1f); }

    void musicOn(boolean on) {
        if (on && music) {
            if (bgm == null) try {
                bgm = new MediaPlayer();
                android.content.res.AssetFileDescriptor fd = getAssets().openFd("music.ogg");
                bgm.setDataSource(fd.getFileDescriptor(), fd.getStartOffset(), fd.getLength());
                bgm.setLooping(true); bgm.setVolume(0.45f, 0.45f); bgm.prepare();
            } catch (Exception e) { bgm = null; }
            if (bgm != null && !bgm.isPlaying()) bgm.start();
        } else if (bgm != null && bgm.isPlaying()) bgm.pause();
    }

    @Override protected void onResume() { super.onResume(); hideBars(); musicOn(true); }
    @Override protected void onPause() { super.onPause(); musicOn(false); }
    void hideBars() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    @Override public boolean onKeyDown(int code, KeyEvent e) {
        int k = Keys.map(code);
        if (k == Keys.NONE) return super.onKeyDown(code, e);
        synchronized (lock) { keyq.add(k); }
        return true;
    }
    @Override public boolean onKeyUp(int code, KeyEvent e) { return Keys.map(code) != Keys.NONE || super.onKeyUp(code, e); }

    @Override public void surfaceCreated(SurfaceHolder h) { }
    @Override public void surfaceChanged(SurfaceHolder h, int fmt, int sw, int sh) {
        stopLoop();
        /* the UI is drawn at about 540 lines and scaled up with crisp pixels; the width follows the screen */
        int vh = sh >= 1000 ? 540 : 480;
        int vw = Math.min(1280, (int) Math.round(vh * (double) sw / sh)) & ~1;
        synchronized (lock) {
            W = vw; H = vh; px = new int[W * H];
            bmp = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888);
            Native.init(W, H); Native.setTheme(theme);
        }
        running = true;
        loop = new Thread(this::run, "draw"); loop.start();
    }
    @Override public void surfaceDestroyed(SurfaceHolder h) { stopLoop(); }
    void stopLoop() { running = false; if (loop != null) try { loop.join(); } catch (InterruptedException ignored) { } loop = null; }

    void run() {
        SurfaceHolder h = view.getHolder();
        Rect dst = new Rect();
        paint.setFilterBitmap(false);
        while (running) {
            long start = System.nanoTime();
            synchronized (lock) {
                while (!keyq.isEmpty()) {
                    int k = keyq.poll();
                    Screen s = stack.peek();
                    if (s != null) s.key(k);
                }
                Screen s = stack.peek();
                if (s == null) { runOnUiThread(this::finish); running = false; break; }
                s.draw(now());
                Native.frame(px);
                bmp.setPixels(px, 0, W, 0, 0, W, H);
            }
            Canvas c = h.lockCanvas();
            if (c != null) {
                c.drawColor(Color.BLACK);
                double sc = Math.min(c.getWidth() / (double) W, c.getHeight() / (double) H);
                int dw = (int) (W * sc), dh = (int) (H * sc);
                dst.set((c.getWidth() - dw) / 2, (c.getHeight() - dh) / 2, (c.getWidth() + dw) / 2, (c.getHeight() + dh) / 2);
                c.drawBitmap(bmp, null, dst, paint);
                h.unlockCanvasAndPost(c);
            }
            long el = (System.nanoTime() - start) / 1000000;
            if (el < 33) try { Thread.sleep(33 - el); } catch (InterruptedException ignored) { }
        }
    }

    void push(Screen s) { stack.push(s); }
    void pop() { stack.pop(); }
    void runBg(Runnable r) { new Thread(r, "net").start(); }

    void play(String url, String title) {
        Intent i = new Intent(this, PlayerActivity.class);
        i.putExtra("url", url); i.putExtra("title", title);
        startActivity(i);
    }

    // ---------------- screens ----------------
    abstract static class Screen { abstract void draw(double t); abstract void key(int k); }

    static final String[] SAY = { "Let's watch old TV!", "Old TV was better, right?", "Hey, let's go back in time!", "I hate new anime, blehhh", "Let's go back to the 90s!", "Eat my shorts!" };

    class Hello extends Screen {
        final String say = SAY[(int) (System.currentTimeMillis() % SAY.length)]; double start = -1;
        void draw(double t) {
            if (start < 0) start = t;
            int shown = (int) ((t - start) * 20);
            Native.hello(say, shown, shown > say.length() + 6 ? "A:Start" : null, t);
        }
        void key(int k) { if (k == Keys.A || k == Keys.START) { sfx(sSelect); stack.pop(); push(new Tabs()); } else if (k == Keys.B) { stack.pop(); } }
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
    static final String[] TABS = { "Search", "Guide", "\u0001" };
    class Tabs extends ListScreen {
        int tab = 1; Guide.Node node;
        Tabs() { node = Guide.load(MainActivity.this); fill(); }
        void fill() {
            clear();
            if (tab == 0) { row("Search videos...", "", 4); row("Search collections...", "", 4); }
            else if (tab == 1) { for (Guide.Node c : node.kids) row(c.name, c.isFolder() ? "" + c.kids.size() : "", c.isFolder() ? 4 : 0); }
            else {
                row("Music", music ? "On" : "Off", 0); row("Menu sounds", sounds ? "On" : "Off", 0);
                row("Theme", Native.themeName(theme), 0); row("Group episodes of a show", group ? "On" : "Off", 0);
                row("Swap A and B", Keys.swapAB ? "On" : "Off", 0); row("About ArchiveTV", "", 4);
            }
        }
        void draw(double t) {
            Native.bg(t); Native.headerTabs(TABS, tab, null, t); drawList(t);
            Native.footer(tab == 2 ? "A:Change  L/R:Tab  B:Quit" : "A:Open  L/R:Tab  B:Quit");
        }
        void key(int k) {
            if (move(k)) return;
            if (k == Keys.L1 || k == Keys.L2 || k == Keys.R1 || k == Keys.R2) {
                tab = (tab + ((k == Keys.L1 || k == Keys.L2) ? TABS.length - 1 : 1)) % TABS.length; sel = 0; top = 0; selT = now(); sfx(sTab); fill(); return;
            }
            if (k == Keys.SELECT) { theme = (theme + 1) % Native.themeCount(); Native.setTheme(theme); save(); say("Theme: " + Native.themeName(theme)); fill(); return; }
            if (k == Keys.B || k == Keys.MENU) { sfx(sBack); push(new Confirm()); return; }
            if (k != Keys.A && k != Keys.START) return;
            sfx(sSelect);
            if (tab == 0) push(new Keyboard(sel == 0 ? "search" : "csearch"));
            else if (tab == 1) { if (m.isEmpty()) return; Guide.Node c = node.kids.get(sel); if (c.isFolder()) push(new Folder(c)); else open(c.kind, c.arg, c.name); }
            else {
                switch (sel) {
                    case 0: music = !music; musicOn(true); if (!music && bgm != null) bgm.pause(); break;
                    case 1: sounds = !sounds; break;
                    case 2: theme = (theme + 1) % Native.themeCount(); Native.setTheme(theme); break;
                    case 3: group = !group; break;
                    case 4: Keys.swapAB = !Keys.swapAB; break;
                    default: push(new About()); return;
                }
                save(); fill();
            }
        }
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
            Native.bg(t); Native.header(title, null, t, Native.HAPPY); drawList(t); Native.footer("A:Open  X:Sort  B:Back");
        }
        void key(int k) {
            if (keyLoading(k) || move(k)) return;
            if (k == Keys.B) { sfx(sBack); pop(); return; }
            if (k == Keys.X) { sort = (sort + 1) % Archive.SORTS.length; page = 1; sel = 0; top = 0; say(Archive.SORT_NAMES[sort]); load(); return; }
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
            Native.bg(t); Native.header(title, null, t, Native.HAPPY); drawList(t); Native.footer("A:Watch  X:Sort  B:Back");
        }
        void key(int k) {
            if (keyLoading(k) || move(k)) return;
            if (k == Keys.B) { sfx(sBack); pop(); return; }
            if (k == Keys.X) { az = !az; rebuild(); sel = 0; top = 0; say(az ? "Sorted: A to Z (episode order)" : "Sorted: best quality first"); return; }
            if ((k != Keys.A && k != Keys.START) || m.isEmpty()) return;
            sfx(sSelect);
            int r = fold.rows.get(sel);
            if (r >= 0) { Archive.VFile v = files.get(r); play(Archive.downloadUrl(id, v.name), v.disp); }
            else { int g = -r - 1; push(new Episodes(this, fold.names.get(g), fold.groups.get(g))); }
        }
    }
    class Episodes extends ListScreen {
        final ItemScreen it; final String title; final List<Integer> mem;
        Episodes(ItemScreen it, String title, List<Integer> mem) { this.it = it; this.title = title; this.mem = mem; for (int i : mem) { Archive.VFile v = it.files.get(i); row(v.disp, Archive.size(v.size), v.rank == 0 ? 1 : 0); } }
        void draw(double t) { Native.bg(t); Native.header(title, null, t, Native.HAPPY); drawList(t); Native.footer("A:Watch  B:Back"); }
        void key(int k) {
            if (move(k)) return;
            if (k == Keys.B) { sfx(sBack); pop(); return; }
            if ((k == Keys.A || k == Keys.START) && !m.isEmpty()) { sfx(sSelect); Archive.VFile v = it.files.get(mem.get(sel)); play(Archive.downloadUrl(it.id, v.name), v.disp); }
        }
    }

    /** the on-screen keyboard from the Miyoo app */
    class Keyboard extends Screen {
        final String kind; StringBuilder text = new StringBuilder(); int kx = 0, ky = 0; boolean caps = false;
        Keyboard(String kind) { this.kind = kind; }
        void draw(double t) { Native.keyboard(kind.equals("search") ? "Search videos" : "Search collections", text.toString(), kx, ky, t, "SEARCH", caps ? 1 : 0); }
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
        void submit() { String q = text.toString().trim(); if (q.isEmpty()) return; sfx(sSelect); pop(); push(new Results(kind, q, q)); }
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
