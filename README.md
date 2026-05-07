# VTracer

Single-exe Windows desktop app that talks to a Nidek LT-980 lens tracer over a virtual COM port, traces a glasses frame, and matches it against a folder of brand catalogs (CSV) within a configurable tolerance on the A and B box dimensions.

- Backend: C++20, Win32 + Microsoft Edge WebView2 (no Qt). Frameless dark window.
- Frontend: Vue 3 + TypeScript + Tailwind v4, embedded inside the .exe.
- Catalogs: any number of `*.csv` files in `<exe-dir>\catalogs\`. Each file's first row is its brand. Brand for a model is resolved per-row.
- Built-in **simulator** so the UI works without the device.

## Run-time requirements (the user's PC)

To run a built `VTracer.exe`:

1. **Windows 10 64-bit (1809+) or Windows 11.** 32-bit Windows is not supported.
2. **Microsoft Edge WebView2 Runtime.** Pre-installed on Windows 11. On Windows 10, download the *Evergreen Standalone Installer* from https://developer.microsoft.com/microsoft-edge/webview2/ and run it once.
3. **Visual C++ Redistributable for VS 2022.** Pre-installed on most up-to-date Windows machines; if VTracer fails to start with a missing-DLL error, install it from https://aka.ms/vs/17/release/vc_redist.x64.exe.
4. **USB driver for the LT-980.** Plug the device in once on a machine that has the OEM software installed - or install the standard *Silicon Labs CP210x* / *FTDI* driver, depending on which serial chip the tracer uses. After that the device shows up as a COM port (`Geraete-Manager` -> `Anschluesse (COM & LPT)`).

That's everything you need at runtime - no .NET, no Java, no Visual Studio.

## Build-time requirements (developer machine)

1. **Visual Studio 2022 Build Tools (or Enterprise/Community)** with the *Desktop development with C++* workload.
2. **CMake** (https://cmake.org/download/, *Add to PATH*).
3. **Node.js LTS** (https://nodejs.org/).
4. **WebView2 SDK headers + lib**: download the NuGet at https://www.nuget.org/packages/Microsoft.Web.WebView2, rename `.nupkg` to `.zip`, extract to `D:\GitLab\Tracing\third_party\webview2\`. The folder must contain `build\native\include\WebView2.h`.

## Build

Open *Developer Command Prompt for VS 2022* and run:

```bat
cd D:\GitLab\Tracing
build.bat
```

Output: `build\Release\VTracer.exe` plus a `catalogs\` folder copied from `data\`.

In CLion, use the **Visual Studio 2022** toolchain. CLion 2025.3.1 does not yet source the VS 2026 *Preview* vcvars correctly; if you have both installed, point the toolchain at `C:\Program Files\Microsoft Visual Studio\2022\Enterprise` and reset the CMake cache.

## Frontend dev mode

For UI iteration without the device or backend:

```bat
cd frontend
npm install
npm run dev
```

The bridge in `useBridge.ts` falls back to a mock when `window.chrome.webview` is missing, so a normal browser can drive the UI.

## NIDEK protocol notes

LT-980 STD framing varies slightly across firmware revisions:

- `backend/src/NidekTracer.cpp :: reqKeywordFor()` picks the REQ keyword per side (`TRC` / `TRCL` / `TRCR`). Adjust if your firmware uses different ones.
- The polar outline is parsed from any of `RL=`/`RR=`, `TRCL=`/`TRCR=`, `PL=`/`PR=`, `POLL=`/`POLR=`, `PTL=`/`PTR=`, or a single shared `R=` block. If yours uses a different key, extend `firstPolar` in `tryParseResponse`.

If a real trace returns no shape, run a serial sniffer against the OEM tool, capture one trace, and tell us the byte sequence - all the variation needed lives in those two functions.

## CSV format

Drop UTF-8 or Windows-1252 CSVs into `<exe-dir>\catalogs\`. The parser handles:

- Brand row at the top (single text cell, rest empty).
- Group row (`TZ;L;;R;;...`) and column row (`Mod.;Boxbreite;Boxhoehe;Boxbreite;Boxhoehe;Einfuehrung;...`).
- Data rows: `model;A_L;B_L;A_R;B_R;intro;...`.
- Both `.` and `,` as decimal separator.
- Empty rows and free-text comment rows are skipped.
- Rows where L and R differ by more than 2 mm are flagged and excluded from matching.

A simpler `Model;A;B;[intro]` layout is also accepted.

## Match rule

A frame matches when both A and B fall within the configured tolerance (default 0.50 mm) on either eye. Best in-tolerance candidate wins. Pass/fail in the wireframe and the saved CSV use this same tolerance.
