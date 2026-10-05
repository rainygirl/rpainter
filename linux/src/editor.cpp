#include "editor.h"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <vector>

static const QImage::Format FMT = QImage::Format_ARGB32_Premultiplied;

const QVector<BlendInfo> &blendModes() {
    static const QVector<BlendInfo> v = {
        {QPainter::CompositionMode_SourceOver, "source-over", "표준"},
        {QPainter::CompositionMode_Multiply, "multiply", "곱하기"},
        {QPainter::CompositionMode_Screen, "screen", "스크린"},
        {QPainter::CompositionMode_Overlay, "overlay", "오버레이"},
        {QPainter::CompositionMode_Darken, "darken", "어둡게"},
        {QPainter::CompositionMode_Lighten, "lighten", "밝게"},
        {QPainter::CompositionMode_ColorDodge, "color-dodge", "색상 닷지"},
        {QPainter::CompositionMode_ColorBurn, "color-burn", "색상 번"},
        {QPainter::CompositionMode_HardLight, "hard-light", "하드 라이트"},
        {QPainter::CompositionMode_SoftLight, "soft-light", "소프트 라이트"},
        {QPainter::CompositionMode_Difference, "difference", "차이"},
        {QPainter::CompositionMode_Exclusion, "exclusion", "제외"},
    };
    return v;
}

// ---------- pixel helpers ----------
QImage blankImage(int w, int h) {
    QImage im(qMax(1, w), qMax(1, h), FMT);
    im.fill(Qt::transparent);
    return im;
}
QImage toArgb(const QImage &im) { return im.format() == FMT ? im : im.convertToFormat(FMT); }

