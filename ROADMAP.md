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
- `vita2d` 60 FPS graphics engine ([src/graphics/renderer.cpp](src/graphics/renderer.cpp)).
- Dark theme UI cards with contrast overlays and smooth scrolling catalog ([src/ui/ui_manager.cpp](src/ui/ui_manager.cpp)).
- Real-time diagnostic performance overlay toggled via **(SELECT)** ([src/ui/diagnostic_overlay.cpp](src/ui/diagnostic_overlay.cpp)).
- CPU boosted to 444 MHz on startup ([src/main.cpp](src/main.cpp)).

### 🟢 Phase 2: Authentication & Token Persistence
- Login with Amazon (LWA) OAuth 2.0 Device Flow ([src/auth/auth_manager.cpp](src/auth/auth_manager.cpp)).
- Client ID: `amzn1.application-oa2-client.654d82afa2634d8ebe01cce9975a367c`.
- Real endpoints: `create/codepair` & `token` polling (`amazon.com/us/code`).
- Session tokens saved to `ux0:data/vita-luna/tokens.dat` with auto-restore on boot.

### 🟢 Phase 3: Hardware Video & Audio Subsystems
- **Hardware AVC/H.264 Video Decoder** ([src/video/h264_decoder.cpp](src/video/h264_decoder.cpp)): Native hardware-accelerated decoding via `SceVideodec` (`SCE_SYSMODULE_AVCDEC`). 720p @ 60 FPS profile.
- **Audio Output** ([src/audio/audio_manager.cpp](src/audio/audio_manager.cpp)): `SceAudio` 48kHz stereo PCM with 16-byte aligned DMA buffers to prevent bus faults.
- **WebSocket Signaling Client** ([src/net/websocket_client.cpp](src/net/websocket_client.cpp)): Session signaling abstraction for `wss://stream.luna.amazon.com/webrtc/v1/session`.

### 🟢 Phase 4: Controller Input Mapping & RTP Depayloading
- **Controller Input** ([include/input/input_manager.h](include/input/input_manager.h), [src/input/input_manager.cpp](src/input/input_manager.cpp)):
  - Full `SceCtrl` button polling mapped to Luna Gamepad API.
  - Dual analog sticks with deadzone normalization.
  - Rear touch pad quadrant mapping (top quadrants $\rightarrow$ L2/R2 triggers `0x40`/`0x80`; bottom quadrants $\rightarrow$ L3/R3 stick clicks `0x400`/`0x800`). Verified 100% on hardware.
  - 60Hz JSON/binary input frame serializer.
- **RTP Transport** ([include/net/rtp_receiver.h](include/net/rtp_receiver.h), [src/net/rtp_receiver.cpp](src/net/rtp_receiver.cpp)):
  - Non-blocking UDP sockets on video (5004) and audio (5006).
  - RFC 6184 H.264 depayloader (single NAL and FU-A reassembly with `0x00000001` start codes) feeding `H264Decoder`.
  - Audio depayloader feeding `AudioManager`.
  - Packet loss percentage, jitter, and dynamic bitrate tracking.
- **Live Stream Canvas Telemetry**: Real-time stick coordinates, trigger states, button hex bitmasks, and RTP packet metrics displayed on-screen.

### 🟡 Phase 5: Cloud Video, Audio Streaming & Signaling Handshake (In Progress)
- **WebRTC Signaling Handshake** ([include/net/websocket_client.h](include/net/websocket_client.h), [src/net/websocket_client.cpp](src/net/websocket_client.cpp)):
  - Full signaling state machine (`AUTH` $\rightarrow$ `SDP_OFFER` $\rightarrow$ `SDP_ANSWER` $\rightarrow$ `ICE_EXCHANGE` $\rightarrow$ `ACTIVE`).
  - Automated session authentication with cached LWA OAuth access tokens.
  - Generates RFC 4566 / RFC 6184 SDP offers with H.264 High Profile 720p60 and PCM audio payload specs.
- **Dedicated Media Worker & SRTP Decryption** ([src/net/rtp_receiver.cpp](src/net/rtp_receiver.cpp), [src/net/srtp_decryptor.cpp](src/net/srtp_decryptor.cpp)):
  - Dedicated background media worker thread pinned to CPU Core 1.
  - RFC 3711 AES-CTR hardware packet decryption layer.
