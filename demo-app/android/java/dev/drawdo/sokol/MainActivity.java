package dev.drawdo.sokol;

import android.app.NativeActivity;
import android.os.Bundle;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;

/** Platform service only. Simulation, UI state, layout and rendering live in C99. */
public final class MainActivity extends NativeActivity {
    static { System.loadLibrary("drawdo_sokol"); }
    private dev.drawdo.textbuffers.TextEditor textEditor;
    private static native void setInsets(int top, int bottom);
    @Override public void onCreate(Bundle state) {
        dev.drawdo.datafriend.IdentityStore.install(this);
        textEditor = new dev.drawdo.textbuffers.TextEditor(this);
        new dev.drawdo.clipboard.Clipboard(this);
        new dev.drawdo.haptics.Haptics(this);
        super.onCreate(state);
        new dev.drawdo.accessibleui.Accessibility(this);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
        getWindow().setStatusBarColor(Color.rgb(16, 25, 35));
        getWindow().setNavigationBarColor(Color.rgb(16, 25, 35));
        android.view.View decor = getWindow().getDecorView();
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

    // Opt-in profiling uses a disposable store; normal launches keep tasks-v1.
    public int[] taskProfile() {
        if (!getIntent().getBooleanExtra("tasks_profile", false)) return new int[0];
        return new int[]{getIntent().getIntExtra("profile_count", 12),
            getIntent().getIntExtra("profile_case", 0),
            getIntent().getIntExtra("profile_warmup", 60),
            getIntent().getIntExtra("profile_frames", 360)};
    }

    @Override protected void onPause() { textEditor.pause(); super.onPause(); }

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
