import 'package:flutter/foundation.dart';
import 'data/app_views.dart';
import 'model/document.dart';

/// Flutter's notification bridge. Native Datafriend owns state and publication;
/// typed projections are read lazily by widgets after notification.
final class AppController extends ChangeNotifier {
  AppController(this.views) {
    views.database.onPublished = _published;
  }
  final AppViews views;
  bool ready = false;
  bool loading = false;
  bool get saving => false;
  String? loadError;
  String? saveError;
  bool _disposed = false;
  bool _editing = false;
  VoidCallback? _failedEdit;

  List<Task> get tasks => views.tasks;
  int get completedTasks => views.completedTasks;
  List<Sticker> get stickers => views.stickers;
  List<Stroke> get strokes => views.strokes;
  AppDocument get document => views.document; // read/export convenience only

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  void _published() {
    if (ready && !_editing) _notify();
  }

  Future<void> load({AppDocument? initial}) async {
    if (_disposed || loading || ready) return;
    loading = true;
    loadError = null;
    _notify();
    try {
      views.initialize(initial: initial);
      ready = true;
    } catch (_) {
      loadError =
          'Your saved space could not be opened. Your data has been kept. Try again.';
    } finally {
      loading = false;
      _notify();
    }
  }

  void _edit(VoidCallback edit) {
    if (_disposed || !ready) return;
    _editing = true;
    try {
      edit();
      _failedEdit = null;
      saveError = null;
    } catch (_) {
      _failedEdit = edit;
      saveError = 'The change could not be saved. Please try again.';
    } finally {
      _editing = false;
    }
    // ListenableBuilder schedules a build. No view decoding occurs here.
    _notify();
  }

  Future<void>
  flush() async {} // no queued UI edits; disk policy belongs to Datafriend
  void retrySave() {
    final edit = _failedEdit;
    if (edit != null) _edit(edit);
  }

  void addTask(String title) {
    title = title.trim();
    if (title.isNotEmpty) _edit(() => views.addTask(title));
  }

  void editTask(String id, String title) {
    title = title.trim();
    if (title.isEmpty) {
      deleteTask(id);
      return;
    }
    _edit(() => views.editTask(id, title));
  }

  void toggleTask(String id) => _edit(() => views.toggleTask(id));
  void deleteTask(String id) => _edit(() => views.deleteTask(id));
  void reorderTask(int oldIndex, int newIndex) =>
      _edit(() => views.reorderTask(oldIndex, newIndex));
  void moveSticker(String id, double x, double y) =>
      _edit(() => views.moveSticker(id, x, y));
  void addStroke(CanvasPage page, int color, List<InkPoint> points) {
    if (points.isEmpty) return;
    final retained = List<InkPoint>.unmodifiable(points);
    _edit(() => views.addStroke(page, color, retained));
  }

  void removeStrokes(Set<String> ids) {
    final retained = Set<String>.unmodifiable(ids);
    _edit(() => views.removeStrokes(retained));
  }

  void undoStroke(CanvasPage page) => _edit(() => views.undoStroke(page));

  @override
  void dispose() {
    _disposed = true;
    views.database.onPublished = null;
    views.close();
    super.dispose();
  }
}
