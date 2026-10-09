/// Optional instrumentation for tests and benchmarks; normal screens omit it.
typedef TodoBuildObserver = void Function(String part, int? entity);

final class TodoBuildProbe {
  final Map<String, int> parts = {};
  final Map<int, int> rows = {};

  void record(String part, int? entity) {
    parts.update(part, (n) => n + 1, ifAbsent: () => 1);
    if (part == 'row' && entity != null) {
      rows.update(entity, (n) => n + 1, ifAbsent: () => 1);
    }
  }

  void reset() {
    parts.clear();
    rows.clear();
  }

  Map<String, Object> snapshot() => {
    for (final part in ['screen', 'list', 'counter', 'row'])
      '${part}_builds': parts[part] ?? 0,
    'row_entities': rows.keys.toList()..sort(),
  };
}
