import 'dart:io';
import 'package:drawdo/data/datafriend.dart';

/// Interactive demo bootstrap only. Widgets own their query subscriptions.
Datafriend openTodoDatabase(String path, {String? libraryPath}) {
  final library =
      libraryPath ??
      Platform.environment['DATAFRIEND_LIBRARY'] ??
      '${File(Platform.resolvedExecutable).parent.parent.path}/Frameworks/libdatafriend.dylib';
  final database = Datafriend.open(path, libraryPath: library);
  try {
    final marker = database.subscribe(
      const DatafriendQuery([(QueryClause(0, 'app/initialized', true), true)]),
    );
    try {
      if (marker.isEmpty) {
        const titles = [
          'Plan the week',
          'Sketch a new idea',
          'Take a walk',
          'Review the prototype',
          'Read something interesting',
          'Make time for a friend',
        ];
        final facts = [Triple(0, 'app/initialized', true)];
        for (var i = 0; i < titles.length; i++) {
          final entity = database.newEntity();
          facts.addAll([
            Triple(entity, 'kind', 'task'),
            Triple(entity, 'id', '$entity'),
            Triple(entity, 'title', titles[i]),
            Triple(entity, 'done', false),
            Triple(entity, 'order', i),
          ]);
        }
        database.edit(add: facts);
      }
    } finally {
      marker.close();
    }
    return database;
  } catch (_) {
    database.close();
    rethrow;
  }
}
