import 'dart:collection';
import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:typed_data';
import 'datafriend_profile.dart';
import 'attributes.dart';
export 'attributes.dart';
part 'datafriend_network.dart';
part 'datafriend_identity.dart';

String _attributeName(Object attribute) => switch (attribute) {
  Attribute a => a.wireName,
  String name => name,
  _ => throw ArgumentError.value(
    attribute,
    'attribute',
    'Expected Attribute or String',
  ),
};

/// Reference to an unsigned 32-bit entity ID in this namespace. In attribute
/// values, distinguishes an entity reference from an ordinary integer.
final class Entity {
  const Entity(this.id);
  final int id;
  @override
  bool operator ==(Object other) => other is Entity && id == other.id;
  @override
  int get hashCode => id.hashCode;
}

final class Triple {
  Triple(this.entity, Object attribute, Object? value)
    : attribute = _attributeName(attribute),
      _value = _encodeValue(value) {
    _checkEntity(entity);
  }
  Triple._(this.entity, this.attribute, this._value);
  final int entity;
  final String attribute;
  final Uint8List _value;
  Object? get value => _decodeValue(_value);
  @override
  bool operator ==(Object other) =>
      other is Triple &&
      entity == other.entity &&
      attribute == other.attribute &&
      _equal(_value, other._value);
  @override
  int get hashCode => Object.hash(entity, attribute, Object.hashAll(_value));
}

void _checkEntity(int id) {
  if (id < 0 || id > 0xffffffff) {
    throw RangeError.range(id, 0, 0xffffffff, 'entity');
  }
}

