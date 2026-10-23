# FilterDrum designs

- `FilterDrum-ConceptA-TwinLanes.png`, `-ConceptB-Focus.png`, `-ConceptC-ChannelStrips.png` — first concepts (5 Oct 2026).
- `FilterDrum-MainDesign.png` — Design 2.0, chosen 5 Oct 2026 (signal-flow style, implemented on branch `Design2.0`).
- `FilterDrum-Polivoks-1-TwinLanes.png` — R2Synth 1.1 black panel (Polivoks style, OLED screens, 70s sci-fi lettering, Studio Worn wear) on the Design 2.0 layout: boxed NOISE / VCF / VCA blocks, envelope bracket, LAUNCH ON rotary, lit-lens step keys + white chaser LED, disabled steps hollow (24 Oct 2026).
- `FilterDrum-Polivoks-2-Focus.png` — same look; one big editor for the drum chosen by an EDIT selector (or clicking its card), cards keep both drums' envelopes and STRIKE lamps in view, large OLEDs, velocity readout line, transport beside MIX so the step keys take the full width (24 Oct 2026).
- `FilterDrum-Polivoks-3-MirrorColumns.png` — same look; the drums as two side-by-side columns flowing downward, every control level with its twin, the D1/D2 mix knob between them, COPY 1→2 / 2→1, transport in the spine (24 Oct 2026).

Polivoks mock-ups are drawn by `polivoks-src/design{1,2,3}.py` (kit.py, drums.py), rendered with `render.py` and worn with `weather.py` (R2Synth's Studio Worn pass): `python3 design1.py out && python3 render.py out <name> && python3 weather.py out/<name>.clean.png out/<name>.geo.json <name>.png`.
