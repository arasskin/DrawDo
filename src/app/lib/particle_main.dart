import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

import 'data/datafriend.dart';
import 'particles/simulation.dart';
import 'particles/comparison.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  Map<String, dynamic> config = {};
  if (Platform.isAndroid) {
    final raw = await const MethodChannel(
      'drawdo/particle-demo',
    ).invokeMethod<String>('config');
    if (raw != null) config = jsonDecode(raw) as Map<String, dynamic>;
  }
  if (config['compare'] == true) {
    runApp(
      const MaterialApp(
        home: Scaffold(
          body: Center(child: Text('Comparing Dart and native particles…')),
        ),
      ),
    );
    await WidgetsBinding.instance.endOfFrame;
    await runParticleComparison(config);
    return;
  }
  runApp(ParticleApp(config: config));
}

class ParticleApp extends StatelessWidget {
  const ParticleApp({super.key, required this.config});
  final Map<String, dynamic> config;
  @override
  Widget build(BuildContext context) => MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: ThemeData.dark(useMaterial3: true).copyWith(
      scaffoldBackgroundColor: const Color(0xff101923),
      colorScheme: ColorScheme.fromSeed(
        seedColor: const Color(0xff70e7ba),
        brightness: Brightness.dark,
      ),
    ),
    home: ParticlePage(config: config),
  );
}

class ParticlePage extends StatefulWidget {
  const ParticlePage({super.key, required this.config});
  final Map<String, dynamic> config;
  @override
  State<ParticlePage> createState() => _ParticlePageState();
}

