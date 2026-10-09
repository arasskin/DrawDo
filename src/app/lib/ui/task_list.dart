import 'package:flutter/material.dart';
import '../app_controller.dart';
import '../model/document.dart';

class TaskList extends StatelessWidget {
  const TaskList({
    super.key,
    required this.controller,
    required this.scrollController,
  });
  final AppController controller;
  final ScrollController scrollController;

  Future<void> _edit(BuildContext context, Task task) async {
    final result = await showDialog<String>(
      context: context,
      builder: (_) => _TaskEditor(task: task),
    );
    if (result != null) controller.editTask(task.id, result);
  }

  @override
  Widget build(BuildContext context) {
    final tasks = controller.tasks;
    return ReorderableListView.builder(
      scrollController: scrollController,
      padding: const EdgeInsets.fromLTRB(16, 8, 16, 180),
      buildDefaultDragHandles: false,
      header: tasks.isEmpty
          ? const Padding(
              padding: EdgeInsets.symmetric(horizontal: 16, vertical: 52),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Icon(
                    Icons.wb_sunny_outlined,
                    size: 38,
                    color: Color(0xffa5a75f),
                  ),
                  SizedBox(height: 22),
                  Text(
                    'Start with one small thing.',
                    style: TextStyle(fontSize: 25, fontWeight: FontWeight.w500),
                  ),
                  SizedBox(height: 10),
                  Text(
                    'Add a task below. There’s room for a doodle, too.',
                    style: TextStyle(color: Color(0xff74776b), height: 1.6),
                  ),
                ],
              ),
            )
          : Padding(
              padding: const EdgeInsets.fromLTRB(12, 6, 12, 20),
              child: Text(
                '${controller.completedTasks} of ${tasks.length} done · at your own pace',
                style: const TextStyle(color: Color(0xff74776b)),
              ),
            ),
      itemCount: tasks.length,
      onReorder: controller.reorderTask,
      itemBuilder: (context, index) {
        final task = tasks[index];
        return Container(
          key: ValueKey(task.id),
          margin: const EdgeInsets.only(bottom: 8),
          decoration: BoxDecoration(
            color: Colors.white.withValues(alpha: .75),
            borderRadius: BorderRadius.circular(18),
          ),
          child: Row(
            children: [
              Checkbox(
                value: task.completed,
                semanticLabel: 'Complete ${task.title}',
                onChanged: (_) => controller.toggleTask(task.id),
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(5),
                ),
              ),
              Expanded(
                child: InkWell(
                  onTap: () => _edit(context, task),
                  child: Padding(
                    padding: const EdgeInsets.symmetric(vertical: 18),
                    child: Text(
                      task.title,
                      style: TextStyle(
                        fontSize: 17,
                        height: 1.4,
                        color: task.completed
                            ? const Color(0xff929588)
                            : const Color(0xff303729),
                        decoration: task.completed
                            ? TextDecoration.lineThrough
                            : null,
                      ),
                    ),
                  ),
                ),
              ),
              IconButton(
                tooltip: 'Delete ${task.title}',
                iconSize: 19,
                onPressed: () => controller.deleteTask(task.id),
                icon: const Icon(Icons.close),
              ),
              ReorderableDragStartListener(
                index: index,
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(4, 16, 14, 16),
                  child: Semantics(
                    label: 'Reorder ${task.title}',
                    child: const Icon(
                      Icons.drag_indicator,
                      color: Color(0xff9c9f92),
                      size: 22,
                    ),
                  ),
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _TaskEditor extends StatefulWidget {
  const _TaskEditor({required this.task});
  final Task task;
  @override
  State<_TaskEditor> createState() => _TaskEditorState();
}

class _TaskEditorState extends State<_TaskEditor> {
  late final _text = TextEditingController(text: widget.task.title);
  @override
  void dispose() {
    _text.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
    title: const Text('Edit task'),
    content: TextField(
      controller: _text,
      autofocus: true,
      maxLength: 300,
      textCapitalization: TextCapitalization.sentences,
      onSubmitted: (_) => Navigator.pop(context, _text.text),
      decoration: const InputDecoration(
        labelText: 'What would you like to do?',
      ),
    ),
    actions: [
      TextButton(
        onPressed: () => Navigator.pop(context),
        child: const Text('Cancel'),
      ),
      FilledButton(
        onPressed: () => Navigator.pop(context, _text.text),
        child: const Text('Save'),
      ),
    ],
  );
}
