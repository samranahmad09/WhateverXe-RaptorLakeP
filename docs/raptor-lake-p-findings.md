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

### Native boot: the internal-panel modeset programs an X-tiled primary plane (2026-10-16)

With the clock declines no longer faulting, the first refusal moved to the plane: the native driver
programs the panel's primary with `PLANE_CTL 0x94000408` (RGB8888, tiling field 1 = X-tiled; the
firmware's plane was `0x94000008`, linear). The plugin's RGB control check accepted linear and Y-tiled only,
so the watermark plan stayed unavailable and the DBUF allocation write was refused as unassociated, and the
PCODE mailbox was refused behind those flags. The HDMI reference path never produced an X-tiled primary.

Linux v6.12 `skl_watermark.c`: on Display 10 and later the X-tiled blocks-per-line takes the same +1 as
linear (the no-+1 case is Gen9), and the only other X-tiled special case is a Gen9 bandwidth workaround.
On Display 13 the X-tiled watermark method is therefore the linear one the module already implements;
only the control check needed to accept tiling 1. The trace-only scanout sampler now takes the tiling mode
and addresses X tiles (512 bytes x 8 lines, stride in tiles). Yf and compressed layouts stay refused.

Pending hardware validation.

### Native boot: the watermark latch meets a real mode change (2026-10-16)

With the X-tiled primary accepted, the DBUF allocation write completed and the plane path reached the
watermark latch. The latch refused with a readback mismatch at `PIPE_WM_LINETIME_A` (`0x45270`): expected
116, the line time of the native path's target timing (1920x1080 @ 60 Hz, 144.52 MHz), observed 48, the
line time of the firmware's 144 Hz mode still running on the pipe. The plugin does not write that register:
its line-time hook stores the new value into the native path's pending CRTC record, which the native driver
programs during its own pipe update, and the latch's expected value follows the path's current timing.

So at the native driver's WM-arm step its software state already describes the next mode while the pipe
still runs the previous one. That is a mode change in flight, which the HDMI reference path never did, and
the latch treated it as a failure that faulted the watermark path and refused everything behind it.

Change: a line-time-only mismatch is a distinct `Timing` result. It is still a refusal (nothing retires,
the update stays pending, no wait), but not a fault, so the native modeset can continue and a later arm
verifies again. On a Timing result the plugin keeps the pipe's timing, DPLL and link M/N registers in
`N97FBWMLatchTimingFirst`, so the next capture shows whether the target mode was then programmed. Pending
hardware validation.

### Native boot: the arm after the Timing refusal, same frame (2026-10-16)

The Timing result worked: `N97FBWMLatchTimingFirst` recorded the first arm at sequence 134, frame 6452,
with `TRANSCONF_A` on (`0xC0000000`), the firmware's 144 Hz timing still live (line time 48, expected
116), `DPLL0_ENABLE` and `DPLL1_ENABLE` both `0xCC000000`, `PLANE_CTL_1_A` `0x94000408` (X-tiled XRGB) and
the cursor off. The boot's first fault then came from the very next arm, in the same frame and sequence:
`Display13WM::latch()` returned Conflict from one of its pre-compare checks, none of which named a register
(lastReg, expected and observed all zero, no wait), so the arm wrapper faulted the watermark path and every
later watermark setup was refused (`N97FBPipe0PrimaryWMFailureInputs`). The cursor snapshot taken at that
refusal shows `CUR_CTL_A` zero, so the cursor check passed; what remains is the primary plane switched off,
a foreign plane, a DSB enable or the native clock.

Within one frame of the Timing arm, the native mode change's natural next step is disabling the primary
plane, then the pipe, before the new timing is programmed. The latch treated a disabled plane or pipe as a
conflict and the mode change was aborted behind the fault.

Change: `Display13WM::Status::Inactive` for a pipe or primary plane that is off at the arm, a refusal like
Timing (nothing retires, the update stays pending, no wait) and not a fault. Every latch refusal now names
its register and value, including the foreign-plane, DSB-control and native-clock checks, and the wrapper's
own refusals are distinct results (`Intent`, `Queue`, `Binding`) instead of an anonymous Conflict; a
refused DBUF retire names the DDB register and the retire status. The pipe/DPLL snapshot is kept for every
refusal kind (`N97FBWMLatchTimingFirst`, `N97FBWMLatchInactiveFirst`, `N97FBWMLatchConflictFirst`; word 1
= status | pipe << 8, word 15 = `PLANE_STRIDE` | refused register << 32). Pending hardware validation.

### Native boot: Inactive tolerated; the pipe produces no frames (2026-10-16)

With the previous change three arms happened inside one frame (6317): Timing (line time 48 against 116),
then Inactive with `PLANE_CTL_1_A` read as 0 (the native driver disabled the primary plane right after
the Timing arm; no fault this time), then at sequence 178 an arm where every compare passed, the last one
`PLANE_WM_SAGV_TRANS_1_A` (0x7025C), but the frame counter (`PIPE_FRMCOUNT_A`, 0x70040) stood at 6317 for
the whole 52 ms budget: CommitTimeout, which was still a fault, and the mode change aborted behind it.

The frame counter was already standing at the first arm of this boot and of the previous one, while
`TRANSCONF_A` read enabled and active (`0xC0000000`), both DPLLs read enabled and locked (`0xCC000000`)
and the firmware's link M/N (0xA4D73 / 0x80000, the 144 Hz values) were still programmed. The firmware
ran DDI A from DPLL1 (`DPCLKA_CFGCR0` select 01, DPLL0 off); at every arm DDI A is on DPLL0 (select 00)
with DPLL0 enabled, so the native driver re-sourced the panel's clock before the first arm. The transcoder
clock select, the DPLL dividers, the DDI buffer and the link-training state were in no record.

