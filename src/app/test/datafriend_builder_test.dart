import 'dart:io';
import 'dart:typed_data';

import 'package:drawdo/data/datafriend.dart';
import 'package:drawdo/data/datafriend_builder.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

String get library =>
    Platform.environment['DATAFRIEND_LIBRARY'] ??
    '${Directory.current.path}/../../datafriend/build/native/libdatafriend.${Platform.isMacOS ? 'dylib' : 'so'}';

DatafriendQuery tasks({bool? done}) => DatafriendQuery([
  (QueryClause(QueryVariable(0), 'kind', 'task'), true),
  (QueryClause(QueryVariable(0), 'title', QueryVariable(1)), true),
  (QueryClause(QueryVariable(0), 'done', done ?? QueryVariable(2)), true),
]);

void main() {
  testWidgets(
    'numeric and wrapped IDs reuse the view; include flags replace it',
    (tester) async {
      final dir = Directory.systemTemp.createTempSync('builder-pairs-');
      final db = Datafriend.open(
        dir.path,
        libraryPath: library,
        durable: false,
      );
      addTearDown(() async {
        await tester.pumpWidget(const SizedBox());
        db.close();
        dir.deleteSync(recursive: true);
      });
      db.edit(add: [Triple(1, 'kind', 'task'), Triple(1, 'title', 'First')]);
      late DatafriendRows current;
      Future<void> show(Object id, bool includeKind) => tester.pumpWidget(
        MaterialApp(
          home: DatafriendBuilder(
            database: db,
            query: DatafriendQuery([
              (QueryClause(id, 'kind', 'task'), includeKind),
              (QueryClause(id, 'title', const QueryVariable(0)), true),
            ]),
            builder: (_, view) {
              current = view;
              return Text(view.single['title'] as String);
            },
          ),
        ),
      );
      await show(1, false);
      final original = current;
      expect(current.single.attributes, ['title']);
      await show(const Entity(1), false);
      expect(identical(original, current), isTrue);
      await show(1, true);
      expect(identical(original, current), isFalse);
      expect(current.single.attributes, unorderedEquals(['kind', 'title']));
      expect(() => original.length, throwsStateError);
    },
  );

  test('attempt publishes the exception and clears it after success', () {
    final error = ValueNotifier<Object?>(null);
    addTearDown(error.dispose);
    final failure = StateError('edit failed');
    var notifications = 0;
    error.addListener(() => notifications++);
    expect(attempt(() => throw failure, error: error), isFalse);
    expect(identical(error.value, failure), isTrue);
    expect(notifications, 1);
    expect(attempt(() {}, error: error), isTrue);
    expect(error.value, isNull);
    expect(notifications, 2);
    expect(attempt(() {}, error: error), isTrue);
    expect(notifications, 2);
  });

  testWidgets(
    'entity attributes, projected retraction and whole-entity deletion',
    (tester) async {
      final dir = Directory.systemTemp.createTempSync('builder-entity-');
      var db = Datafriend.open(dir.path, libraryPath: library, durable: false);
      addTearDown(() async {
        await tester.pumpWidget(const SizedBox());
        db.close();
        dir.deleteSync(recursive: true);
      });
      db.edit(
        add: [
          Triple(1, 'title', 'Visible'),
          Triple(1, 'hidden', 'Unprojected'),
          Triple(1, 'nullable', null),
          Triple(2, 'title', 'Other'),
          Triple(2, 'hidden', 'Other hidden'),
        ],
      );
      final all = db.subscribe(const DatafriendQuery.all());
      final result = all.result();
      final names = result.attributes(1);
      expect(names, unorderedEquals(['title', 'hidden', 'nullable']));
      expect(result.attributes(99), isEmpty);
      expect(() => names.add('bad'), throwsUnsupportedError);
      expect(() => result.attributes(-1), throwsRangeError);
      late DatafriendRows rows;
      await tester.pumpWidget(
        MaterialApp(
          home: DatafriendBuilder(
            database: db,
            query: const DatafriendQuery([
              (QueryClause(QueryVariable(0), 'title', QueryVariable(1)), true),
            ]),
            builder: (_, view) {
              rows = view;
              return Text('${view.length}');
            },
          ),
        ),
      );
      final first = rows[0];
      expect(first.attributes, ['title']);
      final projected = first.triples;
      expect(projected, [Triple(1, 'title', 'Visible')]);
      db.edit(remove: projected);
      expect(first.attributes, isEmpty);
      expect(result.value(1, 'hidden'), 'Unprojected');
      expect(result.containsField(1, 'nullable'), isTrue);
      expect(names, unorderedEquals(['title', 'hidden', 'nullable']));
      await tester.pump();
      expect(find.text('1'), findsOneWidget);
      // Same live accessor can delete by identity even after leaving its query.
      first.delete();
      expect(result.attributes(1), isEmpty);
      expect(db.exportEntities().containsKey(1), isFalse);
      expect(result.value(2, 'hidden'), 'Other hidden');
      expect(() => db.deleteEntity(-1), throwsRangeError);
      first.delete(); // Missing entity is a no-op.
      rows.single.delete();
      expect(rows.isEmpty, isTrue);
      expect(result.attributes(2), isEmpty);
      await tester.pump();
      expect(find.text('0'), findsOneWidget);
      await tester.pumpWidget(const SizedBox());
      expect(() => first.attributes, throwsStateError);
      expect(() => first.delete(), throwsStateError);
      result.close();
      all.close();
      db.close();
      db = Datafriend.open(dir.path, libraryPath: library, durable: false);
      expect(db.exportEntities(), isEmpty);
    },
  );

  testWidgets(
    'live fields, selective rebuilds, filtered membership and disposal',
    (tester) async {
      final dir = Directory.systemTemp.createTempSync('builder-live-');
      final db = Datafriend.open(
        dir.path,
        libraryPath: library,
        durable: false,
      );
      addTearDown(() async {
        await tester.pumpWidget(const SizedBox());
        db.close();
        dir.deleteSync(recursive: true);
      });
      db.edit(
        add: [
          Triple(1, 'kind', 'task'),
          Triple(1, 'title', 'First'),
          Triple(1, 'done', false),
          Triple(2, 'kind', 'task'),
          Triple(2, 'title', 'Second'),
          Triple(2, 'done', true),
        ],
      );
      late DatafriendRows all, active;
      var allBuilds = 0, activeBuilds = 0;
      await tester.pumpWidget(
        MaterialApp(
          home: Column(
            children: [
              DatafriendBuilder(
                database: db,
                query: tasks(),
                builder: (_, view) {
                  all = view;
                  allBuilds++;
                  return Text(view.map((e) => e['title']).join(','));
                },
              ),
              DatafriendBuilder(
                database: db,
                query: tasks(done: false),
                builder: (_, view) {
                  active = view;
                  activeBuilds++;
                  return Text('active: ${view.length}');
                },
              ),
            ],
          ),
        ),
      );
      final first = all[0], second = all[1];
      expect(first['title'], 'First');
      final initialAll = allBuilds, initialActive = activeBuilds;
      db.edit(add: [Triple(99, 'unrelated', 1)]);
      await tester.pump();
      expect(allBuilds, initialAll);
      expect(activeBuilds, initialActive);

      db.edit(add: [Triple(2, 'title', 'Renamed')]);
      expect(
        second['title'],
        'Renamed',
      ); // Already current before Flutter rebuilds.
      expect(identical(first, all[0]), isTrue);
      expect(allBuilds, initialAll);
      await tester.pump();
      expect(find.text('First,Renamed'), findsOneWidget);
      expect(activeBuilds, initialActive);
      db.edit(add: [Triple(2, 'title', 'Again')]);
      expect(
        second['title'],
        'Again',
      ); // Retained accessor across multiple edits.

      db.edit(add: [Triple(1, 'done', true)]);
      expect(active.isEmpty, isTrue);
      await tester.pump();
      expect(find.text('active: 0'), findsOneWidget);
      expect(all.countWhere('done', true), 2);

      // A rejected native transaction resets caches but leaves consistent results.
      expect(
        () => db.edit(add: [Triple(1, 'title', Uint8List(70000))]),
        throwsStateError,
      );
      expect(first['title'], 'First');
      await tester.pump();
      expect(find.text('First,Again'), findsOneWidget);
      await tester.pumpWidget(const SizedBox());
      expect(() => all.length, throwsStateError);
      db.edit(add: [Triple(1, 'title', 'Unmounted')]);
      await tester.pump();
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'equivalent queries reuse views; predicates, database and ordering can change',
    (tester) async {
      final dir = Directory.systemTemp.createTempSync('builder-query-');
      final db = Datafriend.open(
        '${dir.path}/a',
        libraryPath: library,
        durable: false,
      );
      final other = Datafriend.open(
        '${dir.path}/b',
        libraryPath: library,
        durable: false,
      );
      addTearDown(() async {
        await tester.pumpWidget(const SizedBox());
        db.close();
        other.close();
        dir.deleteSync(recursive: true);
      });
      db.edit(
        add: [
          Triple(1, 'kind', 'task'),
          Triple(1, 'title', 'First'),
          Triple(1, 'done', false),
          Triple(2, 'kind', 'task'),
          Triple(2, 'title', 'Second'),
          Triple(2, 'done', true),
          Triple(2, 'order', 10),
        ],
      );
      other.edit(add: [Triple(7, 'title', 'Other')]);
      late DatafriendRows current;
      Future<void> show(
        Datafriend database,
        DatafriendQuery query, {
        String? order,
      }) async {
        await tester.pumpWidget(
          MaterialApp(
            home: DatafriendBuilder(
              database: database,
              query: query,
              orderBy: order,
              builder: (_, view) {
                current = view;
                return Text(view.map((e) => '${e.id}').join(','));
              },
            ),
          ),
        );
      }

      await show(db, tasks());
      final initial = current;
      await show(db, tasks()); // Fresh objects, equal query data.
      expect(identical(initial, current), isTrue);
      await show(db, tasks(done: true));
      expect(find.text('2'), findsOneWidget);
      expect(() => initial.length, throwsStateError);
      await show(db, const DatafriendQuery.all());
      final unsorted = current;
      await show(db, const DatafriendQuery.all(), order: 'order');
      expect(identical(unsorted, current), isTrue);
      expect(find.text('2,1'), findsOneWidget);
      db.edit(add: [Triple(1, 'order', -1)]);
      await tester.pump();
      expect(find.text('1,2'), findsOneWidget);
      db.edit(remove: [Triple(1, 'order', -1)]);
      await tester.pump();
      expect(find.text('2,1'), findsOneWidget);
      await show(other, const DatafriendQuery.all());
      expect(find.text('7'), findsOneWidget);
      expect(() => unsorted.length, throwsStateError);
      db.edit(add: [Triple(3, 'title', 'Old database')]);
      await tester.pump();
      expect(find.text('7'), findsOneWidget);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'inline byte literals compare by content and changes replace subscriptions',
    (tester) async {
      final dir = Directory.systemTemp.createTempSync('builder-bytes-');
      final db = Datafriend.open(
        dir.path,
        libraryPath: library,
        durable: false,
      );
      addTearDown(() async {
        await tester.pumpWidget(const SizedBox());
        db.close();
        dir.deleteSync(recursive: true);
      });
      db.edit(
        add: [
          Triple(1, 'bytes', Uint8List.fromList([1])),
          Triple(2, 'bytes', Uint8List.fromList([2])),
        ],
      );
      late DatafriendRows current;
      Future<void> show(Uint8List bytes) => tester.pumpWidget(
        MaterialApp(
          home: DatafriendBuilder(
            database: db,
            query: DatafriendQuery([
              (QueryClause(QueryVariable(0), 'bytes', bytes), true),
            ]),
            builder: (_, view) {
              current = view;
              return Text('${view.single.id}');
            },
          ),
        ),
      );
      final bytes = Uint8List.fromList([1]);
      await show(bytes);
      final first = current;
      await show(Uint8List.fromList([1]));
      expect(identical(current, first), isTrue);
      bytes[0] = 2;
      await show(bytes);
      expect(find.text('2'), findsOneWidget);
      expect(() => first.length, throwsStateError);
    },
  );
}
