# Raptor Lake-P findings

What this fork has established about Raptor Lake-P graphics, for the plan in the [README](../README.md#plan).

**Values are from the reference machine:** ASUS TUF Gaming F15 FX507VI, i7-13620H, UHD Graphics `8086:A7A8`
rev 04. Other Raptor Lake-P SKUs differ in EU count and board wiring.

**Sources, October 2026:**
- a read-only Linux i915 capture: Ubuntu 26.04, kernel 7.0, `i915.enable_guc=0 drm.debug=0x1e`, with
  registers read through `intel_reg` and the boot journal;
- the macOS R1 readout (`-wex-rplp-observe`), taken from the Big Sur 11.5.2 recovery booted by OpenCore 1.0.8;
- the firmware's VBT and ACPI tables;
- Linux, Mesa and the TGL PRM, cited where used.

## GT

| Register | Value | Meaning |
| --- | --- | --- |
| `0x9138` slice enable | `0x00000001` | 1 slice |
| `0x913C` DSS enable | `0x0000000F` | DSS 0-3 |
| `0x9134` EU disable | `0x00000000` | All 8 EU pairs per DSS enabled, so 4 x 16 = 64 EU |
| `0x9118` L3 | `0x00000088` | Banks 3 and 7 fused off, so 6 L3 banks |
| `0x9140` media | `0x000E00FE` | VDBOX0 and VEBOX0 only |

- **Linux agrees.** Its SSEU report gives 1 slice, 4 DSS, 64 EU, 16 per DSS. The engines are
  `rcs0 bcs0 vcs0 vecs0`.
- **macOS agrees.** The R1 readout reads the same five fuses and resolves 64 EU in 4 DSS, with forcewake
  requested and released three times each and no failures.
- **Stepping.** Graphics and media C0, matching Linux's `adlp_rplp_revids` for PCI revision 4; display is E0.
- **Frequency.** RPn 100 MHz, RP1 300 MHz, RP0 and maximum 1500 MHz.
- **L3 banks.** Read `0x9118` bits 7:0 as one disable bit per bank. That fits every known data point:
  - Alder Lake-N `0xF0`: 4 banks, the capacity upstream's Alder Lake-N profile uses;
  - this part's `0x88`: 6 banks, matching Mesa's `intel_device_info_update_l3_banks` for 3-5 DSS;
  - a fully enabled 6-DSS GT2 (`0x00`): 8 banks, matching the TGL PRM.

  Linux's older bank-pair reading (bits 3:0 only) also gives 6 here, but 8 for Alder Lake-N.
- **URB.** The TGL PRM's 6-bank configuration (GT1.xf) has a 384 KB URB.
- **Values for the GT profile (stage R2):** 8 native subslices, 64 EU, 6 L3 banks, 384 KB URB.
- **Media.** The media fuse equals the value upstream's Alder Lake-N profile requires, so that media handling
  applies unchanged.
- **Execution.** The R2 BCS self-test (`-wex-rplp-gpu`) passes on the reference machine.
  - One `MI_STORE_DWORD_IMM` + `MI_FLUSH_DW` batch submitted through the BCS execlist port completes: both
    values land, head and tail meet at the ring's end, and the CSB records the context's switch-in and
    completion.
  - At handover the firmware leaves bus mastering on (PCI command `0x0007`) and maps all 64 MB of stolen
    memory 1:1 in the GGTT, which is what the self-test's lease requires.

## Stolen memory (DVMT)

| Item | Value |
| --- | --- |
| Pre-allocated graphics memory | 64 MB (GMS `0x02`) |
| Usable after the WOPCM reservation | 62 MB (`GEN6_STOLEN_RESERVED` puts a 2 MB WOPCM block at the top) |
| Firmware framebuffer | At stolen offset 0, mapped at GGTT offset 0, linear 32 bpp |

- The firmware offers no DVMT setting.
- WhateverXe's DVMT code decodes 64 MB natively; `-igfxdvmt` is not needed.
- Whether the donor framebuffer still needs the smaller display pool used for 60 MB systems is an R2 check.

## Display: the firmware state at handover

This is i915's first hardware readout, before Linux changed anything. It is the state WhateverXe's planned
"adopt" path (R3) inherits.

- **Path.** Pipe A to transcoder A to **DDI A** (combo PHY A), output type eDP, AUX-A. Pipes B-D are off.
  Display version 13 has four pipes.
- **Mode.** 1920x1080 at **144 Hz**, pixel clock 347.711 MHz.
  - Horizontal timing: 1920/1968/2000/2080. Vertical timing: 1080/1083/1088/1158.
  - The firmware already trains the panel's native refresh rate.
- **Link.**
  - DP SST, 4 lanes at **HBR (2.7 Gb/s)**, enhanced framing on, FEC off.
  - **PSR off.**
  - PLL configuration `cfgcr0 0xE001A5`, `cfgcr1 0x88`.
  - **The firmware's PLL choice varies.** At Linux's handover it used DPLL0; under OpenCore (GOP mode set to
    1920x1080) it used DPLL1 with the same configuration, and `DPCLKA_CFGCR0` (`0x01E07801`) selected DPLL1
    for DDI A. Code that adopts the link must read the clock select, not assume a PLL.
- **Panel power.** On at handover (`PP_STATUS 0x80000008`).
- **Plane and scaler.** Only the primary plane is enabled.
  - When the boot loader leaves the firmware in a lower GOP mode, the plane keeps that size and pipe
    scaler 0 upscales it. 800x600 was observed.
  - Set the boot loader to the native mode, e.g. OpenCore `UEFI > Output > Resolution` `1920x1080@32`.
    WhateverXe's handoff checks expect a native, unscaled plane.
  - With that setting the macOS readout shows plane 1A at 1920x1080, linear XRGB8888, stride 7680 bytes,
    surface at GGTT offset 0 of the 256 MB aperture, and both pipe A scalers off.
- **DMC.** Linux loads `i915/adlp_dmc.bin` 2.20; the firmware uses DC5.

## Outputs

| VBT child | Port | AUX | Notes |
| --- | --- | --- | --- |
| LFP1 | DP-A | AUX-A | eDP, x4 |
| EFP2 | DP-F | AUX-F | Type-C TC1 |
| EFP3 | DP-G | AUX-G | Type-C TC2 |

There is no HDMI and no combo-B port on the Intel GPU; this laptop's HDMI connector belongs to the NVIDIA
GPU. Upstream drives only fixed HDMI on display version 13 today, so on laptops like this one **nothing can
show a picture through WhateverXe until eDP (R3) or Type-C support exists.**

## Panel and backlight

- **Panel.** BOE NE156FHM-NX6, 8 bpc.
  - EDID modes: 1920x1080 at 60 Hz and 144 Hz (preferred); range 60-144 Hz.
  - eDP 1.3, link rates 1.62 and 2.7 Gb/s only, PSR1 only.
  - Brightness through the PWM pin: DPCD `0x702 = 0x85`, no AUX brightness.
- **VBT panel entry (type 2).**
  - Power sequencing, as programmed by Linux: power-up 200 ms, power-down 50 ms, power-cycle 500 ms,
    backlight-on 100 ms, backlight-off 200 ms. The firmware itself leaves near-zero delays.
  - Low-power (200 mV) voltage-swing table, so retraining (R4) needs Alder Lake-P's low-swing eDP values.
  - Backlight: PWM from the display engine, controller 0, 1000 Hz, minimum 16/255.
- **On this MUX laptop, brightness is owned by the embedded controller.**
  - Linux registers only `nvidia_wmi_ec_backlight` and skips `intel_backlight`.
  - The firmware carries an NVIDIA Dynamic Display Switching table with `_BCM`/`_BCL`.
  - So the Intel PWM registers may not reach the panel, and brightness under macOS may need an ACPI/EC route.
- **The firmware leaves the Intel PWM running anyway.** The macOS readout shows `BLC_PWM_CTL1 0x80000000`
  (enabled), `BLC_PWM_FREQ1 0x4B00` (1000 Hz from 19.2 MHz) and `BLC_PWM_DUTY1 0x1A2D` (about 35%). Whether
  that PWM changes this panel's brightness is the first R4 test.

## Platform

- **CPU.** i7-13620H, CPUID `0x000B06A2` (model `0xBA`), 6 P-cores + 4 E-cores, 16 threads.
- **Big Sur.** It predates hybrid CPUs. OpenCore documents what this needs:
  - a CPUID spoof to Comet Lake (`Cpuid1Data 55060A00…`, mask `FFFFFFFF…`);
  - `ProvideCurrentCpuInfo`, which supports asymmetric cores on macOS 10.14 and newer.

  Big Sur boots on the reference machine with those two settings **plus Booter `SetupVirtualMap` enabled**;
  the other Booter quirks stay at Dortania's defaults (`DevirtualiseMmio`, `RebuildAppleMemoryMap` and
  `SyncRuntimePermissions` on). Without `SetupVirtualMap` the firmware hangs during the hand-off, before the
  kernel starts: the screen stops at `EXITBS:START`, on Big Sur and Sequoia alike. Once booted, macOS starts
  all 16 threads and runs XCPM.
