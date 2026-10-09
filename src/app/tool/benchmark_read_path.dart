import 'dart:io';

import 'package:drawdo/data/app_views.dart';
import 'package:drawdo/data/datafriend.dart';
import 'package:drawdo/model/document.dart';

import 'benchmark_dart_phases.dart' show Measurements, require;

// Controlled read-only variants. Fresh app cases edit outside the read timer;
// the edit_* controls include the complete operation. No per-field timers.
void main(List<String> args) {
  final library = args[0], mode = args[1];
  final count = int.parse(args[2]), samples = int.parse(args[3]);
  final warmup = int.parse(args[4]);
  require(count > 0 && samples > 0 && warmup >= 0, 'Invalid dimensions');
  final dir = Directory.systemTemp.createTempSync('datafriend-read-path-');
  final db = Datafriend.open(dir.path, libraryPath: library, durable: false);
  try {
    final seed = [
      for (var i = 0; i < count; i++) Task(id: '$i', title: 'task $i'),
    ];
    final views = AppViews(db)..initialize(initial: AppDocument(tasks: seed));
    final subscription = db.subscribe(
      const DatafriendQuery([
        (QueryClause(QueryVariable(0), 'kind', 'task'), true),
        (QueryClause(QueryVariable(0), 'id', QueryVariable(1)), true),
        (QueryClause(QueryVariable(0), 'title', QueryVariable(2)), true),
        (QueryClause(QueryVariable(0), 'done', QueryVariable(3)), true),
        (QueryClause(QueryVariable(0), 'order', QueryVariable(4)), true),
      ]),
    );
    final result = subscription.result();
    final ids = result.entities(orderBy: 'order');
    final dartIds = ids.toList(growable: false);
    final encoded = [
      for (var i = 0; i < count; i++)
        [
          Triple(ids[i], 'id', seed[i].id),
          Triple(ids[i], 'title', seed[i].title),
          Triple(ids[i], 'done', false),
        ],
    ];
    var base = 0, visibleBase = 0, idsSum = 0;
    final visibleCount = count < 15 ? count : 15;
    for (var i = 0; i < count; i++) {
      require(result.value(ids[i], 'id') == '$i', 'Initial ID/order');
      require(result.value(ids[i], 'title') == 'task $i', 'Initial title');
      require(result.value(ids[i], 'done') == false, 'Initial done');
      idsSum += ids[i];
      base += seed[i].id.length + seed[i].title.length;
      if (i < visibleCount) {
        visibleBase += seed[i].id.length + seed[i].title.length;
      }
    }
    int consume(Iterable<Task> tasks) {
      var sum = 0;
      for (final task in tasks) {
        sum += task.id.length + task.title.length + (task.completed ? 1 : 0);
      }
      return sum;
    }

    require(consume(views.tasks) == base, 'Initial cached rows');
    var rows = views.tasks;
    List<Task> retained = const [];
    var notifications = 0, edits = 0, observed = 0, expected = 0;
    db.onPublished = () => notifications++;
    void edit() {
      views.toggleTask('0');
      edits++;
    }

    void Function()? prepare;
    late int Function() action;
    int Function() wanted = () => base;
    switch (mode) {
      case 'ids_native_iter':
        action = () {
          var sum = 0;
          for (final id in ids) {
            sum += id;
          }
          return sum;
        };
        wanted = () => idsSum;
      case 'ids_native_index':
        action = () {
          var sum = 0;
          for (var i = 0; i < ids.length; i++) {
            sum += ids[i];
          }
          return sum;
        };
        wanted = () => idsSum;
      case 'ids_dart_iter':
        action = () {
          var sum = 0;
          for (final id in dartIds) {
            sum += id;
          }
          return sum;
        };
        wanted = () => idsSum;
      case 'order_sorted':
      case 'order_unordered':
        action = () {
          final ordered = result.entities(
            orderBy: mode == 'order_sorted' ? 'order' : null,
          );
          final length = ordered.length;
          ordered.close();
          return length;
        };
        wanted = () => count;
      case 'probe_fields':
        action = () {
          var sum = 0;
          for (final id in ids) {
            sum += result.containsField(id, 'id') ? 1 : 0;
            sum += result.containsField(id, 'title') ? 1 : 0;
            sum += result.containsField(id, 'done') ? 1 : 0;
          }
          return sum;
        };
        wanted = () => count * 3;
      case 'decode_fields':
        action = () {
          var sum = 0;
          for (final id in ids) {
            sum += (result.value(id, 'id') as String).length;
            sum += (result.value(id, 'title') as String).length;
            sum += (result.value(id, 'done') as bool) ? 1 : 0;
          }
          return sum;
        };
      case 'decode_preencoded':
        action = () {
          var sum = 0;
          for (final fields in encoded) {
            sum += (fields[0].value as String).length;
            sum += (fields[1].value as String).length;
            sum += (fields[2].value as bool) ? 1 : 0;
          }
          return sum;
        };
      case 'construct_models':
        action = () {
          retained = [
            for (final task in seed)
              Task(id: task.id, title: task.title, completed: task.completed),
          ];
          return consume(retained);
        };
      case 'direct_models':
        action = () {
          retained = [
            for (final id in ids)
              Task(
                id: result.value(id, 'id') as String,
                title: result.value(id, 'title') as String,
                completed: result.value(id, 'done') as bool,
              ),
          ];
          return consume(retained);
        };
      case 'cached_app_read':
        action = () => consume(views.tasks);
      case 'cached_list_read':
        action = () => consume(seed);
      case 'native_count':
        action = () => views.completedTasks;
        wanted = () => 0;
      case 'fresh_order':
        prepare = edit;
        action = () => views.tasks.length;
        wanted = () => count;
      case 'fresh_materialize':
        prepare = () {
          edit();
          rows = views.tasks;
        };
        action = () => consume(rows);
        wanted = () => base + (edits.isOdd ? 1 : 0);
      case 'cached_export':
        action = () {
          retained = List<Task>.unmodifiable(rows);
          return consume(retained);
        };
      case 'fresh_app_read':
        prepare = edit;
        action = () => consume(views.tasks);
        wanted = () => base + (edits.isOdd ? 1 : 0);
      case 'fresh_document':
        prepare = edit;
        action = () => consume(views.document.tasks);
        wanted = () => base + (edits.isOdd ? 1 : 0);
      case 'fresh_visible':
        prepare = edit;
        action = () => consume(views.tasks.take(15)) + views.completedTasks;
        wanted = () => visibleBase + (edits.isOdd ? 2 : 0);
      case 'edit_only':
        action = () {
          edit();
          return 0;
        };
        wanted = () => 0;
      case 'edit_read':
        action = () {
          edit();
          return consume(views.document.tasks);
        };
        wanted = () => base + (edits.isOdd ? 1 : 0);
      case 'edit_visible':
        action = () {
          edit();
          return consume(views.tasks.take(15)) + views.completedTasks;
        };
        wanted = () => visibleBase + (edits.isOdd ? 2 : 0);
      default:
        throw ArgumentError('Unknown mode: $mode');
    }
    final measurements = Measurements('${mode}_$count', samples);
    final watch = Stopwatch();
    for (var i = 0; i < warmup + samples; i++) {
      prepare
          ?.call(); // Invalidating edits/order setup excluded where specified.
      watch.reset();
      watch.start();
      final value = action();
      watch.stop();
      if (i >= warmup) measurements.total[i - warmup] = watch.elapsedTicks;
      observed += value;
      expected += wanted();
      require(value == wanted(), 'Incorrect result on iteration $i: $value');
    }
    require(observed == expected, 'Aggregate checksum');
    require(notifications == edits, 'Notification count');
    require(consume(views.tasks) == base + (edits.isOdd ? 1 : 0), 'Final rows');
    if (retained.isNotEmpty) {
      require(
        retained.length == count && consume(retained) == base,
        'Retained models',
      );
    }
    for (var i = 0; i < count; i++) {
      final task = views.tasks[i];
      require(
        task.id == '$i' &&
            task.title == 'task $i' &&
            task.completed == (i == 0 && edits.isOdd),
        'Final task $i',
      );
    }
    ids.close();
    result.close();
    subscription.close();
    views.close();
    stdout.writeln(
      'workload,build,phase,kind,unit,samples,p50,p95,p99,max,mean',
    );
    measurements.report();
  } finally {
    db.close();
    dir.deleteSync(recursive: true);
  }
}
