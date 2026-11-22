---
title: "Lab 8 – Wi-Fi radar, phase 1: reflector and mechanical build"
author: "Johan Torstensson"
date: "2026-11-17"
---

# 1. Purpose

Phase 1 of the Wi-Fi radar uses **one board** (the controller, board A), a
**rotating reflector** driven by a stepper motor, and the **router as a fixed
signal source** about 3 m away in another room. The reflector turns around the
ESP32's antenna; the received signal strength of the router's beacons per angle
shows the direction of the router and, over time, movement in the radio path.

This document records the physics behind the reflector's dimensions, the
mechanical build and the measurement method. Requirements: FR-RADAR and HW-7 in
`lab8_wifi_requirements.md`.

# 2. Physics

| Quantity | Value | Basis |
|---|---|---|
| Frequency | ≈ 2.44 GHz | Wi-Fi 2.4 GHz band |
| Wavelength λ | ≈ 12.3 cm | λ = c / f |
| Distance antenna → foil | **3.1 cm (λ/4)** | Reflected wave in phase in the forward direction |
| Distance to avoid | < 1.5 cm | Near field: the antenna is detuned, and the phase is wrong |
| Next good distance | ≈ 9.2 cm (3λ/4) | The optimum repeats every λ/2 |
| Foil thickness needed | ≫ 1.7 µm | Skin depth of aluminium at 2.44 GHz: δ = √(ρ / (π f μ₀)) |
| Foil used | 14 µm (0.014 mm) aluminium kitchen foil | ≈ 8 × the skin depth: reflects practically everything |

**Phase.** The reflected wave travels the distance *r* there and back and is
shifted 180° by the reflection:

$$\varphi = \frac{2r}{\lambda} \cdot 360^\circ + 180^\circ$$

Reinforcement is best when φ is a whole turn (360°), i.e. at r ≈ λ/4. The curve
is symmetric around 3.1 cm in phase (1.5 cm and 4.5 cm are both about 90° off),
but too close is worse than too far because of the near field.

**Size.** A reflector needs to be at least about λ/2 (≈ 6 cm) in the direction
of the antenna's polarization to have a clear effect. Height reduces leakage
above and below without costing any sweep.

**The antenna.** The ESP32-S3-WROOM-1 has a PCB inverted-F antenna at the end of
the module, with the radiating element along the module's short edge. It
radiates roughly like a small dipole: strongest perpendicular to the element,
weakest along it (two nulls), and somewhat reduced towards the module itself.
With the board standing and the antenna pointing down, the nulls lie along the
breadboard's face; the router should be straight out from the face.

**Beam width.** The width of the beam is set by the aperture D compared with
λ: θ ≈ 70° · λ / D. With D ≈ 6 cm (< λ) the half-cylinder gives a broad lobe,
not a narrow beam; a narrow slot would make it wider, not narrower. The
direction is therefore found by fitting a curve to the whole sweep and by
locating the sharp edges, not from a narrow beam. A narrow beam (about
30–35°) needs a larger aperture: a parabolic trough of ≈ 25–30 cm or an
external antenna (ESP32-S3-WROOM-1U). Full analysis: design report,
section 3.5.

# 3. The reflector

| Part | Measurement |
|---|---|
| Shape | Straight cylinder (same radius over the whole height) |
| Inside diameter | **6.2 cm** (radius 3.1 cm) |
| Circumference | 19.5 cm (C = 2πr) |
| Strip | 21 cm long (1.5 cm overlap at the seam) |
| Height | **At least 6 cm** (λ/2); as high as possible, up to a few mm below the lid |
| Carrier | 120 g paper (optionally 80 g inner layer to protect the foil) |
| Foil | **9.75 cm** of the strip (half the circumference = 180°), the whole height, on the inside, one smooth continuous layer |
| Seam | On the paper half, opposite the foil |
| Fixing | Tape on surfaces; hot glue only at structural joints |

The paper half is practically transparent to the radio waves; only the foil
decides where the reflector works.

# 4. Attachment to the motor

