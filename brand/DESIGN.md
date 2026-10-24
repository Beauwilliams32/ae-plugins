# Williams Media AE Fleet design system

Part of the Trident Studio brand system. Sixteen original After Effects effects for motion and editorial.

**Tagline (proposed):** Sixteen effects, zero fuss.
**Audience:** Motion designers and editors in After Effects.
**Family:** Media and creative. **Status:** Verified.

## Use it

1. Link `tokens.css` before your own styles. `data-theme="dark"` is the default; set `data-theme="light"` on `<html>` for light.
2. Use the variables, never raw hex values: `var(--accent)`, `var(--surface)`, `var(--text)`.
3. Primary buttons use `--accent` with `--on-accent` text. Links use `--link`. Focus rings use `--link` at 2 px.
4. The product tile (`brand-book.pdf`, page 3) is the app icon, favicon source and avatar. Gradient `#FF6B5E` to `#E04E8B` at 135 degrees, 22% corners, glyph `fx` at 62% of the tile.

## Colour

| Token | Value |
|---|---|
| accent | `#FF6B5E` |
| accent-2 | `#E04E8B` |
| on-accent | `#0B0B0D` |
| dark bg / surface / line | `#161523` / `#221926` / `#3B222C` |
| light bg / surface / line | `#F4EFEE` / `#FFFFFF` / `#DACFD0` |
| link on dark / light | `#FF6B5E` / `#AD4940` (both pass 4.5:1) |

Signals are shared across the fleet: success `#2FD39A`, warning `#F5B83D`, danger `#FF6B5E`, info `#3BA8F0`.

## Type

Display: Playfair Display Bold. Text: Inter. Code: JetBrains Mono. Scale 72 / 40 / 24 / 17 / 13 px. Body never below 16 px on screen.

## Voice

We do: Show the work; Energy without noise; Credit the creator.
We don't: Buzzwords; Stock phrases; Overpromising reach.

## Rules

- One accent pair per product. Do not add colours for one-off pages.
- Keep the product's existing credit line in the footer (Trident Studio where it has one).
- Contrast: 4.5:1 for text, 3:1 for large text and UI borders.
- Motion 160 to 320 ms, transform and opacity only, and honour reduced motion.
- Nothing inside the tile except the glyph.
