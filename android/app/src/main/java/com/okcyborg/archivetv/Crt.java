package com.okcyborg.archivetv;

import android.graphics.Bitmap;
import android.graphics.Rect;
import android.opengl.GLES11Ext;
import android.opengl.GLES20;
import android.opengl.GLUtils;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

/** Draws the pixel pictures with OpenGL through a screen effect. The looks are the ones from the Tsotchke Horror
 *  game (src/main.c): its scanlines (brightness-dependent beams, a touch of colour fringing, vignette, grain) and its
 *  SHARP and SOFT CRTs (written after the looks of crt-hyllian and crt-easymode), here in GLSL ES for phones.
 *  "res" is the picture in UI pixels (each one a beam spot); "curve" bends it like old glass. */
final class Crt {
    static final String[] NAMES = { "Off", "Scanlines", "Sharp CRT", "Soft CRT" };
    static final String VS =
        "attribute vec2 pos; attribute vec2 tc; varying vec2 uv;\n" +
        "void main() { uv = tc; gl_Position = vec4(pos, 0.0, 1.0); }\n";
    static final String HEAD =
        "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n" +
        "SAMPLER uniform vec2 res; uniform float time; uniform float curveOn; uniform float outA; varying vec2 uv;\n" +
        /* the glass (newpixie's curve) */
        "vec2 curve(vec2 p) { p = p - 0.5; p *= vec2(0.925, 1.095); p *= 2.0; p.x *= 1.0 + pow(abs(p.y) / 4.0, 2.0); p.y *= 1.0 + pow(abs(p.x) / 3.0, 2.0); p /= 2.0; p += 0.5; return p * 0.92 + 0.04; }\n" +
        "vec2 warp() { return curveOn > 0.5 ? curve(uv) : uv; }\n" +
        "bool outside(vec2 p) { return p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0; }\n" +
        "float alphaAt(vec2 p) { return samp(p).a; }\n" +
        "float grain(vec2 p) { return fract(sin(dot(floor(p * res) + floor(time * 24.0) * vec2(17.0, 31.0), vec2(12.9898, 78.233))) * 43758.5453); }\n";
    static final String PLAIN = HEAD +
        "void main() { vec2 p = warp(); if (outside(p)) { gl_FragColor = vec4(0.0, 0.0, 0.0, outA); return; }\n" +
        "  gl_FragColor = samp(p); }\n";
    /* Tsotchke Horror's SCAN_FS: one beam per UI pixel row, wider where it's bright */
    static final String SCAN = HEAD +
        "void main() { vec2 p = warp(); if (outside(p)) { gl_FragColor = vec4(0.0, 0.0, 0.0, outA); return; }\n" +
        "  vec2 q = p - 0.5; vec2 ca = 0.3 * (q * 0.014 + q / max(length(q), 1e-4) * 0.0012);\n" +
        "  vec3 c = vec3(samp(clamp(p + ca, 0.001, 0.999)).r, samp(p).g, samp(clamp(p - ca, 0.001, 0.999)).b);\n" +
        "  c *= 1.0 - dot(q, q) * 1.2;\n" +
        "  c += (grain(p) - 0.5) * 0.02;\n" +
        "  float d = fract(p.y * res.y) - 0.5; float l = clamp(dot(c, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);\n" +
        "  float w = mix(0.18, 0.40, sqrt(l)); float beam = exp(-d * d / (2.0 * w * w));\n" +
        "  gl_FragColor = vec4(max(c, 0.0) * (1.0 - 0.55 * (1.0 - beam)), alphaAt(p)); }\n";
    static final String COMMON = HEAD +
        "vec3 lin(vec3 c) { return pow(max(c, 0.0), vec3(2.2)); } vec3 srgb(vec3 c) { return pow(max(c, 0.0), vec3(1.0 / 2.2)); }\n" +
        "vec2 gCa;\n" +
        "vec3 T(vec2 p) { p = clamp(p, 0.5 / res, 1.0 - 0.5 / res); return lin(vec3(samp(clamp(p + gCa, 0.001, 0.999)).r, samp(p).g, samp(clamp(p - gCa, 0.001, 0.999)).b)); }\n";
    /* Tsotchke Horror's SHARP_FS: crisp across a row, round beams that grow with brightness, a fine aperture grille */
    static final String SHARP = COMMON +
        "vec3 Row(vec2 p, float dy) { vec2 px = p * res - 0.5; float fx = fract(px.x); vec2 b = (floor(px) + 0.5 + vec2(0.0, dy)) / res;\n" +
        "  float k = smoothstep(0.0, 1.0, smoothstep(0.0, 1.0, fx)); vec3 a = T(b), c = T(b + vec2(1.0 / res.x, 0.0)); return mix(a, c, k); }\n" +
        "float Beam(float d, vec3 c) { float l = max(max(c.r, c.g), c.b), w = mix(0.28, 0.5, l); return exp(-0.5 * (d * d) / (w * w)); }\n" +
        "void main() { vec2 p = warp(); if (outside(p)) { gl_FragColor = vec4(0.0, 0.0, 0.0, outA); return; }\n" +
        "  vec2 q = p - 0.5; gCa = 0.3 * (q * 0.014 + q / max(length(q), 1e-4) * 0.0012);\n" +
        "  float fy = fract(p.y * res.y - 0.5); vec3 c0 = Row(p, 0.0), c1 = Row(p, 1.0);\n" +
        "  vec3 c = c0 * Beam(fy, c0) + c1 * Beam(1.0 - fy, c1);\n" +
        "  float m = mod(floor(gl_FragCoord.x), 3.0); vec3 mk = m < 1.0 ? vec3(1.0, 0.82, 0.82) : m < 2.0 ? vec3(0.82, 1.0, 0.82) : vec3(0.82, 0.82, 1.0);\n" +
        "  c *= mk * 1.42; c = srgb(c); c *= 1.0 - dot(q, q) * 0.9;\n" +
        "  gl_FragColor = vec4(max(c + (grain(p) - 0.5) * 0.018, 0.0), alphaAt(p)); }\n";
    /* Tsotchke Horror's SOFT_FS: a gentle cubic across the row, soft beams, a dot mask and a little glow */
    static final String SOFT = COMMON +
        "float cw(float t) { float a = -0.5; t = abs(t); return t < 1.0 ? (a + 2.0) * t * t * t - (a + 3.0) * t * t + 1.0 : t < 2.0 ? a * t * t * t - 5.0 * a * t * t + 8.0 * a * t - 4.0 * a : 0.0; }\n" +
        "vec3 Row(vec2 p, float dy) { vec2 px = p * res - 0.5; float fx = fract(px.x); vec2 b = (floor(px) + 0.5 + vec2(0.0, dy)) / res; float ox = 1.0 / res.x;\n" +
        "  float w0 = cw(1.0 + fx), w1 = cw(fx), w2 = cw(1.0 - fx), w3 = cw(2.0 - fx);\n" +
        "  vec3 c = T(b - vec2(ox, 0.0)) * w0 + T(b) * w1 + T(b + vec2(ox, 0.0)) * w2 + T(b + vec2(2.0 * ox, 0.0)) * w3; return max(c / (w0 + w1 + w2 + w3), 0.0); }\n" +
        "void main() { vec2 p = warp(); if (outside(p)) { gl_FragColor = vec4(0.0, 0.0, 0.0, outA); return; }\n" +
        "  vec2 q = p - 0.5; gCa = 0.3 * (q * 0.014 + q / max(length(q), 1e-4) * 0.0012);\n" +
        "  float fy = fract(p.y * res.y - 0.5); vec3 c0 = Row(p, 0.0), c1 = Row(p, 1.0);\n" +
        "  vec3 c = c0 * exp(-fy * fy * 5.5) + c1 * exp(-(1.0 - fy) * (1.0 - fy) * 5.5);\n" +
        "  vec3 glo = (T(p + vec2(1.5, 0.0) / res) + T(p - vec2(1.5, 0.0) / res) + T(p + vec2(0.0, 1.5) / res) + T(p - vec2(0.0, 1.5) / res)) * 0.25;\n" +
        "  c = c * 1.35 + glo * 0.12;\n" +
        "  vec2 mp = floor(gl_FragCoord.xy); float dm = mod(mp.x + mod(mp.y, 2.0) * 1.5, 3.0); vec3 mk = dm < 1.0 ? vec3(1.0, 0.75, 0.75) : dm < 2.0 ? vec3(0.75, 1.0, 0.75) : vec3(0.75, 0.75, 1.0);\n" +
        "  c *= mix(vec3(1.0), mk, 0.6) * 1.12; c = srgb(c); c *= 1.0 - dot(q, q) * 1.1;\n" +
        "  gl_FragColor = vec4(max(c + (grain(p) - 0.5) * 0.02, 0.0), alphaAt(p)); }\n";

