#!/usr/bin/env python3
"""
Amazon Luna / PS Vita Local RTP Stream Test Generator
Streams real RFC 6184 H.264 video NALs and 48kHz PCM stereo audio
directly over UDP to the PlayStation Vita running vita-luna.
"""

import sys
import time
import math
import socket
import struct
import shutil
import subprocess

def run_ffmpeg_streamer(vita_ip="192.168.0.7", duration_sec=30):
    video_port = 5004
    audio_port = 5006

    print("============================================================")
    print(f" Starting Real Hardware H.264 & PCM Streamer to PS Vita ({vita_ip})")
    print(f" Video: {vita_ip}:{video_port} (H.264 720p60 Baseline via libx264)")
    print(f" Audio: {vita_ip}:{audio_port} (48kHz Stereo PCM S16LE)")
    print(f" Duration: {duration_sec}s (Press Ctrl+C to stop)")
    print("============================================================")

    duration_args = ["-t", str(duration_sec)] if duration_sec > 0 else []

    cmd = [
        "ffmpeg",
        "-re"
    ] + duration_args + [
        "-f", "lavfi", "-i", "testsrc=size=1280x720:rate=60",
        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
        "-map", "0:v",
        "-c:v", "libx264",
        "-profile:v", "baseline",
        "-level", "3.1",
        "-pix_fmt", "yuv420p",
        "-tune", "zerolatency",
        "-x264-params", "repeat-headers=1",
        "-g", "30",
        "-b:v", "2500k",
        "-payload_type", "96",
        "-f", "rtp", f"rtp://{vita_ip}:{video_port}",
        "-map", "1:a",
        "-c:a", "pcm_s16le",
        "-ar", "48000",
        "-ac", "2",
        "-payload_type", "97",
        "-f", "rtp", f"rtp://{vita_ip}:{audio_port}"
    ]

    try:
        timeout_val = (duration_sec + 3) if duration_sec > 0 else None
        subprocess.run(cmd, timeout=timeout_val)
    except subprocess.TimeoutExpired:
        print(f"\nStream completed after {duration_sec}s.")
    except KeyboardInterrupt:
        print("\nStream stopped by user.")

def build_rtp_header(seq, timestamp, ssrc, payload_type=96, marker=0):
    # RTP v2, no padding, no extension, CC=0 (0x80)
    byte0 = 0x80
    byte1 = (payload_type & 0x7F) | (0x80 if marker else 0x00)
    return struct.pack("!BBHII", byte0, byte1, seq & 0xFFFF, timestamp & 0xFFFFFFFF, ssrc)