- **Lilu 1.7.2** does not list model `0xBA`; `lilucpu=17` selects RaptorLake. WhateverXe itself never reads
  the CPU generation.
- **ACPI.**
  - Processors are `ACPI0007` devices, so macOS needs legacy `Processor` objects (SSDT-PLUG-ALT), one per
    enabled MADT entry.
  - `AWAC` and the legacy RTC are switched by the NVS field `STAS` (SSDT-AWAC).
  - The EC is `\_SB.PC00.LPCB.EC0`; the dGPU is `\_SB.PC00.PEG1.PEGP`.
  - VMD is enabled by default and must be turned off before installing macOS.
- **Root patch.** The OCLP fork's Xe root patch selects GPUs by device ID (Tiger Lake and Alder Lake-N only so
  far) and build `20G1443`. It does not check the Mac model.
- **Recovery download.** Apple's recovery server answers the Big Sur board ID only in `-os latest` mode;
  `-os default` returns HTTP 403. The image it serves is the 11.5.2 (20G95) recovery.

## Raptor Lake-HX is a different case

HX laptop CPUs use the desktop Raptor Lake-S die. Their iGPU (`A788` and relatives) is display version 12, the
Tiger Lake generation the donor framebuffer was written for. Reports of acceleration on HX machines therefore
say little about Raptor Lake-P, whose display version 13 is the hard part.

