# Roboto Condensed source

Upstream: https://github.com/google/fonts/tree/main/ofl/robotocondensed

Downloaded from the official Google Fonts repository on 2026-10-27. The original
variable font, metadata and SIL Open Font License are retained in this folder.
The source font SHA256 is:
`dace262afcee68a5276f200d8026c57221735c0118ab5fda8c2c0d3dc409a8d0`.

`tools/generate_speed_font.py` renders weight 700 with Pillow/FreeType at four
times resolution, downsamples to 36x76 cells, quantizes to 4-bit alpha and aligns
the ink right edges for stable tabular speed readings. It writes the checked-in
`src/display/SpeedFont.h`; normal firmware builds do not require Pillow or font
downloads. Only digits are embedded in flash; the full TTF is not linked.

Font copyright: 2011 The Roboto Project Authors. See `OFL.txt`.
