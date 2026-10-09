import 'dart:convert';
import 'dart:io';
import '../model/document.dart';

abstract interface class DocumentStore {
  Future<AppDocument?> load();
  Future<void> save(AppDocument document);
}

/// Historical whole-document contract used only by the benchmark fixture.
abstract interface class BlockingDocumentStore implements DocumentStore {
  AppDocument commit(AppDocument document);
  void close();
}

/// One writer per app. Replace the previous snapshot only after flushing the
/// complete temporary file. A failed load never creates a new file.
final class FileDocumentStore implements DocumentStore {
  FileDocumentStore(this.file);
  final File file;
  Future<void> _writes = Future.value();
  @override
  Future<AppDocument?> load() async {
    if (!await file.exists()) return null;
    return AppDocument.fromJson(
      jsonDecode(await file.readAsString()) as Map<String, dynamic>,
    );
  }

  @override
  Future<void> save(AppDocument document) {
    final encoded = jsonEncode(document.toJson());
    final write = _writes.then((_) async {
      await file.parent.create(recursive: true);
      final temporary = File('${file.path}.tmp');
      await temporary.writeAsString(encoded, flush: true);
      await temporary.rename(file.path);
    });
    // Keep the queue usable after failure; the caller still receives the error.
    _writes = write.then<void>((_) {}, onError: (Object _, StackTrace _) {});
    return write;
  }
}

/// A local substitute for tests; never pretends to be a server.
final class MemoryDocumentStore implements DocumentStore {
  MemoryDocumentStore([this.document]);
  AppDocument? document;
  @override
  Future<AppDocument?> load() async => document;
  @override
  Future<void> save(AppDocument document) async {
    this.document = document;
  }
}
