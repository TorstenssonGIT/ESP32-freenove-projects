---
title: |
  Lab 8 - Wi-Fi performance investigation\
  (why the throughput is low)
author: "Johan Torstensson"
date: "2026-11-26"
---

# 0. Scope

The 8.6 bandwidth test reports a peak of about **2.7–3.1 Mbit/s** (1460-byte packets).
That looks low for Wi-Fi, which is rated for tens to hundreds of Mbit/s. This document
explains **why the number is low, what it actually measures, and how to measure the
link's real capacity** (Bredbandskollen and iperf3). The short answer: the test is
**latency-bound, not link-bound** — it is a round-trip (request/response) test with one
packet in flight, so it measures `2 × size / RTT`, not the channel's throughput.

# 1. What the test measures

For each payload size the controller sends 100 UDP packets to an echo node and waits for
each echo before sending the next. Representative result (one run, boards B and C):

| Size (B) | B kbit/s | B median RTT (ms) | C kbit/s | C median RTT (ms) | Loss |
|---------:|---------:|------------------:|---------:|------------------:|:----:|
| 32   | 84   | 5.10 | 75   | 5.24 | 0 |
| 64   | 171  | 5.16 | 163  | 5.19 | 0 |
| 128  | 339  | 5.19 | 319  | 5.34 | 0 |
| 256  | 667  | 5.31 | 629  | 5.39 | 0 |
| 512  | 1197 | 5.64 | 1242 | 5.64 | 0 |
| 1024 | 2214 | 6.16 | 2167 | 6.19 | 0 |
| 1460 | 3091 | 6.62 | 2798 | 6.87 | 0 |

Two patterns: **throughput rises with size**, and **RTT is almost flat** (~5–7 ms) across
all sizes. Both point to the same cause.

The firmware's throughput is defined (FR-BW-4) as:

$$\text{throughput} = \frac{2 \times \text{size}}{\text{average RTT}}$$

The `2 ×` is because the echo returns a packet of the same size, so one round trip moves
`2 × size` bytes. Check at 1460 B: `2 × 1460 × 8 / 0.00662 ≈ 3.5 Mbit/s` (median) — the
same order as the reported 3.1 Mbit/s (which uses the slightly larger *average* RTT).

# 2. Why it is low — stop-and-wait

The protocol is **stop-and-wait**: send one packet, wait for its echo, then send the
next. So the packet rate is fixed by the round-trip time, not by the channel:

$$\text{packets/s} \approx \frac{1}{\text{RTT}} = \frac{1}{0.006\,\text{s}} \approx 165 \text{ /s}$$

At 1460 B that is `165 × 1460 × 8 ≈ 1.9 Mbit/s` one way, or ~3.1 Mbit/s counting both
directions — **exactly what we measure.** The channel is idle almost the whole time; it
is waiting for the round trip, not transmitting. This is why:

- **Small packets are "slow"** (32 B → 84 kbit/s): the ~5 ms RTT is fixed overhead, so a
  tiny payload wastes it. Larger packets amortise the same RTT over more data, so
  throughput climbs almost linearly with size while RTT barely moves.
- **The ceiling is the RTT, not the link.** Halving the RTT would roughly double the
  number; widening the link would not, because only one packet is ever in flight.

The 8.6 test is therefore a good **latency / loss** test (0 lost on every size is the real
result), but it is **not** a throughput/capacity measurement.

# 3. Where the ~6 ms round trip goes

In infrastructure (router) mode every packet travels **A → router → B → router → A** —
four hops over the air per round trip, because stations do not talk directly; the AP
relays. The ~5–7 ms is spent on, roughly:

- **Air time and medium access** (CSMA/CA back-off, each of the 4 hops waits for a free
  channel) — the dominant and variable part; it is why `rtt_max` spikes to 15–47 ms when
  the channel is briefly busy while the median stays ~5 ms.
- **AP store-and-forward** — the router receives the whole frame before relaying it, twice
  per round trip.
- **ESP32 processing** — `parsePacket`, the echo copy, UDP/lwIP on both ends, single core.

RTT rises only slightly with size (5.1 → 6.6 ms from 32 to 1460 B) because the extra
serialisation time of a bigger frame is small next to the fixed access/relay overhead.

# 4. Measuring the link's real capacity (reference tests)

To show that the link is far faster than 3 Mbit/s, measure it with a tool that keeps the
channel full (many packets in flight), from a PC/phone on the **same Wi-Fi**.

## 4.1 Bredbandskollen (easy, real-world)

