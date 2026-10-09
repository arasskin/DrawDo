part of 'datafriend.dart';

/// An opaque Datafriend-owned key. Dart sees only the public namespace and
/// signatures. Persistent aliases belong to this app's platform secure store.
final class LittlebearIdentity implements ffi.Finalizable {
  LittlebearIdentity._(this._native, this._handle, this.namespace) {
    _native.identityFinalizer.attach(this, _handle, detach: this);
  }
  final _Native _native;
  ffi.Pointer<ffi.Void> _handle;
  final String namespace;

  /// Load or create an identity in Keychain (Apple) or Keystore-protected app
  /// storage (Android). Reopen the same alias to keep the same namespace.
  /// Storage errors fail the operation; they never silently create a new key.
  factory LittlebearIdentity.open(String alias, {String? libraryPath}) =>
      LittlebearIdentity._open(alias, false, libraryPath);

  /// Explicitly unpersisted identity for tests. No key bytes cross the FFI.
  factory LittlebearIdentity.ephemeralForTesting({String? libraryPath}) =>
      LittlebearIdentity._open('', true, libraryPath);

  static LittlebearIdentity _open(String alias, bool ephemeral, String? path) {
    final native = _Native.load(path);
    return native.input(
      utf8.encode(alias),
      (input, size) => native.output((out) {
        final handle = native.identityOpen(input, size, ephemeral ? 1 : 0, out);
        if (handle == ffi.nullptr) native.failure(out);
        try {
          if (out.ref.size != 32) throw StateError('Invalid native public key');
          final namespace = out.ref.data
              .asTypedList(32)
              .map((b) => b.toRadixString(16).padLeft(2, '0'))
              .join();
          return LittlebearIdentity._(native, handle, namespace);
        } catch (_) {
          native.identityClose(handle);
          rethrow;
        }
      }),
    );
  }

  void _check() {
    if (_handle == ffi.nullptr) throw StateError('Identity is closed');
  }

  Uint8List _sign(Uint8List message) {
    _check();
    return _native.input(
      message,
      (input, size) => _native.output((out) {
        if (_native.identitySign(_handle, input, size, out) != 0) {
          _native.failure(out);
        }
        return Uint8List.fromList(out.ref.data.asTypedList(out.ref.size));
      }),
    );
  }

  /// Release and wipe the native signing key. The stored identity is retained.
  /// Disconnect connections using it before calling this method.
  void close() {
    if (_handle == ffi.nullptr) return;
    _native.identityFinalizer.detach(this);
    _native.identityClose(_handle);
    _handle = ffi.nullptr;
  }
}