```
        │ cylinder │        ← level 2: the cup's cut edge holds the cylinder
        │╲  cup   ╱│
        │ ╲      ╱ │
        ├──[disc]──┤        ← level 1: support disc Ø 6.1–6.2 cm
            ┃ axle            reinforced cup bottom: 2 × 1.78 mm cardboard + hot glue
         [motor]
```

| Part | Measurement |
|---|---|
| Cup (adapter) | Paper coffee cup: bottom Ø 5.0 cm, top Ø 7.4 cm (excl. rim), skirt 5.16 mm |
| Cut | Where the cup's diameter is ≈ 6.2–6.3 cm (measured position 4.8–4.9 cm from the bottom; check before cutting) |
| Reinforced bottom | 2 × 1.78 mm cardboard, hot glue |
| Support disc | Ø 6.1–6.2 cm: the cylinder slides snugly over it and is centred |
| Axle hole | Drilled **5 mm**, exactly in the centre of all layers |
| Axle | 28BYJ-48: 5 mm with two flats (≈ 3 mm across), sticks out 9–10 mm; hot glue fills the flats |
| Skirt clearance | About 4 mm above the motor's top; glue must not run onto the motor |

# 5. Setup in the shoebox

```
            LCD (facing up, readable from above)
          ┌────────────┐
          │ breadboard │ ← standing on its long edge on the lid
          │  [ESP32]   │
   ═══════╪════════════╪═══════ lid, with a slot for the antenna
              antenna ▼
          ( reflector )   turns 360° around the antenna
               ┃
            [motor]       glued to the bottom of the box
   └──────── shoebox (= base, taped to the table) ────────┘
```

- The breadboard stands on its long edge; the LCD, plugged in perpendicular to
  the breadboard, becomes a horizontal shelf facing up. Support it from below
  so its pin header is not strained.
- The antenna points down through a slot in the lid, well inside the cylinder.
- The motor is glued with its axle exactly under the antenna (plumb line from
  the slot).
- The ULN2003 driver board sits outside the reflector's path; the motor has its
  own 5 V supply with a shared GND.
- No metal near the antenna (remove staples; no metal weights).
- The board's front (the module's shield side) faces the router; the router
  lies straight out from the board's face, never along its short edges (the
  antenna's weak directions).
- A degree scale on the lid around the axle helps check the angles by eye.

**Angle convention:**

| Item | Value |
|---|---|
| The angle refers to | The magnet's position, clockwise from above |
| Index pulse (strongest field at the TMAG5273) | **180°** |
| Reflector opening at 180° | Faces the router (the magnet is behind the foil, the TMAG behind the axle) |
| Router expected at | **180°**: the peak lies in the middle of a 0–345° sweep, so the curve fit needs no wrap-around |
| 0° | Away from the router: the reflector shadows the antenna, so the dip is expected here |

The router's real direction is measured once with a protractor from the axle;
the difference between it and the fitted θ₀ is the system's direction error.

# 6. Motor

| Topic | Value |
|---|---|
| Motor / driver | 28BYJ-48 (5 V) + ULN2003, IN1–IN4 on four GPIO pins |
| Resolution | ≈ 2037.9 full steps per turn ≈ 0.18° per step (gear ratio ≈ 1:63.68, not a whole number); the exact value is measured by the self-calibration, so any stepper motor (e.g. 200 or 400 steps) works without code changes |
| Backlash | 1–2°: always approach a measuring angle from the same direction |
| Speed | At most ≈ 15 rpm |
| Index sensor | TMAG5273 (A1) 3D Hall sensor (Qwiic, I2C 0x35, range ±80 mT), fixed in the box **on the side away from the router**, standing, chip facing the cup, at the magnet's height |
| Magnet | Neodymium Ø 5 × 3 mm (axial), glued on the outside of the cup, inside the cylinder, just above the motor, **in line with the centre of the foil**, flat face pointing outwards; 6–8 mm from the chip (≈ 13–24 mT) |
| Homing | At start, search back and forth past the magnet to find the field peak (or the zero crossing of the tangential axis); then turn forward in the measuring direction to the first angle |
| Rotation while measuring | **Continuous, always in the same direction** (clockwise from above); each turn passes the index, which re-references the step count |
| Lost-step check | At each index pulse, the step count minus the expected count is logged (`index_error_steps`); it should stay within ±1 step |
| Self-calibration | After homing: **two full turns without radio measurements**, measuring steps per turn (index to index), the motor's background field per angle, the index's peak and zero crossing (thresholds), the direction of rotation, and repeatability (the two turns within ±1 step); measurements start only if the calibration passes |
| While measuring | Coils off (all IN low): the gearbox holds the position; no current, no heat, no electrical noise |
| Timing | The motor only moves in the idle window, never during a measurement |
| Library | `AccelStepper`, pin order IN1, IN3, IN2, IN4 |

