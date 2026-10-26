package dev.drawdo.app;

import android.app.NativeActivity;
import android.os.Bundle;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;

public final class MainActivity extends android.app.NativeActivity {
    static { System.loadLibrary("drawdo"); }
    private dev.drawdo.textbuffers.TextEditor editor;
    private static native void setInsets(int top, int bottom);
    @Override public void onCreate(android.os.Bundle state) {
        dev.drawdo.datafriend.IdentityStore.install(this);
        editor = new dev.drawdo.textbuffers.TextEditor(this);
        super.onCreate(state);
        new dev.drawdo.accessibleui.Accessibility(this);
        getWindow().setStatusBarColor(android.graphics.Color.WHITE);
        getWindow().setNavigationBarColor(android.graphics.Color.WHITE);
        getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
            | android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        android.view.View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(android.view.View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR |
            android.view.View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        android.view.View content = findViewById(android.R.id.content);
        content.setFocusableInTouchMode(true);
        content.requestFocus();
        decor.setOnApplyWindowInsetsListener((view, insets) -> {
            // Keyboard occlusion must not become a new app layout/safe area.
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsetsIgnoringVisibility(
                    android.view.WindowInsets.Type.systemBars() | android.view.WindowInsets.Type.displayCutout());
                setInsets(bars.top, bars.bottom);
            } else {
                setInsets(insets.getStableInsetTop(), insets.getStableInsetBottom());
            }
            return insets;
        });
        decor.requestApplyInsets();
    }

    public void showError(String message) {
        runOnUiThread(() -> new android.app.AlertDialog.Builder(this)
            .setTitle("DrawDo").setMessage(message).setPositiveButton("OK", null).show());
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        // The editor's window belongs to this foreground app. Keep Sokol's
        // frame/network loop running through that focus handoff; onPause still
        // suspends it when the activity actually leaves the foreground.
        super.onWindowFocusChanged(focused || (editor != null && editor.hasWindow()));
    }
    @Override protected void onPause() { editor.pause(); super.onPause(); }
    /** Called only when a cached label changes, never once per glyph per frame. */
    public static Bitmap rasterText(byte[] utf8, float pixels, int maxWidth, int maxHeight) {
        String text = new String(utf8, java.nio.charset.StandardCharsets.UTF_8);
        TextPaint paint = new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        paint.setTextSize(pixels);
        paint.setColor(Color.WHITE);
        if (maxHeight > 0) {
            // These metrics match the multiline editor, including font padding
            // and wrapping. Only the visible viewport gets a bitmap.
            paint.setTypeface(android.graphics.Typeface.DEFAULT);
            int width = Math.max(1, maxWidth);
            StaticLayout layout = StaticLayout.Builder.obtain(text, 0, text.length(), paint, width)
                .setAlignment(Layout.Alignment.ALIGN_NORMAL).setIncludePad(false)
                .setLineSpacing(0, 1).setBreakStrategy(Layout.BREAK_STRATEGY_SIMPLE)
                .setHyphenationFrequency(Layout.HYPHENATION_FREQUENCY_NONE)
                .setTextDirection(android.text.TextDirectionHeuristics.FIRSTSTRONG_LTR).build();
            Bitmap image = Bitmap.createBitmap(width, Math.max(1, Math.min(maxHeight, layout.getHeight())), Bitmap.Config.ARGB_8888);
            layout.draw(new Canvas(image));
            return image;
        }
        if (maxWidth > 0) text = android.text.TextUtils.ellipsize(text, paint, maxWidth - 4, android.text.TextUtils.TruncateAt.END).toString();
        int width = Math.max(1, (int)Math.ceil(Layout.getDesiredWidth(text, paint)) + 4);
        StaticLayout layout = StaticLayout.Builder.obtain(text, 0, text.length(), paint, width)
            .setAlignment(Layout.Alignment.ALIGN_NORMAL).setIncludePad(true).build();
        Bitmap image = Bitmap.createBitmap(width, Math.max(1, layout.getHeight()), Bitmap.Config.ARGB_8888);
        layout.draw(new Canvas(image));
        return image;
    }
}
