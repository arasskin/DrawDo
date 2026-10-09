// Historical benchmark fixture. Production callbacks use AppViews directly.
import 'dart:typed_data';
import 'package:drawdo/data/datafriend.dart';
import 'package:drawdo/data/datafriend_profile.dart';
import 'package:drawdo/data/document_store.dart';
import 'package:drawdo/model/document.dart';

/// The app adapter owns query handles only; Datafriend owns records and writes.
/// Stroke points are chunked into separate entities to bound record size.
final class DatafriendDocumentStore implements BlockingDocumentStore {
  DatafriendDocumentStore(this.database) {
    for (final schema in _schemas.entries) {
      final clauses = <QueryClause>[
        QueryClause(const QueryVariable(0), 'kind', schema.key),
        for (var i = 0; i < schema.value.length; i++)
          QueryClause(
            const QueryVariable(0),
            schema.value[i],
            QueryVariable(i + 1),
          ),
      ];
      _dirty.add(schema.key);
      _views[schema.key] =
          database.subscribe(
              DatafriendQuery([for (final clause in clauses) (clause, true)]),
            )
            ..onChanged = () {
              _dirty.add(schema.key);
            };
    }
  }
  final Datafriend database;
  final Map<String, DatafriendView> _views = {};
  Map<int, Map<String, Triple>> _baseline = {};
  final Map<String, DatafriendResult> _roots = {};
  final Set<String> _dirty = {};
  final Map<String, Map<int, Map<String, Triple>>> _rows = {};
  AppDocument? _published;
  static const _schemas = {
    'task': ['id', 'title', 'done', 'order'],
    'sticker': ['id', 'asset', 'label', 'x', 'y'],
    'stroke': ['id', 'stroke', 'part', 'page', 'color', 'order', 'points'],
  };
  AppDocument _read() {
    var phase = DatafriendPhase.appRows;
    if (datafriendProfiling) DatafriendProfile.begin();
    try {
      final changed = <String>{};
      for (final e in _views.entries) {
        final root = e.value.result();
        final old = _roots[e.key];
        if (old != null && !_dirty.contains(e.key)) {
          root.close();
          continue;
        }
        final rows = <int, Map<String, Triple>>{};
        for (final t in root) {
          (rows[t.entity] ??= {})[t.attribute] = t;
        }
        _rows[e.key] = rows;
        _roots[e.key] = root;
        _dirty.remove(e.key);
        old?.close();
        changed.add(e.key);
        if (datafriendProfiling) {
          DatafriendProfile.count(DatafriendCounter.changedRoots);
        }
      }
      if (datafriendProfiling) {
        DatafriendProfile.end(phase);
        phase = DatafriendPhase.appBaseline;
        DatafriendProfile.begin();
      }
      _baseline = {for (final rows in _rows.values) ...rows};
      if (datafriendProfiling) {
        DatafriendProfile.end(phase);
        phase = DatafriendPhase.appModels;
        DatafriendProfile.begin();
      }
      List<Map<String, Object?>> rows(String kind) => _rows[kind]!.values
          .map((r) => r.map((k, v) => MapEntry(k, v.value)))
          .toList();
      var tasks = _published?.tasks ?? <Task>[];
      var stickers = _published?.stickers ?? <Sticker>[];
      var strokes = _published?.strokes ?? <Stroke>[];
      if (changed.contains('task')) {
        final data = rows('task')
          ..sort((a, b) => (a['order'] as int).compareTo(b['order'] as int));
        tasks = data
            .map(
              (r) => Task(
                id: r['id'] as String,
                title: r['title'] as String,
                completed: r['done'] as bool,
              ),
            )
            .toList();
      }
      if (changed.contains('sticker')) {
        stickers = rows('sticker')
            .map(
              (r) => Sticker(
                id: r['id'] as String,
                asset: r['asset'] as String,
                label: r['label'] as String,
                x: r['x'] as double,
                y: r['y'] as double,
              ),
            )
            .toList();
      }
      if (changed.contains('stroke')) {
        final data = rows('stroke')
          ..sort((a, b) {
            final order = (a['order'] as int).compareTo(b['order'] as int);
            return order != 0
                ? order
                : (a['part'] as int).compareTo(b['part'] as int);
          });
        final grouped = <String, List<Map<String, Object?>>>{};
        for (final row in data) {
          (grouped[row['stroke'] as String] ??= []).add(row);
        }
        strokes = grouped.entries
            .map(
              (e) => Stroke(
                id: e.key,
                page: CanvasPage.values[e.value.first['page'] as int],
                color: e.value.first['color'] as int,
                points: e.value.expand((r) {
                  final b = ByteData.sublistView(r['points'] as Uint8List);
                  if (b.lengthInBytes % 16 != 0) {
                    throw const FormatException('Invalid stroke points');
                  }
                  return List.generate(
                    b.lengthInBytes ~/ 16,
                    (i) => InkPoint(
                      b.getFloat64(i * 16),
                      b.getFloat64(i * 16 + 8),
                    ),
                  );
                }),
              ),
            )
            .toList();
      }
      return _published = AppDocument(
        tasks: tasks,
        stickers: stickers,
        strokes: strokes,
      );
    } finally {
      if (datafriendProfiling) DatafriendProfile.end(phase);
    }
  }