bool _equal(List<int> a, List<int> b) {
  if (a.length != b.length) return false;
  for (var i = 0; i < a.length; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

/// Query-local variable identity (0..63). Repeated IDs join clauses; the numbers
/// have no relationship to clause positions or which fields are returned.
final class QueryVariable {
  const QueryVariable(this.index);
  final int index;
}

final class QueryClause {
  const QueryClause(this.entity, this._attribute, this.value);
  final Object entity; // int ID, Entity or QueryVariable
  final Object _attribute;
  String get attribute => _attributeName(_attribute);
  final Object? value; // scalar/bytes/Entity or QueryVariable
}

final class DatafriendQuery {
  /// All clauses constrain matches; true includes that clause's matched triples
  /// in the result, false uses it only as a condition. At least one must be true.
  const DatafriendQuery(this.clauses) : all = false;
  const DatafriendQuery.all() : clauses = const [], all = true;
  final List<(QueryClause, bool)> clauses;
  final bool all;
}

final class _Buffer extends ffi.Struct {
  external ffi.Pointer<ffi.Uint8> data;
  @ffi.UintPtr()
  external int size;
}

final class _Native {
  _Native(this.library);
  factory _Native.load(String? path) => _Native(
    path != null
        ? ffi.DynamicLibrary.open(path)
        : Platform.isIOS
        ? ffi.DynamicLibrary.process()
        : ffi.DynamicLibrary.open(
            Platform.isMacOS ? 'libdatafriend.dylib' : 'libdatafriend.so',
          ),
  );
  late final identityOpen = library
      .lookupFunction<
        ffi.Pointer<ffi.Void> Function(
          ffi.Pointer<ffi.Uint8>,
          ffi.UintPtr,
          ffi.Int,
          ffi.Pointer<_Buffer>,
        ),
        ffi.Pointer<ffi.Void> Function(
          ffi.Pointer<ffi.Uint8>,
          int,
          int,
          ffi.Pointer<_Buffer>,
        )
      >('df_identity_open');
  late final identitySign = library
      .lookupFunction<
        ffi.Int Function(
          ffi.Pointer<ffi.Void>,
          ffi.Pointer<ffi.Uint8>,
          ffi.UintPtr,
          ffi.Pointer<_Buffer>,
        ),
        int Function(
          ffi.Pointer<ffi.Void>,
          ffi.Pointer<ffi.Uint8>,
          int,
          ffi.Pointer<_Buffer>,
        )
      >('df_identity_sign');
  late final identityClose = library
      .lookupFunction<
        ffi.Void Function(ffi.Pointer<ffi.Void>),
        void Function(ffi.Pointer<ffi.Void>)
      >('df_identity_close');
  late final identityFinalizer = ffi.NativeFinalizer(
    library.lookup('df_identity_close'),
  );
  final ffi.DynamicLibrary library;
  late final attributeSchema = library
      .lookupFunction<
        ffi.Int32 Function(ffi.Pointer<_Buffer>),
        int Function(ffi.Pointer<_Buffer>)
      >('df_attribute_schema');
  late final alloc = library
      .lookupFunction<
        ffi.Pointer<ffi.Void> Function(ffi.UintPtr),
        ffi.Pointer<ffi.Void> Function(int)
      >('df_alloc');
  late final free = library
      .lookupFunction<
        ffi.Void Function(ffi.Pointer<ffi.Void>),
        void Function(ffi.Pointer<ffi.Void>)
      >('df_free');
  late final open = library
      .lookupFunction<
        ffi.Pointer<ffi.Void> Function(
          ffi.Pointer<ffi.Uint8>,
          ffi.UintPtr,
          ffi.Pointer<ffi.Uint8>,
          ffi.UintPtr,
          ffi.Int,
          ffi.Pointer<_Buffer>,
        ),
        ffi.Pointer<ffi.Void> Function(
          ffi.Pointer<ffi.Uint8>,
          int,
          ffi.Pointer<ffi.Uint8>,
          int,
          int,
          ffi.Pointer<_Buffer>,
        )
      >('df_open_namespace');
  late final close = library
      .lookupFunction<
        ffi.Void Function(ffi.Pointer<ffi.Void>),
        void Function(ffi.Pointer<ffi.Void>)
      >('df_close');
  late final closeChecked = library
      .lookupFunction<
        ffi.Int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<_Buffer>),
        int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<_Buffer>)
      >('df_close_checked');
  // Ordinary, non-leaf calls: native edits/queries execute synchronously here.
  late final call = library
      .lookupFunction<
        ffi.Int Function(
          ffi.Pointer<ffi.Void>,
          ffi.Pointer<ffi.Uint8>,
          ffi.UintPtr,
          ffi.Pointer<_Buffer>,
        ),
        int Function(
          ffi.Pointer<ffi.Void>,
          ffi.Pointer<ffi.Uint8>,
          int,
          ffi.Pointer<_Buffer>,
        )
      >('df_call');
  late final result = library
      .lookupFunction<
        ffi.Pointer<ffi.Void> Function(
          ffi.Pointer<ffi.Void>,
          ffi.Uint64,
          ffi.Pointer<_Buffer>,
        ),
        ffi.Pointer<ffi.Void> Function(
          ffi.Pointer<ffi.Void>,
          int,
          ffi.Pointer<_Buffer>,
        )
      >('df_result');
  late final info = library
      .lookupFunction<
        ffi.Int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<_Buffer>),
        int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<_Buffer>)
      >('df_result_info');
  late final release = library
      .lookupFunction<
        ffi.Void Function(ffi.Pointer<ffi.Void>),
        void Function(ffi.Pointer<ffi.Void>)
      >('df_result_release');
  late final same = library
      .lookupFunction<
        ffi.Int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<ffi.Void>),
        int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<ffi.Void>)
      >('df_result_same');
  late final list = library
      .lookupFunction<
        ffi.Int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<_Buffer>),
        int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<_Buffer>)
      >('df_result_list');
  late final changes = library
      .lookupFunction<
        ffi.Int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<_Buffer>),
        int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<_Buffer>)
      >('df_result_changes');
  late final attribute = library
      .lookupFunction<
        ffi.Uint32 Function(
          ffi.Pointer<ffi.Void>,
          ffi.Pointer<ffi.Uint8>,
          ffi.UintPtr,
        ),
        int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<ffi.Uint8>, int)
      >('df_result_attribute');
  late final value = library
      .lookupFunction<
        ffi.Int Function(
          ffi.Pointer<ffi.Void>,
          ffi.Uint32,
          ffi.Uint32,
          ffi.Pointer<_Buffer>,
        ),
        int Function(ffi.Pointer<ffi.Void>, int, int, ffi.Pointer<_Buffer>)
      >('df_result_value');
  late final entities = library
      .lookupFunction<
        ffi.Int Function(
          ffi.Pointer<ffi.Void>,
          ffi.Uint32,
          ffi.Uint32,
          ffi.Pointer<_Buffer>,
        ),
        int Function(ffi.Pointer<ffi.Void>, int, int, ffi.Pointer<_Buffer>)
      >('df_result_entities');
  late final attributes = library
      .lookupFunction<
        ffi.Int Function(
          ffi.Pointer<ffi.Void>,
          ffi.Uint32,
          ffi.Pointer<_Buffer>,
        ),
        int Function(ffi.Pointer<ffi.Void>, int, ffi.Pointer<_Buffer>)
      >('df_result_attributes');
  late final find = library
      .lookupFunction<
        ffi.Int64 Function(
          ffi.Pointer<ffi.Void>,
          ffi.Uint32,
          ffi.Pointer<ffi.Uint8>,
          ffi.UintPtr,
        ),
        int Function(ffi.Pointer<ffi.Void>, int, ffi.Pointer<ffi.Uint8>, int)
      >('df_result_find');
  late final countValue = library
      .lookupFunction<
        ffi.Int64 Function(
          ffi.Pointer<ffi.Void>,
          ffi.Uint32,
          ffi.Pointer<ffi.Uint8>,
          ffi.UintPtr,
        ),
        int Function(ffi.Pointer<ffi.Void>, int, ffi.Pointer<ffi.Uint8>, int)
      >('df_result_count_value');
  late final bufferFinalizer = ffi.NativeFinalizer(library.lookup('df_free'));
  T input<T>(List<int> bytes, T Function(ffi.Pointer<ffi.Uint8>, int) action) {
    final p = allocate<ffi.Uint8>(bytes.isEmpty ? 1 : bytes.length);
    try {
      p.asTypedList(bytes.length).setAll(0, bytes);
      return action(p, bytes.length);
    } finally {
      free(p.cast());
    }
  }

  late final resultFinalizer = ffi.NativeFinalizer(
    library.lookup('df_result_release'),
  );
  late final engineFinalizer = ffi.NativeFinalizer(library.lookup('df_close'));
  ffi.Pointer<T> allocate<T extends ffi.NativeType>(int bytes) {
    final p = alloc(bytes);
    if (p == ffi.nullptr) throw StateError('Native allocation failed');
    return p.cast<T>();
  }

  T output<T>(T Function(ffi.Pointer<_Buffer>) action) {
    final out = allocate<_Buffer>(ffi.sizeOf<_Buffer>());
    out.ref.data = ffi.nullptr;
    out.ref.size = 0;
    try {
      return action(out);
    } finally {
      free(out.ref.data.cast());
      free(out.cast());
    }
  }

  Never failure(ffi.Pointer<_Buffer> out) => throw StateError(
    out.ref.size == 0
        ? 'Datafriend failed (no error buffer)'
        : utf8.decode(
            out.ref.data.asTypedList(out.ref.size),
            allowMalformed: true,
          ),
  );
}

