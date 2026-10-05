#include "tools.h"
#include "i18n.h"

static double dist(PtF a, PtF b) { return std::hypot(a.x - b.x, a.y - b.y); }
static int ifloor(double v) { return int(std::floor(v)); }
static int iround(double v) { return int(std::lround(v)); }
static int clampi(int v, int lo, int hi) { return std::min(hi, std::max(lo, v)); }

void Tools::setTool(Tool t) {
    if (xf.on) commitXf();
    cancelPoly();
    tool = t;
    resetCrop();
    if (onMode) onMode();
}
SelMode Tools::selModeFor(Mods m) const {
    return m.shift && m.alt ? SelMode::Intersect : m.shift ? SelMode::Add : m.alt ? SelMode::Sub : SelMode(opt.selMode);
}

// ---------- event entry points ----------
void Tools::down(PtF p, Mods m) {
    if (drag) return;
    mouseDoc = p;
    if (xf.on) { xfDown(p); return; }
    switch (tool) {
    case Tool::Move: moveDown(p); break;
    case Tool::Rect: shapeDown(p, m, true); break;
    case Tool::Ellipse: shapeDown(p, m, false); break;
    case Tool::Lasso: lassoDown(p, m); break;
    case Tool::PolyLasso: polyDown(p, m); break;
    case Tool::Wand: floodDown(p, m, true); break;
    case Tool::Crop: cropDown(p); break;
    case Tool::Brush: strokeDown(p, m, false); break;
    case Tool::Eraser: strokeDown(p, m, true); break;
    case Tool::Bucket: floodDown(p, m, false); break;
    case Tool::Gradient: gradDown(p); break;
    case Tool::Picker:
        pick(p);
        drag.reset(new Drag);
        drag->move = [this](PtF q, Mods) { pick(q); };
        break;
    case Tool::Zoom: if (onZoom) onZoom(p, m.alt ? 1 / 1.5 : 1.5); break;
    default: break;
    }
}
void Tools::move(PtF p, Mods m) {
    mouseDoc = p;
    if (drag && drag->move) {
        drag->move(p, m);
        if (drag->flush) drag->flush();
    }
    if (onOverlay) onOverlay();
}
void Tools::up(PtF p) {
    if (!drag) return;
    const std::unique_ptr<Drag> d = std::move(drag);
    if (d->up) d->up(p);
    if (onOverlay) onOverlay();
}
void Tools::doubleClick() {
    if (xf.on) commitXf();
    else if (polyOn) finishPoly();
    else if (tool == Tool::Crop) applyCrop();
}
bool Tools::enter() {
    if (polyOn) { finishPoly(); return true; }
    if (xf.on) { commitXf(); return true; }
    if (tool == Tool::Crop) { applyCrop(); return true; }
    return false;
}
bool Tools::escape() {
    if (polyOn) { cancelPoly(); return true; }
    if (xf.on) { cancelXf(); return true; }
    if (tool == Tool::Crop) { resetCrop(); if (onOverlay) onOverlay(); return true; }
    return false;
}

