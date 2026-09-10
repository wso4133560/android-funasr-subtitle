package com.wso4133560.funasrsubtitle;

import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.Locale;

final class ModelRepository {
    enum Type {
        SENSEVOICE(
                "sensevoice-small-q8.gguf",
                254208320L,
                "4ae45c94422de949b387e2e0fb10d7e14e4c42c69db30c3444ecc7d4b844b7c5"),
        VAD(
                "fsmn-vad.gguf",
                1720512L,
                "1270f2559c495f4e7b6e739541151027d360761a3fda43fc147034f5719f5479");

        final String fileName;
        final long size;
        final String sha256;

        Type(String fileName, long size, String sha256) {
            this.fileName = fileName;
            this.size = size;
            this.sha256 = sha256;
        }
    }

    interface ImportCallback {
        void onComplete(boolean success, String message);
    }

    private final Context context;
    private final File directory;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    ModelRepository(Context context) {
        this.context = context.getApplicationContext();
        directory = new File(context.getFilesDir(), "models");
    }

    File file(Type type) {
        return new File(directory, type.fileName);
    }

    boolean isInstalled(Type type) {
        File candidate = file(type);
        return candidate.isFile() && candidate.length() == type.size;
    }

    boolean isReady() {
        return isInstalled(Type.SENSEVOICE) && isInstalled(Type.VAD);
    }

    String stateText() {
        return "SenseVoice: " + (isInstalled(Type.SENSEVOICE) ? "已导入" : "缺失")
                + "\nFSMN-VAD: " + (isInstalled(Type.VAD) ? "已导入" : "缺失");
    }

    void importModel(Uri uri, Type type, ImportCallback callback) {
        new Thread(() -> {
            try {
                if (!directory.exists() && !directory.mkdirs()) {
                    throw new IllegalStateException("无法创建模型目录");
                }
                File part = new File(directory, type.fileName + ".part");
                MessageDigest digest = MessageDigest.getInstance("SHA-256");
                long total = 0;
                try (InputStream input = context.getContentResolver().openInputStream(uri);
                     FileOutputStream output = new FileOutputStream(part)) {
                    if (input == null) throw new IllegalStateException("无法读取所选文件");
                    byte[] buffer = new byte[1024 * 1024];
                    int read;
                    while ((read = input.read(buffer)) > 0) {
                        output.write(buffer, 0, read);
                        digest.update(buffer, 0, read);
                        total += read;
                    }
                    output.getFD().sync();
                }

                String actual = toHex(digest.digest());
                if (total != type.size || !actual.equals(type.sha256)) {
                    //noinspection ResultOfMethodCallIgnored
                    part.delete();
                    throw new IllegalArgumentException("模型文件或 SHA256 不匹配");
                }
                File target = file(type);
                if (target.exists() && !target.delete()) {
                    throw new IllegalStateException("无法替换旧模型");
                }
                if (!part.renameTo(target)) {
                    throw new IllegalStateException("无法完成模型导入");
                }
                mainHandler.post(() -> callback.onComplete(true, type.fileName + " 已校验并导入"));
            } catch (Exception error) {
                mainHandler.post(() -> callback.onComplete(false, error.getMessage()));
            }
        }, "model-import").start();
    }

    static boolean verifyFile(File file, Type type) {
        if (!file.isFile() || file.length() != type.size) return false;
        try (FileInputStream input = new FileInputStream(file)) {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[1024 * 1024];
            int read;
            while ((read = input.read(buffer)) > 0) digest.update(buffer, 0, read);
            return toHex(digest.digest()).equals(type.sha256);
        } catch (Exception ignored) {
            return false;
        }
    }

    private static String toHex(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) result.append(String.format(Locale.ROOT, "%02x", value & 0xff));
        return result.toString();
    }
}
