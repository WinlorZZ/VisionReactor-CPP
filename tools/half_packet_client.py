#!/usr/bin/env python3
"""实验一用：把一帧拆成「长度头」和「body」两次发送，中间插入延迟。

用途：证明半包时 FrameContext 跨读保留、读游标不动 —— 表现是 parse_us 暴涨。
只用标准库，不依赖 grpcio。

用法：
  python3 half_packet_client.py --image testdata/test.jpg              # 拆成两次，中间 sleep 0.5s
  python3 half_packet_client.py --image testdata/test.jpg --gap 0      # 对照组：一次发完
  python3 half_packet_client.py --image testdata/test.jpg --gap 0.2 --port 8888
"""
import argparse
import json
import socket
import struct
import sys
import time


def recv_exact(sock, n):
    buf = b''
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError('对端在收到完整回复前关闭了连接')
        buf += chunk
    return buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--image', required=True)
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=8888)
    ap.add_argument('--gap', type=float, default=0.5,
                    help='长度头与 body 之间的间隔秒数；0 表示一次发完（对照组）')
    ap.add_argument('--frames', type=int, default=1)
    ap.add_argument('--timeout', type=float, default=30.0)
    args = ap.parse_args()

    with open(args.image, 'rb') as f:
        jpeg = f.read()
    header = struct.pack('>I', len(jpeg))

    print(f'[E1] 目标 {args.host}:{args.port} ｜ 图像 {len(jpeg)} 字节 ｜ '
          f'间隔 {args.gap*1000:.0f} ms ｜ 帧数 {args.frames}')

    with socket.create_connection((args.host, args.port), timeout=args.timeout) as s:
        for i in range(args.frames):
            t0 = time.perf_counter()
            s.sendall(header)                 # ① 先只发 4 字节长度头
            if args.gap > 0:
                time.sleep(args.gap)          # ② 故意断开，制造半包
            s.sendall(jpeg)                   # ③ 再发 body

            length = struct.unpack('>I', recv_exact(s, 4))[0]
            body = recv_exact(s, length)
            wall_ms = (time.perf_counter() - t0) * 1000

            try:
                r = json.loads(body.decode('utf-8'))
            except Exception:
                print(f'  [帧{i+1}] 回复不是 JSON: {body[:120]!r}')
                continue

            timing = r.get('timing', {})
            print(f'  [帧{i+1}] ok={r.get("ok")} code={r.get("code","")} '
                  f'wall={wall_ms:.1f}ms')
            print(f'        parse_us={timing.get("parse_us")}  '
                  f'queue_to_grpc_us={timing.get("queue_to_grpc_us")}  '
                  f'postprocess_us={timing.get("postprocess_us")}  '
                  f'gateway_us={timing.get("gateway_us")}  '
                  f'total_us={timing.get("total_us")}')
            if r.get('error'):
                print(f'        error={r["error"]}')

    print('\n[E1] 判读：')
    print('  gap=0.5 时 parse_us 应约为 500000（微秒），即把等待时间算进了 T1；')
    print('  gap=0 时 parse_us 应只有几微秒到几十微秒。')
    print('  两者若都正常返回 ok=true，说明半包时读游标没动、FrameContext 跨读保留。')


if __name__ == '__main__':
    sys.exit(main())
