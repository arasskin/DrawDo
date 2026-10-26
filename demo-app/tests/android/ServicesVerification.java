package dev.drawdo.tests;
import android.app.Instrumentation;
import android.app.Activity;
import android.content.*;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.accessibility.AccessibilityNodeInfo;
public final class ServicesVerification extends Instrumentation {
    static { System.loadLibrary("bridge_checks"); }
    private static native String nativeChecks();
    @Override public void onCreate(Bundle args) { super.onCreate(args); start(); }
    private AccessibilityNodeInfo find(AccessibilityNodeInfo node,String label) {
        if(node==null) return null;
        if(label.contentEquals(node.getContentDescription()==null ? "" : node.getContentDescription())) return node;
        for(int i=0;i<node.getChildCount();i++) { AccessibilityNodeInfo found=find(node.getChild(i),label); if(found!=null) return found; }
        return null;
    }
    private AccessibilityNodeInfo waitFor(String label) {
        long until=SystemClock.uptimeMillis()+8000;
        do { AccessibilityNodeInfo node=find(getUiAutomation().getRootInActiveWindow(),label); if(node!=null) return node; SystemClock.sleep(100); } while(SystemClock.uptimeMillis()<until);
        throw new AssertionError("Missing accessible node: "+label);
    }
    private void check(boolean ok,String message) { if(!ok) throw new AssertionError(message); }
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            Intent intent=getTargetContext().getPackageManager().getLaunchIntentForPackage("dev.drawdo.sokol");
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            Activity activity=startActivitySync(intent); waitForIdleSync();
            AccessibilityNodeInfo edit=waitFor("Type / edit");
            check(edit.isClickable() && edit.isEnabled(),"Button semantics");
            check(edit.performAction(AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS),"Accessibility focus");
            check(edit.performAction(AccessibilityNodeInfo.ACTION_CLICK),"Accessibility activation");
            waitFor("Shared document");
            getUiAutomation().performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK);
            waitFor("Type / edit");
            ClipboardManager clipboard=(ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE);
            ClipData previous=clipboard.getPrimaryClip();
            try {
                String failure=nativeChecks();
                check(failure==null,"Native checks: "+failure);
            } finally {
                if(previous!=null) clipboard.setPrimaryClip(previous);
                else if(android.os.Build.VERSION.SDK_INT>=28) clipboard.clearPrimaryClip();
            }
            waitFor("Particles").performAction(AccessibilityNodeInfo.ACTION_CLICK);
            waitFor("Tasks").performAction(AccessibilityNodeInfo.ACTION_CLICK);
            AccessibilityNodeInfo list=waitFor("Task list");
            check(list.isScrollable(),"Task list scroll semantics");
            check(list.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD),"Task list scroll action");
            SystemClock.sleep(300);
            waitFor("Document").performAction(AccessibilityNodeInfo.ACTION_CLICK);
            waitFor("Type / edit");
            result.putString("result","PASS: accessible labels/focus/activation/navigation/scroll, native editor handoff, C clipboard Unicode/empty/invalid input, haptic dispatch");
            finish(Activity.RESULT_OK,result);
        } catch(Throwable error) {
            result.putString("error",android.util.Log.getStackTraceString(error)); finish(Activity.RESULT_CANCELED,result);
        }
    }
}
