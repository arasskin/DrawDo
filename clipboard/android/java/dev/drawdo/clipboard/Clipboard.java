package dev.drawdo.clipboard;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import java.nio.charset.StandardCharsets;
public final class Clipboard {
    private final ClipboardManager clipboard;
    private native void install();
    public Clipboard(Context context) {
        clipboard = (ClipboardManager)context.getSystemService(Context.CLIPBOARD_SERVICE);
        install();
    }
    public void set(byte[] utf8) {
        clipboard.setPrimaryClip(ClipData.newPlainText("", new String(utf8, StandardCharsets.UTF_8)));
    }
    public byte[] get() {
        ClipData clip = clipboard.getPrimaryClip();
        CharSequence text = clip != null && clip.getItemCount() > 0 ? clip.getItemAt(0).getText() : null;
        // Text only: do not open arbitrary content URIs while reading the clipboard.
        return (text == null ? "" : text.toString()).getBytes(StandardCharsets.UTF_8);
    }
}
