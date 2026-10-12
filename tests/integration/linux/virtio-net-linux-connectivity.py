#!/usr/bin/env python3

import os
import selectors
import socket
import struct
import subprocess
import sys
import time


GUEST_IP = b"\x0a\x00\x00\x02"
HOST_IP = b"\x0a\x00\x00\x01"
HOST_MAC = b"\x02\x00\x00\x00\x00\x01"


def checksum(data):
    if len(data) & 1:
        data += b"\0"
    value = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    value = (value & 0xffff) + (value >> 16)
    value = (value & 0xffff) + (value >> 16)
    return (~value) & 0xffff


def make_arp_reply(frame):
    if len(frame) < 42 or frame[12:14] != b"\x08\x06":
        return None
    if frame[20:22] != b"\0\x01" or frame[38:42] != HOST_IP:
        return None
    guest_mac = frame[6:12]
    guest_ip = frame[28:32]
    payload = struct.pack(
        "!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 2, HOST_MAC, HOST_IP,
        guest_mac, guest_ip
    )
    return guest_mac + HOST_MAC + b"\x08\x06" + payload


def make_icmp_reply(frame):
    if len(frame) < 42 or frame[12:14] != b"\x08\x00":
        return None
    ip_offset = 14
    ip_header_length = (frame[ip_offset] & 0x0f) * 4
    if (len(frame) < ip_offset + ip_header_length + 8 or
            frame[ip_offset + 9] != 1 or
            frame[ip_offset + 16:ip_offset + 20] != HOST_IP):
        return None
    icmp_offset = ip_offset + ip_header_length
    if frame[icmp_offset] != 8 or frame[icmp_offset + 1] != 0:
        return None
    result = bytearray(frame)
    result[0:6] = frame[6:12]
    result[6:12] = HOST_MAC
    result[ip_offset + 12:ip_offset + 16] = HOST_IP
    result[ip_offset + 16:ip_offset + 20] = frame[ip_offset + 12:ip_offset + 16]
    result[ip_offset + 10:ip_offset + 12] = b"\0\0"
    result[ip_offset + 10:ip_offset + 12] = struct.pack(
        "!H", checksum(result[ip_offset:ip_offset + ip_header_length])
    )
    result[icmp_offset] = 0
    result[icmp_offset + 2:icmp_offset + 4] = b"\0\0"
    result[icmp_offset + 2:icmp_offset + 4] = struct.pack(
        "!H", checksum(result[icmp_offset:])
    )
    return bytes(result)


def main():
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} VART IMAGE INITRD", file=sys.stderr)
        return 2

    vart, image, initrd = sys.argv[1:]
    guest_backend, host_backend = socket.socketpair(
        socket.AF_UNIX, socket.SOCK_DGRAM
    )
    os.dup2(guest_backend.fileno(), 3)
    if guest_backend.fileno() != 3:
        guest_backend.close()
    command = [
        vart,
        "--kernel", image,
        "--initrd", initrd,
        "--memory", "512M",
        "--cpus", "1",
        "--virtio-net-fd", "3",
        "--append",
        "earlycon=uart8250,mmio,0x10000000 console=ttyS0 loglevel=7",
    ]
    process = subprocess.Popen(
        command,
        pass_fds=(3,),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    os.set_blocking(process.stdout.fileno(), False)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ, "console")
    selector.register(host_backend, selectors.EVENT_READ, "backend")
    output = ""
    command_sent = False
    tx_frames = 0
    deadline = time.monotonic() + 15

    while time.monotonic() < deadline:
        for key, _ in selector.select(0.1):
            if key.data == "console":
                try:
                    data = os.read(process.stdout.fileno(), 4096)
                except BlockingIOError:
                    continue
                if not data:
                    selector.unregister(process.stdout)
                    continue
                output += data.decode(errors="replace")
                if not command_sent and "~ # " in output:
                    process.stdin.write(
                        "ip link set eth0 up; "
                        "ip addr add 10.0.0.2/24 dev eth0; "
                        "ping -c 1 -W 1 10.0.0.1; "
                        "echo VART_NET_OK; poweroff -f\n"
                    )
                    process.stdin.flush()
                    command_sent = True
            else:
                try:
                    frame = host_backend.recv(65535)
                except OSError:
                    frame = b""
                if frame:
                    tx_frames += 1
                    reply = make_arp_reply(frame) or make_icmp_reply(frame)
                    if reply is not None:
                        host_backend.send(reply)
        if process.poll() is not None:
            break

    if process.poll() is None:
        process.kill()
    process.wait()
    selector.close()
    host_backend.close()
    if (process.returncode != 0 or not command_sent or tx_frames < 2 or
            "VART_NET_OK" not in output or
            "1 packets received" not in output):
        print(output, end="", file=sys.stderr)
        print(
            "not ok - Linux virtio-net connectivity "
            f"(status={process.returncode}, tx_frames={tx_frames})",
            file=sys.stderr,
        )
        return 1
    print(f"ok - Linux virtio-net connectivity ({tx_frames} TX frames)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
