# Assets

Source inputs for `tools/convert_assets.py`, which produces the C tables in
`assets/generated/` at build time. Generated files are never committed.

| Path | Contents |
|---|---|
| `fonts/*.bin` | 4 bpp LVGL binary fonts (B612 and B612 Mono). |
| `fonts/OFL.txt` | SIL Open Font License 1.1 for the B612 fonts. |
| `sprites/*.bmp` | 4 bpp indexed 96 × 96 aircraft sprites. |
| `types.json` | ICAO aircraft type code to name and wingspan. |

To regenerate by hand:

```bash
python3 tools/convert_assets.py
```

The CMake build and the host-test Makefile do this automatically.
