#!/usr/bin/env python3
"""Write the R Painter HVIF icon into RPainter.rdef.

The icon is the same palette-and-brush drawing as linux/resources/r-painter.svg,
re-expressed in Haiku's vector icon format (HVIF) because Tracker and Deskbar
only draw BEOS:ICON, which has to be HVIF. The format is written by hand here:
styles (solid colors and gradients), paths (points with bezier handles) and
shapes (a style applied to paths, optionally through a stroke transformer).

    python3 renku/make_icon.py            # rewrites the vector_icon block in RPainter.rdef
    python3 renku/make_icon.py out.hvif   # also dumps the raw bytes, for hvif-check
"""

import io
import math
import os
import re
import struct
import sys

# --- HVIF primitives --------------------------------------------------------

STYLE_SOLID = 1
STYLE_GRADIENT = 2
GRADIENT_LINEAR = 0
GRADIENT_CIRCULAR = 1
GRADIENT_FLAG_TRANSFORM = 1 << 1
PATH_FLAG_CLOSED = 1 << 1
PATH_FLAG_USES_COMMANDS = 1 << 2
PATH_FLAG_NO_CURVES = 1 << 3
CMD_H_LINE, CMD_V_LINE, CMD_LINE, CMD_CURVE = 0, 1, 2, 3
SHAPE_PATH_SOURCE = 0x0A
SHAPE_FLAG_HAS_TRANSFORMERS = 1 << 4
TRANSFORMER_STROKE = 23
JOIN_MITER, JOIN_ROUND, JOIN_BEVEL = 0, 2, 3
CAP_BUTT, CAP_SQUARE, CAP_ROUND = 0, 1, 2


def coord(value):
    """One byte for integers in -32..95, otherwise two bytes with 1/102 precision."""
    value = max(-128.0, min(192.0, value))
    if value == int(value) and -32 <= value <= 95:
        return bytes([int(value) + 32])
    encoded = int(round((value + 128.0) * 102.0)) | 0x8000
    return bytes([encoded >> 8, encoded & 0xFF])


def float24(value):
    """Haiku's 24-bit float: sign, 6-bit exponent biased by 32, 17-bit mantissa."""
    if value == 0.0:
        return b"\0\0\0"
    bits = struct.unpack(">I", struct.pack(">f", value))[0]
    sign = bits >> 31
    exponent = ((bits >> 23) & 0xFF) - 127
    mantissa = bits & 0x7FFFFF
    if exponent >= 32 or exponent < -32:
        raise ValueError("float out of 24-bit range: %r" % value)
    packed = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
    return bytes([packed >> 16, (packed >> 8) & 0xFF, packed & 0xFF])


def matrix(sx, shy, shx, sy, tx, ty):
    # agg::trans_affine order: x' = x*sx + y*shx + tx, y' = x*shy + y*sy + ty
    return b"".join(float24(v) for v in (sx, shy, shx, sy, tx, ty))


def rgba(hex_color, alpha=1.0):
    hex_color = hex_color.lstrip("#")
    r, g, b = (int(hex_color[i:i + 2], 16) for i in (0, 2, 4))
    return (r, g, b, int(round(alpha * 255)))