QRect alphaBounds(const QImage &im) {
    int x0 = im.width(), y0 = im.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < im.height(); y++) {
        const QRgb *p = reinterpret_cast<const QRgb *>(im.constScanLine(y));
        for (int x = 0; x < im.width(); x++) {
            if (qAlpha(p[x])) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    return x1 < 0 ? QRect() : QRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}
Alpha maskAlpha(const QImage &m) {
    const int w = m.width(), h = m.height();
    Alpha a(w * h);
    for (int y = 0; y < h; y++) {
        const QRgb *p = reinterpret_cast<const QRgb *>(m.constScanLine(y));
        for (int x = 0; x < w; x++) a[y * w + x] = uchar(qAlpha(p[x]));
    }
    return a;
}
QImage alphaToMask(const Alpha &a, int w, int h, QColor c) {
    QImage m(w, h, FMT);
    const int r = c.red(), g = c.green(), b = c.blue();
    for (int y = 0; y < h; y++) {
        QRgb *p = reinterpret_cast<QRgb *>(m.scanLine(y));
        for (int x = 0; x < w; x++) p[x] = qPremultiply(qRgba(r, g, b, a[y * w + x]));
    }
    return m;
}
// 1D box pass with replicated edges; fn(sum, n) maps the window sum to the output value
template <class F>
static void boxPass(const uchar *s, uchar *d, int w, int h, int r, bool horiz, F fn) {
    const int n = 2 * r + 1, len = horiz ? w : h, lines = horiz ? h : w, step = horiz ? 1 : w;
    for (int l = 0; l < lines; l++) {
        const int base = horiz ? l * w : l;
        int sum = 0;
        for (int i = -r; i <= r; i++) sum += s[base + qBound(0, i, len - 1) * step];
        for (int i = 0; i < len; i++) {
            d[base + i * step] = fn(sum, n);
            sum += s[base + qMin(len - 1, i + r + 1) * step] - s[base + qMax(0, i - r) * step];
        }
    }
}
void blurAlpha(Alpha &a, int w, int h, int r, int passes) {
    if (r <= 0) return;
    Alpha t(a.size());
    auto avg = [](int s, int n) { return uchar((s + n / 2) / n); };
    for (int i = 0; i < passes; i++) {
        boxPass(a.constData(), t.data(), w, h, r, true, avg);
        boxPass(t.constData(), a.data(), w, h, r, false, avg);
    }
}
void morphAlpha(Alpha &a, int w, int h, int r, bool expand) {
    for (auto &v : a) v = v > 127 ? 255 : 0;
    Alpha t(a.size());
    auto fn = [expand](int s, int n) { return uchar(expand ? (s > 0 ? 255 : 0) : (s == 255 * n ? 255 : 0)); };
    boxPass(a.constData(), t.data(), w, h, r, true, fn);
    boxPass(t.constData(), a.data(), w, h, r, false, fn);
}
Alpha floodMask(const QImage &img, int x, int y, int tol, bool contiguous) {
    const int w = img.width(), h = img.height();
    Alpha out(w * h, 0);
    std::vector<QRgb> px(size_t(w) * h);
    for (int yy = 0; yy < h; yy++) memcpy(&px[size_t(yy) * w], img.constScanLine(yy), size_t(w) * 4);
    const QRgb c0 = px[size_t(y) * w + x];
    const int r0 = qRed(c0), g0 = qGreen(c0), b0 = qBlue(c0), a0 = qAlpha(c0);
    auto match = [&](int p) {
        const QRgb c = px[p];
        return qAbs(qRed(c) - r0) <= tol && qAbs(qGreen(c) - g0) <= tol && qAbs(qBlue(c) - b0) <= tol && qAbs(qAlpha(c) - a0) <= tol;
    };
    if (!contiguous) {
        for (int p = 0; p < w * h; p++) if (match(p)) out[p] = 255;
        return out;
    }
    std::vector<int> stack{y * w + x};
    while (!stack.empty()) {
        const int p = stack.back();
        stack.pop_back();
        if (out[p] || !match(p)) continue;
        const int pxx = p % w, row = p - pxx;
        int l = pxx, r = pxx;
        while (l > 0 && !out[row + l - 1] && match(row + l - 1)) l--;
        while (r < w - 1 && !out[row + r + 1] && match(row + r + 1)) r++;
        for (int i = l; i <= r; i++) out[row + i] = 255;
        for (int nrow : {row - w, row + w}) {
            if (nrow < 0 || nrow >= w * h) continue;
            bool run = false;
            for (int i = l; i <= r; i++) {
                const int q = nrow + i;
                if (!out[q] && match(q)) { if (!run) { stack.push_back(q); run = true; } }
                else run = false;
            }
        }
    }
    return out;
}
static QVector<QLine> buildEdges(const Alpha &a, int w, int h) {
    QVector<QLine> e;
    auto on = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && a[y * w + x] > 127; };
    for (int y = 0; y <= h; y++) {
        int s = -1;
        for (int x = 0; x <= w; x++) {
            if (x < w && on(x, y - 1) != on(x, y)) { if (s < 0) s = x; }
            else if (s >= 0) { e.append(QLine(s, y, x, y)); s = -1; }
        }
    }
    for (int x = 0; x <= w; x++) {
        int s = -1;
        for (int y = 0; y <= h; y++) {
            if (y < h && on(x - 1, y) != on(x, y)) { if (s < 0) s = y; }
            else if (s >= 0) { e.append(QLine(x, s, x, y)); s = -1; }
        }
    }
    return e;
}
static QImage rot90(const QImage &s, bool cw) {
    const int sw = s.width(), sh = s.height();
    QImage o(sh, sw, FMT);
    for (int y = 0; y < sw; y++) {
        QRgb *p = reinterpret_cast<QRgb *>(o.scanLine(y));
        for (int x = 0; x < sh; x++) p[x] = cw ? s.pixel(y, sh - 1 - x) : s.pixel(sw - 1 - y, x);
    }
    return o;
}

