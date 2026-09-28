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
