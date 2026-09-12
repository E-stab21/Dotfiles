#!/usr/bin/env python3
"""Build the TerminalTransparent theme graphics and its KDE color scheme.

The theme used to ship no SVG at all, which meant every widget was drawn by
Kvantum's built-in default skin: glossy gradients and 3D bevels. This takes
KvArcDark (flat, no gloss) as the base, desaturates it to neutral gray, then
appends the custom tab slices under an `mtab` prefix so they don't collide with
the base theme's own `tab-*` ids. The [Tab] kvconfig section points at `mtab`.

KDE apps such as Dolphin read their colors from kdeglobals through KColorScheme
rather than from the Qt palette, so they ignore Kvantum entirely. The --colors
mode emits a matching .colors scheme through the same desaturation, which is
what keeps those apps in step with the widget graphics.

  ./gen-theme.py            > TerminalTransparent.svg
  ./gen-theme.py --colors   > ~/.local/share/color-schemes/TerminalTransparent.colors
"""

import colorsys
import re
import sys

BASE_SVG = "/usr/share/Kvantum/KvArcDark/KvArcDark.svg"
BASE_COLORS = "/usr/share/color-schemes/KvArcDark.colors"
SCHEME_NAME = "TerminalTransparent"

# Anything more saturated than this is one of the base theme's accent colors
# (Arc's blues and one purple) rather than part of its gray structure.
ACCENT_SAT = 0.30
# Floor applied after desaturation so KvArcDark's mid-grays land near charcoal
# instead of the original ~#494949. Near-white (text, ticks) is left alone.
DARKEN = 0.55
LIGHT_KEEP = 0.82

# Accents collapse onto this lightness window so selections stay readable
# against the darker background while white text still reads on them.
ACCENT_L0, ACCENT_L1 = 0.16, 0.22

TAB_R = 6        # tab corner radius; must match frame.* in [Tab]
HAIRLINE = 1
EDGE = 24
SLOT = 44

# fill, fill opacity, then the 1px top highlight that traces the rounded edge.
TAB_STATES = {
    "normal":   ("#1a1a1a", 0.55, None,      0),
    "focused":  ("#2a2a2a", 0.90, "#4a4a4a", 0.55),
    "toggled":  ("#333333", 1.00, "#6a6a6a", 0.85),
    "pressed":  ("#3a3a3a", 1.00, "#7a7a7a", 0.85),
    "disabled": ("#141414", 0.35, None,      0),
}

PARTS = ["topleft", "top", "topright", "left", "right",
         "bottomleft", "bottom", "bottomright"]


def gray_value(r, g, b):
    """Neutral gray level for an 0-1 RGB triple, preserving relative lightness."""
    _, lightness, sat = colorsys.rgb_to_hls(r, g, b)
    if lightness >= LIGHT_KEEP:
        return max(0, min(255, round(lightness * 255)))
    if sat > ACCENT_SAT:
        lightness = ACCENT_L0 + ACCENT_L1 * lightness
    lightness *= DARKEN
    return max(0, min(255, round(lightness * 255)))


def to_gray(hex_str):
    """Map one #rgb/#rrggbb to neutral gray."""
    h = hex_str.lstrip("#")
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    r, g, b = (int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))
    return "#{0:02x}{0:02x}{0:02x}".format(gray_value(r, g, b))


def gray_triple(match):
    """Map one `R,G,B` entry of a KDE .colors file to neutral gray."""
    r, g, b = (int(v) for v in match.group(0).split(","))
    v = gray_value(r / 255, g / 255, b / 255)
    return f"{v},{v},{v}"


