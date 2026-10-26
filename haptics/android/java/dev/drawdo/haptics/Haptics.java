package dev.drawdo.haptics;
import android.app.Activity;
import android.view.HapticFeedbackConstants;
public final class Haptics {
    private final Activity activity;
    private native void install();
    public Haptics(Activity activity) { this.activity = activity; install(); }
    public void play(int kind) {
        final int feedback;
        switch (kind) {
        case 0: feedback = HapticFeedbackConstants.CLOCK_TICK; break;
        case 1: feedback = HapticFeedbackConstants.VIRTUAL_KEY; break;
        case 2: feedback = android.os.Build.VERSION.SDK_INT >= 30 ? HapticFeedbackConstants.CONFIRM : HapticFeedbackConstants.LONG_PRESS; break;
        default: return;
        }
        activity.runOnUiThread(() -> activity.getWindow().getDecorView().performHapticFeedback(feedback));
    }
}
