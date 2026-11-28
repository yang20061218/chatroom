#!/usr/bin/env python3
"""
聊天室登录和广播负载测试。

默认：
    100 个客户端登录
    前 10 个客户端发送消息
    每个发送者发送 5 条
    统计所有客户端收到的 CMD_CHAT 帧

用法：
    python3 test_load.py

服务端需要在 127.0.0.1:7777 运行。
"""

import socket
import struct
import sys
import threading
import time

HOST = "127.0.0.1"
PORT = 7777

MAGIC = b"1218"
VERSION = 1
HEAD_LEN = 11

CMD_CHAT = 1
CMD_LOGIN = 4
CMD_LOGIN_RES = 5

CLIENT_COUNT = 100
SENDER_COUNT = 10
MESSAGES_PER_SENDER = 5
MESSAGE_INTERVAL = 0.3
SETTLE_TIME = 2.0


def pack(cmd, body):
    if isinstance(body, str):
        body = body.encode("utf-8")
    return MAGIC + bytes([VERSION]) + struct.pack("!HI", cmd, len(body)) + body


def recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed")
        data += chunk
    return data


def recv_frame(sock):
    header = recv_exact(sock, HEAD_LEN)
    if header[:4] != MAGIC:
        raise ValueError("bad magic")
    if header[4] != VERSION:
        raise ValueError("bad version")
    cmd, length = struct.unpack("!HI", header[5:11])
    return cmd, recv_exact(sock, length)


class LoadClient:
    def __init__(self, index):
        self.index = index
        self.name = f"load{index:03d}"
        self.sock = socket.create_connection((HOST, PORT), timeout=5)
        self.sock.sendall(pack(CMD_LOGIN, self.name))
        cmd, body = recv_frame(self.sock)
        if cmd != CMD_LOGIN_RES or body == b"no":
            self.sock.close()
            raise RuntimeError(f"{self.name} login failed: {body!r}")

        self.sock.settimeout(None)
        self.chat_frames = 0
        self.errors = []
        self.lock = threading.Lock()
        self.stopped = False
        self.thread = threading.Thread(target=self.reader, daemon=True)
        self.thread.start()

    def reader(self):
        try:
            while not self.stopped:
                cmd, _ = recv_frame(self.sock)
                if cmd == CMD_CHAT:
                    with self.lock:
                        self.chat_frames += 1
        except (ConnectionError, OSError, ValueError) as exc:
            if not self.stopped:
                with self.lock:
                    self.errors.append(str(exc))

    def send_chat(self, text):
        try:
            self.sock.sendall(pack(CMD_CHAT, text))
        except OSError as exc:
            with self.lock:
                self.errors.append(str(exc))

    def close(self):
        self.stopped = True
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()
        self.thread.join(timeout=1)


def main():
    if SENDER_COUNT > CLIENT_COUNT:
        print("SENDER_COUNT must not exceed CLIENT_COUNT")
        return 2

    clients = []
    try:
        print(f"Connecting and logging in {CLIENT_COUNT} clients...")
        for index in range(CLIENT_COUNT):
            clients.append(LoadClient(index))
            if (index + 1) % 10 == 0:
                print(f"  logged in: {index + 1}")

        senders = clients[:SENDER_COUNT]
        total_messages = SENDER_COUNT * MESSAGES_PER_SENDER
        expected_frames = total_messages * (CLIENT_COUNT - 1)

        print(f"Sending {total_messages} chat messages...")
        for round_index in range(MESSAGES_PER_SENDER):
            for sender in senders:
                sender.send_chat(
                    f"load-message-{round_index}-{sender.index}"
                )
            time.sleep(MESSAGE_INTERVAL)

        print("Waiting for broadcasts to settle...")
        time.sleep(SETTLE_TIME)

        with_summary = []
        for client in clients:
            with client.lock:
                with_summary.append(
                    (client.name, client.chat_frames, list(client.errors))
                )

        total_received = sum(item[1] for item in with_summary)
        error_items = [item for item in with_summary if item[2]]

        print()
        print(f"logged_in={len(with_summary)}")
        print(f"sent_chat_messages={total_messages}")
        print(f"expected_received_frames={expected_frames}")
        print(f"actual_received_frames={total_received}")
        print(f"clients_with_errors={len(error_items)}")

        if error_items:
            print("\nFirst client errors:")
            for name, frames, errors in error_items[:10]:
                print(f"  {name}: frames={frames}, errors={errors}")
            return 1

        if total_received != expected_frames:
            print("\nFrame count mismatch.")
            print("Some recipients may have missed messages or were not read fast enough.")
            return 1

        print("\nLoad test passed.")
        return 0
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"Load test failed: {exc}")
        return 1
    finally:
        for client in clients:
            client.close()


if __name__ == "__main__":
    sys.exit(main())