class Icon:
    def __init__(self):
        self.styles = []
        self.paths = []
        self.shapes = []

    # styles ---------------------------------------------------------------
    def solid(self, color):
        self.styles.append(bytes([STYLE_SOLID]) + bytes(color))
        return len(self.styles) - 1

    def linear(self, a, b, stops):
        """Gradient running from point a (offset 0) to point b (offset 1).

        HVIF linear gradients span x = -64..64 of their own space; the matrix
        maps that onto the icon."""
        dx, dy = (b[0] - a[0]) / 128.0, (b[1] - a[1]) / 128.0
        m = matrix(dx, dy, -dy, dx, (a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)
        return self._gradient(GRADIENT_LINEAR, m, stops)

    def radial(self, center, rx, ry, stops):
        """Gradient from center (offset 0) to an ellipse of radii rx, ry (offset 1).

        HVIF circular gradients run from radius 0 to 64 of their own space."""
        m = matrix(rx / 64.0, 0, 0, ry / 64.0, center[0], center[1])
        return self._gradient(GRADIENT_CIRCULAR, m, stops)

    def _gradient(self, kind, m, stops):
        data = bytearray([STYLE_GRADIENT, kind, GRADIENT_FLAG_TRANSFORM, len(stops)])
        data += m
        for offset, color in stops:
            data.append(int(round(offset * 255)))
            data += bytes(color)
        self.styles.append(bytes(data))
        return len(self.styles) - 1

    # paths ----------------------------------------------------------------
    def path(self, points, closed=True):
        """points: list of (point, in_handle, out_handle); handles equal to the
        point mean a straight corner."""
        data = bytearray()
        flags = PATH_FLAG_CLOSED if closed else 0
        if all(p == pin == pout for p, pin, pout in points):
            flags |= PATH_FLAG_NO_CURVES
            data += bytes([flags, len(points)])
            for p, _, _ in points:
                data += coord(p[0]) + coord(p[1])
        else:
            flags |= PATH_FLAG_USES_COMMANDS
            data += bytes([flags, len(points)])
            commands = []
            body = bytearray()
            last = (0.0, 0.0)
            for p, pin, pout in points:
                if p == pin == pout:
                    if p[1] == last[1]:
                        commands.append(CMD_H_LINE)
                        body += coord(p[0])
                    elif p[0] == last[0]:
                        commands.append(CMD_V_LINE)
                        body += coord(p[1])
                    else:
                        commands.append(CMD_LINE)
                        body += coord(p[0]) + coord(p[1])
                else:
                    commands.append(CMD_CURVE)
                    for x, y in (p, pin, pout):
                        body += coord(x) + coord(y)
                last = p
            # Four two-bit commands per byte, low bits first.
            packed = bytearray((len(points) + 3) // 4)
            for i, c in enumerate(commands):
                packed[i // 4] |= c << (2 * (i % 4))
            data += packed + body
        self.paths.append(bytes(data))
        return len(self.paths) - 1

    # shapes ---------------------------------------------------------------
    def fill(self, style, *paths):
        self.shapes.append(bytes([SHAPE_PATH_SOURCE, style, len(paths)]) + bytes(paths) + b"\0")

    def stroke(self, style, width, *paths, join=JOIN_ROUND, cap=CAP_BUTT, miter=4):
        data = bytearray([SHAPE_PATH_SOURCE, style, len(paths)]) + bytes(paths)
        data += bytes([SHAPE_FLAG_HAS_TRANSFORMERS, 1, TRANSFORMER_STROKE,
                       int(round(width)) + 128, (cap << 4) | join, miter])
        self.shapes.append(bytes(data))

    def build(self):
        out = bytearray(b"ncif")
        out.append(len(self.styles))
        out += b"".join(self.styles)
        out.append(len(self.paths))
        out += b"".join(self.paths)
        out.append(len(self.shapes))
        out += b"".join(self.shapes)
        return bytes(out)


# --- geometry helpers -------------------------------------------------------

KAPPA = 0.5522847498


def ellipse(cx, cy, rx, ry, transform=None, reverse=False):
    kx, ky = KAPPA * rx, KAPPA * ry
    pts = [
        ((cx + rx, cy), (cx + rx, cy - ky), (cx + rx, cy + ky)),
        ((cx, cy + ry), (cx + kx, cy + ry), (cx - kx, cy + ry)),
        ((cx - rx, cy), (cx - rx, cy + ky), (cx - rx, cy - ky)),
        ((cx, cy - ry), (cx - kx, cy - ry), (cx + kx, cy - ry)),
    ]
    if reverse:
        pts = [(p, pout, pin) for p, pin, pout in reversed(pts)]
    if transform:
        pts = [tuple(transform(q) for q in triple) for triple in pts]
    return pts


def svg_path(d, transform=None):
    """Absolute M/L/C/Z path data into HVIF points. A trailing point that
    repeats the first is folded into it so the closing segment keeps its curve."""
    tokens = re.findall(r"[MLCZ]|-?\d*\.?\d+", d)
    pts = []
    i = 0
    cmd = None
    while i < len(tokens):
        t = tokens[i]
        if t in "MLCZ":
            cmd = t
            i += 1
            if cmd == "Z":
                continue
        if cmd in ("M", "L"):
            p = (float(tokens[i]), float(tokens[i + 1]))
            i += 2
            pts.append([p, p, p])
        elif cmd == "C":
            c1 = (float(tokens[i]), float(tokens[i + 1]))
            c2 = (float(tokens[i + 2]), float(tokens[i + 3]))
            p = (float(tokens[i + 4]), float(tokens[i + 5]))
            i += 6
            pts[-1][2] = c1
            pts.append([p, c2, p])
    if len(pts) > 1 and pts[-1][0] == pts[0][0]:
        pts[0][1] = pts[-1][1]
        pts.pop()
    if transform:
        pts = [[transform(q) for q in triple] for triple in pts]
    return [tuple(triple) for triple in pts]


def signed_area(points):
    s = 0.0
    for i, (p, _, _) in enumerate(points):
        q = points[(i + 1) % len(points)][0]
        s += p[0] * q[1] - q[0] * p[1]
    return s


# --- the drawing ------------------------------------------------------------

def build_icon():
    icon = Icon()

    # ground shadow
    icon.fill(icon.solid((0, 0, 0, 56)), icon.path(ellipse(33, 55, 25, 5.5)))

    # palette: a darker slab underneath, the wooden top on it; the thumb hole
    # runs the other way round so it stays a hole under the non-zero fill rule
    outline = ("M5 40 C5 28 19 20 35 20 C50 20 60 27 60 36 C60 43 54 46 48 45 "
               "C44 44.5 41 46 42 49 C43 53 38 56 28 56 C15 56 5 50 5 40 Z")
    for dy, fill_style, stroke_color in (
            (0, icon.solid(rgba("#9a6222")), "#5a3408"),
            (-3, icon.linear((5, 17), (60, 53), [(0, rgba("#ffe9b8")), (0.5, rgba("#eec57c")),
                                                 (1, rgba("#c88a3c"))]), "#6e420e")):
        outer = svg_path(outline, lambda q, dy=dy: (q[0], q[1] + dy))
        hole = ellipse(20.6, 45 + dy, 4.6, 3)
        if signed_area(hole) * signed_area(outer) > 0:
            hole = ellipse(20.6, 45 + dy, 4.6, 3, reverse=True)
        po, ph = icon.path(outer), icon.path(hole)
        icon.fill(fill_style, po, ph)
        icon.stroke(icon.solid(rgba(stroke_color)), 2, po, ph)

    # palette highlight
    icon.stroke(icon.solid((255, 255, 255, 166)), 2,
                icon.path(svg_path("M9 34 C11 26 22 20.5 34 20"), closed=False), cap=CAP_ROUND)

    # paint blobs: radial gradient lit from the upper left, dark outline, white glint
    white75 = icon.solid((255, 255, 255, 191))
    for cx, cy, rx, ry, light, dark, edge, gx, gy, grx, gry in (
            (17.5, 31, 5, 3.3, "#b9f08a", "#3f9a1c", "#1f5c0a", 16, 29.8, 1.7, 0.8),
            (29, 24.5, 5, 3.3, "#fff6a8", "#f0b400", "#8a6200", 27.5, 23.3, 1.7, 0.8),
            (42, 24.5, 5, 3.3, "#ff9a8c", "#d42a1e", "#7a120a", 40.5, 23.3, 1.7, 0.8),
            (52, 31.5, 4.6, 3.1, "#a8d4ff", "#2468d8", "#123c8c", 50.6, 30.4, 1.6, 0.75)):
        # SVG radialGradient cx=.35 cy=.3 r=.8 in bounding-box units
        center = (cx - rx + 0.7 * rx, cy - ry + 0.6 * ry)
        style = icon.radial(center, 1.6 * rx, 1.6 * ry, [(0, rgba(light)), (1, rgba(dark))])
        p = icon.path(ellipse(cx, cy, rx, ry))
        icon.fill(style, p)
        icon.stroke(icon.solid(rgba(edge)), 1, p)
        icon.fill(white75, icon.path(ellipse(gx, gy, grx, gry)))

    # brush, lying across the palette: SVG translate(12 58) rotate(-47)
    a = math.radians(-47)
    ca, sa = math.cos(a), math.sin(a)

    def T(q):
        return (12 + q[0] * ca - q[1] * sa, 58 + q[0] * sa + q[1] * ca)

    icon.fill(icon.solid((0, 0, 0, 46)), icon.path(ellipse(30, 5.5, 26, 2.2, T)))

    handle = icon.path(svg_path("M1 -1.6 L36 -3.2 L36 3.2 L1 1.6 C-1.3 1.6 -1.3 -1.6 1 -1.6 Z", T))
    icon.fill(icon.linear(T((0, -3.5)), T((0, 3.5)),
                          [(0, rgba("#ff8f7a")), (0.45, rgba("#e8382a")), (1, rgba("#9c160e"))]), handle)
    icon.stroke(icon.solid(rgba("#5e0c06")), 2, handle)
    icon.stroke(icon.solid((255, 255, 255, 153)), 1,
                icon.path(svg_path("M3 -0.9 L34 -2.1", T), closed=False), cap=CAP_ROUND)

    ferrule = icon.path(svg_path("M36 -3.7 L44.5 -3.7 L44.5 3.7 L36 3.7 Z", T))
    icon.fill(icon.linear(T((0, -4)), T((0, 4)),
                          [(0, rgba("#ffffff")), (0.5, rgba("#c9ced6")), (1, rgba("#7c838f"))]), ferrule)
    icon.stroke(icon.solid(rgba("#474c55")), 1, ferrule)
    icon.stroke(icon.solid((0x47, 0x4c, 0x55, 153)), 1,
                icon.path(svg_path("M38.8 -3.7 L38.8 3.7", T), closed=False),
                icon.path(svg_path("M41.6 -3.7 L41.6 3.7", T), closed=False))

    bristle = icon.path(svg_path("M44.5 -3.7 C49 -5.6 54 -4 59.5 0.4 C54 3.6 49 5.2 44.5 3.7 Z", T))
    icon.fill(icon.linear(T((0, -5)), T((0, 5)),
                          [(0, rgba("#9fd0ff")), (0.5, rgba("#3d8bf0")), (1, rgba("#1549b0"))]), bristle)
    icon.stroke(icon.solid(rgba("#0d3684")), 2, bristle)
    icon.stroke(icon.solid((255, 255, 255, 179)), 1,
                icon.path(svg_path("M46.5 -2.6 C49.5 -3.4 52.5 -2.6 55 -1", T), closed=False), cap=CAP_ROUND)

    return icon.build()


def resource(data):
    value = "".join("%02X" % b for b in data)
    lines = ["resource vector_icon {"]
    lines += ['\t$"%s"' % value[i:i + 64] for i in range(0, len(value), 64)]
    lines.append("};")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    data = build_icon()
    if len(sys.argv) > 1:
        with open(sys.argv[1], "wb") as f:
            f.write(data)
    rdef = os.path.join(os.path.dirname(os.path.abspath(__file__)), "RPainter.rdef")
    text = io.open(rdef, encoding="utf-8").read()
    block = re.compile(r"^resource vector_icon \{.*?^\};\n?", re.S | re.M)
    if block.search(text):
        text = block.sub(lambda _: resource(data), text, count=1)
    else:
        text = text.rstrip("\n") + "\n\n" + resource(data)
    io.open(rdef, "w", encoding="utf-8").write(text)
    print("RPainter.rdef: wrote %d-byte HVIF" % len(data))
