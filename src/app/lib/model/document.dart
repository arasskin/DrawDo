import 'dart:math';

/// Application data is ordinary Dart, independent of widgets and storage.
final class Task {
  const Task({required this.id, required this.title, this.completed = false});
  final String id;
  final String title;
  final bool completed;

  Task copyWith({String? title, bool? completed}) => Task(
    id: id,
    title: title ?? this.title,
    completed: completed ?? this.completed,
  );
  Map<String, Object> toJson() => {
    'id': id,
    'title': title,
    'completed': completed,
  };
  factory Task.fromJson(Map<String, dynamic> json) => Task(
    id: json['id'] as String,
    title: json['title'] as String,
    completed: json['completed'] as bool,
  );
}

/// X is relative to canvas width; Y is in scrollable content pixels.
final class InkPoint {
  const InkPoint(this.x, this.y);
  final double x;
  final double y;
  List<double> toJson() => [x, y];
  factory InkPoint.fromJson(List<dynamic> json) {
    final x = (json[0] as num).toDouble();
    final y = (json[1] as num).toDouble();
    if (!x.isFinite || !y.isFinite) {
      throw const FormatException('Invalid drawing coordinates');
    }
    return InkPoint(x, y);
  }
}

enum CanvasPage { space, tasks }

final class Stroke {
  Stroke({
    required this.id,
    required this.page,
    required this.color,
    required Iterable<InkPoint> points,
  }) : points = List.unmodifiable(points);
  final String id;
  final CanvasPage page;
  final int color;
  final List<InkPoint> points;
  Map<String, Object> toJson() => {
    'id': id,
    'page': page.name,
    'color': color,
    'points': points.map((p) => p.toJson()).toList(),
  };
  factory Stroke.fromJson(Map<String, dynamic> json) => Stroke(
    id: json['id'] as String,
    page: CanvasPage.values.byName(json['page'] as String),
    color: json['color'] as int,
    points: (json['points'] as List).map((p) => InkPoint.fromJson(p as List)),
  );
}

final class Sticker {
  const Sticker({
    required this.id,
    required this.asset,
    required this.label,
    required this.x,
    required this.y,
  });
  final String id;
  final String asset;
  final String label;
  // Fractions of the available travel area keep stickers reachable on resize.
  final double x;
  final double y;
  Sticker moved(double x, double y) => Sticker(
    id: id,
    asset: asset,
    label: label,
    x: x.clamp(0, 1),
    y: y.clamp(0, 1),
  );
  Map<String, Object> toJson() => {
    'id': id,
    'asset': asset,
    'label': label,
    'x': x,
    'y': y,
  };
  factory Sticker.fromJson(Map<String, dynamic> json) {
    final point = InkPoint.fromJson([json['x'], json['y']]);
    return Sticker(
      id: json['id'] as String,
      asset: json['asset'] as String,
      label: json['label'] as String,
      x: point.x.clamp(0, 1),
      y: point.y.clamp(0, 1),
    );
  }
}

final class AppDocument {
  AppDocument({
    Iterable<Task> tasks = const [],
    Iterable<Stroke> strokes = const [],
    Iterable<Sticker> stickers = const [],
  }) : tasks = List.unmodifiable(tasks),
       strokes = List.unmodifiable(strokes),
       stickers = List.unmodifiable(stickers);
  final List<Task> tasks;
  final List<Stroke> strokes;
  final List<Sticker> stickers;
  factory AppDocument.initial() => AppDocument(
    stickers: const [
      Sticker(
        id: 'my-list',
        asset: 'star.gif',
        label: 'My list',
        x: .18,
        y: .15,
      ),
      Sticker(
        id: 'heart',
        asset: 'heart.gif',
        label: 'A little joy',
        x: .78,
        y: .43,
      ),
      Sticker(
        id: 'penguin',
        asset: 'penguin.webp',
        label: 'One step at a time',
        x: .2,
        y: .76,
      ),
    ],
  );
  AppDocument copyWith({
    Iterable<Task>? tasks,
    Iterable<Stroke>? strokes,
    Iterable<Sticker>? stickers,
  }) => AppDocument(
    tasks: tasks ?? this.tasks,
    strokes: strokes ?? this.strokes,
    stickers: stickers ?? this.stickers,
  );
  Map<String, Object> toJson() => {
    'version': 1,
    'tasks': tasks.map((t) => t.toJson()).toList(),
    'strokes': strokes.map((s) => s.toJson()).toList(),
    'stickers': stickers.map((s) => s.toJson()).toList(),
  };
  factory AppDocument.fromJson(Map<String, dynamic> json) {
    if (json['version'] != 1) {
      throw const FormatException('Unsupported document version');
    }
    return AppDocument(
      tasks: (json['tasks'] as List).map(
        (t) => Task.fromJson(t as Map<String, dynamic>),
      ),
      strokes: (json['strokes'] as List).map(
        (s) => Stroke.fromJson(s as Map<String, dynamic>),
      ),
      stickers: (json['stickers'] as List).map(
        (s) => Sticker.fromJson(s as Map<String, dynamic>),
      ),
    );
  }
}

final _random = Random.secure();
String newId() => List.generate(
  16,
  (_) => _random.nextInt(256).toRadixString(16).padLeft(2, '0'),
).join();
