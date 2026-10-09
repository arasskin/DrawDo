import 'dart:typed_data';
import 'dart:collection';
import 'datafriend_profile.dart';
import 'datafriend.dart';
import '../model/document.dart';

/// Read projections are lazy. Mutations below submit explicit database edits;
/// neither a document diff nor a readback is part of their completion boundary.
final class AppViews {
  AppViews(this.database) {
    try {
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
        late final _Rows rows;
        rows = _Rows(
          database.subscribe(
            DatafriendQuery([for (final clause in clauses) (clause, true)]),
            onChanged: () => rows.invalidate(),
          ),
          orderBy: schema.key == 'sticker' ? null : 'order',
          thenBy: schema.key == 'stroke' ? 'part' : null,
        );
        _views[schema.key] = rows;
      }
    } catch (_) {
      close();
      rethrow;
    }
  }
  final Datafriend database;
  final Map<String, _Rows> _views = {};
  static const _schemas = {
    'task': ['id', 'title', 'done', 'order'],
    'sticker': ['id', 'asset', 'label', 'x', 'y'],
    'stroke': ['id', 'stroke', 'part', 'page', 'color', 'order', 'points'],
  };
  List<Task> _tasks = const [];
  List<Sticker> _stickers = const [];
  List<Stroke> _strokes = const [];

  List<Task> get tasks {
    final source = _views['task']!;
    if (source.modelsDirty) {
      final ids = source.ordered;
      _tasks = _LazyModels(ids.length, (i) {
        final entity = ids[i];
        return source.model(entity, () {
          if (datafriendProfiling) {
            DatafriendProfile.count(DatafriendCounter.taskModels);
          }
          return Task(
            id: source.value(entity, 'id') as String,
            title: source.value(entity, 'title') as String,
            completed: source.value(entity, 'done') as bool,
          );
        });
      });
      source.modelsDirty = false;
    }
    return _tasks;
  }

  int get completedTasks => _views['task']!.result.countWhere('done', true);

  List<Sticker> get stickers {
    final source = _views['sticker']!;
    if (source.modelsDirty) {
      final ids = source.ordered;
      _stickers = _LazyModels(ids.length, (i) {
        final entity = ids[i];
        return source.model(
          entity,
          () => Sticker(
            id: source.value(entity, 'id') as String,
            asset: source.value(entity, 'asset') as String,
            label: source.value(entity, 'label') as String,
            x: source.value(entity, 'x') as double,
            y: source.value(entity, 'y') as double,
          ),
        );
      });
      source.modelsDirty = false;
    }
    return _stickers;
  }

  List<Stroke> get strokes {
    final source = _views['stroke']!;
    if (source.modelsDirty) {
      // Only the chunk membership index is grouped here. Points are read when
      // a stroke is requested by the painter, never while reading task rows.
      final grouped = <String, List<int>>{};
      for (final entity in source.ordered) {
        (grouped[source.value(entity, 'stroke') as String] ??= []).add(entity);
      }
      final chunks = grouped.values.toList(growable: false);
      _strokes = _LazyModels(chunks.length, (i) {
        final parts = chunks[i], first = parts.first;
        return Stroke(
          id: source.value(first, 'stroke') as String,
          page: CanvasPage.values[source.value(first, 'page') as int],
          color: source.value(first, 'color') as int,
          points: parts.expand((entity) {
            final data = ByteData.sublistView(
              source.value(entity, 'points') as Uint8List,
            );
            if (data.lengthInBytes % 16 != 0) {
              throw const FormatException('Invalid stroke points');
            }
            return List.generate(
              data.lengthInBytes ~/ 16,
              (j) => InkPoint(
                data.getFloat64(j * 16),
                data.getFloat64(j * 16 + 8),
              ),
            );
          }),
        );
      });
      source.modelsDirty = false;
    }
    return _strokes;
  }

  /// Read-only export/debug projection. Never submitted back as mutable state.
  AppDocument get document =>
      AppDocument(tasks: tasks, stickers: stickers, strokes: strokes);

  /// Initial fixture/bootstrap only, applied once in a single atomic edit.
  void initialize({AppDocument? initial}) {
    final marker = database.subscribe(
      const DatafriendQuery([(QueryClause(0, 'app/initialized', true), true)]),
    );
    try {
      if (marker.get(0, 'app/initialized') != null) return;
      final seed = initial ?? AppDocument.initial();
      final taskTriples = <Triple>[Triple(0, 'app/initialized', true)];
      final stickerTriples = <Triple>[], strokeTriples = <Triple>[];
      for (var i = 0; i < seed.tasks.length; i++) {
        final t = seed.tasks[i];
        taskTriples.addAll(
          _facts(database.newEntity(), {
            'kind': 'task',
            'id': t.id,
            'title': t.title,
            'done': t.completed,
            'order': i,
          }),
        );
      }
      for (final s in seed.stickers) {
        stickerTriples.addAll(
          _facts(database.newEntity(), {
            'kind': 'sticker',
            'id': s.id,
            'asset': s.asset,
            'label': s.label,
            'x': s.x,
            'y': s.y,
          }),
        );
      }
      for (var i = 0; i < seed.strokes.length; i++) {
        strokeTriples.addAll(_strokeFacts(seed.strokes[i], i));
      }
      database.edit(add: [...taskTriples, ...stickerTriples, ...strokeTriples]);
    } finally {
      marker.close();
    }
  }

  static Iterable<Triple> _facts(int entity, Map<String, Object?> values) =>
      values.entries.map((e) => Triple(entity, e.key, e.value));

  int _nextOrder(_Rows source) => source.ordered.isEmpty
      ? 0
      : (source.value(source.ordered.last, 'order') as int) + 1;

  void addTask(String title) {
    final source = _views['task']!;
    final order = _nextOrder(source), entity = database.newEntity();
    database.edit(
      add: _facts(entity, {
        'kind': 'task',
        'id': '$entity',
        'title': title,
        'done': false,
        'order': order,
      }),
    );
  }

  void toggleTask(String id) {
    final source = _views['task']!, entity = source.entity(id);
    if (entity == null) return;
    final done = source.value(entity, 'done');
    if (done != null) {
      database.edit(add: [Triple(entity, 'done', !(done as bool))]);
    }
  }

  void editTask(String id, String title) {
    final source = _views['task']!, entity = source.entity(id);
    if (entity != null && source.result.containsField(entity, 'title')) {
      database.edit(add: [Triple(entity, 'title', title)]);
    }
  }

  void deleteTask(String id) {
    final source = _views['task']!, entity = source.entity(id);
    if (entity == null) return;
    database.edit(
      remove: source.retract([entity], ['kind', ..._schemas['task']!]),
    );
  }

  void reorderTask(int oldIndex, int newIndex) {
    final source = _views['task']!;
    final ordered = source.ordered.toList();
    if (newIndex > oldIndex) newIndex--;
    ordered.insert(newIndex, ordered.removeAt(oldIndex));
    database.edit(
      add: [
        for (var i = 0; i < ordered.length; i++)
          if (source.value(ordered[i], 'order') != i)
            Triple(ordered[i], 'order', i),
      ],
    );
  }

  void moveSticker(String id, double x, double y) {
    final source = _views['sticker']!, entity = source.entity(id);
    if (entity != null && source.result.containsField(entity, 'id')) {
      database.edit(
        add: [
          Triple(entity, 'x', x.clamp(0, 1).toDouble()),
          Triple(entity, 'y', y.clamp(0, 1).toDouble()),
        ],
      );
    }
  }

  Iterable<Triple> _strokeFacts(Stroke stroke, int order) sync* {
    for (var start = 0; start < stroke.points.length; start += 256) {
      final end = (start + 256).clamp(0, stroke.points.length);
      final data = ByteData((end - start) * 16);
      for (var i = start; i < end; i++) {
        data.setFloat64((i - start) * 16, stroke.points[i].x);
        data.setFloat64((i - start) * 16 + 8, stroke.points[i].y);
      }
      yield* _facts(database.newEntity(), {
        'kind': 'stroke',
        'id': '${stroke.id}/${start ~/ 256}',
        'stroke': stroke.id,
        'part': start ~/ 256,
        'page': stroke.page.index,
        'color': stroke.color,
        'order': order,
        'points': data.buffer.asUint8List(),
      });
    }
  }

  void addStroke(CanvasPage page, int color, List<InkPoint> points) {
    final source = _views['stroke']!;
    database.edit(
      add: _strokeFacts(
        Stroke(id: newId(), page: page, color: color, points: points),
        _nextOrder(source),
      ),
    );
  }

  void removeStrokes(Set<String> ids) {
    final source = _views['stroke']!;
    database.edit(
      remove: source.retract(
        source.ordered.where(
          (entity) => ids.contains(source.value(entity, 'stroke')),
        ),
        ['kind', ..._schemas['stroke']!],
      ),
    );
  }

  void undoStroke(CanvasPage page) {
    final matching = strokes.where((s) => s.page == page);
    if (matching.isNotEmpty) removeStrokes({matching.last.id});
  }

  void close() {
    for (final source in _views.values) {
      source.close();
    }
    database.close();
  }
}