  Iterable<Map<String, Object?>> _encode(
    AppDocument d,
    Set<String> changed,
  ) sync* {
    for (var i = 0; changed.contains('task') && i < d.tasks.length; i++) {
      final t = d.tasks[i];
      yield {
        'kind': 'task',
        'id': t.id,
        'title': t.title,
        'done': t.completed,
        'order': i,
      };
    }
    for (final s in changed.contains('sticker') ? d.stickers : <Sticker>[]) {
      yield {
        'kind': 'sticker',
        'id': s.id,
        'asset': s.asset,
        'label': s.label,
        'x': s.x,
        'y': s.y,
      };
    }
    for (var i = 0; changed.contains('stroke') && i < d.strokes.length; i++) {
      final s = d.strokes[i];
      for (var start = 0; start < s.points.length; start += 256) {
        final end = (start + 256).clamp(0, s.points.length);
        final b = ByteData((end - start) * 16);
        for (var j = start; j < end; j++) {
          b.setFloat64((j - start) * 16, s.points[j].x);
          b.setFloat64((j - start) * 16 + 8, s.points[j].y);
        }
        yield {
          'kind': 'stroke',
          'id': '${s.id}/${start ~/ 256}',
          'stroke': s.id,
          'part': start ~/ 256,
          'page': s.page.index,
          'color': s.color,
          'order': i,
          'points': b.buffer.asUint8List(),
        };
      }
    }
  }

  @override
  Future<AppDocument?> load() async {
    // A dedicated metadata query distinguishes a deliberately empty app from a
    // new store, without keeping an all-facts subscription on the edit path.
    final marker = database.subscribe(
      const DatafriendQuery([(QueryClause(0, 'app/initialized', true), true)]),
    );
    try {
      if (marker.isEmpty) {
        final initial = AppDocument.initial();
        _commit(initial, extra: [Triple(0, 'app/initialized', true)]);
      }
      return _read();
    } finally {
      marker.close();
    }
  }

  @override
  AppDocument commit(AppDocument document) => _commit(document);
  AppDocument _commit(AppDocument document, {List<Triple> extra = const []}) {
    var phase = DatafriendPhase.appDetectChanges;
    if (datafriendProfiling) DatafriendProfile.begin();
    try {
      final previous = _published;
      bool same(List<Object> a, List<Object> b) {
        if (a.length != b.length) return false;
        for (var i = 0; i < a.length; i++) {
          if (!identical(a[i], b[i])) return false;
        }
        return true;
      }

      final changed = <String>{
        if (previous == null || !same(previous.tasks, document.tasks)) 'task',
        if (previous == null || !same(previous.stickers, document.stickers))
          'sticker',
        if (previous == null || !same(previous.strokes, document.strokes))
          'stroke',
      };
      if (datafriendProfiling) {
        DatafriendProfile.end(phase);
        phase = DatafriendPhase.appIdentityLookup;
        DatafriendProfile.begin();
      }
      final existing = _baseline;
      final byId = <String, int>{};
      for (final e in existing.entries) {
        byId['${e.value['kind']!.value}:${e.value['id']!.value}'] = e.key;
      }
      if (datafriendProfiling) {
        DatafriendProfile.end(phase);
        phase = DatafriendPhase.appEncodeDiff;
        DatafriendProfile.begin();
      }
      final removals = <String, List<Triple>>{},
          additions = <String, List<Triple>>{
            'task': [...extra],
          };
      final retained = <int>{};
      for (final row in _encode(document, changed)) {
        if (datafriendProfiling) {
          DatafriendProfile.count(DatafriendCounter.encodedRows);
          DatafriendProfile.count(
            DatafriendCounter.candidateTriples,
            row.length,
          );
        }
        final id = byId['${row['kind']}:${row['id']}'] ?? database.newEntity();
        retained.add(id);
        final old = existing[id] ?? {};
        for (final e in row.entries) {
          final next = Triple(id, e.key, e.value);
          if (old[e.key] != next) {
            (additions[row['kind'] as String] ??= []).add(next);
          }
        }
      }
      if (datafriendProfiling) {
        DatafriendProfile.end(phase);
        phase = DatafriendPhase.appRemovals;
        DatafriendProfile.begin();
      }
      for (final e in existing.entries) {
        if (changed.contains(e.value['kind']!.value) &&
            !retained.contains(e.key)) {
          (removals[e.value['kind']!.value as String] ??= []).addAll(
            e.value.values,
          );
        }
      }
      if (datafriendProfiling) {
        DatafriendProfile.end(phase);
        phase = DatafriendPhase.appEdit;
        DatafriendProfile.begin();
      }
      database.editViews({
        for (final kind in {...removals.keys, ...additions.keys})
          _views[kind]!: ViewEdit(
            remove: removals[kind] ?? [],
            add: additions[kind] ?? [],
          ),
      });
      if (datafriendProfiling) {
        DatafriendProfile.end(phase);
        phase = DatafriendPhase.appRefresh;
        DatafriendProfile.begin();
      }
      return _read();
    } finally {
      if (datafriendProfiling) DatafriendProfile.end(phase);
    }
  }

  @override
  Future<void> save(AppDocument document) async {
    commit(document);
  }

  @override
  void close() {
    for (final view in _views.values) {
      view.close();
    }
    for (final root in _roots.values) {
      root.close();
    }
    database.close();
  }
}
