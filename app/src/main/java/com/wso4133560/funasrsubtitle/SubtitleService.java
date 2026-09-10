package com.wso4133560.funasrsubtitle;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.drawable.Icon;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioPlaybackCaptureConfiguration;
import android.media.AudioRecord;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Process;

import java.util.concurrent.atomic.AtomicBoolean;

public final class SubtitleService extends Service {
    private static final String ACTION_START = "com.wso4133560.funasrsubtitle.START";
    private static final String ACTION_STOP = "com.wso4133560.funasrsubtitle.STOP";
    private static final String EXTRA_RESULT_CODE = "result_code";
    private static final String EXTRA_RESULT_DATA = "result_data";
    private static final String CHANNEL_ID = "realtime_subtitle";
    private static final int NOTIFICATION_ID = 1201;
    private static volatile boolean running;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final AtomicBoolean shuttingDown = new AtomicBoolean();
    private MediaProjection projection;
    private AudioRecord recorder;
    private Thread captureThread;
    private volatile boolean capturing;
    private NativeEngine engine;
    private SubtitleOverlay overlay;
    private TranscriptLogger transcript;

    static void start(Context context, int resultCode, Intent resultData) {
        Intent intent = new Intent(context, SubtitleService.class)
                .setAction(ACTION_START)
                .putExtra(EXTRA_RESULT_CODE, resultCode)
                .putExtra(EXTRA_RESULT_DATA, resultData);
        context.startForegroundService(intent);
    }

    static void stop(Context context) {
        context.startService(new Intent(context, SubtitleService.class).setAction(ACTION_STOP));
    }

    static boolean isRunning() {
        return running;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || ACTION_STOP.equals(intent.getAction())) {
            stopSelf();
            return START_NOT_STICKY;
        }
        if (!ACTION_START.equals(intent.getAction()) || running) return START_NOT_STICKY;

        startForeground(NOTIFICATION_ID, notification("正在准备本地识别模型"),
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION);
        running = true;

        try {
            Intent resultData = parcelableIntent(intent, EXTRA_RESULT_DATA);
            int resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, Activity.RESULT_CANCELED);
            if (resultData == null || resultCode != Activity.RESULT_OK) {
                throw new IllegalArgumentException("系统音频授权数据无效");
            }

            AppSettings settings = new AppSettings(this);
            ModelRepository models = new ModelRepository(this);
            if (!models.isReady()) throw new IllegalStateException("识别模型缺失");

            overlay = new SubtitleOverlay(this, settings);
            overlay.show();
            transcript = new TranscriptLogger(this);

            MediaProjectionManager manager = getSystemService(MediaProjectionManager.class);
            projection = manager.getMediaProjection(resultCode, resultData);
            if (projection == null) throw new IllegalStateException("无法创建系统音频捕获会话");
            projection.registerCallback(new MediaProjection.Callback() {
                @Override public void onStop() {
                    if (!shuttingDown.get()) {
                        showStatus("系统已停止音频捕获");
                        stopSelf();
                    }
                }
            }, mainHandler);

