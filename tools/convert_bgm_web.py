#!/usr/bin/env python3
"""Convert TH07's raw-PCM thbgm.dat into per-track MP3 files for the web build.

The original game stores 20 BGM tracks as 44100 Hz / stereo / s16 PCM inside
thbgm.dat.  Bilibili Toy limits a package to ~140 MB, so we cannot ship the raw
archive.  This script:

  1. reads bgm/thbgm.fmt from th07.dat (PBG4 + LZSS);
  2. extracts each track's PCM and encodes it as MP3;
  3. writes assets/bgm/th07_XX.mp3 and a new assets/bgm/thbgm.fmt whose
     intro/total lengths are stored in PCM frames;
  4. writes a 16-byte stub assets/thbgm.dat so the game still enables its WAV
     (streaming) music path.

Usage:
    python scripts/convert_bgm_web.py [--bitrate 192k] [--out assets]
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def lzss_decompress(src: bytes, out_size: int) -> bytes:
    """Port of src/pbg4/Lzss.cpp Decompress()."""
    out = bytearray()
    dictionary = [0] * 0x2000
    dict_head = 1
    src_pos = 0
    in_bit_mask = 0x80
    cur_byte = 0

    def fetch_new_byte() -> None:
        nonlocal src_pos, cur_byte
        if in_bit_mask == 0x80:
            cur_byte = src[src_pos] if src_pos < len(src) else 0
            src_pos += 1

    def read_flag_bit() -> int:
        nonlocal in_bit_mask
        fetch_new_byte()
        bit = 1 if (cur_byte & in_bit_mask) else 0
        in_bit_mask >>= 1
        if in_bit_mask == 0:
            in_bit_mask = 0x80
        return bit

    def read_bits(count: int) -> int:
        nonlocal in_bit_mask
        value = 0
        mask = 1 << (count - 1)
        while mask:
            fetch_new_byte()
            if cur_byte & in_bit_mask:
                value |= mask
            mask >>= 1
            in_bit_mask >>= 1
            if in_bit_mask == 0:
                in_bit_mask = 0x80
        return value

    while True:
        if read_flag_bit():
            byte = read_bits(8)
            out.append(byte)
            dictionary[dict_head] = byte
            dict_head = (dict_head + 1) & 0x1FFF
        else:
            offset = read_bits(13)
            if offset == 0:
                break
            length = read_bits(4) + 2
            for i in range(length + 1):
                byte = dictionary[(offset + i) & 0x1FFF]
                out.append(byte)
                dictionary[dict_head] = byte
                dict_head = (dict_head + 1) & 0x1FFF

    while in_bit_mask != 0x80:
        read_flag_bit()

    return bytes(out[:out_size])


def parse_th07_entries(th07_path: str) -> list[tuple[str, int, int]]:
    with open(th07_path, "rb") as f:
        data = f.read()

    magic, count, header_size, meta_size = struct.unpack_from("<4sIII", data, 0)
    if magic != b"PBG4":
        raise SystemExit(f"bad th07.dat magic: {magic!r}")

    meta = lzss_decompress(data[header_size:], meta_size)
    entries: list[tuple[str, int, int]] = []
    pos = 0
    for _ in range(count):
        end = meta.index(b"\x00", pos)
        name = meta[pos:end].decode("ascii", "replace")
        pos = end + 1
        data_offset, decompressed_size, _magic = struct.unpack_from("<III", meta, pos)
        pos += 12
        entries.append((name, data_offset, decompressed_size))
    return entries


def read_archive_entry(data: bytes, entries: list[tuple[str, int, int]], name: str) -> bytes:
    for i, (entry_name, offset, size) in enumerate(entries):
        if entry_name.lower() == name.lower():
            end = entries[i + 1][1] if i + 1 < len(entries) else 0
            raw = data[offset:end]
            return lzss_decompress(raw, size)
    raise SystemExit(f"entry not found: {name}")


def parse_fmt(fmt: bytes) -> list[dict]:
    tracks = []
    for j in range(20):
        rec = fmt[j * 0x34 : (j + 1) * 0x34]
        name = rec[0:16].split(b"\x00")[0].decode("ascii", "replace")
        start_offset, preload_size, intro_len, total_len = struct.unpack_from("<iIII", rec, 16)
        tag, channels, sample_rate, avg_bytes, block_align, bits, cb_size = struct.unpack_from(
            "<HHIIHHH", rec, 32
        )
        tracks.append(
            {
                "name": name,
                "start": start_offset,
                "preload": preload_size,
                "intro": intro_len,
                "total": total_len,
                "tag": tag,
                "channels": channels,
                "sample_rate": sample_rate,
                "avg_bytes": avg_bytes,
                "block_align": block_align,
                "bits": bits,
                "cb_size": cb_size,
            }
        )
    return tracks


def encode_pcm_to_mp3(ffmpeg: str, pcm_path: str, mp3_path: str, bitrate: str) -> None:
    cmd = [
        ffmpeg,
        "-y",
        "-f",
        "s16le",
        "-ar",
        "44100",
        "-ac",
        "2",
        "-i",
        pcm_path,
        "-c:a",
        "libmp3lame",
        "-b:a",
        bitrate,
        "-write_xing",
        "0",
        mp3_path,
    ]
    result = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if result.returncode != 0:
        # Fall back to ffmpeg's native MP3 encoder if libmp3lame is absent.
        cmd[cmd.index("libmp3lame")] = "mp3"
        result = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise SystemExit(f"ffmpeg failed for {mp3_path}:\n{result.stderr.decode('utf-8', 'replace')}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bitrate", default="192k", help="MP3 bitrate, e.g. 160k / 192k")
    parser.add_argument("--assets", default=os.path.join(REPO_ROOT, "assets"))
    parser.add_argument("--keep-pcm", action="store_true", help="keep temporary raw PCM files")
    args = parser.parse_args()

    try:
        import imageio_ffmpeg

        ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("ffmpeg not found; pip install imageio-ffmpeg or add ffmpeg to PATH")

    th07_path = os.path.join(args.assets, "th07.dat")
    thbgm_path = os.path.join(args.assets, "thbgm.dat")
    bgm_out = os.path.join(args.assets, "bgm")
    os.makedirs(bgm_out, exist_ok=True)

    print("parsing th07.dat ...")
    entries = parse_th07_entries(th07_path)
    with open(th07_path, "rb") as f:
        th07_data = f.read()
    fmt = read_archive_entry(th07_data, entries, "thbgm.fmt")
    tracks = parse_fmt(fmt)

    print(f"found {len(tracks)} tracks")
    new_records = []
    temp_dir = os.path.join(args.assets, ".bgm-tmp")
    os.makedirs(temp_dir, exist_ok=True)
    for i, track in enumerate(tracks):
        stem = track["name"].rsplit(".", 1)[0]
        pcm_path = os.path.join(temp_dir, f"{stem}.pcm")
        mp3_path = os.path.join(bgm_out, f"{stem}.mp3")
        print(f"[{i + 1:02d}/{len(tracks)}] {track['name']} -> {os.path.basename(mp3_path)}")

        with open(pcm_path, "wb") as out, open(thbgm_path, "rb") as src:
            src.seek(track["start"])
            remaining = track["total"]
            while remaining > 0:
                chunk = src.read(min(4 * 1024 * 1024, remaining))
                if not chunk:
                    raise SystemExit(f"unexpected EOF in thbgm.dat at {track['name']}")
                out.write(chunk)
                remaining -= len(chunk)

        encode_pcm_to_mp3(ffmpeg, pcm_path, mp3_path, args.bitrate)
        if not args.keep_pcm:
            os.remove(pcm_path)

        # Lengths in frames (original: 2ch * 16bit = 4 bytes/frame).
        new_records.append(
            {
                "name": f"{stem}.mp3",
                "start": 0,
                "preload": 0,
                "intro": track["intro"] // 4,
                "total": track["total"] // 4,
            }
        )

    shutil.rmtree(temp_dir, ignore_errors=True)

    fmt_path = os.path.join(bgm_out, "thbgm.fmt")
    with open(fmt_path, "wb") as f:
        for rec in new_records:
            name = rec["name"].encode("ascii")[:15]
            f.write(name.ljust(16, b"\x00"))
            f.write(struct.pack("<iIII", rec["start"], rec["preload"], rec["intro"], rec["total"]))
            f.write(struct.pack("<HHIIHHH", 1, 2, 44100, 176400, 4, 16, 0))
            f.write(b"\x00\x00")  # alignment padding to 52-byte ThBgmFormat records
        f.write(b"\x00" * 0x34)  # sentinel record
    print(f"wrote {fmt_path}")

    stub_path = os.path.join(args.assets, "thbgm.dat")
    with open(stub_path, "wb") as f:
        f.write(struct.pack("<4sIII", b"ZWAV", 1, 0x700, 0))
    print(f"wrote stub {stub_path} ({os.path.getsize(stub_path)} bytes)")

    total_mp3 = sum(os.path.getsize(os.path.join(bgm_out, f)) for f in os.listdir(bgm_out) if f.endswith(".mp3"))
    print(f"MP3 total: {total_mp3 / 1_000_000:.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
