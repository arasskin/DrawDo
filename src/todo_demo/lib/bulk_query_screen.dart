// Retained pre-row-query UI for matched benchmarks.
import 'package:drawdo/data/datafriend.dart';
import 'package:drawdo/data/datafriend_builder.dart';
import 'package:flutter/material.dart';
import 'build_probe.dart';

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
    required this.database,
    this.captureKey,
    this.onBuild,
  });
  final Datafriend database;
  final GlobalKey? captureKey;
  final TodoBuildObserver? onBuild;
  @override
  State<TodoScreen> createState() => _TodoScreenState();
}

class _TodoScreenState extends State<TodoScreen> {
  final input = TextEditingController();
  String filter = 'All';
  final error = ValueNotifier<Object?>(null);
  Datafriend get database => widget.database;

  void add(String title) {
    title = title.trim();
    if (title.isEmpty) return;
    attempt(() {
      final entity = database.newEntity();
      database.edit(
        add: [
          Triple(entity, 'kind', 'task'),
          Triple(entity, 'id', '$entity'),
          Triple(entity, 'title', title),
          Triple(entity, 'done', false),
          Triple(entity, 'order', DateTime.now().microsecondsSinceEpoch),
        ],
      );
      input.clear();
    }, error: error);
  }

  Future<void> edit(DatafriendEntity entity) async {
    final id = entity.id;
    final initialTitle = entity['title'] as String;
    final title = await showDialog<String>(
      context: context,
      builder: (_) => _TaskEditor(title: initialTitle),
    );
    if (!mounted || title == null || title.trim().isEmpty) return;
    attempt(() {
      // Check existence after the dialog; the entity may have been deleted.
      final current = database.subscribe(
        DatafriendQuery([(QueryClause(id, 'kind', 'task'), true)]),
      );
      try {
        if (current.isNotEmpty) {
          database.edit(add: [Triple(id, 'title', title.trim())]);
        }
      } finally {
        current.close();
      }
    }, error: error);
  }

  @override
  void dispose() {
    input.dispose();
    error.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    widget.onBuild?.call('screen', null);
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
                    const Text('Datafriend · Stored on this device'),
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
                        DatafriendBuilder(
                          database: database,
                          query: const DatafriendQuery([
                            (
                              QueryClause(QueryVariable(0), 'kind', 'task'),
                              false,
                            ),
                            (
                              QueryClause(
                                QueryVariable(0),
                                'done',
                                QueryVariable(1),
                              ),
                              true,
                            ),
                          ]),
                          builder: (context, view) {
                            widget.onBuild?.call('counter', null);
                            return Text(
                              '${view.countWhere('done', true)} of ${view.length} complete',
                            );
                          },
                        ),
                      ],
                    ),
                    const SizedBox(height: 16),
                    Expanded(
                      child: DatafriendBuilder(
                        database: database,
                        query: DatafriendQuery([
                          (
                            const QueryClause(QueryVariable(0), 'kind', 'task'),
                            true,
                          ),
                          (
                            const QueryClause(
                              QueryVariable(0),
                              'id',
                              QueryVariable(1),
                            ),
                            true,
                          ),
                          (
                            const QueryClause(
                              QueryVariable(0),
                              'title',
                              QueryVariable(2),
                            ),
                            true,
                          ),
                          (
                            QueryClause(
                              const QueryVariable(0),
                              'done',
                              filter == 'All'
                                  ? const QueryVariable(3)
                                  : filter == 'Completed',
                            ),
                            true,
                          ),
                          (
                            const QueryClause(
                              QueryVariable(0),
                              'order',
                              QueryVariable(4),
                            ),
                            true,
                          ),
                        ]),
                        orderBy: 'order',
                        builder: (context, view) {
                          widget.onBuild?.call('list', null);
                          return DecoratedBox(
                            decoration: BoxDecoration(
                              color: Colors.white,
                              borderRadius: BorderRadius.circular(16),
                              border: Border.all(
                                color: const Color(0xffe1e5df),
                              ),
                            ),
                            child: view.isEmpty
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
                                    itemCount: view.length,
                                    itemBuilder: (context, index) {
                                      final entity = view[index];
                                      widget.onBuild?.call('row', entity.id);
                                      final done = entity['done'] as bool;
                                      return ListTile(
                                        key: ValueKey(entity.id),
                                        leading: Checkbox(
                                          value: done,
                                          onChanged: (value) => attempt(
                                            () => database.edit(
                                              add: [
                                                Triple(
                                                  entity.id,
                                                  'done',
                                                  value!,
                                                ),
                                              ],
                                            ),
                                            error: error,
                                          ),
                                        ),
                                        title: Text(
                                          entity['title'] as String,
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
                                              onPressed: () => attempt(
                                                entity.delete,
                                                error: error,
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
                    ValueListenableBuilder<Object?>(
                      valueListenable: error,
                      builder: (context, failure, _) => failure == null
                          ? const SizedBox.shrink()
                          : Text(
                              '$failure',
                              style: TextStyle(
                                color: Theme.of(context).colorScheme.error,
                              ),
                            ),
                    ),
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
