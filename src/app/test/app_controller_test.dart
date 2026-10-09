import 'package:flutter_test/flutter_test.dart';
import 'package:drawdo/data/datafriend.dart';
import 'package:drawdo/data/datafriend_profile.dart';
import 'package:drawdo/model/document.dart';
import 'native_app.dart';

void main() {
  test('task caches retain unchanged rows and ordering across edits', () async {
    final app = await nativeApp(
      initial: AppDocument(
        tasks: [
          for (var i = 0; i < 1000; i++) Task(id: 'task-$i', title: 'Task $i'),
        ],
      ),
    );
    final before = app.tasks.toList();
    if (datafriendProfiling) DatafriendProfile.reset();
    app.toggleTask('task-500');
    final after = app.tasks.toList();
    for (var i = 0; i < 1000; i++) {
      expect(identical(before[i], after[i]), i != 500);
    }
    expect(before[500].completed, isFalse);
    expect(after[500].completed, isTrue);
    if (datafriendProfiling) {
      expect(DatafriendProfile.counters[DatafriendCounter.taskModels.index], 1);
      expect(DatafriendProfile.counters[DatafriendCounter.entityIds.index], 0);
      // One native read for the toggle plus three fields for one rebuilt row.
      expect(
        DatafriendProfile.counters[DatafriendCounter.decodedValues.index],
        4,
      );
    }
    // Several publications without reading must accumulate cache invalidation.
    app.editTask('task-2', 'changed two');
    app.editTask('task-3', 'changed three');
    expect(app.tasks[2].title, 'changed two');
    expect(app.tasks[3].title, 'changed three');
    expect(identical(app.tasks[4], before[4]), isTrue);
    final database = app.views.database;
    final all = database.subscribe(const DatafriendQuery.all());
    final result = all.result();
    final moving = result.findEntity('id', 'task-999')!;
    database.edit(add: [Triple(moving, 'order', -1)]);
    expect(app.tasks.first.id, 'task-999');
    expect(identical(app.tasks[5], before[4]), isTrue);
    final removed = result.findEntity('id', 'task-1')!;
    final exported = database.exportEntities();
    database.importEntities({removed: null});
    expect(app.tasks.length, 999);
    expect(app.tasks.any((t) => t.id == 'task-1'), isFalse);
    database.importEntities({removed: exported[removed]});
    expect(app.tasks.length, 1000);
    expect(app.tasks[2].id, 'task-1');
    expect(identical(app.tasks[2], before[1]), isFalse);
    expect(identical(app.tasks[5], before[4]), isTrue);
    result.close();
    all.close();
  });

  test(
    'a thousand-task view reads only visible rows without reconstructing triples',
    () async {
      final app = await nativeApp(
        initial: AppDocument(
          tasks: [
            for (var i = 0; i < 1000; i++)
              Task(id: 'task-$i', title: 'Task $i'),
          ],
        ),
      );
      if (datafriendProfiling) DatafriendProfile.reset();
      final tasks = app.tasks;
      expect(tasks.length, 1000);
      expect(app.completedTasks, 0);
      if (datafriendProfiling) {
        expect(
          DatafriendProfile.counters[DatafriendCounter.taskModels.index],
          0,
        );
        expect(
          DatafriendProfile.counters[DatafriendCounter.decodedValues.index],
          0,
        );
      }
      for (var i = 0; i < 15; i++) {
        expect(tasks[i].title, 'Task $i');
      }
      if (datafriendProfiling) {
        expect(
          DatafriendProfile.counters[DatafriendCounter.taskModels.index],
          15,
        );
        expect(
          DatafriendProfile.counters[DatafriendCounter.decodedValues.index],
          45,
        );
        expect(
          DatafriendProfile.counters[DatafriendCounter.decodedTriples.index],
          0,
        );
        expect(
          DatafriendProfile.counters[DatafriendCounter.snapshotLists.index],
          0,
        );
      }
      app.toggleTask('task-999');
      expect(app.tasks[999].completed, isTrue);
      expect(app.completedTasks, 1);
      app.reorderTask(999, 0);
      expect(app.tasks.first.id, 'task-999');
    },
  );

  test(
    'direct toggle publishes before notification with no eager view readback',
    () async {
      final app = await nativeApp();
      app.addTask('one');
      final before = app.tasks;
      final id = before.single.id;
      var notifications = 0;
      app.addListener(() => notifications++);
      if (datafriendProfiling) DatafriendProfile.reset();
      app.toggleTask(id);
      expect(notifications, 1);
      if (datafriendProfiling) {
        expect(
          DatafriendProfile.counters[DatafriendCounter.snapshotLists.index],
          0,
        );
        expect(
          DatafriendProfile.counters[DatafriendCounter.liveReadCalls.index],
          0,
        );
        expect(
          DatafriendProfile.counters[DatafriendCounter.candidateTriples.index],
          0,
        );
        expect(
          DatafriendProfile.counters[DatafriendCounter.editCalls.index],
          1,
        );
      }
      // A second callback before any widget rebuild reads the current native value.
      app.toggleTask(id);
      expect(notifications, 2);
      expect(app.tasks.single.completed, isFalse);
      expect(before.single.completed, isFalse);
      app.toggleTask(id);
      expect(app.tasks.single.completed, isTrue);
      expect(before.single.completed, isFalse);
    },
  );

  test(
    'one notification observes every published view, including direct imports',
    () async {
      final app = await nativeApp();
      app.addTask('one');
      final id = app.tasks.single.id;
      final database = app.views.database;
      final all = database.subscribe(const DatafriendQuery.all());
      final taskEntity = int.parse(id);
      final stickerEntity = all
          .firstWhere((t) => t.attribute == 'id' && t.value == 'heart')
          .entity;
      var notifications = 0;
      app.addListener(() {
        notifications++;
        expect(all.get(stickerEntity, 'x')!.value, .4);
        expect(
          all.get(taskEntity, 'title')?.value,
          notifications == 1 ? 'updated' : null,
        );
      });
      database.editViews({
        all: ViewEdit(
          add: [
            Triple(taskEntity, 'title', 'updated'),
            Triple(stickerEntity, 'x', .4),
          ],
        ),
      });
      expect(notifications, 1);
      expect(app.tasks.single.title, 'updated');
      expect(app.stickers.firstWhere((s) => s.id == 'heart').x, .4);
      database.importEntities({taskEntity: null});
      expect(notifications, 2);
      expect(app.tasks, isEmpty);
      all.close();
    },
  );

  test(
    'field failure resets caches while preserving committed values',
    () async {
      final app = await nativeApp();
      app.addTask('one');
      final id = app.tasks.single.id;
      final before = app.tasks;
      app.editTask(id, 'x' * 65535);
      expect(app.saveError, isNotNull);
      expect(identical(app.tasks, before), isFalse);
      expect(app.tasks.single.title, 'one');
      app.retrySave();
      expect(app.saveError, isNotNull);
      app.toggleTask(id);
      app.editTask(id, 'two');
      expect(app.saveError, isNull);
      expect(app.tasks.single.completed, isTrue);
      expect(app.tasks.single.title, 'two');
      app.deleteTask(id);
      app.editTask(id, 'late dialog result');
      expect(app.tasks, isEmpty);
    },
  );

  test(
    'explicit ordering and drawing edits preserve untouched projections',
    () async {
      final app = await nativeApp();
      app.addTask('one');
      app.addTask('two');
      final first = app.tasks.first.id;
      final stickers = app.stickers;
      app.reorderTask(0, 2);
      expect(app.tasks.map((t) => t.title), ['two', 'one']);
      app.addTask('three');
      expect(app.tasks.map((t) => t.title), ['two', 'one', 'three']);
      app.editTask(first, '');
      expect(app.tasks.map((t) => t.title), ['two', 'three']);
      expect(identical(app.stickers, stickers), isTrue);
      app.addStroke(CanvasPage.space, 0xff000000, [const InkPoint(.5, 100)]);
      app.addStroke(CanvasPage.tasks, 0xff000000, [const InkPoint(.5, 100)]);
      final tasks = app.tasks;
      app.undoStroke(CanvasPage.space);
      expect(app.strokes.single.page, CanvasPage.tasks);
      expect(identical(app.tasks, tasks), isTrue);
    },
  );

  test(
    'pair lookup returns current values and distinguishes absence',
    () async {
      final app = await nativeApp();
      final view = app.views.database.subscribe(const DatafriendQuery.all());
      expect(view.get(999, 'missing'), isNull);
      view.edit(add: [Triple(100, 'nil', null), Triple(100, 'value', 42)]);
      expect(view.get(100, 'nil'), Triple(100, 'nil', null));
      expect(view.get(100, 'value')!.value, 42);
      expect(() => view.get(0x100000000, 'value'), throwsRangeError);
      view.close();
      expect(() => view.get(100, 'value'), throwsStateError);
    },
  );
}
