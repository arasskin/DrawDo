import 'dart:io';
import 'package:drawdo/data/app_views.dart';
import 'package:drawdo/data/datafriend.dart';
import 'package:drawdo/model/document.dart';
import 'benchmark_dart_phases.dart' show Measurements, require;

/// Matched user operations for the host Redux comparison. No Flutter rendering.
Future<void> main(List<String> args) async {
  final library = args[0], mode = args[1];
  final count = int.parse(args[2]), samples = int.parse(args[3]);
  final warmup = int.parse(args[4]);
  require(['edit', 'read', 'visible'].contains(mode), 'Invalid mode');
  require(count > 0 && samples > 0 && warmup >= 0, 'Invalid dimensions');
  final directory = Directory.systemTemp.createTempSync('datafriend-redux-');
  final db = Datafriend.open(
    directory.path,
    libraryPath: library,
    durable: false,
  );
  try {
    final views = AppViews(db)
      ..initialize(
        initial: AppDocument(
          tasks: [
            for (var i = 0; i < count; i++) Task(id: '$i', title: 'task $i'),
          ],
          stickers: AppDocument.initial().stickers,
          strokes: [
            Stroke(
              id: 'drawing',
              page: CanvasPage.space,
              color: 0xff112233,
              points: List.generate(
                5000,
                (i) => InkPoint(i / 5000, i.toDouble()),
              ),
            ),
          ],
        ),
      );
    var document = views.document;
    final retainedStroke = document.strokes.single;
    final retainedStickers = views.stickers;
    // Exercise odd/even edits before timing, as the Redux harness does.
    views.toggleTask('0');
    require(views.tasks.first.completed, 'Toggle did not publish');
    views.toggleTask('0');
    require(!views.tasks.first.completed, 'Second toggle did not publish');
    var notifications = 0, checksum = 0, completedCount = 0;
    var visibleDone = false;
    db.onPublished = () => notifications++;
    final results = Measurements('${mode}_$count', samples);
    results.run((i) {
      views.toggleTask('0');
      if (mode == 'read') {
        document = views.document;
        for (final task in document.tasks) {
          checksum +=
              task.title.length + task.id.length + (task.completed ? 1 : 0);
        }
      } else if (mode == 'visible') {
        final rows = views.tasks;
        completedCount = views.completedTasks;
        var index = 0;
        for (final task in rows.take(15)) {
          if (index == 0) visibleDone = task.completed;
          checksum +=
              task.title.length + task.id.length + (task.completed ? 1 : 0);
          index++;
        }
      }
    }, warmup: warmup);
    final iterations = warmup + samples, expectedDone = iterations.isOdd;
    require(notifications == iterations, 'Missing/duplicate notification');
    final current = views.document;
    require(current.tasks.length == count, 'Task count changed');
    for (var i = 0; i < count; i++) {
      final task = current.tasks[i];
      require(
        task.id == '$i' &&
            task.title == 'task $i' &&
            task.completed == (i == 0 && expectedDone),
        'Incorrect task/order at $i',
      );
    }
    require(
      identical(current.strokes.single, retainedStroke),
      'Drawing changed',
    );
    require(identical(views.stickers, retainedStickers), 'Stickers changed');
    if (mode != 'edit') {
      final rows = mode == 'read' ? count : (count < 15 ? count : 15);
      var base = 0;
      for (var i = 0; i < rows; i++) {
        base += 'task $i'.length + '$i'.length;
      }
      require(
        checksum == base * iterations + (iterations + 1) ~/ 2,
        'Read checksum',
      );
      if (mode == 'read') {
        require(
          document.tasks.first.completed == expectedDone,
          'Stale document',
        );
      } else {
        require(visibleDone == expectedDone, 'Stale visible row');
        require(completedCount == (expectedDone ? 1 : 0), 'Stale count');
      }
    }
    views.close();
    stdout.writeln(
      'workload,build,phase,kind,unit,samples,p50,p95,p99,max,mean',
    );
    results.report();
  } finally {
    db.close();
    directory.deleteSync(recursive: true);
  }
}
