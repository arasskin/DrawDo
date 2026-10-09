// Standalone device probe. Run twice without uninstalling the app to check
// persistent identity across process death. Only a public key is recorded.
import 'dart:io';
import 'package:drawdo/data/datafriend.dart';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final directory = await getApplicationSupportDirectory();
  final marker = File('${directory.path}/identity-probe-public.txt');
  final identity = LittlebearIdentity.open('identity-process-probe-v1');
  final namespace = identity.namespace;
  identity.close();
  final priorProcess = marker.existsSync();
  if (priorProcess && marker.readAsStringSync() != namespace) {
    throw StateError('Identity changed across process restart');
  }
  marker.writeAsStringSync(namespace, flush: true);
  // ignore: avoid_print
  print(
    'IDENTITY_PROBE namespace=$namespace priorProcess=$priorProcess passed',
  );
  runApp(
    const MaterialApp(
      home: Scaffold(
        body: Center(child: Text('Datafriend secure identity check passed')),
      ),
    ),
  );
}
