# vita-luna: Project Status & Resume Guide (Phase 5)

> [!NOTE]
> This document provides the complete technical status, hardware configuration, verified deployment workflows, and Phase 5 implementation roadmap for **vita-luna** (Amazon Luna client for PlayStation Vita). Any developer or AI assistant can use this file to immediately resume development.

---

## 1. Project & Repository Information
- **Repository**: `https://github.com/jlafitte/vita-luna`
- **Main Branch**: `main`
- **Target Platform**: PlayStation Vita (ARM Cortex-A9, PowerVR SGX543MP4+, 960x544 native screen)
- **Toolchain**: VitaSDK (`$VITASDK/share/vita.toolchain.cmake`)
- **Title ID**: `LUNA00001`
- **Output Binary**: `build/vita-luna.vpk` & `build/vita-luna.self` (hot-reloaded as `eboot.bin`)

---

## 2. Hardware Deployment Setup
- **PlayStation Vita IP**: `192.168.0.7` (Reserved DHCP)
- **VitaCompanion Version**: v1.06
  - **Port 1338**: Command Socket (`launch <TITLEID>`, `kill <TITLEID>`, `reboot`)
  - **Port 1337**: Embedded Background FTP Daemon (`FTPVita Server`)

### Automated 1-Command Build & Hot-Reload
From the project root:
```bash
./build.sh 192.168.0.7
```
**Actions performed automatically**:
1. Builds `vita-luna.vpk` using VitaSDK.
2. Sends `kill LUNA00001` to VitaCompanion (Port 1338).
3. Connects directly to VitaCompanion's native FTP daemon on Port 1337.
4. Hot-reloads `eboot.bin` directly to `ux0:/app/LUNA00001/eboot.bin` in ~0.5s.
5. Syncs LiveArea metadata to `ux0:/app/LUNA00001/sce_sys/` and `ur0:/appmeta/LUNA00001/`.
6. Sends `launch LUNA00001` to Port 1338 to launch on the PS Vita screen.

### Crash Dump Auto-Retrieval
If a crash ever occurs, pull the crash dumps directly to your desktop:
```bash
python3 scripts/fetch_crash_dumps.py 192.168.0.7
```

---

## 3. Completed Phases Summary

### 🟢 Phase 1: Foundation & Graphics Engine
- `vita2d` 60 FPS graphics engine ([src/graphics/renderer.cpp](file:///Users/john/Developer/vita-luna/src/graphics/renderer.cpp)).
- Dark theme UI cards with contrast overlays and smooth scrolling catalog ([src/ui/ui_manager.cpp](file:///Users/john/Developer/vita-luna/src/ui/ui_manager.cpp)).
- Real-time diagnostic performance overlay toggled via **(SELECT)** ([src/ui/diagnostic_overlay.cpp](file:///Users/john/Developer/vita-luna/src/ui/diagnostic_overlay.cpp)).
- CPU boosted to 444 MHz on startup ([src/main.cpp](file:///Users/john/Developer/vita-luna/src/main.cpp)).

### 🟢 Phase 2: Authentication & Token Persistence
- Login with Amazon (LWA) OAuth 2.0 Device Flow ([src/auth/auth_manager.cpp](file:///Users/john/Developer/vita-luna/src/auth/auth_manager.cpp)).
- Client ID: `amzn1.application-oa2-client.654d82afa2634d8ebe01cce9975a367c`.
- Real endpoints: `create/codepair` & `token` polling (`amazon.com/us/code`).
- Session tokens saved to `ux0:data/vita-luna/tokens.dat` with auto-restore on boot.

### 🟢 Phase 3: Hardware Video & Audio Subsystems
- **Hardware AVC/H.264 Video Decoder** ([src/video/h264_decoder.cpp](file:///Users/john/Developer/vita-luna/src/video/h264_decoder.cpp)): Native hardware-accelerated decoding via `SceVideodec` (`SCE_SYSMODULE_AVCDEC`). 720p @ 60 FPS profile.
- **Audio Output** ([src/audio/audio_manager.cpp](file:///Users/john/Developer/vita-luna/src/audio/audio_manager.cpp)): `SceAudio` 48kHz stereo PCM with 16-byte aligned DMA buffers to prevent bus faults.
- **WebSocket Signaling Client** ([src/net/websocket_client.cpp](file:///Users/john/Developer/vita-luna/src/net/websocket_client.cpp)): Session signaling abstraction for `wss://stream.luna.amazon.com/webrtc/v1/session`.

### 🟢 Phase 4: Controller Input Mapping & RTP Depayloading
- **Controller Input** ([include/input/input_manager.h](file:///Users/john/Developer/vita-luna/include/input/input_manager.h), [src/input/input_manager.cpp](file:///Users/john/Developer/vita-luna/src/input/input_manager.cpp)):
  - Full `SceCtrl` button polling mapped to Luna Gamepad API.
  - Dual analog sticks with deadzone normalization.
  - Rear touch pad quadrant mapping (top quadrants $\rightarrow$ L2/R2 triggers `0x40`/`0x80`; bottom quadrants $\rightarrow$ L3/R3 stick clicks `0x400`/`0x800`). Verified 100% on hardware.
  - 60Hz JSON/binary input frame serializer.
- **RTP Transport** ([include/net/rtp_receiver.h](file:///Users/john/Developer/vita-luna/include/net/rtp_receiver.h), [src/net/rtp_receiver.cpp](file:///Users/john/Developer/vita-luna/src/net/rtp_receiver.cpp)):
  - Non-blocking UDP sockets on video (5004) and audio (5006).
  - RFC 6184 H.264 depayloader (single NAL and FU-A reassembly with `0x00000001` start codes) feeding `H264Decoder`.
  - Audio depayloader feeding `AudioManager`.
  - Packet loss percentage, jitter, and dynamic bitrate tracking.
- **Live Stream Canvas Telemetry**: Real-time stick coordinates, trigger states, button hex bitmasks, and RTP packet metrics displayed on-screen.

---

## 4. Next Step: Phase 5 Implementation (Real Cloud Video & Signaling)

When resuming, the immediate goals for **Phase 5** are:

1. **WebRTC Signaling Handshake**:
   - Authenticate with Amazon Luna session endpoint (`wss://stream.luna.amazon.com/webrtc/v1/session`) using cached LWA token.
   - Send initial session offer and process SDP answer.
   - Exchange ICE candidates to establish peer-to-peer UDP media flow.

2. **Video Texture Blitting**:
   - Take decoded frames from `SceVideodec` (YUV420/RGBA buffers) and map them to `vita2d_texture` / `SceGxmTexture`.
   - Render the live 60 FPS 720p stream scaled down to the PS Vita's 960x544 screen in `STATE_STREAM_ACTIVE`.

3. **Audio Streaming**:
   - Pipe incoming audio packets directly to `AudioManager` PCM ring buffer.

4. **Dynamic Stream Tuning**:
   - Adjust bitrate limits (5, 10, 15 Mbps) dynamically based on RTP loss telemetry.