// ---------- color ----------
ColorMat colorMatrix(double hue, double sat, double bri, double con) {
    const double cs = std::cos(hue * M_PI / 180), sn = std::sin(hue * M_PI / 180), s = 1 + sat / 100;
    const double H[9] = {
        0.213 + cs * 0.787 - sn * 0.213, 0.715 - cs * 0.715 - sn * 0.715, 0.072 - cs * 0.072 + sn * 0.928,
        0.213 - cs * 0.213 + sn * 0.143, 0.715 + cs * 0.285 + sn * 0.140, 0.072 - cs * 0.072 - sn * 0.283,
        0.213 - cs * 0.213 - sn * 0.787, 0.715 - cs * 0.715 + sn * 0.715, 0.072 + cs * 0.928 + sn * 0.072,
    };
    const double S[9] = {
        0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s,
        0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s,
        0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s,
    };
    const double C = con * 2.55, k = (259 * (C + 255)) / (255 * (259 - C));
    ColorMat cm;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) cm.m[r * 3 + c] = k * (S[r * 3] * H[c] + S[r * 3 + 1] * H[3 + c] + S[r * 3 + 2] * H[6 + c]);
    cm.off = 128 * (1 - k) + bri * 1.275;
    return cm;
}
ColorMat invertMatrix() { return ColorMat{{-1, 0, 0, 0, -1, 0, 0, 0, -1}, 255}; }

void applyColor(QImage &dst, const QImage &src, const ColorMat &cm, const QImage *sel) {
    auto cl = [](double v) { return int(qBound(0.0, v + 0.5, 255.0)); };
    const double *m = cm.m;
    for (int y = 0; y < src.height(); y++) {
        const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        const QRgb *k = sel ? reinterpret_cast<const QRgb *>(sel->constScanLine(y)) : nullptr;
        QRgb *o = reinterpret_cast<QRgb *>(dst.scanLine(y));
        for (int x = 0; x < src.width(); x++) {
            const int a = qAlpha(s[x]);
            if (!a) { o[x] = 0; continue; }
            const QRgb u = qUnpremultiply(s[x]);
            const double r = qRed(u), g = qGreen(u), b = qBlue(u);
            double nr = m[0] * r + m[1] * g + m[2] * b + cm.off, ng = m[3] * r + m[4] * g + m[5] * b + cm.off, nb = m[6] * r + m[7] * g + m[8] * b + cm.off;
            if (k) {
                const double t = qAlpha(k[x]) / 255.0;
                nr = r + (qBound(0.0, nr, 255.0) - r) * t; ng = g + (qBound(0.0, ng, 255.0) - g) * t; nb = b + (qBound(0.0, nb, 255.0) - b) * t;
            }
            o[x] = qPremultiply(qRgba(cl(nr), cl(ng), cl(nb), a));
        }
    }
}

// ---------- layers / history ----------
Layer *Editor::active() {
    for (auto &l : d.layers) if (l.id == d.activeId) return &l;
    return nullptr;
}
int Editor::activeIdx() const {
    for (int i = 0; i < d.layers.size(); i++) if (d.layers[i].id == d.activeId) return i;
    return -1;
}
Layer *Editor::editable() {
    Layer *l = active();
    if (l && !l->visible) { toast(K("숨겨진 레이어는 편집할 수 없습니다")); return nullptr; }
    return l;
}
Layer Editor::makeLayer(const QString &n, const QImage &img) {
    Layer l;
    l.id = ++idSeq;
    l.name = n;
    l.img = img.isNull() ? blankImage(d.w, d.h) : img;
    return l;
}
void Editor::addLayer(const Layer &l) {
    d.layers.insert(activeIdx() + 1, l);
    d.activeId = l.id;
}
void Editor::resetDoc(int w, int h, const QVector<Layer> &layers) {
    d = DocState();
    d.w = w; d.h = h;
    d.layers = layers;
    d.activeId = layers.last().id;
    hist.clear();
    histIdx = -1;
    commit();
    modified = false;
    damage();
}
void Editor::newDoc(int w, int h, QColor fillColor) {
    d.w = w; d.h = h;
    Layer l = makeLayer(K("배경"));
    if (fillColor.alpha()) l.img.fill(fillColor);
    resetDoc(w, h, {l});
}
static qint64 histBytes(const QVector<DocState> &hist) {
    QSet<qint64> seen;
    qint64 n = 0;
    auto add = [&](const QImage &im) {
        if (im.isNull() || seen.contains(im.cacheKey())) return;
        seen.insert(im.cacheKey());
        n += qint64(im.width()) * im.height() * 4;
    };
    for (const auto &s : hist) for (const auto &l : s.layers) { add(l.img); add(l.ext); }
    return n;
}
void Editor::commit() {
    hist.resize(histIdx + 1);
    hist.append(d);
    while (hist.size() > 2 && (hist.size() > 50 || histBytes(hist) > 1500000000LL)) hist.removeFirst();
    histIdx = hist.size() - 1;
    modified = true;
    if (onChange) onChange();
}
void Editor::undo() {
    if (histIdx <= 0) return;
    d = hist[--histIdx];
    if (onChange) onChange();
    damage();
}
void Editor::redo() {
    if (histIdx >= hist.size() - 1) return;
    d = hist[++histIdx];
    if (onChange) onChange();
    damage();
}

