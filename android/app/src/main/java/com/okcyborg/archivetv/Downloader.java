package com.okcyborg.archivetv;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;

/** The download queue: one download at a time, half-finished files (.part) pick up where they stopped. */
final class Downloader {
    static final class Job { String url, dst, disp; int state; /* 0 waiting, 1 failed, 2 paused */ volatile long got, total; }
    static final List<Job> queue = new ArrayList<>();
    static volatile Job active; static volatile boolean cancel;
    static volatile int doneSinceIdle = 0;
    static Thread worker;

    static File qfile() { return new File(Store.base, "queue.txt"); }
    static synchronized void load() {
        queue.clear();
        for (String[] r : Store.readTsv(qfile())) {
            if (r.length < 3) continue;
            Job j = new Job(); j.url = r[0]; j.dst = r[1]; j.disp = r[2]; j.state = r.length > 3 ? parse(r[3]) : 0;
            File p = new File(j.dst + ".part"); j.got = p.length();
            queue.add(j);
        }
    }
    static int parse(String s) { try { return Integer.parseInt(s.trim()); } catch (Exception e) { return 0; } }
    static synchronized void save() {
        List<String[]> rows = new ArrayList<>();
        for (Job j : queue) rows.add(new String[] { j.url, j.dst, j.disp, "" + j.state });
        Store.writeTsv(qfile(), rows);
    }
    static synchronized Job find(String dst) { for (Job j : queue) if (j.dst.equals(dst)) return j; return null; }

    /** 0 added, 1 already there, 2 already downloaded */
    static synchronized int add(String url, File dst, String disp) {
        if (dst.exists()) return 2;
        Job j = find(dst.getPath());
        if (j != null) { if (j.state != 0) { j.state = 0; save(); } kick(); return 1; }
        j = new Job(); j.url = url; j.dst = dst.getPath(); j.disp = disp; j.got = new File(j.dst + ".part").length();
        queue.add(j); save(); kick(); return 0;
    }
    static synchronized void pause(Job j, boolean pause) {
        j.state = pause ? 2 : 0;
        if (pause && active == j) cancel = true;
        save(); kick();
    }
    static synchronized void remove(Job j, boolean deletePart) {
        if (active == j) cancel = true;
        queue.remove(j); save();
        if (deletePart) { new File(j.dst + ".part").delete(); }
    }

    static synchronized void kick() {
        if (worker != null && worker.isAlive()) return;
        worker = new Thread(Downloader::run, "download"); worker.start();
    }
    static Job next() {
        synchronized (Downloader.class) {
            for (Job j : queue) if (j.state == 0 && !new File(j.dst).exists()) return j;
            return null;
        }
    }
    static void run() {
        for (;;) {
            Job j = next();
            if (j == null) { active = null; return; }
            active = j; cancel = false;
            boolean ok = fetch(j);
            synchronized (Downloader.class) {
                active = null;
                if (ok) { queue.remove(j); doneSinceIdle++; }
                else if (!cancel && j.state == 0) j.state = 1;
                save();
            }
            if (!ok && !cancel) try { Thread.sleep(1000); } catch (InterruptedException ignored) { }
        }
    }
    static boolean fetch(Job j) {
        File part = new File(j.dst + ".part");
        try {
            long have = part.length();
            HttpURLConnection c = (HttpURLConnection) new URL(j.url).openConnection();
            c.setConnectTimeout(20000); c.setReadTimeout(60000);
            c.setRequestProperty("User-Agent", "ArchiveTV-Android");
            if (have > 0) c.setRequestProperty("Range", "bytes=" + have + "-");
            int code = c.getResponseCode();
            if (code == 416) { part.renameTo(new File(j.dst)); return true; }   /* already complete */
            if (code != 200 && code != 206) return false;
            if (code == 200) have = 0;   /* server ignored the range: start over */
            long len = c.getContentLengthLong();
            j.total = len > 0 ? have + len : -1; j.got = have;
            try (InputStream in = c.getInputStream(); FileOutputStream out = new FileOutputStream(part, code == 206)) {
                byte[] b = new byte[65536]; int n;
                while ((n = in.read(b)) > 0) {
                    if (cancel) return false;
                    out.write(b, 0, n); j.got += n;
                }
            } finally { c.disconnect(); }
            if (j.total > 0 && j.got < j.total) return false;
            return part.renameTo(new File(j.dst));
        } catch (Exception e) { return false; }
    }
    static String badge() {
        Job a = active;
        if (a == null || a.total <= 0) return a == null ? null : "\u0002";
        return "\u0002" + (int) (100 * a.got / a.total) + "%";
    }
}