Change: `Display13WM::Status::Stalled` for a latch whose compares passed but that saw no new frame and a
scanline (`PIPEDSL`, 0x70000) that never moved over the whole budget: the pipe is not running, which is
in flight like Timing (nothing retires, the update stays pending, no fault). A scanline wrap now counts as
a new frame even when the frame counter stands still; a moving scanline without a frame stays
CommitTimeout, a fault. The arm snapshot grew to 48 words: transcoder clock select (0x46140), DPLL0/1
CFGCR0/1, DDI_BUF_CTL A/B, transcoder DP_TP control/status (0x60540/0x60544), two PIPEDSL reads 2 ms apart
with the frame counter, PSR1/PSR2 control/status, PP_CONTROL/PP_STATUS, PIPE_MISC, TRANS_VRR_CTL, DSSM, DSB 0
control/head/tail, power wells, CDCLK_PLL_ENABLE, CDCLK_CTL and the uptime, with the pipe-bank reads taken
only while the pipe's power well is on. A Stalled result keeps `N97FBWMLatchStalledFirst`. Pending
hardware validation.

### Native boot: the transcoder has no clock; the legacy eDP bank (2026-10-16)

The wide snapshot at every arm of the 04fa17a boot reads `TRANS_CLK_SEL_A` (0x46140) as 0, `PIPEDSL_A` frozen
at line 1104 and the frame counter standing, while `DPLL0_CFGCR0/1` are 0x01C001A5/0x88 (8100 MHz DCO, PDIV 3,
KDIV 2 = 1350 MHz, HBR), `DDI_BUF_CTL_A` 0x80000002 (enabled, two lanes) and the transcoder `DP_TP_CTL`
(0x60540) 0x80000300 (normal operation). The boot handoff had required the firmware's select to name DDI A.
So the native driver re-sources and re-trains the panel link correctly, then leaves transcoder A without a
port clock, the convention of the dedicated eDP transcoder of older display generations, which was clocked
implicitly. Its pipe enable, pipe disable and transcoder-offset paths likewise address that dedicated bank
(0x6F000 transcoder, 0x7F008 PIPECONF), which does not exist on Display 13, so `TRANSCONF_A` kept reading
the firmware's 0xC0000000, link M/N stayed at the firmware's 144 Hz values, and the driver's PIPECONF polls
never saw a change. The last arm (46.6 s) shows the driver's teardown: DDI A disabled, DPLL0 off, the DDI A
clock gated, the backlight enable bit cleared. The first fault of that boot came only then, from the DBUF
layout wrapper refusing a layout while the watermark latch was merely in flight.

Change: on the panel path the register read/write hooks redirect legacy-bank traffic (0x6F000..0x6FFFF and
0x7F000..0x7FFFF) to the panel's own transcoder/pipe bank, and a write of a zero port select to the panel
transcoder's `TRANS_CLK_SEL` keeps the panel's DDI (TGL encoding, DDI + 1 in bits 31:28). Counters and the
first redirected accesses are published as `N97FBEDPRemap`. The DBUF layout wrapper keeps old slices pending
without a fault while the latch is in flight. Pending hardware validation.

### Native boot: the pipe runs at 60 Hz; the driver tears it down (2026-10-16)

With the legacy-bank remap and the clock select kept, the native driver's mode change reached the hardware:
75 redirected writes and 31 reads, beginning with the disable path's PIPECONF-off write and poll, the DDI
function off, then the pipe enable path's MSA misc and timing writes. The disable-time snapshots show
transcoder A clocked from DDI A (`TRANS_CLK_SEL_A` 0x10000000), the driver's own `TRANS_DDI_FUNC_CTL_A`
(0x8A010102, two lanes), link M/N at the 60 Hz values (0x44836 / 0x80000 = 144.52 MHz on HBR), the scanline
advancing 157 lines in 2 ms, and CDCLK raised to 652.8 MHz as the driver had asked. Nine clock-select
writes alternated between the shutdown path's clear and the light-up path's write: four complete cycles, each
enabling a correctly timed pipe on a trained two-lane link and then disabling it again, with the panel power
sequencer's backlight enable bit clear, while no plugin fault occurred at all. What the driver checks after
pipe enable, and why it fails, is not in any record yet.

Change: a trace of every register access of the panel path through the hooked access manager (uptime, caller,
remap flags; repeated identical accesses counted), frozen copies of the accesses leading up to each of the
first four plane-disable writes, and the 48-word display state read at capture time, served lazily as
`N97FBEDPTrace`; arm results and faults are stamped into the same trace. The clock-select translation is
narrowed to the light-up path's own write (the shutdown path's zero select is a real clock disconnect and
stays one). The arm snapshot is now kept for Unavailable results too. Pending hardware validation.

### Native boot: link training never reaches clock recovery (2026-10-16)

