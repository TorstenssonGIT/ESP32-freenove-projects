---
title: "Lab 8 – Energy, power and optimisation on the ESP32-S3 (KK3)"
author: "Johan Torstensson"
date: "2026-11-24"
---

# 0. About this report

KK3 applies the course's energy method to the Lab 8 Wi-Fi system. The system has
three ESP32-S3 boards: the controller **A** (`lab8-esp32`, LCD, optional reflector
motor), and two echo nodes **B** (`lab8-echo`) and **C** (`lab8-echo2`). A and the
motor are powered from the KVM switch's 100 W USB‑C charger; B runs from a 5 V / 3 A
power bank.

The report is in two arcs: first an **energy budget** (where the current goes, the
energy modes, duty cycling, and the method with its limits), then the step from
**budget to optimisation** (radio, on‑node computation, memory and CPU). The Lab 8
nodes are the worked example throughout.

The method follows the course rule: **measured times × datasheet currents.** Current
values come from the component datasheets; only durations and duty cycles are
measured on the running system. Values we had to measure (no datasheet) are marked.

---

# 1. Current, power, energy — and why battery life is a calculation

Three quantities, three units:

- **Current** $I$ — how much charge flows, in amperes (A). Set by the circuit.
- **Power** $P = V \cdot I$ — the rate of energy use, in watts (W).
- **Energy** $E = P \cdot t$ — power over time, in joules (J) or watt‑hours (Wh).

A battery stores a fixed amount of energy (or charge). Runtime is therefore never a
property of the device alone — it is a **division**:

$$t_{\text{battery}} = \frac{\text{battery capacity}}{\text{average current}} \qquad\text{e.g. } \frac{\text{mAh}}{\text{mA}} = \text{h}$$

The two things that decide runtime are the **capacity** you carry and the **average
current** you draw. The whole of energy engineering is lowering the second number
without breaking the function. Note it is the *average* current that matters: a short
286 mA transmit burst costs little if it happens rarely, while a steady 95 mA listen
current runs the battery flat even though it looks small.

---

# 2. Where the current goes (the Lab 8 nodes)

Each board is fed **5 V** over USB and drops it to **3.3 V** with an on‑board **linear
LDO** regulator (the DevKitC‑1 reference design the board follows specifies a 5 V→3.3 V
LDO, not a switching converter). A linear regulator passes almost the same current out
as in, so
the 5 V input current ≈ the 3.3 V load current, and the difference
$(5.0-3.3)\,\text{V} \times I$ is lost as heat (see §5). Datasheet currents below are
at the 3.3 V rail for the ESP32‑S3‑WROOM‑1 module.

| Consumer | Current | Source | When it draws |
|-----------------------------|-----------|---------------|------------------------------|
| Radio **TX** 802.11n HT20, +17.5 dBm | 286 mA | WROOM‑1 datasheet | sending (echo reply, UDP burst) |
| Radio **RX** / listening | 95 mA | WROOM‑1 datasheet | connected, waiting for frames |
| CPU 240 MHz, extra over RX ($\Delta I$) | +14.7 mA | datasheet (47.6 − 32.9 mA, WAITI) | computing while the radio is on |
| Flash access | +10 mA | datasheet | reading/writing flash |
| LCD 1602 backlight (board A) | 14.1 mA @ 5 V | **measured** (no datasheet) | backlight on |
| LCD controller + PCF8574 (board A) | < 1.4 mA | HD44780 / PCF8574 datasheets | always, while powered |
| Reflector motor 28BYJ‑48 + ULN2003 (board A) | 164 mA peak | **measured** | only during a reflector sweep |
| PIR HC‑SR501 (board B) | ~50 µA | datasheet (quiescent) | always — negligible |
| INA226 current monitor | ~0.4 mA | datasheet | always — negligible |

The $\Delta I_{\text{CPU}}$ is added to RX rather than counted on its own: the radio
figures already include a running CPU, so taking the full 47.6 mA would **double‑count**
the core. The difference 14.7 mA is the extra cost of keeping the CPU busy while the
radio is active.

**Reading the table:** an echo node that only listens sits near **95 mA + CPU**; it
briefly jumps to **286 mA** when it answers. The LCD backlight (14 mA) and, on A, the
motor (164 mA peak) are the only large non‑radio loads, and the motor runs only when a
reflector sweep is commanded — in the current build it never runs, so it drops out of
the steady‑state budget.

