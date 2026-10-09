import 'dart:collection';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

import 'datafriend.dart';

/// Run a synchronous edit action, publish its error on failure, and clear a
/// previous error on success. Returns whether the action succeeded. The caller
/// owns/disposes [error]; ValueListenableBuilder can display it independently.
bool attempt(VoidCallback edit, {required ValueNotifier<Object?> error}) {
  try {
    edit();
  } catch (failure) {
    error.value = failure;
    return false;
  }
  error.value = null;
  return true;
}

/// Owns a query subscription for this widget's lifetime. Equivalent inline query
/// descriptions reuse the subscription. Edits belong in event callbacks, never
/// in [builder]; notifications invalidate caches and schedule a Flutter rebuild.
class DatafriendBuilder extends StatefulWidget {
  const DatafriendBuilder({
    super.key,
    required this.database,
    required this.query,
    required this.builder,
    this.orderBy,
    this.thenBy,
  });

  final Datafriend database;
  final DatafriendQuery query;
  final String? orderBy, thenBy;
  final Widget Function(BuildContext context, DatafriendRows view) builder;

  @override
  State<DatafriendBuilder> createState() => _DatafriendBuilderState();
}

class _DatafriendBuilderState extends State<DatafriendBuilder> {
  late DatafriendQuery _query;
  DatafriendView? _subscription;
  DatafriendRows? _rows;

  void _subscribe() {
    _query = _copyQuery(widget.query);
    final subscription = widget.database.subscribe(_query, onChanged: _changed);
    try {
      _rows = DatafriendRows._(
        widget.database,
        subscription.result(),
        widget.orderBy,
        widget.thenBy,
      );
      _subscription = subscription;
    } catch (_) {
      subscription.close();
      rethrow;
    }
  }

  void _changed() {
    _rows!._invalidate(_subscription!.changes);
    setState(() {});
  }

  void _release() {
    _subscription?.onChanged = null;
    _rows?._close();
    _subscription?.close();
    _rows = null;
    _subscription = null;
  }

  @override
  void initState() {
    super.initState();
    _subscribe();
  }

  @override
  void didUpdateWidget(DatafriendBuilder oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (!identical(widget.database, oldWidget.database) ||
        !_sameQuery(_query, widget.query)) {
      _release();
      _subscribe();
    } else if (widget.orderBy != oldWidget.orderBy ||
        widget.thenBy != oldWidget.thenBy) {
      _rows!._sort(widget.orderBy, widget.thenBy);
    }
  }

  @override
  Widget build(BuildContext context) => widget.builder(context, _rows!);

  @override
  void dispose() {
    _release();
    super.dispose();
  }
}

/// Read-only, live entity accessors into a shared native query result. Owned by
/// DatafriendBuilder; do not retain the view after unmount or a query change.
/// Missing sort fields come last; entity ID breaks ties. Only projected fields
/// are readable. Values decode lazily and are cached until their entity changes.
final class DatafriendRows extends ListBase<DatafriendEntity> {
  DatafriendRows._(this._database, this._result, this._orderBy, this._thenBy);

  final Datafriend _database;
  final DatafriendResult _result;
  String? _orderBy, _thenBy;
  DatafriendEntityIds? _ids;
  final Map<int, DatafriendEntity> _entities = {};
  bool _closed = false;

  void _check() {
    if (_closed) throw StateError('Datafriend builder view is closed');
  }

  DatafriendEntityIds get _ordered {
    _check();
    return _ids ??= _result.entities(orderBy: _orderBy, thenBy: _thenBy);
  }

  void _dropOrdering() {
    _ids?.close();
    _ids = null;
  }

  void _sort(String? orderBy, String? thenBy) {
    _orderBy = orderBy;
    _thenBy = thenBy;
    _dropOrdering();
  }

  void _invalidate(DatafriendChanges changes) {
    if (changes.reset) {
      for (final entity in _entities.values) {
        entity._values.clear();
      }
      _entities.clear();
    } else {
      for (final id in changes.entities) {
        _entities.remove(id)?._values.clear();
      }
    }
    if (changes.reset ||
        changes.membershipChanged ||
        changes.attributes.contains(_orderBy) ||
        changes.attributes.contains(_thenBy)) {
      _dropOrdering();
    }
  }

  @override
  int get length => _ordered.length;
  @override
  set length(int value) => throw UnsupportedError('Read-only query view');

  /// Count in the native result without decoding every row in Dart.
  int countWhere(String attribute, Object? value) {
    _check();
    return _result.countWhere(attribute, value);
  }

  @override
  DatafriendEntity operator [](int index) {
    final id = _ordered[index];
    return _entities.putIfAbsent(id, () => DatafriendEntity._(this, id));
  }

  @override
  void operator []=(int index, DatafriendEntity value) =>
      throw UnsupportedError('Read-only query view');

  void _close() {
    _closed = true;
    _dropOrdering();
    _entities.clear();
    _result.close();
  }
}

/// An entity ID and lazy field access, not a copied entity map. Accessors belong
/// to their builder view. Treat decoded byte arrays as read-only.
final class DatafriendEntity {
  DatafriendEntity._(this._view, this.id);
  final DatafriendRows _view;
  final int id;
  final Map<String, Object?> _values = {};

  /// Names currently visible in this query, including attributes storing null.
  List<String> get attributes {
    _view._check();
    return _view._result.attributes(id);
  }

  /// Owned triples for the currently projected fields. For selective retraction:
  /// `database.edit(remove: entity.triples)`. Unprojected fields are not included.
  List<Triple> get triples => List.unmodifiable([
    for (final attribute in attributes) Triple(id, attribute, this[attribute]),
  ]);

  /// Delete this whole database entity, including fields outside this query.
  /// Like every edit, blocks until affected results and notifications are current.
  void delete() {
    _view._check();
    _view._database.deleteEntity(id);
  }

  Object? operator [](String attribute) {
    _view._check();
    // A retained accessor always uses the current cache for this entity.
    final current = _view._entities.putIfAbsent(id, () => this);
    return current._values.putIfAbsent(
      attribute,
      () => _view._result.value(id, attribute),
    );
  }
}

// Capture mutable lists/byte literals so inline query comparison is structural,
// including when callers rebuild or mutate a description in place.
DatafriendQuery _copyQuery(DatafriendQuery query) => query.all
    ? const DatafriendQuery.all()
    : DatafriendQuery(
        List.unmodifiable([
          for (final (clause, include) in query.clauses)
            (
              QueryClause(
                clause.entity is int
                    ? Entity(clause.entity as int)
                    : clause.entity,
                clause.attribute,
                clause.value is Uint8List
                    ? Uint8List.fromList(clause.value as Uint8List)
                    : clause.value,
              ),
              include,
            ),
        ]),
      );

bool _sameTerm(Object? a, Object? b) => a is QueryVariable && b is QueryVariable
    ? a.index == b.index
    : a is Uint8List && b is Uint8List
    ? listEquals(a, b)
    : a == b;

bool _sameQuery(DatafriendQuery a, DatafriendQuery b) {
  if (a.all != b.all || a.clauses.length != b.clauses.length) {
    return false;
  }
  for (var i = 0; i < a.clauses.length; i++) {
    final (x, includeX) = a.clauses[i];
    final (y, includeY) = b.clauses[i];
    final entityY = y.entity is int ? Entity(y.entity as int) : y.entity;
    if (includeX != includeY ||
        !_sameTerm(x.entity, entityY) ||
        x.attribute != y.attribute ||
        !_sameTerm(x.value, y.value)) {
      return false;
    }
  }
  return true;
}