final class ViewEdit {
  const ViewEdit({this.remove = const [], this.add = const []});
  final Iterable<Triple> remove, add;
}

enum DatafriendDurability { buffered, synchronous, asynchronous }

/// Owns a native service. close() is deterministic; the native finalizer is only
/// a fallback. Edits return after the transaction, indexes, live query results
/// and synchronous change notifications are complete.
final class Datafriend implements ffi.Finalizable {
  Datafriend._(this._native, this._handle, this.namespace) {
    _native.engineFinalizer.attach(this, _handle, detach: this);
  }
  final _Native _native;
  ffi.Pointer<ffi.Void> _handle;
  final String namespace;
  DatafriendNetwork? _network;
  DatafriendNetwork? get network => _network;

  /// Network operations are separate from local query subscriptions.
  late final DatafriendSync sync = DatafriendSync._(this);

  DatafriendNetwork _attachNetwork(
    LittlebearConfig config,
    LittlebearIdentity? identity,
  ) {
    disconnect();
    final connection = _network = DatafriendNetwork._(this, config, identity);
    connection._schedule();
    return connection;
  }

  // Compatibility for callers migrating to push()/subscribe().
  DatafriendNetwork connect(LittlebearConfig config) =>
      config.mode == LittlebearMode.push
      ? sync.push(config, identity: config.identity!)
      : sync.subscribe(config);

  void disconnect() {
    _network?._close();
    _network = null;
  }

  final Map<int, DatafriendView> _views = {};

  /// Called after changed-view notifications. Callbacks must not throw or edit.
  void Function()? onPublished;
  factory Datafriend.open(
    String directory, {
    bool? durable,
    DatafriendDurability? durability,
    String namespace = 'default',
    String? libraryPath,
    LittlebearConfig? network,
  }) {
    if (durable != null && durability != null) {
      throw ArgumentError('Specify durability or legacy durable, not both');
    }
    final mode =
        durability ??
        (durable == null
            ? DatafriendDurability.asynchronous
            : durable
            ? DatafriendDurability.synchronous
            : DatafriendDurability.buffered);
    final native = _Native.load(libraryPath);
    native.output((out) {
      if (native.attributeSchema(out) != 0) native.failure(out);
      if (utf8.decode(out.ref.data.asTypedList(out.ref.size)) !=
          datafriendAttributeSchema) {
        throw StateError('Datafriend attribute schema mismatch');
      }
    });
    final path = utf8.encode(directory);
    final name = utf8.encode(namespace);
    if (name.isEmpty || name.length > 1024) {
      throw ArgumentError.value(
        namespace,
        'namespace',
        'Must be 1–1024 UTF-8 bytes',
      );
    }
    final input = native.allocate<ffi.Uint8>(path.length);
    input.asTypedList(path.length).setAll(0, path);
    try {
      final namespaceInput = native.allocate<ffi.Uint8>(name.length);
      try {
        namespaceInput.asTypedList(name.length).setAll(0, name);
        return native.output((out) {
          final handle = native.open(
            input,
            path.length,
            namespaceInput,
            name.length,
            mode.index,
            out,
          );
          if (handle == ffi.nullptr) native.failure(out);
          final store = Datafriend._(native, handle, namespace);
          if (network != null) store.connect(network);
          return store;
        });
      } finally {
        native.free(namespaceInput.cast());
      }
    } finally {
      native.free(input.cast());
    }
  }
  void _check() {
    if (_handle == ffi.nullptr) throw StateError('Datafriend is closed');
  }

