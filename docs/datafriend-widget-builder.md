# Inline Datafriend queries in Flutter

`src/app/lib/data/datafriend_builder.dart` provides the subscription lifecycle
widget. Query descriptions and edits use the existing Datafriend API:

```dart
DatafriendBuilder(
  database: database,
  query: const DatafriendQuery([
    (QueryClause(QueryVariable(0), 'kind', 'task'), false),
    (QueryClause(QueryVariable(0), 'title', QueryVariable(1)), true),
    (QueryClause(QueryVariable(0), 'done', QueryVariable(2)), true),
    (QueryClause(QueryVariable(0), 'order', QueryVariable(3)), true),
  ]),
  orderBy: 'order',
  builder: (context, view) => ListView.builder(
    itemCount: view.length,
    itemBuilder: (context, index) {
      final entity = view[index];
      return CheckboxListTile(
        key: ValueKey(entity.id),
        title: Text(entity['title'] as String),
        value: entity['done'] as bool,
        onChanged: (value) => database.edit(
          add: [Triple(entity.id, 'done', value!)],
        ),
      );
    },
  ),
)
```

Queries are lists of `(clause, include)` pairs. Every clause constrains matches;
`true` also returns its matched triples, and `false` only filters. At least one
clause must return triples. Only included attributes are available through the
result; include any fields used for rendering or sorting. Mixed
entities missing a sorting field come last, and entity ID breaks ties. Present
sort values must be integers. With no sort fields, entity IDs determine order.

In the entity position, `QueryClause(42, ...)` and `QueryClause(Entity(42), ...)`
are equivalent. In the value position, `Entity(7)` means a reference to entity 7,
while `7` is an ordinary integer. Query variables use arbitrary IDs from 0 to 63;
equal IDs bind the same value within a query. Their numbers have no relationship
to clause position or inclusion flags. The Dart API derives the native protocol's
projection indices internally; persisted values and the native protocol are unchanged.

The builder subscribes at mount and provides a live, read-only `DatafriendRows`
list. Entities are lightweight ID/field accessors, not eagerly copied maps or
typed model objects. Decoded fields cache per accessed entity, including nulls.
Treat returned byte arrays as read-only. `view.countWhere(attribute, value)` counts
in native code without decoding every entity in Dart.

On publication, the wrapper synchronously reads the change summary, invalidates
affected caches, and calls `setState`. Membership or sorting-field changes also
release the ordering index, which is recreated on demand. Existing field reads
are current before `edit()` returns; Flutter performs the rebuild on its next
frame. Identical native query results may be shared by independent subscriptions.

Fresh but structurally equal inline query descriptions retain the subscription.
Changing the query or database closes the old subscription and shared-result
handle; changing only sorting retains the query. Unmount releases the result,
subscription and ordered-ID buffer. The application owns the database lifetime.

Use views/accessors within their owning builder's lifetime. Across asynchronous
dialogs, retain entity IDs and initial scalar values rather than the view, and
check current existence if deletion can occur while the dialog is open. The demo
does this for rename. Edit from event callbacks, not during a widget build.
Database errors propagate to the caller; the demo displays them locally.

## Edit errors and entity helpers

The Flutter helper `attempt(action, error: notifier)` runs a synchronous action,
returns true on success, and stores the caught exception on failure. A successful
action clears an earlier error. It does not create a transaction around multiple
calls; atomicity remains the responsibility of each database edit.

```dart
final error = ValueNotifier<Object?>(null);

attempt(
  () => database.edit(add: [Triple(entity.id, 'done', true)]),
  error: error,
);

// Remove the whole database entity, including unprojected attributes.
attempt(entity.delete, error: error);

// Enumerate the attributes this query actually returned.
for (final attribute in entity.attributes) {
  print('$attribute: ${entity[attribute]}');
}

// Alternatively, retract only the currently projected triples.
attempt(() => database.edit(remove: entity.triples), error: error);
```

Dart does not pass local variables by reference. The caller-owned
`ValueNotifier<Object?>` is the mutable error slot; a `ValueListenableBuilder`
displays it and rebuilds only the error area. Dispose the notifier with its
widget. The exception itself is retained, so callers can inspect its type rather
than only receiving a string.

`entity.attributes` returns an owned list of projected attribute names, including
attributes whose value is null. It enumerates one native row and copies only
names, not other entities or values. `entity.triples` materializes that row's
projected facts on demand. `entity.delete()` delegates to
`database.deleteEntity(entity.id)`, which uses the existing atomic whole-record
removal operation and synchronous notification path. It does not decode fields
or reconstruct triples in Dart. Missing entities are a no-op. Neither deletion
method cascades to other entities that reference the deleted ID.

The interactive demo uses an order-only task-list subscription, one subscription
per mounted task row for title/completion, and a completion-count subscription.
Its root widget manages only text entry, filter selection and error presentation.
See [per-row queries and rebuild verification](datafriend-row-queries.md).
Historical comparison adapters are retained separately and are not part of the
interactive Datafriend path.
