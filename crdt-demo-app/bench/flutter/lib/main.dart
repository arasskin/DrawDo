import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'dart:ui' show FramePhase;
import 'dart:developer' as developer;
import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

final library = Platform.isAndroid
    ? DynamicLibrary.open('libtext_probe.so')
    : DynamicLibrary.process();
final clock = library.lookupFunction<Uint64 Function(), int Function()>(
  'text_trace_clock',
);
final record = library
    .lookupFunction<
      Void Function(Uint32, Uint32, Uint64),
      void Function(int, int, int)
    >('text_trace_record');
final start = library
    .lookupFunction<
      Int32 Function(Pointer<NativeFunction<Void Function(Uint32)>>),
      int Function(Pointer<NativeFunction<Void Function(Uint32)>>)
    >('bench_start');
final save = library
    .lookupFunction<Int32 Function(Pointer<Utf8>), int Function(Pointer<Utf8>)>(
      'bench_save',
    );
const channel = MethodChannel('text_latency');
int tag = 0;

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const MaterialApp(debugShowCheckedModeBanner: false, home: Editor()));
}

class Editor extends StatefulWidget {
  const Editor({super.key});
  @override
  State<Editor> createState() => EditorState();
}

class EditorState extends State<Editor> {
  final controller = TextEditingController();
  final focus = FocusNode();
  late NativeCallable<Void Function(Uint32)> callback;
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => begin());
  }

  Future<void> begin() async {
    final path = await channel.invokeMethod<String>('prepare');
    WidgetsBinding.instance.addPersistentFrameCallback((_) {
      record(tag, 6, clock());
    });
    record(999997, 40, clock());
    record(999997, 41, developer.Timeline.now * 1000);
    record(999997, 42, clock());
    WidgetsBinding.instance.addTimingsCallback((frames) {
      for (final frame in frames) {
        final number = 800000 + frame.frameNumber;
        for (final pair in [
          (20, FramePhase.vsyncStart),
          (21, FramePhase.buildStart),
          (22, FramePhase.buildFinish),
          (23, FramePhase.rasterStart),
          (24, FramePhase.rasterFinish),
        ]) {
          record(
            number,
            pair.$1,
            frame.timestampInMicroseconds(pair.$2) * 1000,
          );
        }
      }
    });
    final initial = StringBuffer('LB300000\n');
    for (int i = 9; i < 1000; ++i) {
      initial.write(i % 51 == 0 ? '\n' : 'x');
    }
    controller.value = TextEditingValue(
      text: initial.toString(),
      selection: const TextSelection.collapsed(offset: 0),
    );
    callback = NativeCallable<Void Function(Uint32)>.listener((int next) {
      record(next, 3, clock());
      final text = StringBuffer('LB${next.toString().padLeft(6, '0')}\n');
      for (int i = 9; i < 1000; ++i) {
        text.write(i % 51 == 0 ? '\n' : 'x');
      }
      tag = next;
      record(next, 4, clock());
      controller.value = TextEditingValue(
        text: text.toString(),
        selection: const TextSelection.collapsed(offset: 0),
      );
      record(next, 5, clock());
    });
    if (start(callback.nativeFunction) != 0)
      throw StateError('Cannot start native producer');
    Timer(const Duration(seconds: 55), () async {
      final filename = '$path/events.csv'.toNativeUtf8();
      final error = save(filename);
      calloc.free(filename);
      await channel.invokeMethod<void>('stop');
      if (error != 0) throw StateError('Cannot save trace: $error');
    });
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    backgroundColor: Colors.white,
    body: SafeArea(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: EditableText(
          controller: controller,
          focusNode: focus,
          style: const TextStyle(color: Colors.black, fontSize: 18),
          cursorColor: Colors.black,
          backgroundCursorColor: Colors.grey,
          selectionColor: const Color(0x336699ff),
          maxLines: null,
          expands: true,
          autofocus: false,
          showCursor: false,
          textAlign: TextAlign.left,
          keyboardType: TextInputType.multiline,
        ),
      ),
    ),
  );
}
