# Design Specification — Tutorial_AwesomeModernCPP

This document defines the visual identity for the project. It covers the color
palette, required asset formats, AI generation prompts for future professional
assets, and the toolchain workflow for producing final files from sources.

---

## 1. Color Palette

| Role                  | Name            | HEX       | Sample                         | Usage                                    |
|-----------------------|-----------------|-----------|--------------------------------|------------------------------------------|
| Primary               | Indigo          | `#3F51B5` | ![#3F51B5](https://via.placeholder.com/16/3F51B5/3F51B5) | Main brand color, outlines, headings     |
| Primary Light         | Indigo 300      | `#5C6BC0` | ![#5C6BC0](https://via.placeholder.com/16/5C6BC0/5C6BC0) | Secondary strokes, hover states          |
| Accent Blue           | Blue 400        | `#42A5F5` | ![#42A5F5](https://via.placeholder.com/16/42A5F5/42A5F5) | Links, interactive highlights            |
| Accent (Teal)         | Teal            | `#009688` | ![#009688](https://via.placeholder.com/16/009688/009688) | C++ text, version badges, call-to-action |
| Highlight             | Amber           | `#FFC107` | ![#FFC107](https://via.placeholder.com/16/FFC107/FFC107) | Warnings, new-feature badges             |
| Dark Text             | Gray 900        | `#212121` | ![#212121](https://via.placeholder.com/16/212121/212121) | Body text, code comments                 |
| Light Background      | Gray 100        | `#F5F5F5` | ![#F5F5F5](https://via.placeholder.com/16/F5F5F5/F5F5F5) | Page background, code blocks             |
| Dark Mode BG Start    | Navy            | `#1A1A2E` | ![#1A1A2E](https://via.placeholder.com/16/1A1A2E/1A1A2E) | Dark mode gradient start                 |
| Dark Mode BG End      | Deep Blue       | `#0F3460` | ![#0F3460](https://via.placeholder.com/16/0F3460/0F3460) | Dark mode gradient end                   |
| White                 | White           | `#FFFFFF` | ![#FFFFFF](https://via.placeholder.com/16/FFFFFF/FFFFFF) | Card backgrounds, text on dark           |

### Usage Rules

- The primary indigo and teal accent must appear together in every branded asset.
- Amber is used sparingly -- only for attention-calling badges or warnings.
- Dark mode applies the `#1A1A2E` -> `#0F3460` gradient as a background, with
  white text and the same indigo/teal brand colors for accents.
- All text must meet WCAG AA contrast against its background.

---

## 2. Required Outputs

| Asset              | File Name            | Dimensions       | Format | Background   | Max Size | Purpose                           |
|--------------------|----------------------|------------------|--------|--------------|----------|-----------------------------------|
| Logo (master)      | `logo.svg`           | 512 x 512 (viewBox) | SVG  | Transparent  | --       | Source of truth, scalable         |
| Logo (raster)      | `logo.png`           | 512 x 512        | PNG   | Transparent  | 50 KB    | General web use, README           |
| Logo (dark BG)     | `logo-light.png`     | 512 x 512        | PNG   | Transparent  | 50 KB    | Inverted/light version for dark backgrounds |
| Favicon            | `favicon.ico`        | 16/32/48 multi   | ICO   | Transparent  | 15 KB    | Browser tab icon                  |
| Social Preview     | `social-preview.png` | 1280 x 640       | PNG   | Solid or gradient | 1 MB | GitHub/OpenGraph link preview     |

### Notes on Each Asset

- **logo.svg**: Already created as a placeholder. Replace when a professional
  design is available. Keep viewBox `0 0 512 512`.
- **logo.png**: Export from SVG. Must look crisp at 64 px (favicon size) and
  512 px. Use anti-aliased rendering.
- **logo-light.png**: Same design but with stroke colors adjusted for
  visibility on dark backgrounds (swap dark strokes to lighter tints).
- **favicon.ico**: Must contain 16x16, 32x32, and 48x48 layers. Test in
  Chrome, Firefox, and Safari.
- **social-preview.png**: Horizontal banner. Place logo on the left or center.
  Include project title "Awesome Modern C++ Tutorial" and a tagline. Use the
  dark gradient background for visual impact.

---

## 3. AI Image Generation Prompts

These prompts are designed for DALL-E 3, Midjourney v6, or similar tools. They
produce professional replacements for the placeholder assets.

### 3.1 Logo Prompt

```text
A minimalist tech logo for a C++ programming tutorial brand called "Awesome
Modern C++". The design is a stylized integrated circuit chip viewed from above:
a rounded square body with short pin-like markings along all four edges. Inside
the chip body, large curly braces "{ }" are the central motif, rendered in deep
indigo (#3F51B5). Below the braces, the text "C++" appears in teal (#009688)
using a clean monospace typeface. The overall style is flat, geometric, and
modern -- similar to a VS Code extension icon or a JetBrains product logo.
Transparent background. No gradients, no 3D effects, no drop shadows. The
composition is square and centered. White and dark-mode compatible.
```

### 3.2 Social Preview Banner Prompt

```text
A wide horizontal banner (1280x640) for a GitHub repository social preview.
Dark gradient background from deep navy (#1A1A2E) on the left to dark blue
(#0F3460) on the right. In the center, the text "Awesome Modern C++" in large,
bold, white sans-serif font. Below it, a smaller tagline "A modern C++ tutorial
from basics to systems programming" in light gray (#B0BEC5). To the left of the
text, a small version of the brand logo: a stylized chip icon with curly braces
in indigo (#3F51B5) and "C++" in teal (#009688). Minimalist, clean, developer-
aesthetic. No photos of people. No cluttered elements. Subtle geometric grid
lines in the background at very low opacity for texture.
```

### 3.3 Usage Tips

- For **Midjourney**, append `--no shading,3d,shadow --v 6.0 --style raw` to
  the logo prompt for a flatter result.
- For **DALL-E 3**, the prompts work as-is. Request "transparent background"
  explicitly for the logo.
- Always generate at the highest resolution available and downscale afterward.
- Run at least 4 variations and pick the cleanest one.

---

## 4. Tools and Workflow

### 4.1 SVG to PNG

```bash
# Option A: Inkscape (recommended, best rendering)
inkscape -w 512 -h 512 logo.svg -o logo.png

# Option B: rsvg-convert
rsvg-convert -w 512 -h 512 logo.svg -o logo.png

# Option C: ImageMagick (acceptable but may misrender gradients)
convert -background none -density 384 -resize 512x512 logo.svg logo.png
```

### 4.2 Light Version (for dark backgrounds)

Open `logo.svg` in a text editor and create `logo-light.svg` with adjusted
stroke colors:

- `#3F51B5` (indigo) becomes `#7986CB` (lighter indigo)
- `#5C6BC0` becomes `#9FA8DA`
- `#009688` (teal) becomes `#4DB6AC`
- Dot opacity increases from `0.35` to `0.55`

Then export:

```bash
inkscape -w 512 -h 512 logo-light.svg -o logo-light.png
```

### 4.3 PNG to ICO (Favicon)

```bash
# Option A: ImageMagick
convert logo.png -define icon:auto-resize=16,32,48 favicon.ico

# Option B: Online tool (recommended for best results)
# Upload logo.png to https://realfavicongenerator.net
# Download the generated favicon.ico package
```

### 4.4 Social Preview

```bash
# Option A: Canva
#   1. Create a 1280x640 canvas
#   2. Set background gradient: #1A1A2E -> #0F3460
#   3. Place logo.png on the left, scale to ~200px height
#   4. Add title and tagline text
#   5. Export as PNG

# Option B: Figma
#   1. Frame: 1280x640
#   2. Background: linear gradient #1A1A2E -> #0F3460
#   3. Import logo.svg, position left-center
#   4. Text: "Awesome Modern C++" in Inter Bold 72pt white
#   5. Subtitle in Inter Regular 28pt #B0BEC5
#   6. Export frame as PNG @1x

# Option C: CLI with ImageMagick (quick placeholder)
convert -size 1280x640 gradient:#1A1A2E-#0F3460 \
  -font "Inter" -fill white -pointsize 72 -gravity center \
  -annotate +0-40 "Awesome Modern C++" \
  -fill "#B0BEC5" -pointsize 28 \
  -annotate +0+50 "A modern C++ tutorial from basics to systems programming" \
  social-preview.png
```

### 4.5 Full Pipeline (one-liner)

```bash
# Generate all assets from logo.svg in sequence
inkscape -w 512 -h 512 logo.svg -o logo.png && \
convert logo.png -define icon:auto-resize=16,32,48 favicon.ico && \
echo "Done: logo.png, favicon.ico generated"
```

---

## 5. File Inventory

After completing the workflow, the `documents/images/` directory should contain:

```text
documents/images/
  logo.svg              <- Source of truth (current placeholder)
  logo.png              <- 512x512 rasterized logo
  logo-light.svg        <- Light variant source
  logo-light.png        <- 512x512 light logo for dark backgrounds
  favicon.ico           <- Multi-size favicon
  social-preview.png    <- 1280x640 GitHub social preview
  DESIGN_SPEC.md        <- This document
```

---

*Last updated: 2026-04-16*
