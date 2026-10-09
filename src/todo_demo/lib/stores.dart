import 'dart:async';
import 'dart:collection';
import 'dart:io';

import 'package:drawdo/data/app_views.dart';
import 'package:drawdo/data/datafriend.dart' as df;
import 'package:drawdo/model/document.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter_instantdb/flutter_instantdb.dart' as instant;
import 'package:logging/logging.dart';
import 'package:sqflite/sqflite.dart' as sql;

export 'package:drawdo/model/document.dart' show Task;
export 'package:drawdo/data/datafriend.dart' show DatafriendDurability;

String fixtureId(int i) =>
    '00000000-0000-4000-8000-${i.toString().padLeft(12, '0')}';
List<Task> fixture(int count) => [
  for (var i = 0; i < count; i++) Task(id: fixtureId(i), title: 'Task $i'),
];

enum StoreKind { datafriend, instantdb }

/// Null means completion was synchronous. A Future is the backend's write
/// completion, which is distinct from reactive-query publication in InstantDB.
abstract class TodoStore extends ChangeNotifier {
  List<Task> get tasks;
  int get completed;
  Object? error;
  int publications = 0;
  Map<String, Object?> get settings;
  Future<void>? insert(String id, String title, int order);
  Future<void>? toggle(String id);
  Future<void>? rename(String id, String title);
  Future<void>? remove(String id);
  Future<void> close();
  void publish() {
    publications++;
    notifyListeners();
  }

  Future<void> until(bool Function() predicate) {
    final done = Completer<void>();
    void check() {
      if (done.isCompleted) return;
      if (error != null) {
        done.completeError(error!);
      } else if (predicate()) {
        done.complete();
      }
    }

    addListener(check);
    check();
    return done.future
        .timeout(const Duration(seconds: 30))
        .whenComplete(() => removeListener(check));
  }

  static Future<TodoStore> open(
    StoreKind kind,
    String path, {
    List<Task>? seed,
    bool? durable,
    df.DatafriendDurability? durability,
    String? libraryPath,
  }) async {
    await Directory(path).create(recursive: true);
    return switch (kind) {
      StoreKind.datafriend => DatafriendTodos.open(
        path,
        seed: seed,
        durable: durable,
        durability: durability,
        libraryPath: libraryPath,
      ),
      StoreKind.instantdb => InstantTodos.open(
        path,
        seed: seed,
        durable: durable ?? true,
      ),
    };
  }
}

final class DatafriendTodos extends TodoStore {
  DatafriendTodos._(this.views, this.durability) {
    views.database.onPublished = publish;
  }
  static DatafriendTodos open(
    String path, {
    List<Task>? seed,
    bool? durable,
    df.DatafriendDurability? durability,
    String? libraryPath,
  }) {
    final library =
        libraryPath ??
        Platform.environment['DATAFRIEND_LIBRARY'] ??
        '${File(Platform.resolvedExecutable).parent.parent.path}/Frameworks/libdatafriend.dylib';
    final mode =
        durability ??
        (durable == null
            ? df.DatafriendDurability.asynchronous
            : durable
            ? df.DatafriendDurability.synchronous
            : df.DatafriendDurability.buffered);
    final views = AppViews(
      df.Datafriend.open(path, libraryPath: library, durability: mode),
    );
    views.initialize(initial: AppDocument(tasks: seed ?? const []));
    return DatafriendTodos._(views, mode);
  }

  final AppViews views;
  final df.DatafriendDurability durability;
  @override
  List<Task> get tasks => views.tasks;
  @override
  int get completed => views.completedTasks;
  @override
  Map<String, Object?> get settings => {
    'durability': durability.name,
    'sync': false,
    'storage': 'AELMDB',
  };
  @override
  Future<void>? insert(String id, String title, int order) {
    final e = views.database.newEntity();
    views.database.edit(
      add: [
        df.Triple(e, 'kind', 'task'),
        df.Triple(e, 'id', id),
        df.Triple(e, 'title', title),
        df.Triple(e, 'done', false),
        df.Triple(e, 'order', order),
      ],
    );
    return null;
  }

  @override
  Future<void>? toggle(String id) {
    views.toggleTask(id);
    return null;
  }

  @override
  Future<void>? rename(String id, String title) {
    views.editTask(id, title);
    return null;
  }

  @override
  Future<void>? remove(String id) {
    views.deleteTask(id);
    return null;
  }

  @override
  Future<void> close() async {
    views.close();
    dispose();
  }
}

