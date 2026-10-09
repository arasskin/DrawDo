import 'dart:math' as math;
import 'package:flutter/material.dart';
import '../app_controller.dart';
import '../model/document.dart';

enum CanvasTool { move, pen, eraser }

class InkSurface extends StatefulWidget {
  const InkSurface({
    super.key,
    required this.controller,
    required this.page,
    required this.tool,
    required this.color,
    required this.child,
    this.scrollController,
  });
  final AppController controller;
  final CanvasPage page;
  final CanvasTool tool;
  final Color color;
  final Widget child;
  final ScrollController? scrollController;
  @override
  State<InkSurface> createState() => _InkSurfaceState();
}

class _InkSurfaceState extends State<InkSurface> {
  final List<InkPoint> _points = [];
  final Set<String> _erased = {};
  double get _scroll => widget.scrollController?.hasClients == true
      ? widget.scrollController!.offset
      : 0;

  @override
  void initState() {
    super.initState();
    widget.scrollController?.addListener(_repaint);
  }

  @override
  void didUpdateWidget(InkSurface oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.scrollController != widget.scrollController) {
      oldWidget.scrollController?.removeListener(_repaint);
      widget.scrollController?.addListener(_repaint);
    }
    if (oldWidget.tool != widget.tool || oldWidget.page != widget.page) {
      _cancel();
    }
  }

  @override
  void dispose() {
    widget.scrollController?.removeListener(_repaint);
    super.dispose();
  }

  void _repaint() => setState(() {});
  void _cancel() {
    _points.clear();
    _erased.clear();
  }

  void _point(Offset local, double width) {
    final point = InkPoint((local.dx / width).clamp(0, 1), local.dy + _scroll);
    setState(() {
      if (widget.tool == CanvasTool.eraser) {
        final target = Offset(point.x * width, point.y);
        for (final stroke in widget.controller.strokes.where(
          (s) => s.page == widget.page,
        )) {
          for (var i = 0; i < stroke.points.length; i++) {
            final p = stroke.points[i];
            final q = stroke.points[math.max(0, i - 1)];
            final a = Offset(p.x * width, p.y);
            final b = Offset(q.x * width, q.y);
            final delta = b - a;
            final t = delta.distanceSquared == 0
                ? 0.0
                : (((target - a).dx * delta.dx + (target - a).dy * delta.dy) /
                          delta.distanceSquared)
                      .clamp(0.0, 1.0);
            if ((target - (a + delta * t)).distance <= 18) {
              _erased.add(stroke.id);
              break;
            }
          }
        }
      } else {
        _points.add(point);
      }
    });
  }

  void _finish() {
    if (_erased.isNotEmpty) widget.controller.removeStrokes(_erased);
    if (_points.isNotEmpty) {
      widget.controller.addStroke(
        widget.page,
        widget.color.toARGB32(),
        _points,
      );
    }
    setState(_cancel);
  }

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, constraints) {
      final drawing = widget.tool != CanvasTool.move;
      final width = constraints.maxWidth;
      return ClipRect(
        child: GestureDetector(
          key: ValueKey('ink-${widget.page.name}'),
          behavior: HitTestBehavior.opaque,
          onPanStart: drawing ? (d) => _point(d.localPosition, width) : null,
          onPanUpdate: drawing ? (d) => _point(d.localPosition, width) : null,
          onPanEnd: drawing ? (_) => _finish() : null,
          onPanCancel: drawing ? () => setState(_cancel) : null,
          onTapUp: drawing
              ? (d) {
                  _point(d.localPosition, width);
                  _finish();
                }
              : null,
          onLongPressStart: !drawing
              ? (d) => _point(d.localPosition, width)
              : null,
          onLongPressMoveUpdate: !drawing
              ? (d) => _point(d.localPosition, width)
              : null,
          onLongPressEnd: !drawing ? (_) => _finish() : null,
          onLongPressCancel: !drawing ? () => setState(_cancel) : null,
          child: Stack(
            fit: StackFit.expand,
            children: [
              IgnorePointer(ignoring: drawing, child: widget.child),
              IgnorePointer(
                child: CustomPaint(
                  painter: _InkPainter(
                    strokes: widget.controller.strokes
                        .where(
                          (s) =>
                              s.page == widget.page && !_erased.contains(s.id),
                        )
                        .toList(),
                    points: List.of(_points),
                    color: widget.color,
                    scroll: _scroll,
                  ),
                ),
              ),
            ],
          ),
        ),
      );
    },
  );
}

class _InkPainter extends CustomPainter {
  _InkPainter({
    required this.strokes,
    required this.points,
    required this.color,
    required this.scroll,
  });
  final List<Stroke> strokes;
  final List<InkPoint> points;
  final Color color;
  final double scroll;
  void _paintStroke(
    Canvas canvas,
    Size size,
    List<InkPoint> points,
    Color color,
  ) {
    if (points.isEmpty) return;
    final paint = Paint()
      ..color = color
      ..strokeWidth = 4
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round
      ..style = PaintingStyle.stroke;
    Offset offset(InkPoint p) => Offset(p.x * size.width, p.y - scroll);
    if (points.length == 1) {
      canvas.drawCircle(
        offset(points.first),
        2,
        paint..style = PaintingStyle.fill,
      );
      return;
    }
    final first = offset(points.first);
    final path = Path()..moveTo(first.dx, first.dy);
    for (final point in points.skip(1)) {
      final p = offset(point);
      path.lineTo(p.dx, p.dy);
    }
    canvas.drawPath(path, paint);
  }

  @override
  void paint(Canvas canvas, Size size) {
    for (final stroke in strokes) {
      _paintStroke(canvas, size, stroke.points, Color(stroke.color));
    }
    _paintStroke(canvas, size, points, color);
  }

  @override
  bool shouldRepaint(_InkPainter oldDelegate) => true;
}
