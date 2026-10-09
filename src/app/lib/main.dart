import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';

import 'app_controller.dart';
import 'data/datafriend.dart';
import 'data/app_views.dart';
import 'ui/app.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const _Bootstrap());
}

class _Bootstrap extends StatefulWidget {
  const _Bootstrap();
  @override
  State<_Bootstrap> createState() => _BootstrapState();
}

class _BootstrapState extends State<_Bootstrap> {
  AppController? _controller;
  Object? _error;

  @override
  void initState() {
    super.initState();
    _open();
  }

  Future<void> _open() async {
    setState(() => _error = null);
    try {
      final directory = await getApplicationSupportDirectory();
      if (!mounted) return;
      final controller = AppController(
        AppViews(Datafriend.open('${directory.path}/datafriend-v3')),
      );
      setState(() => _controller = controller);
      await controller.load();
    } catch (error) {
      if (mounted) setState(() => _error = error);
    }
  }

  @override
  void dispose() {
    _controller?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => _controller != null
      ? DrawDoApp(controller: _controller!)
      : MaterialApp(
          home: Scaffold(
            body: Center(
              child: _error == null
                  ? const CircularProgressIndicator()
                  : Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        const Text('Could not open device storage.'),
                        TextButton(
                          onPressed: _open,
                          child: const Text('Retry'),
                        ),
                      ],
                    ),
            ),
          ),
        );
}