final class InstantTodos extends TodoStore {
  InstantTodos._(this.db, this.settings);
  static const appId =
      'bb40773a-79fa-4000-8000-019ec2b8fd01'; // local-only demo ID
  static const query = <String, dynamic>{'todos': <String, dynamic>{}};
  final instant.InstantDB db;
  @override
  final Map<String, Object?> settings;
  late final void Function() _stop;
  List<Task> _tasks = const [];
  int _completed = 0;
  @override
  List<Task> get tasks => _tasks;
  @override
  int get completed => _completed;

  static Future<InstantTodos> open(
    String path, {
    List<Task>? seed,
    required bool durable,
  }) async {
    final existed = File('$path/$appId.db').existsSync();
    final db = await instant.InstantDB.init(
      appId: appId,
      config: instant.InstantConfig(persistenceDir: path, syncEnabled: false),
    );
    instant.InstantDBLogging.configure(level: Level.OFF);
    // Reuse sqflite's single-instance connection. This is a benchmark-only
    // durability setting; no package implementation is patched or replaced.
    final connection = await sql.openDatabase('$path/$appId.db');
    if (!durable) await connection.execute('PRAGMA synchronous=OFF');
    final settings = <String, Object?>{
      'sync': false,
      'storage': 'SQLite via sqflite',
      'synchronous': (await connection.rawQuery(
        'PRAGMA synchronous',
      )).single.values.single,
      'journal_mode': (await connection.rawQuery(
        'PRAGMA journal_mode',
      )).single.values.single,
      'package': 'flutter_instantdb 2.3.0',
    };
    if (!existed && seed != null && seed.isNotEmpty) {
      await db.transact(<instant.Operation>[
        for (var i = 0; i < seed.length; i++)
          ...db.tx['todos'][seed[i].id].update({
            'title': seed[i].title,
            'done': seed[i].completed,
            'order': i,
          }).operations,
      ]);
    }
    final store = InstantTodos._(db, settings);
    final initial = Completer<void>();
    final result = db.query(query);
    store._stop = instant.effect(() {
      final current = result.value;
      if (current.isLoading) return;
      if (current.error != null) {
        store.error = StateError(current.error!);
        if (!initial.isCompleted) initial.completeError(store.error!);
      } else {
        final rows = (current.data?['todos'] as List? ?? const [])
            .cast<Map<String, dynamic>>()
            .toList();
        // 2.3.0 converts query order to List<String>, but its SQLite comparator
        // only handles List<Map>. Sort this projection explicitly so the UI has
        // a stable order without patching the dependency. Included in timings.
        rows.sort((a, b) {
          final order = (a['order'] as num).compareTo(b['order'] as num);
          return order != 0
              ? order
              : (a['id'] as String).compareTo(b['id'] as String);
        });
        store._tasks = _InstantRows(rows);
        store._completed = rows.where((row) => row['done'] == true).length;
        if (!initial.isCompleted) initial.complete();
      }
      store.publish();
    });
    await initial.future.timeout(const Duration(seconds: 30));
    return store;
  }

  Future<void> _write(instant.TransactionChunk chunk) async {
    await db.transact(chunk);
  }

  @override
  Future<void> insert(String id, String title, int order) => _write(
    db.tx['todos'][id].update({'title': title, 'done': false, 'order': order}),
  );
  @override
  Future<void> toggle(String id) {
    final task = tasks.firstWhere((t) => t.id == id);
    return _write(db.tx['todos'][id].update({'done': !task.completed}));
  }

  @override
  Future<void> rename(String id, String title) =>
      _write(db.tx['todos'][id].update({'title': title}));
  @override
  Future<void> remove(String id) => _write(db.tx['todos'][id].delete());
  @override
  Future<void> close() async {
    _stop();
    db.queries
        .dispose(); // public query engine cleanup (not done by db.dispose)
    await db.dispose();
    dispose();
  }
}

/// Match AppViews' lazy model conversion: don't manufacture 1,000 Task objects
/// when only 15 rows are requested. InstantDB itself already materializes maps.
final class _InstantRows extends ListBase<Task> {
  _InstantRows(this.rows);
  final List<Map<String, dynamic>> rows;
  final Map<int, Task> _cache = {};
  @override
  int get length => rows.length;
  @override
  set length(int value) => throw UnsupportedError('Read only');
  @override
  Task operator [](int i) => _cache.putIfAbsent(i, () {
    final row = rows[i];
    return Task(
      id: row['id'] as String,
      title: row['title'] as String,
      completed: row['done'] == true,
    );
  });
  @override
  void operator []=(int i, Task value) => throw UnsupportedError('Read only');
}
