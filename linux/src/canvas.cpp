#include "canvas.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <cmath>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define EVPOS(e) (e)->position()
#else
#define EVPOS(e) (e)->localPos()
#endif

Canvas::Canvas(Editor *e, QWidget *parent) : QWidget(parent), ed(e) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setMinimumSize(300, 200);
    antTimer.setInterval(100);
    connect(&antTimer, &QTimer::timeout, this, [this] {
        if (ed->d.selEdges.isEmpty() && !polyOn && !(drag && (!drag->path.isEmpty() || drag->hasLine))) return;
        antPhase = (antPhase + 1) % 8;
        update();
    });
    antTimer.start();
}

void Canvas::setTool(Tool t) {
    if (xf.on) commitXf();
    cancelPoly();
    tool = t;
    crop = ed->docRect();
    updateCursor();
    if (onMode) onMode();
    update();
}

// ---------- view ----------
void Canvas::damage(QRect r) {
    if (r.isNull()) allDirty = true;
    else dirty = dirty.united(r);
    update();
}
void Canvas::ensureComp() {
    if (comp.size() != QSize(ed->d.w, ed->d.h)) { comp = blankImage(ed->d.w, ed->d.h); allDirty = true; crop = ed->docRect(); }
    if (allDirty) ed->composite(comp, ed->docRect());
    else if (!dirty.isEmpty()) ed->composite(comp, dirty & ed->docRect());
    allDirty = false;
    dirty = QRect();
}
void Canvas::zoomAt(QPointF w, double f) {
    const double nz = qBound(0.02, zoom * f, 64.0);
    off = w - (w - off) * nz / zoom;
    zoom = nz;
    if (onStatus) onStatus();
    update();
}
void Canvas::fit(double maxZoom) {
    zoom = qBound(0.02, qMin((width() - 60.0) / ed->d.w, (height() - 60.0) / ed->d.h), maxZoom);
    off = QPointF(qRound((width() - ed->d.w * zoom) / 2), qRound((height() - ed->d.h * zoom) / 2));
    if (onStatus) onStatus();
    update();
}
QPen Canvas::antPen() const {
    QImage pat(8, 8, QImage::Format_RGB32);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) pat.setPixel(x, y, (x + y + antPhase) % 8 < 4 ? 0xff000000 : 0xffffffff);
    QPen pen(QBrush(pat), 1);
    pen.setCosmetic(true);
    return pen;
}
void Canvas::paintEvent(QPaintEvent *) {
    static const QBrush checker = [] {
        QImage c(16, 16, QImage::Format_RGB32);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) c.setPixel(x, y, ((x >> 3) + (y >> 3)) % 2 ? 0xffc8c8c8 : 0xffffffff);
        return QBrush(c);
    }();
    ensureComp();
    QPainter p(this);
    p.fillRect(rect(), QColor(27, 27, 27));
    const QRectF dr(off.x(), off.y(), ed->d.w * zoom, ed->d.h * zoom);
    p.fillRect(dr, checker);
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom < 1);
    p.drawImage(dr, comp);

    const QTransform toView = QTransform::fromScale(zoom, zoom) * QTransform::fromTranslate(off.x(), off.y());
    if (!ed->d.selEdges.isEmpty() && !xf.on && !(drag && drag->hideSel)) {
        QVector<QLineF> lines;
        lines.reserve(ed->d.selEdges.size());
        for (const QLine &l : ed->d.selEdges) lines.append(toView.map(QLineF(l)));
        p.setPen(antPen());
        p.drawLines(lines);
    }
    if (polyOn) {
        QPolygonF pts = poly;
        if (hasMouse) pts << mouseDoc;
        p.setPen(antPen());
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(toView.map(pts));
        const QPointF first = toView.map(poly.first());
        p.drawRect(QRectF(first.x() - 3, first.y() - 3, 6, 6));
    }
    if (tool == Tool::Crop && !xf.on) {
        const QRectF r = toView.mapRect(crop);
        QPainterPath dim;
        dim.addRect(rect());
        dim.addRect(r);
        p.fillPath(dim, QColor(0, 0, 0, 150)); // odd-even fill: everything outside the crop box
        p.setPen(QColor(255, 255, 255, 90));
        for (int i = 1; i <= 2; i++) {
            p.drawLine(QPointF(r.x() + r.width() * i / 3, r.y()), QPointF(r.x() + r.width() * i / 3, r.bottom()));
            p.drawLine(QPointF(r.x(), r.y() + r.height() * i / 3), QPointF(r.right(), r.y() + r.height() * i / 3));
        }
        p.setPen(Qt::white);
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        p.setPen(QColor(40, 40, 40));
        p.setBrush(Qt::white);
        for (int hy = 0; hy <= 2; hy++)
            for (int hx = 0; hx <= 2; hx++)
                if (hx != 1 || hy != 1) p.drawRect(QRectF(r.x() + r.width() * hx / 2 - 4, r.y() + r.height() * hy / 2 - 4, 8, 8));
    }
    if (drag && !drag->path.isEmpty()) { p.setPen(antPen()); p.setBrush(Qt::NoBrush); p.drawPath(toView.map(drag->path)); }
    if (drag && drag->hasLine) { p.setPen(antPen()); p.drawLine(toView.map(drag->line)); }
    if (xf.on) {
        const QRectF r = toView.mapRect(xf.dst);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, xf.angle != 0);
        p.translate(r.center());
        p.rotate(xf.angle);
        p.translate(-r.center());
        p.setPen(QColor(45, 127, 249));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        p.setBrush(Qt::white);
        for (int hy = 0; hy <= 2; hy++)
            for (int hx = 0; hx <= 2; hx++)
                if (hx != 1 || hy != 1) p.drawRect(QRectF(r.x() + r.width() * hx / 2 - 4, r.y() + r.height() * hy / 2 - 4, 8, 8));
        p.restore();
    } else if (hasMouse && (tool == Tool::Brush || tool == Tool::Eraser)) {
        const double r = qMax(1.0, opt.size * zoom / 2);
        p.setRenderHint(QPainter::Antialiasing);
        p.setCompositionMode(QPainter::CompositionMode_Difference);
        p.setPen(Qt::white);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(mouseW, r, r);
    }
}

