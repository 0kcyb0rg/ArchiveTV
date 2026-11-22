package com.okcyborg.archivetv;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Episodes of one show folded into one row (same rules as the Miyoo app). */
final class Series {
    static int natcmp(String a, String b) {
        int i = 0, j = 0;
        while (i < a.length() && j < b.length()) {
            char x = a.charAt(i), y = b.charAt(j);
            if (Character.isDigit(x) && Character.isDigit(y)) {
                int si = i, sj = j;
                while (si < a.length() && a.charAt(si) == '0') si++;
                while (sj < b.length() && b.charAt(sj) == '0') sj++;
                int ei = si, ej = sj;
                while (ei < a.length() && Character.isDigit(a.charAt(ei))) ei++;
                while (ej < b.length() && Character.isDigit(b.charAt(ej))) ej++;
                if (ei - si != ej - sj) return (ei - si) - (ej - sj);
                int c = a.substring(si, ei).compareTo(b.substring(sj, ej));
                if (c != 0) return c;
                i = ei; j = ej; continue;
            }
            int c = Character.toLowerCase(x) - Character.toLowerCase(y);
            if (c != 0) return c;
            i++; j++;
        }
        return (a.length() - i) - (b.length() - j);
    }

    static boolean isNum(String t) { if (t.isEmpty()) return false; for (char c : t.toCharArray()) if (c < '0' || c > '9') return false; return true; }
    static final String[] EPW = { "ep", "eps", "episode", "episodes", "e", "ch", "chapter", "part", "pt", "vol", "volume", "disc", "disk", "no", "act", "reel" };

    static boolean isEpWord(String w) {
        String t = w.toLowerCase(Locale.US);
        if (isNum(t)) return t.length() <= 4;
        for (String s : EPW) if (s.equals(t)) return true;
        if (t.matches("s\\d+(e\\d*)?")) return true;
        return t.matches("e\\d+") || t.matches("ep\\d+") || t.matches("#\\d+");
    }

    /** returns {key, show} or null */
    static String[] key(String in) {
        String s = in;
        int dot = s.lastIndexOf('.');
        if (dot > 0 && s.length() - dot <= 5 && s.length() - dot >= 3 && Character.isLetter(s.charAt(dot + 1))) s = s.substring(0, dot);
        StringBuilder b = new StringBuilder(); int depth = 0;
        for (char c : s.toCharArray()) {
            if (c == '[' || c == '(' || c == '{') { depth++; continue; }
            if (c == ']' || c == ')' || c == '}') { if (depth > 0) depth--; continue; }
            if (depth > 0) continue;
            b.append("_.-~,:|".indexOf(c) >= 0 ? ' ' : c);
        }
        StringBuilder key = new StringBuilder(), show = new StringBuilder(); int words = 0; boolean found = false;
        for (String w : b.toString().trim().split("\\s+")) {
            if (w.isEmpty()) continue;
            if (words > 0 && isEpWord(w)) { found = true; break; }
            if (words == 0 && isNum(w)) return null;
            if (words > 0) { key.append(' '); show.append(' '); }
            key.append(w.toLowerCase(Locale.US)); show.append(w); words++;
        }
        if (found && key.length() >= 3) return new String[] { key.toString(), show.toString() };
        int dash = in.indexOf(" - ");
        if (dash > 0) return key(in.substring(0, dash) + " 1");
        return null;
    }

    /** rows: index >= 0 = entry, < 0 = -(group+1). groups[g] = member indexes in episode order */
    static final class Fold { List<Integer> rows = new ArrayList<>(); List<List<Integer>> groups = new ArrayList<>(); List<String> names = new ArrayList<>(); }

    static Fold fold(List<String> names, boolean on) {
        Fold f = new Fold();
        Map<String, List<Integer>> by = new HashMap<>(); Map<String, String> showOf = new HashMap<>();
        String[] keys = new String[names.size()];
        if (on) for (int i = 0; i < names.size(); i++) {
            String[] k = key(names.get(i));
            if (k == null) continue;
            keys[i] = k[0];
            by.computeIfAbsent(k[0], x -> new ArrayList<>()).add(i);
            showOf.putIfAbsent(k[0], k[1]);
        }
        Map<String, Integer> gid = new HashMap<>();
        for (int i = 0; i < names.size(); i++) {
            String k = keys[i];
            if (k == null || by.get(k).size() < 3) { f.rows.add(i); continue; }
            Integer g = gid.get(k);
            if (g == null) {
                g = f.groups.size(); gid.put(k, g);
                List<Integer> mem = new ArrayList<>(by.get(k));
                mem.sort((x, y) -> natcmp(names.get(x), names.get(y)));
                f.groups.add(mem); f.names.add(showOf.get(k));
                f.rows.add(-(g + 1));
            }
        }
        return f;
    }
}
