import 'dart:io';
import 'package:drawdo/data/app_views.dart';
import 'package:drawdo/data/datafriend.dart';
import 'package:drawdo/model/document.dart';
import 'benchmark_dart_phases.dart' show Measurements, require;
import 'support/legacy_document_store.dart';

Future<void> main(List<String> args) async {
  final library = args[0], mode = args[1];
  final count = int.parse(args[2]), samples = int.parse(args[3]);
  require(
    ['legacy', 'direct_edit', 'direct_read', 'direct_visible'].contains(mode),
    'Invalid mode',
  );
  final directory = Directory.systemTemp.createTempSync('datafriend-direct-');
  final db = Datafriend.open(
    directory.path,
    libraryPath: library,
    durable: false,
  );
  final initial = AppDocument(
    tasks: [for (var i = 0; i < count; i++) Task(id: '$i', title: 'task $i')],
    stickers: AppDocument.initial().stickers,
    strokes: [
      Stroke(
        id: 'drawing',
        page: CanvasPage.space,
        color: 0xff112233,
        points: List.generate(5000, (i) => InkPoint(i / 5000, i.toDouble())),
      ),
    ],
  );
  final results = Measurements('${mode}_$count', samples);
  stdout.writeln('workload,build,phase,kind,unit,samples,p50,p95,p99,max,mean');
  try {
    var notifications = 0;
    if (mode == 'legacy') {
      final store = DatafriendDocumentStore(db);
      await store.load();
      var document = store.commit(initial);
      final retainedStroke = document.strokes.single;
      db.onPublished = () => notifications++;
      results.run((i) {
        document = store.commit(
          document.copyWith(
            tasks: [
              document.tasks.first.copyWith(completed: i.isEven),
              ...document.tasks.skip(1),
            ],
          ),
        );
      });
      require(
        document.tasks.first.completed == (samples + 99).isEven,
        'Legacy result',
      );
      require(
        identical(document.strokes.single, retainedStroke),
        'Legacy drawing changed',
      );
      store.close();
    } else {
      final views = AppViews(db)..initialize(initial: initial);
      var document = views.document;
      final retainedStroke = document.strokes.single,
          id = document.tasks.first.id;
      db.onPublished = () => notifications++;
      var visibleCount = 0,
          completedCount = 0,
          visibleDone = false,
          checksum = 0;
      results.run((i) {
        views.toggleTask(id);
        if (mode == 'direct_read') document = views.document;
        if (mode == 'direct_visible') {
          final rows = views.tasks;
          completedCount = views.completedTasks;
          visibleCount = 0;
          for (final task in rows.take(15)) {
            if (visibleCount == 0) visibleDone = task.completed;
            checksum +=
                task.title.length + task.id.length + (task.completed ? 1 : 0);
            visibleCount++;
          }
        }
      });
      if (mode == 'direct_visible') {
        require(
          visibleCount == (count < 15 ? count : 15) && checksum > 0,
          'Visible rows not consumed',
        );
        require(
          visibleDone == (samples + 99).isEven &&
              completedCount == ((samples + 99).isEven ? 1 : 0),
          'Visible state/count stale',
        );
      }
      final current = views.document;
      require(
        current.tasks.length == count &&
            current.tasks.first.completed == (samples + 99).isEven,
        'Direct result',
      );
      require(
        current.tasks.skip(1).every((t) => !t.completed),
        'Other tasks changed',
      );
      require(
        identical(current.strokes.single, retainedStroke),
        'Direct drawing changed',
      );
      if (mode == 'direct_read') {
        require(
          document.toJson().toString() == current.toJson().toString(),
          'Readback stale',
        );
      }
      views.close();
    }
    require(
      notifications == samples + 100,
      'Missing or duplicate publication notification',
    );
    results.report();
  } finally {
    db.close();
    directory.deleteSync(recursive: true);
  }
}
