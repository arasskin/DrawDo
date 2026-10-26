package dev.drawdo.textbuffers;
import android.text.Layout;
public final class TextEditor {
    private final android.app.Activity activity;
    private native void install();
    private static native boolean timingEnabled();
    private static native void trace(int tag, int stage, long ns);
    private final boolean tracing;
    private static int traceTag(CharSequence text) {
        if (text.length() < 8 || text.charAt(0) != 'L' || text.charAt(1) != 'B') return 0;
        int tag = 0;
        for (int i = 2; i < 8; ++i) {
            char c = text.charAt(i);
            if (c < '0' || c > '9') return 0;
            tag = tag * 10 + c - '0';
        }
        return tag;
    }
    public TextEditor(android.app.Activity activity) { this.activity = activity; install(); tracing = timingEnabled(); }
    private android.widget.EditText editor;
    private android.widget.PopupWindow editorWindow;
    private long editorToken, editorRevision;
    private boolean suppressEditor, editorScheduled;
    private boolean liveEditor, embeddedEditor, popupEditor;
    private boolean retainOnSubmit, retained;
    private static native void finishEditor(long token, long revision, byte[] text, int anchor, int caret, int markedStart, int markedEnd, int events);
    // Shared flags mirror text_buffers/editor.h. Native state stays on the Android UI thread.
    private static final int SUBMITTED = 8, CANCELLED = 16, ACK = 32, REJECTED = 64;
    private final class CompositionWatcher implements android.text.SpanWatcher, android.text.NoCopySpan {
        public void onSpanAdded(android.text.Spannable s, Object what, int start, int end) { scheduleEditor(); }
        public void onSpanRemoved(android.text.Spannable s, Object what, int start, int end) { scheduleEditor(); }
        public void onSpanChanged(android.text.Spannable s, Object what, int a, int b, int c, int d) { scheduleEditor(); }
    }
    private final CompositionWatcher compositionWatcher = new CompositionWatcher();
    private void scheduleEditor() {
        if (editor == null || suppressEditor || retained) return;
        // Bump immediately, even when delivery is coalesced. A queued C command
        // must not overwrite a newer keystroke whose snapshot is still pending.
        ++editorRevision;
        if (editorScheduled) return;
        editorScheduled = true;
        final long token = editorToken;
        editor.post(() -> {
            if (editor == null || editorToken != token || retained) return;
            editorScheduled = false;
            sendEditor(0);
        });
    }
    private void sendEditor(int events) {
        if (editor == null) return;
        android.text.Editable text = editor.getText();
        finishEditor(editorToken, ++editorRevision,
            text.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8),
            Math.max(0, editor.getSelectionStart()), Math.max(0, editor.getSelectionEnd()),
            android.view.inputmethod.BaseInputConnection.getComposingSpanStart(text),
            android.view.inputmethod.BaseInputConnection.getComposingSpanEnd(text), events);
    }
    private void watchComposition() {
        editor.getText().setSpan(compositionWatcher, 0, editor.length(), android.text.Spanned.SPAN_INCLUSIVE_INCLUSIVE);
    }
    private static int rgba(int c) { return (c >>> 8) | (c << 24); }
    public void openEditor(long token, byte[] utf8, int x, int y, int width, int height,
                           float fontPixels, int foreground, int background,
                           boolean multiline, boolean live, byte[] label,
                           boolean placeAtPoint, float pointX, float pointY, boolean embedded,
                           boolean retain) {
        activity.runOnUiThread(() -> {
            closeEditor(true);
            editorToken = token; editorRevision = 0; liveEditor = live; embeddedEditor = embedded;
            retainOnSubmit = retain && !live && !embedded; retained = false;
            // NativeActivity gives its window surface to the renderer; child
            // Views there receive input but cannot draw. Use a separate surface.
            popupEditor = !embedded || activity instanceof android.app.NativeActivity;
            editorScheduled = false; suppressEditor = true;
            editor = new android.widget.EditText(activity) {
                @Override protected void onDraw(android.graphics.Canvas canvas) {
                    if (tracing) trace(traceTag(getText()), 18, System.nanoTime());
                    super.onDraw(canvas);
                    if (tracing) trace(traceTag(getText()), 6, System.nanoTime());
                }
                @Override protected void onSelectionChanged(int start, int end) {
                    super.onSelectionChanged(start, end);
                    scheduleEditor();
                }
                @Override public boolean onKeyPreIme(int key, android.view.KeyEvent event) {
                    if (retained) return true;
                    if (key == android.view.KeyEvent.KEYCODE_BACK) {
                        if (event.getAction() == android.view.KeyEvent.ACTION_UP) {
                            if (embeddedEditor) blurEditor(); else closeEditor(!liveEditor);
                        }
                        return true;
                    }
                    return super.onKeyPreIme(key, event);
                }
                @Override public android.view.inputmethod.InputConnection onCreateInputConnection(
                        android.view.inputmethod.EditorInfo info) {
                    android.view.inputmethod.InputConnection connection = super.onCreateInputConnection(info);
                    // Android adds NO_ENTER_ACTION for multiline fields. A submitted
                    // overlay still needs a reachable Done key; live documents retain Return.
                    if (!liveEditor && !embeddedEditor)
                        info.imeOptions &= ~android.view.inputmethod.EditorInfo.IME_FLAG_NO_ENTER_ACTION;
                    return connection;
                }
            };
            if (tracing) {
                android.view.Choreographer.getInstance().postFrameCallback(new android.view.Choreographer.FrameCallback() {
                    public void doFrame(long frameTime) {
                        trace(999999, 11, frameTime);
                        trace(999999, 12, System.nanoTime());
                        if (editor != null) android.view.Choreographer.getInstance().postFrameCallback(this);
                    }
                });
                editor.getViewTreeObserver().addOnPreDrawListener(() -> {
                    trace(traceTag(editor.getText()), 13, System.nanoTime());
                    return true;
                });
            }
            editor.setSingleLine(!multiline);
            editor.setInputType(android.text.InputType.TYPE_CLASS_TEXT |
                android.text.InputType.TYPE_TEXT_FLAG_CAP_SENTENCES |
                (multiline ? android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE : 0));
            editor.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_DONE |
                android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI);
            editor.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX, fontPixels);
            editor.setTextColor(rgba(foreground)); editor.setBackgroundColor(rgba(background));
            editor.setPadding(0, 0, 0, 0); editor.setIncludeFontPadding(false);
            editor.setTypeface(android.graphics.Typeface.DEFAULT);
            editor.setLineSpacing(0, 1);
            editor.setBreakStrategy(Layout.BREAK_STRATEGY_SIMPLE);
            editor.setHyphenationFrequency(Layout.HYPHENATION_FREQUENCY_NONE);
            editor.setTextDirection(android.view.View.TEXT_DIRECTION_FIRST_STRONG_LTR);
            editor.setGravity(android.view.Gravity.TOP | android.view.Gravity.LEFT);
            editor.setText(new String(utf8, java.nio.charset.StandardCharsets.UTF_8));
            editor.setContentDescription(new String(label, java.nio.charset.StandardCharsets.UTF_8));
            watchComposition();
            editor.addTextChangedListener(new android.text.TextWatcher() {
                public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                public void onTextChanged(CharSequence s, int start, int before, int count) {}
                public void afterTextChanged(android.text.Editable text) { scheduleEditor(); }
            });
            editor.setOnEditorActionListener((view, action, event) -> {
                if (action == android.view.inputmethod.EditorInfo.IME_ACTION_DONE ||
                    (!multiline && event != null && event.getKeyCode() == android.view.KeyEvent.KEYCODE_ENTER)) {
                    if (event == null || event.getAction() == android.view.KeyEvent.ACTION_UP) {
                        if (embeddedEditor) blurEditor(); else closeEditor(false);
                    }
                    return true;
                }
                return false;
            });
            // The host owns layout. Embedded views persist; overlays use a popup.
            android.view.View content = activity.findViewById(android.R.id.content);
            if (!popupEditor) {
                ((android.widget.FrameLayout)content).addView(editor);
                layoutEmbedded(x, y, width, height);
            } else {
                editorWindow = new android.widget.PopupWindow(editor, width, height, true);
                // This is an inline surface handoff, not a popup presentation.
                editorWindow.setAnimationStyle(0);
                editorWindow.setEnterTransition(null);
                editorWindow.setExitTransition(null);
                editorWindow.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(rgba(background)));
                editorWindow.setInputMethodMode(android.widget.PopupWindow.INPUT_METHOD_NEEDED);
                editorWindow.setSoftInputMode(embedded
                    ? android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE | android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN
                    : android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
                editorWindow.setOutsideTouchable(false);
                editorWindow.setOnDismissListener(() -> closeEditor(!liveEditor));
                editorWindow.showAtLocation(content, android.view.Gravity.TOP | android.view.Gravity.LEFT, x, y);
                if (!embedded) editor.requestFocus();
            }
            editor.setSelection(placeAtPoint ? 0 : editor.length());
            final android.widget.EditText opened = editor;
            // The native layout must exist before hit testing. Resolve once,
            // before drawing or opening the keyboard; later taps are native.
            opened.getViewTreeObserver().addOnPreDrawListener(new android.view.ViewTreeObserver.OnPreDrawListener() {
                @Override public boolean onPreDraw() {
                    opened.getViewTreeObserver().removeOnPreDrawListener(this);
                    if (editor != opened || editorToken != token) return true;
                    if (placeAtPoint) {
                        opened.scrollTo(0, 0);
                        int offset = opened.getOffsetForPosition(pointX, pointY);
                        opened.setSelection(offset >= 0 ? offset : opened.length());
                    }
                    suppressEditor = false;
                    sendEditor(0);
                    opened.post(() -> {
                        if (!embedded && editor == opened && editorToken == token)
                            ((android.view.inputmethod.InputMethodManager)activity.getSystemService(android.content.Context.INPUT_METHOD_SERVICE)).showSoftInput(opened, android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT);
                    });
                    return true;
                }
            });
        });
    }
    public void updateEditor(long token, long expected, byte[] utf8, int anchor, int caret) {
        activity.runOnUiThread(() -> {
            if (editor == null || editorToken != token) return;
            String text = new String(utf8, java.nio.charset.StandardCharsets.UTF_8);
            final int tag = tracing ? traceTag(text) : 0;
            if (tag != 0) trace(tag, 4, System.nanoTime());
            if (editorRevision != expected || android.view.inputmethod.BaseInputConnection.getComposingSpanStart(editor.getText()) >= 0) {
                if (tag != 0) trace(tag, 8, System.nanoTime());
                sendEditor(ACK | REJECTED); return;
            }
            if (anchor < 0 || caret < 0 || anchor > text.length() || caret > text.length()) {
                if (tag != 0) trace(tag, 8, System.nanoTime());
                sendEditor(ACK | REJECTED); return;
            }
            suppressEditor = true;
            editor.beginBatchEdit();
            try {
                android.text.Editable current = editor.getText();
                int start = 0, oldEnd = current.length(), newEnd = text.length();
                while (start < oldEnd && start < newEnd && current.charAt(start) == text.charAt(start)) ++start;
                // A common UTF-16 prefix/suffix can stop in the middle of an emoji.
                if (start > 0 && start < oldEnd && Character.isLowSurrogate(current.charAt(start))) --start;
                while (oldEnd > start && newEnd > start && current.charAt(oldEnd - 1) == text.charAt(newEnd - 1)) {
                    --oldEnd; --newEnd;
                }
                if (oldEnd > 0 && oldEnd < current.length() && Character.isLowSurrogate(current.charAt(oldEnd))) {
                    ++oldEnd; ++newEnd;
                }
                if (start != oldEnd || start != newEnd) {
                    if (tag != 0) trace(tag, 31, System.nanoTime());
                    current.replace(start, oldEnd, text, start, newEnd);
                    if (tag != 0) trace(tag, 32, System.nanoTime());
                    watchComposition();
                }
                if (editor.getSelectionStart() != anchor || editor.getSelectionEnd() != caret)
                    editor.setSelection(anchor, caret);
            } finally {
                editor.endBatchEdit();
                if (tag != 0) trace(tag, 33, System.nanoTime());
                suppressEditor = false;
            }
            if (tag != 0) {
                trace(tag, 5, System.nanoTime());
                if (android.os.Build.VERSION.SDK_INT >= 29 && editor.isHardwareAccelerated())
                    editor.getViewTreeObserver().registerFrameCommitCallback(() -> trace(tag, 7, System.nanoTime()));
            }
            sendEditor(ACK);
        });
    }
    public void layoutEditor(long token, int x, int y, int width, int height) {
        activity.runOnUiThread(() -> {
            if (editor == null || editorToken != token) return;
            if (!popupEditor) layoutEmbedded(x, y, width, height);
            else if (editorWindow != null) editorWindow.update(x, y, width, height);
        });
    }
    private void layoutEmbedded(int x, int y, int width, int height) {
        android.view.View parent = activity.findViewById(android.R.id.content);
        int[] origin = new int[2]; parent.getLocationOnScreen(origin);
        android.widget.FrameLayout.LayoutParams params = new android.widget.FrameLayout.LayoutParams(width, height);
        params.leftMargin = x - origin[0]; params.topMargin = y - origin[1];
        editor.setLayoutParams(params);
    }
    private void blurEditor() {
        if (editor == null) return;
        android.view.inputmethod.BaseInputConnection.removeComposingSpans(editor.getText());
        sendEditor(0);
        ((android.view.inputmethod.InputMethodManager)activity.getSystemService(android.content.Context.INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(editor.getWindowToken(), 0);
        editor.clearFocus();
    }
    public void cancelEditor(long token) {
        activity.runOnUiThread(() -> { if (editor != null && editorToken == token) closeEditor(true); });
    }
    private void closeEditor(boolean cancelled) {
        if (editor == null) return;
        if (retained && !cancelled) return;
        suppressEditor = true;
        if (!cancelled) android.view.inputmethod.BaseInputConnection.removeComposingSpans(editor.getText());
        if (!retained) sendEditor(cancelled ? CANCELLED : SUBMITTED);
        ((android.view.inputmethod.InputMethodManager)activity.getSystemService(android.content.Context.INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(editor.getWindowToken(), 0);
        if (!cancelled && retainOnSubmit) {
            retained = true;
            editor.setCursorVisible(false);
            editor.setFocusable(false);
            editor.setOnTouchListener((view, event) -> true);
            // Keep the opaque text surface until the app has presented its replacement.
            return;
        }
        android.widget.PopupWindow window = editorWindow;
        if (!popupEditor && editor.getParent() instanceof android.view.ViewGroup)
            ((android.view.ViewGroup)editor.getParent()).removeView(editor);
        editor = null; editorWindow = null; editorScheduled = false; retained = false;
        if (window != null) window.dismiss();
        suppressEditor = false;
    }
    /** UI thread: true while this editor owns a separate native window. */
    public boolean hasWindow() { return editorWindow != null && editorWindow.isShowing(); }
    public void pause() { if (embeddedEditor) blurEditor(); else closeEditor(!liveEditor); }

}