---

# 3. Energy modes and wake‑up sources

The ESP32‑S3 is not one load but several, depending on what is powered. The course's
four modes, with datasheet currents at 3.3 V:

| Mode | What is on | Current (3.3 V) | Wake latency |
|----------------|--------------------------------------|------------------|----------------|
| **Active** | CPU + radio (TX/RX) | 95–286 mA | — |
| **Modem‑sleep**| CPU on, radio off between beacons | ~33–48 mA | immediate |
| **Light‑sleep**| CPU paused, RAM kept, RTC on | ~240 µA | ~1 ms |
| **Deep‑sleep** | only RTC + RTC memory | ~7 µA | ~0.3 ms + boot |

The jump from active to deep‑sleep is **four orders of magnitude** (286 mA → 7 µA).
The art is spending as much time as possible in the deep rows.

A sleeping node is useless unless something can wake it. **Wake‑up sources:**

- **Timer (RTC)** — wake after a fixed interval; the basis of duty cycling (§4).
- **GPIO / ext0 / ext1** — a pin change, e.g. the **PIR** on board B could wake the
  node only when it sees motion.
- **UART** — activity on the serial line.
- **Touch / ULP** — a low‑power co‑processor can watch sensors while the main CPU
  sleeps.

**Lab 8 today spends everything in Active.** For low latency (NFR‑RT‑2) the firmware
calls `WiFi.setSleep(false)`, which *disables* modem‑sleep and keeps the radio in full
RX. That is the right choice for a mains‑powered monitor, and exactly the wrong one for
a battery node — which is the lesson of §4.

---

# 4. Duty cycling

Average current is the time‑weighted sum of the mode currents:

$$I_{\text{avg}} = \sum_i I_i \cdot \frac{t_i}{T}$$

where $t_i/T$ is the fraction of each period spent in mode $i$. **Duty cycling** means
doing the work in a short active burst, then sleeping for the rest of the period. Worked
on echo node **B** (one 1 s sample + reply per period, otherwise idle):

| Strategy | Active current | Sleep current | Duty | $I_{\text{avg}}$ (3.3 V) |
|-----------------------------------|----------------|---------------|-----------|--------------------------|
| **Today** — radio always on (`setSleep(false)`) | ~110 mA | 110 mA | 100 % | **110 mA** |
| Modem‑sleep between beacons | ~110 mA | ~40 mA | — | **~40 mA** |
| Deep‑sleep, wake 1 s every 60 s | ~110 mA | 7 µA | 1.7 % | **~1.8 mA** |

Converting to runtime on the power bank (nominal 10 000 mAh at 3.7 V ≈ **37 Wh**),
using $P = 5\,\text{V} \times I_{5\text{V}}$ and $I_{5\text{V}} \approx I_{3.3\text{V}}$
(linear regulator):

| Strategy | Power | Runtime (37 Wh) |
|-----------------------------------|---------|---------------------|
| Radio always on | 0.55 W | **~67 h (≈ 3 days)** |
| Modem‑sleep | 0.20 W | **~185 h (≈ 8 days)** |
| Deep‑sleep, 1 s / 60 s | 0.009 W | **~4000 h (≈ 170 days)** |

Same hardware, same battery — **60× longer** runtime from duty cycling alone. The cost
is latency and missed events: a node that sleeps 59 s of every 60 cannot detect
movement in that gap. Duty cycling is only free when the application tolerates the gap,
which is why the Lab 8 monitor (which must react within a second) stays awake and must
be mains‑powered.

---

# 5. The course method — and its limits

**Method:** measured times × datasheet currents. Build a table of modes and their
datasheet currents, measure how long the system spends in each, and compute
$I_{\text{avg}}$ and runtime. It is fast, needs no lab bench, and is good to roughly
±20 %. Its value is *ranking* design choices (always‑on vs modem‑sleep vs duty cycle),
which it does reliably.

**Where it is wrong, and by how much:**

- **Typical vs maximum.** Datasheet currents are typical at 25 °C and nominal voltage.
  Peak TX can exceed the typical figure; temperature and silicon spread move it. Design
  margin, not a single number.
