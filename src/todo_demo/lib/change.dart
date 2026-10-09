import 'dart:async';
import 'stores.dart';

final class ChangeTiming {
  const ChangeTiming(this.callUs, this.commitUs, this.publishUs);
  final int callUs, commitUs, publishUs;
  Map<String, int> toJson() => {
    'call_us': callUs,
    'commit_us': commitUs,
    'publish_us': publishUs,
  };
}

/// Measure the backend return and the actual subscribed result separately.
/// A completed InstantDB transaction is not assumed to mean a published query.
Future<ChangeTiming> change(
  TodoStore store,
  Future<void>? Function() action,
  bool Function() matches,
) async {
  final clock = Stopwatch()..start();
  final published = Completer<void>();
  int? publishUs;
  void observe() {
    if (published.isCompleted) return;
    if (store.error != null || matches()) {
      publishUs = clock.elapsedMicroseconds;
      published.complete();
    }
  }

  store.addListener(observe);
  try {
    final pending = action();
    final callUs = clock.elapsedMicroseconds;
    if (pending != null) await pending;
    final commitUs = clock.elapsedMicroseconds;
    observe();
    await published.future.timeout(
      const Duration(seconds: 30),
      onTimeout: () => throw StateError(
        'Query publication timed out: publications=${store.publications}, '
        'rows=${store.tasks.length}, first=${store.tasks.firstOrNull?.toJson()}, '
        'error=${store.error}',
      ),
    );
    if (store.error != null) throw store.error!;
    return ChangeTiming(callUs, commitUs, publishUs!);
  } finally {
    store.removeListener(observe);
    clock.stop();
  }
}