  Uint8List _call(_Writer w) {
    final data = w.finish();
    final mutates =
        data[1] == 3 ||
        data[1] == 7 ||
        data[1] == 8 ||
        data[1] == 10 ||
        data[1] == 13 ||
        data[1] == 15;
    final Uint8List result;
    try {
      result = _send(data);
    } catch (_) {
      // A failed edit may have rebuilt native results during rollback. Tell
      // adapters to drop caches before returning the original error.
      if (mutates) {
        for (final view in _views.values) {
          view._resetChanges = true;
          view.onChanged?.call();
        }
        if (_views.isNotEmpty) onPublished?.call();
      }
      rethrow;
    }
    if (mutates) {
      // Schedule independently of query notifications (including no subscribers).
      if (data[1] != 15) _network?._schedule();
      final r = _Reader(result), count = r.u32();
      final changed = [for (var i = 0; i < count; i++) r.u64()];
      for (final id in changed) {
        final view = _views[id];
        if (view != null) {
          view._resetChanges = false;
          view.onChanged?.call();
        }
      }
      if (changed.isNotEmpty) onPublished?.call();
    }
    return result;
  }

  Uint8List _send(Uint8List data) {
    if (datafriendProfiling) DatafriendProfile.begin();
    try {
      _check();
      if (datafriendProfiling) {
        DatafriendProfile.count(DatafriendCounter.requestBytes, data.length);
      }
      final input = _native.allocate<ffi.Uint8>(data.length);
      input.asTypedList(data.length).setAll(0, data);
      try {
        return _native.output((out) {
          if (datafriendProfiling) DatafriendProfile.begin();
          final int status;
          try {
            status = _native.call(_handle, input, data.length, out);
          } finally {
            if (datafriendProfiling) {
              final edit = data[1] == 3 || data[1] == 8 || data[1] == 10;
              final read = data[1] == 2;
              DatafriendProfile.end(
                edit
                    ? DatafriendPhase.nativeEdit
                    : read
                    ? DatafriendPhase.nativeRead
                    : DatafriendPhase.nativeOther,
              );
              DatafriendProfile.count(
                edit
                    ? DatafriendCounter.editCalls
                    : read
                    ? DatafriendCounter.liveReadCalls
                    : DatafriendCounter.otherCalls,
              );
            }
          }
          if (status != 0) _native.failure(out);
          if (datafriendProfiling) DatafriendProfile.begin();
          try {
            if (datafriendProfiling) {
              DatafriendProfile.count(
                DatafriendCounter.responseBytes,
                out.ref.size,
              );
            }
            return out.ref.size == 0
                ? Uint8List(0)
                : Uint8List.fromList(out.ref.data.asTypedList(out.ref.size));
          } finally {
            if (datafriendProfiling) {
              DatafriendProfile.end(DatafriendPhase.replyCopy);
            }
          }
        });
      } finally {
        _native.free(input.cast());
      }
    } finally {
      if (datafriendProfiling) DatafriendProfile.end(DatafriendPhase.transport);
    }
  }

  DatafriendView subscribe(
    DatafriendQuery query, {
    void Function()? onChanged,
  }) {
    final w = _Writer.request(1)
      ..u8(query.all ? 1 : 0)
      ..u32(query.clauses.length);
    final projection = <int>[];
    for (var i = 0; i < query.clauses.length; i++) {
      final (c, include) = query.clauses[i];
      if (include) projection.add(i);
      // Entity position supplies the type context for plain numeric IDs.
      final entity = c.entity;
      if (entity is! int && entity is! Entity && entity is! QueryVariable) {
        throw ArgumentError('Expected an entity ID, Entity or QueryVariable');
      }
      w.term(entity is int ? Entity(entity) : entity);
      w.string(c.attribute);
      w.term(c.value);
    }
    w.u32(projection.length);
    for (final i in projection) {
      w.u32(i);
    }
    final id = _Reader(_call(w)).u64();
    return _views[id] = DatafriendView._(this, id)..onChanged = onChanged;
  }

  /// Apply triples directly to this namespace, independently of any query.
  /// Retractions compare the current value; additions replace an EA pair.
  void edit({
    Iterable<Triple> remove = const [],
    Iterable<Triple> add = const [],
  }) {
    late final _Writer w;
    if (datafriendProfiling) DatafriendProfile.begin();
    try {
      final removals = remove.toList(), additions = add.toList();
      w = _Writer.request(10)..u32(removals.length);
      for (final t in removals) {
        w.triple(t);
      }
      w.u32(additions.length);
      for (final t in additions) {
        w.triple(t);
      }
    } finally {
      if (datafriendProfiling) {
        DatafriendProfile.end(DatafriendPhase.requestEncode);
      }
    }
    _call(w);
  }

  /// Compatibility helper for benchmark fixtures. Query handles do not scope edits.
  void editViews(Map<DatafriendView, ViewEdit> batches) {
    for (final view in batches.keys) {
      view._check();
      if (!identical(view.store, this)) {
        throw ArgumentError('View belongs to another store');
      }
    }
    edit(
      remove: batches.values.expand((e) => e.remove),
      add: batches.values.expand((e) => e.add),
    );
  }

  /// Delete the whole entity, including attributes absent from any query view.
  /// Atomic and blocking through result publication; a missing entity is a no-op.
  void deleteEntity(int entity) {
    _checkEntity(entity);
    importEntities({entity: null});
  }

  int newEntity() => _Reader(_call(_Writer.request(5))).u64();
  Map<int, Uint8List> exportEntities() {
    final r = _Reader(_call(_Writer.request(6)));
    final count = r.u32();
    return {for (var i = 0; i < count; i++) r.u64(): r.blob()};
  }

