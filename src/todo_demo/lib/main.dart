import 'dart:convert';
import 'dart:io';
import 'package:drawdo/data/datafriend.dart';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'benchmark.dart';
import 'database.dart';
import 'todo_screen.dart';
import 'widget_benchmark.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final configPath = Platform.environment['TODO_BENCH_CONFIG'];
  if (configPath != null) {
    final uiErrors = <String>[];
    FlutterError.onError = (details) =>
        uiErrors.add(details.exceptionAsString());
    final config =
        jsonDecode(File(configPath).readAsStringSync()) as Map<String, dynamic>;
    try {
      if (config['mode'] == 'widgets') {
        await runWidgetBenchmark(config);
      } else {
        await runBenchmark(config);
      }
      if (uiErrors.isNotEmpty) throw StateError('Flutter errors: $uiErrors');
      exit(0);
    } catch (error, stack) {
      File(
        config['output'] as String,
      ).writeAsStringSync(jsonEncode({'error': '$error', 'stack': '$stack'}));
      stderr.writeln('$error\n$stack');
      exit(1);
    }
  }
  runApp(const TodoLab());
}

class TodoLab extends StatefulWidget {
  const TodoLab({super.key});
  @override
  State<TodoLab> createState() => _TodoLabState();
}

class _TodoLabState extends State<TodoLab> {
  Datafriend? database;
  String? error;
  @override
  void initState() {
    super.initState();
    open();
  }

  Future<void> open() async {
    try {
      final path = await getApplicationSupportDirectory();
      final directory = Directory('${path.path}/todo-lab/datafriend-v3');
      await directory.create(recursive: true);
      if (!mounted) return;
      final opened = openTodoDatabase(directory.path);
      if (!mounted) {
        opened.close();
        return;
      }
      setState(() => database = opened);
    } catch (e) {
      if (mounted) setState(() => error = e.toString());
    }
  }

  @override
  void dispose() {
    database?.close();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'Datafriend todos',
    debugShowCheckedModeBanner: false,
    theme: todoTheme(),
    home: database != null
        ? TodoScreen(database: database!)
        : Scaffold(
            body: Center(
              child: error == null
                  ? const CircularProgressIndicator()
                  : Padding(
                      padding: const EdgeInsets.all(32),
                      child: SelectableText(error!),
                    ),
            ),
          ),
  );
}
