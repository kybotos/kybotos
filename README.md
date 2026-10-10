# Kybotos

**English** | [日本語](README.ja.md)

Kybotos is a platform for running sandboxed WebAssembly apps on small music devices.
Apps reach the display, touch, audio, MIDI and a musical timeline (tempo, meter, scheduled notes) only through the Host API.
The same `.wasm` runs on the ESP32-S3 device and on a Linux host.

[![Demo video](https://img.youtube.com/vi/UdiFrxvP_qg/0.jpg)](https://youtu.be/UdiFrxvP_qg)

> Most of the design notes and development records under `docs/` are written in Japanese.

## Repository layout

| Path | Contents |
|---|---|
| `shared/hostapi_defs.h` | **The Host API** (the contract between hosts and apps). See `docs/hostapi.md` |
| `src/` | ESP32-S3 firmware (ESP-IDF 5.5, WAMR 2.4): launcher, runtime, audio, MIDI, display, touch, SD card |
| `hosts/linux/` | Linux host (SDL2 / ALSA) that runs the same `.wasm` through the same Host API, for development and testing |
| `shared/` | Code shared by both hosts (e.g. the musical-timeline scheduler `seq_core.c`) |
| `wasm-apps/` | Sample apps (Rust, `wasm32-unknown-unknown`, no_std) and `appui`, a UI component library |
| `scripts/` | Regression, measurement and capture scripts |
| `docs/` | Architecture, Host API, UI conventions, roadmap, per-phase records |

The target hardware is the **Waveshare ESP32-S3-Touch-LCD-2.8** (320×240 touch LCD, I2S audio output, SD card)
with MIDI in/out wired to a UART.

The board is selected at build time (Phase 24).

| Board | Name (`KYBOTOS_BOARD`) | Status |
|---|---|---|
| Waveshare ESP32-S3-Touch-LCD-2.8 | `waveshare_lcd28` (default) | Everything |
| Elecrow CrowPanel Advance 2.8" (V1.2) | `crowpanel_adv28` | In progress: display, touch, audio (on-board amplifier), SD, serial commands (on the native-USB USB-C). MIDI follows (`docs/roadmap.md`) |

## Building and flashing the firmware

The build runs in a container image with ESP-IDF.

```
docker run --rm -v ${PWD}:/workspaces/kybotos -w /workspaces/kybotos/src \
  ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:5.5.5 \
  bash -c 'source /opt/esp-idf/export.sh && idf.py build'

DEV=/dev/ttyACM0
docker run --rm -it -v ${PWD}:/workspaces/kybotos -w /workspaces/kybotos/src \
  --device=${DEV} --group-add $(stat -c '%g' ${DEV}) \
  ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:5.5.5 \
  bash -c "source /opt/esp-idf/export.sh && idf.py -p ${DEV} flash"
```

- The commands above build the default board (Waveshare). For other boards use `scripts/fw.sh <board> build|flash|monitor`
  (each board builds in `src/build-<board>/`, and a build for another board is never flashed; `docs/workflow.md` §3.2).
- The versions of the managed components are pinned in `src/dependencies.lock`. Memory on the ESP32-S3 is tight,
  so upgrading them means re-taking the regression baselines (`docs/workflow.md` §3.2).
- On first boot, the apps embedded in the firmware (metronome and mp3player) are copied to `/sdcard/apps` on the SD card and
  listed in the launcher. The test and diagnostic apps in `wasm-apps/dev/` are embedded only when you build with
  `idf.py -DKYBOTOS_DEV_APPS=ON build` (used for the regression).
- The firmware never deletes apps from the SD card. If you update from an older firmware, apps it placed there
  (touch_demo, seq_smoke, midi_loopback, synth_probe) stay in the launcher; remove them with `rm <app>` on the serial
  console (`idf.py monitor`, then type `rm touch_demo`).

## Linux host

```
sudo apt install cmake gcc libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev libasound2-dev
cd hosts/linux
cmake -B build && cmake --build build -j
./build/kybotos_host                                           # launcher (scans ../../wasm-apps)
./build/kybotos_host ../../wasm-apps/metronome/metronome.wasm  # run a single app
```

See `hosts/linux/README.md` for details.

## Writing apps

- An app is a Rust `cdylib` built for `wasm32-unknown-unknown`. See `wasm-apps/README.md`.
- The Host API is defined in `shared/hostapi_defs.h` and described in `docs/hostapi.md`. Tap / long-press / swipe detection and
  a screen stack are in `wasm-apps/appui`; the interaction conventions are in `docs/design/ui-conventions.md`.
- Apps kept outside this repository can be embedded in the firmware by passing `KYBOTOS_EXTRA_APPS` (a list of absolute
  `.wasm` paths) at build time (`src/components/wasm_runtime/CMakeLists.txt`). The Linux host takes a `.wasm` path as its
  argument, so it can run an app from anywhere.

## Regression

Three apps are run on the device and on the Linux host: metronome and mp3player, and hostapi_check
(`wasm-apps/dev/`), which exercises the Host API and shows its verdict on screen. Scripted taps are injected through
the device's serial console and the Linux host's command FIFO, and the text on screen is read back to check each step.
Heap deltas, warnings and WAMR memory usage are checked as well
(`scripts/device-regress.sh` / `scripts/linux-regress.sh`; procedure in `docs/workflow.md`).

## License

**Apache License 2.0** (`LICENSE`, `NOTICE`).

Apps run inside the WebAssembly sandbox and call the host only through the Host API.
**We do not consider an app that merely uses the Host API to be a derivative work of the code in this repository.**
Apps may be distributed under any license their authors choose, including closed-source and paid ones.

### Third-party components

Fetched at build time by the ESP-IDF Component Manager (not included in this repository):

| Component | License |
|---|---|
| LVGL | MIT |
| esp_lvgl_port / esp_lcd_touch (Espressif) | Apache-2.0 |
| WAMR (WebAssembly Micro Runtime) | Apache-2.0 WITH LLVM-exception |
| esp-audio-player | Apache-2.0 |
| esp-libhelix-mp3 | Wrapper: Apache-2.0. The **Helix MP3 decoder** inside it: **RealNetworks Public Source License (RPSL)** |

- **If you distribute firmware binaries**, the RPSL requires a notice that the Helix source code is available under the RPSL
  (in the documentation and wherever copyright notices are shown). This repository does not currently distribute binaries.
- `hosts/linux/font8x8_basic.h` is in the public domain (Daniel Hepper).
- `src/components/wasm_runtime/assets/*.mp3` are test sounds generated for this project and are covered by this repository's license.
