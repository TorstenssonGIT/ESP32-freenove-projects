# KK3 coverage checklist (working index — not a deliverable yet)

Purpose: make sure every KK3 topic lands in a **building block** document. KK3 itself
(the completion/overview report) is assembled from these at delivery; this table is its
skeleton. Status: ✅ done · 🟡 partial · ⬜ to do.

Last updated: 2026-11-24.

## KK3 lesson topics → where they live

| KK3 topic | Building block | Status |
|---|---|---|
| Ström, effekt, energi; batteritid som beräkning | `lab8_kk3_energy.md` §1 | ✅ |
| Var strömmen tar vägen (component breakdown) | `lab8_kk3_energy.md` §2 | ✅ |
| Energilägen & uppvakningskällor | `lab8_kk3_energy.md` §3 | ✅ |
| Duty cycling | `lab8_kk3_energy.md` §4 | ✅ |
| Kursens energimetod + gränser | `lab8_kk3_energy.md` §5 | ✅ |
| Från energibudget till optimering, steg för steg | `lab8_kk3_energy.md` §6 | ✅ |
| Radiooptimering + beräkning på noden (edge vs cloud) | `lab8_kk3_energy.md` §7 | ✅ |
| Minnes- & CPU-optimering (real RAM/flash, threads) | `lab8_kk3_energy.md` §8 | ✅ |
| (§9 Hållbar utveckling) | — dropped by request | ⬜ n/a |

## Project-completion items (KK3 = "all documents complete + energy/resource usage")

| Item | Building block | Status |
|---|---|---|
| Development complete (v8.6.x, all features working) | `lab8_wifi.md` feature list | ✅ |
| 8.6.2 bandwidth web visualisation (per-size charts, NTP timestamps, r-gap) | `lab8_wifi.md`; firmware `src/lab8_bandwidth.cpp` | ✅ |
| Versioning scheme (v8.SECTION.N) | firmware header + web page | ✅ |
| RT implications: blocking r-test, off-core web server, same-link sensing | `lab8_wifi_rt_design.md` §8.1, §8.2 | ✅ |
| Resource usage (RAM 18.7 %, flash 34.2 %, threads) | `lab8_kk3_energy.md` §8 | ✅ |
| Finding: Wi-Fi spread leads the PIR on approach | `lab8_wifi.md` (movement test) | ✅ |
| Supply check: A+motor on KVM 100 W USB-C; B on 5 V/3 A power bank | `lab8_kk3_energy.md` §2, §4 | ✅ |
| Regulator is a linear LDO (verified, DevKitC-1) | `lab8_kk3_energy.md` §2, §5 | ✅ |

## Documents in the KK3 set (all final, English)

| Document | Role | State |
|---|---|---|
| `lab8_wifi.md` | Plan, feature list/status, tests, findings, photos | updated |
| `lab8_wifi_requirements.md` (+PDF) | Requirements with IDs, verification | final |
| `lab8_wifi_rt_design.md` (+PDF) | Real-time design; §8.1/8.2 added | updated |
| `lab8_radar_reflector.md` (+PDF) | Reflector physics, mechanics, calibration | final |
| `lab8_wifi_security_analysis.md` (+PDF) | Assets, threats, credential protection | final |
| `lab8_kk2_design_report.md` (+PDF) | KK2 overview (links the above) | final |
| `lab8_kk3_energy.md` (+PDF) | **New:** energy + resource usage | done |
| `lab8_kk3_design_report` | **KK3 overview — assemble at delivery** | ⬜ not started (by choice) |

## Open / deferred

- ⬜ **KK3 overview report** — deliberately deferred; assemble from this table at delivery.
- ⬜ **Swedish Word versions** — all docs; deferred until delivery (no translation before then).
- 🟡 Optional KK2-conclusion line: add "Wi-Fi leads the PIR on approach" to the KK2
  design report conclusions (finding is already in `lab8_wifi.md`).
- ⬜ Future work (not required): run the monitor through the r-test with rate-aware
  statistics as a high-rate active-sensing probe (`lab8_wifi_rt_design.md` §8.2).
