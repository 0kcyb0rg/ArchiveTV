package com.okcyborg.archivetv;

import android.app.Activity;
import android.graphics.Color;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.widget.VideoView;

import java.util.Locale;

/** Plays a video straight from archive.org with Android's own player (hardware decoding, seek anywhere). */
public class PlayerActivity extends Activity {
    VideoView video; TextView osd; String title;
    final Runnable hide = () -> osd.setVisibility(View.GONE);

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        FrameLayout root = new FrameLayout(this); root.setBackgroundColor(Color.BLACK);
        video = new VideoView(this);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT, Gravity.CENTER);
        root.addView(video, lp);
        osd = new TextView(this); osd.setTextColor(Color.WHITE); osd.setBackgroundColor(0xAA20103A); osd.setPadding(32, 16, 32, 16); osd.setTextSize(18);
        root.addView(osd, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL));
        setContentView(root);
        title = getIntent().getStringExtra("title");
        video.setVideoURI(Uri.parse(getIntent().getStringExtra("url")));
        video.setOnPreparedListener(mp -> { video.start(); show("Playing"); });
        video.setOnCompletionListener(mp -> finish());
        video.setOnErrorListener((mp, what, extra) -> { show("Can't play this file (" + what + "). Try another one."); return true; });
        show("Tuning in...");
    }

    @Override protected void onResume() {
        super.onResume();
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    static String clock(int ms) { int s = Math.max(0, ms / 1000); return s >= 3600 ? String.format(Locale.US, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60) : String.format(Locale.US, "%d:%02d", s / 60, s % 60); }
    void show(String what) {
        String pos = video.getDuration() > 0 ? "  " + clock(video.getCurrentPosition()) + " / " + clock(video.getDuration()) : "";
        osd.setText(title + "\n" + what + pos);
        osd.setVisibility(View.VISIBLE);
        osd.removeCallbacks(hide); osd.postDelayed(hide, 2500);
    }
    void seek(int deltaMs) {
        int d = video.getDuration(); if (d <= 0) return;
        int to = Math.max(0, Math.min(d - 2000, video.getCurrentPosition() + deltaMs));
        video.seekTo(to); show(deltaMs > 0 ? "+" + deltaMs / 1000 + " s" : deltaMs / 1000 + " s");
    }

    @Override public boolean onKeyDown(int code, KeyEvent e) {
        switch (Keys.map(code)) {
            case Keys.A: case Keys.START: if (video.isPlaying()) { video.pause(); show("Paused"); } else { video.start(); show("Playing"); } return true;
            case Keys.LEFT: seek(-10000); return true;
            case Keys.RIGHT: seek(10000); return true;
            case Keys.DOWN: case Keys.L1: seek(-60000); return true;
            case Keys.UP: case Keys.R1: seek(60000); return true;
            case Keys.L2: seek(-300000); return true;
            case Keys.R2: seek(300000); return true;
            case Keys.SELECT: video.seekTo(0); show("Start over"); return true;
            case Keys.Y: show(video.isPlaying() ? "Playing" : "Paused"); return true;
            case Keys.B: case Keys.MENU: finish(); return true;
        }
        return super.onKeyDown(code, e);
    }
}
