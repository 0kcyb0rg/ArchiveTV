package com.okcyborg.archivetv;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.ShortBuffer;

/** "Select a Level", looped seamlessly between the same points as on the Miyoo (music.txt). */
final class MusicLoop {
    static final int LOOP_START = 3211610, LOOP_END = 6482507;
    short[] pcm; int frames; volatile boolean playing, quit; float vol = 0.45f;
    Thread th; AudioTrack track;

    void start(Context c) {
        if (th != null) { playing = true; return; }
        playing = true;
        th = new Thread(() -> run(c), "music"); th.start();
    }
    void pause() { playing = false; }
    void stop() { quit = true; playing = false; }

    void decode(Context c) throws Exception {
        MediaExtractor ex = new MediaExtractor();
        AssetFileDescriptor fd = c.getAssets().openFd("music.ogg");
        ex.setDataSource(fd.getFileDescriptor(), fd.getStartOffset(), fd.getLength());
        ex.selectTrack(0);
        MediaFormat f = ex.getTrackFormat(0);
        MediaCodec dec = MediaCodec.createDecoderByType(f.getString(MediaFormat.KEY_MIME));
        dec.configure(f, null, null, 0); dec.start();
        pcm = new short[(LOOP_END + 4096) * 2];
        int w = 0; boolean inDone = false; MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        while (!quit) {
            if (!inDone) {
                int i = dec.dequeueInputBuffer(10000);
                if (i >= 0) {
                    ByteBuffer b = dec.getInputBuffer(i);
                    int n = ex.readSampleData(b, 0);
                    if (n < 0) { dec.queueInputBuffer(i, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inDone = true; }
                    else { dec.queueInputBuffer(i, 0, n, ex.getSampleTime(), 0); ex.advance(); }
                }
            }
            int o = dec.dequeueOutputBuffer(info, 10000);
            if (o >= 0) {
                ShortBuffer sb = dec.getOutputBuffer(o).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer();
                int n = Math.min(sb.remaining(), pcm.length - w);
                sb.get(pcm, w, n); w += n;
                dec.releaseOutputBuffer(o, false);
                if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0 || w >= pcm.length) break;
            }
        }
        dec.stop(); dec.release(); ex.release();
        frames = Math.min(w / 2, LOOP_END);
    }

    void run(Context c) {
        try { decode(c); } catch (Exception e) { return; }
        if (frames < 44100) return;
        int min = AudioTrack.getMinBufferSize(44100, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        track = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(new AudioFormat.Builder().setSampleRate(44100).setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                .setBufferSizeInBytes(Math.max(min, 16384)).setTransferMode(AudioTrack.MODE_STREAM).build();
        track.setVolume(vol);
        int pos = 0, end = frames, loop = Math.min(LOOP_START, frames - 1);
        short[] chunk = new short[4096 * 2];
        boolean on = false;
        while (!quit) {
            if (!playing) { if (on) { track.pause(); track.flush(); on = false; } try { Thread.sleep(50); } catch (InterruptedException ignored) { } continue; }
            if (!on) { track.play(); on = true; }
            int n = 0;
            while (n < 4096) { int take = Math.min(4096 - n, end - pos); System.arraycopy(pcm, pos * 2, chunk, n * 2, take * 2); n += take; pos += take; if (pos >= end) pos = loop; }
            track.write(chunk, 0, n * 2);
        }
        track.stop(); track.release();
    }
}
