import 'dart:io';
import 'package:drawdo/data/datafriend.dart';
import 'support/legacy_document_store.dart';
import 'package:drawdo/data/datafriend_profile.dart';
import 'package:drawdo/model/document.dart';

void require(bool condition, String message) {
  if (!condition) throw StateError(message);
}

final class Measurements {
  Measurements(this.workload, this.samples)
    : total = List<int>.filled(samples, 0),
      inclusive = List.generate(
        DatafriendPhase.values.length,
        (_) => List<int>.filled(samples, 0),
      ),
      exclusive = List.generate(
        DatafriendPhase.values.length,
        (_) => List<int>.filled(samples, 0),
      ),
      counts = List.generate(
        DatafriendCounter.values.length,
        (_) => List<int>.filled(samples, 0),
      );
  final String workload;
  final int samples;
  final List<int> total;
  final List<List<int>> inclusive, exclusive, counts;
  void run(void Function(int) action, {int warmup = 100}) {
    final watch = Stopwatch();
    for (var i = 0; i < warmup + samples; i++) {
      if (datafriendProfiling) DatafriendProfile.reset();
      watch.reset();
      watch.start();
      if (datafriendProfiling) DatafriendProfile.begin();
      action(i);
      if (datafriendProfiling) DatafriendProfile.end(DatafriendPhase.action);
      watch.stop();
      if (datafriendProfiling) DatafriendProfile.checkComplete();
      if (i < warmup) continue;
      final at = i - warmup;
      total[at] = watch.elapsedTicks;
      if (datafriendProfiling) {
        for (final p in DatafriendPhase.values) {
          inclusive[p.index][at] = DatafriendProfile.inclusive[p.index];
          exclusive[p.index][at] = DatafriendProfile.exclusive[p.index];
        }
        for (final c in DatafriendCounter.values) {
          counts[c.index][at] = DatafriendProfile.counters[c.index];
        }
      }
    }
  }

  void report() {
    void row(String phase, String kind, List<int> values, String unit) {
      final sorted = [...values]..sort();
      final scale = unit == 'us' ? 1000000 / Stopwatch().frequency : 1.0;
      String at(int index) => (sorted[index] * scale).toStringAsFixed(3);
      final mean = values.reduce((a, b) => a + b) / samples * scale;
      stdout.writeln(
        '$workload,${datafriendProfiling ? 'profile' : 'control'},$phase,$kind,$unit,$samples,${at(samples ~/ 2)},${at(samples * 95 ~/ 100)},${at(samples * 99 ~/ 100)},${at(samples - 1)},${mean.toStringAsFixed(3)}',
      );
    }

    row('total', 'wall', total, 'us');
    if (!datafriendProfiling) return;
    for (final p in DatafriendPhase.values) {
      if (inclusive[p.index].every((x) => x == 0)) continue;
      row(p.name, 'inclusive', inclusive[p.index], 'us');
      row(p.name, 'exclusive', exclusive[p.index], 'us');
    }
    for (final c in DatafriendCounter.values) {
      row(c.name, 'counter', counts[c.index], 'count');
    }
  }
}

