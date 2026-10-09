import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:drawdo/app_controller.dart';
import 'native_app.dart';
import 'package:drawdo/model/document.dart';
import 'package:drawdo/ui/app.dart';

Future<AppController> openApp(
  WidgetTester tester, {
  AppDocument? document,
}) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final app = await nativeApp(initial: document);
  await tester.pumpWidget(
    RepaintBoundary(
      key: const ValueKey('capture'),
      child: DrawDoApp(controller: app),
    ),
  );
  await tester.pump(const Duration(milliseconds: 100));
  return app;
}

Future<void> openList(WidgetTester tester) async {
  await tester.tap(find.byType(NavigationDestination).last);
  await tester.pump(const Duration(milliseconds: 300));
}

void main() {
  testWidgets('add, complete, edit and delete tasks through the UI', (
    tester,
  ) async {
    final app = await openApp(tester);
    await openList(tester);
    await tester.enterText(
      find.byKey(const ValueKey('new-task')),
      'Water the plants',
    );
    await tester.tap(find.byTooltip('Add task'));
    await tester.pump(const Duration(milliseconds: 300));
    expect(app.document.tasks.single.title, 'Water the plants');
    await tester.tap(find.byType(Checkbox));
    expect(app.tasks.single.completed, isTrue);
    expect(tester.widget<Checkbox>(find.byType(Checkbox)).value, isFalse);
    await tester.pump();
    expect(tester.widget<Checkbox>(find.byType(Checkbox)).value, isTrue);
    expect(app.document.tasks.single.completed, isTrue);
    await tester.tap(find.text('Water the plants'));
    await tester.pump(const Duration(milliseconds: 300));
    await tester.enterText(
      find.descendant(
        of: find.byType(AlertDialog),
        matching: find.byType(TextField),
      ),
      'Water the garden',
    );
    await tester.tap(find.text('Save'));
    await tester.pump(const Duration(milliseconds: 300));
    expect(app.document.tasks.single.title, 'Water the garden');
    await tester.tap(find.byTooltip('Delete Water the garden'));
    await tester.pump();
    expect(app.document.tasks, isEmpty);
    expect(tester.takeException(), isNull);
  });

  testWidgets('drawing, undo, and erasing a line between sample points', (
    tester,
  ) async {
    final app = await openApp(tester);
    await tester.tap(find.byTooltip('Draw'));
    await tester.pump();
    final surface = find.byKey(const ValueKey('ink-space'));
    final start = tester.getTopLeft(surface) + const Offset(50, 100);
    await tester.dragFrom(start, const Offset(180, 50));
    await tester.pump();
    expect(app.document.strokes, hasLength(1));
    await tester.tap(find.byTooltip('Undo last stroke'));
    await tester.pump();
    expect(app.document.strokes, isEmpty);
    app.addStroke(CanvasPage.space, 0xff000000, [
      const InkPoint(.1, 100),
      const InkPoint(.9, 100),
    ]);
    await tester.pump();
    await tester.tap(find.byTooltip('Erase'));
    await tester.pump();
    await tester.tapAt(tester.getTopLeft(surface) + const Offset(195, 100));
    await tester.pump();
    expect(app.document.strokes, isEmpty);
    expect(tester.takeException(), isNull);
  });

  testWidgets('dragged stickers persist their position after settling', (
    tester,
  ) async {
    final app = await openApp(tester);
    final before = app.document.stickers.first;
    await tester.drag(
      find.byKey(const ValueKey('my-list')),
      const Offset(75, 90),
    );
    await tester.pump(const Duration(milliseconds: 450));
    await tester.pump(const Duration(milliseconds: 450));
    expect(app.document.stickers.first.x, greaterThan(before.x));
    expect(app.document.stickers.first.y, greaterThan(before.y));
    expect(tester.takeException(), isNull);
  });

  testWidgets('drawing on a scrolled list uses content coordinates', (
    tester,
  ) async {
    final app = await openApp(
      tester,
      document: AppDocument(
        tasks: List.generate(25, (i) => Task(id: '$i', title: 'Task $i')),
      ),
    );
    await openList(tester);
    await tester.drag(find.byType(ReorderableListView), const Offset(0, -350));
    await tester.pump(const Duration(milliseconds: 500));
    await tester.tap(find.byTooltip('Draw'));
    await tester.pump();
    final top = tester.getTopLeft(find.byKey(const ValueKey('ink-tasks')));
    await tester.dragFrom(top + const Offset(40, 80), const Offset(180, 40));
    await tester.pump();
    expect(app.document.strokes.single.page, CanvasPage.tasks);
    expect(app.document.strokes.single.points.first.y, greaterThan(200));
    expect(tester.takeException(), isNull);
  });

  testWidgets('narrow screen and open keyboard do not overflow', (
    tester,
  ) async {
    await openApp(tester);
    tester.view.physicalSize = const Size(320, 568);
    await tester.pump();
    await openList(tester);
    tester.view.viewInsets = const FakeViewPadding(bottom: 260);
    addTearDown(tester.view.resetViewInsets);
    await tester.pump();
    expect(tester.takeException(), isNull);
  });
}
