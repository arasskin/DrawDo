// Package-free, screen-owned setState baseline for matched benchmarks.
import 'package:flutter/material.dart';
import 'build_probe.dart';

class FlutterTask {
  FlutterTask(this.id, this.title, this.order, {this.done = false});
  final int id;
  String title;
  int order;
  bool done;
  FlutterTask copy() => FlutterTask(id, title, order, done: done);
}

class PureFlutterScreen extends StatefulWidget {
  const PureFlutterScreen({
    super.key,
    required this.initialTasks,
    this.onEdited,
    this.captureKey,
    this.onBuild,
  });
  final List<FlutterTask> initialTasks;
  final VoidCallback? onEdited;
  final GlobalKey? captureKey;
  final TodoBuildObserver? onBuild;
  @override
  State<PureFlutterScreen> createState() => PureFlutterScreenState();
}

class PureFlutterScreenState extends State<PureFlutterScreen> {
  final input = TextEditingController();
  String filter = 'All';
  late final List<FlutterTask> tasks = [
    for (final task in widget.initialTasks) task.copy(),
  ];
  late int nextId =
      tasks.fold(0, (id, task) => task.id > id ? task.id : id) + 1;

  void change(VoidCallback update) {
    setState(update);
    widget.onEdited?.call();
  }

  void renameTask(int id, String title) => change(() {
    tasks.firstWhere((task) => task.id == id).title = title;
  });

  void setOrder(int id, int order) => change(() {
    tasks.firstWhere((task) => task.id == id).order = order;
  });

  void add(String title) {
    title = title.trim();
    if (title.isEmpty) return;
    change(() {
      tasks.add(
        FlutterTask(nextId++, title, DateTime.now().microsecondsSinceEpoch),
      );
      input.clear();
    });
  }

  Future<void> edit(FlutterTask task) async {
    final title = await showDialog<String>(
      context: context,
      builder: (_) => _TaskEditor(title: task.title),
    );
    if (!mounted ||
        title == null ||
        title.trim().isEmpty ||
        !tasks.contains(task)) {
      return;
    }
    renameTask(task.id, title.trim());
  }

  @override
  void dispose() {
    input.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    widget.onBuild?.call('screen', null);
    // Ordinary derived state is recomputed during build, after setState returns.
    final visible =
        tasks
            .where(
              (task) => filter == 'All' || task.done == (filter == 'Completed'),
            )
            .toList()
          ..sort((a, b) {
            final order = a.order.compareTo(b.order);
            return order != 0 ? order : a.id.compareTo(b.id);
          });
    final complete = tasks.where((task) => task.done).length;
    return RepaintBoundary(
      key: widget.captureKey,
      child: Scaffold(
        body: SafeArea(
          child: Center(
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 820),
              child: Padding(
                padding: const EdgeInsets.fromLTRB(32, 30, 32, 20),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'A little more done.',
                      style: TextStyle(
                        fontSize: 28,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                    const SizedBox(height: 10),
                    const Text('Flutter · In memory'),
                    const SizedBox(height: 26),
                    Row(
                      children: [
                        Expanded(
                          child: TextField(
                            key: const ValueKey('new-task'),
                            controller: input,
                            decoration: const InputDecoration(
                              hintText: 'What would you like to get done?',
                              filled: true,
                              fillColor: Colors.white,
                            ),
                            onSubmitted: add,
                          ),
                        ),
                        const SizedBox(width: 12),
                        FilledButton.icon(
                          key: const ValueKey('add-task'),
                          onPressed: () => add(input.text),
                          icon: const Icon(Icons.add),
                          label: const Text('Add task'),
                        ),
                      ],
                    ),
                    const SizedBox(height: 24),
                    Row(
                      children: [
                        for (final name in ['All', 'Active', 'Completed'])
                          Padding(
                            padding: const EdgeInsets.only(right: 8),
                            child: ChoiceChip(
                              label: Text(name),
                              selected: filter == name,
                              onSelected: (_) => setState(() => filter = name),
                            ),
                          ),
                        const Spacer(),
                        Builder(
                          builder: (context) {
                            widget.onBuild?.call('counter', null);
                            return Text(
                              '$complete of ${tasks.length} complete',
                            );
                          },
                        ),
                      ],
                    ),
                    const SizedBox(height: 16),
                    Expanded(
                      child: Builder(
                        builder: (context) {
                          widget.onBuild?.call('list', null);
                          return DecoratedBox(
                            decoration: BoxDecoration(
                              color: Colors.white,
                              borderRadius: BorderRadius.circular(16),
                              border: Border.all(
                                color: const Color(0xffe1e5df),
                              ),
                            ),
                            child: visible.isEmpty
                                ? const Center(
                                    child: Text(
                                      'A clear list. Room for something good.',
                                    ),
                                  )
                                : ListView.builder(
                                    key: const ValueKey('todo-list'),
                                    padding: const EdgeInsets.symmetric(
                                      vertical: 8,
                                    ),
                                    itemExtent: 60,
                                    itemCount: visible.length,
                                    findChildIndexCallback: (key) {
                                      if (key is! ValueKey<int>) return null;
                                      final index = visible.indexWhere(
                                        (task) => task.id == key.value,
                                      );
                                      return index < 0 ? null : index;
                                    },
                                    itemBuilder: (context, index) {
                                      final entity = visible[index];
                                      widget.onBuild?.call('row', entity.id);
                                      final done = entity.done;
                                      return ListTile(
                                        key: ValueKey(entity.id),
                                        leading: Checkbox(
                                          value: done,
                                          onChanged: (value) => change(
                                            () => entity.done = value!,
                                          ),
                                        ),
                                        title: Text(
                                          entity.title,
                                          maxLines: 1,
                                          overflow: TextOverflow.ellipsis,
                                          style: TextStyle(
                                            decoration: done
                                                ? TextDecoration.lineThrough
                                                : null,
                                          ),
                                        ),
                                        trailing: Row(
                                          mainAxisSize: MainAxisSize.min,
                                          children: [
                                            IconButton(
                                              tooltip: 'Edit task',
                                              onPressed: () => edit(entity),
                                              icon: const Icon(
                                                Icons.edit_outlined,
                                                size: 19,
                                              ),
                                            ),
                                            IconButton(
                                              tooltip: 'Delete task',
                                              onPressed: () => change(
                                                () => tasks.remove(entity),
                                              ),
                                              icon: const Icon(
                                                Icons.close,
                                                size: 19,
                                              ),
                                            ),
                                          ],
                                        ),
                                      );
                                    },
                                  ),
                          );
                        },
                      ),
                    ),
                    const SizedBox(height: 16),
                    const SizedBox.shrink(),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _TaskEditor extends StatefulWidget {
  const _TaskEditor({required this.title});
  final String title;
  @override
  State<_TaskEditor> createState() => _TaskEditorState();
}

class _TaskEditorState extends State<_TaskEditor> {
  late final text = TextEditingController(text: widget.title);
  @override
  void dispose() {
    text.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
    title: const Text('Edit task'),
    content: TextField(
      controller: text,
      autofocus: true,
      onSubmitted: (value) => Navigator.pop(context, value),
    ),
    actions: [
      TextButton(
        onPressed: () => Navigator.pop(context),
        child: const Text('Cancel'),
      ),
      FilledButton(
        onPressed: () => Navigator.pop(context, text.text),
        child: const Text('Save'),
      ),
    ],
  );
}