// ---------- painting ----------
void Tools::pick(PtF p) {
    const Pt q{ifloor(p.x), ifloor(p.y)};
    if (!ed->docRect().contains(q)) return;
    const Px c = ed->flatten().pixel(q.x, q.y);
    if (!pxA(c)) return;
    ed->fg = straightRgb(c);
    if (onColors) onColors();
}
struct Stroke {
    Image base, buf;
    bool erase = false, hard = true;
    double size = 1, hardness = 1, opacity = 1, rem = 0;
    Px rgb = 0;
    PtF last;
    IRect pending;
};
void Tools::strokeDown(PtF p, Mods m, bool erase) {
    if (!erase && m.alt) {
        pick(p);
        drag.reset(new Drag);
        drag->move = [this](PtF q, Mods) { pick(q); };
        return;
    }
    Layer *l = ed->editable();
    if (!l) return;
    auto s = std::make_shared<Stroke>();
    s->base = l->img;
    s->buf = Image(ed->d.w, ed->d.h);
    s->erase = erase;
    s->hard = opt.hardness >= 100;
    s->size = opt.size;
    s->hardness = opt.hardness / 100.0;
    s->opacity = opt.opacity / 100.0;
    s->rgb = erase ? 0x000000 : ed->fg;
    s->last = p;
    auto dab = [s](PtF c) {
        const IRect r = s->hard ? capsule(s->buf, c, c, s->size / 2, s->rgb) : softDab(s->buf, c, s->size, s->hardness, s->rgb, 0.5);
        s->pending = s->pending.united(r);
    };
    auto seg = [s, dab](PtF q) {
        if (s->hard) s->pending = s->pending.united(capsule(s->buf, s->last, q, s->size / 2, s->rgb));
        else {
            const double dx = q.x - s->last.x, dy = q.y - s->last.y, len = std::hypot(dx, dy), step = std::max(1.0, s->size * 0.1);
            double t = step - s->rem;
            for (; t <= len; t += step) dab(PtF{s->last.x + dx * t / len, s->last.y + dy * t / len});
            s->rem = len - (t - step);
        }
        s->last = q;
    };
    // layer = base + stroke buffer (clipped to the selection), recomputed only inside the touched rect
    auto paint = [this, s] {
        const IRect r = s->pending & ed->docRect();
        s->pending = IRect();
        Layer *al = ed->active();
        if (r.empty() || !al) return;
        Image src = s->buf.copy(r);
        if (ed->hasSel()) draw(src, ed->d.sel, 0, 0, Op::In, 1, Blend::Normal, &r);
        draw(al->img, s->base, r.x, r.y, Op::Copy, 1, Blend::Normal, &r);
        draw(al->img, src, r.x, r.y, s->erase ? Op::Out : Op::Over, s->opacity);
        damage(r);
    };
    dab(p);
    paint();
    drag.reset(new Drag);
    drag->move = [seg](PtF q, Mods) { seg(q); };
    drag->flush = paint;
    drag->up = [this](PtF) { ed->commit(); };
}
void Tools::floodDown(PtF p, Mods m, bool wand) {
    Layer *l = wand ? ed->active() : ed->editable();
    const Pt q{ifloor(p.x), ifloor(p.y)};
    if (!l || !ed->docRect().contains(q)) return;
    Alpha a = floodMask(opt.sampleAll ? ed->flatten() : l->img, q.x, q.y, opt.tolerance, opt.contiguous);
    const int w = ed->d.w, h = ed->d.h;
    if (wand) {
        if (opt.antiAlias) blurAlpha(a, w, h, 1, 1);
        ed->finishSel(alphaToMask(a, w, h), selModeFor(m), opt.feather);
    } else {
        ed->paintMasked(*l, alphaToMask(a, w, h, ed->fg), opt.opacity / 100.0);
        ed->commit();
        damage();
    }
}
void Tools::gradDown(PtF o) {
    if (!ed->editable()) return;
    Drag *dg = new Drag;
    drag.reset(dg);
    dg->move = [dg, o](PtF q, Mods) { dg->line0 = o; dg->line1 = q; dg->hasLine = true; };
    dg->up = [this, o](PtF q) {
        if (dist(o, q) < 2) return;
        Image t(ed->d.w, ed->d.h);
        linearGradient(t, o, q, ed->fg, 255, opt.toTransparent ? ed->fg : ed->bg, opt.toTransparent ? 0 : 255);
        ed->paintMasked(*ed->active(), t, opt.opacity / 100.0);
        ed->commit();
        damage();
    };
}

