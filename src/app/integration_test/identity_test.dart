import 'dart:io';
import 'package:drawdo/data/datafriend.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:path_provider/path_provider.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  testWidgets(
    'native secure identity survives reopen and rejects damaged storage',
    (tester) async {
      final support = await getApplicationSupportDirectory();
      const alias = 'integration-test-persistent-owner-v1';
      final first = LittlebearIdentity.open(alias);
      final namespace = first.namespace;
      first.close();
      final reopened = LittlebearIdentity.open(alias);
      expect(reopened.namespace, namespace);
      reopened.close();
      // Public-only marker verifies another app process run, never stores a seed.
      final marker = File('${support.path}/identity-test-public.txt');
      final priorProcess = marker.existsSync();
      if (priorProcess) expect(marker.readAsStringSync(), namespace);
      marker.writeAsStringSync(namespace, flush: true);
      final transient = LittlebearIdentity.ephemeralForTesting();
      expect(transient.namespace, isNot(namespace));
      transient.close();

      if (Platform.isAndroid) {
        final vault = Directory(
          '${support.parent.path}/no_backup/datafriend-identities',
        );
        final savedPaths = vault.listSync().map((f) => f.path).toSet();
        final alias =
            'integration-corruption-${DateTime.now().microsecondsSinceEpoch}';
        final key = LittlebearIdentity.open(alias);
        final expected = key.namespace;
        key.close();
        final encrypted = File(
          vault
              .listSync()
              .singleWhere(
                (f) => f.path.endsWith('.key') && !savedPaths.contains(f.path),
              )
              .path,
        );
        final bytes = encrypted.readAsBytesSync();
        expect(bytes.length, 61); // version + IV + AES-GCM ciphertext/tag
        try {
          final damaged = List<int>.of(bytes);
          damaged[20] ^= 1;
          encrypted.writeAsBytesSync(damaged, flush: true);
          expect(() => LittlebearIdentity.open(alias), throwsStateError);
          encrypted.deleteSync();
          expect(() => LittlebearIdentity.open(alias), throwsStateError);
        } finally {
          encrypted.writeAsBytesSync(bytes, flush: true);
        }
        final recovered = LittlebearIdentity.open(alias);
        expect(recovered.namespace, expected);
        recovered.close();
      }
      // ignore: avoid_print
      print(
        'IDENTITY_TEST namespace=$namespace priorProcess=$priorProcess passed',
      );
    },
  );
}
