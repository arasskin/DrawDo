import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/scheduler.dart';
import 'change.dart';
import 'stores.dart';
import 'todo_screen.dart';
import 'comparison_screen.dart' as comparison;

void require(bool condition, String message) {
  if (!condition) throw StateError(message);
}

int consume(TodoStore store, int limit) {
  var sum = store.completed;
  for (final task in store.tasks.take(limit)) {
    sum += task.id.length + task.title.length + (task.completed ? 1 : 0);
  }
  return sum;
}

Future<void> capture(GlobalKey key, String path) async {
  await WidgetsBinding.instance.endOfFrame;
  final boundary =
      key.currentContext!.findRenderObject()! as RenderRepaintBoundary;
  final image = await boundary.toImage(pixelRatio: 1.5);
  final bytes = await image.toByteData(format: ui.ImageByteFormat.png);
  await File(path).writeAsBytes(bytes!.buffer.asUint8List());
  image.dispose();
}

Future<void> runBenchmark(Map<String, dynamic> config) async {
  final kind = StoreKind.values.byName(config['engine'] as String);
  final count = config['count'] as int;
  final samples = config['samples'] as int? ?? 30;
  final warmup = config['warmup'] as int? ?? 5;
  final mode = config['mode'] as String? ?? 'store';
  final directory = config['directory'] as String;
  final output = config['output'] as String;
  final durable = config['durable'] as bool? ?? false;
  final durability = config['durability'] == null
      ? null
      : DatafriendDurability.values.byName(config['durability'] as String);
  final setupClock = Stopwatch()..start();
  var store = await TodoStore.open(
    kind,
    directory,
    seed: fixture(count),
    durable: durable,
    durability: durability,
  );
  final setupUs = setupClock.elapsedMicroseconds;
  final settings = store.settings;
  final rows = <Map<String, Object?>>[];
  final controller = comparison.TodoController(store);
  final key = GlobalKey();
  runApp(
    MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: todoTheme(),
      home: mode == 'store'
          ? const Scaffold(
              body: Center(child: Text('Running store benchmarks…')),
            )
          : store is DatafriendTodos
          ? TodoScreen(database: store.views.database, captureKey: key)
          : comparison.TodoScreen(
              controller: controller,
              kind: kind,
              captureKey: key,
            ),
    ),
  );
  await WidgetsBinding.instance.endOfFrame;
  consume(store, count);
  // Drain the seed's scheduled query refresh and initial Flutter setup.
  await Future<void>.delayed(const Duration(milliseconds: 500));
  consume(store, count);
  if (mode == 'capture') {
    await capture(key, config['screenshot'] as String);
  } else if (mode == 'frames') {
    final timings = <FrameTiming>[];
    final reported = <int>{};
    Set<int>? expectedFrames;
    final allReported = Completer<void>();
    void record(List<FrameTiming> frames) {
      timings.addAll(frames);
      reported.addAll(frames.map((f) => f.frameNumber));
      if (expectedFrames != null &&
          reported.containsAll(expectedFrames) &&
          !allReported.isCompleted) {
        allReported.complete();
      }
    }

    SchedulerBinding.instance.addTimingsCallback(record);
    for (var i = 0; i < samples + warmup; i++) {
      final clock = Stopwatch()..start();
      await controller.toggle(store.tasks.first);
      if (controller.error != null) throw StateError(controller.error!);
      final readyUs = clock.elapsedMicroseconds;
      int? frameStartUs;
      int? frameNumber;
      int? postFrameUs;
      SchedulerBinding.instance.scheduleFrameCallback((_) {
        frameStartUs = clock.elapsedMicroseconds;
      });
      SchedulerBinding.instance.addPostFrameCallback((_) {
        postFrameUs = clock.elapsedMicroseconds;
        // Flutter updates frameData after dispatching onBeginFrame; reading it
        // in the transient frame-start callback would select the previous frame.
        frameNumber = ui.PlatformDispatcher.instance.frameData.frameNumber;
      });
      await WidgetsBinding.instance.endOfFrame;
      final frameEndUs = clock.elapsedMicroseconds;
      require(
        frameStartUs != null && postFrameUs != null,
        'Missing frame callbacks',
      );
      if (i >= warmup) {
        rows.add({
          'workload': 'toggle_frame',
          ...controller.last!.toJson(),
          'post_frame_us': frameEndUs,
          'data_ready_us': readyUs,
          'wait_for_frame_us': frameStartUs! - readyUs,
          'framework_frame_us': postFrameUs! - frameStartUs!,
          'post_frame_resume_us': frameEndUs - postFrameUs!,
          'frame_number': frameNumber,
        });
      }
      await Future<void>.delayed(const Duration(milliseconds: 250));
      if (i == warmup - 1) timings.clear();
    }
    // Release builds batch timing reports for up to one second. Wait for the
    // measured frame IDs, not an arbitrary delay that can omit the last frame.
    expectedFrames = rows.map((r) => r['frame_number'] as int).toSet();
    record(const []);
    await allReported.future.timeout(const Duration(seconds: 5));
    SchedulerBinding.instance.removeTimingsCallback(record);
    require(timings.isNotEmpty, 'No rendered frames were captured');
    final byNumber = {for (final t in timings) t.frameNumber: t};
    for (final row in rows.where((r) => r['workload'] == 'toggle_frame')) {
      final frame = byNumber[row['frame_number']];
      require(
        frame != null,
        'Updated frame timing was not captured: '
        '${row['frame_number']}; recorded=${byNumber.keys.toList()}',
      );
      row['update_build_us'] = frame!.buildDuration.inMicroseconds;
      row['update_raster_us'] = frame.rasterDuration.inMicroseconds;
    }
    for (final timing in timings) {
      rows.add({
        'workload': 'frame',
        'build_us': timing.buildDuration.inMicroseconds,
        'raster_us': timing.rasterDuration.inMicroseconds,
        'total_us': timing.totalSpan.inMicroseconds,
      });
    }
    if (config['screenshot'] != null) {
      await capture(key, config['screenshot'] as String);
    }
  } else {
    for (final workload in [
      'toggle_visible',
      'toggle_full',
      if (config['paced'] == true) 'paced_visible',
    ]) {
      final limit = workload == 'toggle_full' ? count : 15;
      final length = count < limit ? count : limit;
      final base = fixture(
        count,
      ).take(length).fold<int>(0, (n, t) => n + t.id.length + t.title.length);
      for (var i = 0; i < samples + warmup; i++) {
        if (workload == 'paced_visible') {
          await Future<void>.delayed(const Duration(milliseconds: 16));
        }
        final target = !store.tasks.first.completed;
        final timing = await change(
          store,
          () => store.toggle(fixtureId(0)),
          () => store.tasks.first.completed == target,
        );
        final readClock = Stopwatch()..start();
        final checksum = consume(store, limit);
        final readUs = readClock.elapsedMicroseconds;
        require(checksum == base + (target ? 2 : 0), 'Read checksum');
        if (i >= warmup) {
          rows.add({
            'workload': workload,
            ...timing.toJson(),
            'read_us': readUs,
            'publications': store.publications,
          });
        }
      }
    }
    for (var i = 0; i < 1100; i++) {
      final watch = Stopwatch()..start();
      final sum = consume(store, count);
      final elapsed = watch.elapsedMicroseconds;
      require(sum > 0, 'Cached checksum');
      if (i >= 100) rows.add({'workload': 'cached_full', 'read_us': elapsed});
    }
  }
  runApp(const SizedBox.shrink());
  await WidgetsBinding.instance.endOfFrame;
  controller.dispose();
  // Real CRUD and persistence checks, outside benchmark timers.
  final added = fixtureId(count + 10);
  await change(
    store,
    () => store.insert(added, 'Added task', count + 1),
    () => store.tasks.length == count + 1,
  );
  await change(
    store,
    () => store.rename(added, 'Renamed task'),
    () => store.tasks.last.title == 'Renamed task',
  );
  await change(
    store,
    () => store.remove(added),
    () => store.tasks.length == count,
  );
  final expected = store.tasks.map((t) => t.toJson()).toList();
  final flushClock = Stopwatch()..start();
  if (store is DatafriendTodos) store.views.database.flush();
  final flushUs = flushClock.elapsedMicroseconds;
  await store.close();
  final reopen = Stopwatch()..start();
  store = await TodoStore.open(
    kind,
    directory,
    durable: durable,
    durability: durability,
  );
  final reopened = store.tasks.map((t) => t.toJson()).toList();
  final reopenUs = reopen.elapsedMicroseconds;
  require(
    jsonEncode(reopened) == jsonEncode(expected),
    'Persistence/reopen mismatch',
  );
  await store.close();
  await File(output).writeAsString(
    jsonEncode({
      'engine': kind.name,
      'package': kind == StoreKind.instantdb
          ? 'flutter_instantdb 2.3.0'
          : 'Datafriend',
      'mode': mode,
      'count': count,
      'durable': durable,
      'durability': durability?.name,
      'flush_barrier_us': flushUs,
      'samples': samples,
      'warmup': warmup,
      'setup_us': setupUs,
      'reopen_read_us': reopenUs,
      'settings': settings,
      'checks': 'CRUD, checksums and persistence passed',
      'rows': rows,
    }),
  );
}