- **Hardware Decoder Memory & CDRAM Allocation** ([src/video/h264_decoder.cpp](src/video/h264_decoder.cpp)):
  - Fixed `0x80620009` crash by mapping decoder memory into PS Vita's 128 MB CDRAM pool (`SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW`) aligned to 1 MB.
  - Decoder instantiation verified on hardware (`Hdl=0x00000001`, `Create=0x00000000`).
  - AU parameter timestamps verified (`Ret=0x00000000`).
- **Hardware Video Texture Blitting** ([src/graphics/renderer.h](src/graphics/renderer.h), [src/graphics/renderer.cpp](src/graphics/renderer.cpp)):
  - Dedicated hardware video stream texture (`vita2d_texture` in `SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_RGBA` with bilinear filtering).
  - Scaled to the PS Vita's 960x544 screen in `STATE_STREAM_ACTIVE` at 60 FPS.
- **Audio Jitter Ring Buffer** ([include/audio/audio_manager.h](include/audio/audio_manager.h), [src/audio/audio_manager.cpp](src/audio/audio_manager.cpp)):
  - 8192-frame (~170ms) stereo PCM ring buffer mitigating network packet jitter.
  - Dedicated `pumpAudio()` draining 512-frame aligned blocks directly to PS Vita DMA hardware with clean underrun silence padding.
- **Pending Verification**:
  - Verification of first decoded frame output (`Out > 0` on `sceAvcdecDecode`) upon receiving valid IDR keyframe (NAL type 5) with SPS/PPS.

---

## 4. Next Step: Complete Phase 5 (Live Video Frame Output)

1. **Verify Live H.264 IDR Frame Output**:
   - Ensure the RTP receiver properly assembles and submits initial SPS (Type 7), PPS (Type 8), and IDR (Type 5) Access Units to transition decoder output from `Out=0` to `Out=1`.
2. **Audio/Video Sync**:
   - Verify smooth 60 FPS presentation and audio sync over UDP.
3. **Phase 6 Polish**:
   - Extended streaming stability test, game catalog thumbnail caching on `ux0:data/vita-luna/cache/`.

---

## 5. Handover & Resume Notes for Next Session

### Current Working Baseline (Do NOT re-diagnose or revert)
1. **Memory Allocation**: CDRAM (`SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW`) aligned to 1 MB is strictly required for `SceVideodec`. Main RAM PHYCONT fails with `0x80620009` due to small pool size.
2. **Decoder Instantiation**: `sceAvcdecCreateDecoder` succeeds on hardware with `Hdl=0x00000001` and `Create=0x00000000`.
3. **Decode Parameters**: All uninitialized timestamps in `SceAvcdecAu` must be strictly `0xFFFFFFFF` (`au.pts.upper = 0xFFFFFFFF; au.pts.lower = 0xFFFFFFFF; ...`). Passing `0` triggers `0x80620002`.
4. **LiveArea Assets**: All PNGs in `sce_sys/` must be 8-bit indexed colormap (`pal8`). Truecolor PNGs trigger package install error `0x8010113D`. Use `magick input.png PNG8:output.png`.
5. **Multithreaded Media Worker**: Dedicated thread on CPU Core 1 handles UDP socket draining and SRTP decryption.

### Immediate Action Item when Resuming
* **The `Out=0` Roadblock**:
  * `sceAvcdecDecode` currently returns `Ret=0x00000000` (success), but `numOfOutput` remains `0` because incoming stream packets were P-frames (NAL Type 1) without an IDR reference frame.
  * In `src/net/rtp_receiver.cpp`, verify that STAP-A (Type 24) and FU-A (Type 28) keyframes correctly construct a full Annex-B Access Unit buffer containing SPS (`0x67`), PPS (`0x68`), and IDR (`0x65`) before passing to `H264Decoder::decodeNALUnit`.
  * Alternatively, test decoding a local 720p H.264 IDR Annex-B sample loaded directly from memory/file to verify `Out=1` independently of the UDP network transport.