[bredbandskollen.se](https://www.bredbandskollen.se) (Internetstiftelsen) runs parallel TCP
streams to a nearby server and reports **down/up Mbit/s** and latency.

- **How:** on a laptop/phone joined to the same Wi-Fi as the ESP32 boards, open the site
  (or the app) and run the test two or three times.
- **What it shows:** the throughput the Wi-Fi + the home connection actually deliver —
  typically **tens to hundreds of Mbit/s**, i.e. 10–100× the ESP32 figure.
- **What it does *not* isolate:** it measures all the way to an Internet server, so it is
  bounded by the **slower of the Wi-Fi link and the ISP subscription**. It proves the
  *Wi-Fi path* is not the bottleneck for our 3 Mbit/s, but it is a WAN test, not a pure
  LAN-Wi-Fi test.
- **It also reports latency** — useful to compare with our ~6 ms RTT, but remember the
  paths differ. Bredbandskollen's ping is **PC → router → Internet → server** and back
  (one way out, one way home); our ESP RTT is a **LAN** round trip **A → router → B →
  router → A** (four air hops, no Internet). So Bredbandskollen's latency is dominated by
  the WAN leg, while ours is pure local air access + AP relay. If Bredbandskollen shows a
  latency not much larger than our 6 ms, almost all of our RTT is the local Wi-Fi/AP; if
  it is much larger, the extra is the Internet path — either way it helps attribute where
  the milliseconds live.

**Record here after running:**

| Run | Down (Mbit/s) | Up (Mbit/s) | Latency (ms) |
|-----|---------------|-------------|--------------|
| 1   |               |             |              |
| 2   |               |             |              |

## 4.2 iperf3, PC ↔ PC on the LAN (pure Wi-Fi capacity)

For the Wi-Fi link *without* the Internet, run `iperf3` between two computers on the LAN
(one wired to the router, one on Wi-Fi):

```bash
# on the wired PC (server)
iperf3 -s
# on the Wi-Fi PC (client) — 10 s TCP, then UDP
iperf3 -c <server-ip> -t 10
iperf3 -c <server-ip> -u -b 100M -t 10
```

This isolates the Wi-Fi hop and typically shows **30–150 Mbit/s** on 2.4 GHz 802.11n,
again far above the ESP32 stop-and-wait number.

**Measured — TCP (PC ↔ PC, 192.168.32.4 → .5, iperf 3.18, 1 stream, 10 s):**

| Run | blksize | Throughput (receiver) |
|---|---|---|
| 2026-11-25 | default | 17.3 Mbit/s |
| 2026-11-26 (×5) | 1024–1460 | 31 – 42 Mbit/s |

The two full-MSS runs (`blksize 1460`, matching the `w`/echo packet size) sit steadily at
**~40 Mbit/s** — the cleanest TCP reference. The 2026-11-25 run was simply a worse radio
moment; the link is the same.

**Measured — UDP sweep (same link, finds the ceiling + jitter + loss):** target bitrate
raised until loss appears (`-u -b <rate> -l 1460`, 10 s each):

| Target | Throughput (received) | Jitter | Loss |
|---|---|---|---|
| 40 Mbit | 40.0 Mbit/s | 0.2 – 0.7 ms | ~0 % |
| 45 Mbit | 44.7 – 44.9 Mbit/s | 0.7 – 1.1 ms | 0 – 0.01 % |
| 50 Mbit | 48.2 – 48.6 Mbit/s | 0.5 ms | 0.4 – 1.9 % |

**The 2.4 GHz link saturates at ~48 Mbit/s:** clean up to 45 Mbit, loss sets in at 50 Mbit
while the receiver cannot take more than ~48. Repeatable over two sweeps.

Both PCs were on **Wi-Fi**, so traffic crossed the air **twice** (PC → router → PC) — these
are two-hop figures. The single Wi-Fi hop is therefore roughly **2× ≈ 80–96 Mbit/s**. Either
way the link carries **far more than the ESP32 echo test**: the ~40–48 Mbit/s two-hop
capacity is **~13–16×** the 3 Mbit/s stop-and-wait number.

## 4.3 ESP32 windowed test (`w`, v8.7.0) — measured on hardware

Running the implemented one-way burst (§5.1) node-to-node confirms the ESP's **own**
ceiling. Measured 2026-11-26, A → echo nodes, 10000 × 1460 B:

| Leg | tx | Throughput (received) | Loss | Jitter |
|---|---|---|---|---|
| A → B | 11.26 | **11.00 Mbit/s** | 232/10000 (2.3 %) | 0.49 ms |
| A → C | 11.23 | **10.98 Mbit/s** | 223/10000 (2.2 %) | 1.88 ms |
| A → PC | 11.38 | *(tx only — WSL NAT blocked the report)* | — | — |

The ESP's transmit side tops out at **~11 Mbit/s** in all three directions — consistently,
whether the receiver is another ESP or the PC. So the bottleneck is the **ESP itself**
(single stream + CPU), not the Wi-Fi link, which carries 40–48 Mbit/s. Loss ~2.3 %, jitter
under 2 ms.

## 4.4 Comparison

| Measurement | What limits it | Result |
|---|---|---|
| 8.6 echo test (ESP32, stop-and-wait) | **RTT** (one packet in flight) | ~0.08 → 3.1 Mbit/s |
| **w test (ESP32, windowed burst)** | **ESP TX** (single stream + CPU) | **~11 Mbit/s** (measured, 2.3 % loss) |
| iperf3 PC ↔ PC, TCP, 2.4 GHz (two hops) | Wi-Fi link only | **~40 Mbit/s** (measured, full MSS) |
| iperf3 PC ↔ PC, UDP ceiling (two hops) | Wi-Fi link only | **~48 Mbit/s** (loss starts at 50) |
| → same, single Wi-Fi hop (estimated) | Wi-Fi link only | ~80–96 Mbit/s |
| Bredbandskollen (PC, Wi-Fi → WAN) | slower of Wi-Fi / ISP | not run (WAN-bound; see §4.1) |

The three ESP/PC rows tell the whole story: **3 → 11 → 48 Mbit/s**. Stop-and-wait (3) is a
method artefact; windowing lifts the ESP to its real ceiling (~11, ~3.7× more); and the
2.4 GHz link itself carries ~48 — **4× what the ESP can push**. So the 8.6 number was never a
link limit, and even an optimal ESP sender stays well under the air capacity: the node, not
the Wi-Fi, is the bottleneck.

# 5. How to make the ESP32 test measure throughput

If we wanted the ESP32 itself to report true throughput rather than latency, the test
would have to keep several packets in flight:

- **Windowed / pipelined UDP:** send N packets before waiting for echoes (a sliding
  window), so the channel stays busy; throughput then tracks the link, not the RTT.
- **One-way bulk + a final count:** blast a fixed number of packets one way and have the
  echo node report how many arrived in how long (bytes ÷ time), removing the return trip
  from the timing.
- **TCP:** let the stack's own windowing fill the pipe; `iperf3`-style.

Any of these would raise the measured number toward the link capacity — at the cost of a
more complex test and losing the clean per-packet RTT/loss figures that the current test
gives so cleanly.

## 5.1 The windowed UDP test — implemented (v8.7.0, Serial command `w`)

This is now built into `lab8_bandwidth.cpp`. Board A blasts a **one-way burst** of UDP
packets (no per-packet wait, many in flight); the receiver counts them and reports back.
It measures the **ESP's own** throughput ceiling plus **packet loss and jitter** — the
three numbers the RTT echo test cannot give.

**Protocol** (tiny, reuses the echo socket on port 5001):

| Message | Meaning |
|---|---|
| `WSTART` | reset the receiver window (reply `WOK`) |
| `WD <seq:4><send_µs:4><filler>` | one-way data packet (not echoed) |
| `WEND` | end the window (reply `WREPORT …`) |
| `WREPORT rx_pkts=… rx_bytes=… dur_us=… maxseq=… jitter_us=… ooo=…` | the receiver's stats |

**Metrics**
- **Throughput** — TX: `bytes·8 / send-time`; RX: `rx_bytes·8 / receive-duration`.
- **Loss** — `(maxseq+1) − rx_pkts`, from the sequence numbers (and `%`).
- **Jitter** — RFC 3550 smoothed inter-arrival variation (`J += (|D|−J)/16`), where the
  per-packet transit uses the embedded send timestamp (clock offset cancels in `D`).
- **Out-of-order** (`ooo`) packets.

**Three directions**

| Command | Path | Isolates |
|---|---|---|
| `w` (on A) | A → echo nodes B and C | node-to-node over Wi-Fi (both ESPs, 2 hops) |
| `w <pc-ip>` (on A) | A → PC running `udp_perf.py --recv` | the ESP's **transmit** ceiling (PC drains fast) |
| `udp_perf.py --send --target <echo-ip>` | PC → echo node | the ESP's **receive** ceiling |

**How to run**
```bash
# ESP A -> echo nodes B and C (on A's Serial):   w
# ESP A -> this PC:
python udp_perf.py --recv --port 5001          # on the PC, then send `w <pc-ip>` on A
# PC -> ESP echo node (its RX ceiling):
python udp_perf.py --send --target 192.168.32.51 --port 5001 --size 1460 --count 10000
```
A prints one line per run:
`WPERF target=B size=1460 sent=10000 tx_us=… tx_mbps=… rx_pkts=… lost=… loss_pct=… jitter_ms=… ooo=… rx_mbps=…`

**Caveats to note when reading the numbers**
- `w` runs **block the monitor** for the burst (shown as the shaded gap), like `r`.
- **A → echo** includes the echo node's own receive-loop limit, so it is node-to-node, not
  a pure link measure. **A → PC** isolates the ESP's transmit ceiling best (the PC keeps up).
- Both ESPs are **2.4 GHz**, both legs are **two hops** via the router, so compare against
  the PC↔PC ~40–48 Mbit/s (two-hop) figure, not the ~80–96 single-hop estimate.
- **Measured ESP ceiling ~11 Mbit/s** (§4.3) — ~3.7× the stop-and-wait 3 Mbit/s, but only
  ~¼ of the PC two-hop figure: the ESP's single stream and CPU are the limit, not the link.

# 6. Conclusions

- The 8.6 number (~3 Mbit/s) is **correct for what it measures** — round-trip,
  one-packet-in-flight — and is **RTT-bound, not link-bound.** It is a latency and loss
  test (0 lost everywhere), not a capacity test.
- Throughput rises with packet size purely because the fixed ~5 ms RTT is amortised over
  more data; RTT itself is set by 4-hop air access and AP relay in infrastructure mode.
- The Wi-Fi link is far faster: **iperf3** on the same 2.4 GHz test network measured
  **~40 Mbit/s** (TCP) and saturates near **~48 Mbit/s** (UDP) PC↔PC at two hops, confirming
  the link is not the bottleneck.
- The windowed **`w` test (v8.7.0)** then measured the ESP directly at **~11 Mbit/s** — the
  real story is **3 → 11 → 48 Mbit/s** (method → ESP → link): stop-and-wait was a method
  artefact, windowing reaches the ESP's true ceiling, and even that stays ~4× below the air
  capacity. The **node, not the Wi-Fi, is the bottleneck.**

# 7. Open items

- [x] **iperf3 PC↔PC on 2.4 GHz (TCP)** — done 2026-11-25: **17.3 Mbit/s** (two hops),
      filled into §4.2/§4.3. No Ethernet cable, by choice.
- [x] **iperf3 UDP sweep** — done 2026-11-26: ceiling **~48 Mbit/s**, jitter 0.2–1.1 ms, loss ~0 below 45 Mbit (into §4.2/§4.3).
- [x] **Bredbandskollen decision** — not run; it is WAN-bound, so iperf3 is the stated
      primary reference. (Could still be added later as a one-line real-world note.)
- [ ] **Fill the reference tables and rebuild** the PDF once the numbers are in (record
      band = 2.4 GHz, TCP/UDP, and the two-hop note).
- [x] **BUILT — ESP32-side windowed UDP throughput test (v8.7.0, `w` command).** Route (a)
      below is implemented and documented in §5.1: one-way burst, measures throughput +
      loss + jitter, three directions (A→echo, A→PC, PC→echo via `udp_perf.py`). Syntax- and
      loopback-verified, then **hardware-tested 2026-11-26**: ESP ceiling ~11 Mbit/s (§4.3).
    - *(a) Done)* **custom windowed/pipelined UDP mode** in `lab8_bandwidth.cpp`: A sends N
      packets back-to-back without waiting; the receiver counts bytes over time and reports
      throughput/loss/jitter. Reuses the jumper/role setup; no external protocol. Serial
      command `w` (N = `KPerfCount`), PC side in `udp_perf.py`.
    - *(b) Not needed)* **official iperf via the ESP-IDF iperf component** — true iperf, but
      integration work in an Arduino project. *Note: iperf is open source (C/`libiperf`);
      the Python `iperf3` package is only a wrapper around that C library and runs on a PC,
      not on the ESP32 — so Python does not shortcut the ESP side.* Route (a) made this
      unnecessary.
- [x] iperf3 **UDP** sweep on the PC side (jitter/loss) — done 2026-11-26 (see above).
- [ ] Add this document to the repo and the KK3 coverage checklist.
