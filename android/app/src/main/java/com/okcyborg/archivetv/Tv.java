package com.okcyborg.archivetv;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.net.Uri;
import android.view.GestureDetector;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.VideoView;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** The TV panel: plays a video (Android's own player) under the Miyoo-style time bar.
 *  With the buttons ("focus"): A pause, left/right 10 s, up/down 1 min, L2/R2 5 min, X full screen,
 *  Y time bar, SELECT stop, B hands the buttons back to the menus (the video keeps playing). */
final class Tv {
    final MainActivity a;
    final FrameLayout box; final VideoView video; final Bar bar;
    String title, key, src; boolean ready, playing, focus, pinned; long barUntil;
    String note; long noteUntil;
    int barW = 640, barH = 480; int[] barPx; Bitmap barBmp; final Object barLock = new Object();
    final GestureDetector gest;

    Tv(MainActivity act) {
        a = act;
        box = new FrameLayout(a); box.setBackgroundColor(Color.BLACK);
        video = new VideoView(a);
        box.addView(video, new FrameLayout.LayoutParams(-1, -1, Gravity.CENTER));
        video.setZOrderMediaOverlay(true);
        bar = new Bar();
        box.setVisibility(View.GONE); bar.setVisibility(View.GONE);
        video.setOnPreparedListener(mp -> {
            ready = true;
            int at = key != null ? Store.resume(a, key) : 0;
            if (at > 10000 && at < video.getDuration() - 30000) { video.seekTo(at); flash("Picking up where you stopped"); }
            video.start(); poke();
        });
        video.setOnCompletionListener(mp -> { if (key != null) Store.setResume(a, key, 0); stop(); });
        video.setOnErrorListener((mp, what, extra) -> { flash("Can't play this file. Try another one."); ready = true; return true; });
        gest = new GestureDetector(a, new GestureDetector.SimpleOnGestureListener() {
            @Override public boolean onDown(MotionEvent e) { return true; }
            @Override public boolean onSingleTapConfirmed(MotionEvent e) { takeFocus(); if (System.currentTimeMillis() < barUntil) barUntil = 0; else poke(); return true; }
            @Override public boolean onDoubleTap(MotionEvent e) {
                float x = e.getX() - box.getLeft(), w = box.getWidth();
                if (x < w / 3) seek(-10000); else if (x > w * 2 / 3) seek(10000); else toggle();
                return true;
            }
            @Override public boolean onScroll(MotionEvent e1, MotionEvent e2, float dx, float dy) {
                /* dragging along the bottom of the picture scrubs through the video */
                if (e1 != null && e1.getY() - box.getTop() > box.getHeight() * 0.8f && video.getDuration() > 0) {
                    float f = Math.max(0, Math.min(1, (e2.getX() - box.getLeft()) / (float) box.getWidth()));
                    video.seekTo((int) (f * video.getDuration())); poke();
                }
                return true;
            }
        });
    }

    boolean active() { return playing; }

    void play(String src, String title, String key) {
        saveSpot();
        this.src = src; this.title = title; this.key = key; ready = false; playing = true; focus = true; note = null;
        if (src.startsWith("/")) video.setVideoPath(src); else video.setVideoURI(Uri.parse(src));
        poke();
    }
    void stop() {
        saveSpot();
        video.stopPlayback(); playing = false; focus = false; ready = false;
        a.tvStopped();
    }
    void pause() { if (playing && video.isPlaying()) video.pause(); }
    void saveSpot() {
        if (!playing || !ready || key == null) return;
        int pos = video.getCurrentPosition(), dur = video.getDuration();
        Store.setResume(a, key, dur > 0 && pos > dur - 30000 ? 0 : pos);
    }
    void takeFocus() { focus = true; poke(); }
    void toggle() { if (video.isPlaying()) video.pause(); else video.start(); poke(); }
    void seek(int d) { int dur = video.getDuration(); if (dur <= 0) return; video.seekTo(Math.max(0, Math.min(dur - 2000, video.getCurrentPosition() + d))); poke(); }
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

    /** the time bar, drawn by ui.c on the draw thread (under MainActivity.lock) */
    void drawBar(double t) {
        long now = System.currentTimeMillis();
        boolean show = pinned || now < barUntil || !ready || (ready && !video.isPlaying());
        int w = Math.max(2, bar.getWidth()), h = Math.max(2, bar.getHeight());
        int bw = 640, bh = Math.max(240, (int) Math.round(640.0 * h / w)) & ~1;
        synchronized (barLock) {
            if (barPx == null || bw != barW || bh != barH) { barW = bw; barH = bh; barPx = new int[bw * bh]; barBmp = Bitmap.createBitmap(bw, bh, Bitmap.Config.ARGB_8888); }
            if (show) {
                String clock = new SimpleDateFormat("H:mm", Locale.US).format(new Date());
                String ttl = note != null && now < noteUntil ? note : (ready ? title : "Tuning in...");
                if (focus && a.mode != 0 && !a.full && ready && video.isPlaying() && note == null) ttl = title;
                Native.select(2, bw, bh);
                double avail = -1; int b = video.getBufferPercentage();
                if (src != null && !src.startsWith("/") && b > 0 && b < 100) avail = b / 100.0;
                Native.osd(barPx, ttl, video.getCurrentPosition() / 1000.0, video.getDuration() / 1000.0, ready && !video.isPlaying() ? 1 : 0, clock, avail, t);
                barBmp.setPixels(barPx, 0, bw, 0, 0, bw, bh);
            }
            bar.show = show;
        }
        bar.postInvalidate();
    }

    final class Bar extends View {
        volatile boolean show; final Paint p = new Paint(); final Rect d = new Rect();
        Bar() { super(a); p.setFilterBitmap(false); }
        @Override protected void onDraw(Canvas c) {
            synchronized (barLock) {
                if (!show || barBmp == null) return;
                d.set(0, 0, getWidth(), getHeight());
                c.drawBitmap(barBmp, null, d, p);
            }
        }
    }
}
