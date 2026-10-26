package dev.drawdo.crdttests;

import android.view.accessibility.AccessibilityNodeInfo;

/** Drives the native editor's accessibility API, then waits for the other phone. */
public final class SyncTest extends android.app.Instrumentation {
    @Override public void onCreate(android.os.Bundle arguments) { super.onCreate(arguments); start(); }
    private AccessibilityNodeInfo find(AccessibilityNodeInfo node) {
        if (node == null) return null;
        if ("Shared text".contentEquals(node.getContentDescription() == null ? "" : node.getContentDescription()))
            return node;
        for (int i = 0; i < node.getChildCount(); ++i) {
            AccessibilityNodeInfo found = find(node.getChild(i));
            if (found != null) return found;
        }
        return null;
    }
    private AccessibilityNodeInfo editor() {
        AccessibilityNodeInfo found = find(getUiAutomation().getRootInActiveWindow());
        if (found != null) return found;
        for (android.view.accessibility.AccessibilityWindowInfo window : getUiAutomation().getWindows()) {
            found = find(window.getRoot());
            if (found != null) return found;
        }
        return null;
    }
    private void expect(String text, long seconds) throws Exception {
        long deadline = android.os.SystemClock.elapsedRealtime() + seconds * 1000;
        while (android.os.SystemClock.elapsedRealtime() < deadline) {
            AccessibilityNodeInfo node = editor();
            if (node != null && text.contentEquals(node.getText() == null ? "" : node.getText())) return;
            Thread.sleep(100);
        }
        AccessibilityNodeInfo last = editor();
        throw new AssertionError("Timed out waiting for native text: " + text + " actual=" + (last == null ? "<missing>" : last.getText()));
    }
    private void set(String text) {
        AccessibilityNodeInfo node = editor();
        if (node == null) throw new AssertionError("Missing native editor");
        android.os.Bundle args = new android.os.Bundle();
        args.putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, text);
        if (!node.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, args))
            throw new AssertionError("Native text edit rejected");
    }
    @Override public void onStart() {
        android.os.Bundle result = new android.os.Bundle();
        try {
            android.accessibilityservice.AccessibilityServiceInfo info = getUiAutomation().getServiceInfo();
            info.flags |= android.accessibilityservice.AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS;
            getUiAutomation().setServiceInfo(info);
            android.content.Intent launch = getTargetContext().getPackageManager()
                .getLaunchIntentForPackage("dev.drawdo.crdt");
            launch.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
            getTargetContext().startActivity(launch);
            long deadline = android.os.SystemClock.elapsedRealtime() + 15000;
            while (editor() == null && android.os.SystemClock.elapsedRealtime() < deadline) Thread.sleep(100);
            set("Android");
            android.util.Log.i("CRDTtest", "Published Android seed");
            expect("Android iPhone ", 240);
            android.util.Log.i("CRDTtest", "Received iPhone edit");
            getUiAutomation().performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_HOME);
            Thread.sleep(500);
            try (java.io.InputStream input = new android.os.ParcelFileDescriptor.AutoCloseInputStream(
                    getUiAutomation().executeShellCommand("am start -W -a android.intent.action.MAIN -c android.intent.category.LAUNCHER --activity-reorder-to-front --activity-single-top -n dev.drawdo.crdt/.MainActivity"))) {
                byte[] output = new byte[1024];
                while (input.read(output) >= 0) {}
            }
            expect("Android iPhone ", 15);
            set("Android iPhone synced");
            expect("", 90);
            result.putString("stream", "PASS: Android -> iPhone -> Android, remote refresh, deletion\n");
            finish(android.app.Activity.RESULT_OK, result);
        } catch (Throwable error) {
            result.putString("stream", "FAIL: " + error + "\n");
            finish(android.app.Activity.RESULT_CANCELED, result);
        }
    }
}
