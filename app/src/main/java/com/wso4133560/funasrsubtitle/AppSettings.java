package com.wso4133560.funasrsubtitle;

import android.content.Context;
import android.content.SharedPreferences;

final class AppSettings {
    private static final String FILE = "subtitle_settings";
    static final String FONT_SIZE = "font_size_sp";
    static final String BACKGROUND_ALPHA = "background_alpha";
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
        return preferences.getInt(FONT_SIZE, 28);
    }

    void setFontSizeSp(int value) {
        preferences.edit().putInt(FONT_SIZE, value).apply();
    }

    int backgroundAlpha() {
        return preferences.getInt(BACKGROUND_ALPHA, 170);
    }

    void setBackgroundAlpha(int value) {
        preferences.edit().putInt(BACKGROUND_ALPHA, value).apply();
    }

    void registerListener(SharedPreferences.OnSharedPreferenceChangeListener listener) {
        preferences.registerOnSharedPreferenceChangeListener(listener);
    }

    void unregisterListener(SharedPreferences.OnSharedPreferenceChangeListener listener) {
        preferences.unregisterOnSharedPreferenceChangeListener(listener);
    }
}