            engine = new NativeEngine(
                    models.file(ModelRepository.Type.SENSEVOICE).getAbsolutePath(),
                    models.file(ModelRepository.Type.VAD).getAbsolutePath(),
                    settings.threads(),
                    new NativeEngine.Callback() {
                        @Override public void onReady() {
                            mainHandler.post(SubtitleService.this::startCapture);
                        }

                        @Override public void onSubtitle(long id, boolean isFinal, String text,
                                                         double startSeconds, double endSeconds,
                                                         double computeMs) {
                            if (transcript != null) transcript.append(id, isFinal, text,
                                    startSeconds, endSeconds, computeMs);
                            mainHandler.post(() -> {
                                if (overlay != null) overlay.update(text, isFinal, computeMs);
                            });
                        }

                        @Override public void onStatus(String status) {
                            showStatus(status);
                        }

                        @Override public void onError(String message) {
                            showStatus(message);
                            mainHandler.post(SubtitleService.this::stopSelf);
                        }
                    });
        } catch (Exception error) {
            showStatus(error.getMessage());
            stopSelf();
        }
        return START_NOT_STICKY;
    }

    @SuppressLint("MissingPermission")
    private void startCapture() {
        if (shuttingDown.get() || capturing || projection == null || engine == null) return;
        try {
            AudioPlaybackCaptureConfiguration captureConfiguration =
                    new AudioPlaybackCaptureConfiguration.Builder(projection)
                            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                            .addMatchingUsage(AudioAttributes.USAGE_GAME)
                            .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                            .build();
            AudioFormat format = new AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(16000)
                    .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
                    .build();
            int minimum = AudioRecord.getMinBufferSize(16000, AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT);
            recorder = new AudioRecord.Builder()
                    .setAudioFormat(format)
                    .setAudioPlaybackCaptureConfig(captureConfiguration)
                    .setBufferSizeInBytes(Math.max(minimum, 16384))
                    .build();
            if (recorder.getState() != AudioRecord.STATE_INITIALIZED) {
                throw new IllegalStateException("系统音频采集初始化失败");
            }
            recorder.startRecording();
            capturing = true;
            captureThread = new Thread(this::captureLoop, "playback-capture");
            captureThread.start();
            showStatus("正在监听允许捕获的系统播放音频");
            updateNotification("正在识别系统播放音频");
        } catch (Exception error) {
            showStatus(error.getMessage());
            stopSelf();
        }
    }

    private void captureLoop() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO);
        short[] buffer = new short[2560];
        while (capturing && !shuttingDown.get()) {
            int count = recorder.read(buffer, 0, buffer.length, AudioRecord.READ_BLOCKING);
            if (count > 0) {
                if (!engine.push(buffer, count)) showStatus("音频缓冲区已满，已丢弃当前数据");
            } else if (count == AudioRecord.ERROR_DEAD_OBJECT || count == AudioRecord.ERROR_INVALID_OPERATION) {
                showStatus("系统音频采集已中断");
                mainHandler.post(this::stopSelf);
                break;
            }
        }
    }

    private void showStatus(String message) {
        String safe = message == null || message.trim().isEmpty() ? "字幕服务状态未知" : message;
        mainHandler.post(() -> {
            if (overlay != null) overlay.showStatus(safe);
        });
    }

    private void updateNotification(String text) {
        getSystemService(NotificationManager.class).notify(NOTIFICATION_ID, notification(text));
    }

    private Notification notification(String text) {
        PendingIntent open = PendingIntent.getActivity(this, 0,
                new Intent(this, MainActivity.class), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop = PendingIntent.getService(this, 1,
                new Intent(this, SubtitleService.class).setAction(ACTION_STOP),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(this, CHANNEL_ID)
                .setSmallIcon(R.drawable.ic_subtitles)
                .setContentTitle(getString(R.string.app_name))
                .setContentText(text)
                .setOngoing(true)
                .setContentIntent(open)
                .addAction(new Notification.Action.Builder(
                        Icon.createWithResource(this, R.drawable.ic_subtitles), "停止", stop).build())
                .build();
    }

    private void createNotificationChannel() {
        NotificationChannel channel = new NotificationChannel(CHANNEL_ID,
                getString(R.string.notification_channel), NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("保持系统音频实时字幕服务运行");
        getSystemService(NotificationManager.class).createNotificationChannel(channel);
    }

    @Override
    public void onDestroy() {
        if (!shuttingDown.compareAndSet(false, true)) {
            super.onDestroy();
            return;
        }
        running = false;
        capturing = false;
        if (recorder != null) {
            try { recorder.stop(); } catch (IllegalStateException ignored) { }
        }
        if (captureThread != null) {
            try { captureThread.join(1500); } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
        }
        if (recorder != null) recorder.release();
        recorder = null;
        if (engine != null) engine.close();
        engine = null;
        if (projection != null) projection.stop();
        projection = null;
        if (overlay != null) overlay.remove();
        overlay = null;
        if (transcript != null) transcript.close();
        transcript = null;
        stopForeground(STOP_FOREGROUND_REMOVE);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @SuppressWarnings("deprecation")
    private static Intent parcelableIntent(Intent source, String key) {
        if (Build.VERSION.SDK_INT >= 33) return source.getParcelableExtra(key, Intent.class);
        return source.getParcelableExtra(key);
    }
}
