package com.wso4133560.funasrsubtitle;

import android.content.Context;
import android.content.SharedPreferences;

final class AppSettings {
    private static final String FILE = "subtitle_settings";
    private final SharedPreferences preferences;

    AppSettings(Context context) {
        preferences = context.getSharedPreferences(FILE, Context.MODE_PRIVATE);
    }

    int threads() {
        return preferences.getInt("threads", 4);
    }

    void setThreads(int value) {
        preferences.edit().putInt("threads", value).apply();
    }

    int fontSizeSp() {
        return preferences.getInt("font_size_sp", 28);
    }

    void setFontSizeSp(int value) {
        preferences.edit().putInt("font_size_sp", value).apply();
    }

    int backgroundAlpha() {
        return preferences.getInt("background_alpha", 170);
    }

    void setBackgroundAlpha(int value) {
        preferences.edit().putInt("background_alpha", value).apply();
    }
}
