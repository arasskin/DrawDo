import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:drawdo/app_controller.dart';
import 'package:drawdo/data/datafriend.dart';
import 'package:drawdo/data/app_views.dart';
import 'package:drawdo/model/document.dart';

String get datafriendLibrary =>
    Platform.environment['DATAFRIEND_LIBRARY'] ??
    '${Directory.current.path}/../../datafriend/build/native/libdatafriend.${Platform.isMacOS ? 'dylib' : 'so'}';

Future<AppController> nativeApp({AppDocument? initial}) async {
  final directory = Directory.systemTemp.createTempSync('drawdo-ui-');
  final controller = AppController(
    AppViews(
      Datafriend.open(
        directory.path,
        libraryPath: datafriendLibrary,
        durable: false,
      ),
    ),
  );
  addTearDown(() {
    controller.dispose();
    directory.deleteSync(recursive: true);
  });
  await controller.load(initial: initial);
  return controller;
}
