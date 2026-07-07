# Protocol V1

VisionReactor supports two client transports that carry the same image payloads and JSON result body.

## Raw TCP

Request:

```text
[4-byte big-endian body_length][JPEG/PNG bytes]
```

Response:

```text
[4-byte big-endian body_length][UTF-8 JSON]
```

Rules:

- `body_length` must be greater than 0 and no larger than 10 MiB.
- A TCP connection may send multiple frames continuously.
- Sticky packets and half packets are handled by the gateway buffer.
- Coordinates are returned in the original decoded image pixel space.

## WebSocket

Browser clients connect directly to the C++ gateway with a normal WebSocket Upgrade request.

- Client to gateway: binary WebSocket frames containing JPEG/PNG bytes.
- Gateway to client: text WebSocket frames containing UTF-8 JSON.
- Browser frames must be masked as required by RFC 6455.
- The first version supports complete, non-fragmented frames.

## JSON Response

Success:

```json
{
  "frame_id": 1,
  "ok": true,
  "error": "",
  "timing": {
    "total_us": 0,
    "gateway_us": 0,
    "parse_us": 0,
    "queue_to_grpc_us": 0,
    "grpc_round_trip_us": 0,
    "grpc_transport_us": 0,
    "infer_us": 0,
    "postprocess_us": 0
  },
  "detections": [
    {
      "class": "person",
      "confidence": 0.95,
      "x": 100,
      "y": 120,
      "w": 80,
      "h": 160
    }
  ]
}
```

Error:

```json
{
  "frame_id": 0,
  "ok": false,
  "error": "Too many in-flight frames; slow down the sender.",
  "code": "OVERLOADED",
  "timing": {
    "total_us": 0,
    "gateway_us": 0,
    "infer_us": 0
  },
  "detections": []
}
```

The gateway currently allows at most 2 in-flight AI requests per connection. Clients should lower their send FPS when they receive `OVERLOADED`.