    /* programs: [0..3] the looks for pictures (sampler2D), [4..7] the same for the video (an Android camera/video texture) */
    final int[] prog = new int[8], aPos = new int[8], aTc = new int[8], uRes = new int[8], uTime = new int[8], uCurve = new int[8], uOutA = new int[8], uStm = new int[8];
    final int[] tex = new int[3];
    final FloatBuffer vb = ByteBuffer.allocateDirect(16 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer();
    final long t0 = System.nanoTime();
    static final float[] ID = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    static int shader(int type, String src) { int s = GLES20.glCreateShader(type); GLES20.glShaderSource(s, src); GLES20.glCompileShader(s); return s; }
    static String variant(String fs, boolean video) {
        if (!video) return fs.replace("SAMPLER", "uniform sampler2D tex; vec4 samp(vec2 p) { return texture2D(tex, p); }\n");
        return "#extension GL_OES_EGL_image_external : require\n" + fs.replace("SAMPLER",
            "uniform samplerExternalOES tex; uniform mat4 stmF; vec4 samp(vec2 p) { return vec4(texture2D(tex, (stmF * vec4(p.x, 1.0 - p.y, 0.0, 1.0)).xy).rgb, 1.0); }\n");
    }
    void init() {
        String[] fs = { PLAIN, SCAN, SHARP, SOFT };
        for (int i = 0; i < 8; i++) {
            int p = GLES20.glCreateProgram();
            GLES20.glAttachShader(p, shader(GLES20.GL_VERTEX_SHADER, VS));
            GLES20.glAttachShader(p, shader(GLES20.GL_FRAGMENT_SHADER, variant(fs[i % 4], i >= 4)));
            GLES20.glLinkProgram(p);
            int[] ok = new int[1]; GLES20.glGetProgramiv(p, GLES20.GL_LINK_STATUS, ok, 0);
            if (ok[0] == 0 && i % 4 > 0) p = prog[i - i % 4];   /* a phone that can't run a look falls back to plain */
            prog[i] = p;
            aPos[i] = GLES20.glGetAttribLocation(p, "pos"); aTc[i] = GLES20.glGetAttribLocation(p, "tc");
            uRes[i] = GLES20.glGetUniformLocation(p, "res"); uTime[i] = GLES20.glGetUniformLocation(p, "time"); uCurve[i] = GLES20.glGetUniformLocation(p, "curveOn");
            uOutA[i] = GLES20.glGetUniformLocation(p, "outA"); uStm[i] = GLES20.glGetUniformLocation(p, "stmF");
        }
        GLES20.glGenTextures(3, tex, 0);
        for (int k = 0; k < 3; k++) {
            int target = k == 2 ? GLES11Ext.GL_TEXTURE_EXTERNAL_OES : GLES20.GL_TEXTURE_2D;
            GLES20.glBindTexture(target, tex[k]);
            int filt = k == 2 ? GLES20.GL_LINEAR : GLES20.GL_NEAREST;
            GLES20.glTexParameteri(target, GLES20.GL_TEXTURE_MIN_FILTER, filt);
            GLES20.glTexParameteri(target, GLES20.GL_TEXTURE_MAG_FILTER, filt);
            GLES20.glTexParameteri(target, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
            GLES20.glTexParameteri(target, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
        }
        GLES20.glEnable(GLES20.GL_BLEND);
        GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA);
    }
    int videoTexture() { return tex[2]; }

    void quad(int i, Rect r, int sw, int sh, float rw, float rh, boolean curve, float outA) {
        float l = 2f * r.left / sw - 1, rr = 2f * r.right / sw - 1, t = 1 - 2f * r.top / sh, bo = 1 - 2f * r.bottom / sh;
        vb.clear(); vb.put(new float[] { l, bo, 0, 1, rr, bo, 1, 1, l, t, 0, 0, rr, t, 1, 0 });
        vb.position(0); GLES20.glVertexAttribPointer(aPos[i], 2, GLES20.GL_FLOAT, false, 16, vb); GLES20.glEnableVertexAttribArray(aPos[i]);
        vb.position(2); GLES20.glVertexAttribPointer(aTc[i], 2, GLES20.GL_FLOAT, false, 16, vb); GLES20.glEnableVertexAttribArray(aTc[i]);
        GLES20.glUniform2f(uRes[i], rw, rh);
        GLES20.glUniform1f(uTime[i], (System.nanoTime() - t0) / 1e9f);
        GLES20.glUniform1f(uCurve[i], curve ? 1f : 0f);
        GLES20.glUniform1f(uOutA[i], outA);
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
    }
    /** the video frame (from a SurfaceTexture) into r: lines = how many scanlines across its height */
    void drawVideo(Rect r, int sw, int sh, int mode, boolean curve, float[] stm, float lines) {
        int i = 4 + Math.max(0, Math.min(3, mode));
        GLES20.glUseProgram(prog[i]);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, tex[2]);
        GLES20.glUniformMatrix4fv(uStm[i], 1, false, stm, 0);
        quad(i, r, sw, sh, lines * r.width() / (float) Math.max(1, r.height()), lines, curve, 1f);
    }
    /** draws picture b (texture slot 0 or 1) into rectangle r of a sw x sh screen with look `mode`;
     *  see-through pictures (the time bar) blend over what's there */
    void draw(int slot, Bitmap b, Rect r, int sw, int sh, int mode, boolean curve, boolean overlay) {
        int i = Math.max(0, Math.min(3, mode));
        GLES20.glUseProgram(prog[i]);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex[slot]);
        GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, b, 0);
        quad(i, r, sw, sh, b.getWidth() / 2f, b.getHeight() / 2f, curve, overlay ? 0f : 1f);   /* one beam per UI pixel (2 picture pixels) */
    }
}