// ---------- selection ----------
void Editor::setSelection(QImage mask) {
    d.selEdges.clear();
    if (!mask.isNull()) {
        const Alpha a = maskAlpha(mask);
        bool any = false;
        for (uchar v : a) if (v > 127) { any = true; break; }
        if (any) d.selEdges = buildEdges(a, mask.width(), mask.height());
        else mask = QImage();
    }
    d.sel = mask;
    if (onView) onView();
}
void Editor::combineSel(const QImage &shape, SelMode mode) {
    if (mode == SelMode::Replace || d.sel.isNull()) {
        if (mode == SelMode::Replace || mode == SelMode::Add) setSelection(shape);
        return;
    }
    QImage m = d.sel;
    {
        QPainter p(&m);
        p.setCompositionMode(mode == SelMode::Add ? QPainter::CompositionMode_SourceOver
                             : mode == SelMode::Sub ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, shape);
    }
    setSelection(m);
}
void Editor::finishSel(QImage mask, SelMode mode, int feather) {
    if (feather > 0) {
        Alpha a = maskAlpha(mask);
        blurAlpha(a, d.w, d.h, feather);
        mask = alphaToMask(a, d.w, d.h);
    }
    combineSel(mask, mode);
    commit();
}
QImage Editor::fullMask() const {
    QImage m(d.w, d.h, FMT);
    m.fill(Qt::white);
    return m;
}
void Editor::selectAll() { setSelection(fullMask()); commit(); }
void Editor::selectNone() { if (!d.sel.isNull()) { lastSel = d.sel; setSelection(QImage()); commit(); } }
void Editor::reselect() {
    if (lastSel.isNull() || lastSel.size() != QSize(d.w, d.h)) return toast(K("다시 선택할 영역이 없습니다"));
    setSelection(lastSel);
    commit();
}
void Editor::invertSel() {
    if (d.sel.isNull()) return toast(K("선택 영역이 없습니다"));
    QImage m = fullMask();
    {
        QPainter p(&m);
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(0, 0, d.sel);
    }
    setSelection(m);
    commit();
}
void Editor::selectLayerPixels() {
    Layer *l = active();
    if (!l) return;
    setSelection(alphaToMask(maskAlpha(l->img), d.w, d.h));
    commit();
}
void Editor::modifySel(const std::function<void(Alpha &)> &fn) {
    if (d.sel.isNull()) return toast(K("선택 영역이 없습니다"));
    Alpha a = maskAlpha(d.sel);
    fn(a);
    setSelection(alphaToMask(a, d.w, d.h));
    commit();
}

// ---------- compositing ----------
void Editor::composite(QImage &out, QRect clip) const {
    QPainter p(&out);
    p.setClipRect(clip);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(clip, Qt::transparent);
    for (const auto &l : d.layers) {
        if (!l.visible) continue;
        p.setOpacity(l.opacity);
        p.setCompositionMode(l.blend);
        p.drawImage(0, 0, l.img);
    }
}
QImage Editor::flatten() const {
    QImage c = blankImage(d.w, d.h);
    composite(c, docRect());
    return c;
}
// draws canvas-sized temp image `src` onto the layer, clipped to the selection
void Editor::paintMasked(Layer &l, QImage src, double alpha) {
    if (!d.sel.isNull()) {
        QPainter q(&src);
        q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        q.drawImage(0, 0, d.sel);
    }
    QPainter p(&l.img);
    p.setOpacity(alpha);
    p.drawImage(0, 0, src);
}

