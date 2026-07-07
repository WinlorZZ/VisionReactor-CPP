# VisionReactor Resume and Interview Notes

## Current Result Snapshot

- Built a C++17 Reactor-based AI vision gateway with epoll ET, non-blocking sockets, eventfd wakeup, connection lifetime protection, and async gRPC downstream calls.
- Browser demo now uploads local video, extracts JPEG frames, sends binary WebSocket frames to the C++ gateway, and renders YOLO detection boxes plus per-frame latency metrics.
- Remote YOLO inference service is accessed through gRPC/protobuf. The browser-facing result uses small JSON messages only for detections and latency metadata.
- Measured demo latency is about 50 ms end-to-end on the tested network. Gateway-side overhead is sub-millisecond in the observed run; most latency is gRPC transport plus remote inference.

## Resume Bullets

Pick 2-3 bullets depending on space:

- Implemented a C++17 asynchronous AI vision gateway using epoll ET Reactor, non-blocking I/O, eventfd cross-thread wakeup, and thread-safe task dispatch back to the EventLoop.
- Integrated async gRPC CompletionQueue with a Python YOLOv8 inference service, using weak_ptr-based connection association to safely return results after client disconnects.
- Designed a low-latency browser-to-gateway protocol: binary WebSocket JPEG frames for image data, protobuf/gRPC for downstream inference, and lightweight JSON for detection results and latency telemetry.
- Added per-frame latency profiling across TCP/WebSocket parsing, gRPC send/receive, remote inference, and gateway post-processing; achieved roughly 50 ms end-to-end latency in a remote inference demo.
- Implemented backpressure with per-connection in-flight request limits to avoid stale video-frame queues and preserve real-time behavior under slow inference.
- Hardened network I/O paths with ET read/write drain loops, length-prefixed TCP framing, WebSocket frame parsing, fragmented frame aggregation, and graceful output-buffer draining.

## Interview Storyline

Use this sequence when explaining the project:

1. Problem:
   Real-time video inference needs low latency and predictable behavior. The gateway should avoid blocking on AI inference and should not build up stale frames.

2. Architecture:
   Browser extracts JPEG frames and sends them as binary WebSocket frames. C++ gateway parses frames in the EventLoop thread, submits async gRPC requests to the YOLO service, and returns detection results to the browser.

3. Low-latency decisions:
   Frame data stays binary. JSON is only used for small result metadata. Async gRPC prevents the Reactor from blocking. Per-connection in-flight limits provide backpressure instead of queueing unlimited frames.

4. Safety decisions:
   Connection objects are managed by shared_ptr and Channel::tie during event callbacks. Async gRPC stores only weak_ptr<Connection>, so late AI responses are dropped if the client has disconnected.

5. Observability:
   Each frame carries a FrameContext with timestamps across parse, gRPC send, remote inference, gRPC receive, and response serialization. This made it clear that the gateway overhead was tiny compared with network and inference time.

## Likely Interview Questions

### Why Reactor instead of one thread per connection?

Reactor keeps I/O readiness handling centralized and avoids per-connection blocking threads. With epoll ET and non-blocking sockets, one EventLoop can handle many connections. Worker threads or gRPC completion threads must not directly mutate Channel/Buffer state; they post work back through queueInLoop/eventfd.

### Why use weak_ptr for gRPC callbacks?

The AI response can arrive after the client disconnects. Holding shared_ptr in the RPC call would extend the connection lifetime artificially. Holding a raw pointer risks use-after-free. weak_ptr lets the callback check whether the connection is still alive and discard stale results safely.

### Why JSON if the goal is low latency?

The video frame is not JSON. It is binary JPEG over WebSocket and protobuf bytes over gRPC. JSON is only used for small detection results and latency fields, usually far smaller than the image payload. It improves browser integration and debugging. If result payload cost becomes visible, ResponseSerializer can be replaced with protobuf binary.

### How do you prevent video latency from growing over time?

By limiting per-connection in-flight requests. If YOLO is slower than frame production, the gateway returns OVERLOADED instead of queueing unlimited frames. This sacrifices frame rate to preserve freshness and low latency.

### What happens on half packets and sticky packets?

Raw TCP uses a 4-byte big-endian length prefix and Buffer keeps unread data until a full frame is available. WebSocket parsing also waits until the complete frame is buffered. The EventLoop owns Buffer consumption, avoiding cross-thread races.

### Why did you add WebSocket fragmentation support?

Browsers may split larger messages into fragmented WebSocket frames. Without aggregation, the gateway could submit partial JPEG bytes to YOLO, causing decode failures. The gateway now buffers continuation frames until FIN before submitting the complete image.

### What does the latency breakdown mean?

- total_us: gateway-observed full path for one frame.
- gateway_us: parse plus local post-processing overhead.
- grpc_round_trip_us: C++ to Python service round trip.
- infer_us: Python YOLO pure inference time reported by the service.
- grpc_transport_us: approximate gRPC/network/serialization cost after subtracting infer_us.

## Things Worth Improving Later

### High value

- Add a formal load test: multiple WebSocket clients, controlled FPS, p50/p95/p99 latency, drop/overload rate, CPU and memory usage.
- Add adaptive client-side FPS: automatically tune capture FPS based on OVERLOADED rate and recent total_us.
- Make listen address, listen port, max in-flight frames, JPEG quality, and max frame size configurable.
- Add structured logs with trace_id, frame_id, connection id, and latency fields for easier profiling.
- Add integration tests for WebSocket handshake, masked frames, fragmented frames, and close frames.

### Medium value

- Switch result messages from JSON to protobuf binary if result payload overhead becomes measurable.
- Add a latest-frame policy: when overloaded, drop older pending frames and keep only the newest frame.
- Add TLS or reverse proxy support if the demo needs to run outside localhost.
- Add health-check and readiness checks for the downstream AI service.
- Add graceful shutdown for active gRPC calls and client connections.

### Lower priority

- Add multi-Reactor or multi-loop support. Only do this after load tests show one EventLoop is the bottleneck.
- Explore H.264 Annex B direct stream input. For this demo, JPEG frame extraction is simpler and easier to validate.
- Add object tracking across frames. This improves visual stability but is separate from gateway latency work.

## Caveats to Be Honest About

- The current demo proves the low-latency path, but high concurrency still needs measured load testing.
- YOLO labels may be semantically wrong for game footage because the base model is trained on general objects.
- Current latency numbers depend on route quality, proxy settings, remote GPU load, JPEG quality, and frame size.
- The gateway is currently single-Reactor. This is fine for the demo, but scaling claims should be backed by benchmark data before putting them on a resume.

## Suggested Resume Project Title

VisionReactor: C++ Reactor-based low-latency AI video inference gateway

## One-minute Pitch

I built a low-latency AI vision gateway in C++17. The browser extracts video frames as JPEG and sends them over binary WebSocket to a non-blocking epoll Reactor. The gateway submits each frame to a remote YOLOv8 service through async gRPC and returns detection boxes plus latency telemetry to the browser for real-time rendering. I focused on avoiding stale-frame queues: frame data stays binary, gRPC is asynchronous, connection lifetimes are protected with weak_ptr, and each connection has bounded in-flight requests. In the demo, end-to-end latency stayed around 50 ms, while gateway overhead was sub-millisecond; most time came from network plus remote inference.