The access trace explains the four cycles. Each one runs DisplayPort link training on DDI A at two lanes, HBR: five
rounds, 130 ms apart, each programming combo PHY A's tuning words (PORT_TX DW2 group 0x3098, DW4 per lane 0x7038 with
loadgen on lanes 1 and 2, DW5 group 0x00390030 then 0x80390030, PORT_CL_DW10 0x000300c0 with lanes 2 and 3 powered
down), writing DPCD 0x103..0x106 = 0x07 (swing 3, max swing reached, no pre-emphasis) and reading DPCD 0x202..0x207 =
00 00 00 00 33 00: no clock recovery on any lane and the sink requesting swing 3 again. The driver then clears the
training pattern, finishes the light-up anyway (transcoder on with the 60 Hz timing and M/N, panel power on, backlight
PWM on) and within 10 ms disables the display: that 10 ms of backlight is the flicker. The firmware trained the same
panel at four lanes, and the machine's VBT selects the low-swing eDP table. The donor's tuning at the sink's requested
level carries the DW4 coefficients of Linux's ADL-P low-swing entry for 350 mV (cursor 0x38, post-cursor 0x07) but a
different DW2 swing select (0x6 against 0x4), and it never writes DW7 N_SCALAR, which carries the drive strength on this
PHY generation.

Change (an experiment): at the boot handoff, before the driver's first training, the plugin reads the firmware's combo
PHY A tuning (DW2, DW4, DW5, DW7 for each lane, plus PCS, CL and COMP words) into `N97FBPHYBoot`, and during training it
replaces the driver's writes to those four words with the firmware's: DW2 whole, DW4 bits 17:0, DW5 all but the
training-enable bit, DW7 bits 30:24. Lane power, loadgen and the sequence stay the driver's. Substitutions are counted in
`N97FBPHYOverride` and flagged in the trace. Pending hardware validation.

### Correction and revision before the next boot (2026-10-19)

A closer look at the donor's signal-level routine corrects the previous note: its DW4 write ORs a small table operand
into the register's existing bits 17:0, so the 0x7038 coefficients seen in the trace were the firmware's, not a table's,
and the same write clears bits 31:18 of the active lanes, dropping LOADGEN on lanes 0 and 1. Its DW2 (swing select 6,
RCOMP 0x98) and DW5 (scaling mode 6, TAP3 enabled) differ from Linux's values for this PHY (swing select 4; scaling 2,
TAP3 disabled), and it never writes DW7. Two lanes come from a bandwidth-minimum rule, with a port flag that keeps the
sink's capability count instead.

Revised experiment: DW4 takes the firmware's whole word with LOADGEN re-derived for the active lane count (lanes 1 to 3
with four lanes, lanes 1 and 2 with fewer); DW7 is written for every lane on each training round with the firmware's
N_SCALAR; group writes are expanded into per-lane firmware words; TX_DW8 joins the handoff record; and the port flag is
set at boot-path adoption so the driver keeps four lanes, the firmware's proven link, computing DPCD, DDI width, lane
power, loadgen and M/N for that count itself (`N97FBLanePolicy`). Pending hardware validation.

### Native boot: the firmware's tuning applied, still no clock recovery; the transcoder selects no DDI (2026-10-19)

The handoff snapshot gave the firmware's combo PHY A tuning: DW2 0x2098 (swing select 4), DW4 0x80287038 (cursor 0x38,
post-cursor 7, loadgen on lanes 1 to 3), DW5 0xA0290030 (scaling mode 2, TAP3 disabled, RTERM 6), DW7 N_SCALAR 0x50,
CL_DW10 with all lanes up: exactly Linux's ADL-P low-swing level-0 entry. It was substituted into the driver's training
writes 225 times, with DW7 restored every round, and the panel still answered lane status zero at every round, with the
driver still on two lanes. The arm snapshot taken after training and before the pipe enable shows the decisive register:
`TRANS_DDI_FUNC_CTL_A` = 0x02000106, enable clear and DDI select none. The disable path clears the select and the panel
path writes the word again only in the pipe enable, after training. On Display 12 and later the training pattern is
generated by the transcoder (DP_TP_CTL in the transcoder bank) and the DDI transmits only for a transcoder that selects it,
so every training attempt ran with nothing leaving DDI A, whatever the swing, tuning or lane count. Linux configures the
function word (port, mode, width, enable clear) before enabling the DDI buffer and starting pattern 1.