Future<void> main(List<String> args) async {
  final library = args[0], workload = args[1];
  final samples = args.length > 2 ? int.parse(args[2]) : 1000;
  final entities = args.length > 3 ? int.parse(args[3]) : 10000;
  require(samples > 0 && entities >= 100, 'Invalid dimensions');
  final dir = Directory.systemTemp.createTempSync('datafriend-dart-phases-');
  final db = Datafriend.open(dir.path, libraryPath: library, durable: false);
  final measurements = Measurements(workload, samples);
  stdout.writeln('workload,build,phase,kind,unit,samples,p50,p95,p99,max,mean');
  try {
    if (workload.startsWith('views_')) {
      final mixed = workload.contains('mixed');
      final editOnly = workload.endsWith('edit_only');
      final add = workload.contains('_add_');
      final cached = workload.endsWith('snapshot_reuse');
      final all = db.subscribe(const DatafriendQuery.all());
      all.addAll([
        for (var i = 1; i <= entities; i++) ...[
          Triple(i, 'group', i % 100),
          Triple(i, 'done', false),
          Triple(i, 'title', 'task'),
        ],
      ]);
      all.close();
      final groups = [
        for (var i = 0; i < 10; i++) 1,
        if (mixed)
          for (var i = 2; i <= 11; i++) i,
      ];
      final views = [
        for (final group in groups)
          db.subscribe(
            DatafriendQuery([
              (QueryClause(const QueryVariable(0), 'group', group), false),
              (
                const QueryClause(QueryVariable(0), 'done', QueryVariable(1)),
                true,
              ),
              (
                const QueryClause(QueryVariable(0), 'title', QueryVariable(2)),
                true,
              ),
            ]),
          ),
      ];
      final retained = views.first.result();
      final roots = cached
          ? [for (final v in views) v.result()]
          : <DatafriendResult>[];
      final dirty = List<bool>.filled(views.length, false);
      for (var j = 0; j < views.length; j++) {
        views[j].onChanged = () {
          dirty[j] = true;
        };
      }
      final decoded = cached
          ? [for (final r in roots) r.toList(growable: false)]
          : <List<Triple>>[];
      var observed = 0;
      measurements.run((i) {
        final change = Triple(1, 'done', i.isEven);
        if (add) {
          require(views.first.add(change), 'Toggle did not change membership');
        } else {
          views.first.edit(add: [change]);
        }
        observed = 0;
        if (cached) {
          for (var j = 0; j < views.length; j++) {
            final next = views[j].result();
            if (dirty[j]) {
              decoded[j] = next.toList(growable: false);
              roots[j].close();
              roots[j] = next;
              dirty[j] = false;
            } else {
              next.close();
            }
            observed += decoded[j].length;
          }
        } else if (!editOnly) {
          for (final view in views) {
            observed += view.toList(growable: false).length;
          }
        }
      });
      var expectedCount = 0;
      for (var j = 0; j < views.length; j++) {
        final rows = views[j].toList(growable: false);
        final expectedEntities = [
          for (var id = 1; id <= entities; id++)
            if (id % 100 == groups[j]) id,
        ];
        require(
          rows.length == expectedEntities.length * 2,
          'Incorrect query cardinality',
        );
        expectedCount += rows.length;
        final byEntity = <int, Map<String, Object?>>{};
        for (final row in rows) {
          (byEntity[row.entity] ??= {})[row.attribute] = row.value;
        }
        for (final id in expectedEntities) {
          require(byEntity[id]?['title'] == 'task', 'Incorrect title');
          require(
            byEntity[id]?['done'] == (id == 1 && (samples + 99).isEven),
            'Incorrect completion',
          );
        }
        if (cached) {
          require(
            decoded[j].toSet().containsAll(rows) &&
                decoded[j].length == rows.length,
            'Cached result differs',
          );
        }
      }
      require(
        editOnly || observed == expectedCount,
        'Result consumption missing',
      );
      require(
        retained
                .firstWhere((t) => t.entity == 1 && t.attribute == 'done')
                .value ==
            (samples + 99).isEven,
        'Live result is stale',
      );
      retained.close();
      for (final r in roots) {
        r.close();
      }
      for (final v in views) {
        v.close();
      }
    } else {
      require(
        ['app_100', 'app_1000', 'app_noop'].contains(workload),
        'Unknown workload',
      );
      final taskCount = workload == 'app_1000' ? 1000 : 100;
      final store = DatafriendDocumentStore(db);
      await store.load();
      var doc = store.commit(
        AppDocument(
          tasks: [
            for (var i = 0; i < taskCount; i++)
              Task(id: '$i', title: 'task $i'),
          ],
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
          stickers: AppDocument.initial().stickers,
        ),
      );
      final stroke = doc.strokes.single, sticker = doc.stickers.first;
      measurements.run((i) {
        doc = store.commit(
          workload == 'app_noop'
              ? doc
              : doc.copyWith(
                  tasks: [
                    doc.tasks.first.copyWith(completed: i.isEven),
                    ...doc.tasks.skip(1),
                  ],
                ),
        );
      });
      require(
        doc.tasks.length == taskCount && doc.tasks.first.id == '0',
        'App task identity/order',
      );
      require(
        doc.tasks.first.completed ==
            (workload != 'app_noop' && (samples + 99).isEven),
        'App toggle missing',
      );
      require(
        doc.tasks.skip(1).every((t) => !t.completed),
        'Other tasks changed',
      );
      require(
        identical(doc.strokes.single, stroke) &&
            identical(doc.stickers.first, sticker),
        'Unchanged collections rebuilt',
      );
      require(
        stroke.points.length == 5000 && stroke.points.last.y == 4999,
        'Drawing changed',
      );
      store.close();
    }
    measurements.report();
  } finally {
    db.close();
    dir.deleteSync(recursive: true);
  }
}
