import Flutter
import UIKit

@main
@objc class AppDelegate: FlutterAppDelegate, FlutterImplicitEngineDelegate {
  override func application(_ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
    application.isIdleTimerDisabled = true
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }
  func didInitializeImplicitFlutterEngine(_ engineBridge: FlutterImplicitEngineBridge) {
    GeneratedPluginRegistrant.register(with: engineBridge.pluginRegistry)
    let registrar = engineBridge.pluginRegistry.registrar(forPlugin: "TextLatency")!
    FlutterMethodChannel(name: "text_latency", binaryMessenger: registrar.messenger()).setMethodCallHandler { call, result in
      if call.method == "prepare" {
        let path = NSSearchPathForDirectoriesInDomains(.documentDirectory, .userDomainMask, true)[0] + "/timing"
        try! FileManager.default.createDirectory(atPath: path, withIntermediateDirectories: true)
        path.withCString { timing_capture_start($0) }
        result(path)
      } else if call.method == "stop" {
        timing_capture_stop()
        result(nil)
      } else { result(FlutterMethodNotImplemented) }
    }
  }
}
