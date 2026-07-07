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

function getVideoContentRect() {
  const stageRect = video.parentElement.getBoundingClientRect();
  const videoWidth = video.videoWidth || 16;
  const videoHeight = video.videoHeight || 9;
  const stageAspect = stageRect.width / stageRect.height;
  const videoAspect = videoWidth / videoHeight;

  let width = stageRect.width;
  let height = stageRect.height;
  let left = 0;
  let top = 0;

  if (stageAspect > videoAspect) {
    width = height * videoAspect;
    left = (stageRect.width - width) / 2;
  } else {
    height = width / videoAspect;
    top = (stageRect.height - height) / 2;
  }

  return { left, top, width, height };
}

function resizeCanvases() {
  const w = video.videoWidth || 1280;
  const h = video.videoHeight || 720;
  overlay.width = w;
  overlay.height = h;
  capture.width = w;
  capture.height = h;
  positionOverlay();
  drawDetections();
}

function positionOverlay() {
  const rect = getVideoContentRect();
  overlay.style.left = `${rect.left}px`;
  overlay.style.top = `${rect.top}px`;
  overlay.style.width = `${rect.width}px`;
  overlay.style.height = `${rect.height}px`;
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function normalizeDetection(det) {
  const w = Number(det.w ?? det.width ?? 0);
  const h = Number(det.h ?? det.height ?? 0);
  const cx = Number(det.x ?? 0);
  const cy = Number(det.y ?? 0);
  return {
    label: String(det.class ?? det.class_name ?? "object"),
    confidence: Number(det.confidence ?? 0),
    x: cx - w / 2,
    y: cy - h / 2,
    w,
    h,
  };
}

function drawDetections() {
  positionOverlay();
  overlayCtx.clearRect(0, 0, overlay.width, overlay.height);
  overlayCtx.lineWidth = Math.max(2, overlay.width / 480);
  overlayCtx.font = `${Math.max(14, overlay.width / 70)}px system-ui`;
  overlayCtx.textBaseline = "top";

  for (const raw of latestDetections) {
    const det = normalizeDetection(raw);
    if (!Number.isFinite(det.x) || !Number.isFinite(det.y) || det.w <= 0 || det.h <= 0) {
      continue;
    }

    const x = clamp(det.x, 0, overlay.width);
    const y = clamp(det.y, 0, overlay.height);
    const w = clamp(det.w, 0, overlay.width - x);
    const h = clamp(det.h, 0, overlay.height - y);
    if (w <= 0 || h <= 0) continue;

    overlayCtx.strokeStyle = "#8fd14f";
    overlayCtx.fillStyle = "rgba(20, 24, 18, 0.82)";
    overlayCtx.strokeRect(x, y, w, h);

    const label = `${det.label} ${(det.confidence * 100).toFixed(0)}%`;
    const labelWidth = overlayCtx.measureText(label).width + 12;
    const labelY = Math.max(0, y - 24);
    const labelX = clamp(x, 0, Math.max(0, overlay.width - labelWidth));
    overlayCtx.fillRect(labelX, labelY, labelWidth, 22);
    overlayCtx.fillStyle = "#f4f7ef";
    overlayCtx.fillText(label, labelX + 6, labelY + 3);
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
    if (latestDetections.length === 0) {
      setMessage("已收到推理结果，本帧没有检测框");
    }
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
video.addEventListener("playing", resizeCanvases);
window.addEventListener("resize", resizeCanvases);

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