# 7. Build order (prototype first)

1. Place the breadboard on the lid; mark where the antenna ends up.
2. Cut the slot; check that the antenna hangs freely.
3. Mark the centre under the antenna on the bottom (plumb line).
4. Glue the motor with the axle exactly at the mark.
5. Make a **plain paper** cylinder (taped) and test: height to the lid,
   clearance to the antenna all the way round, a slow full turn.
6. Adjust and record the final dimensions (section 9).
7. Make the real cylinder: 120 g paper, foil, measurements from step 6.
8. Check that the cylinder does not wobble: the antenna stays centred during a
   full turn.

# 8. Measurement and analysis

1. **Reference sweep without a reflector:** shows the antenna's own pattern and
   the room's background.
2. **Sweeps with the reflector:** the median of about 50 beacons per angle
   (≈ 5 s), so differences under 1 dB become visible despite 1 dB RSSI
   resolution.
3. **No locked channel:** the router channel is logged with every value; the
   baseline is calculated per channel; values around a channel change are
   flagged.
4. **Curve fit** of the median per angle:

$$P(\theta) = A \cdot e^{-\frac{(\theta - \theta_0)^2}{2\sigma^2}} + C$$

| Result | Meaning |
|---|---|
| θ₀ | Direction to the router, more precise than the motor step |
| σ | Beam width of the reflector |
| C | Leakage plus the room's background |
| A / C | Contrast: how much the reflector stands out from the leakage |
| Peak minus dip | Front-to-back ratio |

5. **Comparisons:** no reflector, the cylinder at 6 cm height, and a taller
   one; the difference in C shows how height reduces leakage.
6. **Log the room:** distance (≈ 3 m), the wall, door open or closed, and
   where people are.

# 9. Dimensions to record after the prototype

| Measurement | Value |
|---|---|
| Antenna end below the lid | |
| Cylinder height (final) | |
| Clearance cylinder top → lid | |
| Smallest clearance antenna → cylinder during a full turn | |
| Cup cut height / diameter at the cut | |
| Measured inside diameter of the final cylinder | |
| Distance and direction to the router | |
| Router channel during the reference sweep | |

# 10. Next step: phase 2 – slaves help pinpoint the direction

Phase 1 finds the direction to a fixed source and sees movement on one link.
Phase 2 adds two more boards (slaves B and C) at fixed points around the area,
so a movement can be located, not only detected. Timing is described in
`lab8_wifi_rt_design.md`, sections 5.1–5.2.

```
            A (master: reflector on motor, LCD)
           / \
          /   \        movement disturbs the links it crosses
         B ─── C       → which links are disturbed gives the sector
      (slave) (slave)  → A's reflector then homes in within that sector
```

| Step | What is added | Result |
|---|---|---|
| **2a – passive slaves** | B and C listen to the same router beacons in the same windows as A; nobody transmits | Three router links (A, B, C): a coarse position; A's reflector sweeps only that sector |
| **2b – active slaves** | The boards also transmit to each other in turns (NTP-scheduled slots with guard times) | Three extra links (A–B, A–C, B–C), independent of the router; each link measured in both directions |

**Placement:** a triangle around the area to watch, the boards at the same
height, a few metres apart, each with a calibrated empty-room baseline.

**Homing:** when one or two links show movement, A works out the sector, steps
its reflector within it (one angle per cycle or round) and fits the curve, as in
phase 1. The result is a direction from A plus the sector from the triangle.

**Hardware per slave:** an ESP32-S3 on USB power (power bank), no reflector, no LCD. Host names and fixed IPs:
`lab8-echo` (B, 192.168.32.51) and `lab8-echo2` (C, 192.168.32.52).
