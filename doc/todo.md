# Project TODOs & Technical Debt

This document tracks upcoming features, architectural refactors, and performance optimizations.

---

## 1. FLPR Power & Memory Optimization

> **Reference:** See *FLPR Bare Metal* in project notebook.

### Overview
* The FLPR currently runs a full Zephyr multi-image build from SRAM, requiring a **64 KB** memory allocation (`0x20070000` to `0x2007FC00`) in both:
  * [`app/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay`](../app/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay)
  * [`app_flpr/boards/xiao_nrf54lm20a_nrf54lm20a_cpuflpr.overlay`](../app_flpr/boards/xiao_nrf54lm20a_nrf54lm20a_cpuflpr.overlay)
* This configuration must be revisited in the future for better power management, pending official **`nrfxlib`** (Soft Peripherals / High-Performance Framework) and bare-metal support for the **nRF54LM20A** architecture.

### Action Items
- [ ] **Reclaim SRAM:** Transition to a bare-metal `nrfx` loop or `nrfxlib` micro-runtime to shrink the FLPR memory footprint from **64 KB down to ~2–4 KB**, freeing up ~60 KB of SRAM for the main application core.
- [ ] **Reduce Active Cycles:** Eliminate RTOS scheduling and IPC stack overhead on sensor interrupts, dropping active CPU execution time per event from ~10 µs to < 0.5 µs.
- [ ] **Power Bank Gating:** Power off the unused 32 KB SRAM bank completely during low-power idle.
- [ ] **Fixed-Point Math:** Replace floating-point variance and soft-float dependencies with 32-bit fixed-point integer arithmetic.

---

## 2. General TODOs

- [ ] Add future project action items here
