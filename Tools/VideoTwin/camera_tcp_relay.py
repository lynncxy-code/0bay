"""Local RTSP/TCP relay pinned to the camera-facing Windows interface.

Only IP addresses and ports are configured here; camera credentials stay in MediaMTX.
"""
import argparse
import concurrent.futures
import json
import select
import socket
import struct


def forward(client, route, interface_index, source_ip):
    try:
        with client, socket.socket(socket.AF_INET, socket.SOCK_STREAM) as upstream:
            upstream.settimeout(8)
            upstream.setsockopt(socket.IPPROTO_IP, 31, struct.pack("!I", interface_index))
            upstream.bind((source_ip, 0))
            upstream.connect((route["host"], route["port"]))
            upstream.settimeout(30)
            client.settimeout(30)
            while True:
                readable, _, _ = select.select([client, upstream], [], [], 30)
                if not readable:
                    break
                for source in readable:
                    data = source.recv(65536)
                    if not data:
                        return
                    (upstream if source is client else client).sendall(data)
    except OSError as exc:
        print(f"{route['host']}: {type(exc).__name__}", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--routes", required=True)
    parser.add_argument("--interface-index", required=True, type=int)
    parser.add_argument("--source-ip", required=True)
    parser.add_argument("--workers", type=int, default=24)
    args = parser.parse_args()
    with open(args.routes, encoding="utf-8-sig") as source:
        routes = json.load(source)
    listeners = []
    try:
        for route in routes:
            listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            listener.bind(("127.0.0.1", route["local_port"]))
            listener.listen(8)
            listeners.append((listener, route))
        print(f"Ready: {len(listeners)} local camera relays on interface {args.interface_index}", flush=True)
        mapping = dict(listeners)
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.workers, 128))) as pool:
            while True:
                ready, _, _ = select.select(list(mapping), [], [])
                for listener in ready:
                    client, _ = listener.accept()
                    pool.submit(forward, client, mapping[listener], args.interface_index, args.source_ip)
    finally:
        for listener, _ in listeners:
            listener.close()


if __name__ == "__main__":
    main()