// ---------- off-canvas aware layer access ----------
Full Editor::fullOf(const Layer &l) const {
    if (l.ext.isNull()) return {l.img, QPoint(0, 0)};
    const QRect u = docRect().united(QRect(l.extPos, l.ext.size()));
    QImage c = blankImage(u.width(), u.height());
    QPainter p(&c);
    p.drawImage(l.extPos - u.topLeft(), l.ext);
    p.drawImage(-u.topLeft(), l.img);
    return {c, u.topLeft()};
}
void Editor::setFull(Layer &l, const Full &f) {
    l.img = blankImage(d.w, d.h);
    l.ext = QImage();
    l.extPos = QPoint();
    if (f.img.isNull()) return;
    {
        QPainter p(&l.img);
        p.drawImage(f.pos, f.img);
    }
    if (docRect().contains(QRect(f.pos, f.img.size()))) return;
    QImage e = f.img;
    {
        QPainter p(&e);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(docRect().translated(-f.pos), Qt::transparent);
    }
    const QRect bb = alphaBounds(e);
    if (bb.isNull()) return;
    l.ext = e.copy(bb);
    l.extPos = f.pos + bb.topLeft();
}
Lifted Editor::lift(const Layer &l) const {
    const Full F = fullOf(l);
    Lifted L;
    L.pos = F.pos;
    L.flt = F.img;
    L.base = blankImage(F.img.width(), F.img.height());
    if (!d.sel.isNull()) {
        // Qt composites only where the source is drawn, so the mask must cover the whole image
        QImage m = blankImage(F.img.width(), F.img.height());
        { QPainter p(&m); p.drawImage(-F.pos, d.sel); }
        { QPainter p(&L.flt); p.setCompositionMode(QPainter::CompositionMode_DestinationIn); p.drawImage(0, 0, m); }
        L.base = F.img;
        { QPainter p(&L.base); p.setCompositionMode(QPainter::CompositionMode_DestinationOut); p.drawImage(0, 0, m); }
    }
    return L;
}
// base plus float rect s (float coords) placed at dst (doc coords), rotated by angle degrees around
// dst's center; origin = doc coords of target's top-left
void Editor::drawParts(QImage &target, const Lifted &L, QRectF s, QRectF dst, QPoint origin, double angle) const {
    target.fill(Qt::transparent);
    QPainter p(&target);
    p.drawImage(L.pos - origin, L.base);
    const QRectF r = dst.translated(-origin);
    p.setRenderHint(QPainter::SmoothPixmapTransform, angle != 0 || s.size() != r.size());
    if (angle != 0) {
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(r.center());
        p.rotate(angle);
        p.translate(-r.center());
    }
    p.drawImage(r, L.flt, s);
}
void Editor::commitParts(Layer &l, const Lifted &L, QRect s, QRectF dst, double angle) {
    QTransform t;
    t.translate(dst.center().x(), dst.center().y());
    t.rotate(angle);
    t.translate(-dst.center().x(), -dst.center().y());
    const QRect covered = t.mapRect(dst).toAlignedRect().adjusted(-1, -1, 1, 1);
    const QRect u = QRect(L.pos, L.base.size()).united(covered) & QRect(-PAD, -PAD, d.w + 2 * PAD, d.h + 2 * PAD);
    QImage c = blankImage(u.width(), u.height());
    drawParts(c, L, s, dst, u.topLeft(), angle);
    setFull(l, {c, u.topLeft()});
}
void Editor::moveBy(Layer &l, const Lifted &L, int dx, int dy) {
    const QRect s(0, 0, L.flt.width(), L.flt.height());
    commitParts(l, L, s, s.translated(L.pos + QPoint(dx, dy)));
    if (!d.sel.isNull()) {
        QImage m = blankImage(d.w, d.h);
        { QPainter p(&m); p.drawImage(dx, dy, d.sel); }
        setSelection(m);
    }
    commit();
    damage();
}
static Full clampFull(const Full &f, int nw, int nh) {
    if (f.img.isNull()) return f;
    const QRect full(f.pos, f.img.size()), r = full & QRect(-PAD, -PAD, nw + 2 * PAD, nh + 2 * PAD);
    if (r == full) return f;
    if (r.isEmpty()) return {QImage(), QPoint()};
    return {f.img.copy(r.translated(-f.pos)), r.topLeft()};
}
// f maps a full layer from old doc coords to new doc coords
void Editor::mapAll(int nw, int nh, const std::function<Full(const Full &)> &f) {
    QVector<Full> fulls;
    for (const auto &l : d.layers) fulls.append(clampFull(f(fullOf(l)), nw, nh));
    QImage sel;
    if (!d.sel.isNull()) {
        const Full s = f({d.sel, QPoint(0, 0)});
        sel = blankImage(nw, nh);
        QPainter p(&sel);
        p.drawImage(s.pos, s.img);
    }
    d.w = nw; d.h = nh;
    for (int i = 0; i < d.layers.size(); i++) setFull(d.layers[i], fulls[i]);
    setSelection(sel);
    commit();
    damage();
}
Full Editor::xfFull(const Full &f, DocXf x) const {
    const int w = d.w, h = d.h, px = f.pos.x(), py = f.pos.y(), fw = f.img.width(), fh = f.img.height();
    switch (x) {
    case DocXf::RotCW: return {rot90(f.img, true), QPoint(h - py - fh, px)};
    case DocXf::RotCCW: return {rot90(f.img, false), QPoint(py, w - px - fw)};
    case DocXf::Rot180: return {f.img.mirrored(true, true), QPoint(w - px - fw, h - py - fh)};
    case DocXf::FlipH: return {f.img.mirrored(true, false), QPoint(w - px - fw, py)};
    default: return {f.img.mirrored(false, true), QPoint(px, h - py - fh)};
    }
}

