# MacFanCtl

Fan and temperature control for Macs running Windows via Boot Camp.

Native Win32, no runtime to install. Reads every temperature sensor the Mac's
SMC exposes, and drives the fans either at a fixed speed or from an editable
temperature curve.

Built and verified on a **MacBook Pro 15" 2017 with Touch Bar (MacBookPro14,3)**
running Windows 10 x64.

![demo](docs/demo.gif)

---

## What it does

- **Live temperature monitoring** — every sensor that reports a physically
  plausible value, with human-readable names (`TC0P` → "CPU proximity"). Dead
  keys that return the SMC's `-127` / `-128` / `-38.375` sentinels are filtered
  out automatically. 40 real sensors on a MacBookPro14,3.
- **Per-fan control**, three modes:
  - **System** — hands the fan back to the Mac's own firmware thermal loop.
  - **Manual** — a fixed RPM you pick with a slider, clamped to the fan's own
    reported minimum and maximum.
  - **Auto (curve)** — a piecewise-linear temperature → RPM curve you edit by
    dragging points on the graph. Follows either a specific sensor or whichever
    component is currently hottest.
- **Tray icon** with live temperature and fan speeds; the window closes to tray.
- **Settings persist** to `%APPDATA%\MacFanCtl\config.ini`.
- **CLI** (`macfanctl-cli.exe`) for scripting and diagnostics.

---

## Which Macs this works on

Two hard requirements: **Windows running natively via Boot Camp** (so Intel Macs
only), and the **`applesmc.sys` driver installed** with its `AppleSMC` service
running.

Nothing about the machine is hardcoded — fan count comes from `FNum`, fan names
from `F<N>ID`, speed limits from `F<N>Mn`/`F<N>Mx`, every value is decoded from
the type the SMC declares for that key, and sensors are discovered by
enumerating the key space and keeping whatever reads plausibly. So the code is
model-agnostic by construction. How much of that is *tested* is another matter:

