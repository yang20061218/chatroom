#!/usr/bin/env python3
"""
慢客户端背压测试。

场景：
    1. slow客户端登录后不读取任何广播
    2. receiver客户端正常读取
    3. sender客户端发送大量接近服务器上限的消息
    4. 最后新客户端仍能登录，证明服务端没有阻塞

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

MESSAGE_COUNT = 600
PAYLOAD_SIZE = 4000
SEND_INTERVAL = 0.002
RECEIVE_TIMEOUT = 15.0
RUN_ID = f"{int(time.time()) % 1000000:06d}"
MIN_RECEIVER_FRAMES = 1


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


def login(name):
    sock = socket.create_connection((HOST, PORT), timeout=5)
    sock.sendall(pack(CMD_LOGIN, name))
    cmd, body = recv_frame(sock)
    if cmd != CMD_LOGIN_RES or body == b"no":
        sock.close()
        raise RuntimeError(f"{name} login failed: {body!r}")
    sock.settimeout(None)
    return sock


class Receiver:
    def __init__(self, name):
        self.sock = login(name)
        self.count = 0
        self.errors = []
        self.stopped = False
        self.thread = threading.Thread(target=self.read_loop, daemon=True)
        self.thread.start()

    def read_loop(self):
        try:
            while not self.stopped:
                cmd, _ = recv_frame(self.sock)
                if cmd == CMD_CHAT:
                    self.count += 1
        except (ConnectionError, OSError, ValueError) as exc:
            if not self.stopped:
                self.errors.append(str(exc))

    def close(self):
        self.stopped = True
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()
        self.thread.join(timeout=1)


def wait_for_count(receiver, expected, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if receiver.count >= expected:
            return True
        if receiver.errors:
            return False
        time.sleep(0.05)
    return receiver.count >= expected


def main():
    slow = None
    sender = None
    receiver = None
    probe = None
    stage = "initialization"

    try:
        stage = "slow client login"
        print("Logging in clients...")
        slow = login(f"slow-{RUN_ID}")
        stage = "receiver login"
        receiver = Receiver(f"recv-{RUN_ID}")
        stage = "sender login"
        sender = login(f"send-{RUN_ID}")
        sender.settimeout(5)

        # slow登录成功后就停止读取，服务端发送缓冲区最终会进入背压状态。
        print(f"Sending {MESSAGE_COUNT} messages of {PAYLOAD_SIZE} bytes...")
        payload = "s" * PAYLOAD_SIZE
        for index in range(MESSAGE_COUNT):
            stage = f"sender send frame {index}"
            sender.sendall(pack(CMD_CHAT, f"{index:04d}-{payload}"))
            time.sleep(SEND_INTERVAL)

        stage = "wait for receiver"
        print("Waiting for the active receiver...")
        received_some = wait_for_count(receiver, MIN_RECEIVER_FRAMES, RECEIVE_TIMEOUT)

        # 慢客户端退出后，服务端仍应能接受新的登录连接。
        stage = "probe login"
        probe = login(f"probe-{RUN_ID}")

        print()
        print(f"sent_messages={MESSAGE_COUNT}")
        print(f"receiver_frames={receiver.count}")
        print(f"receiver_errors={receiver.errors}")
        print(f"probe_login=ok")
        if receiver.count < MESSAGE_COUNT:
            print("backpressure_drops=yes")

        if not received_some:
            print("Slow-client test failed: active receiver received no messages")
            return 1
        if receiver.errors:
            print("Slow-client test failed: active receiver reported an error")
            return 1

        print("Slow-client test passed: server remained responsive")
        if receiver.count < MESSAGE_COUNT:
            print("Note: messages were dropped because the fixed write buffer filled.")
        return 0
    except (ConnectionError, OSError, RuntimeError, ValueError) as exc:
        print(f"Slow-client test failed at {stage}: {exc}")
        return 1
    finally:
        for sock in (probe, sender, slow):
            if sock is not None:
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                sock.close()
        if receiver is not None:
            receiver.close()


if __name__ == "__main__":
    sys.exit(main())
