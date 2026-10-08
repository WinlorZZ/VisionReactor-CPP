const video = document.querySelector("#video");
const overlay = document.querySelector("#overlay");
const overlayCtx = overlay.getContext("2d");
const capture = document.querySelector("#capture");
const captureCtx = capture.getContext("2d", { willReadFrequently: false });

const fileInput = document.querySelector("#file");
const endpointInput = document.querySelector("#endpoint");
const connectButton = document.querySelector("#connect");
const playButton = document.querySelector("#play");
const pauseButton = document.querySelector("#pause");
const fpsInput = document.querySelector("#fps");
const fpsValue = document.querySelector("#fpsValue");
const statusEl = document.querySelector("#status");
const messageEl = document.querySelector("#message");

const metrics = {
  frame: document.querySelector("#mFrame"),
  total: document.querySelector("#mTotal"),
  gateway: document.querySelector("#mGateway"),
  grpc: document.querySelector("#mGrpc"),
  infer: document.querySelector("#mInfer"),
  objects: document.querySelector("#mObjects"),
  inflight: document.querySelector("#mInflight"),
};

let socket = null;
let sendTimer = 0;
let lastSendAt = 0;
let inFlight = 0;
let latestDetections = [];
let latestTiming = null;

function setStatus(text, state = "") {
  statusEl.textContent = text;
  statusEl.className = `status ${state}`.trim();
}

function setMessage(text) {
  messageEl.textContent = text;
}

function fmtUs(value) {
  if (typeof value !== "number") return "-";
  return `${(value / 1000).toFixed(2)} ms`;
}

function updateInflight() {
  metrics.inflight.textContent = String(inFlight);
}

function resizeCanvases() {
  const w = video.videoWidth || 1280;
  const h = video.videoHeight || 720;
  overlay.width = w;
  overlay.height = h;
  capture.width = w;
  capture.height = h;
  drawDetections();
}

function drawDetections() {
  overlayCtx.clearRect(0, 0, overlay.width, overlay.height);
  overlayCtx.lineWidth = Math.max(2, overlay.width / 480);
  overlayCtx.font = `${Math.max(14, overlay.width / 70)}px system-ui`;
  overlayCtx.textBaseline = "top";

  for (const det of latestDetections) {
    const x = det.x - det.w / 2;
    const y = det.y - det.h / 2;
    overlayCtx.strokeStyle = "#8fd14f";
    overlayCtx.fillStyle = "rgba(20, 24, 18, 0.82)";
    overlayCtx.strokeRect(x, y, det.w, det.h);

    const label = `${det.class} ${(det.confidence * 100).toFixed(0)}%`;
    const labelWidth = overlayCtx.measureText(label).width + 12;
    const labelY = Math.max(0, y - 24);
    overlayCtx.fillRect(x, labelY, labelWidth, 22);
    overlayCtx.fillStyle = "#f4f7ef";
    overlayCtx.fillText(label, x + 6, labelY + 3);
  }
}

function updateMetrics(payload) {
  latestTiming = payload.timing || {};
  latestDetections = payload.detections || [];
  metrics.frame.textContent = payload.frame_id ?? "-";
  metrics.total.textContent = fmtUs(latestTiming.total_us);
  metrics.gateway.textContent = fmtUs(latestTiming.gateway_us);
  metrics.grpc.textContent = fmtUs(latestTiming.grpc_round_trip_us);
  metrics.infer.textContent = fmtUs(latestTiming.infer_us);
  metrics.objects.textContent = String(latestDetections.length);
  drawDetections();
}

function connect() {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.close();
    return;
  }

  socket = new WebSocket(endpointInput.value);
  socket.binaryType = "arraybuffer";
  setStatus("connecting", "warn");
  setMessage("");

  socket.addEventListener("open", () => {
    setStatus("online", "ready");
    connectButton.textContent = "断开";
  });

  socket.addEventListener("close", () => {
    setStatus("offline");
    connectButton.textContent = "连接";
    inFlight = 0;
    updateInflight();
  });

  socket.addEventListener("error", () => {
    setStatus("error", "bad");
    setMessage("WebSocket 连接失败");
  });

  socket.addEventListener("message", (event) => {
    inFlight = Math.max(0, inFlight - 1);
    updateInflight();
    const payload = JSON.parse(event.data);
    if (!payload.ok) {
      setMessage(payload.error || payload.code || "request failed");
      if (payload.code === "OVERLOADED") {
        fpsInput.value = String(Math.max(1, Number(fpsInput.value) - 1));
        fpsValue.textContent = fpsInput.value;
      }
      return;
    }
    setMessage("");
    updateMetrics(payload);
  });
}

function canSendFrame() {
  return socket &&
    socket.readyState === WebSocket.OPEN &&
    !video.paused &&
    !video.ended &&
    video.videoWidth > 0 &&
    inFlight < 2;
}

function sendFrame() {
  if (!canSendFrame()) return;
  const now = performance.now();
  const minInterval = 1000 / Number(fpsInput.value);
  if (now - lastSendAt < minInterval) return;
  lastSendAt = now;

  captureCtx.drawImage(video, 0, 0, capture.width, capture.height);
  capture.toBlob((blob) => {
    if (!blob || !canSendFrame()) return;
    socket.send(blob);
    inFlight += 1;
    updateInflight();
  }, "image/jpeg", 0.72);
}

function startSender() {
  cancelAnimationFrame(sendTimer);
  const tick = () => {
    sendFrame();
    sendTimer = requestAnimationFrame(tick);
  };
  sendTimer = requestAnimationFrame(tick);
}

fileInput.addEventListener("change", () => {
  const file = fileInput.files[0];
  if (!file) return;
  video.src = URL.createObjectURL(file);
  playButton.disabled = false;
  pauseButton.disabled = false;
  latestDetections = [];
  setMessage("");
});

video.addEventListener("loadedmetadata", resizeCanvases);
window.addEventListener("resize", drawDetections);

connectButton.addEventListener("click", connect);
playButton.addEventListener("click", async () => {
  await video.play();
  startSender();
});
pauseButton.addEventListener("click", () => {
  video.pause();
});

fpsInput.addEventListener("input", () => {
  fpsValue.textContent = fpsInput.value;
});

setStatus("offline");
updateInflight();
