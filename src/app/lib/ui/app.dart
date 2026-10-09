import 'dart:async';
import 'package:flutter/material.dart';
import '../app_controller.dart';
import '../model/document.dart';
import 'ink_surface.dart';
import 'space.dart';
import 'task_list.dart';

class DrawDoApp extends StatelessWidget {
  const DrawDoApp({super.key, required this.controller});
  final AppController controller;
  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'DrawDo',
    debugShowCheckedModeBanner: false,
    theme: ThemeData(
      useMaterial3: true,
      scaffoldBackgroundColor: const Color(0xfffaf9f2),
      colorScheme: ColorScheme.fromSeed(
        seedColor: const Color(0xff66734f),
        surface: const Color(0xfffaf9f2),
      ),
      appBarTheme: const AppBarTheme(
        backgroundColor: Color(0xfffaf9f2),
        surfaceTintColor: Colors.transparent,
      ),
      inputDecorationTheme: const InputDecorationTheme(
        border: InputBorder.none,
      ),
    ),
    home: _Home(controller: controller),
  );
}

class _Home extends StatefulWidget {
  const _Home({required this.controller});
  final AppController controller;
  @override
  State<_Home> createState() => _HomeState();
}

class _HomeState extends State<_Home> with WidgetsBindingObserver {
  final _scroll = ScrollController();
  final _newTask = TextEditingController();
  CanvasPage _page = CanvasPage.space;
  CanvasTool _tool = CanvasTool.move;
  Color _color = const Color(0xff353d30);
  static const _colors = [
    Color(0xff353d30),
    Color(0xffd65b54),
    Color(0xffd9a23c),
    Color(0xff6c8b58),
    Color(0xff5a8fb3),
    Color(0xff9875b5),
  ];

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _scroll.dispose();
    _newTask.dispose();
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state != AppLifecycleState.resumed) {
      unawaited(widget.controller.flush());
    }
  }

  void _selectPage(CanvasPage page) {
    FocusManager.instance.primaryFocus?.unfocus();
    setState(() {
      _page = page;
      _tool = CanvasTool.move;
    });
  }

  void _addTask() {
    if (_newTask.text.trim().isEmpty) return;
    widget.controller.addTask(_newTask.text);
    _newTask.clear();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_scroll.hasClients) {
        _scroll.animateTo(
          _scroll.position.maxScrollExtent,
          duration: const Duration(milliseconds: 220),
          curve: Curves.easeOut,
        );
      }
    });
  }

  @override
  Widget build(BuildContext context) => ListenableBuilder(
    listenable: widget.controller,
    builder: (context, _) {
      final controller = widget.controller;
      if (!controller.ready) {
        return Scaffold(
          body: SafeArea(
            child: Center(
              child: controller.loadError == null
                  ? const CircularProgressIndicator()
                  : Padding(
                      padding: const EdgeInsets.all(32),
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          const Icon(Icons.folder_off_outlined, size: 40),
                          const SizedBox(height: 16),
                          Text(
                            controller.loadError!,
                            textAlign: TextAlign.center,
                          ),
                          TextButton(
                            onPressed: controller.load,
                            child: const Text('Retry'),
                          ),
                        ],
                      ),
                    ),
            ),
          ),
        );
      }
      return Scaffold(
        appBar: AppBar(
          toolbarHeight: 86,
          title: Padding(
            padding: const EdgeInsets.only(left: 8),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'DRAWDO',
                  style: TextStyle(
                    fontSize: 10,
                    letterSpacing: 3,
                    fontWeight: FontWeight.w700,
                    color: Color(0xff818773),
                  ),
                ),
                const SizedBox(height: 6),
                Text(
                  _page == CanvasPage.space ? 'My space' : 'My list',
                  style: const TextStyle(
                    fontSize: 30,
                    fontWeight: FontWeight.w600,
                    letterSpacing: -1,
                  ),
                ),
              ],
            ),
          ),
          actions: [
            Padding(
              padding: const EdgeInsets.only(right: 22),
              child: Tooltip(
                message:
                    'Stored on this device. Cloud sync is not connected yet.',
                child: Row(
                  children: [
                    Icon(
                      controller.saveError != null
                          ? Icons.error_outline
                          : Icons.circle,
                      size: controller.saveError != null ? 16 : 7,
                      color: controller.saveError != null
                          ? Colors.red
                          : const Color(0xff7d8c63),
                    ),
                    const SizedBox(width: 7),
                    Text(
                      controller.saveError != null
                          ? 'Not saved'
                          : controller.saving
                          ? 'Saving…'
                          : 'On this device',
                      style: const TextStyle(
                        fontSize: 11,
                        color: Color(0xff74776b),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
        body: SafeArea(
          top: false,
          child: Column(
            children: [
              if (controller.saveError != null)
                MaterialBanner(
                  content: Text(controller.saveError!),
                  actions: [
                    TextButton(
                      onPressed: controller.retrySave,
                      child: const Text('Retry save'),
                    ),
                  ],
                ),
              _toolbar(),
              Expanded(
                child: InkSurface(
                  key: ValueKey(_page),
                  controller: controller,
                  page: _page,
                  tool: _tool,
                  color: _color,
                  scrollController: _page == CanvasPage.tasks ? _scroll : null,
                  child: _page == CanvasPage.space
                      ? Space(
                          controller: controller,
                          openTasks: () => _selectPage(CanvasPage.tasks),
                        )
                      : TaskList(
                          controller: controller,
                          scrollController: _scroll,
                        ),
                ),
              ),
              if (_page == CanvasPage.tasks && _tool == CanvasTool.move)
                Padding(
                  padding: const EdgeInsets.fromLTRB(20, 8, 20, 12),
                  child: DecoratedBox(
                    decoration: BoxDecoration(
                      color: Colors.white,
                      border: Border.all(color: const Color(0xffdedfd3)),
                      borderRadius: BorderRadius.circular(20),
                    ),
                    child: Row(
                      children: [
                        const SizedBox(width: 18),
                        Expanded(
                          child: TextField(
                            key: const ValueKey('new-task'),
                            controller: _newTask,
                            maxLength: 300,
                            textCapitalization: TextCapitalization.sentences,
                            textInputAction: TextInputAction.done,
                            onSubmitted: (_) => _addTask(),
                            decoration: const InputDecoration(
                              hintText: 'One small thing to do…',
                              counterText: '',
                            ),
                          ),
                        ),
                        IconButton(
                          tooltip: 'Add task',
                          onPressed: _addTask,
                          icon: const Icon(Icons.add_circle, size: 30),
                        ),
                        const SizedBox(width: 6),
                      ],
                    ),
                  ),
                ),
            ],
          ),
        ),
        bottomNavigationBar: NavigationBar(
          height: 70,
          selectedIndex: _page.index,
          backgroundColor: const Color(0xfff1f1e6),
          onDestinationSelected: (index) =>
              _selectPage(CanvasPage.values[index]),
          destinations: const [
            NavigationDestination(
              icon: Icon(Icons.spa_outlined),
              selectedIcon: Icon(Icons.spa),
              label: 'My space',
            ),
            NavigationDestination(
              icon: Icon(Icons.checklist_rounded),
              label: 'My list',
            ),
          ],
        ),
      );
    },
  );

  Widget _toolbar() => Padding(
    padding: const EdgeInsets.fromLTRB(18, 0, 18, 10),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            for (final (tool, icon, label) in [
              (CanvasTool.move, Icons.open_with, 'Move'),
              (CanvasTool.pen, Icons.edit_outlined, 'Draw'),
              (CanvasTool.eraser, Icons.auto_fix_normal, 'Erase'),
            ])
              Padding(
                padding: const EdgeInsets.only(right: 6),
                child: IconButton.filledTonal(
                  tooltip: label,
                  isSelected: _tool == tool,
                  style: IconButton.styleFrom(
                    backgroundColor: _tool == tool
                        ? const Color(0xffe0e6d3)
                        : Colors.transparent,
                  ),
                  onPressed: () {
                    FocusManager.instance.primaryFocus?.unfocus();
                    setState(() => _tool = tool);
                  },
                  icon: Icon(icon, size: 21),
                ),
              ),
            const Spacer(),
            IconButton(
              tooltip: 'Undo last stroke',
              onPressed: widget.controller.strokes.any((s) => s.page == _page)
                  ? () => widget.controller.undoStroke(_page)
                  : null,
              icon: const Icon(Icons.undo, size: 21),
            ),
          ],
        ),
        if (_tool == CanvasTool.pen)
          Wrap(
            spacing: 4,
            children: [
              for (var i = 0; i < _colors.length; i++)
                Semantics(
                  label: [
                    'Black ink',
                    'Red ink',
                    'Yellow ink',
                    'Green ink',
                    'Blue ink',
                    'Purple ink',
                  ][i],
                  selected: _color == _colors[i],
                  button: true,
                  child: InkWell(
                    onTap: () => setState(() => _color = _colors[i]),
                    borderRadius: BorderRadius.circular(24),
                    child: SizedBox(
                      width: 42,
                      height: 42,
                      child: Center(
                        child: Container(
                          width: 23,
                          height: 23,
                          decoration: BoxDecoration(
                            color: _colors[i],
                            shape: BoxShape.circle,
                            border: Border.all(
                              color: _color == _colors[i]
                                  ? Colors.white
                                  : _colors[i],
                              width: 3,
                            ),
                            boxShadow: _color == _colors[i]
                                ? [
                                    BoxShadow(
                                      color: _colors[i],
                                      spreadRadius: 2,
                                    ),
                                  ]
                                : [],
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
            ],
          ),
      ],
    ),
  );
}
