package com.wso4133560.funasrsubtitle;

import android.content.Context;

import java.io.BufferedWriter;
import java.io.Closeable;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

final class TranscriptLogger implements Closeable {
    private final BufferedWriter tsv;
    private final BufferedWriter srt;
    private int srtIndex;

    TranscriptLogger(Context context) throws IOException {
        File directory = new File(context.getFilesDir(), "logs");
        if (!directory.exists() && !directory.mkdirs()) throw new IOException("无法创建字幕日志目录");
        String session = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
        tsv = new BufferedWriter(new FileWriter(new File(directory, session + ".tsv")));
        srt = new BufferedWriter(new FileWriter(new File(directory, session + ".srt")));
        tsv.write("id\tfinal\tstart_s\tend_s\tcompute_ms\ttext\n");
        tsv.flush();
    }

    synchronized void append(long id, boolean isFinal, String text, double start, double end,
                             double computeMs) {
        try {
            String clean = text.replace('\t', ' ').replace('\n', ' ');
            tsv.write(String.format(Locale.ROOT, "%d\t%d\t%.3f\t%.3f\t%.2f\t%s%n",
                    id, isFinal ? 1 : 0, start, end, computeMs, clean));
            tsv.flush();
            if (isFinal) {
                srt.write(Integer.toString(++srtIndex));
                srt.newLine();
                srt.write(stamp(start) + " --> " + stamp(Math.max(end, start + 0.01)));
                srt.newLine();
                srt.write(clean);
                srt.newLine();
                srt.newLine();
                srt.flush();
            }
        } catch (IOException ignored) {
            // Recognition remains available if storage becomes temporarily unavailable.
        }
    }

    private static String stamp(double seconds) {
        long milliseconds = Math.max(0, (long) (seconds * 1000));
        return String.format(Locale.ROOT, "%02d:%02d:%02d,%03d",
                milliseconds / 3600000,
                (milliseconds / 60000) % 60,
                (milliseconds / 1000) % 60,
                milliseconds % 1000);
    }

    @Override
    public synchronized void close() {
        try { tsv.close(); } catch (IOException ignored) { }
        try { srt.close(); } catch (IOException ignored) { }
    }
}