def run_test_streamer(vita_ip="192.168.0.7", duration_sec=15):
    video_port = 5004
    audio_port = 5006

    print(f"============================================================")
    print(f" Starting Native Local RTP Streamer to PS Vita ({vita_ip})")
    print(f" Video: {vita_ip}:{video_port} (H.264 720p60 NALs)")
    print(f" Audio: {vita_ip}:{audio_port} (48kHz Stereo PCM Harmonic)")
    print(f" Duration: {duration_sec}s")
    print(f"============================================================")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Valid H.264 SPS & PPS (720p High Profile, Baseline compatible)
    sps_nal = bytes([
        0x67, 0x64, 0x00, 0x1F, 0xAC, 0xD9, 0x40, 0x50, 0x05, 0xBA,
        0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x03,
        0xC0, 0xF1, 0x83, 0x19, 0x60
    ])
    pps_nal = bytes([0x68, 0xEB, 0xE3, 0xCB, 0x22, 0xC0])

    # Sample IDR Slice NAL header (Type 5 - IDR picture)
    sample_idr = bytes([0x65, 0x88, 0x84, 0x00, 0x10, 0x00, 0x00]) + (b"\xAA" * 1200)

    video_ssrc = 0x12345678
    audio_ssrc = 0x87654321

    video_seq = 1
    audio_seq = 1

    video_ts = 0
    audio_ts = 0

    video_pkts_sent = 0
    audio_pkts_sent = 0
    total_bytes_sent = 0

    fps = 60
    frame_interval = 1.0 / fps
    audio_interval = 0.020  # 20ms audio chunks (960 frames at 48kHz)

    start_time = time.time()
    last_audio_time = start_time
    next_frame_time = start_time

    # Generate 440 Hz + 554 Hz (A4 + C#5 pleasant chord) stereo PCM
    audio_chunk_frames = 960
    sample_rate = 48000
    pcm_chunk = bytearray()
    for i in range(audio_chunk_frames):
        t = float(i) / sample_rate
        # Left channel: 440 Hz sine
        s_left = int(8000 * math.sin(2 * math.pi * 440.0 * t))
        # Right channel: 554.37 Hz sine (Major third)
        s_right = int(8000 * math.sin(2 * math.pi * 554.37 * t))
        pcm_chunk.extend(struct.pack("<hh", s_left, s_right))

    try:
        while True:
            now = time.time()
            elapsed = now - start_time
            if duration_sec > 0 and elapsed >= duration_sec:
                break

            # 1. Send Audio Packet every 20ms
            if now - last_audio_time >= audio_interval:
                hdr = build_rtp_header(audio_seq, audio_ts, audio_ssrc, payload_type=97, marker=0)
                pkt = hdr + pcm_chunk
                sock.sendto(pkt, (vita_ip, audio_port))

                audio_seq = (audio_seq + 1) & 0xFFFF
                audio_ts += audio_chunk_frames
                audio_pkts_sent += 1
                total_bytes_sent += len(pkt)
                last_audio_time = now

            # 2. Send 60 FPS Video Frame (SPS/PPS keyframe every 60 frames)
            if now >= next_frame_time:
                frame_idx = video_pkts_sent

                # Periodic SPS + PPS parameter sets
                if frame_idx % 60 == 0:
                    hdr_sps = build_rtp_header(video_seq, video_ts, video_ssrc, payload_type=96, marker=0)
                    sock.sendto(hdr_sps + sps_nal, (vita_ip, video_port))
                    video_seq = (video_seq + 1) & 0xFFFF

                    hdr_pps = build_rtp_header(video_seq, video_ts, video_ssrc, payload_type=96, marker=0)
                    sock.sendto(hdr_pps + pps_nal, (vita_ip, video_port))
                    video_seq = (video_seq + 1) & 0xFFFF
                    video_pkts_sent += 2

                # Video Frame NAL
                hdr_v = build_rtp_header(video_seq, video_ts, video_ssrc, payload_type=96, marker=1)
                sock.sendto(hdr_v + sample_idr, (vita_ip, video_port))

                video_seq = (video_seq + 1) & 0xFFFF
                video_ts += 1500  # 90000 / 60 = 1500 ticks per frame
                video_pkts_sent += 1
                total_bytes_sent += len(hdr_v) + len(sample_idr)

                next_frame_time += frame_interval

            time.sleep(0.001)

    except KeyboardInterrupt:
        print("\nStopping stream test...")

    total_time = time.time() - start_time
    kb_sent = total_bytes_sent / 1024.0
    bitrate_kbps = (total_bytes_sent * 8) / (total_time * 1000.0) if total_time > 0 else 0

    print(f"============================================================")
    print(f" Streaming Test Complete ({total_time:.1f}s elapsed)")
    print(f" Video Packets: {video_pkts_sent} ({video_pkts_sent / total_time:.1f} pkts/sec)")
    print(f" Audio Packets: {audio_pkts_sent} ({audio_pkts_sent / total_time:.1f} pkts/sec)")
    print(f" Total Transferred: {kb_sent:.1f} KB ({bitrate_kbps:.1f} kbps)")
    print(f"============================================================")

if __name__ == "__main__":
    vita_ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.7"
    duration = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    if shutil.which("ffmpeg"):
        run_ffmpeg_streamer(vita_ip, duration)
    else:
        run_test_streamer(vita_ip, duration)
