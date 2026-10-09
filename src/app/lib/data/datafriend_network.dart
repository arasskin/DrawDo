part of 'datafriend.dart';

/// Public reads and owner-signed writes over plain TCP.
enum LittlebearMode { push, pull }

Uint8List _namespaceKey(String namespace) {
  if (!RegExp(r'^[0-9a-f]{64}$').hasMatch(namespace)) {
    throw ArgumentError(
      'A network namespace must be a lowercase Ed25519 public key (64 hex digits)',
    );
  }
  return Uint8List.fromList([
    for (var i = 0; i < namespace.length; i += 2)
      int.parse(namespace.substring(i, i + 2), radix: 16),
  ]);
}

final class LittlebearConfig {
  LittlebearConfig({
    required this.host,
    this.port = 7778,
    this.mode,
    this.identity,
    this.pollInterval = const Duration(seconds: 5),
    this.timeout = const Duration(seconds: 10),
  }) {
    if (host.isEmpty ||
        port < 1 ||
        port > 65535 ||
        pollInterval <= Duration.zero ||
        timeout <= Duration.zero ||
        (mode == LittlebearMode.push && identity == null)) {
      throw ArgumentError('Invalid Littlebear connection configuration');
    }
  }
  final String host;
  final int port;
  final LittlebearMode? mode;
  final LittlebearIdentity? identity;

  /// Retained for source compatibility. Updates now arrive from the server.
  final Duration pollInterval;
  final Duration timeout;
}

/// Explicit network operations; Datafriend.subscribe(query) remains local.
final class DatafriendSync {
  DatafriendSync._(this._store);
  final Datafriend _store;
  DatafriendNetwork push(
    LittlebearConfig config, {
    required LittlebearIdentity identity,
  }) {
    _store._check();
    _namespaceKey(_store.namespace);
    identity._check();
    if (identity.namespace != _store.namespace) {
      throw ArgumentError('Owner key does not match namespace');
    }
    return _store._attachNetwork(config, identity);
  }

  DatafriendNetwork subscribe(LittlebearConfig config) {
    _store._check();
    _namespaceKey(_store.namespace);
    return _store._attachNetwork(config, null);
  }
}

/// One namespace attached to the shared endpoint connection. Local edits only
/// schedule work; subscriptions are driven by server openings, never polling.
final class DatafriendNetwork {
  DatafriendNetwork._(this._store, this.config, this._identity) {
    _connection = _LittlebearConnection.acquire(config);
    _channel = _connection.attach(this);
  }
  final Datafriend _store;
  final LittlebearConfig config;
  final LittlebearIdentity? _identity;
  late final _LittlebearConnection _connection;
  late final int _channel;
  bool get _pushing => _identity != null;
  Timer? _timer;
  Future<void>? _active;
  bool _closed = false, _again = false, _subscribed = false;
  int _failures = 0, _generation = 0;
  _SyncReply? _pending, _latest;
  Object? lastError;
  DateTime? lastSynced;
  bool waitingForOwner = false;
  int exchanges = 0, bytesSent = 0, bytesReceived = 0;
  int challenges = 0,
      signedBatches = 0,
      requests = 0,
      reconciliationRequests = 0;
  int restarts = 0;
  bool get syncing => _active != null;
  int get connectionsOpened => _connection.connectionsOpened;
  bool sharesConnectionWith(DatafriendNetwork other) =>
      identical(_connection, other._connection);

  void _schedule() {
    if (_closed) return;
    if (_active != null) {
      _again = true;
      return;
    }
    _timer?.cancel();
    _timer = Timer(const Duration(milliseconds: 10), _background);
  }

  void _background() {
    if (!_closed) unawaited(syncNow().catchError((Object _) {}));
  }

  /// Explicit refresh/convergence barrier. Subscriptions otherwise run from
  /// server messages; owners run from local edits and reconnection.
  Future<void> syncNow() {
    if (_closed) return Future.error(StateError('Network connection closed'));
    _timer?.cancel();
    if (_active != null) {
      _again = true;
      return _active!;
    }
    return _active = _run(refresh: true);
  }

  Uint8List _native(int command, [Uint8List? packet]) {
    if (command != 4 && (_closed || !identical(_store._network, this))) {
      throw StateError('Network connection closed');
    }
    return _store._call(
      _Writer.request(14)
        ..u8(command)
        ..blob(packet ?? const []),
    );
  }

  void _opening(_SyncReply reply) {
    if (_closed || _pushing) return;
    if (_latest != null &&
        _latest!.epoch == reply.epoch &&
        reply.revision < _latest!.revision) {
      return;
    }
    _latest = _pending = reply;
    _generation++;
    if (_active == null) {
      _timer?.cancel();
      _active = _run(refresh: false);
      unawaited(_active!.catchError((Object _) {}));
    }
  }