// ---------- events ----------
void Canvas::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::RightButton || dragging()) return;
    setFocus();
    const QPointF w = EVPOS(e), p = toDoc(w);
    if (space || e->button() == Qt::MiddleButton || tool == Tool::Hand) {
        panning = true; panStart = w; panOff = off;
    } else if (xf.on) xfDown(p);
    else toolDown(p, w, e->modifiers());
    update();
}
void Canvas::mouseMoveEvent(QMouseEvent *e) {
    mouseW = EVPOS(e);
    mouseDoc = toDoc(mouseW);
    hasMouse = true;
    if ((xf.on || tool == Tool::Crop) && !dragging()) updateCursor();
    if (panning) off = panOff + (mouseW - panStart);
    else if (drag && drag->move) {
        drag->move(mouseDoc, e->modifiers());
        if (drag->flush) drag->flush();
    }
    if (onStatus) onStatus();
    update();
}
void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (panning) { panning = false; return; }
    if (!drag) return;
    const std::unique_ptr<Drag> d = std::move(drag);
    if (d->up) d->up(toDoc(EVPOS(e)));
    update();
}
void Canvas::mouseDoubleClickEvent(QMouseEvent *) {
    if (xf.on) commitXf();
    else if (polyOn) finishPoly();
    else if (tool == Tool::Crop) applyCrop();
}
void Canvas::leaveEvent(QEvent *) { hasMouse = false; update(); }
void Canvas::wheelEvent(QWheelEvent *e) {
    const QPoint px = e->pixelDelta(), ang = e->angleDelta();
    if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) {
        const int dy = !px.isNull() ? px.y() + px.x() : ang.y() + ang.x(); // Alt swaps the wheel axis on some platforms
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        const QPointF at = e->position();
#else
        const QPointF at = e->posF(); // Qt 5.12 (e.g. Ubuntu 20.04 / Mint 20)
#endif
        zoomAt(at, std::pow(1.0015, !px.isNull() ? dy * 4 : dy));
    } else {
        off += !px.isNull() ? QPointF(px) : QPointF(ang) / 2;
        update();
    }
}
bool Canvas::event(QEvent *e) {
    if (e->type() == QEvent::NativeGesture) {
        auto *g = static_cast<QNativeGestureEvent *>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture) { zoomAt(mapFromGlobal(QCursor::pos()), 1 + g->value()); return true; }
    }
    return QWidget::event(e);
}
void Canvas::keyPressEvent(QKeyEvent *e) {
    const int k = e->key();
    if (k == Qt::Key_Space) { if (!e->isAutoRepeat()) space = true; return; }
    const bool enter = k == Qt::Key_Return || k == Qt::Key_Enter;
    if (polyOn && enter) return finishPoly();
    if (polyOn && k == Qt::Key_Escape) return cancelPoly();
    if (tool == Tool::Crop && !xf.on && enter) return applyCrop();
    if (tool == Tool::Crop && !xf.on && k == Qt::Key_Escape) { crop = ed->docRect(); update(); return; }
    if (xf.on && (k == Qt::Key_Return || k == Qt::Key_Enter)) return commitXf();
    if (xf.on && k == Qt::Key_Escape) return cancelXf();
    if (tool == Tool::Move && !xf.on && !dragging() && (k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_Up || k == Qt::Key_Down)) {
        const int n = e->modifiers() & Qt::ShiftModifier ? 10 : 1;
        return nudge(k == Qt::Key_Left ? -n : k == Qt::Key_Right ? n : 0, k == Qt::Key_Up ? -n : k == Qt::Key_Down ? n : 0);
    }
    QWidget::keyPressEvent(e);
}
void Canvas::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) space = false;
    else QWidget::keyReleaseEvent(e);
}

