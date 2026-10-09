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

/** Platform service only. Simulation, UI state, layout and rendering live in C++. */
public final class MainActivity extends NativeActivity {
    static { System.loadLibrary("drawdo_sokol"); }
    private android.widget.EditText editor;
    private android.widget.PopupWindow editorWindow;
    private long editorToken;
    private boolean liveEditor;
    private static native void finishEditor(long token, byte[] text, boolean cancelled, boolean finished);
    private static native void setInsets(int top, int bottom);
    @Override public void onCreate(Bundle state) {
        dev.drawdo.datafriend.IdentityStore.install(this);
        super.onCreate(state);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
        getWindow().setStatusBarColor(Color.rgb(16, 25, 35));
        getWindow().setNavigationBarColor(Color.rgb(16, 25, 35));
        android.view.View decor = getWindow().getDecorView();
        decor.setOnApplyWindowInsetsListener((view, insets) -> {
            setInsets(insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetBottom());
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

    /** Native text-input service: real Android IME composition, selection and paste.
     * Application edits are delivered to the C++ frame thread through a mailbox. */
    public void openEditor(long token, byte[] utf8, int x, int y, int width, int height, boolean live) {
        runOnUiThread(() -> {
            closeEditor(true);
            editorToken = token;liveEditor = live;
            editor = new android.widget.EditText(this) {
                @Override public boolean onKeyPreIme(int key, android.view.KeyEvent event) {
                    if (key == android.view.KeyEvent.KEYCODE_BACK) {
                        if (event.getAction() == android.view.KeyEvent.ACTION_UP) closeEditor(true);
                        return true;
                    }
                    return super.onKeyPreIme(key, event);
                }
            };
            editor.setSingleLine(!live);
            editor.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_CAP_SENTENCES | (live ? android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE : 0));
            editor.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_DONE | android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI);
            editor.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX, live ? 44 : height * .34f);
            editor.setTextColor(Color.rgb(232, 242, 247));
            editor.setBackgroundColor(Color.rgb(32, 50, 63));
            editor.setPadding(12, 0, 12, 0);
            editor.setText(new String(utf8, java.nio.charset.StandardCharsets.UTF_8));
            editor.setHint("What would you like to get done?");
            editor.setHintTextColor(Color.rgb(135,165,182));
            editor.setContentDescription(live ? "Shared document draft" : "Task title");
            editor.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(live ? 20000 : 240)});
            if (live) editor.addTextChangedListener(new android.text.TextWatcher() {
                public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                public void onTextChanged(CharSequence s, int start, int before, int count) {}
                public void afterTextChanged(android.text.Editable s) {
                    if (android.view.inputmethod.BaseInputConnection.getComposingSpanStart(s) >= 0) return;
                    finishEditor(editorToken, s.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8), false, false);
                }
            });
            editor.setOnEditorActionListener((view, action, event) -> {
                if (action == android.view.inputmethod.EditorInfo.IME_ACTION_DONE ||
                    (!live && event != null && event.getKeyCode() == android.view.KeyEvent.KEYCODE_ENTER)) {
                    if (event == null || event.getAction() == android.view.KeyEvent.ACTION_UP) closeEditor(false);
                    return true;
                }
                return false;
            });
            // The native surface and overlay can have different window origins.
            android.view.View content = findViewById(android.R.id.content);
            // NativeActivity's EGL renderer owns the main window surface. Use a
            // separate popup surface for Android's editor so Sokol cannot paint
            // over its text/caret/selection.
            editorWindow = new android.widget.PopupWindow(editor, width, height, true);
            editorWindow.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Color.rgb(32,50,63)));
            editorWindow.setInputMethodMode(android.widget.PopupWindow.INPUT_METHOD_NEEDED);
            editorWindow.setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
            editorWindow.setOutsideTouchable(false);
            editorWindow.setOnDismissListener(() -> closeEditor(true));
            editorWindow.showAtLocation(content, android.view.Gravity.TOP | android.view.Gravity.LEFT, x, y);
            editor.requestFocus();editor.setSelection(editor.length());
            editor.post(() -> { if (editor != null) ((android.view.inputmethod.InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).showSoftInput(editor, android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT); });
        });
    }
    private void closeEditor(boolean cancelled) {
        if (editor == null) return;
        finishEditor(editorToken, editor.getText().toString().getBytes(java.nio.charset.StandardCharsets.UTF_8), cancelled && !liveEditor, true);
        ((android.view.inputmethod.InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(editor.getWindowToken(), 0);
        android.widget.PopupWindow window = editorWindow;
        editor = null;editorWindow = null;
        window.dismiss();
    }
    @Override protected void onPause() { closeEditor(true);super.onPause(); }

    /** Called only when a cached label changes, never once per glyph per frame. */
    public static Bitmap rasterText(byte[] utf8, float pixels, int maxWidth) {
        String text = new String(utf8, java.nio.charset.StandardCharsets.UTF_8);
        TextPaint paint = new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        paint.setTextSize(pixels);
        paint.setColor(Color.WHITE);
        if (maxWidth > 0) text = android.text.TextUtils.ellipsize(text, paint, maxWidth - 4, android.text.TextUtils.TruncateAt.END).toString();
        int width = Math.max(1, (int)Math.ceil(Layout.getDesiredWidth(text, paint)) + 4);
        StaticLayout layout = StaticLayout.Builder.obtain(text, 0, text.length(), paint, width)
            .setAlignment(Layout.Alignment.ALIGN_NORMAL).setIncludePad(true).build();
        Bitmap image = Bitmap.createBitmap(width, Math.max(1, layout.getHeight()), Bitmap.Config.ARGB_8888);
        layout.draw(new Canvas(image));
        return image;
    }
}