  void importEntities(Map<int, Uint8List?> records) {
    final w = _Writer.request(7)..u32(records.length);
    for (final e in records.entries) {
      w.u64(e.key);
      w.u8(e.value == null ? 0 : 1);
      if (e.value != null) w.blob(e.value!);
    }
    _call(w);
  }

  /// Packed entity/attribute keys and fingerprint-prefixed value records.
  /// This is the storage/network unit; exporting all facts is a diagnostic helper.
  Map<int, Uint8List> exportFacts() {
    final r = _Reader(_call(_Writer.request(12)));
    final count = r.u32();
    return {for (var i = 0; i < count; i++) r.u64(): r.blob()};
  }

  /// One atomic batch; null removes the fact. Notifications finish before return.
  void importFacts(Map<int, Uint8List?> records) {
    final w = _Writer.request(13)..u32(records.length);
    for (final e in records.entries) {
      w.u64(e.key);
      w.u8(e.value == null ? 0 : 1);
      if (e.value != null) w.blob(e.value!);
    }
    _call(w);
  }

  /// Blocking disk barrier for explicit checkpoints; ordinary edits do not wait
  /// for a flush in the default asynchronous mode. Flush failures throw.
  void flush() {
    _call(_Writer.request(11));
  }

  /// Flush pending writes, stop the worker and release the database. Releases
  /// the handle even if flushing fails; that failure is reported to the caller.
  void close() {
    if (_handle == ffi.nullptr) return;
    disconnect();
    onPublished = null;
    for (final view in _views.values) {
      view.onChanged = null;
      view._changeResult?.close();
    }
    _views.clear();
    _native.engineFinalizer.detach(this);
    final handle = _handle;
    _handle = ffi.nullptr;
    _native.output((out) {
      if (_native.closeChecked(handle, out) != 0) _native.failure(out);
    });
  }
}

/// Live set of writable facts. Adding replaces an entity/attribute pair, even
/// when its previous value differs. A batch retracts first, then adds atomically.
/// Query predicates may immediately exclude an added fact after the commit.
final class DatafriendView extends SetBase<Triple> {
  DatafriendView._(this.store, this.id);
  final Datafriend store;
  final int id;
  bool _closed = false;
  bool _resetChanges = true;
  DatafriendResult? _changeResult;

  /// Read inside onChanged. Describes actual result changes, including joined
  /// entities, or requests a complete cache reset after an unsuccessful edit.
  /// Read immediately: native summaries are not a retained event history.
  DatafriendChanges get changes {
    _check();
    return _resetChanges
        ? const DatafriendChanges.reset()
        : (_changeResult ??= result()).changes;
  }

  /// Result changed, or caches need resetting after an unsuccessful edit.
  /// Runs before the store notification and before the edit returns/throws.
  /// Must not throw or edit the store.
  void Function()? onChanged;
  void _check() {
    store._check();
    if (_closed) throw StateError('View is closed');
  }

  List<Triple> _read() {
    _check();
    final r = _Reader(store._call(_Writer.request(2)..u64(id)));
    if (datafriendProfiling) DatafriendProfile.begin();
    try {
      final n = r.u32();
      if (datafriendProfiling) {
        DatafriendProfile.count(DatafriendCounter.decodedTriples, n);
      }
      return List.generate(n, (_) => r.triple(), growable: false);
    } finally {
      if (datafriendProfiling) {
        DatafriendProfile.end(DatafriendPhase.tripleDecode);
      }
    }
  }

  /// Read one current EA pair without materializing the view.
  Triple? get(int entity, String attribute) {
    _checkEntity(entity);
    _check();
    final r = _Reader(
      store._call(
        _Writer.request(9)
          ..u64(id)
          ..u64(entity)
          ..string(attribute),
      ),
    );
    if (datafriendProfiling) DatafriendProfile.begin();
    try {
      if (r.u8() == 0) return null;
      if (datafriendProfiling) {
        DatafriendProfile.count(DatafriendCounter.decodedTriples);
      }
      return r.triple();
    } finally {
      if (datafriendProfiling) {
        DatafriendProfile.end(DatafriendPhase.tripleDecode);
      }
    }
  }

  DatafriendResult result() {
    if (datafriendProfiling) DatafriendProfile.begin();
    try {
      _check();
      return store._native.output((out) {
        if (datafriendProfiling) DatafriendProfile.begin();
        final ffi.Pointer<ffi.Void> p;
        try {
          p = store._native.result(store._handle, id, out);
        } finally {
          if (datafriendProfiling) {
            DatafriendProfile.end(DatafriendPhase.nativeSnapshot);
            DatafriendProfile.count(DatafriendCounter.snapshotCalls);
          }
        }
        if (p == ffi.nullptr) store._native.failure(out);
        return DatafriendResult._(store._native, p);
      });
    } finally {
      if (datafriendProfiling) {
        DatafriendProfile.end(DatafriendPhase.snapshotManage);
      }
    }
  }