## Reproducing

- [`tools/rplp-capture.sh`](../tools/rplp-capture.sh) makes the Linux reference capture.
  - It is read-only; dmidecode serials are redacted; the Windows product-key table (MSDM) is skipped.
- [`tools/rplp-decode.py`](../tools/rplp-decode.py) decodes the macOS R1 readout (`-wex-rplp-observe`) and
  compares it with a capture.
  - With this machine's fuses, a simulated readout already matches all five fuses and the EU and DSS totals.

### Native boot: display interrupt enable refused the firmware's Type-C hot-plug enables (2026-10-15)

With the panel path bound on A7A8, the boot handoff validated every step except the display interrupt
enable, so the power bridge faulted on its first operation and refused the driver's later power requests;
the panel stayed dark while the driver retried (the backlight visibly cycled). The IRQ module's record
named the cause: `ForeignIRQ` at `0x4447c` with bits `0x30003`.

- `0x44470`–`0x4447c` is the north display hot-plug bank (Linux `GEN11_DE_HPD_ISR/IMR/IIR/IER`). On
  Display 11+ it carries Type-C hot-plug (`GEN11_TC_HOTPLUG`, bits 16–21) and Thunderbolt hot-plug
  (`GEN11_TBT_HOTPLUG`, bits 0–5) in addition to the PCH SDE bank (bits 24–29).
- `0x30003` = TC1|TC2|TBT1|TBT2: the two Type-C ports this laptop's VBT lists. The firmware leaves their
  enables set. The reference Alder Lake-N machines have no Type-C ports, so this bank was always clean.
- The native policy already excludes Type-C in the PCH bank (`excludedPCH`, verified write) but had no
  counterpart for the north bank, so the scan for a foreign interrupt owner tripped on the plugin's own
  policy.
- Change: derive the north-bank exclusion from the PCH one (`hpdExcluded`), tolerate those bits in the
  foreign-owner scan, and mask them with the same write-and-verify as the PCH bank. Host tests cover the
  masked case, the no-policy refusal, and an unrelated enable in that bank.

Measured on the same boot, for later stages: the handoff path pipe is 0 with the attach state `2` (as on
the HDMI reference), the plane snapshot matched the R1 readout, the donor's own `start()` returned true,
the panel's EDID was read and a 1920×1080 mode attached, and the MMIO uncached preflight passed all nine
conditions. Pending hardware validation of the change.

### Native boot: a declined CDCLK stop was treated as a clock fault (2026-10-16)

With the interrupt enable fixed, the whole boot handoff passed (boot path adopted, framebuffer start
accepted, 299 power operations without error, EDID read, a mode attached). The panel stayed dark and the
backlight cycled four times. The plugin's own records placed the next gate in its clock policy:

- At power-operation 86 (~27 s) the native driver asked to stop CDCLK while pipe A and transcoder A were
  still active (guard trace: `TRANSCONF A 0xc0000000`, `TRANS_DDI_FUNC_CTL A 0x8a000106`). The guard
  declined (`ActiveDividerChange`, no writes), which is correct: a stop under a live pipe ends scanout.
- The wrapper then latched `clockFaulted` on that decline, and the latch refused every later clock
  calculation, every PCODE mailbox request (the last record shows the CDCLK voltage request, command 7,
  not accepted) and the watermark programming. On the HDMI reference path a stop is only requested with
  the pipes already off, so the latch never fired there; the internal panel keeps its pipe active across
  these calls.
- Change: a change declined before any register write (`Display13Clock::declined`) is recorded, not
  latched; a stop is declined up front when a consumer is active, without quiescing interrupts for the
  guard. The firmware's 179.2 MHz CDCLK is valid for both the 144 Hz and the EDID's 60 Hz timing, and the
  set path's ratio-only crawl under active pipes is unchanged.

Pending hardware validation. Not yet explained: the four backlight cycles, and what the driver does after
its clock request on this path.

Addendum (2026-10-16): the decline rule above was applied to the stop wrapper and the multi-output set
wrapper only. The internal panel uses the single-output set wrapper, which still latched on any result
other than Complete; a divider change (179.2 MHz is ratio 14 / divider 3, the requested 652.8 MHz is
ratio 34 / divider 2) under the live pipe is a decline there too. With that wrapper fixed, the watermark
inputs record confirms the watermark failure was downstream of the latch (`clockFaulted` was its only
false precondition). The wrapper now also keeps its first non-Complete result, because later calls were
overwriting the only record of what happened.
