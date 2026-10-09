// Historical comparison UI and benchmark driver. The interactive Datafriend
// screen lives in todo_screen.dart and uses inline query subscriptions.
import 'package:flutter/material.dart';
import 'package:uuid/uuid.dart';
import 'change.dart';
import 'stores.dart';

final class TodoController extends ChangeNotifier {
  TodoController(this.store) {
    store.addListener(_published);
  }
  final TodoStore store;
  bool busy = false;
  String? error;
  ChangeTiming? last;
  String filter = 'All';
  void _published() => notifyListeners();
  List<Task> get visible => filter == 'All'
      ? store.tasks
      : store.tasks
            .where((t) => t.completed == (filter == 'Completed'))
            .toList();
  void selectFilter(String value) {
    filter = value;
    notifyListeners();
  }

  Future<void> apply(
    Future<void>? Function() action,
    bool Function() matches,
  ) async {
    if (busy) return;
    busy = true;
    error = null;
    notifyListeners();
    try {
      last = await change(store, action, matches);
    } catch (e) {
      error = e.toString();
    } finally {
      busy = false;
      notifyListeners();
    }
  }

  Future<void> add(String title) {
    final text = title.trim();
    if (text.isEmpty) return Future.value();
    final order = DateTime.now().microsecondsSinceEpoch;
    final id = const Uuid().v4();
    return apply(
      () => store.insert(id, text, order),
      () => store.tasks.any((t) => t.id == id),
    );
  }

  Future<void> toggle(Task task) => apply(
    () => store.toggle(task.id),
    () => store.tasks.any(
      (t) => t.id == task.id && t.completed != task.completed,
    ),
  );
  Future<void> rename(Task task, String title) {
    if (title.trim().isEmpty) return remove(task);
    return apply(
      () => store.rename(task.id, title.trim()),
      () => store.tasks.any((t) => t.id == task.id && t.title == title.trim()),
    );
  }

  Future<void> remove(Task task) => apply(
    () => store.remove(task.id),
    () => !store.tasks.any((t) => t.id == task.id),
  );
  @override
  void dispose() {
    store.removeListener(_published);
    super.dispose();
  }
}

ThemeData todoTheme() => ThemeData(
  useMaterial3: true,
  colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xff24665b)),
  scaffoldBackgroundColor: const Color(0xfff5f5ef),
  inputDecorationTheme: const InputDecorationTheme(
    border: OutlineInputBorder(),
  ),
);

class TodoScreen extends StatefulWidget {
  const TodoScreen({
    super.key,
    required this.controller,
    required this.kind,
    this.onSwitch,
    this.captureKey,
  });
  final TodoController controller;
  final StoreKind kind;
  final ValueChanged<StoreKind>? onSwitch;
  final GlobalKey? captureKey;
  @override
  State<TodoScreen> createState() => _TodoScreenState();
}

class _TodoScreenState extends State<TodoScreen> {
  final input = TextEditingController();
  @override
  void dispose() {
    input.dispose();
    super.dispose();
  }