  void _lost(Object error) {
    if (_closed) return;
    _subscribed = false;
    _pending = _latest = null;
    _generation++;
    lastError = error;
    if (_active == null) _schedule();
  }

  Future<_SyncReply> _request(
    int op,
    Uint8List payload, {
    int epoch = 0,
    int revision = 0,
    Uint8List? challenge,
    int sequence = 0,
  }) async {
    if (_closed) throw StateError('Network connection closed');
    requests++;
    if (op == 1) reconciliationRequests++;
    return _connection.request(
      this,
      op,
      payload,
      epoch: epoch,
      revision: revision,
      challenge: challenge,
      sequence: sequence,
    );
  }

  Future<void> _run({required bool refresh}) async {
    try {
      do {
        _again = false;
        if (_pushing) {
          for (var retry = 0; ; retry++) {
            try {
              await _push();
              break;
            } on _LittlebearRetry {
              if (_closed || retry == 3) rethrow;
              await Future<void>.delayed(const Duration(milliseconds: 10));
            }
          }
        } else {
          if (!_subscribed || refresh) {
            final opening = await _request(4, Uint8List(0));
            if (_closed) throw StateError('Network connection closed');
            _subscribed = true;
            _opening(opening);
          }
          while (_pending != null && !_closed) {
            final opening = _pending!;
            _pending = null;
            await _pull(opening, _generation);
          }
        }
        lastError = null;
        _failures = 0;
        refresh = _again;
      } while (_again && !_closed);
    } catch (error) {
      if (!_closed) {
        lastError = error;
        _failures++;
      }
      rethrow;
    } finally {
      _active = null;
      if (!_closed && _failures != 0) {
        _timer = Timer(
          Duration(milliseconds: 250 * (1 << _failures.clamp(0, 7))),
          _background,
        );
      }
    }
  }

  void _checkReply(_SyncReply reply) {
    if (reply.status == 1) throw const _LittlebearRetry();
    if (reply.status != 0 && reply.status != 3 && reply.status != 4) {
      throw StateError('Littlebear rejected request');
    }
  }

  Future<void> _push() async {
    var epoch = 0, revision = 0, sequence = 0;
    Uint8List? challenge;
    final deadline = Stopwatch()..start();
    Future<_SyncReply> request(int op, Uint8List payload) async {
      if (deadline.elapsed > const Duration(seconds: 30)) {
        throw TimeoutException('Push deadline');
      }
      final reply = await _request(
        op,
        payload,
        epoch: epoch,
        revision: revision,
        challenge: challenge,
        sequence: op == 3 ? sequence++ : 0,
      );
      _checkReply(reply);
      if (reply.challenge != null) {
        if (challenge != null ||
            !((op == 0 && reply.status == 3) ||
                (op == 1 && reply.status == 0))) {
          throw const FormatException('Unexpected write challenge');
        }
        challenge = reply.challenge;
        challenges++;
      }
      epoch = reply.epoch;
      revision = reply.revision;
      return reply;
    }

    try {
      var absent = (await request(0, Uint8List.fromList([1]))).status == 3;
      var packet = _native(0);
      for (var round = 0; ; round++) {
        if (round == 128) throw StateError('Reconciliation round limit');
        packet = _native(1, (await request(1, packet)).payload);
        if (packet.length == 4) break;
      }
      for (;;) {
        final changes = _native(2);
        if (_Reader(changes).u32() == 0 && !absent) break;
        if (challenge == null) {
          throw const FormatException('Missing write challenge');
        }
        await request(3, changes);
        absent = false;
      }
      exchanges++;
      lastSynced = DateTime.now();
    } finally {
      if (!_closed && identical(_store._network, this)) _native(4);
    }
  }