// ---------- selections ----------
void Tools::shapeDown(PtF o, Mods m, bool rect) {
    const SelMode mode = selModeFor(m);
    Drag *dg = new Drag;
    drag.reset(dg);
    auto geom = [this, o](PtF q) {
        const int x0 = clampi(iround(std::min(o.x, q.x)), 0, ed->d.w), x1 = clampi(iround(std::max(o.x, q.x)), 0, ed->d.w);
        const int y0 = clampi(iround(std::min(o.y, q.y)), 0, ed->d.h), y1 = clampi(iround(std::max(o.y, q.y)), 0, ed->d.h);
        return IRect{x0, y0, x1 - x0, y1 - y0};
    };
    dg->move = [dg, geom, rect](PtF q, Mods) {
        const IRect r = geom(q);
        dg->path.clear();
        if (rect) dg->path = {PtF{double(r.x), double(r.y)}, PtF{double(r.r()), double(r.y)}, PtF{double(r.r()), double(r.b())}, PtF{double(r.x), double(r.b())}};
        else for (int i = 0; i < 72; i++) dg->path.push_back(PtF{r.x + r.w / 2.0 + std::cos(i * M_PI / 36) * r.w / 2.0, r.y + r.h / 2.0 + std::sin(i * M_PI / 36) * r.h / 2.0});
    };
    dg->up = [this, geom, mode, rect](PtF q) {
        const IRect r = geom(q);
        if (r.w < 1 || r.h < 1) { if (mode == SelMode::Replace) ed->selectNone(); return; }
        ed->finishSel(rect ? rectMask(ed->d.w, ed->d.h, r) : ellipseMask(ed->d.w, ed->d.h, RectF(r)), mode, opt.feather);
    };
}
void Tools::lassoDown(PtF o, Mods m) {
    const SelMode mode = selModeFor(m);
    Drag *dg = new Drag;
    drag.reset(dg);
    dg->path.push_back(o);
    dg->move = [dg](PtF q, Mods) { dg->path.push_back(q); };
    dg->up = [this, dg, mode](PtF) {
        if (dg->path.size() < 3) { if (mode == SelMode::Replace) ed->selectNone(); return; }
        ed->finishSel(polygonMask(ed->d.w, ed->d.h, dg->path), mode, opt.feather);
    };
}
// polygonal lasso: click adds a corner; Enter, double-click or clicking the first corner closes it
void Tools::polyDown(PtF p, Mods m) {
    if (!polyOn) {
        polyOn = true;
        poly.clear();
        poly.push_back(p);
        polyMode = selModeFor(m);
    } else if (poly.size() >= 3 && dist(poly.front(), p) * zoom <= 8) finishPoly();
    else poly.push_back(p);
    if (onOverlay) onOverlay();
}
void Tools::finishPoly() {
    if (!polyOn) return;
    polyOn = false;
    if (poly.size() >= 3) ed->finishSel(polygonMask(ed->d.w, ed->d.h, poly), polyMode, opt.feather);
    poly.clear();
    if (onOverlay) onOverlay();
}
void Tools::cancelPoly() {
    if (!polyOn) return;
    polyOn = false;
    poly.clear();
    if (onOverlay) onOverlay();
}

// ---------- move ----------
void Tools::moveDown(PtF o) {
    Layer *l = ed->editable();
    if (!l) return;
    auto L = std::make_shared<Lifted>(ed->lift(*l));
    auto delta = std::make_shared<Pt>();
    drag.reset(new Drag);
    drag->hideSel = true;
    drag->move = [this, L, delta, o](PtF q, Mods) {
        *delta = Pt{iround(q.x - o.x), iround(q.y - o.y)};
        const IRect s{0, 0, L->flt.w, L->flt.h};
        ed->drawParts(ed->active()->img, *L, RectF(s), RectF(s.moved(L->pos + *delta)), Pt{0, 0});
        damage();
    };
    drag->up = [this, L, delta](PtF) { if (delta->x || delta->y) ed->moveBy(*ed->active(), *L, delta->x, delta->y); };
}
void Tools::nudge(int dx, int dy) {
    Layer *l = ed->editable();
    if (l && !xf.on && !drag) ed->moveBy(*l, ed->lift(*l), dx, dy);
}

