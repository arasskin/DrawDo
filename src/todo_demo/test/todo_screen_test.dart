import 'dart:io';
import 'package:drawdo/data/datafriend.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:datafriend_todo_lab/database.dart';
import 'package:datafriend_todo_lab/todo_screen.dart';

String get library =>
    Platform.environment['DATAFRIEND_LIBRARY'] ??
    '${Directory.current.parent.path}/datafriend/build/native/libdatafriend.${Platform.isMacOS ? 'dylib' : 'so'}';

void main() {
  testWidgets('inline queries add, complete, filter, edit, delete and reopen', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(900, 740);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final dir = Directory.systemTemp.createTempSync('todo-ui-');
    final db = Datafriend.open(dir.path, libraryPath: library);
    addTearDown(() async {
      await tester.pumpWidget(const SizedBox());
      db.close();
      dir.deleteSync(recursive: true);
    });
    db.edit(
      add: [
        Triple(1, 'kind', 'task'),
        Triple(1, 'id', '1'),
        Triple(1, 'title', 'First task'),
        Triple(1, 'done', false),
        Triple(1, 'order', 0),
        Triple(1, 'extra', 'not projected by the UI'),
      ],
    );
    await tester.pumpWidget(
      MaterialApp(
        theme: todoTheme(),
        home: TodoScreen(database: db),
      ),
    );
    await tester.enterText(find.byKey(const ValueKey('new-task')), 'x' * 70000);
    await tester.tap(find.byKey(const ValueKey('add-task')));
    await tester.pumpAndSettle();
    expect(find.textContaining('oversized value'), findsOneWidget);
    expect(find.text('0 of 1 complete'), findsOneWidget);
    await tester.enterText(
      find.byKey(const ValueKey('new-task')),
      'Write tests',
    );
    await tester.tap(find.byKey(const ValueKey('add-task')));
    await tester.pumpAndSettle();
    expect(find.text('Write tests'), findsOneWidget);
    expect(find.textContaining('oversized value'), findsNothing);
    await tester.tap(find.byType(Checkbox).first);
    await tester.pumpAndSettle();
    expect(find.text('1 of 2 complete'), findsOneWidget);
    await tester.tap(find.text('Completed'));
    await tester.pumpAndSettle();
    expect(find.text('First task'), findsOneWidget);
    expect(find.text('Write tests'), findsNothing);
    // Toggling while filtered must remove the row from this query immediately.
    await tester.tap(find.byType(Checkbox).first);
    await tester.pumpAndSettle();
    expect(find.text('First task'), findsNothing);
    expect(find.text('0 of 2 complete'), findsOneWidget);
    await tester.tap(find.text('Active'));
    await tester.pumpAndSettle();
    expect(find.text('First task'), findsOneWidget);
    expect(find.text('Write tests'), findsOneWidget);
    await tester.tap(find.text('All'));
    await tester.pumpAndSettle();
    await tester.tap(find.byTooltip('Edit task').first);
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField).last, 'Edited task');
    await tester.tap(find.text('Save'));
    await tester.pumpAndSettle();
    expect(find.text('Edited task'), findsOneWidget);
    await tester.tap(find.byTooltip('Delete task').first);
    await tester.pumpAndSettle();
    expect(find.text('Edited task'), findsNothing);
    expect(find.text('Write tests'), findsOneWidget);
    expect(find.text('0 of 1 complete'), findsOneWidget);
    expect(db.exportEntities().containsKey(1), isFalse);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
    db.close();
    final reopened = Datafriend.open(dir.path, libraryPath: library);
    try {
      final all = reopened.subscribe(const DatafriendQuery.all());
      expect(
        all.where((t) => t.attribute == 'title').single.value,
        'Write tests',
      );
    } finally {
      reopened.close();
    }
  });

  testWidgets('dialog does not resurrect a task deleted while open', (
    tester,
  ) async {
    final dir = Directory.systemTemp.createTempSync('todo-dialog-');
    final db = openTodoDatabase(dir.path, libraryPath: library);
    addTearDown(() async {
      await tester.pumpWidget(const SizedBox());
      db.close();
      dir.deleteSync(recursive: true);
    });
    await tester.pumpWidget(
      MaterialApp(
        theme: todoTheme(),
        home: TodoScreen(database: db),
      ),
    );
    await tester.tap(find.byTooltip('Edit task').first);
    await tester.pumpAndSettle();
    final all = db.subscribe(const DatafriendQuery.all());
    final id = all
        .firstWhere((t) => t.attribute == 'title' && t.value == 'Plan the week')
        .entity;
    db.edit(remove: all.where((t) => t.entity == id).toList());
    await tester.enterText(find.byType(TextField).last, 'Resurrected');
    await tester.tap(find.text('Save'));
    await tester.pumpAndSettle();
    expect(find.text('Resurrected'), findsNothing);
    expect(all.any((t) => t.entity == id), isFalse);
    all.close();
    expect(tester.takeException(), isNull);
  });
}
