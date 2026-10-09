import 'dart:math' as math;
import 'dart:ui';

import 'package:flutter/foundation.dart';

import '../data/datafriend.dart';

const particleColumns = 8;
const particleRows = 12;
const particleCellSize = 80.0;
const particleWidth = particleColumns * particleCellSize;
const particleHeight = particleRows * particleCellSize;
const particleRadius = 24.0;

int particleCell(double x, double y) =>
    (y / particleCellSize).floor() * particleColumns +
    (x / particleCellSize).floor();

class Particle {
  const Particle(this.id, this.x, this.y);
  final int id;
  final double x, y;
}

/// A Dart render/read cache of one live native query, refreshed by notifications.
/// Physics and painting read these results, not a second authoritative database.
class ParticleCell extends ChangeNotifier {
  ParticleCell(this.index);
  final int index;
  late final DatafriendView view;
  late final DatafriendResult result;
  final particles = <int, Particle>{};
  Float32List points = Float32List(0);
  final changed = <int>{};
  bool membershipChanged = true;
  bool active = false;

  void setActive(bool value) {
    if (active == value) return;
    active = value;
    notifyListeners();
  }

  int refresh() {
    var reads = 0;
    if (membershipChanged) {
      final ids = result.entities();
      try {
        final present = ids.toSet();
        particles.removeWhere((id, _) => !present.contains(id));
        for (final id in present) {
          if (!particles.containsKey(id)) changed.add(id);
        }
      } finally {
        ids.close();
      }
    }
    for (final id in changed) {
      final x = result.value(id, 'x') as double?;
      if (x == null) {
        particles.remove(id);
      } else {
        particles[id] = Particle(id, x, result.value(id, 'y') as double);
        reads++;
      }
    }
    changed.clear();
    membershipChanged = false;
    points = Float32List(particles.length * 2);
    var offset = 0;
    for (final p in particles.values) {
      points[offset++] = p.x;
      points[offset++] = p.y;
    }
    notifyListeners();
    return reads;
  }

  void close() {
    result.close();
    view.close();
    dispose();
  }
}

/// Fixed 1/60-second steps. Overloaded rendering does not trigger catch-up steps;
/// measured frame intervals expose the resulting slowdown.
class ParticleSimulation extends ChangeNotifier {
  ParticleSimulation(
    this.database, {
    required this.count,
    this.local = true,
    List<Offset>? initialPositions,
  }) {
    final random = math.Random(74291);
    final seed = <Triple>[];
    for (var id = 1; id <= count; id++) {
      final p =
          initialPositions?[id - 1] ??
          Offset(
            random.nextDouble() * particleWidth,
            random.nextDouble() * particleHeight,
          );
      seed.addAll([
        Triple(id, 'x', p.dx),
        Triple(id, 'y', p.dy),
        Triple(id, 'region', particleCell(p.dx, p.dy)),
      ]);
      final angle = id * 2.399963229728653;
      drift[id] = Offset(math.cos(angle) * 45, math.sin(angle) * 45);
    }
    database.edit(add: seed);
    for (var index = 0; index < particleColumns * particleRows; index++) {
      final cell = ParticleCell(index);
      cell.view = database.subscribe(
        DatafriendQuery([
          (QueryClause(const QueryVariable(0), 'region', index), false),
          (const QueryClause(QueryVariable(0), 'x', QueryVariable(1)), true),
          (const QueryClause(QueryVariable(0), 'y', QueryVariable(2)), true),
        ]),
        onChanged: () {
          final changes = cell.view.changes;
          cell.membershipChanged |= changes.membershipChanged || changes.reset;
          if (changes.reset) {
            cell.particles.clear();
          }
          cell.changed.addAll(changes.entities);
          dirty.add(cell);
        },
      );
      cell.result = cell.view.result();
      cell.refresh();
      cells.add(cell);
    }
    _markActive();
    notifyListeners();
  }

  final Datafriend database;
  final int count;
  bool local;
  final cells = <ParticleCell>[];
  final dirty = <ParticleCell>{};
  final drift = <int, Offset>{};
  int tick = 0;
  int patchColumn = 3, patchRow = 5;
  bool automaticPatch = true;
  int paintMicros = 0, paintedCells = 0;

  void setPatch(double x, double y) {
    automaticPatch = false;
    patchColumn = (x / particleCellSize).floor().clamp(0, particleColumns - 1);
    patchRow = (y / particleCellSize).floor().clamp(0, particleRows - 1);
    _markActive();
    notifyListeners();
  }

  void _markActive() {
    for (final cell in cells) {
      final col = cell.index % particleColumns,
          row = cell.index ~/ particleColumns;
      final dx = (col - patchColumn).abs(), dy = (row - patchRow).abs();
      final active =
          !local ||
          (math.min(dx, particleColumns - dx) <= 1 &&
              math.min(dy, particleRows - dy) <= 1);
      cell.setActive(active);
    }
  }

