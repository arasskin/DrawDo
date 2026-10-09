import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqflite/sqflite.dart' as sql;
import 'package:sqflite_common_ffi/sqflite_ffi.dart';
import 'package:datafriend_todo_lab/change.dart';
import 'package:datafriend_todo_lab/stores.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  sqfliteFfiInit();
  sql.databaseFactory = databaseFactoryFfi;
  test('InstantDB projection preserves ordering beyond small lists', () async {
    final dir = Directory.systemTemp.createTempSync('todo-order-test-');
    final store = await TodoStore.open(
      StoreKind.instantdb,
      dir.path,
      seed: fixture(100),
      durable: false,
    );
    try {
      expect(store.tasks.map((t) => t.id), fixture(100).map((t) => t.id));
      for (var i = 0; i < 3; i++) {
        final target = !store.tasks.first.completed;
        await change(
          store,
          () => store.toggle(fixtureId(0)),
          () => store.tasks.first.completed == target,
        );
        expect(store.tasks.map((t) => t.id), fixture(100).map((t) => t.id));
      }
    } finally {
      await store.close();
      dir.deleteSync(recursive: true);
    }
  });
  for (final kind in StoreKind.values) {
    test('${kind.name}: real CRUD, reactive publication and reopen', () async {
      final dir = Directory.systemTemp.createTempSync('todo-lab-test-');
      final library =
          '${Directory.current.parent.path}/datafriend/build/native/libdatafriend.dylib';
      var store = await TodoStore.open(
        kind,
        dir.path,
        seed: fixture(3),
        durable: false,
        libraryPath: library,
      );
      try {
        expect(store.tasks.map((t) => t.title), ['Task 0', 'Task 1', 'Task 2']);
        await change(
          store,
          () => store.toggle(fixtureId(0)),
          () => store.tasks.first.completed,
        );
        expect(store.completed, 1);
        await change(
          store,
          () => store.rename(fixtureId(1), 'renamed'),
          () => store.tasks[1].title == 'renamed',
        );
        await change(
          store,
          () => store.insert(fixtureId(3), 'added', 3),
          () => store.tasks.length == 4,
        );
        await change(
          store,
          () => store.remove(fixtureId(2)),
          () => store.tasks.length == 3,
        );
        expect(store.tasks.map((t) => t.id), [
          fixtureId(0),
          fixtureId(1),
          fixtureId(3),
        ]);
        await store.close();
        store = await TodoStore.open(
          kind,
          dir.path,
          durable: false,
          libraryPath: library,
        );
        expect(store.tasks.map((t) => t.title), ['Task 0', 'renamed', 'added']);
        expect(store.tasks.first.completed, isTrue);
      } finally {
        await store.close();
        dir.deleteSync(recursive: true);
      }
    });
  }
}
