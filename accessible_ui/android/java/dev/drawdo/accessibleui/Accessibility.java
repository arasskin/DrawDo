package dev.drawdo.accessibleui;
import android.app.Activity;
import android.content.Context;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Transparent semantic overlay. It neither draws nor intercepts ordinary touches. */
public final class Accessibility extends View {
    private static final int BUTTON=1, CHECKBOX=2, CHECKED=4, SELECTED=8, DISABLED=16, HEADER=32, SCROLL=64;
    private final Activity activity;
    private final AccessibilityManager manager;
    private ArrayList<Node> nodes = new ArrayList<>();
    private int nextId=1, focus=View.NO_ID, hover=View.NO_ID;
    private native void install();
    private static native boolean activate(long key, int action);
    private static final class Node {
        long key; int id, flags; Rect bounds; String label, value;
    }
    public Accessibility(Activity activity) {
        super(activity); this.activity=activity;
        manager=(AccessibilityManager)activity.getSystemService(Context.ACCESSIBILITY_SERVICE);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
        setFocusable(false); setWillNotDraw(true);
        activity.addContentView(this, new ViewGroup.LayoutParams(-1, -1));
        install();
    }
    private Node node(int id) { for(Node n:nodes) if(n.id==id) return n; return null; }
    public void update(long[] keys, float[] rects, int[] flags, byte[][] labels, byte[][] values) {
        activity.runOnUiThread(() -> {
            ArrayList<Node> next=new ArrayList<>(keys.length);
            for(int i=0;i<keys.length;i++) {
                Node n=null;
                for(Node old:nodes) if(old.key==keys[i]) { n=old; break; }
                if(n==null) { n=new Node(); n.key=keys[i]; n.id=nextId++; }
                n.flags=flags[i];
                n.bounds=new Rect((int)rects[i*4],(int)rects[i*4+1],
                    (int)(rects[i*4]+rects[i*4+2]),(int)(rects[i*4+1]+rects[i*4+3]));
                n.label=new String(labels[i],StandardCharsets.UTF_8);
                n.value=new String(values[i],StandardCharsets.UTF_8); next.add(n);
            }
            if(focus!=NO_ID && !contains(next,focus)) { send(focus,AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED); focus=NO_ID; }
            if(hover!=NO_ID && !contains(next,hover)) { send(hover,AccessibilityEvent.TYPE_VIEW_HOVER_EXIT); hover=NO_ID; }
            nodes=next;
            if(manager.isEnabled()) {
                AccessibilityEvent event=AccessibilityEvent.obtain(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
                event.setContentChangeTypes(AccessibilityEvent.CONTENT_CHANGE_TYPE_SUBTREE);
                event.setSource(this); event.setPackageName(activity.getPackageName());
                if(getParent()!=null) getParent().requestSendAccessibilityEvent(this,event);
            }
        });
    }
    private static boolean contains(ArrayList<Node> list,int id) { for(Node n:list) if(n.id==id) return true; return false; }
    private void send(int id,int type) {
        Node n=node(id); if(n==null || !manager.isEnabled() || getParent()==null) return;
        AccessibilityEvent e=AccessibilityEvent.obtain(type);
        e.setSource(this,id); e.setPackageName(activity.getPackageName());
        e.setClassName((n.flags&CHECKBOX)!=0 ? "android.widget.CheckBox" : "android.widget.Button");
        e.setContentDescription(n.label); e.setEnabled((n.flags&DISABLED)==0);
        getParent().requestSendAccessibilityEvent(this,e);
    }
    @Override public boolean onHoverEvent(MotionEvent e) {
        if(!manager.isTouchExplorationEnabled()) return false;
        int next=NO_ID;
        if(e.getAction()!=MotionEvent.ACTION_HOVER_EXIT) {
            int[] origin=new int[2]; getLocationOnScreen(origin);
            for(int i=nodes.size()-1;i>=0;i--)
                if(nodes.get(i).bounds.contains((int)e.getX()+origin[0],(int)e.getY()+origin[1])) { next=nodes.get(i).id; break; }
        }
        if(next!=hover) {
            if(next!=NO_ID) send(next,AccessibilityEvent.TYPE_VIEW_HOVER_ENTER);
            if(hover!=NO_ID) send(hover,AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);
            hover=next;
        }
        return next!=NO_ID || e.getAction()==MotionEvent.ACTION_HOVER_EXIT;
    }
    @Override public AccessibilityNodeProvider getAccessibilityNodeProvider() { return provider; }
    private final AccessibilityNodeProvider provider=new AccessibilityNodeProvider() {
        @Override public AccessibilityNodeInfo createAccessibilityNodeInfo(int id) {
            if(id==HOST_VIEW_ID) {
                AccessibilityNodeInfo info=AccessibilityNodeInfo.obtain(Accessibility.this);
                Accessibility.this.onInitializeAccessibilityNodeInfo(info);
                for(Node n:nodes) info.addChild(Accessibility.this,n.id);
                return info;
            }
            Node n=node(id); if(n==null) return null;
            AccessibilityNodeInfo info=AccessibilityNodeInfo.obtain();
            info.setSource(Accessibility.this,id); info.setParent(Accessibility.this);
            info.setPackageName(activity.getPackageName());
            info.setViewIdResourceName(activity.getPackageName()+":id/canvas_"+n.key);
            info.setClassName((n.flags&CHECKBOX)!=0 ? "android.widget.CheckBox" :
                (n.flags&BUTTON)!=0 ? "android.widget.Button" : "android.widget.TextView");
            info.setContentDescription(n.label); if(!n.value.isEmpty()) info.setText(n.value);
            info.setEnabled((n.flags&DISABLED)==0); info.setCheckable((n.flags&CHECKBOX)!=0);
            info.setChecked((n.flags&CHECKED)!=0); info.setSelected((n.flags&SELECTED)!=0);
            if(android.os.Build.VERSION.SDK_INT>=28) info.setHeading((n.flags&HEADER)!=0);
            info.setBoundsInScreen(n.bounds);
            Rect local=new Rect(n.bounds); int[] origin=new int[2]; getLocationOnScreen(origin);
            local.offset(-origin[0],-origin[1]); info.setBoundsInParent(local);
            info.setVisibleToUser(isShown()); info.setFocusable(true);
            info.setAccessibilityFocused(focus==id);
            info.addAction(focus==id ? AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS : AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS);
            if((n.flags&(BUTTON|CHECKBOX))!=0 && (n.flags&DISABLED)==0) {
                info.setClickable(true); info.addAction(AccessibilityNodeInfo.ACTION_CLICK);
            }
            if((n.flags&SCROLL)!=0) {
                info.setScrollable(true); info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD);
                info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD);
            }
            return info;
        }
        @Override public AccessibilityNodeInfo findFocus(int kind) {
            return kind==AccessibilityNodeInfo.FOCUS_ACCESSIBILITY ? createAccessibilityNodeInfo(focus) : null;
        }
        @Override public List<AccessibilityNodeInfo> findAccessibilityNodeInfosByText(String text,int id) {
            ArrayList<AccessibilityNodeInfo> result=new ArrayList<>();
            for(Node n:nodes) if((id==HOST_VIEW_ID || id==n.id) &&
                (n.label.toLowerCase(java.util.Locale.ROOT).contains(text.toLowerCase(java.util.Locale.ROOT)) ||
                 n.value.toLowerCase(java.util.Locale.ROOT).contains(text.toLowerCase(java.util.Locale.ROOT)))) result.add(createAccessibilityNodeInfo(n.id));
            return result;
        }
        @Override public boolean performAction(int id,int action,Bundle args) {
            Node n=node(id); if(n==null) return false;
            if(action==AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS) {
                if(focus==id) return false;
                if(focus!=NO_ID) send(focus,AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
                focus=id; send(id,AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED); return true;
            }
            if(action==AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS) {
                if(focus!=id) return false;
                focus=NO_ID; send(id,AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED); return true;
            }
            if((n.flags&DISABLED)!=0) return false;
            if(action==AccessibilityNodeInfo.ACTION_CLICK && (n.flags&(BUTTON|CHECKBOX))!=0) {
                boolean ok=activate(n.key,1); if(ok) send(id,AccessibilityEvent.TYPE_VIEW_CLICKED); return ok;
            }
            if((n.flags&SCROLL)!=0 && (action==AccessibilityNodeInfo.ACTION_SCROLL_FORWARD || action==AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD))
                return activate(n.key,action==AccessibilityNodeInfo.ACTION_SCROLL_FORWARD ? 2 : 3);
            return false;
        }
    };
}