Change: before any DP_TP_CTL enable on the panel's transcoder, if the function word selects no port, the plugin writes it
as the pre-training configuration (DDI A, DP SST, the enabled DDI buffer's width, enable clear, other bits kept) and
records it in `N97FBDDIAssociate`. The capability-lane-count flag is re-applied at every AUX transaction, and the trace's
lead-up windows grow to 1024 entries so a whole light-up is visible. Pending hardware validation.

### Revision before the boot: the whole pre-training boundary (2026-10-19)

Three corrections from a closer look at the donor, before the association change was ever booted. The port byte used as
a lane policy is the donor's MST state flag: a mode-set step resets it, and the training-pattern helper turns it into
DP_TP_CTL's MST mode bit, so forcing it would have trained in MST mode under an SST function word. Those writes are gone.
The disable path also clears the transcoder's clock select, and the panel path restores it only after training, so the
pattern generator was unclocked as well as unrouted. And the first training pattern is written before the DDI buffer is
enabled, so the function word's width must come from the lane count the driver resolved, not from the buffer.

Change: at every DP_TP_CTL enable write on the panel's transcoder, in Linux's pre-training order, the plugin selects the
transcoder clock from DDI A when the select is empty, configures the function word (DDI A, DP SST, the resolved lane
count's width, enable clear) when it differs, and carries the sink's negotiated enhanced framing into the training
control word. Readbacks of the clock select, function word, DP_TP_CTL, DDI buffer and the port's lane, rate, MST and
framing fields are recorded at each boundary (`N97FBDDIAssociate`). Pending hardware validation.

### Native boot: the link trains and the panel lights (2026-10-20)

With the transcoder clock selected and the function word configured before the first training pattern, and
enhanced framing carried into the training control word, the native mode change completed in one pass: training
pattern 1, then pattern 4 for equalisation, idle, normal operation, no retry. The pre-training record shows the
state the plugin corrected at the first pattern (clock select zero, function word with no DDI selected). The capture
taken at 61 s shows the transcoder on with the driver's own function word, DDI A on DPLL0 with the clock selected,
DP_TP in normal operation with enhanced framing, the pipe scanning at 60 Hz, the backlight on, WindowServer flipping
to surface 0 with the cursor plane updating, and a Complete watermark latch with the DBUF retired. The panel shows
uniform noise rather than the desktop. The plane scans the borrowed boot scanout at GGTT offset 0, which is outside
the plugin's shared pool, so the pixel sampler recorded nothing, and the training exchange itself fell between the
frozen window and the live ring.

Change: the pixel sampler accepts the boot scanout below the aperture and records each sample's GGTT entry even
when the physical read helper is unavailable; the trace freezes lead-up windows at the first training pattern 1
and at the first normal-operation write, which bracket the whole exchange (DPCD rate, lane count, pattern writes,
status replies); the r3 configuration carries the read-only display-evidence boot argument that enables the
sampler. Pending hardware validation.

Addendum (2026-10-20): the user reports the noise moves like television static, which points at the link layer: the
sink is locked but cannot frame the pixel stream. The enhanced-framing bit of the training control word is now taken
from the lane-count byte the driver itself writes to DPCD 0x101 over AUX (observed on the AUX data registers), with
the port flag only as a fallback before that write is seen, and the observed byte is recorded at each boundary.

### Native boot: moving static, no cursor; the scrambler seed (2026-10-20)

The full AUX conversation of the training is in this capture. The driver writes the link rate and lane count (HBR,
two lanes, enhanced framing, matching the training control word), then the eDP configuration byte DPCD 0x10A = 0x01,
which switches the panel to the alternate scrambler seed, then downspread 0 and pattern 1 at swing level 0. The panel
reports clock recovery on both lanes, pattern 4 follows, and the panel reports clock recovery, equalisation, symbol lock
and interlane alignment, so training ends normally. Every training control word the driver writes selects the standard
scrambler on the source. Control symbols and framing are not scrambled and equalisation is judged on signal quality,
so training passes with mismatched seeds; every data symbol of the video then descrambles to noise, which is the moving
static with no cursor. The firmware never enabled the alternate seed and showed a picture. The pixel samples show the
plane's surface mapping linearly to the stolen framebuffer, the pages WindowServer writes, so memory is not the cause.

Change: the plugin observes the driver's native AUX writes and clears bit 0 of the DPCD 0x10A byte before the payload is
written, keeping the panel on the standard seed the source uses, and records the original byte. Pending hardware
validation.

### Native boot: first picture (2026-10-20)

With the panel kept on the standard scrambler seed, the native mode change produced the desktop: the in-boot capture
shows the eDP configuration byte going out as 0x00, clock recovery on both lanes after pattern 1, equalisation, symbol
lock and alignment after pattern 4, the transcoder on with the driver's function word, the pipe scanning at 60 Hz, the
backlight on, and WindowServer presenting to the stolen framebuffer with the cursor plane updating. The user reports
the login screen and desktop behaving like the stock configuration (software rendered).

Open display-side item: about 15 seconds after the desktop appeared, a watermark arm whose compares passed had its
DBUF retire refused (the retire step returned Conflict) and the arm wrapper faulted the watermark path; the picture is
unaffected but later watermark updates are refused until the cause is found.

The accelerator does not start: its kext loads, no accelerator object is created, the native probe is blocked and native
start is not allowed, because the strong-uncacheable MMIO mapping is reported not ready even though all nine of its
preflight conditions pass. That is the next milestone.

### Native boot: the accelerator starts (2026-10-20)

The merge of upstream 0.3.0 (branch `rplp-accel`) changed the outcome of the strong-uncacheable MMIO mapping on the
20G1443 kernel: its page-table bridges are now bound to the GPU's BAR page range, and the plugin's own check of the
resulting mapping passes (all nine preflight conditions, then the mapping itself). On this boot the firmware placed
the 16 MiB register BAR at 0x4010000000; an earlier boot of the same machine had it at 0x622e000000, so the plugin
takes the address from BAR0 instead of assuming one.

With that gate open the native accelerator probed the device (A7A8, revision 4), was allowed to start, and attached:
the Metal, OpenGL and video plugin names are published, WindowServer created its device, context and command-queue
objects, and the accelerator reports 1536 MB. The values it consumed for this GT are the measured ones: 64 EU, 8 native
subslices, 1 slice, 6 L3 banks, 8 EU per subslice. In the 50-second capture the host scheduler recorded about 1500
submissions and 2600 context-status events, no hang recovery ran, and the user reports a visibly smoother desktop.

The display path is unchanged from the first picture: 1920x1080 at 60 Hz, pixel clock 144.52 MHz, and the watermark
arm retired its allocation cleanly. The earlier refusal after the first picture is explained and fixed on this branch:
the latch had completed on a scanline wrap while the frame counter stood still, and retirement refused the equal
counters; the wrap is now an explicit retirement input, and a backward counter is still refused.

Not validated yet: Metal workloads beyond the desktop, sleep/wake, panel power sequencing, brightness control, 144 Hz,
external outputs, and whether the revision-4 workaround table the driver applies suits this GT C0 stepping.

### Display wake: interrupts are enabled before the pipe (2026-10-20)

Turning the internal panel off and on again (display sleep, then a key press) brought the picture back but left every
display interrupt dead, and the compositor gave up forty seconds later with a transaction hang: the accelerator's display
pipe had seen no VBlank since the display went off. The plugin's records explain the order: at wake the native driver
asks for the display interrupts first, while the pipe is still disabled (PIPECONF 0), and only then re-enables the pipe
and asks for the VBlank interrupt. The plugin's interrupt module had treated "expected pipe not running" as a topology
fault and latched itself off, so the later VBlank request was refused too. Boot never shows this because the firmware's
pipe is already running when the first request arrives.

The fix defers instead of faulting: a request with no running pipe while the expected pipe is powered is pending, and
the VBlank request after the pipe is on re-arms the whole bank (masters, pipe bank, DSB and hotplug policy) before
setting the VBlank bit. The re-test recorded the pending answer at the re-enable, one re-arm with a complete status,
interrupts and GPU submissions flowing for the following minute, no display-pipe timeouts and no compositor report. The
panel's own power-off and relink therefore work on this path; a full system sleep has not been attempted yet.

### OpenGL: the user-mode drivers judge the device by the accelerator's capabilities reply (2026-10-21)

With the accelerator running, Metal applications worked while every OpenGL application (Chess, Preview's image
adjustments, Safari's WebGL and video pages) aborted at an assertion in the OpenGL driver's compiler setup. The
deciding value is neither the IORegistry `device-id` nor the GT description: it is the device identity inside the
reply the user-mode drivers receive when they ask the accelerator for the GPU's hardware capabilities. The prepatched
OpenGL and video drivers accept Tiger Lake and Alder Lake-N identities there and nothing else, and the installed
drivers are signed as a set, so editing their device lists on disk was not an option.

The plugin answers that one question differently for A7A8: after the accelerator has filled the reply, the device
halfword is rewritten from A7A8 to 9A49 (Tiger Lake GT2); every other byte of the reply stays as the accelerator
produced it for this GPU, the IORegistry keeps the physical `device-id`, and Metal is not involved (it describes the
platform through a different query). `-wex-rplp-nocaps` restores the physical identity, and the record
`N97RPLPCapsReply` counts calls, rewrites and refusals.

First result: Chess starts and draws its 3D board, the first OpenGL rendering on this GPU under macOS, while the Metal
desktop, the Drift screen saver and a Safari video are unaffected. The board flickers while it is rotated and the
application feels slow; that rendering question is open, and the record of the first boot is still to be read.

### OpenGL and video after admission: the second boot (2026-10-21)

A second session on the same build separated the consumers. Preview's image adjustments (a 2D OpenGL layer) work.
Chess flickers while its board is rotated, and the spinning WebGL cube on get.webgl.org is drawn as vertical dashed
stripes inside the cube's outline. Meanwhile the plugin's records show nothing wrong on its own side: every
hardware-capabilities query was rewritten (no errors, no odd sizes), the GPU recovery counters stayed at zero, interrupt
and submission counters rose steadily, and the display interrupt bank reported no fault. Screen recording failed three
times in a row: the recorder crashed inside the video driver with an access to address 8 (a null object), reached from
VideoToolbox's pixel-transfer session through the GVA framework; a Safari video had played in the earlier session. The
crash reports are kept with the private captures. Both the rendering defect and the video crash are open; the
`-wex-rplp-capsadln` experiment exists to tell whether the identity presented after admission plays a part.

### Backlight: the display engine's PWM duty does not reach this panel (2026-10-21)

The firmware leaves the display engine's backlight PWM enabled: BXT_BLC_PWM_CTL1 (0xC8250) = 0x80000000, FREQ1
(0xC8254) = 17777 and DUTY1 (0xC8258) = 6701, about 38 percent. The opt-in probe (`-wex-rplp-pwm`) wrote DUTY1 = 12444
(70 percent) once the desktop's first watermark update had retired; every later pass read 12444 back, and the panel's
brightness did not change on any of four boots. The Linux capture had already named the embedded controller as the
backlight owner (the `nvidia_wmi_ec_backlight` driver bound), and the probe confirms it on the register side: the PWM
pin is not what this panel follows. Brightness control therefore has to go through the embedded controller's ACPI
interface rather than the display engine's PWM. Record: `N97RPLPBacklightProbe` = [1, attempted, applied, percent, CTL1,
FREQ1, before, written, after].

### Brightness: the embedded controller's WMI method, reached from the kernel (2026-10-21)

The firmware exposes the panel's brightness the way Windows and Linux use it: a WMI device under the root bridge whose
`_WDG` carries the GUID 603E9613-EF25-4338-A3D0-C46177516DB7 as a method, a 24-byte argument block (mode byte, value
DWORD) and three operations on the level (get, set, maximum) plus one on the source. The level is a memory-mapped mailbox
field of the embedded controller, so no EC driver is involved. `-wex-rplp-ecbl` finds that device by its `_WDG`, and on
the first boot every call succeeded from a kernel thread call: level 38, maximum 100, source 2 (the EC), a step to 53,
the restore to 38. The framebuffer writes the backlight duty register on its own during light-up (five writes, one of
them zero), and those writes, scaled by the period, are what the bridge forwards (38 percent, the firmware's ratio, where
the EC's own default had been 35).

With a PNLF device present, AppleBacklight attaches to it and the built-in display becomes an `AppleBacklightDisplay`
whose parameters include the framebuffer's backlight attribute (`bklt`, 24704 of 65535, the same 38 percent) but no
`brightness`: the panel driver publishes that parameter only for a panel it knows by name, and this BOE panel is not in
its table. The plugin now adds the generic panel curves to that table, as WhateverGreen does, so the slider can appear.

### OpenGL: readback isolates two defects (2026-10-21)

A pure-Python test (`tools/rplp-gl-verify.py`) creates an accelerated legacy context ("Intel HD Graphics TGL OpenGL Engine
(GT2 LP)", 2.1 INTEL-16.0.32) with no window, renders into a 64x64 RGBA8 framebuffer and reads the pixels back. Passing:
a fixed-function triangle over 32 alternating frames, a flat GLSL colour, a 2x2 nearest-filtered texture, a 4x multisample
resolve, a 1024x1024 shader checker compared row by row, and 16 frames each with and without a finish before readback.
Failing, deterministically: after drawing with one program and switching to a newly built second program, the second
program's first draw produced the first program's colour over the whole target (the same checker shader built later,
after fixed-function drawing, was correct); and with the depth test enabled (less), a nearer triangle drawn after a
farther one was rejected everywhere (the far colour stayed), while the opposite order was correct. The follow-up test
(`tools/rplp-gl-verify2.py`) reads the depth buffer itself and varies formats, functions, draw orders and program
switches to separate wrong depth values from wrong comparisons and to characterise the stale-shader case.

### Brightness: the slider works through the bridge (2026-10-21)

With the generic panel curves added to AppleBacklight's table, the built-in display's parameters gained `brightness`
(0 to 1024) next to the framebuffer's own backlight attribute, the Displays preference pane showed its slider, and the
panel followed it. In one session the slider produced 397 duty writes from the framebuffer; the bridge turned them into
369 level requests, coalesced into 145 calls to the embedded controller's method, none failing, and skipped 28 writes of
zero duty (the panel-off path). The brightness keys are not part of this: on this laptop they are firmware events that
reach the operating system as ACPI display notifications, which a separate component has to turn into key presses.

### OpenGL: the second readback probe narrows the depth defect (2026-10-21)

The follow-up probe (`tools/rplp-gl-verify2.py`) read the depth buffer back directly: after the clear it holds 1.0, after
the far triangle 0.75, after the near triangle 0.25, and the colours are correct whenever a readback separates the two
draws. Drawn back to back without a read in between, the nearer second triangle is still rejected (16-bit depth), and
with 24-bit, 32-bit float and depth-texture attachments created after the first one the whole target came back black,
clear included. GL_LEQUAL, an inverted GL_GREATER test, GL_ALWAYS, a masked first draw and a disabled test all pass.
Every program-switch variant with prebuilt programs passes, so the stale-shader case from the first probe is tied to
the first use of a freshly linked shader. The third probe (`tools/rplp-gl-verify3.py`) runs each case in a fresh context
and varies only the operation between the two draws (nothing, flush, finish, a colour read, a depth read), the depth
attachment type, and prebuilt versus freshly linked shaders. A diagnostic flag (`-wex-rplp-warev3`) exists to compare
the same probes with the accelerator's workaround table initialised as for revision 3.

### Brightness keys: the firmware's OS-version gate (2026-10-22)

The keys never reached the operating system because the DSDT runs its brightness-key cases, which notify the iGPU's eDP display
device with the standard ACPI codes 0x86 and 0x87, only when its OS-version variable says "Windows 2012" or newer, and that
variable is set from Windows `_OSI` strings alone (default 1000 under Darwin). The usual answer, an `_OSI` to `XOSI` rename in the
DSDT plus a table that answers the Windows strings under Darwin, makes the variable 0x07DC when the table stops at "Windows
2012", which is exactly the gate's threshold, and acidanthera's BrightnessKeys turns the notifications into key presses. With
that in place the keys change the brightness through the same chain as the slider. Nothing in the plugin is involved; the table
and the kext are EFI-side. A first boot with the broader table (answering every string up to "Windows 2022", variable 0x07DF)
reached the desktop about two minutes late, with the waiting inside the accelerator's own initialisation; the narrowed table is
being timed.

### OpenGL: isolated sequences pass; the history matters (2026-10-22)

A third probe ran each depth and shader case in a fresh context with one readback at the end and every operation between the two
depth draws (nothing, flush, finish, a colour read, a depth read), for 16-bit, 24-bit, 32-bit-float and texture depth, and the
freshly linked checker in every order tried: all 30 cases passed, on the default build and on the revision-3 diagnostic (which
turned out not to have applied). Minutes earlier on the same boot the first probe failed both of its stages as before. So the
depth and shader defects need what happened earlier in the same context; a fourth probe replays the failing history and changes
one boundary per case (unbind-before-readback, the 32-frame prelude, previous shader activity, a rebuilt framebuffer, completion
between the draws) to name the boundary.

### OpenGL: the bisection names a readback boundary; the work is missing from the returned image (2026-10-22)

The fourth probe (`tools/rplp-gl-verify4.py`) replays the first probe's exact history in a fresh context per case and changes
one boundary at a time. Checker: the control fails with the clear colour where the quad should be; removing the readback that
followed the earlier program's draw makes it pass, and so does reading the quad back while its program is still bound;
unbinding the earlier program after its readback instead of before, building the checker program first, or drawing it again
afterwards still fail; without the 32-frame prelude one of the quad's two triangles is missing instead of both. The first
probe's own picture from the same session shows the earlier stage's triangle on the clear colour, so the checker stage's clear
and quad are absent from the returned image: the earlier description of this defect as a stale fragment shader was wrong;
whether the work was dropped, rejected or read back stale is open.
Depth: without the earlier shader stages, or with the framebuffer rebuilt before the two draws, the pair passes; glFinish or a
colour readback between the draws does not repair it, a depth readback does. The accelerator's workaround-table revision (the
`-wex-rplp-warev3` diagnostic, now verified to apply) changes none of this, except one bisection case that passed once without
it and failed once with it (neither noise nor a regression on a single run each). The fifth probe (`tools/rplp-gl-verify5.py`,
revision 2: the history replayed as the exact API sequence, strict and relaxed controls, a re-read with the program rebound, the
depth read before and after the colour, a clear-only sentinel) adds a
picture and a wrong-colour histogram per case and the discriminating variants: the clear colour changed, a re-read after
completion, the quad as two triangles, a fixed-function quad, completion instead of the readback, the depth values after both
draws, the near draw with GL_ALWAYS, both triangles in one batch.

### Brightness keys without the OS masquerade (2026-10-23)

The four firmware hotkey cases that notify the display device sit behind "OS version at least Windows 2012"; rewriting the
comparand of exactly those four guards in the DSDT (two 15-byte OpenCore patches, two matches each) makes them true under
Darwin without changing any other OS-version gate. With BrightnessKeys the keys work, the slider still works, and the boot
is as fast as without the patches, where the `_OSI` to `XOSI` table had cost about twenty seconds of silent early-kernel time.

### OpenGL: after the boundary, what the readback returns (2026-10-23)

The fifth probe's second revision, run twice on one boot with identical results, replays the first probe's call sequence
exactly. After a program draw followed by a readback (a finish or a flush does the same), nothing added later is in any
readback of that framebuffer: not a clear of any colour, with or without geometry, not a program or fixed-function draw, not
a re-read at once, after glFinish or with the program rebound, not a second clear and draw. Not reading, finishing or
flushing after the first draw keeps everything correct. The exception carries the information: when the next draw is two
separate triangles, the first is in the image and the second is not, and the clear before them is not. The depth pair
behaves the same way: the farther triangle is in the image and the nearer one is not, in colour and in depth (the depth
attachment reads the farther value at every interior pixel, read before or after the colour), with the depth function set to
always pass, with the farther draw's depth write masked, with a depth clear between the draws, with a third nearer draw after
the second (both absent) and with both triangles in one glBegin; so an ordinary, correctly applied depth comparison is not
the mechanism (stale depth state or a missing depth clear are not excluded). A depth readback between the two draws still
restores the pair; one after the pair recovers nothing. There is no general "first draw succeeds" rule either: the lone
quad is absent although it is the first draw after its clear. Whether the later draws never reach the GPU, reach it with
stale state, or land where the readback does not look is the next question (`tools/rplp-gl-verify6.py`, revision 2: a
trailing throwaway, clipped or depth-rejected draw, a trailing clear, a texture upload, a framebuffer switch with and
without an allocation, vertex arrays instead of immediate mode).

### OpenGL: the completion wait and the trailing-work probe (2026-10-23)

A completion wait after every GPU submission (the accelerator personality's `WaitForComplete` set to 1, verified in the
registry and in the log on and off) did not change the two readback sequences: the same images came back, one case passed
once, probe 6 was identical, and the policy added lag and a new failure on a 1024x1024 readback surface. It is retired. The
sixth probe found no trailing operation that brings the missing quad back into the readback (a tiny triangle, a clipped
triangle, a finish, a texture upload, a framebuffer switch with or without an allocation), yet a trailing tiny triangle is
itself present while the quad before it is not, a trailing clear is present, and a quad drawn a second time is present;
under the depth test a second nearer triangle is still absent. Through client vertex arrays the picture is the same as in
immediate mode: a GL_QUADS primitive is absent, a six-vertex GL_TRIANGLES draw shows its first triangle only. The next
probe looks at the same framebuffer through a blit, a texture copy, other pixel formats and a sub-rectangle to tell a
stale readback from work the surface never received.

### OpenGL: every second look returns the old image (2026-10-23)

The seventh probe looked at the framebuffer after the failing sequence through other paths, each first validated on a good
source in a fresh context: a framebuffer blit into a second framebuffer, a copy into a texture drawn into a second
framebuffer (texture allocated before or after), BGRA and float readbacks, a sub-rectangle, and for the depth pair a
colour-and-depth blit. Every path returned exactly what the direct readback returned: the image from before the sequence,
with the blitted depth at the farther triangle's value everywhere. So the missing colour is not in the attachment that the
GPU blit, the GPU copy and the readback all reference; a stale CPU-side copy alone does not explain it. One ordering differs:
a blit made before any readback shows the clear colour and no quad, and the readback taken after it shows the same; a
readback taken first fixes the old image for every later observer. Of three small triangles drawn after the clear only the
first is present; after an absent quad, two trailing triangles are both present. The eighth probe wraps every draw in an
occlusion query, to learn whether the absent draws rasterised at all, and re-asserts one piece of pipeline state at a time
before the draw.

### OpenGL: an occlusion query around each draw makes the missing draws land; compression does not matter (2026-10-23)

With IOSurface render compression disabled in the accelerator personality (logged by the driver), the readback sequences,
Chess and WebGL were unchanged, so the device-cache compression that upstream 0.4.0 keeps off is not the cause here. The
eighth probe wrapped every draw of the failing sequences in an occlusion query (`GL_SAMPLES_PASSED`) and read the counts only
after the readback: the lone quad landed with all 4096 samples counted, two triangles landed, the depth pair came out with
the nearer triangle on top; re-asserting pipeline state (colour mask, viewport, framebuffer binding, draw buffer, scissor,
depth mask, blend) beside the query changed nothing, which with the query present neither identifies nor excludes lost
state. A finish or flush in the same places never had this effect. Under the queries one sequence still loses a draw: of
three small triangles the first counts zero samples (no sample counted as passing) and is absent while the other two are
present, the reverse of the result without queries. A query places depth-count and completion writes around the draw and,
while active, forces the render target writeable in the pixel-shader blend state. The ninth probe places empty and spanning
queries differently (around the clear, before or after the draw, deleted unread, a timer query, around one draw of several)
and, with no query at all, changes plain pixel state before the draw, to tell the two apart.

### OpenGL: the query's commands must sit before the draw; no plain state change repairs; the loss moves (2026-10-23)

The ninth probe placed the query differently around the failing sequence. A query around the quad repairs it (and the
repaired quad shows through a blit into a second framebuffer), a query around the clear that has already ended before the
quad repairs it too, an empty query before the clear makes the clear visible but not the quad, an empty query after the
quad or a warm-up query before the history changes nothing, a query deleted without reading its result still repairs, and
a timer query repairs like an occlusion query. So the repair comes from what a query's begin and end leave behind for the
next operation, not from an active query during the draw. No plain pixel state change without a query repairs anything:
the colour mask re-asserted or toggled, blending enabled with a one/zero function, the depth test or the scissor toggled,
all of them together, before the quad or before the nearer triangle of the depth pair. With queries around some draws of
a sequence, which draw is lost moves: the draw before a query begin lands in every case, the draw inside a query is
sometimes the one lost, and the same draw counts zero samples or its full area depending only on what follows it, so the
occlusion counts observe the same disturbed pipeline rather than measuring it independently. The tenth probe draws eight
small triangles in eight cells in a fixed order and records, per cell, which land under each placement of an empty or
spanning query, a depth readback, a finish or a flush.

### OpenGL: the second and third draws after the clear are the ones lost (2026-10-23)

The tenth probe drew eight small triangles in eight cells in a fixed order after the boundary and the clear, with no
queries: cells 2 and 3 are missing and the other six present, twice over, and all eight present without the history. A
finish, a flush, a depth readback, an empty occlusion query or an empty timer query between the clear and the first draw
brings all eight back and not the clear; an empty query before the clear brings the clear back and nothing else; a query
around mark 3, 4, 6 or 8 loses that mark (it then counts zero samples), around mark 5 or 7 changes nothing, so no
parity rule follows; queries around marks 1 and 8 together lose 1 and 6; queries around all eight lose only the first;
a finish after the fourth mark loses the fifth. Together with the earlier results
(two triangles lose the second, three corners the second and third, a six-vertex array draw its second triangle, a
four-vertex quad is lost whole), the simplest reading is a lost stretch of about six vertices of vertex data, starting
after the first three, in the first batch after the boundary, with a draw that straddles it lost whole and anything that
flushes or re-emits state before the first draw moving the draws out of it. The eleventh probe varies the vertex layout
(order, one glBegin, one array draw, six vertices per mark, a wider vertex, a twelve-vertex first mark, no clear, a quad
instead of the clear, drawn twice, a flush before each) to test that reading.

### OpenGL: the loss is in the vertex data (2026-10-23)

The eleventh probe varied the vertex layout of the eight marks with no queries. One glBegin and one glDrawArrays of 24
vertices each lose exactly their second and third triangles; drawn in reverse order the second and third draws are the
lost ones; a first mark made of twelve vertices loses its second and third sub-triangles and every later mark survives;
with two triangles per mark every mark is present in the final image; a per-vertex colour changes nothing; without the
clear the first mark is lost instead and the rest survive; a fixed-function quad in place of the clear lands and the
first mark after it is lost; the marks drawn twice are all present in the final image (which copy landed is not
established); a flush before each mark scatters the loss over marks 2,
5, 7 and 8. So a stretch of about six vertices of vertex data is not what the GPU reads in the first batch after the
boundary, its position moves with whatever was written into the vertex arena before it, and a draw that straddles it is
lost whole or in part. The twelfth probe maps the region on a 40-triangle grid drawn in one go, against the input
format, the draw path (a client array, one or forty glBegin, glDrawElements in storage or reverse order, a vertex
buffer object, one-pixel points, two passes in two colours), the length of the history before the boundary and what
precedes the draw, with a no-history control per path and the triangles judged separately from the background.

### OpenGL: the loss needs no history and recurs every 84 vertices (2026-10-23)

The twelfth probe drew a grid of 40 small triangles (120 vertices) through several paths, each with a no-history control.
Every no-history control failed: in a fresh context, after a plain clear, one draw of 120 vertices misses vertices 45 and
46 (drawn as points) or the triangle made of vertices 45 to 47, the same for 2, 3 or 4 components per vertex, one glBegin
or a client array, glDrawElements, and with a flush or an empty query before the draw. The earlier no-history controls
passed only because they never reached position 45. The missing set follows the draw position, not the storage position:
with the triangles drawn in reverse order through an index array, the 16th drawn triangle is the missing one. The window
recurs every 84 vertices along one draw: after the earlier probes' history the misses are at vertices 3 and 87, after a
fixed-function quad at 0 and 84, after a flush at 33 and 117, with nothing before the draw at 45. What precedes the draw
moves the window by amounts unrelated to its vertex count. Two draws of 60 vertices each miss their own 45th vertex; four
draws of 30 miss nothing; forty separate glBegin calls move the windows; the vertex-buffer-object path misses at other
positions; the second of two passes misses different triangles than the first, so a complete image drawn twice says nothing
about either pass. Separately, whether the clear itself is in the returned image depends on how many frames were read back
before it: present after 0 or 16, missing after 32 or 48. The thirteenth probe measures the window in vertex units with
1024 one-pixel points behind a varying number of clipped spacer points in one draw.
