# DESIGN.md — DS4Arduino docs site

## Color (OKLCH, committed strategy)
- Surface: deep blue-black. `--bg: oklch(0.18 0.02 260)` (near-black, blue tint).
- Panel: `oklch(0.23 0.025 260)`. Borders `oklch(0.32 0.03 260)`.
- Text: `oklch(0.92 0.008 260)`. Muted: `oklch(0.65 0.02 260)`.
- Identity accent, light-bar blue: `oklch(0.65 0.18 250)`. Reserved for
  link state, live connection glow, primary actions.
- Functional accents, PlayStation face colors, used ONLY as pressed-state
  fills on the tester: triangle green `oklch(0.72 0.16 145)`,
  circle red-pink `oklch(0.62 0.20 15)`, cross blue `oklch(0.65 0.16 250)`,
  square pink `oklch(0.68 0.17 340)`. Never decorative elsewhere.

## Typography
System stack only (offline reliability). Display 2.4rem/800, section
1.35rem/700, body 1rem/400 capped at 70ch, mono for code/protocol.

## Shape and elevation
8–12px radii, 1px borders, no shadows except the light-bar glow
(box-shadow in light-bar blue, connection-gated). No gradients except a
single restrained hero wash.

## Components
- Sticky slim nav (brand + Docs / Live test).
- Numbered pairing steps (leading numerals, no side-stripe accents).
- API as a definition table, mono call column.
- Live stage: inline SVG DualShock 4, data-bound groups:
  sticks (translate), face buttons (fill swap to face color),
  D-pad (fill swap), touchpad (finger dots), light bar (glow on link),
  whole-controller gyro tilt (subtle rotate, disabled under
  prefers-reduced-motion).
- Telemetry as plain labeled numbers, log as mono console block.
- Diagnostics strip: counts + last rejection reason (plain text).
