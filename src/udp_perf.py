#!/usr/bin/env python3
"""
udp_perf.py - PC side of the Lab 8 windowed UDP throughput test (v8.7.0).

Speaks the same little protocol as board A's `w` command and the echo nodes:
  WSTART            reset the receiver window        (reply: WOK)
  WD <seq:4><us:4>  one-way data packet (big-endian)
  WEND              end the window                   (reply: WREPORT ...)
  WREPORT rx_pkts=.. rx_bytes=.. dur_us=.. maxseq=.. jitter_us=.. ooo=..

Two modes:
  --recv            act as the receiver/sink (measure ESP -> PC): the ESP sends
                    `w <this-pc-ip>` to this script.
  --send --target IP  act as the sender (measure PC -> ESP echo node, i.e. the
                    node's receive ceiling): this script blasts to an echo node.

Metrics: throughput (Mbit/s), packet loss (from sequence numbers) and RFC 3550
inter-arrival jitter. Pure standard library.

Examples:
  python udp_perf.py --recv --port 5001
  python udp_perf.py --send --target 192.168.32.51 --port 5001 --size 1460 --count 10000
"""
import argparse, socket, struct, time

MAGIC = b"WD"

def now_us():
    return int(time.perf_counter() * 1_000_000) & 0xFFFFFFFF

def s32(x):                       # interpret low 32 bits as signed
    x &= 0xFFFFFFFF
    return x - 0x100000000 if x >= 0x80000000 else x

def receiver(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", port))
    print(f"udp_perf receiver listening on UDP {port} (Ctrl-C to stop)")
    reset = lambda: dict(pkts=0, bytes=0, first=0, last=0, maxseq=-1,
                         prev_seq=None, prev_transit=None, jitter=0.0, ooo=0)
    st = reset()
    while True:
        data, addr = sock.recvfrom(2048)
        if data[:6] == b"WSTART":
            st = reset()
            sock.sendto(b"WOK", addr)
        elif data[:4] == b"WEND":
            dur = st["last"] - st["first"]
            rep = (f"WREPORT rx_pkts={st['pkts']} rx_bytes={st['bytes']} "
                   f"dur_us={dur} maxseq={max(st['maxseq'],0)} "
                   f"jitter_us={st['jitter']:.1f} ooo={st['ooo']}")
            sock.sendto(rep.encode(), addr)
            _summary("ESP->PC (this sink)", st, dur)
        elif data[:2] == MAGIC and len(data) >= 10:
            t = now_us()
            seq, su = struct.unpack(">II", data[2:10])
            if st["pkts"] == 0:
                st["first"] = t
            st["last"] = t
            st["pkts"] += 1
            st["bytes"] += len(data)
            st["maxseq"] = max(st["maxseq"], seq)
            transit = s32(t - su)
            if st["prev_seq"] is not None:
                if seq < st["prev_seq"]:
                    st["ooo"] += 1
                d = abs(transit - st["prev_transit"])
                st["jitter"] += (d - st["jitter"]) / 16.0
            st["prev_seq"] = seq
            st["prev_transit"] = transit

def _summary(label, st, dur_us):
    expected = max(st["maxseq"], 0) + 1 if st["pkts"] else 0
    lost = max(expected - st["pkts"], 0)
    loss = 100.0 * lost / expected if expected else 0.0
    mbps = st["bytes"] * 8.0 / dur_us if dur_us else 0.0
    print(f"[{label}] rx_pkts={st['pkts']} bytes={st['bytes']} dur={dur_us/1e6:.3f}s "
          f"throughput={mbps:.2f} Mbit/s loss={lost}/{expected} ({loss:.2f}%) "
          f"jitter={st['jitter']/1000:.3f} ms ooo={st['ooo']}")

def sender(target, port, size, count):
    size = max(10, min(size, 1460))
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.5)
    filler = bytes(size - 10)
    # handshake
    sock.sendto(b"WSTART", (target, port))
    try:
        sock.recvfrom(64)                 # WOK (best effort)
    except socket.timeout:
        pass
    t0 = time.perf_counter()
    for seq in range(count):
        pkt = MAGIC + struct.pack(">II", seq, now_us()) + filler
        sock.sendto(pkt, (target, port))
    tx_us = (time.perf_counter() - t0) * 1e6
    tx_mbps = count * size * 8.0 / tx_us if tx_us else 0.0
    # ask for the report
    rep = None
    for _ in range(6):
        sock.sendto(b"WEND", (target, port))
        try:
            data, _ = sock.recvfrom(256)
            if data.startswith(b"WREPORT"):
                rep = data.decode(errors="ignore")
                break
        except socket.timeout:
            continue
    print(f"[PC->{target} TX] sent={count} size={size} tx={tx_mbps:.2f} Mbit/s")
    if not rep:
        print("  no WREPORT from target (node may not support the w protocol)")
        return
    f = dict(tok.split("=") for tok in rep.split()[1:])
    rx_pkts = int(f["rx_pkts"]); rx_bytes = int(f["rx_bytes"])
    dur = int(f["dur_us"]); maxseq = int(f["maxseq"]); jit = float(f["jitter_us"]); ooo = int(f["ooo"])
    expected = maxseq + 1
    lost = max(expected - rx_pkts, 0)
    loss = 100.0 * lost / expected if expected else 0.0
    rx_mbps = rx_bytes * 8.0 / dur if dur else 0.0
    print(f"[PC->{target} RX] rx_pkts={rx_pkts} throughput={rx_mbps:.2f} Mbit/s "
          f"loss={lost}/{expected} ({loss:.2f}%) jitter={jit/1000:.3f} ms ooo={ooo}")

def main():
    ap = argparse.ArgumentParser(description="Lab 8 windowed UDP perf test (PC side)")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--recv", action="store_true", help="receive/sink mode (measure ESP->PC)")
    g.add_argument("--send", action="store_true", help="send mode (measure PC->ESP echo node)")
    ap.add_argument("--target", help="target IP for --send")
    ap.add_argument("--port", type=int, default=5001)
    ap.add_argument("--size", type=int, default=1460)
    ap.add_argument("--count", type=int, default=10000)
    a = ap.parse_args()
    if a.recv:
        receiver(a.port)
    else:
        if not a.target:
            ap.error("--send requires --target")
        sender(a.target, a.port, a.size, a.count)

if __name__ == "__main__":
    main()
