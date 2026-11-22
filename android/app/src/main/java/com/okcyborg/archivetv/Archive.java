package com.okcyborg.archivetv;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** archive.org searches and item details (the same requests the Miyoo app makes). */
final class Archive {
    static final String[] SORTS = { "downloads desc", "addeddate desc", "addeddate asc", "titleSorter asc" };
    static final String[] SORT_NAMES = { "Sorted: most popular", "Sorted: newest", "Sorted: oldest", "Sorted: A to Z" };

    static final class Item { String id, title; boolean coll; }
    static final class VFile { String name, disp; long size; int rank; }
    static final class Page { List<Item> items = new ArrayList<>(); int found; }

    static String get(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(20000); c.setReadTimeout(30000);
        c.setRequestProperty("User-Agent", "ArchiveTV-Android");
        try (InputStream in = c.getInputStream()) {
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            byte[] b = new byte[32768]; int n;
            while ((n = in.read(b)) > 0) o.write(b, 0, n);
            return o.toString("UTF-8");
        } finally { c.disconnect(); }
    }

    /** kind: "collection", "search" (videos) or "csearch" (collections) */
    static Page search(String kind, String arg, int page, int sort) throws Exception {
        String q;
        if (kind.equals("collection")) q = "collection:(" + arg + ") AND mediatype:(movies OR collection)";
        else if (kind.equals("csearch")) q = "(" + arg + ") AND mediatype:(collection)";
        else q = "(" + arg + ") AND mediatype:(movies)";
        String url = "https://archive.org/advancedsearch.php?q=" + URLEncoder.encode(q, "UTF-8")
                + "&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=mediatype&rows=100&page=" + page
                + "&sort%5B%5D=" + URLEncoder.encode(SORTS[sort % SORTS.length], "UTF-8") + "&output=json";
        JSONObject r = new JSONObject(get(url)).getJSONObject("response");
        Page p = new Page();
        p.found = r.optInt("numFound");
        JSONArray docs = r.getJSONArray("docs");
        for (int i = 0; i < docs.length(); i++) {
            JSONObject d = docs.getJSONObject(i);
            Item it = new Item();
            it.id = d.optString("identifier");
            Object t = d.opt("title");
            it.title = t instanceof JSONArray ? ((JSONArray) t).optString(0) : d.optString("title", it.id);
            it.coll = "collection".equals(d.optString("mediatype"));
            p.items.add(it);
        }
        return p;
    }

    static boolean isVideo(String n) {
        String l = n.toLowerCase();
        return l.endsWith(".mp4") || l.endsWith(".m4v") || l.endsWith(".ogv") || l.endsWith(".mkv") || l.endsWith(".webm")
                || l.endsWith(".avi") || l.endsWith(".mpeg") || l.endsWith(".mpg") || l.endsWith(".mov") || l.endsWith(".flv");
    }

    /** an item's video files, best first (mp4s, then smaller ones) */
    static List<VFile> files(String id, String[] titleOut) throws Exception {
        JSONObject m = new JSONObject(get("https://archive.org/metadata/" + URLEncoder.encode(id, "UTF-8")));
        JSONObject md = m.optJSONObject("metadata");
        if (md != null && titleOut != null) { Object t = md.opt("title"); titleOut[0] = t instanceof JSONArray ? ((JSONArray) t).optString(0) : md.optString("title", id); }
        JSONArray fa = m.optJSONArray("files");
        List<VFile> out = new ArrayList<>();
        if (fa == null) return out;
        for (int i = 0; i < fa.length(); i++) {
            JSONObject f = fa.getJSONObject(i);
            String n = f.optString("name");
            if (!isVideo(n)) continue;
            VFile v = new VFile();
            v.name = n;
            int s = n.lastIndexOf('/'); v.disp = s >= 0 ? n.substring(s + 1) : n;
            v.size = f.optLong("size", 0);
            String l = n.toLowerCase();
            v.rank = l.endsWith(".mp4") || l.endsWith(".m4v") ? 0 : l.endsWith(".webm") || l.endsWith(".mkv") ? 1 : 2;
            out.add(v);
        }
        Collections.sort(out, (a, b) -> a.rank != b.rank ? a.rank - b.rank : Series.natcmp(a.disp, b.disp));
        return out;
    }

    static String downloadUrl(String id, String name) {
        StringBuilder b = new StringBuilder("https://archive.org/download/").append(id).append('/');
        for (String part : name.split("/", -1)) {
            if (b.charAt(b.length() - 1) != '/') b.append('/');
            try { b.append(URLEncoder.encode(part, "UTF-8").replace("+", "%20")); } catch (Exception e) { b.append(part); }
        }
        return b.toString();
    }

    static String size(long n) {
        if (n <= 0) return "";
        if (n >= 1L << 30) return String.format(java.util.Locale.US, "%.1fG", n / (double) (1L << 30));
        if (n >= 10L << 20) return (n >> 20) + "M";
        return String.format(java.util.Locale.US, "%.1fM", n / (double) (1L << 20));
    }
}
