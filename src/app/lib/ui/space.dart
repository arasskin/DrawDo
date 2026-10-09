import 'dart:math' as math;
import 'package:flutter/material.dart';
import '../app_controller.dart';
import '../model/document.dart';

class Space extends StatelessWidget {
  const Space({super.key, required this.controller, required this.openTasks});
  final AppController controller;
  final VoidCallback openTasks;
  @override
  Widget build(BuildContext context) => CustomPaint(
    painter: const _PaperPainter(),
    child: LayoutBuilder(
      builder: (context, constraints) => Stack(
        children: [
          const Positioned(
            top: 16,
            left: 24,
            right: 24,
            child: Text(
              'A little room to make things happen.',
              style: TextStyle(color: Color(0xff838475), fontSize: 14),
            ),
          ),
          for (final sticker in controller.stickers)
            _MovableSticker(
              key: ValueKey(sticker.id),
              sticker: sticker,
              bounds: constraints.biggest,
              onTap: sticker.id == 'my-list' ? openTasks : null,
              onCommit: (x, y) => controller.moveSticker(sticker.id, x, y),
            ),
          const Positioned(
            bottom: 18,
            left: 24,
            right: 24,
            child: Text(
              'Move the little things. Leave a doodle.\nTap the star for your list.',
              textAlign: TextAlign.center,
              style: TextStyle(
                color: Color(0xff838475),
                fontSize: 13,
                height: 1.6,
              ),
            ),
          ),
        ],
      ),
    ),
  );
}

class _MovableSticker extends StatefulWidget {
  const _MovableSticker({
    super.key,
    required this.sticker,
    required this.bounds,
    required this.onCommit,
    this.onTap,
  });
  final Sticker sticker;
  final Size bounds;
  final void Function(double, double) onCommit;
  final VoidCallback? onTap;
  @override
  State<_MovableSticker> createState() => _MovableStickerState();
}

class _MovableStickerState extends State<_MovableSticker>
    with SingleTickerProviderStateMixin {
  late final AnimationController _flight;
  Offset? _position;
  Offset _start = Offset.zero;
  Offset _end = Offset.zero;
  bool _dragging = false;
  double get _width => math.max(1, widget.bounds.width - 120);
  double get _height => math.max(1, widget.bounds.height - 150);
  Offset get _current =>
      _position ??
      Offset(widget.sticker.x * _width, widget.sticker.y * _height);
  Offset _clamp(Offset p) =>
      Offset(p.dx.clamp(0, _width), p.dy.clamp(0, _height));

  @override
  void initState() {
    super.initState();
    _flight =
        AnimationController(
            vsync: this,
            duration: const Duration(milliseconds: 380),
          )
          ..addListener(
            () => setState(() {
              _position = Offset.lerp(
                _start,
                _end,
                Curves.decelerate.transform(_flight.value),
              );
            }),
          )
          ..addStatusListener((status) {
            if (status == AnimationStatus.completed) {
              setState(() => _position = null);
            }
          });
  }

  void _commit() {
    final position = _clamp(_current);
    widget.onCommit(position.dx / _width, position.dy / _height);
    _position = null;
  }

  @override
  void dispose() {
    _flight.dispose();
    super.dispose();
  }

  @override
  void didUpdateWidget(_MovableSticker oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.bounds != oldWidget.bounds) {
      _flight.stop();
      _position = null;
      _dragging = false;
    }
  }

  @override
  Widget build(BuildContext context) {
    final position = _clamp(_current);
    return Positioned(
      left: position.dx,
      top: position.dy,
      width: 120,
      height: 130,
      child: Semantics(
        label: widget.sticker.label,
        button: widget.onTap != null,
        child: GestureDetector(
          onTap: widget.onTap,
          onPanStart: (_) {
            _flight.stop();
            setState(() {
              _position = _current;
              _dragging = true;
            });
          },
          onPanUpdate: (d) =>
              setState(() => _position = _clamp(_current + d.delta)),
          onPanCancel: () {
            setState(() => _dragging = false);
            _commit();
          },
          onPanEnd: (d) {
            setState(() => _dragging = false);
            _start = _current;
            _end = _clamp(_start + d.velocity.pixelsPerSecond * .12);
            // Save the resting point now, even if the user switches pages mid-flight.
            widget.onCommit(_end.dx / _width, _end.dy / _height);
            _flight.forward(from: 0);
          },
          child: AnimatedScale(
            scale: _dragging ? 1.08 : 1,
            duration: const Duration(milliseconds: 130),
            child: Column(
              children: [
                Image.asset(
                  'assets/animated/${widget.sticker.asset}',
                  width: 86,
                  height: 86,
                  fit: BoxFit.contain,
                  excludeFromSemantics: true,
                ),
                const SizedBox(height: 6),
                Text(
                  widget.sticker.label,
                  textAlign: TextAlign.center,
                  style: const TextStyle(
                    fontSize: 13,
                    color: Color(0xff535747),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _PaperPainter extends CustomPainter {
  const _PaperPainter();
  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()..color = const Color(0xffdeded0);
    for (double x = 16; x < size.width; x += 24) {
      for (double y = 16; y < size.height; y += 24) {
        canvas.drawCircle(Offset(x, y), .7, paint);
      }
    }
  }

  @override
  bool shouldRepaint(_PaperPainter oldDelegate) => false;
}
