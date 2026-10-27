# Behringer KICK (BE 0700-AAM): source-code teardown and video handover

Written 27 Oct 2026 by a Claude Code cloud session for David. It stands alone: you don't need the chat
that produced it. It covers what Behringer's published KICK source reveals about the hardware, which
files and lines back each claim, what's still uncertain, and the state of the video, slides and site note.

**Headline:** the release has no camera or gesture code at all. The rest of the hardware can be
identified from the kernel patches and the app's hardware layer.

## 1. Source reviewed

- Repo: <https://github.com/MusicTribeOpenSource/0700-AAM>, `main` at `27194180` (14 commits, 14–15 Oct 2026).
  Unmerged branches (`codeowners-team`, `pin-buildroot-ci-image`, `product-page`) were checked on 27 Oct; none of them names a chip.
- Layout: `Kick/` (JUCE 7.0.12 app, AGPL-3.0), `AudIO/` (audio engine, Airwindows effects, AGPL-3.0),
  `ComProtocol/` (panel packet protocol, AGPL-3.0), `Linux/` (Buildroot tree, GPL-2.0).
- It doesn't build as released: samples, the Myriad Pro font, the aarch64 SDK and `juce_serialport` were removed.
  The Linux image is a dev build with a passwordless root account and Dropbear SSH enabled.

Abbreviation used below: `P/` = `Linux/buildroot/board/freescale/imx8mm_var_som/`.

## 2. Hardware findings, with sources

