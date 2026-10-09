package dev.drawdo.drawdo

import io.flutter.embedding.android.FlutterActivity

class MainActivity : FlutterActivity() {
    override fun configureFlutterEngine(flutterEngine: io.flutter.embedding.engine.FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        io.flutter.plugin.common.MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "drawdo/particle-demo")
            .setMethodCallHandler { call, result ->
                if (call.method == "config") result.success(intent.getStringExtra("particle_config"))
                else result.notImplemented()
            }
    }
    override fun onCreate(savedInstanceState: android.os.Bundle?) {
        dev.drawdo.datafriend.IdentityStore.install(this)
        super.onCreate(savedInstanceState)
    }
}
