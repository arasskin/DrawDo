package dev.drawdo.text_latency
import android.view.Choreographer
import android.view.WindowManager
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity(), Choreographer.FrameCallback {
    private external fun trace(stage: Int, ns: Long)
    private var running = false
    override fun configureFlutterEngine(engine: FlutterEngine) {
        super.configureFlutterEngine(engine)
        System.loadLibrary("text_probe")
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        MethodChannel(engine.dartExecutor.binaryMessenger, "text_latency").setMethodCallHandler { call, result ->
            when (call.method) {
                "prepare" -> {
                    running = true
                    Choreographer.getInstance().postFrameCallback(this)
                    result.success(getExternalFilesDir(null)!!.absolutePath)
                }
                "stop" -> { running = false; result.success(null) }
                else -> result.notImplemented()
            }
        }
    }
    override fun doFrame(frameTime: Long) {
        trace(11, frameTime)
        trace(12, System.nanoTime())
        if (running) Choreographer.getInstance().postFrameCallback(this)
    }
}
