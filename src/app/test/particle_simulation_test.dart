import 'dart:io';

import 'package:drawdo/data/datafriend.dart';
import 'package:drawdo/particles/simulation.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  final library = Platform.environment['DATAFRIEND_LIBRARY'];
  Future<(ParticleSimulation, Directory)> make({
    int count = 100,
    bool local = false,
    List<Offset>? positions,
  }) async {
    final directory = await Directory.systemTemp.createTemp('particle-test-');
    final database = Datafriend.open(
      directory.path,
      libraryPath: library,
      durable: false,
    );
    return (
      ParticleSimulation(
        database,
        count: count,
        local: local,
        initialPositions: positions,
      ),
      directory,
    );
  }

  test(
    'crossing a wrapped cell boundary updates both static cell views',
    () async {
      final (simulation, directory) = await make(
        count: 1,
        positions: [const Offset(.1, 240.1)],
      );
      try {
        final oldCell = particleCell(.1, 240.1);
        final step = simulation.step();
        expect(step['crossings'], 1);
        expect(step['changed_views'], 2);
        expect(simulation.cells[oldCell].particles, isEmpty);
        simulation.validate();
        for (var i = 0; i < 150; i++) {
          simulation.step();
        }
        simulation.validate();
      } finally {
        simulation.close();
        await directory.delete(recursive: true);
      }
    },
  );

  test(
    'neighbor-cell physics agrees with all-pairs reference, including wrap',
    () async {
      final positions = [
        const Offset(1, 1),
        const Offset(639, 959),
        const Offset(79, 79),
        const Offset(81, 81),
        const Offset(100, 100),
        const Offset(100, 100),
      ];
      final (spatial, a) = await make(
        count: positions.length,
        positions: positions,
      );
      final (reference, b) = await make(
        count: positions.length,
        positions: positions,
      );
      try {
        for (var tick = 0; tick < 20; tick++) {
          final spatialStep = spatial.step();
          final referenceStep = reference.step(bruteForce: true);
          expect(spatialStep['interactions'], referenceStep['interactions']);
          final expected = {
            for (final cell in reference.cells) ...cell.particles,
          };
          for (final cell in spatial.cells) {
            for (final p in cell.particles.values) {
              expect(p.x, closeTo(expected[p.id]!.x, 1e-7));
              expect(p.y, closeTo(expected[p.id]!.y, 1e-7));
            }
          }
        }
        spatial.validate();
        reference.validate();
      } finally {
        spatial.close();
        reference.close();
        await a.delete(recursive: true);
        await b.delete(recursive: true);
      }
    },
  );

  test(
    'local activity leaves distant cells unchanged and survives patch movement',
    () async {
      final (simulation, directory) = await make(count: 300, local: true);
      try {
        final before = {for (final cell in simulation.cells) ...cell.particles};
        final inactive = {
          for (final cell in simulation.cells)
            if (!cell.active) ...cell.particles.keys,
        };
        final first = simulation.step();
        expect(first['active_cells'], 9);
        expect(first['moved'], lessThan(300));
        for (final cell in simulation.cells) {
          for (final p in cell.particles.values) {
            if (inactive.contains(p.id)) {
              expect(p.x, before[p.id]!.x);
              expect(p.y, before[p.id]!.y);
            }
          }
        }
        for (var i = 0; i < 200; i++) {
          simulation.step();
        }
        simulation.validate();
      } finally {
        simulation.close();
        await directory.delete(recursive: true);
      }
    },
  );
}
