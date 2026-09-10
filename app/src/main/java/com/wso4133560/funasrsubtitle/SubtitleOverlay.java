package com.wso4133560.funasrsubtitle;

import android.content.Context;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.Locale;

final class SubtitleOverlay {
    private final WindowManager windowManager;
    private final WindowManager.LayoutParams params;
    private final LinearLayout root;
    private final TextView subtitle;
    private final TextView status;
    private final int backgroundAlpha;
    private boolean attached;
    private float downX;
    private float downY;
    private int startX;
    private int startY;

    SubtitleOverlay(Context context, AppSettings settings) {
        windowManager = context.getSystemService(WindowManager.class);
        backgroundAlpha = settings.backgroundAlpha();
        root = new LinearLayout(context);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(context, 18), dp(context, 12), dp(context, 18), dp(context, 10));
        root.setBackground(background());

        subtitle = new TextView(context);
        subtitle.setText("正在加载本地语音模型…");
        subtitle.setTextColor(Color.WHITE);
        subtitle.setTextSize(settings.fontSizeSp());
        subtitle.setGravity(Gravity.CENTER);
        subtitle.setMaxLines(3);
        subtitle.setShadowLayer(4, 0, 1, Color.BLACK);
        root.addView(subtitle, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        status = new TextView(context);
        status.setText("FunASR · CPU");
        status.setTextColor(Color.rgb(182, 201, 194));
        status.setTextSize(11);
        status.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams statusParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        statusParams.topMargin = dp(context, 5);
        root.addView(status, statusParams);

        int width = context.getResources().getDisplayMetrics().widthPixels - dp(context, 24);
        int height = context.getResources().getDisplayMetrics().heightPixels;
        params = new WindowManager.LayoutParams(
                Math.max(width, dp(context, 280)),
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
        params.gravity = Gravity.TOP | Gravity.START;
        params.x = dp(context, 12);
        params.y = Math.max(dp(context, 48), height - dp(context, 220));
        root.setOnTouchListener((view, event) -> drag(event));
    }

    void show() {
        if (attached) return;
        windowManager.addView(root, params);
        attached = true;
    }

    void update(String text, boolean isFinal, double computeMs) {
        subtitle.setText(text);
        status.setText(String.format(Locale.ROOT, "CPU · %.0f ms · %s", computeMs,
                isFinal ? "Final" : "Live"));
    }

    void showStatus(String message) {
        status.setText(message);
    }

    void remove() {
        if (!attached) return;
        windowManager.removeView(root);
        attached = false;
    }

    private boolean drag(MotionEvent event) {
        if (event.getAction() == MotionEvent.ACTION_DOWN) {
            downX = event.getRawX();
            downY = event.getRawY();
            startX = params.x;
            startY = params.y;
            return true;
        }
        if (event.getAction() == MotionEvent.ACTION_MOVE) {
            params.x = startX + Math.round(event.getRawX() - downX);
            params.y = startY + Math.round(event.getRawY() - downY);
            if (attached) windowManager.updateViewLayout(root, params);
            return true;
        }
        return event.getAction() == MotionEvent.ACTION_UP;
    }

    private GradientDrawable background() {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(Color.argb(backgroundAlpha, 12, 16, 17));
        drawable.setCornerRadius(8);
        drawable.setStroke(1, Color.argb(Math.min(220, backgroundAlpha + 30), 130, 150, 144));
        return drawable;
    }

    private static int dp(Context context, int value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }
}
