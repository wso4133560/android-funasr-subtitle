package com.wso4133560.funasrsubtitle;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.projection.MediaProjectionManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private static final int REQUEST_PERMISSIONS = 100;
    private static final int REQUEST_OVERLAY = 101;
    private static final int REQUEST_CAPTURE = 102;
    private ModelRepository models;
    private AppSettings settings;
    private TextView statusText;
    private TextView modelStateText;
    private TextView fontSizeLabel;
    private TextView opacityLabel;
    private boolean waitingForOverlay;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_main);
        models = new ModelRepository(this);
        settings = new AppSettings(this);
        statusText = findViewById(R.id.statusText);
        modelStateText = findViewById(R.id.modelStateText);
        fontSizeLabel = findViewById(R.id.fontSizeLabel);
        opacityLabel = findViewById(R.id.opacityLabel);

        findViewById(R.id.startButton).setOnClickListener(v -> beginStart());
        findViewById(R.id.stopButton).setOnClickListener(v -> {
            SubtitleService.stop(this);
            statusText.setText("正在停止字幕服务");
        });
        configureSettings();
        refreshState();
        models.installBundledModels((success, message) -> {
            if (!success) statusText.setText(message);
            else if (!message.isEmpty()) statusText.setText(message);
            refreshState();
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshState();
        if (waitingForOverlay && Settings.canDrawOverlays(this)) {
            waitingForOverlay = false;
            requestCapture();
        }
    }

    private void configureSettings() {
        Spinner spinner = findViewById(R.id.threadSpinner);
        ArrayAdapter<CharSequence> adapter = ArrayAdapter.createFromResource(
                this, R.array.thread_values, android.R.layout.simple_spinner_item);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        int[] values = {2, 4, 6, 8};
        int selected = 1;
        for (int i = 0; i < values.length; i++) if (values[i] == settings.threads()) selected = i;
        spinner.setSelection(selected);
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                settings.setThreads(values[position]);
            }
            @Override public void onNothingSelected(AdapterView<?> parent) { }
        });

        SeekBar font = findViewById(R.id.fontSizeSeek);
        font.setProgress(settings.fontSizeSp() - 18);
        updateFontLabel(settings.fontSizeSp());
        font.setOnSeekBarChangeListener(new SimpleSeekListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int value = 18 + progress;
                settings.setFontSizeSp(value);
                updateFontLabel(value);
            }
        });

        SeekBar opacity = findViewById(R.id.opacitySeek);
        opacity.setProgress(settings.backgroundAlpha() - 70);
        updateOpacityLabel(settings.backgroundAlpha());
        opacity.setOnSeekBarChangeListener(new SimpleSeekListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int value = 70 + progress;
                settings.setBackgroundAlpha(value);
                updateOpacityLabel(value);
            }
        });
    }

    private void updateFontLabel(int value) {
        fontSizeLabel.setText("字号 · " + value + " sp");
    }

    private void updateOpacityLabel(int value) {
        opacityLabel.setText("背景透明度 · " + Math.round(value * 100f / 255f) + "%");
    }

    private void beginStart() {
        if (!models.isReady()) {
            statusText.setText("内置模型尚未准备完成，请稍候");
            return;
        }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            if (Build.VERSION.SDK_INT >= 33) {
                requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO,
                        Manifest.permission.POST_NOTIFICATIONS}, REQUEST_PERMISSIONS);
            } else {
                requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, REQUEST_PERMISSIONS);
            }
            return;
        }
        requestOverlayThenCapture();
    }

    private void requestOverlayThenCapture() {
        if (!Settings.canDrawOverlays(this)) {
            waitingForOverlay = true;
            Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            startActivityForResult(intent, REQUEST_OVERLAY);
            return;
        }
        requestCapture();
    }

    private void requestCapture() {
        MediaProjectionManager manager = getSystemService(MediaProjectionManager.class);
        startActivityForResult(manager.createScreenCaptureIntent(), REQUEST_CAPTURE);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode != REQUEST_PERMISSIONS) return;
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
            requestOverlayThenCapture();
        } else {
            statusText.setText("系统音频采集需要录音权限");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_OVERLAY) {
            waitingForOverlay = false;
            if (Settings.canDrawOverlays(this)) requestCapture();
            else statusText.setText("显示透明字幕需要悬浮窗权限");
        } else if (requestCode == REQUEST_CAPTURE) {
            if (resultCode == RESULT_OK && data != null) {
                SubtitleService.start(this, resultCode, data);
                statusText.setText("正在启动字幕服务");
            } else {
                statusText.setText("未获得系统音频捕获授权");
            }
        }
    }

    private void refreshState() {
        modelStateText.setText(models.stateText());
        if (SubtitleService.isRunning()) statusText.setText(R.string.status_running);
        else if (models.isReady()) statusText.setText(R.string.status_models_ready);
    }

    private abstract static class SimpleSeekListener implements SeekBar.OnSeekBarChangeListener {
        @Override public void onStartTrackingTouch(SeekBar seekBar) { }
        @Override public void onStopTrackingTouch(SeekBar seekBar) { }
    }
}