  Future<void> edit(Task task) async {
    final text = TextEditingController(text: task.title);
    final value = await showDialog<String>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Edit task'),
        content: TextField(
          controller: text,
          autofocus: true,
          onSubmitted: (v) => Navigator.pop(context, v),
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
      ),
    );
    // The closing route may still use its controller during its transition.
    Future<void>.delayed(const Duration(seconds: 1), text.dispose);
    if (value != null && mounted) await widget.controller.rename(task, value);
  }

  @override
  Widget build(BuildContext context) => RepaintBoundary(
    key: widget.captureKey,
    child: AnimatedBuilder(
      animation: widget.controller,
      builder: (context, _) {
        final c = widget.controller, store = c.store, rows = c.visible;
        final name = widget.kind == StoreKind.datafriend
            ? 'Datafriend'
            : 'InstantDB';
        return Scaffold(
          body: SafeArea(
            child: Center(
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 820),
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(32, 30, 32, 20),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          const Icon(
                            Icons.check_circle_outline,
                            color: Color(0xff24665b),
                            size: 30,
                          ),
                          const SizedBox(width: 10),
                          const Expanded(
                            child: Text(
                              'A little more done.',
                              maxLines: 1,
                              overflow: TextOverflow.ellipsis,
                              style: TextStyle(
                                fontSize: 28,
                                fontWeight: FontWeight.w700,
                              ),
                            ),
                          ),
                          DropdownButton<StoreKind>(
                            value: widget.kind,
                            onChanged: c.busy || widget.onSwitch == null
                                ? null
                                : (v) => widget.onSwitch!(v!),
                            items: const [
                              DropdownMenuItem(
                                value: StoreKind.datafriend,
                                child: Text('Datafriend'),
                              ),
                              DropdownMenuItem(
                                value: StoreKind.instantdb,
                                child: Text('InstantDB'),
                              ),
                            ],
                          ),
                        ],
                      ),
                      const SizedBox(height: 10),
                      Text(
                        '$name · Saved on this device',
                        style: const TextStyle(color: Color(0xff64716b)),
                      ),
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
                              onSubmitted: c.busy
                                  ? null
                                  : (value) {
                                      input.clear();
                                      c.add(value);
                                    },
                            ),
                          ),
                          const SizedBox(width: 12),
                          FilledButton.icon(
                            key: const ValueKey('add-task'),
                            onPressed: c.busy
                                ? null
                                : () {
                                    final value = input.text;
                                    input.clear();
                                    c.add(value);
                                  },
                            icon: const Icon(Icons.add),
                            label: const Text('Add task'),
                          ),
                        ],
                      ),
                      const SizedBox(height: 24),
                      Row(
                        children: [
                          for (final filter in ['All', 'Active', 'Completed'])
                            Padding(
                              padding: const EdgeInsets.only(right: 8),
                              child: ChoiceChip(
                                label: Text(filter),
                                selected: c.filter == filter,
                                onSelected: (_) => c.selectFilter(filter),
                              ),
                            ),
                          const Spacer(),
                          Text(
                            '${store.completed} of ${store.tasks.length} complete',
                          ),
                        ],
                      ),
                      const SizedBox(height: 16),
                      Expanded(
                        child: DecoratedBox(
                          decoration: BoxDecoration(
                            color: Colors.white,
                            borderRadius: BorderRadius.circular(16),
                            border: Border.all(color: const Color(0xffe1e5df)),
                          ),
                          child: rows.isEmpty
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
                                  itemCount: rows.length,
                                  itemExtent: 60,
                                  itemBuilder: (context, i) {
                                    final task = rows[i];
                                    return ListTile(
                                      key: ValueKey(task.id),
                                      leading: Checkbox(
                                        value: task.completed,
                                        onChanged: c.busy
                                            ? null
                                            : (_) => c.toggle(task),
                                      ),
                                      title: Text(
                                        task.title,
                                        maxLines: 1,
                                        overflow: TextOverflow.ellipsis,
                                        style: TextStyle(
                                          decoration: task.completed
                                              ? TextDecoration.lineThrough
                                              : null,
                                          color: task.completed
                                              ? Colors.grey
                                              : const Color(0xff273832),
                                        ),
                                      ),
                                      trailing: Row(
                                        mainAxisSize: MainAxisSize.min,
                                        children: [
                                          IconButton(
                                            tooltip: 'Edit task',
                                            onPressed: c.busy
                                                ? null
                                                : () => edit(task),
                                            icon: const Icon(
                                              Icons.edit_outlined,
                                              size: 19,
                                            ),
                                          ),
                                          IconButton(
                                            tooltip: 'Delete task',
                                            onPressed: c.busy
                                                ? null
                                                : () => c.remove(task),
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
                        ),
                      ),
                      const SizedBox(height: 16),
                      if (c.error != null)
                        Text(
                          c.error!,
                          style: TextStyle(
                            color: Theme.of(context).colorScheme.error,
                          ),
                        )
                      else
                        Row(
                          children: [
                            Icon(
                              c.busy ? Icons.more_horiz : Icons.check,
                              size: 17,
                              color: const Color(0xff64716b),
                            ),
                            const SizedBox(width: 6),
                            Text(c.busy ? 'Updating…' : 'All changes applied'),
                            const Spacer(),
                            if (c.last != null)
                              Text(
                                'Last update ${(c.last!.publishUs / 1000).toStringAsFixed(1)} ms',
                                style: const TextStyle(
                                  color: Color(0xff64716b),
                                  fontSize: 12,
                                ),
                              ),
                          ],
                        ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        );
      },
    ),
  );
}
