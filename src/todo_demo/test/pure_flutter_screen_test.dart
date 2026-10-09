import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:datafriend_todo_lab/build_probe.dart';
import 'package:datafriend_todo_lab/pure_flutter_screen.dart';
import 'package:datafriend_todo_lab/todo_screen.dart' show todoTheme;

void main() {
  testWidgets(
    'setState baseline rebuilds the screen and implements todo actions',
    (tester) async {
      tester.view.physicalSize = const Size(900, 740);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final key = GlobalKey<PureFlutterScreenState>();
      final probe = TodoBuildProbe();
      await tester.pumpWidget(
        MaterialApp(
          theme: todoTheme(),
          home: PureFlutterScreen(
            key: key,
            initialTasks: [
              for (var i = 0; i < 1000; i++) FlutterTask(i + 1, 'Task $i', i),
            ],
            onBuild: probe.record,
          ),
        ),
      );
      await tester.pumpAndSettle();
      final mounted = probe.rows.keys.toSet();
      expect(mounted.length, lessThan(1000));
      void expectWholeScreen() {
        expect(probe.parts['screen'], 1);
        expect(probe.parts['list'], 1);
        expect(probe.parts['counter'], 1);
        expect(probe.rows.keys.toSet(), mounted);
        expect(probe.rows.values.every((n) => n == 1), isTrue);
      }

      probe.reset();
      key.currentState!.renameTask(1, 'Renamed');
      await tester.pumpAndSettle();
      expectWholeScreen();
      expect(find.text('Renamed'), findsOneWidget);

      probe.reset();
      await tester.tap(find.byType(Checkbox).first);
      await tester.pumpAndSettle();
      expectWholeScreen();
      expect(find.text('1 of 1000 complete'), findsOneWidget);

      probe.reset();
      key.currentState!.setOrder(1, 2);
      await tester.pumpAndSettle();
      expectWholeScreen();
      expect(
        (tester.widget<ListTile>(find.byType(ListTile).first).title as Text)
            .data,
        'Task 1',
      );

      await tester.tap(find.text('Completed'));
      await tester.pumpAndSettle();
      expect(find.byType(ListTile), findsOneWidget);
      expect(find.text('Renamed'), findsOneWidget);
      await tester.tap(find.byTooltip('Edit task'));
      await tester.pumpAndSettle();
      await tester.enterText(find.byType(TextField).last, 'From dialog');
      await tester.tap(find.text('Save'));
      await tester.pumpAndSettle();
      expect(find.text('From dialog'), findsOneWidget);
      await tester.tap(find.byTooltip('Delete task'));
      await tester.pumpAndSettle();
      expect(find.byType(ListTile), findsNothing);
      expect(find.text('0 of 999 complete'), findsOneWidget);
      await tester.tap(find.text('Active'));
      await tester.pumpAndSettle();
      await tester.enterText(find.byKey(const ValueKey('new-task')), 'Added');
      await tester.tap(find.byKey(const ValueKey('add-task')));
      await tester.pumpAndSettle();
      expect(key.currentState!.tasks.last.title, 'Added');
      expect(
        key.currentState!.tasks.map((task) => task.id).toSet().length,
        1000,
      );
      expect(find.text('0 of 1000 complete'), findsOneWidget);
      expect(tester.takeException(), isNull);
    },
  );
}