| Part | Finding | Where (file:line) |
| --- | --- | --- |
| Main computer | Variscite DART-MX8M-MINI module, NXP i.MX 8M Mini (quad Cortex-A53); kernel from `varigit/linux-imx`; device tree is Variscite's Symphony eval-board DTS, patched | `Linux/buildroot/configs/KTKick_defconfig:13`; first README in commit `04ee49e4`; removed in `46f10751` |
| Display | LVDS panel set to 1024×600 at 33 MHz. Comment: "tried 1366 & 1000 as well.. 1024 works.." | `P/patches/linux/001.imx8mm_dtsi.patch:181-182` |
| Touch | Goodix GT911 at I²C 0x14, but `touchscreen-size` left at 800×480 (mismatch) | `001.imx8mm_dtsi.patch:220-237`; `P/patches/linux/003.goodix.c.patch` |
| Audio codec | Coolaudio V4220M (Music Tribe's chip brand; marketed as CS4220-compatible) replaces Variscite's WM8904. I²S on SAI2, MCLK 12.288 MHz | `001.imx8mm_dtsi.patch:106-165` (`sound-v4220m`, `&sai2` with `assigned-clock-rates = <12288000>`) |
| Codec driver hack | No Linux driver for V4220M, so the `linux,bt-sco` dummy codec is reused with a WM8804 rate table (32–192 kHz), `channels_max = 2`. ASoC core error returns commented out | `001.imx8mm_dtsi.patch:163-165`; `P/patches/linux/002.fsl_sai_codec.patch:12,26,36` and the `soc-core.c` hunk |
| Codec GPIOs | Reset on GPIO4_24, mute on GPIO4_25 (as gpio-leds) | `001.imx8mm_dtsi.patch:26-37` |
| ALSA | 48 kHz, S32_LE | `P/rootfs_overlay/etc/asound.conf:17` |
| RTC | NXP PCF85263 at 0x68 (replaces DS1337) | `001.imx8mm_dtsi.patch:245` |
| Panel MCU | Separate microcontroller on `/dev/ttymxc0` (UART1) at 230,400 8N1; firmware `Kick_MCU.hex` flashed from Linux | `Kick/modules/input_events/input_events.cpp:46-60`; `Kick/modules/hardware/source/kick_FirmwareUpdate.h:97` |
| Panel protocol | CRC-framed packets; types system, button, LED, pad, knobs, MIDI, CV, slider. **No camera type** | `ComProtocol/PktMgr/PktMgr.hpp:36-43` |
| Controls | 16 pads (RGB, aftertouch), banks A–H, Q-Link knobs, encoder; buttons Full Level, 16 Levels, Note Repeat, Prog Edit, Track/Pad Mute, Next Seq, Erase, Overdub, TC (Akai MPC One vocabulary) | `Kick/modules/contracts/dcl/dcl_contracts.h` (e.g. `:135` Q-Link, `:198` 16 Levels) |
| Touch strip | Slider packet with touch input plus bar/level/individual LED modes; not on the README feature list | `dcl_contracts.h:246` onward |
| CV DAC | 8-channel 12-bit DAC on `/dev/i2c-3` at 0x0C; writes `0x3n` + 12-bit left-justified, readback `0x9n`; 0–5 V, calibrated per volt | `Kick/modules/hardware/source/kick_I2c.cpp:34,140,166,334`; `kick_I2c.h:55,83,86` |
| CV jacks | Four stereo jacks labelled 1/5, 2/6, 3/7, 4/8 (rear photo `docs/images/kick-rear.webp`). The UI lists CV-1…CV-8, each assignable to pitch/gate/mod wheel/velocity; the driver maps ports 1–4 → DAC 4–7 and 5–8 → DAC 0–3 | `Kick/modules/kick_ui_common/source/kick_common_defs.h:69`; `kick_I2c.h:83`; `Kick/modules/engine/audio_program/source/mt_audio_cv_program.h` |
| Wireless | Broadcom BCM43xx Wi-Fi + Bluetooth on the module; app has a Wi-Fi page (`iwlist`, `wpa_supplicant`) | `KTKick_defconfig:80`; `P/linux-configs/Linux-defconfig:252`; `Kick/modules/hardware/source/kick_WifiFirmware.cpp:94,190` |
| USB / updates | USB FunctionFS "PC mode"; SWUpdate for Linux and MCU from PC, SD, USB stick or OTA | `kick_UsbFunctionFS.*`; `kick_Hwcommon.h` |
| Camera | OV5640 and UVC drivers are stock Variscite kernel config only. No camera node in the DT patches, no protocol message, no app code | `Linux-defconfig:364,371,373`; grep of `Kick/` and `AudIO/` for camera/v4l2/gesture/opencv/tflite finds only SDL's touch gestures |

The i.MX 8M Mini has no NPU (the 8M Plus does), so on-device hand tracking would need an AI chip in
the camera module, or would have to run on the A53 cores.

## 3. Commit-history findings

| When (2026, CEST) | Commit | What changed |
| --- | --- | --- |
| 14 Oct 14:10 | `04ee49e4` | Initial release. READMEs call it "an MPC-style groove box / sampler that was not finished… development has stopped… not maintained" |
| 14 Oct 14:49 | `c9160a62` | "Shorten README descriptions" deletes every status line |
| 15 Oct 13:17 | `508ca0ec` | Product photos and key features; "AI camera" first appears |
| 15 Oct 13:20 | `46f10751` | "Remove processor and module details from READMEs" deletes "NXP i.MX8M Mini (Variscite DART-MX8M-MINI)" |
| 15 Oct 23:55 | `02b9e346` | Renames `az01` storage/overlay units to `kick-storage`; replaces disk UUIDs |

- The replaced UUIDs (e.g. `62cc7d66-bdec-11eb-…`) are version-1 UUIDs, and their timestamps decode to **26 May 2021, ~06:34 UTC**.
- `version.txt` = 0.3.3 in every app. Copyright headers cluster in 2021 (314 files) and 2024 (188).
- Kernel patch comments are signed "setu"; most release commits carry an AI-assistant co-author trailer.

## 4. Open items and inferences (keep hedged on camera)

- **Panel MCU = GigaDevice GD32?** The only evidence is `EN_SYS_RESET_GD` (`dcl_contracts.h:73`).
- **CV DAC = Analog Devices AD5671R?** The command nibbles 0x3/0x9 and address 0x0C fit the nanoDAC+ family. Confirm against ADI's datasheet.
- **V4220 ↔ CS4220 compatibility:** from search summaries only; Synthtopia, Synth Anatomy and the SDIY wiki didn't load from the cloud container.
- **Which half of each CV jack is on the tip:** not in the code.
- **What was az01:** only shown as an earlier codename for this Linux image.
- **AGPL angle:** if the shipping firmware does gesture control, this release isn't the complete corresponding source.

## 5. Video deliverables (state on 27 Oct)

| Item | Where | State |
| --- | --- | --- |
| Script | <https://claude.ai/artifact/RQtTZE29YPaf8HD6oWqURy> | Complete: cold open, 4 segments, outro with "Uli, send me one", chapters, B-roll, fact-check list |
| Slides | <https://claude.ai/artifact/QRSEED6roSesu91An7T8dJ> | 12 slides, script in speaker notes; **layout not checked visually** |
| Short note | <https://claude.ai/artifact/Lrdahd1tVnLvqWJd3BUr4r> | Same content as the site note below |
| Chat handover doc | <https://claude.ai/artifact/2HRGscdDXsywYc62vhxdDg> | Superseded by this file |

All four are private claude.ai artifacts on David's account.

## 6. Site note for eight4awish.com (not yet published)

Nothing has been committed to `Eight4aWish/eight4awish`. To publish:

1. Save the block below as `src/content/notes/behringer-kick-source.md`.
2. `npm ci`. In the cloud container `sharp` was missing; `npm i --no-save sharp` fixed it.
3. `npm run build` builds `/notes/behringer-kick-source/` and updates `docs/index.html` and `docs/notes/index.html`.
4. The build re-encoded `docs/opt/amyboard_flat.og.jpg`, `docs/opt/tut_music.og.jpg` and their `public/opt/` copies. `git checkout` those so the commit carries only the note.
5. Commit to `main` and push (per that repo's CLAUDE.md), or use `npm run deploy` after step 4.

```markdown
---
title: What Behringer's KICK source code reveals
date: 2026-10-27
eyebrow: Teardown
summary: >-
  Behringer published the full source for the KICK groove box. I read every
  file and all 14 commits. The hardware is clearly identifiable, but there is
  no camera code in the release at all.
chips:
  - Behringer KICK
  - Source code
  - Teardown
repo: https://github.com/MusicTribeOpenSource/0700-AAM
ask: >-
  Uli, you've published the code and I've read all of it. The only way to
  find out what that camera really does is to put a KICK on my desk. Send me
  one and I'll build the firmware from this repo, test all eight CV outputs
  and go looking for the gesture code on real hardware. If you know which
  panel MCU or CV DAC this is, or what "az01" was, please get in touch.
draft: false
---
## No AI camera in the code

On 14 October Behringer put the source for the KICK (BE 0700-AAM) on GitHub: the JUCE app, the audio engine, the panel protocol and a complete Buildroot Linux image. The headline feature is an AI camera for gesture control. There is no code for it.

- Music Tribe's own code never mentions a camera or gesture, in any of the 14 commits. The only "gesture" hits are in SDL's touchscreen library.
- The panel protocol has message types for system, buttons, LEDs, pads, knobs, MIDI, CV and a slider. There's no camera type.
- The device-tree patches add no camera. The OV5640 and USB webcam drivers in the kernel config are Variscite's stock defaults.
- The i.MX 8M Mini has no neural accelerator; the 8M Plus does. Hand tracking would need its own AI chip in the camera module, or would have to run on the CPU.

Either this code predates the camera feature, or the camera lives in firmware they haven't published. The second case matters: under the AGPL, if the shipping firmware does gesture control, this release isn't the complete source.

## The hardware the code reveals

| Part | What the code shows |
| --- | --- |
| Main computer | Variscite DART-MX8M-MINI module, NXP i.MX 8M Mini, Buildroot Linux, JUCE app |
| Panel controller | Separate MCU on UART1 at 230,400 baud, flashed from `Kick_MCU.hex`. A command named `EN_SYS_RESET_GD` hints at a GigaDevice GD32 (unconfirmed) |
| Audio | Coolaudio V4220M stereo codec (Music Tribe's own chip brand), 12.288 MHz clock, 48 kHz. Linux drives it through a hacked dummy codec capped at two channels |
| CV / gate | 8-channel 12-bit I²C DAC at 0x0C, 0–5 V. The command set matches Analog Devices' AD5671R. Four stereo jacks labelled 1/5 to 4/8, two outputs each |
| Display and touch | 1024×600 LVDS panel. The Goodix GT911 touch controller is still set to 800×480 |
| Other | PCF85263 clock chip, Broadcom Wi-Fi and Bluetooth (not on the spec sheet), USB-C "PC mode", updates from SD, USB, PC or over the air |
| Controls | 16 RGB pads with aftertouch, banks A–H, Q-Link knobs, encoder and a touch strip. The button names (Full Level, 16 Levels, Note Repeat, Prog Edit, Track Mute, Next Seq) follow the Akai MPC One |

My favourite comment in the repo is on the screen timing: "tried 1366 & 1000 as well.. 1024 works..".

## What the commit history shows

- The first README called the KICK "an MPC-style groove box / sampler that was not finished" and said development had stopped. A commit 39 minutes later deleted those lines.
- On 15 October a commit titled "Remove processor and module details from READMEs" deleted the chip line. The build config still names the board.
- The last commit renamed leftovers from an earlier codename, "az01". The disk IDs it replaced are version-1 UUIDs with a timestamp built in, and they decode to 26 May 2021.
- The app is version 0.3.3, with copyright headers from 2021 to 2024. It reads as a prototype that was dusted off for launch.

The GD32 and AD5671R identifications come from command names and command bytes, not part numbers, so treat them as strong guesses. Everything here is from the public source and history as of 27 October 2026; the final hardware may differ.
```

## 7. Decisions for David

- [ ] Publish the site note as is, or polish it first (in the Mac session)?
- [ ] Add a graphic to the note? It currently has none; the repo's product photos are Music Tribe's.
- [ ] Say "AD5671R" and "GD32" on camera as "likely", or leave the part names out?
- [ ] Keep a `videos/<slug>/` folder convention in eight4awish for scripts and slide links? (Proposed; not created.)
- [ ] Have the slides checked visually before recording.
