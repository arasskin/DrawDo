package dev.drawdo.crdt;

public final class MainActivity extends android.app.NativeActivity {
    static { System.loadLibrary("crdt_demo"); }
    private dev.drawdo.textbuffers.TextEditor editor;
    private static native void setBounds(int x, int y, int width, int height);
    @Override public void onCreate(android.os.Bundle state) {
        dev.drawdo.datafriend.IdentityStore.install(this);
        editor = new dev.drawdo.textbuffers.TextEditor(this);
        super.onCreate(state);
        getWindow().setStatusBarColor(android.graphics.Color.WHITE);
        getWindow().setNavigationBarColor(android.graphics.Color.WHITE);
        getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE
            | android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        android.view.View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(android.view.View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR |
            android.view.View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        android.view.View content = findViewById(android.R.id.content);
        content.setFocusableInTouchMode(true);
        content.requestFocus();
        content.getViewTreeObserver().addOnGlobalLayoutListener(() -> {
            android.graphics.Rect bounds = new android.graphics.Rect();
            content.getGlobalVisibleRect(bounds);
            setBounds(bounds.left, bounds.top, bounds.width(), bounds.height());
        });
    }
    public void showError(String message) {
        runOnUiThread(() -> new android.app.AlertDialog.Builder(this)
            .setTitle("CRDT demo").setMessage(message).setPositiveButton("OK", null).show());
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        // The editor's window belongs to this foreground app. Keep Sokol's
        // frame/network loop running through that focus handoff; onPause still
        // suspends it when the activity actually leaves the foreground.
        super.onWindowFocusChanged(focused || (editor != null && editor.hasWindow()));
    }
    @Override protected void onPause() { editor.pause(); super.onPause(); }
}