  Map<String, num> step({bool bruteForce = false}) {
    final clock = Stopwatch()..start();
    paintMicros = 0;
    paintedCells = 0;
    if (automaticPatch && local && tick > 0 && tick % 90 == 0) {
      patchColumn = (patchColumn + 1) % particleColumns;
      if (patchColumn == 0) patchRow = (patchRow + 1) % particleRows;
    }
    _markActive();
    final updates = <Particle>[];
    var pairs = 0, interactions = 0, crossings = 0, activeCells = 0;
    final all = bruteForce
        ? [for (final cell in cells) ...cell.particles.values]
        : const <Particle>[];
    for (final cell in cells) {
      if (!cell.active) continue;
      activeCells++;
      final neighbors = <Particle>[];
      if (bruteForce) {
        neighbors.addAll(all);
      } else {
        final col = cell.index % particleColumns,
            row = cell.index ~/ particleColumns;
        for (var dy = -1; dy <= 1; dy++) {
          for (var dx = -1; dx <= 1; dx++) {
            final index =
                ((row + dy) % particleRows) * particleColumns +
                ((col + dx) % particleColumns);
            neighbors.addAll(cells[index].particles.values);
          }
        }
      }
      for (final p in cell.particles.values) {
        var fx = 0.0, fy = 0.0;
        for (final other in neighbors) {
          if (p.id == other.id) continue;
          pairs++;
          var dx = p.x - other.x, dy = p.y - other.y;
          if (dx > particleWidth / 2) dx -= particleWidth;
          if (dx < -particleWidth / 2) dx += particleWidth;
          if (dy > particleHeight / 2) dy -= particleHeight;
          if (dy < -particleHeight / 2) dy += particleHeight;
          final squared = dx * dx + dy * dy;
          if (squared >= particleRadius * particleRadius || squared < 1e-12) {
            continue;
          }
          interactions++;
          final distance = math.sqrt(squared);
          final force = (1 - distance / particleRadius) * 80 / distance;
          fx += dx * force;
          fy += dy * force;
        }
        final velocity = drift[p.id]!;
        final x =
            (p.x + ((velocity.dx + fx) / 60).clamp(-10.0, 10.0)) %
            particleWidth;
        final y =
            (p.y + ((velocity.dy + fy) / 60).clamp(-10.0, 10.0)) %
            particleHeight;
        updates.add(Particle(p.id, x, y));
        if (particleCell(x, y) != cell.index) crossings++;
      }
    }
    final physics = clock.elapsedMicroseconds;
    final triples = <Triple>[];
    for (final p in updates) {
      triples.add(Triple(p.id, 'x', p.x));
      triples.add(Triple(p.id, 'y', p.y));
      // Locate old membership through the current cell cache, not a DB query.
      final nextCell = particleCell(p.x, p.y);
      if (!cells[nextCell].particles.containsKey(p.id)) {
        triples.add(Triple(p.id, 'region', nextCell));
      }
    }
    final prepared = clock.elapsedMicroseconds;
    database.edit(add: triples);
    final edited = clock.elapsedMicroseconds;
    final changedViews = dirty.length;
    var reads = 0;
    for (final cell in dirty) {
      reads += cell.refresh();
    }
    dirty.clear();
    notifyListeners();
    tick++;
    final total = clock.elapsedMicroseconds;
    return {
      'physics_us': physics,
      'triple_prepare_us': prepared - physics,
      'edit_us': edited - prepared,
      'readback_us': total - edited,
      'step_us': total,
      'moved': updates.length,
      'crossings': crossings,
      'changed_views': changedViews,
      'read_entities': reads,
      'candidate_pairs': pairs,
      'interactions': interactions,
      'active_cells': activeCells,
    };
  }

  /// Outside measured frames: compare all cell caches with authoritative facts.
  void validate() {
    final all = database.subscribe(const DatafriendQuery.all());
    final result = all.result();
    try {
      final seen = <int>{};
      for (final cell in cells) {
        if (cell.result.length != cell.particles.length * 2) {
          throw StateError('Cell result size differs from cache');
        }
        for (final p in cell.particles.values) {
          if (!seen.add(p.id) ||
              !p.x.isFinite ||
              !p.y.isFinite ||
              particleCell(p.x, p.y) != cell.index ||
              result.value(p.id, 'region') != cell.index ||
              result.value(p.id, 'x') != p.x ||
              result.value(p.id, 'y') != p.y) {
            throw StateError(
              'Particle ${p.id} has inconsistent cell/coordinates',
            );
          }
        }
      }
      if (seen.length != count || result.length != count * 3) {
        throw StateError('Particles lost or duplicated');
      }
    } finally {
      result.close();
      all.close();
    }
  }

  void close() {
    for (final cell in cells) {
      cell.close();
    }
    database.close();
    dispose();
  }
}
