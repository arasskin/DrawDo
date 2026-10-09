/// Diagnostic instrumentation only. Constant guards remove call sites from the
/// normal AOT build. Timers cover whole phases, never individual facts/values.
const datafriendProfiling = bool.fromEnvironment('DATAFRIEND_PROFILE');

enum DatafriendPhase {
  action,
  requestEncode,
  transport,
  nativeEdit,
  nativeRead,
  nativeOther,
  replyCopy,
  tripleDecode,
  snapshotManage,
  nativeSnapshot,
  snapshotRead,
  nativeSnapshotList,
  appDetectChanges,
  appIdentityLookup,
  appEncodeDiff,
  appRemovals,
  appEdit,
  appRefresh,
  appRows,
  appBaseline,
  appModels,
}

enum DatafriendCounter {
  editCalls,
  liveReadCalls,
  otherCalls,
  snapshotCalls,
  snapshotLists,
  decodedTriples,
  responseBytes,
  requestBytes,
  encodedRows,
  candidateTriples,
  changedRoots,
  fieldReads,
  decodedValues,
  entityIds,
  taskModels,
}

/// Single Dart isolate only. Inclusive and exclusive ticks let nested FFI calls
/// be separated from Dart orchestration without double-counting their time.
abstract final class DatafriendProfile {
  static final _clock = Stopwatch()..start();
  static final inclusive = List<int>.filled(DatafriendPhase.values.length, 0);
  static final exclusive = List<int>.filled(DatafriendPhase.values.length, 0);
  static final calls = List<int>.filled(DatafriendPhase.values.length, 0);
  static final counters = List<int>.filled(DatafriendCounter.values.length, 0);
  static final _starts = List<int>.filled(32, 0);
  static final _children = List<int>.filled(32, 0);
  static int _depth = 0;
  static void begin() {
    _starts[_depth] = _clock.elapsedTicks;
    _children[_depth++] = 0;
  }

  static void end(DatafriendPhase phase) {
    final end = _clock.elapsedTicks;
    final depth = --_depth;
    final ticks = end - _starts[depth];
    inclusive[phase.index] += ticks;
    exclusive[phase.index] += ticks - _children[depth];
    calls[phase.index]++;
    if (depth > 0) _children[depth - 1] += ticks;
  }

  static void count(DatafriendCounter counter, [int n = 1]) {
    counters[counter.index] += n;
  }

  static void reset() {
    if (_depth != 0) throw StateError('Unbalanced Datafriend profile scopes');
    inclusive.fillRange(0, inclusive.length, 0);
    exclusive.fillRange(0, exclusive.length, 0);
    calls.fillRange(0, calls.length, 0);
    counters.fillRange(0, counters.length, 0);
  }

  static void checkComplete() {
    if (_depth != 0) throw StateError('Unbalanced Datafriend profile scopes');
    if (exclusive.reduce((a, b) => a + b) !=
        inclusive[DatafriendPhase.action.index]) {
      throw StateError('Exclusive phase times do not partition the action');
    }
  }
}