  /// Set convenience: all edits target the database, not this query's membership.
  void edit({
    Iterable<Triple> remove = const [],
    Iterable<Triple> add = const [],
  }) {
    _check();
    store.edit(remove: remove, add: add);
  }

  @override
  bool add(Triple value) {
    final changed = !contains(value);
    edit(add: [value]);
    return changed;
  }

  @override
  void addAll(Iterable<Triple> elements) => edit(add: elements);
  @override
  bool remove(Object? value) {
    if (value is! Triple || !contains(value)) return false;
    edit(remove: [value]);
    return true;
  }

  @override
  void removeAll(Iterable<Object?> elements) =>
      edit(remove: elements.whereType<Triple>());
  @override
  void clear() => edit(remove: _read());
  @override
  void removeWhere(bool Function(Triple) test) =>
      edit(remove: _read().where(test));
  @override
  void retainWhere(bool Function(Triple) test) =>
      edit(remove: _read().where((t) => !test(t)));
  @override
  void retainAll(Iterable<Object?> elements) {
    final keep = elements.toSet();
    removeWhere((t) => !keep.contains(t));
  }

  @override
  int get length {
    final s = result();
    try {
      return s.length;
    } finally {
      s.close();
    }
  }

  @override
  Iterator<Triple> get iterator => _read().iterator;
  @override
  bool contains(Object? element) =>
      element is Triple && _read().contains(element);
  @override
  Triple? lookup(Object? element) {
    for (final t in _read()) {
      if (t == element) return t;
    }
    return null;
  }

  @override
  Set<Triple> toSet() => _read().toSet();
  void close() {
    if (_closed) return;
    _changeResult?.close();
    if (store._handle != ffi.nullptr) store._call(_Writer.request(4)..u64(id));
    store._views.remove(id);
    onChanged = null;
    _closed = true;
  }
}

/// Owned change metadata; attributes refer to projected fields, not input edits.
final class DatafriendChanges {
  const DatafriendChanges.reset()
    : reset = true,
      membershipChanged = true,
      entities = const {},
      attributes = const {};
  DatafriendChanges._(
    this.reset,
    this.membershipChanged,
    Set<int> entities,
    Set<String> attributes,
  ) : entities = Set.unmodifiable(entities),
      attributes = Set.unmodifiable(attributes);
  final bool reset, membershipChanged;
  final Set<int> entities;
  final Set<String> attributes;
}

