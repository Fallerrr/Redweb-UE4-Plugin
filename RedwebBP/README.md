# Redweb Blueprint Sockets

Blueprint-friendly WebSocket client for Gemhouse/Redweb. This version keeps the existing Blueprint API stable while replacing Unreal's `WebSockets` runtime module with a dedicated WinHTTP WebSocket transport.

The transport runs its receive loop on its own native thread, forwards packets directly to Unreal's game thread, and uses generation checks to discard callbacks from a previous connection. It is designed for Windows packaged builds as well as the editor and does not require Unreal's WebSockets plugin.

## Blueprint API

- `Connect`
- `Disconnect`
- `SendRaw`
- `SendJson`
- `SendTypedFields`
- `IsConnected`
- `OnConnected`
- `OnDisconnected`
- `OnError`
- `OnRawMessage`
- `OnTypedMessage`

## Default Connection

- `Server Url`: `ws://127.0.0.1:3000`
- `Route Path`: `/socket`

The final URL is built by joining `Server Url`, `Route Path`, and any query params.

## Typed Messages

`SendJson(JsonPayload, Type)` parses `JsonPayload`, injects the `type` field, and sends the final JSON to the server.

`OnTypedMessage` reads incoming JSON, extracts the `type` field as the first event parameter, removes `type` from the payload JSON, and sends the remaining JSON as the second event parameter.

## Latency Logging

Message logging is disabled by default for gameplay performance. Enable `Log Received Messages` only while debugging, and enable `Log Received Payloads` only when you truly need the full JSON body in the Unreal log.

If the server includes a numeric `timestamp` field in milliseconds since Unix epoch, the plugin logs:

```text
Redweb received type=<type> server_to_client_latency=<ms> payload=<json>
```

This helps distinguish socket/plugin delay from Blueprint-side processing delay.

## Performance Settings

- `Queue Incoming Messages`: disabled by default. Leave it disabled for the lowest latency; messages are delivered on the next available game-thread task. Enable it only if your Blueprint message work needs explicit per-frame throttling.
- `Max Messages Per Frame`: defaults to `120`. Lower this if online play hitches during packet bursts. Raise it if gameplay feels delayed during very busy matches.
- `Log Received Messages`: disabled by default.
- `Log Received Payloads`: disabled by default.

For live gameplay, keep payload logging off. Movement/action-heavy matches can send enough packets that logging every JSON payload will cause visible framerate drops. The component still exposes all existing connection, reconnect, heartbeat, raw-message, typed-message, and JSON helper functionality.

## Baseline Tests

Before changing the wire protocol, run the characterization suite from this plugin directory:

```powershell
$env:UE4_EDITOR_CMD = 'C:\path\to\UE_4.27\Engine\Binaries\Win64\UE4Editor-Cmd.exe'
./Tests/run-baseline.ps1
```

The suite uses a real Redweb 0.16.5 server, not mocks. It records the current compatibility boundary: raw `{ type, ...fields }` messages work on a legacy-compatible route, but a protocol-versioned route rejects the current handshake unless the client negotiates `redwebVersion=1` and sends the versioned envelope. Unreal automation tests characterize the Blueprint component's legacy JSON helpers and exercise its WinHTTP transport against that local server.

The helper-level Unreal tests cover both branches in `ExtractTypedPayload` and `BuildJsonFromFields`. The executable Node integration fixture is enforced at 100% line, branch, function, and statement coverage. The Unreal tests require an installed UE 4.27 editor to compile and run; `UE4_EDITOR_CMD` must point to that installation. Keep protocol-upgrade work gated on a successful run of the Unreal automation suite and measured coverage for the plugin code.

The baseline runner also validates UE4's exported `index.json` automation report: both named tests must appear exactly once and every reported test must succeed. Automation reports and the Unreal log are retained under `Tests/results/<run-id>/` for review; a zero process exit without the expected executed tests is not accepted as a pass. The report checker itself has unit tests with 100% line, branch, function, and statement coverage.
