package com.wso4133560.funasrsubtitle;

final class NativeEngine implements AutoCloseable {
    interface Callback {
        void onReady();
        void onSubtitle(long id, boolean isFinal, String text, double startSeconds,
                        double endSeconds, double computeMs);
        void onStatus(String status);
        void onError(String message);
    }

    static {
        System.loadLibrary("funasr_engine");
    }

    private final Callback callback;
    private long handle;

    NativeEngine(String modelPath, String vadPath, int threads, Callback callback) {
        this.callback = callback;
        handle = nativeCreate(modelPath, vadPath, threads);
        if (handle == 0) throw new IllegalStateException("无法创建原生识别器");
    }

    synchronized boolean push(short[] samples, int count) {
        return handle != 0 && nativePushPcm16(handle, samples, count);
    }

    @Override
    public synchronized void close() {
        if (handle == 0) return;
        nativeDestroy(handle);
        handle = 0;
    }

    @SuppressWarnings("unused")
    private void onNativeReady() {
        callback.onReady();
    }

    @SuppressWarnings("unused")
    private void onNativeSubtitle(long id, boolean isFinal, String text, double startSeconds,
                                  double endSeconds, double computeMs) {
        callback.onSubtitle(id, isFinal, text, startSeconds, endSeconds, computeMs);
    }

    @SuppressWarnings("unused")
    private void onNativeStatus(String status) {
        callback.onStatus(status);
    }

    @SuppressWarnings("unused")
    private void onNativeError(String message) {
        callback.onError(message);
    }

    private native long nativeCreate(String modelPath, String vadPath, int threads);
    private native boolean nativePushPcm16(long handle, short[] samples, int count);
    private native void nativeDestroy(long handle);
}
