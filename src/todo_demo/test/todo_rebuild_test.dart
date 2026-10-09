import 'dart:io';
import 'package:drawdo/data/datafriend.dart';
import 'package:drawdo/data/datafriend_builder.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:datafriend_todo_lab/build_probe.dart';
import 'package:datafriend_todo_lab/todo_screen.dart';

void main() {
  testWidgets('row queries isolate edits; order and membership update the list', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(900, 740);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final dir = Directory.systemTemp.createTempSync('todo-rebuild-');
    final library =
        Platform.environment['DATAFRIEND_LIBRARY'] ??
        '${Directory.current.parent.path}/datafriend/build/native/libdatafriend.${Platform.isMacOS ? 'dylib' : 'so'}';
    final db = Datafriend.open(dir.path, libraryPath: library, durable: false);
    addTearDown(() async {
      await tester.pumpWidget(const SizedBox());
      db.close();
      dir.deleteSync(recursive: true);
    });
    db.edit(
      add: [
        for (var id = 1; id <= 60; id++) ...[
          Triple(id, 'kind', 'task'),
          Triple(id, 'id', '$id'),
          Triple(id, 'order', id),
          Triple(id, 'title', 'Task $id'),
          Triple(id, 'done', false),
        ],
      ],
    );
    final probe = TodoBuildProbe();
    await tester.pumpWidget(
      MaterialApp(
        theme: todoTheme(),
        home: TodoScreen(database: db, onBuild: probe.record),
      ),
    );
    await tester.pumpAndSettle();
    expect(find.byType(TaskRow).evaluate().length, lessThan(60));
    final mounted = probe.rows.keys.toSet();
    expect(mounted, containsAll([1, 2]));
    expect(mounted, isNot(contains(60)));
    State subscription(int id) => tester.state(
      find.descendant(
        of: find.byKey(ValueKey(id)),
        matching: find.byType(DatafriendBuilder),
      ),
    );
    final firstSubscription = subscription(1);

    probe.reset();
    db.edit(add: [Triple(1, 'title', 'Renamed')]);
    await tester.pumpAndSettle();
    expect(probe.parts, {'row': 1});
    expect(probe.rows, {1: 1});
    expect(find.text('Renamed'), findsOneWidget);

    probe.reset();
    await tester.tap(find.byType(Checkbox).first);
    await tester.pumpAndSettle();
    expect(probe.parts, {'row': 1, 'counter': 1});
    expect(probe.rows, {1: 1});
    expect(find.text('1 of 60 complete'), findsOneWidget);

    probe.reset();
    db.edit(add: [Triple(1, 'title', 'First'), Triple(2, 'title', 'Second')]);
    await tester.pumpAndSettle();
    expect(probe.parts, {'row': 2});
    expect(probe.rows, {1: 1, 2: 1});

    probe.reset();
    db.edit(add: [Triple(60, 'title', 'Offscreen update')]);
    await tester.pumpAndSettle();
    expect(probe.parts, isEmpty);

    probe.reset();
    db.edit(add: [Triple(1, 'order', 3)]);
    await tester.pumpAndSettle();
    expect(probe.parts['list'], 1);
    expect(probe.parts['counter'] ?? 0, 0);
    expect(probe.parts['screen'] ?? 0, 0);
    expect(tester.widget<TaskRow>(find.byType(TaskRow).first).entityId, 2);
    expect(identical(subscription(1), firstSubscription), isTrue);

    probe.reset();
    db.edit(
      add: [
        Triple(61, 'kind', 'task'),
        Triple(61, 'id', '61'),
        Triple(61, 'title', 'Inserted'),
        Triple(61, 'done', false),
        Triple(61, 'order', 0),
      ],
    );
    await tester.pumpAndSettle();
    expect(probe.parts['list'], 1);
    expect(probe.parts['counter'], 1);
    expect(find.text('Inserted'), findsOneWidget);
    probe.reset();
    db.deleteEntity(61);
    await tester.pumpAndSettle();
    expect(probe.parts['list'], 1);
    expect(probe.parts['counter'], 1);
    expect(find.text('Inserted'), findsNothing);

    // The filtered list depends on done for membership, although done is omitted
    // from its projected fields. Completing a visible active task must remove it.
    await tester.tap(find.text('Active'));
    await tester.pumpAndSettle();
    probe.reset();
    db.edit(add: [Triple(2, 'done', true)]);
    await tester.pumpAndSettle();
    expect(probe.parts['list'], 1);
    expect(probe.parts['counter'], 1);
    expect(find.text('Second'), findsNothing);
    expect(find.text('2 of 60 complete'), findsOneWidget);

    final scroll = tester.state<ScrollableState>(
      find.descendant(
        of: find.byKey(const ValueKey('todo-list')),
        matching: find.byType(Scrollable),
      ),
    );
    scroll.position.jumpTo(scroll.position.maxScrollExtent);
    await tester.pumpAndSettle();
    expect(find.text('Offscreen update'), findsOneWidget);
    expect(find.byKey(const ValueKey(1)), findsNothing);
    probe.reset();
    db.edit(add: [Triple(1, 'title', 'Unmounted update')]);
    await tester.pumpAndSettle();
    expect(probe.parts, isEmpty);
    expect(tester.takeException(), isNull);
  });
}