// UI objects are constructed only for requested rows, then reused within that
// published projection. This is not a second triple store or a bulk readback.
final class _LazyModels<T> extends ListBase<T> {
  _LazyModels(this._length, this._read);
  final int _length;
  final T Function(int) _read;
  final Map<int, T> _accessed = {};
  @override
  int get length => _length;
  @override
  set length(int _) => throw UnsupportedError('Read-only projection');
  @override
  T operator [](int index) {
    RangeError.checkValidIndex(index, this, null, _length);
    return _accessed.putIfAbsent(index, () => _read(index));
  }

  @override
  void operator []=(int index, T value) =>
      throw UnsupportedError('Read-only projection');
}

final class _Rows {
  _Rows(this.view, {this.orderBy, this.thenBy}) : result = view.result();
  final DatafriendView view;
  final DatafriendResult result;
  final String? orderBy, thenBy;
  DatafriendEntityIds? _ordered;
  final Map<int, Object> _models = {};
  T model<T extends Object>(int entity, T Function() read) =>
      _models.putIfAbsent(entity, read) as T;
  bool modelsDirty = true;
  void invalidate() {
    final changes = view.changes;
    if (changes.reset) {
      _models.clear();
    } else {
      for (final entity in changes.entities) {
        _models.remove(entity);
      }
    }
    if (changes.reset ||
        changes.membershipChanged ||
        changes.attributes.contains(orderBy) ||
        changes.attributes.contains(thenBy)) {
      _ordered = null;
    }
    modelsDirty = true;
  }

  // Native-owned metadata only. Old lists retain their ID buffer until collected.
  DatafriendEntityIds get ordered =>
      _ordered ??= result.entities(orderBy: orderBy, thenBy: thenBy);
  Object? value(int entity, String attribute) =>
      result.value(entity, attribute);
  int? entity(String id) => result.findEntity('id', id);
  Iterable<Triple> retract(
    Iterable<int> entities,
    Iterable<String> attributes,
  ) sync* {
    for (final entity in entities) {
      for (final attribute in attributes) {
        if (result.containsField(entity, attribute)) {
          yield Triple(entity, attribute, value(entity, attribute));
        }
      }
    }
  }

  void close() {
    result.close();
    view.close();
  }
}
