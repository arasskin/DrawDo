import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui';

import 'package:path_provider/path_provider.dart';

import '../data/datafriend.dart';
import '../data/datafriend_profile.dart';
import 'simulation.dart';

/// Benchmark-only. Both clients run sequentially on the same application thread,
/// against the same production library. No ticker, painting or network work.
Future<void> runParticleComparison(Map<String, dynamic> config) async {
  final external = (await getExternalStorageDirectory())!;
  final output = File('${external.path}/particle-comparison.json');
  Directory? work;
  try {
    final native = ffi.DynamicLibrary.open('libparticle_benchmark.so');
    final library = ffi.DynamicLibrary.open('libdatafriend.so');
    final alloc = library
        .lookupFunction<
          ffi.Pointer<ffi.Void> Function(ffi.Size),
          ffi.Pointer<ffi.Void> Function(int)
        >('df_alloc');
    final free = library
        .lookupFunction<
          ffi.Void Function(ffi.Pointer<ffi.Void>),
          void Function(ffi.Pointer<ffi.Void>)
        >('df_free');
    final pin = native
        .lookupFunction<ffi.Int Function(ffi.Int), int Function(int)>(
          'particle_pin',
        );
    final run = native
        .lookupFunction<
          ffi.Int Function(
            ffi.Pointer<ffi.Uint8>,
            ffi.Pointer<ffi.Double>,
            ffi.Int,
            ffi.Int,
            ffi.Int,
            ffi.Int,
            ffi.Pointer<ffi.Uint8>,
          ),
          int Function(
            ffi.Pointer<ffi.Uint8>,
            ffi.Pointer<ffi.Double>,
            int,
            int,
            int,
            int,
            ffi.Pointer<ffi.Uint8>,
          )
        >('particle_run');
    final frequency = Stopwatch().frequency;
    final cpu = config['cpu'] as int?;
    if (cpu != null && pin(cpu) != 0) throw StateError('CPU affinity failed');
    work = await (await getTemporaryDirectory()).createTemp(
      'particle-compare-',
    );
    final count = config['count'] as int;
    final local = config['local'] as bool;
    final warmup = config['warmup'] as int;
    final samples = config['samples'] as int;
    final random = math.Random(74291);
    final positions = <Offset>[];
    final initial = alloc(
      count * 4 * ffi.sizeOf<ffi.Double>(),
    ).cast<ffi.Double>();
    for (var i = 0; i < count; i++) {
      final p = Offset(
        random.nextDouble() * particleWidth,
        random.nextDouble() * particleHeight,
      );
      positions.add(p);
      final angle = (i + 1) * 2.399963229728653;
      initial[4 * i] = p.dx;
      initial[4 * i + 1] = p.dy;
      initial[4 * i + 2] = math.cos(angle) * 45;
      initial[4 * i + 3] = math.sin(angle) * 45;
    }
    ffi.Pointer<ffi.Uint8> cString(String value) {
      final bytes = utf8.encode(value);
      final p = alloc(bytes.length + 1).cast<ffi.Uint8>();
      p.asTypedList(bytes.length + 1).setAll(0, [...bytes, 0]);
      return p;
    }

    final results = <String, Map<String, dynamic>>{};
    try {
      for (final variant in config['variants'] as List) {
        // Let pending platform messages finish outside measured sections.
        await Future<void>.delayed(const Duration(milliseconds: 100));
        final directory = Directory('${work.path}/$variant')..createSync();
        if (variant == 'native') {
          final report = File('${work.path}/native.json');
          final dir = cString(directory.path), path = cString(report.path);
          try {
            final status = run(
              dir,
              initial,
              count,
              local ? 1 : 0,
              warmup,
              samples,
              path,
            );
            if (status != 0) throw StateError('Native benchmark failed');
          } finally {
            free(dir.cast());
            free(path.cast());
          }
          results['native'] =
              jsonDecode(report.readAsStringSync()) as Map<String, dynamic>;
        } else {
          final simulation = ParticleSimulation(
            Datafriend.open(directory.path),
            count: count,
            local: local,
            initialPositions: positions,
          );
          try {
            simulation.validate();
            final rows = <Map<String, num>>[];
            for (var i = 0; i < warmup + samples; i++) {
              if (datafriendProfiling) {
                DatafriendProfile.reset();
                DatafriendProfile.begin();
              }
              final row = simulation.step();
              if (datafriendProfiling) {
                DatafriendProfile.end(DatafriendPhase.action);
                DatafriendProfile.checkComplete();
                for (final phase in [
                  DatafriendPhase.requestEncode,
                  DatafriendPhase.nativeEdit,
                  DatafriendPhase.transport,
                  DatafriendPhase.replyCopy,
                ]) {
                  row['profile_${phase.name}_us'] =
                      DatafriendProfile.exclusive[phase.index] *
                      1000000 /
                      frequency;
                }
              }
              if (i >= warmup) rows.add(row);
            }
            simulation.validate();
            final state = [
              for (final cell in simulation.cells)
                for (final p in cell.particles.values) [p.id, p.x, p.y],
            ];
            state.sort((a, b) => a[0].compareTo(b[0]));
            results['dart'] = {
              'variant': 'dart',
              'rows': rows,
              'state': state,
              'checks': 'passed',
            };
          } finally {
            simulation.close();
          }
        }
      }
    } finally {
      free(initial.cast());
    }
    final dart = results['dart']!, cpp = results['native']!;
    final ds = dart['state'] as List, ns = cpp['state'] as List;
    if (ds.length != ns.length) {
      throw StateError('Cross-language entity count differs');
    }
    var maxError = 0.0;
    for (var i = 0; i < ds.length; i++) {
      if (ds[i][0] != ns[i][0]) {
        throw StateError('Cross-language entity IDs differ');
      }
      for (var coordinate = 1; coordinate <= 2; coordinate++) {
        final error = ((ds[i][coordinate] as num) - (ns[i][coordinate] as num))
            .abs()
            .toDouble();
        maxError = math.max(maxError, error);
      }
    }
    if (maxError > 1e-6) {
      throw StateError('Cross-language physics diverged: $maxError');
    }
    for (var i = 0; i < samples; i++) {
      for (final key in [
        'moved',
        'crossings',
        'changed_views',
        'read_entities',
        'candidate_pairs',
        'interactions',
      ]) {
        if (dart['rows'][i][key] != cpp['rows'][i][key]) {
          throw StateError(
            'Cross-language work differs at tick $i: $key (${dart['rows'][i][key]} vs ${cpp['rows'][i][key]})',
          );
        }
      }
    }
    await output.writeAsString(
      jsonEncode({
        'config': config,
        'profile': datafriendProfiling,
        'checks':
            'database consistency, per-tick work and final coordinates match',
        'max_coordinate_error': maxError,
        'results': results,
      }),
    );
  } catch (e, st) {
    await output.writeAsString(jsonEncode({'error': '$e', 'stack': '$st'}));
  } finally {
    if (work != null) await work.delete(recursive: true);
  }
}
