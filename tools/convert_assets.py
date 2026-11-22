#!/usr/bin/env python3
"""Convert the checked-in source assets into C tables for the firmware.

Inputs (all already in this repository, no downloads):
  assets/fonts/*.bin       4 bpp LVGL binary fonts
  assets/sprites/*.bmp     4 bpp indexed, 96 x 96
  assets/types.json        ICAO type -> name/span
  certs/ca-bundle.pem      Mozilla roots (+ GTS Root R4)
  certs/gtsr4.pem          extra root required by adsb.fi

Outputs C source in assets/generated (or --out). The generated files are not
hand-edited; rerun this script or `cmake --build ... --target fc_assets`.

The font converter emits static `lv_font_t` / `lv_font_fmt_txt_dsc_t` data,
which keeps the 84 KiB of glyphs in XIP flash instead of the LVGL heap.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
FONT_DIR = ASSETS / "fonts"
SPRITE_DIR = ASSETS / "sprites"
TYPES_JSON = ASSETS / "types.json"
GTSR4 = ROOT / "certs" / "gtsr4.pem"
CA_BUNDLE = ROOT / "certs" / "ca-bundle.pem"

FONTS = [
    ("fc_font_clock", "b612mono-bold-78.bin", "Clock B612 Mono Bold 78"),
    ("fc_font_callsign", "b612mono-bold-34.bin", "Callsign B612 Mono Bold 34"),
    ("fc_font_route", "b612mono-bold-25.bin", "Route B612 Mono Bold 25"),
    ("fc_font_body", "b612-regular-18.bin", "Type B612 Regular 18"),
    ("fc_font_value", "b612mono-regular-17.bin", "Values B612 Mono Regular 17"),
    ("fc_font_date", "b612-regular-14.bin", "Date B612 Regular 14"),
    ("fc_font_airline", "b612-regular-11.bin", "Airline B612 Regular 11"),
    ("fc_font_small", "b612mono-regular-9.bin", "Small B612 Mono Regular 9"),
    ("fc_font_label", "b612-bold-9.bin", "Labels B612 Bold 9"),
]


def c_string_bytes(data: bytes, per_line: int = 24) -> str:
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk))
    return ",\n".join(lines)


def c_pem_string(text: str) -> str:
    out = []
    for line in text.splitlines(keepends=True):
        escaped = line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
        out.append(f'    "{escaped}"')
    return "\n".join(out)


def parse_bin_font(path: Path) -> dict:
    data = path.read_bytes()
    sections = {}
    pos = 0
    while pos + 8 <= len(data):
        size, marker = struct.unpack("<I4s", data[pos:pos + 8])
        if size < 8 or pos + size > len(data):
            raise ValueError(f"{path}: bad section at {pos}")
        sections[marker] = (pos, data[pos + 8:pos + size])
        pos += size

    for needed in (b"head", b"cmap", b"loca", b"glyf"):
        if needed not in sections:
            raise ValueError(f"{path}: missing {needed!r}")

    head_pos, head = sections[b"head"]
    font_size = struct.unpack("<H", head[6:8])[0]
    ascent = struct.unpack("<H", head[8:10])[0]
    descent = struct.unpack("<h", head[10:12])[0]
    typo_ascent = struct.unpack("<H", head[12:14])[0]
    typo_descent = struct.unpack("<h", head[14:16])[0]
    bpp = head[29]
    xy_bits = head[30]
    wh_bits = head[31]
    adv_bits = head[32]
    underline_pos = struct.unpack("<h", head[36:38])[0] if len(head) >= 38 else 0
    underline_thick = struct.unpack("<H", head[38:40])[0] if len(head) >= 40 else 0

    cmap_pos, cmap = sections[b"cmap"]
    loca_pos, loca = sections[b"loca"]
    glyf_pos, glyf = sections[b"glyf"]

    n_sub = struct.unpack("<I", cmap[:4])[0]
    runs = []
    for i in range(n_sub):
        h = cmap[4 + 16 * i:20 + 16 * i]
        doff, rstart, rlen, goff, ecount, _fmt, _pad = struct.unpack("<IIHHHBB", h)
        values = list(data[cmap_pos + doff:cmap_pos + doff + ecount])
        runs.append((rstart, rlen, goff, values))

    gcount = struct.unpack("<I", loca[:4])[0]
    loca_start = loca_pos + 8 + 4
    offsets = [struct.unpack("<H", data[loca_start + i * 2:loca_start + i * 2 + 2])[0]
               for i in range(gcount)]

    header_bits = adv_bits + 2 * xy_bits + 2 * wh_bits
    if header_bits % 8:
        raise ValueError(f"{path}: non-byte-aligned glyph header not supported")
    header_bytes = header_bits // 8

    def read_bits(buf: bytes, bitpos: int, n: int) -> int:
        value = 0
        for _ in range(n):
            byte = buf[bitpos // 8]
            value = (value << 1) | ((byte >> (7 - (bitpos % 8))) & 1)
            bitpos += 1
        return value

    glyphs = []
    bitmap = bytearray()
    for i in range(gcount):
        abs_off = glyf_pos + offsets[i]
        if i == 0:
            glyphs.append(dict(adv=0, w=0, h=0, xo=0, yo=0, bmp_index=0))
            continue
        if offsets[i] == 0:
            glyphs.append(dict(adv=0, w=0, h=0, xo=0, yo=0, bmp_index=len(bitmap)))
            continue
        chunk = data[abs_off:abs_off + header_bytes]
        bitpos = 0
        adv = read_bits(chunk, bitpos, adv_bits) if adv_bits else 0
        bitpos += adv_bits
        xo = read_bits(chunk, bitpos, xy_bits)
        bitpos += xy_bits
        yo = read_bits(chunk, bitpos, xy_bits)
        bitpos += xy_bits
        w = read_bits(chunk, bitpos, wh_bits)
        bitpos += wh_bits
        h = read_bits(chunk, bitpos, wh_bits)
        if xo & (1 << (xy_bits - 1)):
            xo -= 1 << xy_bits
        if yo & (1 << (xy_bits - 1)):
            yo -= 1 << xy_bits
        next_abs = ((glyf_pos + offsets[i + 1]) if i + 1 < gcount
                    else glyf_pos + 8 + len(glyf))
        bmp_size = next_abs - abs_off - header_bytes
        bmp = data[abs_off + header_bytes:abs_off + header_bytes + bmp_size]
        if w and h:
            bitmap.extend(bmp)
        glyphs.append(dict(adv=adv, w=w, h=h, xo=xo, yo=yo,
                           bmp_index=len(bitmap) - (len(bmp) if w and h else 0)))

    return dict(
        path=path, size=font_size, ascent=ascent, descent=descent,
        typo_ascent=typo_ascent, typo_descent=typo_descent,
        bpp=bpp, glyphs=glyphs, runs=runs, bitmap=bytes(bitmap),
        underline_pos=underline_pos, underline_thick=underline_thick,
    )


def emit_font(name: str, font: dict, out: list[str]) -> None:
    base = name
    out.append(f"/* {font['path'].name}: {font['size']} px, {font['bpp']} bpp */")
    out.append(f"static const uint8_t {base}_bitmap[] = {{")
    out.append(c_string_bytes(font["bitmap"]))
    out.append("};")
    out.append("")

    out.append(f"static const lv_font_fmt_txt_glyph_dsc_t {base}_glyphs[] = {{")
    for g in font["glyphs"]:
        out.append(
            "    { .bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, "
            ".ofs_x = %d, .ofs_y = %d }," % (
                g["bmp_index"], g["adv"] * 16, g["w"], g["h"], g["xo"], g["yo"]))
    out.append("};")
    out.append("")

    cmap_arrays = []
    for i, (rstart, rlen, goff, values) in enumerate(font["runs"]):
        arr = f"{base}_cmap_{i}"
        cmap_arrays.append(arr)
        out.append(f"static const uint8_t {arr}[] = {{")
        out.append("    " + ", ".join(str(v) for v in values))
        out.append("};")
    out.append("")

    out.append(f"static const lv_font_fmt_txt_cmap_t {base}_cmaps[] = {{")
    for i, (rstart, rlen, goff, values) in enumerate(font["runs"]):
        out.append(
            "    { .range_start = %d, .range_length = %d, .glyph_id_start = 0, "
            ".unicode_list = NULL, .glyph_id_ofs_list = %s, "
            ".list_length = %d, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL },"
            % (rstart, rlen, cmap_arrays[i], len(values)))
    out.append("};")
    out.append("")

    out.append(f"static const lv_font_fmt_txt_dsc_t {base}_dsc = {{")
    out.append(f"    .glyph_bitmap = {base}_bitmap,")
    out.append(f"    .glyph_dsc = {base}_glyphs,")
    out.append(f"    .cmaps = {base}_cmaps,")
    out.append("    .kern_dsc = NULL,")
    out.append("    .kern_scale = 0,")
    out.append(f"    .cmap_num = {len(font['runs'])},")
    out.append(f"    .bpp = {font['bpp']},")
    out.append("    .kern_classes = 0,")
    out.append("    .bitmap_format = 0,")
    out.append("    .stride = 0,")
    out.append("};")
    out.append("")

    out.append(f"const lv_font_t {name} = {{")
    out.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    out.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    out.append("    .release_glyph = NULL,")
    out.append(f"    .line_height = {font['ascent'] - font['descent']},")
    out.append(f"    .base_line = {-font['descent']},")
    out.append("    .subpx = 0,")
    out.append("    .kerning = 0,")
    out.append("    .static_bitmap = 0,")
    out.append(f"    .underline_position = {font['underline_pos']},")
    out.append(f"    .underline_thickness = {font['underline_thick']},")
    out.append(f"    .dsc = &{base}_dsc,")
    out.append("    .fallback = NULL,")
    out.append("    .user_data = NULL,")
    out.append("};")
    out.append("")


def parse_bmp_4bpp(path: Path) -> bytes:
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    pixel_offset = struct.unpack("<I", data[10:14])[0]
    dib_size = struct.unpack("<I", data[14:18])[0]
    if dib_size < 40:
        raise ValueError(f"{path}: unsupported DIB")
    width, height = struct.unpack("<ii", data[18:26])
    bpp = struct.unpack("<H", data[28:30])[0]
    compression = struct.unpack("<I", data[30:34])[0]
    if bpp != 4 or compression != 0:
        raise ValueError(f"{path}: expected 4 bpp uncompressed")
    if width != 96 or abs(height) != 96:
        raise ValueError(f"{path}: expected 96x96, got {width}x{height}")
    row_bytes = ((width * 4 + 7) // 8 + 3) & ~3
    out = bytearray(96 * 96 // 2)
    top_down = height < 0
    for y in range(96):
        src_y = y if top_down else (95 - y)
        row = pixel_offset + src_y * row_bytes
        for x in range(96):
            byte = data[row + x // 2]
            nibble = (byte >> 4) if (x % 2 == 0) else (byte & 0x0F)
            if x % 2 == 0:
                out[y * 48 + x // 2] = (nibble << 4)
            else:
                out[y * 48 + x // 2] |= nibble
    return bytes(out)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=ROOT / "assets" / "generated")
    args = ap.parse_args()
    out_dir: Path = args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    # --- fonts -----------------------------------------------------------
    src = [
        '#include "lvgl.h"',
        '#include "fc_assets_generated.h"',
        "",
        "/* Generated by tools/convert_assets.py - do not edit. */",
        "",
    ]
    for name, filename, _label in FONTS:
        emit_font(name, parse_bin_font(FONT_DIR / filename), src)
    (out_dir / "fc_fonts.c").write_text("\n".join(src) + "\n")

    # --- type table ------------------------------------------------------
    types = json.loads(TYPES_JSON.read_text())
    gen = [
        '#include "types.h"',
        "",
        "/* Generated from assets/types.json - do not edit. */",
        "",
        "const fc_aircraft_type_t fc_types[] = {",
    ]
    for code in sorted(types):
        info = types[code]
        name = info["name"].replace("\\", "\\\\").replace('"', '\\"')
        gen.append('    { "%s", "%s", %.1ff },' % (code, name, float(info["span"])))
    gen.append("};")
    gen.append(f"const unsigned fc_type_count = {len(types)};")
    gen.append("")
    (out_dir / "fc_types.c").write_text("\n".join(gen) + "\n")

    # --- sprites ---------------------------------------------------------
    sprite_paths = sorted(SPRITE_DIR.glob("*.bmp"))
    sprite_blobs = []
    sprite_entries = []
    offset = 0
    for path in sprite_paths:
        blob = parse_bmp_4bpp(path)
        sprite_blobs.append(blob)
        sprite_entries.append((path.stem, offset, len(blob)))
        offset += len(blob)
    combined = b"".join(sprite_blobs)

    sp = [
        '#include "sprite.h"',
        "",
        "/* Generated from assets/sprites - do not edit. */",
        "",
        "static const uint8_t fc_sprite_data[] = {",
        c_string_bytes(combined, 32),
        "};",
        "",
        "const fc_sprite_t fc_sprites[] = {",
    ]
    for name, off, size in sprite_entries:
        sp.append('    { "%s", fc_sprite_data + %d, %d },' % (name, off, size))
    sp.append("};")
    sp.append(f"const unsigned fc_sprite_count = {len(sprite_entries)};")
    sp.append("")
    (out_dir / "fc_sprites.c").write_text("\n".join(sp) + "\n")

    # --- certificates ----------------------------------------------------
    bundle = CA_BUNDLE.read_text() if CA_BUNDLE.exists() else ""
    gtsr4 = GTSR4.read_text()
    certs = [
        '#include "fc_assets_generated.h"',
        "",
        "/* Generated from certs/ca-bundle.pem and certs/gtsr4.pem. */",
        "",
        "const char fc_ca_bundle_pem[] =",
        c_pem_string(bundle) if bundle else '    ""',
        ";",
        "",
        "const char fc_gtsr4_pem[] =",
        c_pem_string(gtsr4),
        ";",
        "",
    ]
    (out_dir / "fc_certs.c").write_text("\n".join(certs) + "\n")

    # --- header ----------------------------------------------------------
    hdr = [
        "/* Generated by tools/convert_assets.py - do not edit. */",
        "#ifndef FC_ASSETS_GENERATED_H",
        "#define FC_ASSETS_GENERATED_H",
        "",
        '#include "lvgl.h"',
        "",
    ]
    for name, _filename, label in FONTS:
        hdr.append(f"extern const lv_font_t {name}; /* {label} */")
    hdr += [
        "",
        "extern const char fc_ca_bundle_pem[];",
        "extern const char fc_gtsr4_pem[];",
        "",
        "#endif",
        "",
    ]
    (out_dir / "fc_assets_generated.h").write_text("\n".join(hdr))

    print(f"fonts: {len(FONTS)} ({sum(p.stat().st_size for p in FONT_DIR.glob('*.bin')) // 1024} KiB source)")
    print(f"sprites: {len(sprite_entries)} ({len(combined) // 1024} KiB indexed)")
    print(f"types: {len(types)}")
    print(f"ca bundle: {len(bundle) // 1024} KiB, gtsr4: {len(gtsr4) // 1024} KiB")
    print(f"wrote {out_dir}")


if __name__ == "__main__":
    main()