  Future<void> _pull(_SyncReply opening, int generation) async {
    if (opening.status != 1) _checkReply(opening);
    if (opening.challenge != null) {
      throw const FormatException('Challenge on subscription');
    }
    if (opening.status == 3) {
      waitingForOwner = true;
      _native(4);
      return;
    }
    waitingForOwner = false;
    // A write is in progress. Its completion will send a new opening.
    if (opening.status == 1 && opening.payload.isEmpty) return;
    final deadline = Stopwatch()..start();
    bool current() => !_closed && generation == _generation;
    Future<_SyncReply?> request(int op, Uint8List payload) async {
      if (deadline.elapsed > const Duration(seconds: 30)) {
        throw TimeoutException('Subscription sync deadline');
      }
      final reply = await _request(
        op,
        payload,
        epoch: opening.epoch,
        revision: opening.revision,
      );
      if (!current()) return null;
      if (reply.status == 1 ||
          reply.status == 3 ||
          reply.epoch != opening.epoch ||
          reply.revision != opening.revision) {
        restarts++;
        if (reply.status == 3 || reply.payload.isNotEmpty) _opening(reply);
        return null;
      }
      _checkReply(reply);
      return reply;
    }

    try {
      var packet = _native(5, opening.payload);
      for (var round = 0; packet.length != 4; round++) {
        if (!current()) return;
        if (round == 128) throw StateError('Reconciliation round limit');
        final reply = await request(1, packet);
        if (reply == null) return;
        packet = _native(1, reply.payload);
      }
      while (current()) {
        final keys = _native(3);
        final reply = await request(2, Uint8List.sublistView(keys, 1));
        if (reply == null) return;
        _store._call(_Writer.request(15)..blob(reply.payload));
        if (keys[0] == 1) {
          exchanges++;
          lastSynced = DateTime.now();
          break;
        }
      }
    } finally {
      if (!_closed && identical(_store._network, this)) _native(4);
    }
  }

  void _close() {
    if (_closed) return;
    _closed = true;
    _generation++;
    _timer?.cancel();
    _pending = _latest = null;
    _connection.detach(this);
    if (_store._handle != ffi.nullptr) _native(4);
  }
}

final class _SyncReply {
  _SyncReply(Uint8List packet) {
    final r = _Reader(packet);
    request = r.u32();
    channel = r.u32();
    final flags = r.u8();
    status = flags & 0x7f;
    epoch = r.u64();
    revision = r.u64();
    challenge = flags & 0x80 != 0 ? Uint8List.fromList(r.take(32)) : null;
    payload = r.take(packet.length - r.offset);
  }
  late final int request, channel, status, epoch, revision;
  late final Uint8List? challenge;
  late final Uint8List payload;
}

final class _LittlebearRetry implements Exception {
  const _LittlebearRetry();
  @override
  String toString() => 'Littlebear changed during sync; retry';
}

/// One TCP connection per endpoint, shared by all namespace handles. Exactly one
/// reader demultiplexes replies and unsolicited openings. No per-namespace socket.
final class _LittlebearConnection {
  _LittlebearConnection(this.config, this.key);
  static final Map<String, _LittlebearConnection> _pool = {};
  static _LittlebearConnection acquire(LittlebearConfig config) {
    final key = '${config.host}:${config.port}';
    return _pool.putIfAbsent(key, () => _LittlebearConnection(config, key));
  }

  final LittlebearConfig config;
  final String key;
  final Map<int, DatafriendNetwork> channels = {};
  final Map<int, Completer<_SyncReply>> pending = {};
  int _nextChannel = 1, _nextRequest = 1, connectionsOpened = 0;
  _LittlebearSocket? socket;
  Future<void>? connecting;
  Timer? heartbeat;
  bool disposed = false;
  int attach(DatafriendNetwork network) {
    if (channels.length == 64 || _nextChannel > 0xffffffff) {
      throw StateError('Connection channel limit');
    }
    final id = _nextChannel++;
    channels[id] = network;
    return id;
  }

  void detach(DatafriendNetwork network) {
    channels.remove(network._channel);
    if (channels.isEmpty) {
      disposed = true;
      _pool.remove(key);
      _fail(StateError('Connection closed'));
    } else if (socket != null) {
      unawaited(
        request(network, 5, Uint8List(0)).catchError((Object error) {
          return _SyncReply(Uint8List(25));
        }),
      );
    }
  }

  Future<void> ensure() async {
    if (disposed) throw StateError('Connection closed');
    if (socket != null) return;
    if (connecting != null) return connecting!;
    connecting = _connect();
    try {
      await connecting;
    } finally {
      connecting = null;
    }
  }

  Future<void> _connect() async {
    final connected = await Socket.connect(
      config.host,
      config.port,
      timeout: config.timeout,
    );
    if (disposed) {
      connected.destroy();
      throw StateError('Connection closed');
    }
    connected.setOption(SocketOption.tcpNoDelay, true);
    final peer = socket = _LittlebearSocket(connected, config.timeout);
    connectionsOpened++;
    unawaited(_read(peer));
    heartbeat = Timer.periodic(const Duration(seconds: 10), (_) {
      if (socket == peer) {
        unawaited(
          _ping().catchError((Object error) {
            _fail(error, peer);
          }),
        );
      }
    });
  }

