import 'dart:io';
import 'package:drawdo/data/datafriend.dart';
import 'support/legacy_document_store.dart';
import 'package:drawdo/model/document.dart';

void report(String workload, List<int> times) {
  times.sort();
  stdout.writeln(
    '$workload,${times[times.length ~/ 2]},${times[times.length * 95 ~/ 100]},${times[times.length * 99 ~/ 100]},${times.last}',
  );
}

Future<void> main(List<String> args) async {
  final library = args.first;
  final durable = args.length < 2 || args[1] != '0';
  stdout.writeln('workload,p50_us,p95_us,p99_us,max_us');
  final dir = Directory.systemTemp.createTempSync('datafriend-dart-bench-');
  final db = Datafriend.open(dir.path, libraryPath: library, durable: durable);
  final all = db.subscribe(const DatafriendQuery.all());
  all.addAll([
    for (var i = 1; i <= 10000; i++) ...[
      Triple(i, 'group', i % 100),
      Triple(i, 'done', false),
      Triple(i, 'title', 'task'),
    ],
  ]);
  all.close();
  final views = [
    for (var i = 0; i < 10; i++)
      db.subscribe(
        const DatafriendQuery([
          (QueryClause(QueryVariable(0), 'group', 1), false),
          (QueryClause(QueryVariable(0), 'done', QueryVariable(1)), true),
          (QueryClause(QueryVariable(0), 'title', QueryVariable(2)), true),
        ]),
      ),
  ];
  final times = <int>[];
  for (var i = 0; i < 250; i++) {
    final watch = Stopwatch()..start();
    views.first.add(Triple(1, 'done', i.isEven));
    // Consume every owner's published view, as a Dart rebuild would.
    for (final v in views) {
      v.toList(growable: false);
    }
    watch.stop();
    if (i >= 50) times.add(watch.elapsedMicroseconds);
  }
  report('10000 entities/10 owners/write+read', times);
  for (final v in views) {
    v.close();
  }
  db.close();
  dir.deleteSync(recursive: true);

  final appDir = Directory.systemTemp.createTempSync('datafriend-app-bench-');
  final store = DatafriendDocumentStore(
    Datafriend.open(appDir.path, libraryPath: library, durable: durable),
  );
  await store.load();
  var doc = store.commit(
    AppDocument(
      tasks: [for (var i = 0; i < 100; i++) Task(id: '$i', title: 'task $i')],
      strokes: [
        Stroke(
          id: 'drawing',
          page: CanvasPage.space,
          color: 0xff112233,
          points: List.generate(5000, (i) => InkPoint(i / 5000, i.toDouble())),
        ),
      ],
      stickers: AppDocument.initial().stickers,
    ),
  );
  times.clear();
  for (var i = 0; i < 250; i++) {
    final watch = Stopwatch()..start();
    doc = store.commit(
      doc.copyWith(
        tasks: [
          doc.tasks.first.copyWith(completed: i.isEven),
          ...doc.tasks.skip(1),
        ],
      ),
    );
    watch.stop();
    if (i >= 50) times.add(watch.elapsedMicroseconds);
  }
  report('app/100 tasks+5000 points/commit+document', times);
  store.close();
  appDir.deleteSync(recursive: true);
}