// ---------- box helpers (free transform and crop) ----------
// 0 = outside, 1 = inside, 2 = on one of the 8 handles (hx, hy in -1..1); the box is rotated around its center
static int boxHit(RectF s, double angle, PtF p, double zoom, int &hx, int &hy, PtF *local = nullptr) {
    const double rad = -angle * M_PI / 180;
    const PtF c = s.center(), v = p - c;
    const PtF lp{c.x + v.x * std::cos(rad) - v.y * std::sin(rad), c.y + v.x * std::sin(rad) + v.y * std::cos(rad)};
    if (local) *local = lp;
    hx = hy = 0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            if (!i && !j) continue;
            if (std::fabs(s.x + s.w * (i + 1) / 2.0 - lp.x) * zoom <= 8 && std::fabs(s.y + s.h * (j + 1) / 2.0 - lp.y) * zoom <= 8) { hx = i; hy = j; }
        }
    return hx || hy ? 2 : s.contains(lp) ? 1 : 0;
}
// moves the edges picked by (hx, hy); keep = corners keep the aspect ratio, center = scale around the center
static void resizeBox(RectF s, int hx, int hy, double dx, double dy, bool keep, bool center, double &x, double &y, double &x2, double &y2) {
    x = s.x; y = s.y; x2 = s.x + s.w; y2 = s.y + s.h;
    if (hx < 0) x = std::min(x + dx, x2 - 1);
    if (hx > 0) x2 = std::max(x2 + dx, x + 1);
    if (hy < 0) y = std::min(y + dy, y2 - 1);
    if (hy > 0) y2 = std::max(y2 + dy, y + 1);
    if (hx && hy && keep) {
        const double sc = std::max((x2 - x) / s.w, (y2 - y) / s.h), nw = s.w * sc, nh = s.h * sc;
        if (hx < 0) x = x2 - nw; else x2 = x + nw;
        if (hy < 0) y = y2 - nh; else y2 = y + nh;
    }
    if (center) {
        const PtF c = s.center();
        if (hx) { const double half = std::max(0.5, hx < 0 ? c.x - x : x2 - c.x); x = c.x - half; x2 = c.x + half; }
        if (hy) { const double half = std::max(0.5, hy < 0 ? c.y - y : y2 - c.y); y = c.y - half; y2 = c.y + half; }
    }
}
static CursorKind boxCursor(int hit, int hx, int hy, double angle, CursorKind outside) {
    if (hit == 0) return outside;
    if (hit == 1) return CursorKind::Move;
    double deg = std::fmod(std::atan2(double(hy), double(hx)) * 180 / M_PI + angle, 180);
    if (deg < 0) deg += 180;
    return deg < 22.5 || deg >= 157.5 ? CursorKind::ResizeH : deg < 67.5 ? CursorKind::ResizeFDiag : deg < 112.5 ? CursorKind::ResizeV : CursorKind::ResizeBDiag;
}
CursorKind Tools::cursor() const {
    int hx, hy;
    // hit test first: argument evaluation order is unspecified, so it must not share a call with hx / hy
    if (xf.on) { const int hit = boxHit(xf.dst, xf.angle, mouseDoc, zoom, hx, hy); return boxCursor(hit, hx, hy, xf.angle, CursorKind::Rotate); }
    if (tool == Tool::Crop) { const int hit = boxHit(crop, 0, mouseDoc, zoom, hx, hy); return boxCursor(hit, hx, hy, 0, CursorKind::Cross); }
    return tool == Tool::Move ? CursorKind::Move : tool == Tool::Hand ? CursorKind::Hand : CursorKind::Cross;
}

// ---------- crop ----------
// Handles resize (Shift keeps the aspect ratio), dragging inside moves the box, dragging outside draws a new one.
void Tools::cropDown(PtF p) {
    int hx, hy;
    const int hit = boxHit(crop, 0, p, zoom, hx, hy);
    const RectF s = crop;
    drag.reset(new Drag);
    drag->move = [this, s, p, hit, hx, hy](PtF q, Mods m) {
        double x = s.x, y = s.y, x2 = s.x + s.w, y2 = s.y + s.h;
        if (hit == 0) { x = std::min(p.x, q.x); x2 = std::max(p.x, q.x); y = std::min(p.y, q.y); y2 = std::max(p.y, q.y); }
        else if (hit == 1) { const PtF dd = q - p; x += dd.x; x2 += dd.x; y += dd.y; y2 += dd.y; }
        else resizeBox(s, hx, hy, q.x - p.x, q.y - p.y, m.shift, false, x, y, x2, y2);
        int rx = iround(x), ry = iround(y), rw = clampi(iround(x2 - x), 1, MAX_DIM), rh = clampi(iround(y2 - y), 1, MAX_DIM);
        const int lx0 = -PAD, ly0 = -PAD, lx1 = ed->d.w + PAD, ly1 = ed->d.h + PAD;
        if (hit == 1) { rx = clampi(rx, lx0, lx1 - rw); ry = clampi(ry, ly0, ly1 - rh); }
        else {
            const int ex = std::min(rx + rw, lx1), ey = std::min(ry + rh, ly1);
            rx = std::max(rx, lx0); ry = std::max(ry, ly0); rw = ex - rx; rh = ey - ry;
        }
        if (rw >= 1 && rh >= 1) crop = RectF(rx, ry, rw, rh);
    };
}
void Tools::applyCrop() {
    const IRect r = crop.toRect();
    if (r != ed->docRect()) {
        ed->cropRect(r);
        if (onFit) onFit();
    }
    resetCrop();
    if (onOverlay) onOverlay();
}