  void _fail(Object error, [_LittlebearSocket? peer]) {
    if (peer != null && socket != peer) return;
    socket?.close();
    socket = null;
    heartbeat?.cancel();
    heartbeat = null;
    final waiters = pending.values.toList();
    pending.clear();
    for (final waiter in waiters) {
      if (!waiter.isCompleted) waiter.completeError(error);
    }
    for (final network in channels.values.toList()) {
      network._lost(error);
    }
  }

  Future<void> _read(_LittlebearSocket peer) async {
    try {
      while (!disposed && socket == peer) {
        final length = _Reader(await peer.read(4)).u32();
        if (length < 25 || length > 8 * 1024 * 1024) {
          throw const FormatException('Invalid frame length');
        }
        final reply = _SyncReply(await peer.read(length));
        final network = channels[reply.channel];
        if (network != null) network.bytesReceived += length + 4;
        if (reply.request == 0) {
          if (reply.status != 4 && reply.status != 3) {
            throw const FormatException('Invalid server opening');
          }
          network?._opening(reply);
        } else {
          final waiter = pending.remove(reply.request);
          if (waiter == null) throw const FormatException('Unknown reply');
          waiter.complete(reply);
        }
      }
    } catch (error) {
      _fail(error, peer);
    }
  }

  Uint8List _body(
    int id,
    int channel,
    int op,
    Uint8List namespace,
    Uint8List payload,
    int epoch,
    int revision,
  ) {
    final w = _Writer();
    for (final b in [76, 66, 83, 5]) {
      w.u8(b);
    }
    w.u32(id);
    w.u32(channel);
    w.u8(op);
    w.blob(namespace);
    w.string(datafriendAttributeSchema);
    w.u64(epoch);
    w.u64(revision);
    w.blob(payload);
    return w.finish();
  }

  Future<_SyncReply> _send(int id, Uint8List body, Duration timeout) async {
    final peer = socket!;
    final waiter = Completer<_SyncReply>();
    pending[id] = waiter;
    // Install the error listener before sending: disconnect may be synchronous.
    final result = waiter.future.timeout(timeout);
    try {
      peer.socket.add((_Writer()..blob(body)).finish());
    } catch (error) {
      _fail(error, peer);
    }
    try {
      return await result;
    } catch (error) {
      _fail(error, peer);
      rethrow;
    }
  }

  int _requestId() {
    if (_nextRequest > 0xffffffff) {
      throw StateError('Request counter exhausted');
    }
    return _nextRequest++;
  }

  Future<void> _ping() async {
    final id = _requestId();
    await _send(
      id,
      _body(id, 0, 7, Uint8List(0), Uint8List(0), 0, 0),
      config.timeout,
    );
  }

  Future<_SyncReply> request(
    DatafriendNetwork network,
    int op,
    Uint8List payload, {
    int epoch = 0,
    int revision = 0,
    Uint8List? challenge,
    int sequence = 0,
  }) async {
    await ensure();
    if (network._closed && op != 5) {
      throw StateError('Network connection closed');
    }
    final id = _requestId();
    var body = _body(
      id,
      network._channel,
      op,
      _namespaceKey(network._store.namespace),
      payload,
      epoch,
      revision,
    );
    if (op == 3) {
      final signed = Uint8List(body.length + 40)
        ..setAll(0, body)
        ..setAll(body.length, challenge!);
      ByteData.sublistView(signed).setUint64(body.length + 32, sequence);
      final signature = network._identity!._sign(signed);
      body = Uint8List(signed.length + 64)
        ..setAll(0, signed)
        ..setAll(signed.length, signature);
      network.signedBatches++;
    }
    if (body.length > 8 * 1024 * 1024) throw StateError('Frame limit');
    network.bytesSent += body.length + 4;
    final reply = await _send(id, body, network.config.timeout);
    if (reply.channel != network._channel) {
      throw const FormatException('Reply channel mismatch');
    }
    return reply;
  }
}

final class _LittlebearSocket {
  _LittlebearSocket(this.socket, this.timeout)
    : chunks = StreamIterator(socket);
  final Socket socket;
  final Duration timeout;
  final StreamIterator<Uint8List> chunks;
  Uint8List current = Uint8List(0);
  int offset = 0;
  bool closed = false;
  Future<Uint8List> read(int length) async {
    final result = Uint8List(length);
    var used = 0;
    while (used < length) {
      if (offset == current.length) {
        if (!await chunks.moveNext()) {
          throw const SocketException('Littlebear disconnected');
        }
        current = chunks.current;
        offset = 0;
      }
      final count = (current.length - offset).clamp(0, length - used);
      result.setRange(used, used + count, current, offset);
      used += count;
      offset += count;
    }
    return result;
  }

  void close() {
    if (closed) return;
    closed = true;
    socket.destroy();
    unawaited(chunks.cancel());
  }
}
