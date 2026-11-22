package com.okcyborg.archivetv;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Everything kept on the device, as small text files in Android/data/com.okcyborg.archivetv/files
 *  (the same kinds of files as the Miyoo app: videos/, playlists/, queue.txt, saved.txt). */
final class Store {
    static File base, videos, playlists;

    static void init(Context c) {
        base = c.getExternalFilesDir(null);
        if (base == null) base = c.getFilesDir();
        videos = new File(base, "videos"); videos.mkdirs();
        playlists = new File(base, "playlists"); playlists.mkdirs();
    }

    static List<String[]> readTsv(File f) {
        List<String[]> out = new ArrayList<>();
        if (!f.exists()) return out;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            String l; while ((l = r.readLine()) != null) { if (l.trim().isEmpty() || l.startsWith("#")) continue; out.add(l.split("\t", -1)); }
        } catch (Exception ignored) { }
        return out;
    }
    static void writeTsv(File f, List<String[]> rows) {
        File tmp = new File(f.getPath() + ".new");
        try (Writer w = new OutputStreamWriter(new FileOutputStream(tmp), StandardCharsets.UTF_8)) {
            for (String[] r : rows) { w.write(String.join("\t", clean(r))); w.write('\n'); }
        } catch (Exception e) { return; }
        tmp.renameTo(f);
    }
    static String[] clean(String[] r) { String[] o = new String[r.length]; for (int i = 0; i < r.length; i++) o[i] = r[i] == null ? "" : r[i].replace('\t', ' ').replace('\n', ' '); return o; }

    // --- Saved: kind, arg, title (kind = collection, item, search, csearch) ---
    static File savedFile() { return new File(base, "saved.txt"); }
    static List<String[]> saved() { return readTsv(savedFile()); }
    /** newest on top; returns false if it was already saved */
    static boolean save(String kind, String arg, String title) {
        List<String[]> s = saved();
        for (String[] r : s) if (r.length >= 2 && r[0].equals(kind) && r[1].equals(arg)) return false;
        s.add(0, new String[] { kind, arg, title });
        writeTsv(savedFile(), s); return true;
    }
    static void unsave(int i) { List<String[]> s = saved(); if (i >= 0 && i < s.size()) { s.remove(i); writeTsv(savedFile(), s); } }

    // --- playlists: playlists/<name>.txt, one "id, file name, title" per line ---
    static List<String> playlistNames() {
        String[] n = playlists.list(); List<String> out = new ArrayList<>();
        if (n != null) { Arrays.sort(n, String.CASE_INSENSITIVE_ORDER); for (String s : n) if (s.endsWith(".txt")) out.add(s.substring(0, s.length() - 4)); }
        return out;
    }
    static File plFile(String name) { return new File(playlists, name.replaceAll("[/\\\\:*?\"<>|]", "_") + ".txt"); }
    static List<String[]> playlist(String name) { return readTsv(plFile(name)); }
    static boolean plAdd(String name, String id, String file, String title) {
        List<String[]> p = playlist(name);
        for (String[] r : p) if (r.length >= 2 && r[0].equals(id) && r[1].equals(file)) return false;
        p.add(new String[] { id, file, title }); writeTsv(plFile(name), p); return true;
    }
    static void plRemove(String name, int i) { List<String[]> p = playlist(name); if (i >= 0 && i < p.size()) { p.remove(i); writeTsv(plFile(name), p); } }

    /** where a video of an item lives once downloaded: videos/<id>__<file name> */
    static File local(String id, String name) {
        int s = name.lastIndexOf('/');
        return new File(videos, id + "__" + (s >= 0 ? name.substring(s + 1) : name).replaceAll("[/\\\\:*?\"<>|]", "_"));
    }
    static String disp(File f) { String n = f.getName(); int i = n.indexOf("__"); return i >= 0 ? n.substring(i + 2) : n; }

    // --- where you stopped watching (seconds), by video ---
    static int resume(Context c, String key) { return c.getSharedPreferences("resume", Context.MODE_PRIVATE).getInt(key, 0); }
    static void setResume(Context c, String key, int ms) { c.getSharedPreferences("resume", Context.MODE_PRIVATE).edit().putInt(key, ms).apply(); }
}