// ---------- tools ----------
// Photoshop modifiers: Shift adds, Alt subtracts, Shift+Alt intersects; otherwise the mode picked in the options bar
SelMode Canvas::selModeFor(Qt::KeyboardModifiers m) const {
    const bool s = m & Qt::ShiftModifier, a = m & Qt::AltModifier;
    return s && a ? SelMode::Intersect : s ? SelMode::Add : a ? SelMode::Sub : SelMode(opt.selMode);
}
void Canvas::clickDeselect(SelMode mode) { if (mode == SelMode::Replace) ed->selectNone(); }

void Canvas::toolDown(QPointF p, QPointF w, Qt::KeyboardModifiers mods) {
    switch (tool) {
    case Tool::Move: moveDown(p); break;
    case Tool::Rect: shapeDown(p, mods, true); break;
    case Tool::Ellipse: shapeDown(p, mods, false); break;
    case Tool::Lasso: lassoDown(p, mods); break;
    case Tool::PolyLasso: polyDown(p, mods); break;
    case Tool::Crop: cropDown(p, mods); break;
    case Tool::Wand: floodDown(p, mods, true); break;
    case Tool::Brush: strokeDown(p, mods, false); break;
    case Tool::Eraser: strokeDown(p, mods, true); break;
    case Tool::Bucket: floodDown(p, mods, false); break;
    case Tool::Gradient: gradDown(p); break;
    case Tool::Picker:
        pick(p);
        drag.reset(new Drag);
        drag->move = [this](QPointF q, Qt::KeyboardModifiers) { pick(q); };
        break;
    case Tool::Zoom: zoomAt(w, mods & Qt::AltModifier ? 1 / 1.5 : 1.5); break;
    default: break;
    }
}
void Canvas::pick(QPointF p) {
    const QPoint q(int(std::floor(p.x())), int(std::floor(p.y())));
    if (!ed->docRect().contains(q)) return;
    ensureComp();
    const QRgb c = comp.pixel(q);
    if (!qAlpha(c)) return;
    ed->fg = QColor(qRed(c), qGreen(c), qBlue(c));
    if (onColors) onColors();
}

