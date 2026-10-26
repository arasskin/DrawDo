package dev.drawdo.app.tests;
import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.EditText;

/** Exercises the real C app through its semantic tree and native editor. */
public final class TodoVerification extends Instrumentation {
    private Activity activity;
    private String mode;
    @Override public void onCreate(Bundle args) { super.onCreate(args); mode=args==null?"full":args.getString("mode","full"); start(); }
    private void check(boolean value,String message) { if(!value) throw new AssertionError(message); }
    private AccessibilityNodeInfo find(AccessibilityNodeInfo node,String label,boolean checkbox) {
        if(node==null) return null;
        if(label.contentEquals(node.getContentDescription()==null?"":node.getContentDescription()) && node.isCheckable()==checkbox) return node;
        for(int i=0;i<node.getChildCount();i++) { AccessibilityNodeInfo n=find(node.getChild(i),label,checkbox); if(n!=null) return n; }
        return null;
    }
    private AccessibilityNodeInfo waitFor(String label,boolean checkbox) {
        long deadline=SystemClock.uptimeMillis()+8000;
        do { AccessibilityNodeInfo n=find(getUiAutomation().getRootInActiveWindow(),label,checkbox); if(n!=null) return n; SystemClock.sleep(50); } while(SystemClock.uptimeMillis()<deadline);
        throw new AssertionError("Missing node: "+label+" checkbox="+checkbox);
    }
    private void click(String label,boolean checkbox) { check(waitFor(label,checkbox).performAction(AccessibilityNodeInfo.ACTION_CLICK),"Click "+label); }
    private EditText editor() {
        try {
            java.lang.reflect.Field bridge=activity.getClass().getDeclaredField("editor"); bridge.setAccessible(true);
            Object host=bridge.get(activity);
            java.lang.reflect.Field field=host.getClass().getDeclaredField("editor"); field.setAccessible(true);
            return (EditText)field.get(host);
        } catch(Exception e) { throw new RuntimeException(e); }
    }
    private void submit(String text) {
        waitFor(editorLabel,false);
        SystemClock.sleep(400); // Let the real keyboard finish opening before Done.
        runOnMainSync(() -> {
            EditText view=editor(); check(view!=null,"Native EditText");
            android.view.inputmethod.EditorInfo info=new android.view.inputmethod.EditorInfo();
            view.onCreateInputConnection(info);
            check((info.imeOptions & android.view.inputmethod.EditorInfo.IME_FLAG_NO_ENTER_ACTION)==0,"Multiline input suppressed Done");
            view.setText(text); view.onEditorAction(android.view.inputmethod.EditorInfo.IME_ACTION_DONE);
            check(editor()==view,"Done removed text before the replacement frame");
            check(!view.isFocusable() && !view.isCursorVisible(),"Submitted editor must freeze while retained");
        });
        waitFor("New task",false);
    }
    private String editorLabel;
    private void add(String text) { editorLabel="New task"; click("New task",false); waitNative(); submit(text); waitFor(text,false); }
    private void waitNative() {
        long deadline=SystemClock.uptimeMillis()+8000;
        while(SystemClock.uptimeMillis()<deadline) { final boolean[] exists={false}; runOnMainSync(() -> exists[0]=editor()!=null); if(exists[0]) return; SystemClock.sleep(50); }
        throw new AssertionError("Native editor did not open");
    }
    private void screenshot(String name) throws Exception {
        SystemClock.sleep(200);
        try(java.io.FileOutputStream out=new java.io.FileOutputStream(new java.io.File(getTargetContext().getFilesDir(),name))) {
            getUiAutomation().takeScreenshot().compress(android.graphics.Bitmap.CompressFormat.PNG,100,out);
        }
    }
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            Intent intent=getTargetContext().getPackageManager().getLaunchIntentForPackage("dev.drawdo.app");
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); activity=startActivitySync(intent); waitForIdleSync();
            if (!"full".equals(mode)) {
                waitFor("DrawDo phone test",false);
                if ("cleanup".equals(mode)) {
                    click("DrawDo phone test",false); editorLabel="Task title"; waitNative(); submit("");
                    SystemClock.sleep(300);
                    check(find(getUiAutomation().getRootInActiveWindow(),"DrawDo phone test",false)==null,"Test task cleanup");
                } else check(!waitFor("DrawDo phone test",true).isChecked(),"Persisted completion state");
                result.putString("result","PASS: "+mode);
                finish(Activity.RESULT_OK,result); return;
            }
            add("DrawDo phone test");
            AccessibilityNodeInfo title=waitFor("DrawDo phone test",false);
            Rect before=new Rect(); title.getBoundsInScreen(before);
            editorLabel="Task title";
            // Tap toward the end of the visible title; the native cursor should hit-test there.
            long now=SystemClock.uptimeMillis(); float x=before.left+before.width()*.65f,y=before.centerY();
            getUiAutomation().injectInputEvent(android.view.MotionEvent.obtain(now,now,0,x,y,0),true);
            getUiAutomation().injectInputEvent(android.view.MotionEvent.obtain(now,now+60,1,x,y,0),true);
            waitNative(); waitFor("Task title",false); SystemClock.sleep(500);
            runOnMainSync(() -> {
                EditText view=editor(); int[] pos=new int[2]; view.getLocationOnScreen(pos);
                check(Math.abs(pos[0]-before.left)<=2 && Math.abs(pos[1]-before.top)<=2,"Keyboard moved text origin: "+pos[0]+","+pos[1]+" vs "+before);
                check(view.getSelectionStart()>0,"Tap cursor stayed at start");
            });
            screenshot("drawdo-keyboard.png");
            submit("DrawDo renamed ✓"); waitFor("DrawDo renamed ✓",false);
            click("DrawDo renamed ✓",true);
            long until=SystemClock.uptimeMillis()+5000;
            while(!waitFor("DrawDo renamed ✓",true).isChecked() && SystemClock.uptimeMillis()<until) SystemClock.sleep(50);
            check(waitFor("DrawDo renamed ✓",true).isChecked(),"Checkbox did not update");
            add("A longer task wraps naturally onto another line");
            screenshot("drawdo-todos.png");
            click("A longer task wraps naturally onto another line",false); editorLabel="Task title"; waitNative(); submit("");
            check(find(getUiAutomation().getRootInActiveWindow(),"A longer task wraps naturally onto another line",false)==null,"Empty title did not delete");
            click("DrawDo renamed ✓",false); waitNative(); submit("DrawDo phone test");
            click("DrawDo phone test",true); SystemClock.sleep(100);
            check(!waitFor("DrawDo phone test",true).isChecked(),"Unchecked state");
            // Leave exactly one known task for the host's server subscription check.
            result.putString("result","PASS: add/edit/toggle/delete, Unicode, wrapped text, accessibility actions, tap cursor, stable keyboard origin");
            finish(Activity.RESULT_OK,result);
        } catch(Throwable error) {
            result.putString("error",android.util.Log.getStackTraceString(error)); finish(Activity.RESULT_CANCELED,result);
        }
    }
}