| Tier | Machines | Expectation |
|---|---|---|
| **Verified** | MacBookPro14,3 (15" 2017 Touch Bar) | The only machine actually tested. Full monitoring + control. |
| **Expected to work** | Intel Macs ~2008–2017 — MacBook Pro / Air, iMac, Mac mini, Mac Pro | Same SMC generation: `fpe2` fan keys and the `FS!` bitmask, both exercised here. Many sensor keys are already in the catalogue; unrecognised ones still appear with generated names. |
| **Untested code path** | T2 Intel Macs, 2018–2020 (MBP/MBA 2018+, Mac mini 2018, iMac Pro, Mac Pro 2019) | The SMC moved into the T2 and fan keys are often `flt` rather than `fpe2`, and some models use per-fan `F<N>Md` instead of `FS!`. Both are implemented and branch off what the hardware declares — but neither has been run against real T2 hardware. |
| **Monitoring only** | Fanless Intel Macs (12" MacBook Retina, 2015–2017) | `FNum` = 0, so there is nothing to control. Temperatures still display. |
| **Will not work** | Apple Silicon — M1/M2/M3/M4 | No Boot Camp, no native Windows. Nothing to run against. |

On an untested model the failure mode is designed to be *safe, not silent*: if
the fan limits can't be read the fan is refused rather than driven with a
guessed range, and if no temperature is usable a curve hands the fan back to the
firmware instead of holding a stale speed.

## Requirements

**The `AppleSMC` kernel service must be installed and running.** MacFanCtl talks
to the `\\.\APPLESMC` device that service publishes; it does not ship or install
a driver of its own. Check it with:

```bash
sc query AppleSMC
```

You want `STATE: 4 RUNNING`. If the service is stopped, MacFanCtl attempts to
start it on launch (that step needs Administrator).

> **The device allows one handle at a time.** Only one program can hold the SMC
> device at once, so close any other SMC utility -- tray icon included -- before
> starting MacFanCtl. MacFanCtl enforces a single instance of itself.

---

## Building

Needs a C++17 compiler and CMake. Verified with the MinGW-w64 toolchain already
on this machine (`C:\C Compiler\mingw64`, GCC 12.2.0).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Produces `build/MacFanCtl.exe` (GUI) and `build/macfanctl-cli.exe` (CLI). Both
are statically linked — copy them anywhere.

---

## Using the GUI

Run `MacFanCtl.exe`. No elevation needed as long as the `AppleSMC` service is
already running.

The left pane lists sensors, hottest first. Each fan gets its own panel:

| Control | What it does |
|---|---|
| **System / Manual / Auto (curve)** | Selects the control mode for that fan |
| **Slider** | Target RPM in Manual mode, clamped to the fan's real limits |
| **Source** | Which sensor the curve follows; defaults to hottest component |
| **Graph** | Drag a point to move it · double-click empty space to add · right-click a point to remove |

The red dot on the graph is where the fan is actually operating right now, so
you can see the curve's effect and the fan's lag.

Right-click the tray icon for **Return all fans to system control** and **Exit**.

---

## Using the CLI

```bash
macfanctl-cli.exe info
```

```
device    : \\.\APPLESMC
protocol  : mmio
keys      : 911
fans      : 2
control   : available (FS! mask 0x0000)
  [0] Left side       3552 RPM  (min 2160, max 5927, target 3553)
  [1] Right side      3284 RPM  (min 2000, max 5489, target 3290)
```

| Command | Purpose |
|---|---|
| `info` | Device, protocol, key count, fan summary |
| `temps` | Every plausible temperature sensor |
| `sensors` | Temperatures, voltages, currents, power |
| `fans` | Fan speeds, limits, control mode |
| `keys` | Dump all 911 SMC keys with type and value |
| `get <KEY>` | Read one key, e.g. `get TC0P` |
| `set <fan> <rpm>` | Take a fan to manual at a speed |
| `auto [fan]` | Return one fan, or all fans, to system control |
| `selftest` | Non-destructive write-path check |

`selftest` engages manual mode at the fan's *current* speed (so nothing audibly
changes), verifies the readback, and restores the original state:

```
FS! before        : 0x0000
fan 0 current     : 4038 RPM (target 4025)
engaging manual at: 4038 RPM (same speed - should be inaudible)
FS! after set     : 0x0001  OK (bit 0 set)
target readback   : 4038 RPM  OK
manual flag       : OK
restoring...
FS! after restore : 0x0000  OK
```

---

## Safety

The firmware's own thermal protection stays active in every mode — the SMC will
still throttle the CPU and can still spin the fans up on its own. Manual control
sets a *target*, it does not disable thermal management. That said:

1. **Targets are clamped** to the `F<N>Mn` / `F<N>Mx` limits the fan itself
   reports. On this Mac that is 2160–5927 RPM (left) and 2000–5489 (right).
   MacFanCtl will not command a speed outside those.
2. **Fans are returned to system control on exit** — normal exit, window close,
   Windows shutdown/logoff (`WM_ENDSESSION`), and unhandled crashes (via an
   exception filter that reopens the device and clears `FS!`).
3. **Manual targets are re-asserted after sleep.** The SMC drops manual fan
   control across a suspend, so the app re-applies on `PBT_APMRESUMEAUTOMATIC`.
4. **Implausible sensor readings are ignored** rather than fed to a curve. If a
   curve's source sensor disappears, that fan falls back to system control
   rather than holding a stale speed.
5. **Curve output is rate-limited** — rises fast (thermal safety), falls slowly
   (avoids audible hunting), with a 40 RPM deadband.

### The one case that leaves fans pinned

If the process is **force-killed** (Task Manager "End task", `Stop-Process`,
power loss), Windows gives it no chance to run any cleanup, so a fan left in
manual mode stays there until something resets it. Fix it with:

```bash
macfanctl-cli.exe auto
```

This is not hypothetical — it happened during development, leaving a fan pinned
at 5048 RPM. Prefer **Exit** from the tray menu over killing the process.

---

## How it works

MacFanCtl opens the `\\.\APPLESMC` device and speaks the SMC key/value protocol
over `DeviceIoControl`. Every key is a four-character identifier with a declared
type, so the app reads the type the hardware reports for each key and decodes
accordingly rather than assuming a fixed layout. Fans are enumerated from
`FNum`, with their names and speed limits read from the hardware.

See `src/smc.h` and `src/smc.cpp` for the transport, and `src/fans.cpp` for the
fan control logic.

---

## Licence

[MIT](LICENSE) — Copyright (c) 2026 IRP_HongKong. Use it, modify it, ship it in
closed-source products; just keep the copyright notice. No warranty: this
software writes to a hardware fan controller, and you run it at your own risk.

## Layout

```
src/smc.{h,cpp}         Device transport, IOCTLs, key/value encoding
src/sensordb.{h,cpp}    Key → human name, category, plausibility ranges
src/fans.{h,cpp}        Fan discovery and control (FS! / F<N>Md)
src/curve.h             Curve evaluation, smoothing and rate limiting
src/controller.{h,cpp}  Polling thread, control policy, restore-on-exit
src/config.{h,cpp}      Settings persistence
src/curvectrl.{h,cpp}   The draggable curve graph control
src/gui.cpp             Main window, fan panels, tray icon
src/cli.cpp             Command line front end
tools/probe.cpp         Protocol validation harness
tools/dump.cpp          Full SMC key dump
tools/smc_dump.txt      All 911 keys from this MacBookPro14,3
```

## Limitations

- Only the `\\.\APPLESMC` provider is implemented, so the `AppleSMC` service
  must be present.
- Fan count, names and limits are read from the hardware, so other Mac models
  should work — but only MacBookPro14,3 has been tested.
- Newer Macs report fan keys as `flt` rather than `fpe2`. The code reads each
  key's declared type and branches, so that path should work, but it is
  untested on real `flt`-fan hardware.
