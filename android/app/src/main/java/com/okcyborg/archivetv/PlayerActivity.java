package com.okcyborg.archivetv;

import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.VideoView;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** Plays a video (from archive.org or the device) with Android's own player, under the Miyoo-style time bar. */
public class PlayerActivity extends Activity {
    VideoView video; Osd osd; String title, key;
    long osdUntil = 0; boolean ready = false; String note = null; long noteUntil = 0;
    final long t0 = System.nanoTime();

    /** draws ui.c's time bar over the video */
    class Osd extends View {
        final int W = Math.max(2, Native.width()), H = Math.max(2, Native.height());
        final int[] px = new int[W * H]; final Bitmap bmp = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888);
        final Paint p = new Paint(); final Rect dst = new Rect();
        Osd(Context c) { super(c); p.setFilterBitmap(false); }
        @Override protected void onDraw(Canvas c) {
            long now = System.currentTimeMillis();
            boolean show = now < osdUntil || !video.isPlaying() || !ready;
            if (show) {
                double t = (System.nanoTime() - t0) / 1e9;
                String clock = new SimpleDateFormat("H:mm", Locale.US).format(new Date());
                String ttl = note != null && now < noteUntil ? note : (ready ? title : "Tuning in...");
                Native.osd(px, ttl, video.getCurrentPosition() / 1000.0, video.getDuration() / 1000.0, !video.isPlaying() && ready ? 1 : 0, clock, avail(), t);
                bmp.setPixels(px, 0, W, 0, 0, W, H);
                double sc = Math.min(getWidth() / (double) W, getHeight() / (double) H);
                int dw = (int) (W * sc), dh = (int) (H * sc);
                dst.set((getWidth() - dw) / 2, (getHeight() - dh) / 2, (getWidth() + dw) / 2, (getHeight() + dh) / 2);
                c.drawBitmap(bmp, null, dst, p);
            }
            postInvalidateDelayed(show ? 100 : 300);
        }
    }
    double avail() { int b = video.getBufferPercentage(); return b > 0 && b < 100 && !isLocal() ? b / 100.0 : -1; }
    boolean isLocal() { String u = getIntent().getStringExtra("url"); return u != null && u.startsWith("/"); }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        FrameLayout root = new FrameLayout(this); root.setBackgroundColor(Color.BLACK);
        video = new VideoView(this);
        root.addView(video, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT, Gravity.CENTER));
        osd = new Osd(this);
        root.addView(osd, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        setContentView(root);
        title = getIntent().getStringExtra("title"); key = getIntent().getStringExtra("key");
        String src = getIntent().getStringExtra("url");
        if (src.startsWith("/")) video.setVideoPath(src); else video.setVideoURI(Uri.parse(src));
        video.setOnPreparedListener(mp -> {
            ready = true;
            int at = key != null ? Store.resume(this, key) : 0;
            if (at > 10000 && at < video.getDuration() - 30000) { video.seekTo(at); flash("Picking up where you stopped"); }
            video.start(); poke();
        });
        video.setOnCompletionListener(mp -> { if (key != null) Store.setResume(this, key, 0); finish(); });
        video.setOnErrorListener((mp, what, extra) -> { flash("Can't play this file. Try another one."); ready = true; return true; });
    }
    void poke() { osdUntil = System.currentTimeMillis() + 2500; }
    void flash(String s) { note = s; noteUntil = System.currentTimeMillis() + 2500; poke(); }

    @Override protected void onResume() {
        super.onResume();
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
    @Override protected void onPause() {
        super.onPause();
        if (ready && key != null) {
            int pos = video.getCurrentPosition(), dur = video.getDuration();
            Store.setResume(this, key, dur > 0 && pos > dur - 30000 ? 0 : pos);
        }
        video.pause();
    }

    void seek(int deltaMs) {
        int d = video.getDuration(); if (d <= 0) return;
        video.seekTo(Math.max(0, Math.min(d - 2000, video.getCurrentPosition() + deltaMs))); poke();
    }

    @Override public boolean onKeyDown(int code, KeyEvent e) {
        switch (Keys.map(code)) {
            case Keys.A: case Keys.START: if (video.isPlaying()) video.pause(); else video.start(); poke(); return true;
            case Keys.LEFT: seek(-10000); return true;
            case Keys.RIGHT: seek(10000); return true;
            case Keys.DOWN: case Keys.L1: seek(-60000); return true;
            case Keys.UP: case Keys.R1: seek(60000); return true;
            case Keys.L2: seek(-300000); return true;
            case Keys.R2: seek(300000); return true;
            case Keys.SELECT: video.seekTo(0); flash("Start over"); return true;
            case Keys.Y: if (System.currentTimeMillis() < osdUntil) osdUntil = 0; else osdUntil = System.currentTimeMillis() + 60000; return true;
            case Keys.B: case Keys.MENU: finish(); return true;
        }
        return super.onKeyDown(code, e);
    }
}
