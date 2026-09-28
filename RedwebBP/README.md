# Redweb Blueprint Sockets

A Blueprint-friendly WebSocket client for Redweb. The component keeps the existing Blueprint send/event API while speaking Redweb protocol v1 over Unreal Engine's native Windows WinHTTP WebSocket transport.

The receive loop runs on a native worker thread. Blueprint events are dispatched on Unreal's game thread, and callbacks from replaced connections are discarded. The plugin supports Windows editor and packaged builds and does not require Unreal's WebSockets runtime plugin.

## Connect it to a Redweb route

The component automatically negotiates `redwebVersion=1` on the WebSocket URL. The server route must enable protocol v1, typically through a socket contract:

```typescript
import { SocketRoute } from 'redweb';
import { defineSocketContract } from 'redweb/contract';
import { z } from 'zod';

const gameProtocol = defineSocketContract('1', {
    move: z.object({ cell: z.number().int().min(0).max(8) }),
});

const MoveHandler = gameProtocol.handler('move', (socket, move) => {
    // Validate and apply the player's move.
});

class GameRoute extends SocketRoute {
    constructor() {
        super({
            path: '/game',
            handlers: [MoveHandler],
            protocol: gameProtocol.protocol,
        });
    }
}
```

The client negotiates version 1 and exchanges protocol envelopes:

```json
{"v":"1","type":"move","payload":{"cell":4}}
```

## Blueprint API

- `Connect`, `Disconnect`, and `IsConnected`
- `SendRaw`, `SendJson`, and `SendTypedFields`
- `OnConnected`, `OnDisconnected`, and `OnError`
- `OnRawMessage` and `OnTypedMessage`

`SendJson(JsonPayload, Type)` and `SendTypedFields(Type, Fields)` keep their Blueprint-facing shape. They produce a protocol-v1 message automatically. `SendRaw` also accepts the former `{ "type": "...", ...fields }` shape and lifts its application fields into `payload`, so existing typed Blueprint calls can move to a protocol-v1 server without rebuilding their JSON. An already versioned envelope is passed through unchanged; malformed or untyped JSON is left untouched for Redweb to diagnose.

`OnRawMessage` receives the complete server frame, including its protocol envelope. `OnTypedMessage` receives the message type and serialized `payload` separately. Redweb protocol error envelopes are reported through `OnError` (and remain visible in `OnRawMessage`).

## Default connection

- `Server Url`: `ws://127.0.0.1:3000`
- `Route Path`: `/socket`
- Negotiation query: `redwebVersion=1` (added automatically)

Additional `Query Params` are URL-encoded and appended after the protocol version. A custom `redwebVersion` entry is ignored so it cannot silently select an unsupported wire format.

## Latency and performance

Message logging is disabled by default. Enable `Log Received Messages` for diagnostics and `Log Received Payloads` only when you need full JSON in the Unreal log. If the application payload includes a numeric `timestamp` in milliseconds since the Unix epoch, the log includes an estimated server-to-client latency.

- `Queue Incoming Messages` is disabled by default for lowest latency. Enable it to throttle Blueprint work to frame ticks.
- `Max Messages Per Frame` defaults to `120`; values at or below zero drain the full queue.
- Keep payload logging off during live gameplay: high-rate movement messages can make logging itself a source of frame hitches.

## Tests and coverage

The deterministic suite uses a real Redweb 0.16.5 server and real WebSocket connections for protocol negotiation, echo messages, large payloads, empty and binary frames, remote close metadata, protocol errors, and abrupt disconnects. It does not mock Redweb for integration coverage. Unit tests cover the JSON codecs, protocol envelope handling, component lifecycle, and test-report/coverage verifiers.

Run the baseline integration and Unreal automation suite:

```powershell
$env:UE4_EDITOR_CMD = 'C:\path\to\UE_4.27\Engine\Binaries\Win64\UE4Editor-Cmd.exe'
./Tests/run-baseline.ps1
```

Measure native production-source line coverage (the same deterministic suite is executed under coverage instrumentation):

```powershell
./Tests/run-native-coverage.ps1 -UnrealEditorCmd $env:UE4_EDITOR_CMD
```

The native gate requires 100% coverage of measured RedwebBP production lines. The Redweb fixture, report validators, and coverage verifier separately enforce 100% line, branch, function, and statement coverage. Unreal automation reports and logs are retained under `Tests/results/` for diagnosis. The test host disables VR plugins, including SteamVR, so these tests do not launch VR software.