/// Shared live native query result. Updates in place until the final subscription
/// closes; retained handles then keep the final contents. Iteration copies current facts.
final class DatafriendResult extends IterableBase<Triple>
    implements ffi.Finalizable {
  DatafriendResult._(this._native, this._handle) {
    try {
      _scratch = _native.allocate<_Buffer>(ffi.sizeOf<_Buffer>());
    } catch (_) {
      _native.release(_handle);
      rethrow;
    }
    _native.resultFinalizer.attach(this, _handle, detach: this);
    _native.bufferFinalizer.attach(this, _scratch.cast(), detach: this);
  }
  late final ffi.Pointer<_Buffer> _scratch;
  final _Native _native;
  ffi.Pointer<ffi.Void> _handle;
  void _check() {
    if (_handle == ffi.nullptr) throw StateError('Result is closed');
  }

  int _attribute(String name) {
    _check();
    return attributeIds[name] ?? 0;
  }

  int _readValue(int entity, String attribute) {
    _check();
    _checkEntity(entity);
    final status = _native.value(
      _handle,
      entity,
      _attribute(attribute),
      _scratch,
    );
    if (status < 0) throw StateError('Datafriend result is unavailable');
    if (datafriendProfiling) {
      DatafriendProfile.count(DatafriendCounter.fieldReads);
    }
    return status;
  }

  /// Direct lookup in the shared native map. Decodes only this field, with no triple.
  /// Missing fields return null; use containsField to distinguish a stored null.
  Object? value(int entity, String attribute) {
    if (_readValue(entity, attribute) != 0) return null;
    if (datafriendProfiling) {
      DatafriendProfile.count(DatafriendCounter.decodedValues);
    }
    // Never expose borrowed memory. Strings and byte values become owned Dart values
    // before returning; edits can then release/replace the native box safely.
    return _decodeValue(_scratch.ref.data.asTypedList(_scratch.ref.size));
  }

  bool containsField(int entity, String attribute) =>
      _readValue(entity, attribute) == 0;

  /// Attribute names in this entity's projected result row, in unspecified order.
  /// Copies names only, not values or other rows. Missing entities return [].
  List<String> attributes(int entity) {
    _check();
    _checkEntity(entity);
    return _native.output((out) {
      if (_native.attributes(_handle, entity, out) != 0) _native.failure(out);
      final r = _Reader(
        Uint8List.fromList(out.ref.data.asTypedList(out.ref.size)),
      );
      final count = r.u32();
      return List.unmodifiable([
        for (var i = 0; i < count; i++) utf8.decode(r.blob()),
      ]);
    });
  }

  DatafriendChanges get changes {
    _check();
    return _native.output((out) {
      if (_native.changes(_handle, out) != 0) _native.failure(out);
      final reader = _Reader(out.ref.data.asTypedList(out.ref.size));
      final reset = reader.u8() != 0, membership = reader.u8() != 0;
      final entityCount = reader.u32();
      final entities = {for (var i = 0; i < entityCount; i++) reader.u32()};
      final attributeCount = reader.u32();
      final attributes = {
        for (var i = 0; i < attributeCount; i++) utf8.decode(reader.blob()),
      };
      return DatafriendChanges._(reset, membership, entities, attributes);
    });
  }

  int? findEntity(String attribute, Object? value) {
    _check();
    final attr = _attribute(attribute);
    final id = _native.input(
      _encodeValue(value),
      (p, n) => _native.find(_handle, attr, p, n),
    );
    if (id == -2) throw StateError('Datafriend result is unavailable');
    return id < 0 ? null : id;
  }

  int countWhere(String attribute, Object? value) {
    _check();
    final attr = _attribute(attribute);
    final count = _native.input(
      _encodeValue(value),
      (p, n) => _native.countValue(_handle, attr, p, n),
    );
    if (count < 0) throw StateError('Datafriend result is unavailable');
    return count;
  }

  /// Native-owned ID index, not copied facts. Present ordering fields must be
  /// int64 values. Sorts ascending, missing last for each field, then by entity ID.
  /// Without ordering fields, sorts by entity ID alone.
  /// IDs are fixed for this index; field reads always use the live result.
  DatafriendEntityIds entities({String? orderBy, String? thenBy}) {
    _check();
    int order(String? name) {
      if (name == null) return 0;
      // An unknown attribute is absent from every row: all rows tie on it.
      return _attribute(name);
    }

    return _native.output((out) {
      if (_native.entities(_handle, order(orderBy), order(thenBy), out) != 0) {
        _native.failure(out);
      }
      final ids = DatafriendEntityIds._(
        _native,
        out.ref.data.cast(),
        out.ref.size ~/ 4,
      );
      out.ref.data = ffi.nullptr;
      out.ref.size = 0; // transfer ownership from output buffer
      if (datafriendProfiling) {
        DatafriendProfile.count(DatafriendCounter.entityIds, ids.length);
      }
      return ids;
    });
  }

  int _count() {
    _check();
    return _native.output((out) {
      if (_native.info(_handle, out) != 0) _native.failure(out);
      final r = _Reader(
        Uint8List.fromList(out.ref.data.asTypedList(out.ref.size)),
      );
      return r.u64();
    });
  }

  @override
  int get length => _count();
  @override
  Triple elementAt(int index) => toList(growable: false)[index];

  @override
  Iterator<Triple> get iterator => toList(growable: false).iterator;
  bool sharesWith(DatafriendResult other) {
    _check();
    other._check();
    return _native.same(_handle, other._handle) != 0;
  }

  @override
  List<Triple> toList({bool growable = true}) {
    if (datafriendProfiling) DatafriendProfile.begin();
    try {
      _check();
      return _native.output((out) {
        if (datafriendProfiling) DatafriendProfile.begin();
        final int status;
        try {
          status = _native.list(_handle, out);
        } finally {
          if (datafriendProfiling) {
            DatafriendProfile.end(DatafriendPhase.nativeSnapshotList);
            DatafriendProfile.count(DatafriendCounter.snapshotLists);
          }
        }
        if (status != 0) _native.failure(out);
        if (datafriendProfiling) DatafriendProfile.begin();
        final Uint8List bytes;
        try {
          bytes = Uint8List.fromList(out.ref.data.asTypedList(out.ref.size));
          if (datafriendProfiling) {
            DatafriendProfile.count(
              DatafriendCounter.responseBytes,
              out.ref.size,
            );
          }
        } finally {
          if (datafriendProfiling) {
            DatafriendProfile.end(DatafriendPhase.replyCopy);
          }
        }
        if (datafriendProfiling) DatafriendProfile.begin();
        try {
          final r = _Reader(bytes), n = r.u32();
          if (datafriendProfiling) {
            DatafriendProfile.count(DatafriendCounter.decodedTriples, n);
          }
          return List.generate(n, (_) => r.triple(), growable: growable);
        } finally {
          if (datafriendProfiling) {
            DatafriendProfile.end(DatafriendPhase.tripleDecode);
          }
        }
      });
    } finally {
      if (datafriendProfiling) {
        DatafriendProfile.end(DatafriendPhase.snapshotRead);
      }
    }
  }

  void close() {
    if (_handle == ffi.nullptr) return;
    _native.resultFinalizer.detach(this);
    _native.bufferFinalizer.detach(this);
    _native.free(_scratch.cast());
    _native.release(_handle);
    _handle = ffi.nullptr;
  }
}

