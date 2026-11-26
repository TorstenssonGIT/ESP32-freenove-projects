# Windowed UDP throughput test (`w`, firmware v8.7.0)

Quick reference. Full methodology, metrics and caveats: see
`lab8_wifi_performance.md` §5.1.

Measures the ESP32's real throughput + packet loss + jitter with a one-way
UDP burst (not the RTT-bound stop-and-wait of the `r` test).

## Files
- `lab8_bandwidth.cpp` — firmware v8.7.0 (Serial command `w`; echo nodes
  handle the `WSTART / WD / WEND / WREPORT` protocol).
- `udp_perf.py` — PC side (pure Python 3 stdlib).

## Run

```bash
# A -> echo nodes B and C (on A's Serial monitor):
w

# A -> this PC (ESP transmit ceiling):
python udp_perf.py --recv --port 5001      # on the PC, then `w <pc-ip>` on A

# PC -> echo node (its receive ceiling):
python udp_perf.py --send --target 192.168.32.51 --port 5001 --size 1460 --count 10000
```

Board A prints one line per run:

```
WPERF target=B size=1460 sent=10000 tx_us=.. tx_mbps=.. rx_pkts=.. lost=.. loss_pct=.. jitter_ms=.. ooo=.. rx_mbps=..
```

## Notes
- IPs: A .50, B .51, C .52, PC .5 — all on 192.168.32.x (same subnet; the
  echo nodes drop off-subnet packets).
- `w` blocks the movement monitor for the burst (shown as the shaded gap,
  like `r`).
- A->echo is node-to-node (includes the echo node's RX-loop limit); A->PC
  isolates the ESP transmit ceiling best.
- Expected ESP ceiling ~10-30 Mbit/s vs the PC reference ~40-48 Mbit/s
  (2.4 GHz, two hops) — see §4.2/§5.1.