- **Transients are averaged away.** A 286 mA TX burst of a few ms disappears into an
  averaged figure, but it still sizes the supply and the decoupling. Averaging answers
  "how long does the battery last," not "will the rail sag on a TX burst."
- **The regulator is not free.** The 5 V→3.3 V linear drop wastes
  $(5-3.3)/5 = 34\,\%$ of the input energy as heat. The datasheet current is at 3.3 V;
  the *battery* pays at 5 V. A switching regulator would recover most of that.
- **The battery is not ideal.** Capacity falls with load, age and cold; many power
  banks **auto‑shut‑off below ~50–75 mA**, so a deep‑sleep node drawing 1.8 mA would
  simply switch the bank off — the calculation says 170 days, the bank says 0. This is
  the single biggest gap between the number and reality for our node.
- **Board housekeeping.** The USB‑UART bridge, LEDs and quiescent regulator current
  (tens of mA on a dev board) are not in the module datasheet but are real.

We cross‑checked the datasheet budget against the **INA226** on board A. The measured
steady current agreed with the datasheet estimate to within the method's ±20 %, which
is the point: the method is a good estimator, not a measurement, and should be labelled
as such.

---

# 6. From energy budget to optimisation, step by step

The budget tells you where to spend effort. A repeatable order:

1. **Measure the baseline.** Average current and the duty breakdown (§4). Without a
   number, "optimisation" is guessing.
2. **Attack the biggest time‑slice, not the biggest current.** The 286 mA TX burst is
   dramatic but rare; the 95–110 mA always‑on listen is boring but dominant. Cut the
   dominant *average* contributor first.
3. **Add the deepest sleep the latency budget allows** (§4). This is almost always the
   largest single win.
4. **Shorten the active burst** — faster CPU to finish sooner (race‑to‑sleep), less
   radio airtime, fewer flash writes.
5. **Fix the supply** — a switching regulator instead of the linear drop (§5) recovers
   ~34 % with no firmware change.
6. **Re‑measure and stop** when the remaining gain is smaller than its cost in
   latency, reliability or complexity.

For Lab 8 the ranked wins are: (1) allow modem‑sleep when latency permits
(110→40 mA), (2) duty‑cycle with a PIR wake so the node sleeps until motion
(40 mA→~2 mA), (3) swap the linear regulator for a buck converter (−34 %). Steps 1–2
dwarf everything else.

---

# 7. Radio optimisation, and computing on the node instead of the cloud

**The radio is the dominant load,** so radio airtime is the dominant knob:

- **Send less, less often.** Every frame costs ~286 mA while it is in the air. Batch
  readings, drop redundant packets, and raise the beacon/DTIM interval so the radio
  wakes for fewer beacons.
- **Right‑size the transmit power.** +17.5 dBm is full power; on a short indoor link a
  lower setting still closes the link at lower current.
- **Shrink the payload.** Our UDP status messages are tens of bytes — short frames mean
  short airtime.

**Compute on the node, not in the cloud.** The Lab 8 design already does this, and it is
an energy decision, not only a latency one. Board B computes the **median RSSI and the
per‑second spread** locally and sends A a compact `MEAS` summary once per second, rather
than streaming every 100 ms raw sample to a server for processing. The trade‑off is
direct:

- **Streaming raw to the cloud:** ~10 samples/s on the air continuously → the radio can
  never leave RX/TX, and average current stays high.
- **Computing on the node (edge):** a few milliseconds of cheap CPU math replaces ten
  transmissions with one. CPU energy is far cheaper than radio energy, so moving the
  work to where the data is born *lowers* total energy — and keeps the raw data, which
  may be private, off the network. Edge computation is the rule for battery sensors for
  exactly this reason.

---

# 8. Memory and CPU optimisation

**What the Lab 8 firmware actually uses** (PlatformIO build report, one image for all
three roles):

| Resource | Used | Available | % |
|-----------|-------------|---------------|--------|
| RAM (SRAM) | 61.4 kB | 320 kB | **18.7 %** |
| Flash | 1.14 MB | 3.34 MB | **34.2 %** |

There is plenty of head‑room: no PSRAM is needed, and the program fits an 8 MB module
with the 16 MB module (board B) only giving spare flash. Sizing the part to the program
matters because powered, unused flash still leaks.

## 8.1 Threads