/// Read-only native-owned ordering index. No Dart copy of the entity ID array.
final class DatafriendEntityIds extends ListBase<int>
    implements ffi.Finalizable {
  DatafriendEntityIds._(this._native, this._handle, this._length) {
    if (_handle != ffi.nullptr) {
      _native.bufferFinalizer.attach(
        this,
        _handle.cast(),
        detach: this,
        externalSize: _length * 4,
      );
    }
  }
  final _Native _native;
  ffi.Pointer<ffi.Uint32> _handle;
  final int _length;
  bool _closed = false;
  void _check() {
    if (_closed) throw StateError('Entity index is closed');
  }

  @override
  int get length {
    _check();
    return _length;
  }

  @override
  set length(int _) => throw UnsupportedError('Read-only entity index');
  @override
  int operator [](int index) {
    _check();
    RangeError.checkValidIndex(index, this, null, _length);
    return (_handle + index).value;
  }

  @override
  void operator []=(int index, int value) =>
      throw UnsupportedError('Read-only entity index');
  void close() {
    if (_closed) return;
    _native.bufferFinalizer.detach(this);
    _native.free(_handle.cast());
    _handle = ffi.nullptr;
    _closed = true;
  }
}

final class _Writer {
  _Writer();
  _Writer.request(int op) {
    u8(1);
    u8(op);
  }
  final BytesBuilder _b = BytesBuilder(copy: false);
  void u8(int x) => _b.addByte(x);
  void u32(int x) {
    final b = ByteData(4)..setUint32(0, x);
    _b.add(b.buffer.asUint8List());
  }

  void u64(int x) {
    final b = ByteData(8)..setUint64(0, x);
    _b.add(b.buffer.asUint8List());
  }

  void blob(List<int> v) {
    u32(v.length);
    _b.add(v);
  }

  void string(String v) => blob(utf8.encode(v));
  void triple(Triple t) {
    u64(t.entity);
    string(t.attribute);
    blob(t._value);
  }

  void term(Object? t) {
    if (t is QueryVariable) {
      if (t.index < 0 || t.index >= 64) {
        throw ArgumentError('Variable outside 0..63');
      }
      u8(1);
      u8(t.index);
    } else {
      u8(0);
      blob(_encodeValue(t));
    }
  }

  Uint8List finish() => _b.takeBytes();
}

final class _Reader {
  _Reader(this.bytes);
  final Uint8List bytes;
  int offset = 0;
  Uint8List take(int n) {
    if (n < 0 || offset + n > bytes.length) {
      throw const FormatException('Truncated Datafriend data');
    }
    final result = Uint8List.sublistView(bytes, offset, offset + n);
    offset += n;
    return result;
  }

  int u8() => take(1)[0];
  int u32() => ByteData.sublistView(take(4)).getUint32(0);
  int u64() => ByteData.sublistView(take(8)).getUint64(0);
  Uint8List blob() => take(u32());
  Triple triple() =>
      Triple._(u64(), utf8.decode(blob()), Uint8List.fromList(blob()));
}

Uint8List _encodeValue(Object? v) {
  final w = _Writer();
  if (v == null) {
    w.u8(0);
  } else if (v is bool) {
    w.u8(1);
    w.u8(v ? 1 : 0);
  } else if (v is String) {
    w.u8(2);
    w.string(v);
  } else if (v is int) {
    w.u8(3);
    w.u64(v);
  } else if (v is double) {
    if (!v.isFinite) throw ArgumentError('Nonfinite number');
    w.u8(4);
    final b = ByteData(8)..setFloat64(0, v == 0 ? 0 : v);
    w._b.add(b.buffer.asUint8List());
  } else if (v is Uint8List) {
    w.u8(5);
    w.blob(Uint8List.fromList(v));
  } else if (v is Entity) {
    _checkEntity(v.id);
    w.u8(6);
    w.u64(v.id);
  } else {
    throw ArgumentError('Unsupported Datafriend value: ${v.runtimeType}');
  }
  return w.finish();
}

Object? _decodeValue(Uint8List b) {
  if (b.isEmpty) throw const FormatException('Truncated Datafriend data');
  // Values have a fixed header. Read offsets directly instead of constructing
  // a Reader and typed-list views for each tag, length and payload segment.
  switch (b[0]) {
    case 0:
      _checkValueLength(b.length, 1);
      return null;
    case 1:
      _checkValueLength(b.length, 2);
      return b[1] != 0;
    case 2:
    case 5:
      if (b.length < 5) {
        throw const FormatException('Truncated Datafriend data');
      }
      final size = (b[1] << 24) | (b[2] << 16) | (b[3] << 8) | b[4];
      _checkValueLength(b.length, 5 + size);
      // Both results own their data; borrowed native memory never escapes.
      return b[0] == 2 ? const Utf8Decoder().convert(b, 5) : b.sublist(5);
    case 3:
      _checkValueLength(b.length, 9);
      return ByteData.sublistView(b).getInt64(1);
    case 4:
      _checkValueLength(b.length, 9);
      return ByteData.sublistView(b).getFloat64(1);
    case 6:
      _checkValueLength(b.length, 9);
      return Entity(ByteData.sublistView(b).getUint64(1));
    default:
      throw const FormatException('Unknown Datafriend value type');
  }
}

void _checkValueLength(int actual, int expected) {
  if (actual < expected) {
    throw const FormatException('Truncated Datafriend data');
  }
  if (actual > expected) {
    throw const FormatException('Trailing Datafriend value data');
  }
}
