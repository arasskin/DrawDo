import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:drawdo/data/datafriend.dart' as df;
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import 'comparison_screen.dart' as legacy;
import 'stores.dart';
import 'todo_screen.dart';
import 'bulk_query_screen.dart' as bulk;
import 'build_probe.dart';
import 'pure_flutter_screen.dart';

/// Drives the mounted checkbox callback in each UI, without pointer injection.
/// Cases use only the screen's subscriptions during measurement.
Future<void> runWidgetBenchmark(Map<String, dynamic> config) async {
  final count = config['count'] as int;
  final samples = config['samples'] as int;
  final warmup = config['warmup'] as int;
  final old = config['ui'] == 'legacy';
  final pure = config['ui'] == 'flutter';
  final rowQueries = config['ui'] == 'rows' || config['ui'] == 'inline';
  final workload = config['workload'] as String? ?? 'toggle';
  if (old && workload != 'toggle') {
    throw ArgumentError('Legacy supports toggle only');
  }
  if (!['toggle', 'title', 'order'].contains(workload)) {
    throw ArgumentError('Invalid workload');
  }
  final path = config['directory'] as String;
  final library =
      '${File(Platform.resolvedExecutable).parent.parent.path}/Frameworks/libdatafriend.dylib';
  DatafriendTodos? store;
  legacy.TodoController? controller;
  final df.Datafriend? database;
  final flutterKey = GlobalKey<PureFlutterScreenState>();
  void Function()? onPureEdited;
  var targetEntity = 0;
  if (pure) {
    database = null;
    targetEntity = 1;
  } else if (old) {
    store = DatafriendTodos.open(
      path,
      seed: fixture(count),
      libraryPath: library,
    );
    database = store.views.database;
    controller = legacy.TodoController(store);
  } else {
    database = df.Datafriend.open(path, libraryPath: library);
    final facts = <df.Triple>[];
    for (var i = 0; i < count; i++) {
      final id = database.newEntity();
      if (i == 0) targetEntity = id;
      facts.addAll([
        df.Triple(id, 'kind', 'task'),
        df.Triple(id, 'id', fixtureId(i)),
        df.Triple(id, 'title', 'Task $i'),
        df.Triple(id, 'done', false),
        df.Triple(id, 'order', i),
      ]);
    }
    database.edit(add: facts);
  }
  final key = GlobalKey();
  final probe = TodoBuildProbe();
  final initialFrame = Stopwatch()..start();
  runApp(
    MaterialApp(
      theme: todoTheme(),
      home: pure
          ? PureFlutterScreen(
              key: flutterKey,
              initialTasks: [
                for (var i = 0; i < count; i++)
                  FlutterTask(i + 1, 'Task $i', i),
              ],
              captureKey: key,
              onBuild: probe.record,
              onEdited: () => onPureEdited?.call(),
            )
          : old
          ? legacy.TodoScreen(
              controller: controller!,
              kind: StoreKind.datafriend,
              captureKey: key,
            )
          : rowQueries
          ? TodoScreen(
              database: database!,
              captureKey: key,
              onBuild: probe.record,
            )
          : bulk.TodoScreen(
              database: database!,
              captureKey: key,
              onBuild: probe.record,
            ),
    ),
  );
  await WidgetsBinding.instance.endOfFrame;
  final initialFrameUs = initialFrame.elapsedMicroseconds;
  await Future<void>.delayed(const Duration(milliseconds: 500));
  final initiallyBuiltRows = probe.rows.length;

  bool hasText(String text) {
    var found = false;
    void visit(Element element) {
      if (element.widget case Text(data: final data) when data == text) {
        found = true;
      }
      if (!found) element.visitChildElements(visit);
    }

    visit(key.currentContext! as Element);
    return found;
  }

  Checkbox checkbox() {
    Checkbox? found;
    void visit(Element e) {
      if (found != null) return;
      if (e.widget is Checkbox) {
        found = e.widget as Checkbox;
        return;
      }
      e.visitChildElements(visit);
    }

    visit(key.currentContext! as Element);
    if (found == null || found!.onChanged == null) {
      throw StateError('No enabled checkbox');
    }
    return found!;
  }

  final rows = <Map<String, Object>>[];
  final timings = <int, FrameTiming>{};
  final received = Completer<void>();
  Set<int>? wanted;
  void record(List<FrameTiming> values) {
    for (final value in values) {
      timings[value.frameNumber] = value;
    }
    if (wanted != null &&
        wanted.every(timings.containsKey) &&
        !received.isCompleted) {
      received.complete();
    }
  }

  SchedulerBinding.instance.addTimingsCallback(record);
  for (var i = 0; i < samples + warmup; i++) {
    final control = checkbox(); // Locate outside the measured action.
    final target = !control.value!;
    final title = i.isEven ? 'Updated task 0' : 'Task 0';
    final order = i.isEven ? 2 : 0;
    final finished = Completer<void>();
    void changed() {
      if (!controller!.busy && !finished.isCompleted) finished.complete();
    }

    controller?.addListener(changed);
    final prior = database?.onPublished;
    probe.reset();
    int? published;
    final clock = Stopwatch();
    database?.onPublished = () {
      prior?.call();
      published = clock.elapsedMicroseconds;
    };
    onPureEdited = () => published = clock.elapsedMicroseconds;
    clock.start();
    switch (workload) {
      case 'toggle':
        control.onChanged!(target);
      case 'title':
        if (pure) {
          flutterKey.currentState!.renameTask(targetEntity, title);
        } else {
          database!.edit(add: [df.Triple(targetEntity, 'title', title)]);
        }
      case 'order':
        if (pure) {
          flutterKey.currentState!.setOrder(targetEntity, order);
        } else {
          database!.edit(add: [df.Triple(targetEntity, 'order', order)]);
        }
    }
    final call = clock.elapsedMicroseconds;
    if (controller != null) {
      await finished.future.timeout(const Duration(seconds: 5));
    }
    final ready = clock.elapsedMicroseconds;
    database?.onPublished = prior;
    onPureEdited = null;
    controller?.removeListener(changed);
    if (published == null || controller?.error != null) {
      throw StateError('Edit did not publish: ${controller?.error}');
    }
    int? start, end, frame;
    SchedulerBinding.instance.scheduleFrameCallback((_) {
      start = clock.elapsedMicroseconds;
    });
    SchedulerBinding.instance.addPostFrameCallback((_) {
      end = clock.elapsedMicroseconds;
      frame = ui.PlatformDispatcher.instance.frameData.frameNumber;
    });
    await WidgetsBinding.instance.endOfFrame;
    final total = clock.elapsedMicroseconds;
    if (start == null || end == null || frame == null) {
      throw StateError('Missing frame callbacks');
    }
    if (workload == 'toggle' && checkbox().value != target) {
      throw StateError('Rendered checkbox is stale');
    }
    if (workload == 'title' && !hasText(title)) {
      throw StateError('Rendered title is stale');
    }
    if (!hasText(
      '${workload == 'toggle' && target ? 1 : 0} of $count complete',
    )) {
      throw StateError('Rendered completion count is stale');
    }
    if (workload == 'order') {
      String? firstTitle;
      void visit(Element e) {
        if (firstTitle != null) return;
        if (e.widget case ListTile(title: Text(data: final text))) {
          firstTitle = text;
        }
        if (firstTitle == null) e.visitChildElements(visit);
      }

      visit(key.currentContext! as Element);
      if (firstTitle != (order == 2 ? 'Task 1' : 'Task 0')) {
        throw StateError('Rendered order is stale');
      }
    }
    if (pure) {
      if (probe.parts['screen'] != 1 ||
          probe.parts['list'] != 1 ||
          probe.parts['counter'] != 1 ||
          probe.rows.length != initiallyBuiltRows ||
          probe.rows.values.any((builds) => builds != 1)) {
        throw StateError(
          'Unexpected setState rebuild scope: ${probe.snapshot()}',
        );
      }
    } else if (rowQueries) {
      if ((probe.parts['screen'] ?? 0) != 0) {
        throw StateError('Unexpected whole screen rebuild');
      }
      if (workload != 'order') {
        final expectedCounter = workload == 'toggle' ? 1 : 0;
        if ((probe.parts['list'] ?? 0) != 0 ||
            (probe.parts['counter'] ?? 0) != expectedCounter ||
            probe.rows.length != 1 ||
            probe.rows[targetEntity] != 1) {
          throw StateError('Unexpected row rebuild scope: ${probe.snapshot()}');
        }
      } else if (probe.parts['list'] != 1 ||
          (probe.parts['counter'] ?? 0) != 0) {
        throw StateError('Unexpected reorder scope: ${probe.snapshot()}');
      }
    }
    if (i >= warmup) {
      rows.add({
        'call_us': call,
        'publish_us': published!,
        'data_ready_us': ready,
        'wait_for_frame_us': start! - ready,
        'framework_frame_us': end! - start!,
        'post_frame_resume_us': total - end!,
        'post_frame_us': total,
        'frame_number': frame!,
        ...probe.snapshot(),
      });
    }
    await Future<void>.delayed(const Duration(milliseconds: 250));
  }
  wanted = rows.map((r) => r['frame_number'] as int).toSet();
  record(const []);
  await received.future.timeout(const Duration(seconds: 5));
  SchedulerBinding.instance.removeTimingsCallback(record);
  for (final row in rows) {
    final frame = timings[row['frame_number']]!;
    row['build_us'] = frame.buildDuration.inMicroseconds;
    row['raster_us'] = frame.rasterDuration.inMicroseconds;
  }
  if (pure) {
    final tasks = flutterKey.currentState!.tasks;
    if (tasks.length != count) throw StateError('Wrong task count');
    final lastEven = (samples + warmup - 1).isEven;
    for (var i = 0; i < count; i++) {
      final task = tasks[i];
      if (task.id != i + 1 ||
          task.title !=
              (i == 0 && workload == 'title' && lastEven
                  ? 'Updated task 0'
                  : 'Task $i') ||
          task.order != (i == 0 && workload == 'order' && lastEven ? 2 : i) ||
          task.done != (i == 0 && workload == 'toggle' && lastEven)) {
        throw StateError('Wrong final in-memory task $i');
      }
    }
  }
  runApp(const SizedBox.shrink());
  await WidgetsBinding.instance.endOfFrame;
  controller?.dispose();

  // Validation subscriptions exist only after the measured widgets unmount.
  String state(df.Datafriend db) {
    final all = db.subscribe(const df.DatafriendQuery.all());
    try {
      final facts = all.toList()
        ..sort((a, b) {
          final order = a.entity.compareTo(b.entity);
          return order != 0 ? order : a.attribute.compareTo(b.attribute);
        });
      if (facts.where((t) => t.attribute == 'title').length != count) {
        throw StateError('Wrong task count');
      }
      final done = facts
          .where((t) => t.attribute == 'done' && t.value == true)
          .length;
      if (done != (workload == 'toggle' ? (samples + warmup) % 2 : 0)) {
        throw StateError('Wrong final completion count');
      }
      return jsonEncode([
        for (final t in facts) [t.entity, t.attribute, t.value],
      ]);
    } finally {
      all.close();
    }
  }

  if (database != null) {
    final expected = state(database);
    if (store != null) {
      await store.close();
    } else {
      database.close();
    }
    final reopened = df.Datafriend.open(path, libraryPath: library);
    try {
      if (state(reopened) != expected) throw StateError('Reopen mismatch');
    } finally {
      reopened.close();
    }
  }
  await File(config['output'] as String).writeAsString(
    jsonEncode({
      'ui': config['ui'],
      'count': count,
      'samples': samples,
      'warmup': warmup,
      'workload': workload,
      'initial_frame_us': initialFrameUs,
      'initially_built_rows': initiallyBuiltRows,
      'checks': pure
          ? 'rendered updates and counts passed'
          : 'rendered updates, counts and persistence passed',
      'persistence_checks': pure
          ? 'not applicable: in memory'
          : 'exact reopen passed',
      'rebuild_checks': rowQueries || pure
          ? 'per-action row/list/counter scope passed'
          : 'counts recorded',
      'rows': rows,
    }),
  );
}