The ESP32‑S3 is **dual‑core** and the Arduino core runs on **FreeRTOS**, so the system
is multi‑threaded even though our application logic is not. The tasks that run:

| Task | Core | Role |
|--------------------|------|-------------------------------------------|
| `loopTask` (ours) | 1 | all application code — sampling, web server, bandwidth test |
| Wi‑Fi driver + lwIP | 0 | the radio and TCP/IP stack |
| idle task × 2 | 0,1 | run when a core has nothing to do (WAITI) |
| timer / event tasks | — | FreeRTOS housekeeping |

**Does it make a difference?** For **energy**, little in our design: both cores and the
radio are kept on, so the second core mostly idles but still draws — it is already
inside the 95–110 mA listen current, and we gain nothing from it because our work is
tiny. For **behaviour** it matters: the Wi‑Fi stack running on its own core keeps the
link alive even while our blocking bandwidth test monopolises `loop()`. But the **web
server is serviced from `loop()`**, so during an `r` run the page freezes — that is the
shaded gap on the live charts. Moving the web server (and the sampler) onto its own
FreeRTOS task would keep the page responsive. The shared‑buffer read/write this raises is
routine — a mutex/semaphore around the history and bandwidth buffers. The deeper point is
that the monitor and the bandwidth test are packets over the **same router link**, so the
test does not inject foreign noise; it only changes the **sampling pattern** (the node is
briefly deaf while it transmits, and the RSSI sample rate jumps from ~10/s beacons to
hundreds/s of packets). With rate‑aware statistics that is manageable — and, turned
around, the packet flood is a far **denser** channel probe, so the test window could be
the best sensing window, not the worst (the active‑sensing idea). The halt is therefore a
simplification of the single‑threaded design, not a necessity; the real‑time design doc
§8.2 analyses this in full.

## 8.2 CPU and flash as energy

CPU and memory choices feed back into energy through **time** (race‑to‑sleep) and
**flash activity**:

- **Race to sleep.** A faster clock finishes the active burst sooner and returns to
  sleep sooner. Running 240 MHz for 2 ms can use *less* energy than 80 MHz for 8 ms,
  even though the instantaneous current is higher — the integral is what counts.
- **Fewer flash writes.** Flash access adds ~10 mA and is slow; a write stalls the CPU.
  Buffer in RAM and write in batches rather than per sample. Lab 8 keeps its history
  and the bandwidth runs in RAM ring buffers (the 20‑run, ~280‑value bandwidth buffer)
  and never touches flash during steady operation.
- **Right‑sized buffers.** The ESP32‑S3 has 320 kB SRAM; our buffers are a few hundred
  bytes each, so there is no need for PSRAM (which would add its own current). Board A
  uses an 8 MB‑flash module, board B a 16 MB one — flash size costs almost nothing when
  idle but more flash powered up is more leakage, so size it to the program.
- **Keep the CPU asleep, not spinning.** Busy‑wait loops burn the full active current
  for no work. Event‑driven code (interrupts, `delay()` that yields, the RTC timer)
  lets the core idle (WAITI, the 32.9 mA figure) instead of spinning at 47.6 mA.

The common thread with §§6–7: **do the work in the cheapest place and then stop.**
Cheapest place is on‑node for data reduction and in the lowest clock/mode that meets
the deadline; "then stop" is race‑to‑sleep and the deepest sleep the latency budget
allows.

---

# 9. Summary

- Runtime is a calculation: capacity ÷ **average** current, and average current is set
  by **duty cycle**, not peak draw.
- In the Lab 8 nodes the **radio dominates** (95 mA listen, 286 mA send); the LCD
  backlight (14 mA) and the motor (164 mA, only on sweeps) are the only other large
  loads.
- **Duty cycling is the big lever** — from always‑on (~3 days) to deep‑sleep 1 s/60 s
  (~170 days) on the same battery — but it trades away responsiveness, which is why the
  always‑awake monitor stays mains‑powered.
- The course method (measured times × datasheet currents) ranks design choices well to
  ±20 %, but hides transients, the 34 % linear‑regulator loss, and the power bank's
  low‑current cut‑off — label it an estimate, not a measurement.
- Optimisation order: measure, cut the dominant *average* load, sleep as deep as
  latency allows, race to sleep, fix the supply, re‑measure and stop. Compute on the
  node to turn expensive radio energy into cheap CPU energy.
