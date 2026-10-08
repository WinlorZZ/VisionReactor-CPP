#!/usr/bin/env python3
"""VisionReactor 端到端压测客户端（快速开始里的实验三、实验四用）

协议：[4 字节大端 body_length][JPEG/PNG bytes] -> [4 字节大端 body_length][UTF-8 JSON]

两种模式：
  seq   逐帧同步：发一帧等一帧，测单帧链路延迟分布
  burst 流水线：一次性连发 N 帧再收，用于触发每连接在途上限与 OVERLOADED 背压
"""
import argparse
import json
import socket
import statistics
import struct
import sys
import time


def recv_exactly(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("连接被对端关闭")
        buf += chunk
    return buf


def recv_json(sock):
    header = recv_exactly(sock, 4)
    (body_len,) = struct.unpack(">I", header)
    body = recv_exactly(sock, body_len)
    return json.loads(body.decode("utf-8"))


def send_frame(sock, payload):
    sock.sendall(struct.pack(">I", len(payload)) + payload)


def pct(values, p):
    if not values:
        return None
    values = sorted(values)
    idx = min(len(values) - 1, int(round((p / 100.0) * (len(values) - 1))))
    return values[idx]


def summarize(responses, wall_ms):
    ok = [r for r in responses if r.get("ok")]
    bad = [r for r in responses if not r.get("ok")]
    stats = {}
    if ok:
        for field in ("total_us", "gateway_us", "parse_us", "queue_to_grpc_us",
                      "grpc_round_trip_us", "grpc_transport_us", "infer_us",
                      "postprocess_us"):
            vals = [r["timing"][field] for r in ok
                    if "timing" in r and field in r["timing"]]
            if vals:
                stats[field] = {
                    "min": min(vals),
                    "median": int(statistics.median(vals)),
                    "mean": int(statistics.mean(vals)),
                    "p95": pct(vals, 95),
                    "max": max(vals),
                }
    err_codes = {}
    for r in bad:
        code = r.get("code", "UNKNOWN")
        err_codes[code] = err_codes.get(code, 0) + 1
    return {
        "sent": len(responses),
        "ok": len(ok),
        "failed": len(bad),
        "error_codes": err_codes,
        "wall_ms": round(wall_ms, 1),
        "fps": round(len(ok) / (wall_ms / 1000.0), 1) if wall_ms > 0 else None,
        "detected_objects_total": sum(len(r.get("detections", [])) for r in ok),
        "timing_us": stats,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8888)
    ap.add_argument("--image", required=True)
    ap.add_argument("--frames", type=int, default=20)
    ap.add_argument("--mode", choices=["seq", "burst"], default="seq")
    ap.add_argument("--out", default=None, help="汇总 JSON 输出路径")
    args = ap.parse_args()

    with open(args.image, "rb") as f:
        payload = f.read()

    responses = []
    t0 = time.perf_counter()
    with socket.create_connection((args.host, args.port), timeout=30) as sock:
        if args.mode == "seq":
            for _ in range(args.frames):
                send_frame(sock, payload)
                responses.append(recv_json(sock))
        else:
            for _ in range(args.frames):
                send_frame(sock, payload)
            for _ in range(args.frames):
                responses.append(recv_json(sock))
    wall_ms = (time.perf_counter() - t0) * 1000.0

    result = summarize(responses, wall_ms)
    result["mode"] = args.mode
    result["image_bytes"] = len(payload)
    result["image"] = args.image
    sample = next((r for r in responses if r.get("ok")), None)
    if sample:
        result["sample_detections"] = sample.get("detections", [])[:5]

    print(json.dumps(result, ensure_ascii=False, indent=2))
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