class _ParticlePageState extends State<ParticlePage>
    with SingleTickerProviderStateMixin {
  ParticleSimulation? simulation;
  Directory? directory;
  late final Ticker ticker;
  Timer? displayTimer;
  final frames = <Map<String, num>>[];
  final timings = <int, FrameTiming>{};
  final errors = <String>[];
  Map<String, num>? last;
  int count = 300, warmup = 60, samples = 240, steps = 0, setupUs = 0;
  bool local = true, loading = true, running = true, finished = false;
  bool get benchmark => widget.config['benchmark'] == true;
  String? error;
  int? previousFrameUs;
  void Function(FlutterErrorDetails)? previousErrorHandler;

  @override
  void initState() {
    super.initState();
    count = widget.config['count'] as int? ?? count;
    local = widget.config['local'] as bool? ?? local;
    warmup = widget.config['warmup'] as int? ?? warmup;
    samples = widget.config['samples'] as int? ?? samples;
    ticker = createTicker(onTick);
    SchedulerBinding.instance.addTimingsCallback(recordTimings);
    if (benchmark) {
      previousErrorHandler = FlutterError.onError;
      FlutterError.onError = (details) {
        errors.add(details.exceptionAsString());
        previousErrorHandler?.call(details);
      };
    }
    displayTimer = Timer.periodic(const Duration(milliseconds: 500), (_) {
      if (mounted && !loading) setState(() {});
    });
    open();
  }

  void recordTimings(List<FrameTiming> batch) {
    if (!benchmark) return;
    for (final t in batch) {
      timings[t.frameNumber] = t;
    }
  }

  Future<void> open() async {
    ticker.stop();
    previousFrameUs = null;
    setState(() {
      loading = true;
      error = null;
    });
    try {
      simulation?.close();
      simulation = null;
      if (directory != null) await directory!.delete(recursive: true);
      final temp = await getTemporaryDirectory();
      directory = await temp.createTemp('particle-demo-');
      final clock = Stopwatch()..start();
      final db = Datafriend.open(directory!.path);
      try {
        simulation = ParticleSimulation(db, count: count, local: local);
      } catch (_) {
        db.close();
        rethrow;
      }
      setupUs = clock.elapsedMicroseconds;
      simulation!.validate();
      if (!mounted) return;
      setState(() {
        loading = false;
        running = true;
        finished = false;
      });
      await WidgetsBinding.instance.endOfFrame;
      ticker.start();
    } catch (e, st) {
      await fail(e, st);
    }
  }

  void onTick(Duration elapsed) {
    final s = simulation;
    if (s == null || finished) return;
    try {
      final row = s.step();
      final frameUs = elapsed.inMicroseconds;
      row['interval_us'] = previousFrameUs == null
          ? 0
          : frameUs - previousFrameUs!;
      previousFrameUs = frameUs;
      last = row;
      final measured = benchmark && steps >= warmup;
      if (measured) frames.add(row);
      steps++;
      SchedulerBinding.instance.addPostFrameCallback((_) {
        row['frame_number'] =
            ui.PlatformDispatcher.instance.frameData.frameNumber;
        row['paint_us'] = s.paintMicros;
        row['painted_cells'] = s.paintedCells;
        if (benchmark && steps >= warmup + samples && !finished) {
          finished = true;
          ticker.stop();
          finishBenchmark();
        }
      });
    } catch (e, st) {
      ticker.stop();
      finished = true;
      fail(e, st);
    }
  }

  Future<File> outputFile() async {
    final location = Platform.isAndroid
        ? await getExternalStorageDirectory()
        : await getApplicationSupportDirectory();
    return File('${location!.path}/particle-result.json');
  }

  Future<void> fail(Object e, StackTrace st) async {
    ticker.stop();
    error = '$e';
    if (mounted) {
      setState(() {
        loading = false;
        running = false;
      });
    }
    if (benchmark) {
      final file = await outputFile();
      await file.writeAsString(jsonEncode({'error': '$e', 'stack': '$st'}));
    }
  }

  Future<void> finishBenchmark() async {
    try {
      // Timings arrive asynchronously in batches after rasterization.
      for (var i = 0; i < 30; i++) {
        if (frames.every((row) => timings.containsKey(row['frame_number']))) {
          break;
        }
        await Future<void>.delayed(const Duration(milliseconds: 100));
      }
      if (frames.length != samples || errors.isNotEmpty) {
        throw StateError('Invalid frame count or Flutter errors: $errors');
      }
      simulation!.validate();
      for (final row in frames) {
        final timing = timings[row['frame_number']];
        if (timing == null) {
          throw StateError(
            'Missing engine timing for frame ${row['frame_number']}',
          );
        }
        row['build_us'] = timing.buildDuration.inMicroseconds;
        row['raster_us'] = timing.rasterDuration.inMicroseconds;
        row['frame_total_us'] = timing.totalSpan.inMicroseconds;
      }
      final file = await outputFile();
      await file.writeAsString(
        jsonEncode({
          'config': widget.config,
          'setup_us': setupUs,
          'cells': particleColumns * particleRows,
          'world': [particleWidth, particleHeight],
          'radius': particleRadius,
          'checks':
              'cell memberships, values, counts and frame timing coverage passed',
          'rows': frames,
        }),
      );
      if (mounted) {
        setState(() {
          running = false;
        });
      }
    } catch (e, st) {
      await fail(e, st);
    }
  }

  @override
  void dispose() {
    ticker.dispose();
    displayTimer?.cancel();
    SchedulerBinding.instance.removeTimingsCallback(recordTimings);
    if (benchmark) FlutterError.onError = previousErrorHandler;
    simulation?.close();
    directory?.delete(recursive: true);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final s = simulation;
    return Scaffold(
      appBar: AppBar(
        title: const Text('Cell particles'),
        actions: [
          IconButton(
            onPressed: loading || benchmark
                ? null
                : () {
                    setState(() => running = !running);
                    if (running) {
                      previousFrameUs = null;
                      ticker.start();
                    } else {
                      ticker.stop();
                    }
                  },
            icon: Icon(running ? Icons.pause : Icons.play_arrow),
          ),
        ],
      ),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const Text(
                'Nearby interactions · shared cell views',
                style: TextStyle(color: Colors.white60),
              ),
              const SizedBox(height: 12),
              Row(
                children: [
                  DropdownButton<int>(
                    value: count,
                    items: [100, 300, 1000, 3000]
                        .map(
                          (n) => DropdownMenuItem(
                            value: n,
                            child: Text('$n particles'),
                          ),
                        )
                        .toList(),
                    onChanged: benchmark || loading
                        ? null
                        : (n) {
                            count = n!;
                            open();
                          },
                  ),
                  const Spacer(),
                  Switch(
                    value: !local,
                    onChanged: benchmark || loading
                        ? null
                        : (v) {
                            setState(() {
                              local = !v;
                              s?.local = local;
                            });
                          },
                  ),
                  const Text('All moving'),
                ],
              ),
              Expanded(
                child: Center(
                  child: error != null
                      ? SelectableText(
                          error!,
                          style: const TextStyle(color: Colors.redAccent),
                        )
                      : loading || s == null
                      ? const CircularProgressIndicator()
                      : AspectRatio(
                          aspectRatio: particleWidth / particleHeight,
                          child: LayoutBuilder(
                            builder: (context, bounds) {
                              final width = bounds.maxWidth / particleColumns;
                              final height = bounds.maxHeight / particleRows;
                              return GestureDetector(
                                onPanDown: benchmark
                                    ? null
                                    : (d) => s.setPatch(
                                        d.localPosition.dx /
                                            bounds.maxWidth *
                                            particleWidth,
                                        d.localPosition.dy /
                                            bounds.maxHeight *
                                            particleHeight,
                                      ),
                                onPanUpdate: benchmark
                                    ? null
                                    : (d) => s.setPatch(
                                        d.localPosition.dx /
                                            bounds.maxWidth *
                                            particleWidth,
                                        d.localPosition.dy /
                                            bounds.maxHeight *
                                            particleHeight,
                                      ),
                                child: widget.config['renderer'] != 'cells'
                                    ? RepaintBoundary(
                                        child: CustomPaint(
                                          painter: _BoardPainter(s),
                                          size: Size.infinite,
                                        ),
                                      )
                                    : Stack(
                                        children: [
                                          for (final cell in s.cells)
                                            Positioned(
                                              left:
                                                  (cell.index %
                                                      particleColumns) *
                                                  width,
                                              top:
                                                  (cell.index ~/
                                                      particleColumns) *
                                                  height,
                                              width: width,
                                              height: height,
                                              child: RepaintBoundary(
                                                child: CustomPaint(
                                                  painter: _CellPainter(
                                                    s,
                                                    cell,
                                                  ),
                                                ),
                                              ),
                                            ),
                                        ],
                                      ),
                              );
                            },
                          ),
                        ),
                ),
              ),
              const SizedBox(height: 12),
              Text(
                local
                    ? 'Local patch · drag to move it'
                    : 'Every particle moves each tick',
                style: const TextStyle(color: Color(0xff70e7ba)),
              ),
              const SizedBox(height: 6),
              Text(
                last == null
                    ? 'Preparing cell views…'
                    : '${(last!['step_us']! / 1000).toStringAsFixed(1)} ms step   '
                          '${last!['moved']} moved   ${last!['changed_views']} cells updated',
              ),
              Text(
                last == null
                    ? ''
                    : '${last!['crossings']} boundary crossings · '
                          '${last!['candidate_pairs']} neighbor checks',
                style: const TextStyle(color: Colors.white54),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _CellPainter extends CustomPainter {
  _CellPainter(this.simulation, this.cell) : super(repaint: cell);
  final ParticleSimulation simulation;
  final ParticleCell cell;
  @override
  void paint(Canvas canvas, Size size) {
    final clock = Stopwatch()..start();
    canvas.clipRect(Offset.zero & size);
    canvas.drawRect(
      Offset.zero & size,
      Paint()
        ..color = cell.active
            ? const Color(0xff193c3b)
            : const Color(0xff14232e),
    );
    canvas.drawRect(
      Offset.zero & size,
      Paint()
        ..color = const Color(0xff29424c)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 0.5,
    );
    final left = (cell.index % particleColumns) * particleCellSize;
    final top = (cell.index ~/ particleColumns) * particleCellSize;
    final paint = Paint()
      ..color = cell.active ? const Color(0xff8ef5ca) : const Color(0xff84a7bf);
    for (final p in cell.particles.values) {
      canvas.drawCircle(
        Offset(
          (p.x - left) / particleCellSize * size.width,
          (p.y - top) / particleCellSize * size.height,
        ),
        1.8,
        paint,
      );
    }
    simulation.paintMicros += clock.elapsedMicroseconds;
    simulation.paintedCells++;
  }

  @override
  bool shouldRepaint(covariant _CellPainter oldDelegate) =>
      oldDelegate.cell != cell;
}

/// Batch cached points into one layer; subscriptions and reads remain per cell.
class _BoardPainter extends CustomPainter {
  _BoardPainter(this.simulation) : super(repaint: simulation);
  final ParticleSimulation simulation;
  @override
  void paint(Canvas canvas, Size size) {
    final clock = Stopwatch()..start();
    final scale = size.width / particleWidth;
    canvas.clipRect(Offset.zero & size);
    canvas.scale(scale, size.height / particleHeight);
    canvas.drawRect(
      const Rect.fromLTWH(0, 0, particleWidth, particleHeight),
      Paint()..color = const Color(0xff14232e),
    );
    final active = Paint()..color = const Color(0xff193c3b);
    final grid = Paint()
      ..color = const Color(0xff29424c)
      ..strokeWidth = 0.5 / scale;
    for (final cell in simulation.cells) {
      if (cell.active) {
        canvas.drawRect(
          Rect.fromLTWH(
            (cell.index % particleColumns) * particleCellSize,
            (cell.index ~/ particleColumns) * particleCellSize,
            particleCellSize,
            particleCellSize,
          ),
          active,
        );
      }
    }
    for (var col = 0; col <= particleColumns; col++) {
      canvas.drawLine(
        Offset(col * particleCellSize, 0),
        Offset(col * particleCellSize, particleHeight),
        grid,
      );
    }
    for (var row = 0; row <= particleRows; row++) {
      canvas.drawLine(
        Offset(0, row * particleCellSize),
        Offset(particleWidth, row * particleCellSize),
        grid,
      );
    }
    final point = Paint()
      ..strokeCap = StrokeCap.round
      ..strokeWidth = 3.6 / scale;
    for (final cell in simulation.cells) {
      point.color = cell.active
          ? const Color(0xff8ef5ca)
          : const Color(0xff84a7bf);
      if (cell.points.isNotEmpty) {
        canvas.drawRawPoints(ui.PointMode.points, cell.points, point);
      }
    }
    simulation.paintMicros += clock.elapsedMicroseconds;
    simulation.paintedCells += simulation.cells.length;
  }

  @override
  bool shouldRepaint(covariant _BoardPainter oldDelegate) =>
      oldDelegate.simulation != simulation;
}