struct Stroke {
    QImage base, buf, tip;
    bool erase = false, hard = true;
    double size = 1, opacity = 1, rem = 0;
    QColor color;
    QPointF last;
    QRect pending;
};
void Canvas::strokeDown(QPointF p, Qt::KeyboardModifiers mods, bool erase) {
    if (!erase && (mods & Qt::AltModifier)) {
        pick(p);
        drag.reset(new Drag);
        drag->move = [this](QPointF q, Qt::KeyboardModifiers) { pick(q); };
        return;
    }
    Layer *l = ed->editable();
    if (!l) return;
    auto s = std::make_shared<Stroke>();
    s->base = l->img;
    s->buf = blankImage(ed->d.w, ed->d.h);
    s->erase = erase;
    s->hard = opt.hardness >= 100;
    s->size = opt.size;
    s->opacity = opt.opacity / 100.0;
    s->color = erase ? QColor(Qt::black) : ed->fg;
    s->last = p;
    if (!s->hard) {
        const int n = int(std::ceil(s->size));
        s->tip = blankImage(n, n);
        QRadialGradient g(n / 2.0, n / 2.0, n / 2.0);
        QColor clear = s->color;
        clear.setAlpha(0);
        g.setColorAt(0, s->color);
        g.setColorAt(opt.hardness / 100.0, s->color);
        g.setColorAt(1, clear);
        QPainter tp(&s->tip);
        tp.fillRect(0, 0, n, n, g);
    }
    auto mark = [s](QPointF a, QPointF b) {
        const double r = s->size / 2 + 2;
        s->pending = s->pending.united(QRectF(QPointF(qMin(a.x(), b.x()) - r, qMin(a.y(), b.y()) - r), QPointF(qMax(a.x(), b.x()) + r, qMax(a.y(), b.y()) + r)).toAlignedRect());
    };
    auto dab = [s, mark](QPainter &b, QPointF c) {
        if (s->hard) { b.setPen(Qt::NoPen); b.setBrush(s->color); b.drawEllipse(c, s->size / 2, s->size / 2); }
        else { b.setOpacity(0.5); b.drawImage(QPointF(c.x() - s->size / 2, c.y() - s->size / 2), s->tip); }
        mark(c, c);
    };
    auto seg = [s, dab, mark](QPointF q) {
        QPainter b(&s->buf);
        b.setRenderHint(QPainter::Antialiasing);
        if (s->hard) {
            b.setPen(QPen(s->color, s->size, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            b.drawLine(s->last, q);
            mark(s->last, q);
        } else {
            const double dx = q.x() - s->last.x(), dy = q.y() - s->last.y(), dist = std::hypot(dx, dy), step = qMax(1.0, s->size * 0.1);
            double t = step - s->rem;
            for (; t <= dist; t += step) dab(b, QPointF(s->last.x() + dx * t / dist, s->last.y() + dy * t / dist));
            s->rem = dist - (t - step);
        }
        s->last = q;
    };
    // layer = base + stroke buffer (clipped to the selection), recomputed only inside the touched rect
    auto paint = [this, s] {
        const QRect r = s->pending & ed->docRect();
        s->pending = QRect();
        Layer *l = ed->active();
        if (r.isEmpty() || !l) return;
        QImage src = s->buf.copy(r);
        if (!ed->d.sel.isNull()) {
            QPainter q(&src);
            q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            q.drawImage(QPoint(0, 0), ed->d.sel, r);
        }
        QPainter lp(&l->img);
        lp.setCompositionMode(QPainter::CompositionMode_Source);
        lp.drawImage(r.topLeft(), s->base, r);
        lp.setOpacity(s->opacity);
        lp.setCompositionMode(s->erase ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_SourceOver);
        lp.drawImage(r.topLeft(), src);
        lp.end();
        damage(r);
    };
    {
        QPainter b(&s->buf);
        b.setRenderHint(QPainter::Antialiasing);
        dab(b, p);
    }
    paint();
    drag.reset(new Drag);
    drag->move = [seg](QPointF q, Qt::KeyboardModifiers) { seg(q); };
    drag->flush = paint;
    drag->up = [this](QPointF) { ed->commit(); };
}
void Canvas::floodDown(QPointF p, Qt::KeyboardModifiers mods, bool wand) {
    Layer *l = wand ? ed->active() : ed->editable();
    const QPoint q(int(std::floor(p.x())), int(std::floor(p.y())));
    if (!l || !ed->docRect().contains(q)) return;
    if (opt.sampleAll) ensureComp();
    Alpha a = floodMask(opt.sampleAll ? comp : l->img, q.x(), q.y(), opt.tolerance, opt.contiguous);
    const int w = ed->d.w, h = ed->d.h;
    if (wand) {
        if (opt.antiAlias) blurAlpha(a, w, h, 1, 1);
        ed->finishSel(alphaToMask(a, w, h), selModeFor(mods), opt.feather);
    } else {
        ed->paintMasked(*l, alphaToMask(a, w, h, ed->fg), opt.opacity / 100.0);
        ed->commit();
        damage(QRect());
    }
}
void Canvas::shapeDown(QPointF o, Qt::KeyboardModifiers mods, bool rect) {
    const SelMode mode = selModeFor(mods);
    Drag *dg = new Drag;
    drag.reset(dg);
    auto geom = [this, o](QPointF q) {
        const int x0 = qBound(0, qRound(qMin(o.x(), q.x())), ed->d.w), x1 = qBound(0, qRound(qMax(o.x(), q.x())), ed->d.w);
        const int y0 = qBound(0, qRound(qMin(o.y(), q.y())), ed->d.h), y1 = qBound(0, qRound(qMax(o.y(), q.y())), ed->d.h);
        return QRect(x0, y0, x1 - x0, y1 - y0);
    };
    auto mk = [rect](QRect r) {
        QPainterPath pa;
        if (rect) pa.addRect(r); else pa.addEllipse(r);
        return pa;
    };
    dg->move = [dg, geom, mk](QPointF q, Qt::KeyboardModifiers) { dg->path = mk(geom(q)); };
    dg->up = [this, geom, mk, mode, rect](QPointF q) {
        const QRect r = geom(q);
        if (r.width() < 1 || r.height() < 1) return clickDeselect(mode);
        QImage m = blankImage(ed->d.w, ed->d.h);
        {
            QPainter p(&m);
            p.setRenderHint(QPainter::Antialiasing, !rect);
            p.fillPath(mk(r), Qt::white);
        }
        ed->finishSel(m, mode, opt.feather);
    };
}
void Canvas::lassoDown(QPointF o, Qt::KeyboardModifiers mods) {
    const SelMode mode = selModeFor(mods);
    auto pts = std::make_shared<QPolygonF>();
    pts->append(o);
    Drag *dg = new Drag;
    drag.reset(dg);
    dg->move = [dg, pts](QPointF q, Qt::KeyboardModifiers) {
        pts->append(q);
        QPainterPath pa;
        pa.addPolygon(*pts);
        pa.closeSubpath();
        dg->path = pa;
    };
    dg->up = [this, pts, mode](QPointF) {
        if (pts->size() < 3) return clickDeselect(mode);
        QImage m = blankImage(ed->d.w, ed->d.h);
        {
            QPainter p(&m);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawPolygon(*pts);
        }
        ed->finishSel(m, mode, opt.feather);
    };
}
void Canvas::moveDown(QPointF o) {
    Layer *l = ed->editable();
    if (!l) return;
    auto L = std::make_shared<Lifted>(ed->lift(*l));
    auto delta = std::make_shared<QPoint>();
    drag.reset(new Drag);
    drag->hideSel = true;
    drag->move = [this, L, delta, o](QPointF q, Qt::KeyboardModifiers) {
        *delta = QPoint(qRound(q.x() - o.x()), qRound(q.y() - o.y()));
        const QRectF s(0, 0, L->flt.width(), L->flt.height());
        ed->drawParts(ed->active()->img, *L, s, s.translated(L->pos + *delta), QPoint(0, 0));
        damage(QRect());
    };
    drag->up = [this, L, delta](QPointF) { if (!delta->isNull()) ed->moveBy(*ed->active(), *L, delta->x(), delta->y()); };
}
void Canvas::nudge(int dx, int dy) {
    Layer *l = ed->editable();
    if (l) ed->moveBy(*l, ed->lift(*l), dx, dy);
}
void Canvas::gradDown(QPointF o) {
    if (!ed->editable()) return;
    Drag *dg = new Drag;
    drag.reset(dg);
    dg->move = [dg, o](QPointF q, Qt::KeyboardModifiers) { dg->line = QLineF(o, q); dg->hasLine = true; };
    dg->up = [this, o](QPointF q) {
        if (QLineF(o, q).length() < 2) return;
        QLinearGradient g(o, q);
        QColor end = ed->bg;
        if (opt.toTransparent) { end = ed->fg; end.setAlpha(0); }
        g.setColorAt(0, ed->fg);
        g.setColorAt(1, end);
        QImage t = blankImage(ed->d.w, ed->d.h);
        { QPainter p(&t); p.fillRect(t.rect(), g); }
        ed->paintMasked(*ed->active(), t, opt.opacity / 100.0);
        ed->commit();
        damage(QRect());
    };
}

// ---------- free transform ----------
void Canvas::startTransform() {
    Layer *l = ed->editable();
    if (!l || xf.on) return;
    const Lifted L = ed->lift(*l);
    const QRect bb = alphaBounds(L.flt);
    if (bb.isNull()) return ed->toast(K("변형할 픽셀이 없습니다"));
    xf.on = true;
    xf.L = L;
    xf.src = bb;
    xf.orig = bb.translated(L.pos);
    xf.dst = xf.orig;
    xf.angle = 0;
    updateCursor();
    if (onMode) onMode();
    update();
}
void Canvas::drawXf() {
    ed->drawParts(ed->active()->img, xf.L, xf.src, xf.dst, QPoint(0, 0), xf.angle);
    damage(QRect());
}
void Canvas::commitXf() {
    if (!xf.on) return;
    xf.on = false;
    if (xf.dst != QRectF(xf.orig) || xf.angle != 0) {
        ed->commitParts(*ed->active(), xf.L, xf.src, xf.dst, xf.angle);
        if (!ed->d.sel.isNull()) {
            QImage m = blankImage(ed->d.w, ed->d.h);
            {
                QPainter p(&m);
                p.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
                p.translate(xf.dst.center());
                p.rotate(xf.angle);
                p.translate(-xf.dst.center());
                p.drawImage(xf.dst, ed->d.sel, QRectF(xf.orig));
            }
            ed->setSelection(m);
        }
        ed->commit();
    }
    xf.L = Lifted();
    xf.angle = 0;
    updateCursor();
    if (onMode) onMode();
    damage(QRect());
}
void Canvas::cancelXf() {
    if (!xf.on) return;
    xf.dst = xf.orig;
    xf.angle = 0;
    drawXf();
    xf.on = false;
    xf.L = Lifted();
    updateCursor();
    if (onMode) onMode();
}
// 0 = outside, 1 = inside, 2 = on one of the 8 handles (hx, hy in -1..1)
static int rectHit(QRectF s, QPointF p, double zoom, int &hx, int &hy) {
    hx = hy = 0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            if (!i && !j) continue;
            const QPointF h(s.x() + s.width() * (i + 1) / 2.0, s.y() + s.height() * (j + 1) / 2.0);
            if (qAbs(h.x() - p.x()) * zoom <= 8 && qAbs(h.y() - p.y()) * zoom <= 8) { hx = i; hy = j; }
        }
    return hx || hy ? 2 : s.contains(p) ? 1 : 0;
}

// ---------- polygonal lasso ----------
void Canvas::polyDown(QPointF p, Qt::KeyboardModifiers mods) {
    if (!polyOn) {
        polyOn = true;
        poly.clear();
        poly << p;
        polyMode = selModeFor(mods);
    } else if (poly.size() >= 3 && QLineF(poly.first(), p).length() * zoom <= 8) finishPoly();
    else poly << p;
}
void Canvas::finishPoly() {
    if (!polyOn) return;
    polyOn = false;
    if (poly.size() >= 3) {
        QImage m = blankImage(ed->d.w, ed->d.h);
        {
            QPainter p(&m);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawPolygon(poly);
        }
        ed->finishSel(m, polyMode, opt.feather);
    }
    poly.clear();
    update();
}
void Canvas::cancelPoly() {
    if (!polyOn) return;
    polyOn = false;
    poly.clear();
    update();
}

// ---------- crop tool ----------
// Handles resize (Shift keeps the aspect ratio), dragging inside moves the box, dragging outside draws a new one.
void Canvas::cropDown(QPointF p, Qt::KeyboardModifiers) {
    int hx, hy;
    const int hit = rectHit(crop, p, zoom, hx, hy);
    const QRectF s = crop;
    drag.reset(new Drag);
    drag->move = [this, s, p, hit, hx, hy](QPointF q, Qt::KeyboardModifiers mods) {
        double x = s.x(), y = s.y(), x2 = s.x() + s.width(), y2 = s.y() + s.height();
        if (hit == 0) { x = qMin(p.x(), q.x()); x2 = qMax(p.x(), q.x()); y = qMin(p.y(), q.y()); y2 = qMax(p.y(), q.y()); }
        else if (hit == 1) { const QPointF d = q - p; x += d.x(); x2 += d.x(); y += d.y(); y2 += d.y(); }
        else {
            const double dx = q.x() - p.x(), dy = q.y() - p.y();
            if (hx < 0) x = qMin(x + dx, x2 - 1);
            if (hx > 0) x2 = qMax(x2 + dx, x + 1);
            if (hy < 0) y = qMin(y + dy, y2 - 1);
            if (hy > 0) y2 = qMax(y2 + dy, y + 1);
            if (hx && hy && (mods & Qt::ShiftModifier)) {
                const double sc = qMax((x2 - x) / s.width(), (y2 - y) / s.height()), nw = s.width() * sc, nh = s.height() * sc;
                if (hx < 0) x = x2 - nw; else x2 = x + nw;
                if (hy < 0) y = y2 - nh; else y2 = y + nh;
            }
        }
        const QRectF limit(-PAD, -PAD, ed->d.w + 2 * PAD, ed->d.h + 2 * PAD);
        QRectF r(qRound(x), qRound(y), qBound(1, qRound(x2 - x), MAX_DIM), qBound(1, qRound(y2 - y), MAX_DIM));
        if (hit == 1) { // moving: keep the size, stay within the limit
            r.moveLeft(qBound(limit.left(), r.left(), limit.right() - r.width()));
            r.moveTop(qBound(limit.top(), r.top(), limit.bottom() - r.height()));
        } else r &= limit;
        if (r.width() >= 1 && r.height() >= 1) crop = r;
    };
    drag->up = [this](QPointF) { updateCursor(); };
}
void Canvas::applyCrop() {
    const QRect r = crop.toRect();
    if (r != ed->docRect()) {
        ed->cropRect(r);
        fit();
    }
    crop = ed->docRect();
    update();
}

int Canvas::xfHit(QPointF p, int &hx, int &hy) const {
    const QRectF s = xf.dst;
    QTransform rot;
    rot.rotate(-xf.angle);
    const QPointF lp = rot.map(p - s.center()) + s.center(); // box-aligned coordinates
    hx = hy = 0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            if (!i && !j) continue;
            const QPointF h(s.x() + s.width() * (i + 1) / 2.0, s.y() + s.height() * (j + 1) / 2.0);
            if (qAbs(h.x() - lp.x()) * zoom <= 8 && qAbs(h.y() - lp.y()) * zoom <= 8) { hx = i; hy = j; }
        }
    return hx || hy ? 2 : s.contains(lp) ? 1 : 0;
}
static QCursor rotateCursor() {
    static const QCursor cursor = [] {
        QPixmap pm(64, 64); // 32x32 at 2x
        pm.setDevicePixelRatio(2);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF circle(8, 8, 16, 16);
        QPainterPath arc;
        arc.arcMoveTo(circle, 60);
        arc.arcTo(circle, 60, 270);
        // arrowhead at the arc's end (330 degrees), pointing along the counter-clockwise tangent
        const QPointF end = arc.currentPosition(), t(0.5, -0.866), n(0.866, 0.5);
        QPolygonF head;
        head << end + t * 5 << end + n * 4 - t << end - n * 4 - t;
        p.setPen(QPen(Qt::white, 4.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(arc);
        p.setBrush(Qt::white);
        p.drawPolygon(head);
        p.setPen(QPen(Qt::black, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(arc);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawPolygon(head);
        return QCursor(pm, 16, 16);
    }();
    return cursor;
}
// Free transform shows what a drag would do: resize arrows on handles (turned with the box),
// move inside, rotate outside. Otherwise the cursor follows the tool.
void Canvas::updateCursor() {
    if (!xf.on && tool == Tool::Crop) {
        int hx, hy;
        const int hit = rectHit(crop, mouseDoc, zoom, hx, hy);
        return setCursor(hit == 0 ? Qt::CrossCursor : hit == 1 ? Qt::SizeAllCursor : !hy ? Qt::SizeHorCursor : !hx ? Qt::SizeVerCursor : hx == hy ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    }
    if (!xf.on) {
        setCursor(tool == Tool::Move ? Qt::SizeAllCursor : tool == Tool::Hand ? Qt::OpenHandCursor : Qt::CrossCursor);
        return;
    }
    int hx, hy;
    const int hit = xfHit(mouseDoc, hx, hy);
    if (hit == 0) return setCursor(rotateCursor());
    if (hit == 1) return setCursor(Qt::SizeAllCursor);
    double deg = std::fmod(std::atan2(double(hy), double(hx)) * 180 / M_PI + xf.angle, 180);
    if (deg < 0) deg += 180;
    setCursor(deg < 22.5 || deg >= 157.5 ? Qt::SizeHorCursor : deg < 67.5 ? Qt::SizeFDiagCursor : deg < 112.5 ? Qt::SizeVerCursor : Qt::SizeBDiagCursor);
}
// Handle: resize. Inside the box: move. Outside: rotate (Shift snaps to 15 degrees).
void Canvas::xfDown(QPointF p) {
    const QRectF s = xf.dst;
    const double a0 = xf.angle;
    const QPointF c0 = s.center();
    QTransform rot;
    rot.rotate(a0);
    const QTransform inv = rot.inverted();
    auto toLocal = [inv, c0](QPointF q) { return inv.map(q - c0) + c0; };
    const QPointF lp = toLocal(p);
    int hx, hy;
    const int hit = xfHit(p, hx, hy);
    const bool handle = hit == 2, inside = hit == 1;
    drag.reset(new Drag);
    drag->move = [=](QPointF q, Qt::KeyboardModifiers mods) {
        if (!handle && !inside) {
            double a = a0 + (std::atan2(q.y() - c0.y(), q.x() - c0.x()) - std::atan2(p.y() - c0.y(), p.x() - c0.x())) * 180 / M_PI;
            if (mods & Qt::ShiftModifier) a = std::round(a / 15) * 15;
            a = std::fmod(a, 360);
            if (a > 180) a -= 360;
            if (a <= -180) a += 360;
            xf.angle = a;
            ed->toast(K("회전 %1도").arg(a, 0, 'f', 1));
            return;
        }
        if (!handle) {
            const QPointF d = q - p;
            xf.dst = a0 == 0 ? QRectF(qRound(s.x() + d.x()), qRound(s.y() + d.y()), s.width(), s.height()) : s.translated(d);
            return;
        }
        const QPointF lq = toLocal(q);
        const double dx = lq.x() - lp.x(), dy = lq.y() - lp.y();
        double x = s.x(), y = s.y(), x2 = s.x() + s.width(), y2 = s.y() + s.height();
        if (hx < 0) x = qMin(x + dx, x2 - 1);
        if (hx > 0) x2 = qMax(x2 + dx, x + 1);
        if (hy < 0) y = qMin(y + dy, y2 - 1);
        if (hy > 0) y2 = qMax(y2 + dy, y + 1);
        if (hx && hy && !(mods & Qt::ShiftModifier)) { // corners keep the aspect ratio
            const double sc = qMax((x2 - x) / s.width(), (y2 - y) / s.height()), nw = s.width() * sc, nh = s.height() * sc;
            if (hx < 0) x = x2 - nw; else x2 = x + nw;
            if (hy < 0) y = y2 - nh; else y2 = y + nh;
        }
        if (mods & Qt::AltModifier) { // Alt: scale around the center
            if (hx) { const double half = hx < 0 ? c0.x() - x : x2 - c0.x(); x = c0.x() - qMax(0.5, half); x2 = c0.x() + qMax(0.5, half); }
            if (hy) { const double half = hy < 0 ? c0.y() - y : y2 - c0.y(); y = c0.y() - qMax(0.5, half); y2 = c0.y() + qMax(0.5, half); }
        }
        const double w = qMax(1.0, std::round(x2 - x)), h = qMax(1.0, std::round(y2 - y));
        if (a0 == 0) { xf.dst = QRectF(qRound(x), qRound(y), w, h); return; }
        // the box was resized in its own rotated frame: put its new center back into doc coords
        const QPointF nc = c0 + rot.map(QPointF((x + x2) / 2, (y + y2) / 2) - c0);
        xf.dst = QRectF(nc.x() - w / 2, nc.y() - h / 2, w, h);
    };
    drag->flush = [this] { drawXf(); };
    drag->up = [this](QPointF) { updateCursor(); };
}