// ---------- free transform ----------
void Tools::startTransform() {
    Layer *l = ed->editable();
    if (!l || xf.on) return;
    const Lifted L = ed->lift(*l);
    const IRect bb = alphaBounds(L.flt);
    if (bb.empty()) return ed->toast(TR("변형할 픽셀이 없습니다"));
    xf.on = true;
    xf.L = L;
    xf.src = bb;
    xf.orig = bb.moved(L.pos);
    xf.dst = RectF(xf.orig);
    xf.angle = 0;
    if (onMode) onMode();
}
void Tools::drawXf() {
    ed->drawParts(ed->active()->img, xf.L, RectF(xf.src), xf.dst, Pt{0, 0}, xf.angle);
    damage();
}
void Tools::commitXf() {
    if (!xf.on) return;
    xf.on = false;
    if (!(xf.dst == RectF(xf.orig)) || xf.angle != 0) {
        ed->commitParts(*ed->active(), xf.L, xf.src, xf.dst, xf.angle);
        if (ed->hasSel()) {
            Image m(ed->d.w, ed->d.h);
            drawTransformed(m, ed->d.sel, RectF(xf.orig), xf.dst, xf.angle);
            ed->setSelection(m);
        }
        ed->commit();
    }
    xf.L = Lifted();
    xf.angle = 0;
    if (onMode) onMode();
    damage();
}
void Tools::cancelXf() {
    if (!xf.on) return;
    xf.dst = RectF(xf.orig);
    xf.angle = 0;
    drawXf();
    xf.on = false;
    xf.L = Lifted();
    if (onMode) onMode();
}
int Tools::xfHit(PtF p, int &hx, int &hy) const { return boxHit(xf.dst, xf.angle, p, zoom, hx, hy); }
// Handle: resize. Inside the box: move. Outside: rotate (Shift snaps to 15 degrees).
void Tools::xfDown(PtF p) {
    const RectF s = xf.dst;
    const double a0 = xf.angle, rad = a0 * M_PI / 180;
    const PtF c0 = s.center();
    int hx, hy;
    PtF lp;
    const int hit = boxHit(s, a0, p, zoom, hx, hy, &lp);
    drag.reset(new Drag);
    drag->move = [this, s, a0, rad, c0, hx, hy, hit, lp, p](PtF q, Mods m) {
        if (hit == 0) {
            double a = a0 + (std::atan2(q.y - c0.y, q.x - c0.x) - std::atan2(p.y - c0.y, p.x - c0.x)) * 180 / M_PI;
            if (m.shift) a = std::round(a / 15) * 15;
            a = std::fmod(a, 360);
            if (a > 180) a -= 360;
            if (a <= -180) a += 360;
            xf.angle = a;
            char buf[64];
            snprintf(buf, sizeof buf, TR("회전 %.1f도"), a);
            ed->toast(buf);
            return;
        }
        if (hit == 1) {
            const PtF dd = q - p;
            xf.dst = a0 == 0 ? RectF(iround(s.x + dd.x), iround(s.y + dd.y), s.w, s.h) : s.moved(dd);
            return;
        }
        int ix, iy;
        PtF lq;
        boxHit(s, a0, q, zoom, ix, iy, &lq);
        double x, y, x2, y2;
        resizeBox(s, hx, hy, lq.x - lp.x, lq.y - lp.y, !m.shift, m.alt, x, y, x2, y2);
        const double w = std::max(1.0, std::round(x2 - x)), h = std::max(1.0, std::round(y2 - y));
        if (a0 == 0) { xf.dst = RectF(iround(x), iround(y), w, h); return; }
        // the box was resized in its own rotated frame: put its new center back into doc coords
        const double ox = (x + x2) / 2 - c0.x, oy = (y + y2) / 2 - c0.y;
        const PtF nc{c0.x + ox * std::cos(rad) - oy * std::sin(rad), c0.y + ox * std::sin(rad) + oy * std::cos(rad)};
        xf.dst = RectF(nc.x - w / 2, nc.y - h / 2, w, h);
    };
    drag->flush = [this] { drawXf(); };
}
