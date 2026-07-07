#!/usr/bin/env python3
import argparse
import json
import socket
import struct


def recvall(sock, size):
    chunks = []
    remaining = size
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError("connection closed before full response arrived")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def main():
    parser = argparse.ArgumentParser(description="Send one JPEG/PNG frame to VisionReactor TCP Protocol V1.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8888)
    parser.add_argument("--image", required=True)
    args = parser.parse_args()

    with open(args.image, "rb") as f:
        image = f.read()

    request = struct.pack("!I", len(image)) + image
    with socket.create_connection((args.host, args.port), timeout=10) as sock:
        sock.sendall(request)
        response_len = struct.unpack("!I", recvall(sock, 4))[0]
        payload = recvall(sock, response_len)

    result = json.loads(payload.decode("utf-8"))
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
