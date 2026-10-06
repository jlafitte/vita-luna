# vita-luna: Amazon Luna Client for PlayStation Vita

`vita-luna` is a native PlayStation Vita homebrew application designed to stream games from **Amazon Luna** using hardware-accelerated H.264 video decoding (720p 60 FPS downscaled to 960x544).

---

## Features (Phase 1 Scaffold)
* **Native Vita UI**: Built with `vita2d` / `vitaGL` rendering at 60 FPS.
* **Performance Telemetry HUD**: Real-time FPS, stream resolution, target bitrate, and hardware decoder latency monitoring (Toggle with `SELECT`).
* **Hardware Profile Tuning**: Locked 720p stream request profile with selectable bitrate targets (5 / 10 / 15 Mbps).
* **Clock Frequency Boost**: Automatically boosts Vita ARM CPU to 444 MHz for minimal stream decoding latency.

---

## Building

### Prerequisites
* [VitaSDK](https://vitasdk.org/) installed and set in your environment (`$VITASDK`).

### Build Instructions
```bash
mkdir build
cd build
cmake ..
make
```

This generates `vita-luna.vpk` ready for installation on PlayStation Vita via VitaShell.

---

## Security Verification Plan
- Automated static security scanning (`run_security_scanner`).
- Input validation and secure session memory handling.
