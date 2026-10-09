import 'dart:io';
import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:drawdo/app_controller.dart';
import 'package:drawdo/data/datafriend.dart';
import 'package:drawdo/data/app_views.dart';
import 'package:drawdo/model/document.dart';

String get library =>
    Platform.environment['DATAFRIEND_LIBRARY'] ??
    '${Directory.current.path}/../../datafriend/build/native/libdatafriend.${Platform.isMacOS ? 'dylib' : 'so'}';
void main() {
  test(
    'native network owner identity must match namespace and cannot be used after close',
    () async {
      final identity = LittlebearIdentity.ephemeralForTesting(
        libraryPath: library,
      );
      final other = LittlebearIdentity.ephemeralForTesting(
        libraryPath: library,
      );
      final root = Directory.systemTemp.createTempSync('datafriend-identity-');
      final store = Datafriend.open(
        root.path,
        namespace: identity.namespace,
        libraryPath: library,
      );
      addTearDown(() {
        store.close();
        identity.close();
        other.close();
        root.deleteSync(recursive: true);
      });
      expect(identity.namespace, matches(RegExp(r'^[0-9a-f]{64}$')));
      expect(
        () => LittlebearConfig(host: 'localhost', mode: LittlebearMode.push),
        throwsArgumentError,
      );
      expect(
        () => store.connect(
          LittlebearConfig(
            host: 'localhost',
            mode: LittlebearMode.push,
            identity: other,
          ),
        ),
        throwsArgumentError,
      );
      expect(store.network, isNull);
      store.connect(
        LittlebearConfig(
          host: 'localhost',
          mode: LittlebearMode.push,
          identity: identity,
        ),
      );
      store.disconnect();
      // Replicas need no private key.
      store.connect(
        LittlebearConfig(host: 'localhost', mode: LittlebearMode.pull),
      );
      store.disconnect();
      identity.close();
      expect(
        () => store.connect(
          LittlebearConfig(
            host: 'localhost',
            mode: LittlebearMode.push,
            identity: identity,
          ),
        ),
        throwsStateError,
      );
      expect(
        () => LittlebearIdentity.open('../bad', libraryPath: library),
        throwsStateError,
      );
    },
  );

  test('shared enum and packed facts round-trip atomically across stores', () {
    final a = Directory.systemTemp.createTempSync('datafriend-facts-a-');
    final b = Directory.systemTemp.createTempSync('datafriend-facts-b-');
    final first = Datafriend.open(a.path, libraryPath: library);
    final second = Datafriend.open(b.path, libraryPath: library);
    addTearDown(() {
      first.close();
      second.close();
      a.deleteSync(recursive: true);
      b.deleteSync(recursive: true);
    });
    first.edit(
      add: [
        Triple(7, Attribute.title, 'one'),
        Triple(7, Attribute.done, false),
        Triple(0xffffffff, Attribute.title, 'last'),
      ],
    );
    final title = (7 << 32) | Attribute.title.code;
    final done = (7 << 32) | Attribute.done.code;
    final facts = first.exportFacts();
    expect(facts.keys.toSet(), {
      title,
      done,
      (0xffffffff << 32) | Attribute.title.code,
    });
    var calls = 0;
    late DatafriendView view;
    view = second.subscribe(
      const DatafriendQuery([
        (QueryClause(7, Attribute.title, QueryVariable(0)), true),
        (QueryClause(7, Attribute.done, QueryVariable(1)), true),
      ]),
      onChanged: () {
        calls++;
        expect(view.get(7, 'title')!.value, 'one');
        expect(view.get(7, 'done')!.value, false);
      },
    );
    second.importFacts(facts);
    expect(calls, 1);
    expect(second.exportFacts(), facts);
    view.close();
    first.edit(add: [Triple(7, Attribute.title, 'two')]);
    expect(first.exportFacts()[done], facts[done]);
    second.importFacts({title: first.exportFacts()[title], done: null});
    final all = second.subscribe(const DatafriendQuery.all());
    expect(all.get(7, 'title')!.value, 'two');
    expect(all.get(7, 'done'), isNull);
    final before = second.exportFacts();
    final corrupt = Uint8List.fromList(facts[done]!)..[0] ^= 1;
    expect(
      () => second.importFacts({title: facts[title], done: corrupt}),
      throwsStateError,
    );
    expect(second.exportFacts(), before);
    expect(
      () => second.edit(add: [Triple(7, 'not-in-the-shared-schema', 1)]),
      throwsStateError,
    );
    expect(second.exportFacts(), before);
    second.deleteEntity(7);
    expect(all.get(7, 'title'), isNull);
    expect(all.get(0xffffffff, 'title')!.value, 'last');
    all.close();
    second.flush();
  });
  test(
    'query pairs select fields; entity IDs and reference values retain their types',
    () {
      final dir = Directory.systemTemp.createTempSync(
        'datafriend-query-pairs-',
      );
      final db = Datafriend.open(
        dir.path,
        libraryPath: library,
        durable: false,
      );
      addTearDown(() {
        db.close();
        dir.deleteSync(recursive: true);
      });
      db.edit(
        add: [
          Triple(1, 'kind', 'task'),
          Triple(1, 'title', 'First'),
          Triple(1, 'project', const Entity(7)),
          Triple(2, 'project', 7),
          Triple(0, 'title', 'Zero'),
          Triple(0xffffffff, 'title', 'Last'),
        ],
      );
      final plain = db.subscribe(
        const DatafriendQuery([
          (QueryClause(1, 'kind', 'task'), false),
          (QueryClause(1, 'title', QueryVariable(12)), true),
        ]),
      );
      final wrapped = db.subscribe(
        const DatafriendQuery([
          (QueryClause(Entity(1), 'kind', 'task'), false),
          (QueryClause(Entity(1), 'title', QueryVariable(12)), true),
        ]),
      );
      expect(plain.toList(), [Triple(1, 'title', 'First')]);
      final a = plain.result(), b = wrapped.result();
      expect(a.sharesWith(b), isTrue);
      a.close();
      b.close();
      var changed = 0;
      plain.onChanged = () => changed++;
      db.edit(add: [Triple(1, 'kind', 'other')]);
      expect(plain, isEmpty); // Excluded clauses still constrain membership.
      expect(changed, 1);
      final references = db.subscribe(
        const DatafriendQuery([
          (QueryClause(QueryVariable(23), 'project', Entity(7)), true),
        ]),
      );
      final numbers = db.subscribe(
        const DatafriendQuery([
          (QueryClause(QueryVariable(23), 'project', 7), true),
        ]),
      );
      expect(references.single.entity, 1);
      expect(references.single.value, const Entity(7));
      expect(numbers.single.entity, 2);
      expect(numbers.single.value, 7);
      for (final id in [0, 0xffffffff]) {
        final view = db.subscribe(
          DatafriendQuery([
            (QueryClause(id, 'title', const QueryVariable(63)), true),
          ]),
        );
        expect(view.single.entity, id);
        view.close();
      }
      for (final id in [-1, 0x100000000]) {
        expect(
          () => db.subscribe(
            DatafriendQuery([
              (QueryClause(id, 'title', const QueryVariable(0)), true),
            ]),
          ),
          throwsRangeError,
        );
      }
    },
  );

  test('mixed results sort missing attributes last, with entity ID ties', () {
    final dir = Directory.systemTemp.createTempSync('datafriend-order-');
    final db = Datafriend.open(dir.path, libraryPath: library, durable: false);
    final view = db.subscribe(const DatafriendQuery.all());
    final result = view.result();
    addTearDown(() {
      result.close();
      view.close();
      db.close();
      dir.deleteSync(recursive: true);
    });
    void ordered(List<int> expected, {String? orderBy, String? thenBy}) {
      final ids = result.entities(orderBy: orderBy, thenBy: thenBy);
      try {
        expect(ids, expected);
      } finally {
        ids.close();
      }
    }

    db.edit(
      add: [
        Triple(0, 'kind', 'sticker'),
        Triple(1, 'order', 7),
        Triple(2, 'kind', 'task'),
        Triple(2, 'order', -9),
        Triple(3, 'order', 7),
        Triple(3, 'part', 0),
        Triple(4, 'kind', 'sticker'),
      ],
    );
    ordered([2, 1, 3, 0, 4], orderBy: 'order');
    ordered([2, 3, 1, 0, 4], orderBy: 'order', thenBy: 'part');
    ordered([0, 1, 2, 3, 4]);
    ordered([0, 1, 2, 3, 4], orderBy: 'unknown');
    ordered([2, 1, 3, 0, 4], orderBy: 'unknown', thenBy: 'order');
    db.edit(add: [Triple(4, 'unknown', 1)]);
    ordered([4, 0, 1, 2, 3], orderBy: 'unknown');
    db.edit(remove: [Triple(2, 'order', -9)]);
    ordered([1, 3, 0, 2, 4], orderBy: 'order');
  });
  test(
    'default async persistence keeps callbacks synchronous and flushes on close',
    () {
      final dir = Directory.systemTemp.createTempSync('datafriend-flush-');
      var db = Datafriend.open(dir.path, libraryPath: library);
      try {
        final view = db.subscribe(const DatafriendQuery.all());
        var notifications = 0;
        view.onChanged = () => notifications++;
        db.edit(add: [Triple(1, 'title', 'saved')]);
        expect(notifications, 1);
        db.flush();
        expect(
          notifications,
          1,
        ); // Persistence never republishes query results.
        db.edit(add: [Triple(1, 'title', 'saved on close')]);
        db.close();
        db = Datafriend.open(dir.path, libraryPath: library);
        final reopened = db.subscribe(const DatafriendQuery.all());
        expect(reopened.get(1, 'title')?.value, 'saved on close');
      } finally {
        db.close();
        dir.deleteSync(recursive: true);
      }
    },
  );
  test(
    'change summaries describe joined result rows and are shared safely',
    () {
      final dir = Directory.systemTemp.createTempSync('datafriend-changes-');
      final db = Datafriend.open(
        dir.path,
        libraryPath: library,
        durable: false,
      );
      const query = DatafriendQuery([
        (QueryClause(QueryVariable(0), 'owner', QueryVariable(1)), false),
        (QueryClause(QueryVariable(1), 'name', 'Alex'), false),
        (QueryClause(QueryVariable(0), 'title', QueryVariable(2)), true),
      ]);
      final first = db.subscribe(query), second = db.subscribe(query);
      final observed = <DatafriendChanges>[];
      first.onChanged = () => observed.add(first.changes);
      second.onChanged = () => observed.add(second.changes);
      addTearDown(() {
        first.close();
        second.close();
        db.close();
        dir.deleteSync(recursive: true);
      });
      db.edit(
        add: [
          Triple(1, 'owner', const Entity(10)),
          Triple(1, 'title', 'one'),
          Triple(2, 'owner', const Entity(10)),
          Triple(2, 'title', 'two'),
          Triple(10, 'name', 'Alex'),
        ],
      );
      final initial = observed.first;
      expect(initial.entities, {1, 2});
      expect(initial.attributes, {'title'});
      expect(initial.membershipChanged, isTrue);
      observed.clear();
      db.edit(add: [Triple(1, 'title', 'updated')]);
      expect(observed.length, 2);
      for (final changes in observed) {
        expect(changes.reset, isFalse);
        expect(changes.entities, {1});
        expect(changes.attributes, {'title'});
        expect(changes.membershipChanged, isFalse);
      }
      expect(initial.entities, {1, 2}); // prior metadata owns its contents
      observed.clear();
      db.edit(add: [Triple(10, 'name', 'Other')]);
      expect(observed.length, 2);
      for (final changes in observed) {
        expect(changes.entities, {1, 2});
        expect(changes.attributes, {
          'title',
        }); // projected attributes, not 'name'
        expect(changes.membershipChanged, isTrue);
      }
      observed.clear();
      db.edit(add: [Triple(10, 'name', 'Other')]);
      expect(observed, isEmpty);
      expect(
        () => db.edit(add: [Triple(1, 'title', 'x' * 65535)]),
        throwsStateError,
      );
      expect(observed.length, 2);
      expect(observed.every((change) => change.reset), isTrue);
    },
  );

  test('value decoder preserves boundary values and owns binary results', () {
    final dir = Directory.systemTemp.createTempSync('datafriend-codec-');
    final db = Datafriend.open(dir.path, libraryPath: library, durable: false);
    final view = db.subscribe(const DatafriendQuery.all());
    final result = view.result();
    addTearDown(() {
      result.close();
      view.close();
      db.close();
      dir.deleteSync(recursive: true);
    });
    final backing = Uint8List.fromList([99, 0, 128, 255, 99]);
    final values = <Object?>[
      null,
      false,
      true,
      '',
      'ASCII',
      '\u0000é漢字🐻',
      '🐻é' * 1000,
      0,
      -1,
      0x7fffffffffffffff,
      -0x8000000000000000,
      0.0,
      -1.25,
      double.minPositive,
      double.maxFinite,
      const Entity(0),
      const Entity(0xffffffff),
      Uint8List(0),
      Uint8List.sublistView(backing, 1, 4),
    ];
    final triples = [
      for (var i = 0; i < values.length; i++) Triple(i + 1, 'value', values[i]),
    ];
    db.edit(add: triples);
    final retained = <Object?>[];
    for (var i = 0; i < values.length; i++) {
      expect(triples[i].value, values[i]);
      expect(result.value(i + 1, 'value'), values[i]);
      retained.add(result.value(i + 1, 'value'));
    }
    // Owned copies must survive an edit that replaces every underlying box.
    db.edit(
      add: [
        for (var i = 0; i < values.length; i++)
          Triple(i + 1, 'value', 'replaced'),
      ],
    );
    for (var i = 0; i < values.length; i++) {
      expect(retained[i], values[i]);
    }
    final bytes = triples.last.value as Uint8List;
    bytes[0] = 42;
    expect(triples.last.value, [0, 128, 255]);
    expect(backing, [99, 0, 128, 255, 99]);
  });

  test(
    'database edits need no view and result notifications track actual changes',
    () {
      final dir = Directory.systemTemp.createTempSync('datafriend-live-');
      final db = Datafriend.open(
        dir.path,
        libraryPath: library,
        durable: false,
      );
      addTearDown(() {
        db.close();
        dir.deleteSync(recursive: true);
      });
      db.edit(add: [Triple(1, 'title', 'before'), Triple(1, 'hidden', 7)]);
      var changes = 0;
      late DatafriendView view;
      late DatafriendResult result;
      view = db.subscribe(
        const DatafriendQuery([
          (QueryClause(QueryVariable(0), 'title', QueryVariable(1)), true),
        ]),
        onChanged: () {
          changes++;
          expect(result.toList(), view.toList());
        },
      );
      result = view.result();
      addTearDown(() {
        result.close();
        view.close();
      });
      db.edit(add: [Triple(1, 'hidden', 8)]);
      expect(changes, 0);
      db.edit(add: [Triple(1, 'title', 'after')]);
      expect(changes, 1);
      expect(result.single.value, 'after');
      db.edit(add: [Triple(1, 'title', 'after')]);
      db.edit(remove: [Triple(1, 'title', 'stale')]);
      expect(changes, 1);
      db.edit(remove: [Triple(1, 'title', 'after')]);
      expect(changes, 2);
      expect(result, isEmpty);
    },
  );

  test(
    'direct field reads own values and native ID indexes have explicit lifetimes',
    () {
      final dir = Directory.systemTemp.createTempSync('datafriend-fields-');
      final db = Datafriend.open(
        dir.path,
        libraryPath: library,
        durable: false,
      );
      final view = db.subscribe(const DatafriendQuery.all()),
          result = view.result();
      addTearDown(() {
        result.close();
        view.close();
        db.close();
        dir.deleteSync(recursive: true);
      });
      expect(result.value(1, 'later'), isNull);
      db.edit(
        add: [
          Triple(1, 'order', 7),
          Triple(2, 'order', -9),
          Triple(3, 'order', 7),
          Triple(1, 'part', 1),
          Triple(2, 'part', 0),
          Triple(3, 'part', 0),
          Triple(1, 'bytes', Uint8List.fromList([1, 2, 3])),
          Triple(1, 'title', 'héllo 🐻'),
          Triple(1, 'nil', null),
          Triple(1, 'later', 42),
          Triple(1, 'done', true),
          Triple(2, 'done', false),
        ],
      );
      expect(
        result.value(1, 'later'),
        42,
      ); // absent attribute IDs were not cached
      expect(result.containsField(1, 'nil'), isTrue);
      expect(result.containsField(99, 'nil'), isFalse);
      expect(result.value(1, 'nil'), isNull);
      expect(result.findEntity('title', 'héllo 🐻'), 1);
      expect(result.findEntity('title', 'missing'), isNull);
      expect(result.countWhere('done', true), 1);
      final ids = result.entities(orderBy: 'order', thenBy: 'part');
      expect(ids, [2, 3, 1]);
      expect(() => ids[-1], throwsRangeError);
      expect(() => ids[3], throwsRangeError);
      expect(() => ids[0] = 7, throwsUnsupportedError);
      final text = result.value(1, 'title'),
          bytes = result.value(1, 'bytes') as Uint8List;
      bytes[0] = 99;
      expect(result.value(1, 'bytes'), [1, 2, 3]);
      db.edit(
        add: [
          Triple(1, 'title', 'new'),
          Triple(1, 'bytes', Uint8List(10000)),
          Triple(1, 'order', -10),
        ],
      );
      expect(text, 'héllo 🐻');
      expect(bytes, [99, 2, 3]);
      expect(ids, [2, 3, 1]); // fixed index, live field reads
      final fresh = result.entities(orderBy: 'order');
      expect(fresh, [1, 2, 3]);
      fresh.close();
      db.edit(add: [Triple(2, 'order', 'invalid')]);
      expect(() => result.entities(orderBy: 'order'), throwsStateError);
      expect(() => result.value(0x100000000, 'title'), throwsRangeError);
      view.close();
      db.close();
      expect(result.value(1, 'title'), 'new');
      result.close();
      expect(() => result.value(1, 'title'), throwsStateError);
      expect(ids, [2, 3, 1]);
      ids.close();
      expect(() => ids.length, throwsStateError);
    },
  );

  test('namespace and 32-bit IDs survive reopening', () {
    final dir = Directory.systemTemp.createTempSync('datafriend-namespace-');
    Datafriend open(String namespace) => Datafriend.open(
      dir.path,
      namespace: namespace,
      libraryPath: library,
      durable: false,
    );
    var db = open('icare');
    expect(db.newEntity(), 1);
    final view = db.subscribe(const DatafriendQuery.all());
    view.add(Triple(7, 'title', 'retained'));
    final snapshot = view.result();
    expect(() => Triple(0x100000000, 'title', 'bad'), throwsRangeError);
    expect(() => Triple(-1, 'title', 'bad'), throwsRangeError);
    expect(() => Triple(7, 'ref', const Entity(0x100000000)), throwsRangeError);
    view.clear();
    view.close();
    db.close();
    expect(snapshot, isEmpty);
    snapshot.close();
    expect(() => open('wrong'), throwsStateError);
    db = open('icare');
    expect(db.newEntity(), 8);
    db.close();
    dir.deleteSync(recursive: true);
  });

  test(
    'identical subscriptions share live results and independent handles',
    () {
      final dir = Directory.systemTemp.createTempSync('datafriend-sharing-');
      final db = Datafriend.open(
        dir.path,
        libraryPath: library,
        durable: false,
      );
      const query = DatafriendQuery([
        (QueryClause(QueryVariable(0), 'title', QueryVariable(1)), true),
      ]);
      final first = db.subscribe(query), second = db.subscribe(query);
      first.add(Triple(1, 'title', 'before'));
      final retained = first.result(), sibling = second.result();
      expect(retained.sharesWith(sibling), isTrue);
      sibling.close();
      db.editViews({
        first: ViewEdit(remove: [Triple(1, 'title', 'before')]),
        second: ViewEdit(add: [Triple(1, 'title', 'after')]),
      });
      expect(first.single.value, 'after');
      expect(second.single.value, 'after');
      expect(retained.single.value, 'after');
      first.close();
      second.add(Triple(2, 'title', 'another'));
      final third = db.subscribe(query);
      final current = second.result(), joined = third.result();
      expect(current.sharesWith(joined), isTrue);
      current.close();
      joined.close();
      second.close();
      third.close();
      db.close();
      expect(retained.map((t) => t.value).toSet(), {'after', 'another'});
      retained.close();
      dir.deleteSync(recursive: true);
    },
  );

  test('blocking set edits publish every query and retain native values', () {
    final dir = Directory.systemTemp.createTempSync('datafriend-dart-');
    final db = Datafriend.open(dir.path, libraryPath: library);
    final all = db.subscribe(const DatafriendQuery.all());
    final incomplete = db.subscribe(
      const DatafriendQuery([
        (QueryClause(QueryVariable(0), 'done', false), true),
        (QueryClause(QueryVariable(0), 'title', QueryVariable(1)), true),
      ]),
    );
    final id = db.newEntity();
    final bytes = Uint8List.fromList([1, 2, 3]);
    final values = <String, Object?>{
      'done': false,
      'title': 'task',
      'nil': null,
      'field0': -42,
      'field1': 1.25,
      'ref': Entity(id),
      'bytes': bytes,
    };
    all.addAll(values.entries.map((e) => Triple(id, e.key, e.value)));
    bytes[0] = 99;
    final snapshot = incomplete.result();
    expect(snapshot.length, 2);
    incomplete.add(Triple(id, 'done', true));
    expect(incomplete, isEmpty); // already published on the same Dart stack
    expect(all.contains(Triple(id, 'done', true)), isTrue);
    expect(snapshot, isEmpty);
    expect(all.remove(Triple(id, 'title', 'stale')), isFalse);
    for (final t in all) {
      if (t.attribute == 'bytes') {
        expect(t.value, [1, 2, 3]);
      }
      if (t.attribute == 'field0') {
        expect(t.value, -42);
      }
      if (t.attribute == 'ref') {
        expect(t.value, Entity(id));
      }
    }
    final before = all.toSet();
    expect(
      () => all.addAll([
        Triple(id, 'title', 'bad'),
        Triple(99, 'big', 'x' * 65535),
      ]),
      throwsStateError,
    );
    expect(all.toSet(), before);
    final export = db.exportEntities();
    db.importEntities({id: null});
    expect(all, isEmpty);
    db.importEntities(export);
    expect(all.toSet(), before);
    all.close();
    incomplete.close();
    db.close();
    expect(snapshot, isEmpty);
    snapshot.close();
    expect(() => snapshot.length, throwsStateError);
    final reopened = Datafriend.open(dir.path, libraryPath: library);
    final view = reopened.subscribe(const DatafriendQuery.all());
    expect(view.toSet(), before);
    view.close();
    reopened.close();
    dir.deleteSync(recursive: true);
  });

  test(
    'app commits synchronously, chunks drawings, reopens, rolls back errors',
    () async {
      final dir = Directory.systemTemp.createTempSync('datafriend-app-');
      AppViews store() =>
          AppViews(Datafriend.open(dir.path, libraryPath: library));
      var controller = AppController(store());
      await controller.load();
      expect(controller.ready, isTrue);
      expect(controller.document.stickers.length, 3);
      controller.addTask('first');
      final id = controller.document.tasks.single.id;
      controller.toggleTask(id);
      expect(controller.document.tasks.single.completed, isTrue);
      expect(controller.saving, isFalse);
      controller.editTask(id, 'x' * 65535);
      expect(controller.saveError, isNotNull);
      expect(controller.document.tasks.single.title, 'first');
      controller.retrySave();
      expect(controller.saveError, isNotNull);
      expect(controller.document.tasks.single.title, 'first');
      controller.editTask(id, 'second');
      expect(controller.saveError, isNull);
      controller.addStroke(
        CanvasPage.space,
        0xff112233,
        List.generate(5000, (i) => InkPoint(i / 5000, i.toDouble())),
      );
      expect(controller.document.strokes.single.points.length, 5000);
      final expected = controller.document.toJson();
      controller.dispose();
      controller = AppController(store());
      await controller.load();
      expect(controller.document.toJson(), expected);
      controller.undoStroke(CanvasPage.space);
      expect(controller.document.strokes, isEmpty);
      controller.deleteTask(id);
      expect(controller.document.tasks, isEmpty);
      controller.dispose();
      dir.deleteSync(recursive: true);
    },
  );
}
