# PRODUCT.md — DS4Arduino docs site

## Users
Makers and hobbyists wiring a Sony DualShock 4 to an ESP32 over Bluetooth
Classic with Arduino. They read docs at a workbench, often next to the
physical controller, and they judge a library by whether the live tester
moves when they push a stick.

## Product purpose
Two surfaces in `docs/` (GitHub Pages):
1. `index.html` — docs landing that earns trust fast: what it is, pairing,
   minimal sketch, API, troubleshooting. (brand register)
2. `live.html` — WebSerial instrument panel that proves input end to end.
   (product register)

## Brand
Native hardware honesty. Everything on screen must correspond to a real
byte on the wire (report 0x11, `$DS4` checksums, packet counters). No
marketing gloss, no fake metrics. The DualShock 4 itself is the visual
anchor: black body, glowing blue light bar, four colored face symbols.

## Tone
Technical, precise, maker-direct. Short sentences. Numbers over adjectives.
Errors are stated plainly with a next action.

## Anti-references
- Generic SaaS docs themes (purple gradients, hero-metric cards).
- Neon gamer clichés (cyberpunk grids, lens flares, edgy all-caps).
- Dashboard template look (identical icon cards in a grid).

## Principles
1. The controller outline is the interface, not decoration.
2. Face-button colors mean exactly one thing: that button is pressed.
3. Light-bar blue means exactly one thing: link state.
4. Every visualization degrades to a number when graphics can't render.
5. Works offline except the serial port itself (no CDNs, no webfonts).

## Register
Mixed: index.html = brand, live.html = product.
