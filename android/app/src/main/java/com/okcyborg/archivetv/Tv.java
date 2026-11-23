package com.okcyborg.archivetv;

import android.graphics.Bitmap;
import android.graphics.Rect;
import android.graphics.SurfaceTexture;
import android.media.MediaPlayer;
import android.net.Uri;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.Surface;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** The TV: plays a video into an OpenGL texture, so it goes through the same screen effect (and the
 *  TV's curved glass) as everything else, with the Miyoo-style time bar on top.
 *  With the buttons ("focus"): A pause, left/right 10 s, up/down 1 min, L2/R2 5 min, X full screen,
 *  Y time bar, SELECT stop, B hands the buttons back to the menus (the video keeps playing). */
final class Tv {
    final MainActivity a;
    MediaPlayer mp; SurfaceTexture st; Surface surface; final float[] stm = new float[16];
    volatile boolean frame; int vw = 4, vh = 3, buffered = 100;
    String title, key, src; volatile boolean ready, playing, focus, pinned; volatile long barUntil;
    String note; long noteUntil;
    int barW = 640, barH = 480; int[] barPx; Bitmap barBmp; volatile boolean barShow;
    final GestureDetector gest;

    Tv(MainActivity act) {
        a = act;
        gest = new GestureDetector(a, new GestureDetector.SimpleOnGestureListener() {
            @Override public boolean onDown(MotionEvent e) { return true; }
            @Override public boolean onSingleTapConfirmed(MotionEvent e) { takeFocus(); if (System.currentTimeMillis() < barUntil) barUntil = 0; else poke(); return true; }
            @Override public boolean onDoubleTap(MotionEvent e) {
                Rect r = a.tvArea(); float x = e.getX() - r.left, w = r.width();
                if (x < w / 3) seek(-10000); else if (x > w * 2 / 3) seek(10000); else toggle();
                return true;
            }
            @Override public boolean onScroll(MotionEvent e1, MotionEvent e2, float dx, float dy) {
                /* dragging along the bottom of the picture scrubs through the video */
                Rect r = a.tvArea();
                if (e1 != null && e1.getY() - r.top > r.height() * 0.8f && duration() > 0) {
                    float f = Math.max(0, Math.min(1, (e2.getX() - r.left) / (float) r.width()));
                    mp.seekTo((int) (f * duration())); poke();
                }
                return true;
            }
        });
    }

    /** the GL thread made (or remade) the video texture */
    void glReady(int texId) {
        st = new SurfaceTexture(texId);
        st.setOnFrameAvailableListener(t -> { frame = true; a.view.requestRender(); });
        Surface old = surface; surface = new Surface(st);
        if (mp != null) try { mp.setSurface(surface); } catch (Exception ignored) { }
        if (old != null) old.release();
    }
    /** GL thread: take the newest frame, if any */
    boolean latch() {
        if (st == null) return false;
        if (frame) { frame = false; try { st.updateTexImage(); st.getTransformMatrix(stm); } catch (Exception e) { return false; } }
        return true;
    }

    boolean active() { return playing; }
    int duration() { try { return mp != null && ready ? mp.getDuration() : 0; } catch (Exception e) { return 0; } }
    int position() { try { return mp != null && ready ? mp.getCurrentPosition() : 0; } catch (Exception e) { return 0; } }
    boolean isPlaying() { try { return mp != null && ready && mp.isPlaying(); } catch (Exception e) { return false; } }

    void play(String src, String title, String key) {
        saveSpot();
        release();
        this.src = src; this.title = title; this.key = key; ready = false; playing = true; focus = true; note = null; buffered = 100; frame = false;
        mp = new MediaPlayer();
        try {
            if (src.startsWith("/")) mp.setDataSource(src); else mp.setDataSource(a, Uri.parse(src));
            if (surface != null) mp.setSurface(surface);
            mp.setOnPreparedListener(m -> {
                ready = true;
                int at = key != null ? Store.resume(a, key) : 0;
                if (at > 10000 && at < m.getDuration() - 30000) { m.seekTo(at); flash("Picking up where you stopped"); }
                m.start(); poke();
            });
            mp.setOnVideoSizeChangedListener((m, w, h) -> { if (w > 0 && h > 0) { vw = w; vh = h; } });
            mp.setOnBufferingUpdateListener((m, pct) -> buffered = pct);
            mp.setOnCompletionListener(m -> { if (key != null) Store.setResume(a, key, 0); stop(); });
            mp.setOnErrorListener((m, what, extra) -> { flash("Can't play this file. Try another one."); ready = false; return true; });
            mp.prepareAsync();
        } catch (Exception e) { flash("Can't play this file. Try another one."); }
        poke();
    }
    void release() { if (mp != null) { try { mp.release(); } catch (Exception ignored) { } mp = null; } }
    void stop() {
        saveSpot();
        release(); playing = false; focus = false; ready = false;
        a.tvStopped();
    }
    void pause() { if (isPlaying()) mp.pause(); }
    void saveSpot() {
        if (!playing || !ready || key == null) return;
        int pos = position(), dur = duration();
        Store.setResume(a, key, dur > 0 && pos > dur - 30000 ? 0 : pos);
    }
    void takeFocus() { focus = true; poke(); }
    void toggle() { if (!ready) return; if (mp.isPlaying()) mp.pause(); else mp.start(); poke(); }
    void seek(int d) { int dur = duration(); if (dur <= 0) return; mp.seekTo(Math.max(0, Math.min(dur - 2000, position() + d))); poke(); }
    void poke() { barUntil = System.currentTimeMillis() + 2500; }
    void flash(String s) { note = s; noteUntil = System.currentTimeMillis() + 2500; poke(); }

    void key(int k) {
        switch (k) {
            case Keys.A: case Keys.START: toggle(); break;
            case Keys.LEFT: seek(-10000); break;
            case Keys.RIGHT: seek(10000); break;
            case Keys.DOWN: case Keys.L1: seek(-60000); break;
            case Keys.UP: case Keys.R1: seek(60000); break;
            case Keys.L2: seek(-300000); break;
            case Keys.R2: seek(300000); break;
            case Keys.X: a.setFull(!a.full); poke(); break;
            case Keys.Y: pinned = !pinned; poke(); break;
            case Keys.SELECT: stop(); break;
            case Keys.B: case Keys.MENU:
                if (a.mode == 0) { stop(); break; }   /* nowhere to browse beside it: stop */
                if (a.full) { a.setFull(false); poke(); break; }
                focus = false; flash("Browse while it plays. Tap the video for its buttons."); break;
        }
    }
    void touch(MotionEvent e) { gest.onTouchEvent(e); }

    /** the time bar, drawn by ui.c on the draw thread (under MainActivity.lock), the size of the TV area */
    void drawBar(double t, Rect area) {
        long now = System.currentTimeMillis();
        boolean playingNow = isPlaying();
        barShow = pinned || now < barUntil || !ready || !playingNow;
        if (!barShow) return;
        int bw = 640, bh = Math.max(240, (int) Math.round(640.0 * area.height() / Math.max(1, area.width()))) & ~1;
        if (barPx == null || bw != barW || bh != barH) { barW = bw; barH = bh; barPx = new int[bw * bh]; barBmp = Bitmap.createBitmap(bw, bh, Bitmap.Config.ARGB_8888); }
        String clock = new SimpleDateFormat("H:mm", Locale.US).format(new Date());
        String ttl = note != null && now < noteUntil ? note : (ready ? title : "Tuning in...");
        Native.select(2, bw, bh);
        double avail = src != null && !src.startsWith("/") && buffered > 0 && buffered < 100 ? buffered / 100.0 : -1;
        Native.osd(barPx, ttl, position() / 1000.0, duration() / 1000.0, ready && !playingNow ? 1 : 0, clock, avail, t);
        barBmp.setPixels(barPx, 0, bw, 0, 0, bw, bh);
    }
}
