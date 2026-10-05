// Toolbar icons drawn from tiny path specs on a 24x24 grid (no SVG module needed).
#pragma once
#include <QIcon>

// Commands (absolute coords): M x y, L x y, C x1 y1 x2 y2 x y, Z, O cx cy rx ry (ellipse), R x y w h (rect)
QIcon makeIcon(const char *spec, bool dashed = false);