// ---------- document operations ----------
void Editor::deleteSel() {
    Layer *l = editable();
    if (!l) return;
    if (!d.sel.isNull()) {
        QPainter p(&l->img);
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(0, 0, d.sel);
    } else {
        l->img = blankImage(d.w, d.h);
        l->ext = QImage();
    }
    commit();
    damage();
}
void Editor::fill(QColor c) {
    Layer *l = editable();
    if (!l) return;
    QImage t(d.w, d.h, FMT);
    t.fill(c);
    paintMasked(*l, t);
    commit();
    damage();
}
void Editor::colorExt(Layer &l, const ColorMat &cm) {
    if (l.ext.isNull() || !d.sel.isNull()) return;
    const QImage src = l.ext;
    applyColor(l.ext, src, cm, nullptr);
}
void Editor::quickColor(const ColorMat &cm) {
    Layer *l = editable();
    if (!l) return;
    const QImage src = l->img;
    applyColor(l->img, src, cm, d.sel.isNull() ? nullptr : &d.sel);
    colorExt(*l, cm);
    commit();
    damage();
}
static QImage selectedPixels(const Layer &l, const QImage &sel) {
    QImage c = l.img;
    if (!sel.isNull()) {
        QPainter p(&c);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, sel);
    }
    return c;
}
bool Editor::copySel(bool cut, bool merged) {
    Layer *l = editable();
    if (!l) return false;
    Layer all = *l;
    if (merged) all.img = flatten();
    const QImage c = selectedPixels(merged ? all : *l, d.sel);
    const QRect bb = alphaBounds(c);
    if (bb.isNull()) { toast(K("복사할 픽셀이 없습니다")); return false; }
    clip = c.copy(bb);
    clipPos = bb.topLeft();
    if (cut) deleteSel();
    return true;
}
void Editor::pasteImage(const QImage &im, QPoint pos) {
    Layer n = makeLayer(K("붙여넣기"));
    { QPainter p(&n.img); p.drawImage(pos, im); }
    addLayer(n);
    commit();
    damage();
}
void Editor::placeImage(const QImage &src, const QString &layerName) {
    QImage im = toArgb(src);
    if (im.width() > d.w || im.height() > d.h) im = im.scaled(d.w, d.h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    Layer n = makeLayer(layerName);
    { QPainter p(&n.img); p.drawImage((d.w - im.width()) / 2, (d.h - im.height()) / 2, im); }
    addLayer(n);
    commit();
    damage();
}
void Editor::newLayer() {
    addLayer(makeLayer(K("레이어 %1").arg(d.layers.size() + 1)));
    commit();
}
void Editor::layerViaCopy(bool cut) {
    Layer *l = editable();
    if (!l) return;
    Layer n = makeLayer(l->name + " " + K("복사본"), selectedPixels(*l, d.sel));
    if (d.sel.isNull()) { n.opacity = l->opacity; n.blend = l->blend; n.ext = l->ext; n.extPos = l->extPos; }
    else if (cut) {
        QPainter p(&l->img);
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(0, 0, d.sel);
    }
    addLayer(n);
    commit();
    damage();
}
void Editor::duplicateLayer() {
    Layer *l = active();
    if (!l) return;
    Layer n = *l;
    n.id = ++idSeq;
    n.name += " " + K("복사본");
    addLayer(n);
    commit();
    damage();
}
void Editor::deleteLayer() {
    if (d.layers.size() < 2) return toast(K("마지막 레이어는 삭제할 수 없습니다"));
    const int i = activeIdx();
    d.layers.removeAt(i);
    d.activeId = d.layers[qMax(0, i - 1)].id;
    commit();
    damage();
}
void Editor::moveLayer(int dir) {
    const int i = activeIdx(), j = i + dir;
    if (i < 0 || j < 0 || j >= d.layers.size()) return;
    d.layers.move(i, j);
    commit();
    damage();
}
void Editor::mergeDown() {
    const int i = activeIdx();
    if (i <= 0) return toast(K("아래에 레이어가 없습니다"));
    const Layer up = d.layers[i];
    const Full A = fullOf(d.layers[i - 1]), B = fullOf(up);
    const QRect u = QRect(A.pos, A.img.size()).united(QRect(B.pos, B.img.size()));
    QImage c = blankImage(u.width(), u.height());
    {
        QPainter p(&c);
        p.drawImage(A.pos - u.topLeft(), A.img);
        // limit blending to the upper layer's rect: Qt blend modes only touch drawn pixels anyway
        p.setOpacity(up.opacity);
        p.setCompositionMode(up.blend);
        p.drawImage(B.pos - u.topLeft(), B.img);
    }
    setFull(d.layers[i - 1], {c, u.topLeft()});
    d.activeId = d.layers[i - 1].id;
    d.layers.removeAt(i);
    commit();
    damage();
}
void Editor::flattenAll() {
    const Layer l = makeLayer(K("배경"), flatten());
    d.layers = {l};
    d.activeId = l.id;
    commit();
    damage();
}
void Editor::layerFlip(DocXf x) {
    Layer *l = editable();
    if (!l) return;
    setFull(*l, clampFull(xfFull(fullOf(*l), x), d.w, d.h));
    commit();
    damage();
}
void Editor::transformDoc(DocXf x) {
    const bool swap = x == DocXf::RotCW || x == DocXf::RotCCW;
    mapAll(swap ? d.h : d.w, swap ? d.w : d.h, [this, x](const Full &f) { return xfFull(f, x); });
}
void Editor::resizeImage(int nw, int nh) {
    if (nw == d.w && nh == d.h) return;
    const double sx = double(nw) / d.w, sy = double(nh) / d.h;
    mapAll(nw, nh, [=](const Full &f0) {
        // pre-crop to what can survive the PAD clamp so upscaling never allocates a huge image
        const QRect keep(int(std::floor(-PAD / sx)), int(std::floor(-PAD / sy)), int(std::ceil((nw + 2 * PAD) / sx)), int(std::ceil((nh + 2 * PAD) / sy)));
        const QRect src = QRect(f0.pos, f0.img.size()) & keep;
        if (src.isEmpty()) return Full{QImage(), QPoint()};
        const QImage part = f0.img.copy(src.translated(-f0.pos));
        const int x0 = int(std::floor(src.x() * sx + 1e-6)), y0 = int(std::floor(src.y() * sy + 1e-6));
        const int x1 = int(std::ceil((src.x() + src.width()) * sx - 1e-6)), y1 = int(std::ceil((src.y() + src.height()) * sy - 1e-6));
        return Full{part.scaled(qMax(1, x1 - x0), qMax(1, y1 - y0), Qt::IgnoreAspectRatio, Qt::SmoothTransformation), QPoint(x0, y0)};
    });
}
void Editor::resizeCanvas(int nw, int nh, int anchor) {
    if (nw == d.w && nh == d.h) return;
    const QPoint o(qRound((nw - d.w) * (anchor % 3) / 2.0), qRound((nh - d.h) * (anchor / 3) / 2.0));
    mapAll(nw, nh, [o](const Full &f) { return Full{f.img, f.pos + o}; });
}
void Editor::cropToSel() {
    const QRect bb = d.sel.isNull() ? QRect() : alphaBounds(d.sel);
    if (bb.isNull()) return toast(K("선택 영역이 없습니다"));
    cropRect(bb);
}
void Editor::cropRect(QRect r) {
    if (r.width() < 1 || r.height() < 1 || r == docRect()) return;
    mapAll(r.width(), r.height(), [r](const Full &f) { return Full{f.img, f.pos - r.topLeft()}; });
}

// ---------- files ----------
bool Editor::openFile(const QString &path, bool asLayer) {
    const QString base = QFileInfo(path).completeBaseName();
    if (path.endsWith(".rpaint", Qt::CaseInsensitive)) {
        if (!loadProject(path)) return false;
        name = base;
        return true;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage im = toArgb(reader.read());
    if (im.isNull()) return false;
    if (asLayer) { placeImage(im, base); return true; }
    if (im.width() > MAX_DIM || im.height() > MAX_DIM) { toast(K("이미지가 너무 큽니다 (최대 %1px)").arg(MAX_DIM)); return false; }
    d.w = im.width(); d.h = im.height();
    resetDoc(d.w, d.h, {makeLayer(K("배경"), im)});
    name = base;
    return true;
}
bool Editor::exportImage(const QString &path, const QByteArray &fmt, int quality) {
    QImage out = flatten();
    if (fmt == "jpg") {
        QImage o(d.w, d.h, QImage::Format_RGB32);
        o.fill(Qt::white);
        { QPainter p(&o); p.drawImage(0, 0, out); }
        out = o;
    } else out = out.convertToFormat(QImage::Format_ARGB32);
    QImageWriter writer(path, fmt);
    if (fmt != "png") writer.setQuality(quality);
    return writer.write(out);
}
// Same JSON format as the web version, so .rpaint files are interchangeable.
bool Editor::saveProject(const QString &path) {
    QJsonArray layers;
    for (const auto &l : d.layers) {
        const Full F = fullOf(l);
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        F.img.save(&buf, "PNG");
        QString css = "source-over";
        for (const auto &b : blendModes()) if (b.mode == l.blend) css = b.css;
        layers.append(QJsonObject{{"name", l.name}, {"opacity", l.opacity}, {"visible", l.visible}, {"blend", css},
                                  {"x", F.pos.x()}, {"y", F.pos.y()}, {"data", QString("data:image/png;base64,") + png.toBase64()}});
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{"app", "r-painter"}, {"w", d.w}, {"h", d.h}, {"layers", layers}}).toJson(QJsonDocument::Compact));
    modified = false;
    return true;
}
bool Editor::loadProject(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const int w = o["w"].toInt(), h = o["h"].toInt();
    const QJsonArray arr = o["layers"].toArray();
    if (w < 1 || h < 1 || w > MAX_DIM || h > MAX_DIM || arr.isEmpty()) return false;
    QVector<QImage> imgs;
    for (const auto &v : arr) {
        const QString data = v.toObject()["data"].toString();
        const QImage im = toArgb(QImage::fromData(QByteArray::fromBase64(data.mid(data.indexOf(',') + 1).toLatin1())));
        if (im.isNull()) return false;
        imgs.append(im);
    }
    d.w = w; d.h = h;
    QVector<Layer> layers;
    for (int i = 0; i < arr.size(); i++) {
        const QJsonObject lo = arr[i].toObject();
        Layer l = makeLayer(lo["name"].toString());
        setFull(l, {imgs[i], QPoint(lo["x"].toInt(), lo["y"].toInt())});
        l.opacity = lo["opacity"].toDouble(1);
        l.visible = lo["visible"].toBool(true);
        for (const auto &b : blendModes()) if (lo["blend"].toString() == b.css) l.blend = b.mode;
        layers.append(l);
    }
    resetDoc(w, h, layers);
    return true;
}
