// The old JSON store remains a standalone fixture, outside the production UI.
import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:drawdo/data/document_store.dart';
import 'package:drawdo/model/document.dart';

void main() {
  late Directory directory;
  late File file;
  setUp(() {
    directory = Directory.systemTemp.createTempSync('drawdo-file-test-');
    file = File('${directory.path}/drawdo.json');
  });
  tearDown(() => directory.deleteSync(recursive: true));
  test('concurrent fixture saves preserve the latest document', () async {
    final store = FileDocumentStore(file);
    await Future.wait([
      for (var i = 0; i < 30; i++)
        store.save(
          AppDocument(
            tasks: [Task(id: 'task', title: '$i')],
          ),
        ),
    ]);
    expect((await store.load())!.tasks.single.title, '29');
  });
  test(
    'unfinished temporary fixture write preserves the complete file',
    () async {
      final store = FileDocumentStore(file);
      await store.save(AppDocument.initial());
      await File('${file.path}.tmp').writeAsString('{incomplete');
      expect((await store.load())!.stickers.length, 3);
    },
  );
  test(
    'corrupt or unsupported fixtures fail without overwriting data',
    () async {
      for (final content in ['{broken', '{"version":999}']) {
        await file.writeAsString(content);
        await expectLater(
          FileDocumentStore(file).load(),
          throwsFormatException,
        );
        expect(await file.readAsString(), content);
      }
    },
  );
}