def emit_colors():
    with open(BASE_COLORS, encoding="utf-8") as fh:
        txt = fh.read()
    txt = re.sub(r"\b\d{1,3},\d{1,3},\d{1,3}\b", gray_triple, txt)
    txt = re.sub(r"^Name=.*$", f"Name={SCHEME_NAME}", txt, flags=re.M)
    # Keep the KDE View/Window colors in lockstep with kvconfig base/window.
    view_rgb = "14,14,14"
    txt = re.sub(
        r"(\[Colors:View\]\n)BackgroundAlternate=\d+,\d+,\d+\nBackgroundNormal=\d+,\d+,\d+",
        rf"\1BackgroundAlternate={view_rgb}\nBackgroundNormal={view_rgb}",
        txt,
        count=1,
    )
    txt = re.sub(
        r"(\[Colors:Window\]\n)BackgroundAlternate=\d+,\d+,\d+\nBackgroundNormal=\d+,\d+,\d+",
        rf"\1BackgroundAlternate={view_rgb}\nBackgroundNormal={view_rgb}",
        txt,
        count=1,
    )
    return txt


def desaturate(svg):
    return re.sub(r"#[0-9a-fA-F]{6}\b|#[0-9a-fA-F]{3}\b",
                  lambda m: to_gray(m.group(0)), svg)


def spacer(w, h):
    return f'<rect x="0" y="0" width="{w}" height="{h}" fill="none"/>'


def box(w, h, fill, opacity, x=0, y=0):
    return (f'<rect x="{x}" y="{y}" width="{w}" height="{h}" '
            f'fill="{fill}" fill-opacity="{opacity}"/>')


def slice_size(part, r, long_edge):
    if part in ("top", "bottom"):
        return long_edge, r
    if part in ("left", "right"):
        return r, long_edge
    return r, r


def tab_slices(fill, opacity, edge, edge_opacity):
    """Rounded top corners, square bottom, so the tab sits flush on the pane."""
    r, t = TAB_R, HAIRLINE
    inner = r - t
    quarter = {
        "topleft":  f"M 0,{r} A {r},{r} 0 0 1 {r},0 L {r},{r} Z",
        "topright": f"M 0,0 A {r},{r} 0 0 1 {r},{r} L 0,{r} Z",
    }
    # 1px band hugging the outer curve, so the highlight wraps the corners and
    # meets the straight run drawn by the `top` slice.
    band = {
        "topleft":  f"M 0,{r} A {r},{r} 0 0 1 {r},0 L {r},{t} "
                    f"A {inner},{inner} 0 0 0 {t},{r} Z",
        "topright": f"M 0,0 A {r},{r} 0 0 1 {r},{r} L {inner},{r} "
                    f"A {inner},{inner} 0 0 0 0,{t} Z",
        "top":      None,
    }
    out = {}
    for part in PARTS:
        w, h = slice_size(part, r, EDGE)
        if part in quarter:
            shape = f'<path d="{quarter[part]}" fill="{fill}" fill-opacity="{opacity}"/>'
        else:
            shape = box(w, h, fill, opacity)
        if edge and part in band:
            d = band[part]
            shape += (f'<path d="{d}" fill="{edge}" fill-opacity="{edge_opacity}"/>'
                      if d else box(w, t, edge, edge_opacity))
        out[part] = spacer(w, h) + shape
    return out


def tab_group():
    """Kvantum maps a slice onto its target rect via the SVG bounding box, so
    every group carries an invisible spacer that pins the box to the slice size."""
    out = []
    for row, (state, args) in enumerate(TAB_STATES.items()):
        y = row * SLOT
        slices = tab_slices(*args)
        fill, opacity = args[0], args[1]
        out.append(f'<g id="mtab-{state}" transform="translate(0,{y})">'
                   f"{box(EDGE, EDGE, fill, opacity)}</g>")
        for col, part in enumerate(PARTS, start=1):
            out.append(f'<g id="mtab-{state}-{part}" '
                       f'transform="translate({col * SLOT},{y})">{slices[part]}</g>')
    return out


if "--colors" in sys.argv[1:]:
    sys.stdout.write(emit_colors())
    sys.exit(0)

with open(BASE_SVG, encoding="utf-8") as fh:
    svg = desaturate(fh.read())

# Splice the tab slices in as the last children of the root <svg>.
idx = svg.rindex("</svg>")
extra = '\n<g id="custom-tabs">\n  ' + "\n  ".join(tab_group()) + "\n</g>\n"
sys.stdout.write(svg[:idx] + extra + svg[idx:])
