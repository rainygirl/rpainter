#include "doc.h"
#include "i18n.h"
#include <cstdio>
#include <cstdlib>
#include <set>

// ---------- layers / history ----------
Layer *Editor::active() {
    for (auto &l : d.layers) if (l.id == d.activeId) return &l;
    return nullptr;
}
int Editor::activeIdx() const {
    for (size_t i = 0; i < d.layers.size(); i++) if (d.layers[i].id == d.activeId) return int(i);
    return -1;
}
Layer *Editor::editable() {
    Layer *l = active();
    if (l && !l->visible) { toast(TR("숨겨진 레이어는 편집할 수 없습니다")); return nullptr; }
    return l;
}
Layer Editor::makeLayer(const std::string &n, const Image &img) {
    Layer l;
    l.id = ++idSeq;
    l.name = n;
    l.img = img.null() ? Image(d.w, d.h) : img;
    return l;
}
void Editor::addLayer(const Layer &l) {
    d.layers.insert(d.layers.begin() + (activeIdx() + 1), l);
    d.activeId = l.id;
}
void Editor::resetDoc(int w, int h, const std::vector<Layer> &layers) {
    d = DocState();
    d.w = w; d.h = h;
    d.layers = layers;
    d.activeId = layers.back().id;
    hist.clear();
    histIdx = -1;
    commit();
    modified = false;
    damage();
}
void Editor::newDoc(int w, int h, bool fillIt, Px rgb) {
    d.w = w; d.h = h;
    Layer l = makeLayer(TR("배경"));
    if (fillIt) l.img.fill(0xff000000u | rgb);
    resetDoc(w, h, {l});
}
static size_t histBytes(const std::vector<DocState> &hist) {
    std::set<const void *> seen;
    size_t n = 0;
    auto add = [&](const Image &im) { if (!im.null() && seen.insert(im.key()).second) n += im.bytes(); };
    for (const auto &s : hist) for (const auto &l : s.layers) { add(l.img); add(l.ext); }
    return n;
}
void Editor::commit() {
    hist.resize(histIdx + 1);
    hist.push_back(d);
    while (hist.size() > 2 && (hist.size() > 50 || histBytes(hist) > 1500000000u)) hist.erase(hist.begin());
    histIdx = int(hist.size()) - 1;
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
    if (histIdx >= int(hist.size()) - 1) return;
    d = hist[++histIdx];
    if (onChange) onChange();
    damage();
}

// ---------- selection ----------
static std::vector<Line> buildEdges(const Alpha &a, int w, int h) {
    std::vector<Line> e;
    auto on = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && a[size_t(y) * w + x] > 127; };
    for (int y = 0; y <= h; y++) {
        int s = -1;
        for (int x = 0; x <= w; x++) {
            if (x < w && on(x, y - 1) != on(x, y)) { if (s < 0) s = x; }
            else if (s >= 0) { e.push_back({s, y, x, y}); s = -1; }
        }
    }
    for (int x = 0; x <= w; x++) {
        int s = -1;
        for (int y = 0; y <= h; y++) {
            if (y < h && on(x - 1, y) != on(x, y)) { if (s < 0) s = y; }
            else if (s >= 0) { e.push_back({x, s, x, y}); s = -1; }
        }
    }
    return e;
}
void Editor::setSelection(Image mask) {
    d.selEdges.reset();
    if (!mask.null()) {
        const Alpha a = maskAlpha(mask);
        bool any = false;
        for (uint8_t v : a) if (v > 127) { any = true; break; }
        if (any) d.selEdges = std::make_shared<const std::vector<Line>>(buildEdges(a, mask.w, mask.h));
        else mask = Image();
    }
    d.sel = mask;
    if (onView) onView();
}
void Editor::combineSel(const Image &shape, SelMode mode) {
    if (mode == SelMode::Replace || d.sel.null()) {
        if (mode == SelMode::Replace || mode == SelMode::Add) setSelection(shape);
        return;
    }
    Image m = d.sel;
    draw(m, shape, 0, 0, mode == SelMode::Add ? Op::Over : mode == SelMode::Sub ? Op::Out : Op::In);
    setSelection(m);
}
void Editor::finishSel(Image mask, SelMode mode, int feather) {
    if (feather > 0) {
        Alpha a = maskAlpha(mask);
        blurAlpha(a, d.w, d.h, feather);
        mask = alphaToMask(a, d.w, d.h);
    }
    combineSel(mask, mode);
    commit();
}
void Editor::selectAll() { setSelection(rectMask(d.w, d.h, docRect())); commit(); }
void Editor::selectNone() { if (!d.sel.null()) { lastSel = d.sel; setSelection(Image()); commit(); } }
void Editor::reselect() {
    if (lastSel.null() || lastSel.w != d.w || lastSel.h != d.h) return toast(TR("다시 선택할 영역이 없습니다"));
    setSelection(lastSel);
    commit();
}
void Editor::invertSel() {
    if (d.sel.null()) return toast(TR("선택 영역이 없습니다"));
    Image m = rectMask(d.w, d.h, docRect());
    draw(m, d.sel, 0, 0, Op::Out);
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
    if (d.sel.null()) return toast(TR("선택 영역이 없습니다"));
    Alpha a = maskAlpha(d.sel);
    fn(a);
    setSelection(alphaToMask(a, d.w, d.h));
    commit();
}

// ---------- compositing ----------
void Editor::composite(Image &out, IRect clip) const {
    clip = clip & docRect();
    if (clip.empty()) return;
    clearRect(out, clip);
    for (const auto &l : d.layers) if (l.visible) draw(out, l.img, clip.x, clip.y, Op::Over, l.opacity, l.blend, &clip);
}
Image Editor::flatten() const {
    Image c(d.w, d.h);
    composite(c, docRect());
    return c;
}
// draws canvas-sized temp image `src` onto the layer, clipped to the selection
void Editor::paintMasked(Layer &l, Image src, double alpha) {
    if (!d.sel.null()) draw(src, d.sel, 0, 0, Op::In);
    draw(l.img, src, 0, 0, Op::Over, alpha);
}

// ---------- off-canvas aware layer access ----------
Full Editor::fullOf(const Layer &l) const {
    if (l.ext.null()) return {l.img, Pt{0, 0}};
    const IRect u = docRect().united(IRect{l.extPos.x, l.extPos.y, l.ext.w, l.ext.h});
    Image c(u.w, u.h);
    draw(c, l.ext, l.extPos.x - u.x, l.extPos.y - u.y, Op::Copy);
    draw(c, l.img, -u.x, -u.y, Op::Over);
    return {c, u.pos()};
}
void Editor::setFull(Layer &l, const Full &f) {
    l.img = Image(d.w, d.h);
    l.ext = Image();
    l.extPos = Pt();
    if (f.img.null()) return;
    draw(l.img, f.img, f.pos.x, f.pos.y, Op::Copy);
    if (docRect().contains(IRect{f.pos.x, f.pos.y, f.img.w, f.img.h})) return;
    Image e = f.img;
    clearRect(e, docRect().moved(Pt{-f.pos.x, -f.pos.y}));
    const IRect bb = alphaBounds(e);
    if (bb.empty()) return;
    l.ext = e.copy(bb);
    l.extPos = f.pos + bb.pos();
}
Lifted Editor::lift(const Layer &l) const {
    const Full F = fullOf(l);
    Lifted L;
    L.pos = F.pos;
    L.flt = F.img;
    L.base = Image(F.img.w, F.img.h);
    if (!d.sel.null()) {
        Image m(F.img.w, F.img.h); // selection mask in the full layer's coordinates
        draw(m, d.sel, -F.pos.x, -F.pos.y, Op::Copy);
        draw(L.flt, m, 0, 0, Op::In);
        L.base = F.img;
        draw(L.base, m, 0, 0, Op::Out);
    }
    return L;
}
// base plus float rect s (float coords) placed at dst (doc coords), rotated by angle degrees around
// dst's center; origin = doc coords of target's top-left
void Editor::drawParts(Image &target, const Lifted &L, RectF s, RectF dst, Pt origin, double angle) const {
    target.fill(0);
    draw(target, L.base, L.pos.x - origin.x, L.pos.y - origin.y, Op::Copy);
    const RectF r = dst.moved(PtF{double(-origin.x), double(-origin.y)});
    const bool exact = angle == 0 && s.w == r.w && s.h == r.h && r.x == std::floor(r.x) && r.y == std::floor(r.y) && s.x == std::floor(s.x) && s.y == std::floor(s.y);
    if (exact) {
        const IRect sr = s.toRect();
        draw(target, L.flt, int(r.x), int(r.y), Op::Over, 1, Blend::Normal, &sr);
    } else drawTransformed(target, L.flt, s, r, angle);
}
void Editor::commitParts(Layer &l, const Lifted &L, IRect s, RectF dst, double angle) {
    const double rad = angle * M_PI / 180, cs = std::fabs(std::cos(rad)), sn = std::fabs(std::sin(rad));
    const double bw = dst.w * cs + dst.h * sn, bh = dst.w * sn + dst.h * cs;
    const PtF c = dst.center();
    const IRect covered{int(std::floor(c.x - bw / 2)) - 1, int(std::floor(c.y - bh / 2)) - 1, int(std::ceil(bw)) + 3, int(std::ceil(bh)) + 3};
    const IRect u = IRect{L.pos.x, L.pos.y, L.base.w, L.base.h}.united(covered) & IRect{-PAD, -PAD, d.w + 2 * PAD, d.h + 2 * PAD};
    Image c2(u.w, u.h);
    drawParts(c2, L, s, dst, u.pos(), angle);
    setFull(l, {c2, u.pos()});
}
void Editor::moveBy(Layer &l, const Lifted &L, int dx, int dy) {
    const IRect s{0, 0, L.flt.w, L.flt.h};
    commitParts(l, L, s, RectF(s.moved(L.pos + Pt{dx, dy})));
    if (!d.sel.null()) {
        Image m(d.w, d.h);
        draw(m, d.sel, dx, dy, Op::Copy);
        setSelection(m);
    }
    commit();
    damage();
}
static Full clampFull(const Full &f, int nw, int nh) {
    if (f.img.null()) return f;
    const IRect full{f.pos.x, f.pos.y, f.img.w, f.img.h}, r = full & IRect{-PAD, -PAD, nw + 2 * PAD, nh + 2 * PAD};
    if (r == full) return f;
    if (r.empty()) return {Image(), Pt()};
    return {f.img.copy(r.moved(Pt{-f.pos.x, -f.pos.y})), r.pos()};
}
// f maps a full layer from old doc coords to new doc coords
void Editor::mapAll(int nw, int nh, const std::function<Full(const Full &)> &f) {
    std::vector<Full> fulls;
    for (const auto &l : d.layers) fulls.push_back(clampFull(f(fullOf(l)), nw, nh));
    Image sel;
    if (!d.sel.null()) {
        const Full s = f({d.sel, Pt{0, 0}});
        sel = Image(nw, nh);
        if (!s.img.null()) draw(sel, s.img, s.pos.x, s.pos.y, Op::Copy);
    }
    d.w = nw; d.h = nh;
    for (size_t i = 0; i < d.layers.size(); i++) setFull(d.layers[i], fulls[i]);
    setSelection(sel);
    commit();
    damage();
}
Full Editor::xfFull(const Full &f, DocXf x) const {
    const int w = d.w, h = d.h, px = f.pos.x, py = f.pos.y, fw = f.img.w, fh = f.img.h;
    switch (x) {
    case DocXf::RotCW: return {rot90(f.img, true), Pt{h - py - fh, px}};
    case DocXf::RotCCW: return {rot90(f.img, false), Pt{py, w - px - fw}};
    case DocXf::Rot180: return {mirrored(f.img, true, true), Pt{w - px - fw, h - py - fh}};
    case DocXf::FlipH: return {mirrored(f.img, true, false), Pt{w - px - fw, py}};
    default: return {mirrored(f.img, false, true), Pt{px, h - py - fh}};
    }
}

// ---------- document operations ----------
void Editor::deleteSel() {
    Layer *l = editable();
    if (!l) return;
    if (!d.sel.null()) draw(l->img, d.sel, 0, 0, Op::Out);
    else { l->img = Image(d.w, d.h); l->ext = Image(); }
    commit();
    damage();
}
void Editor::fill(Px rgb) {
    Layer *l = editable();
    if (!l) return;
    Image t(d.w, d.h);
    t.fill(0xff000000u | rgb);
    paintMasked(*l, t);
    commit();
    damage();
}
void Editor::colorExt(Layer &l, const ColorMat &cm) {
    if (l.ext.null() || !d.sel.null()) return;
    const Image src = l.ext;
    applyColor(l.ext, src, cm, nullptr);
}
void Editor::quickColor(const ColorMat &cm) {
    Layer *l = editable();
    if (!l) return;
    const Image src = l->img;
    applyColor(l->img, src, cm, d.sel.null() ? nullptr : &d.sel);
    colorExt(*l, cm);
    commit();
    damage();
}
static Image selectedPixels(const Image &img, const Image &sel) {
    Image c = img;
    if (!sel.null()) draw(c, sel, 0, 0, Op::In);
    return c;
}
bool Editor::copySel(bool cut, bool merged) {
    Layer *l = editable();
    if (!l) return false;
    const Image c = selectedPixels(merged ? flatten() : l->img, d.sel);
    const IRect bb = alphaBounds(c);
    if (bb.empty()) { toast(TR("복사할 픽셀이 없습니다")); return false; }
    clip = c.copy(bb);
    clipPos = bb.pos();
    if (cut) deleteSel();
    return true;
}
void Editor::pasteImage(const Image &im, Pt pos) {
    Layer n = makeLayer(TR("붙여넣기"));
    draw(n.img, im, pos.x, pos.y, Op::Copy);
    addLayer(n);
    commit();
    damage();
}
void Editor::placeImage(const Image &src, const std::string &layerName) {
    Image im = src;
    if (im.w > d.w || im.h > d.h) {
        const double sc = std::min(double(d.w) / im.w, double(d.h) / im.h);
        im = scaled(im, std::max(1, int(im.w * sc)), std::max(1, int(im.h * sc)));
    }
    Layer n = makeLayer(layerName);
    draw(n.img, im, (d.w - im.w) / 2, (d.h - im.h) / 2, Op::Copy);
    addLayer(n);
    commit();
    damage();
}
void Editor::openImage(const Image &im, const std::string &docName) {
    d.w = im.w; d.h = im.h;
    resetDoc(im.w, im.h, {makeLayer(TR("배경"), im)});
    name = docName;
}
void Editor::newLayer() {
    char buf[64];
    snprintf(buf, sizeof buf, TR("레이어 %d"), int(d.layers.size()) + 1);
    addLayer(makeLayer(buf));
    commit();
}
void Editor::layerViaCopy(bool cut) {
    Layer *l = editable();
    if (!l) return;
    Layer n = makeLayer(l->name + " " + TR("복사본"), selectedPixels(l->img, d.sel));
    if (d.sel.null()) { n.opacity = l->opacity; n.blend = l->blend; n.ext = l->ext; n.extPos = l->extPos; }
    else if (cut) draw(l->img, d.sel, 0, 0, Op::Out);
    addLayer(n);
    commit();
    damage();
}
void Editor::duplicateLayer() {
    Layer *l = active();
    if (!l) return;
    Layer n = *l;
    n.id = ++idSeq;
    n.name += std::string(" ") + TR("복사본");
    addLayer(n);
    commit();
    damage();
}
void Editor::deleteLayer() {
    if (d.layers.size() < 2) return toast(TR("마지막 레이어는 삭제할 수 없습니다"));
    const int i = activeIdx();
    d.layers.erase(d.layers.begin() + i);
    d.activeId = d.layers[std::max(0, i - 1)].id;
    commit();
    damage();
}
void Editor::moveLayer(int dir) {
    const int i = activeIdx(), j = i + dir;
    if (i < 0 || j < 0 || j >= int(d.layers.size())) return;
    std::swap(d.layers[i], d.layers[j]);
    commit();
    damage();
}
void Editor::moveLayerTo(int from, int to) {
    const int n = int(d.layers.size());
    if (from < 0 || from >= n) return;
    to = std::min(n - 1, std::max(0, to));
    if (from == to) return;
    const Layer l = d.layers[from];
    d.layers.erase(d.layers.begin() + from);
    d.layers.insert(d.layers.begin() + to, l);
    commit();
    damage();
}
void Editor::mergeDown() {
    const int i = activeIdx();
    if (i <= 0) return toast(TR("아래에 레이어가 없습니다"));
    const Layer up = d.layers[i];
    const Full A = fullOf(d.layers[i - 1]), B = fullOf(up);
    const IRect u = IRect{A.pos.x, A.pos.y, A.img.w, A.img.h}.united(IRect{B.pos.x, B.pos.y, B.img.w, B.img.h});
    Image c(u.w, u.h);
    draw(c, A.img, A.pos.x - u.x, A.pos.y - u.y, Op::Copy);
    draw(c, B.img, B.pos.x - u.x, B.pos.y - u.y, Op::Over, up.opacity, up.blend);
    setFull(d.layers[i - 1], {c, u.pos()});
    d.activeId = d.layers[i - 1].id;
    d.layers.erase(d.layers.begin() + i);
    commit();
    damage();
}
void Editor::flattenAll() {
    const Layer l = makeLayer(TR("배경"), flatten());
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
        const IRect keep{int(std::floor(-PAD / sx)), int(std::floor(-PAD / sy)), int(std::ceil((nw + 2 * PAD) / sx)), int(std::ceil((nh + 2 * PAD) / sy))};
        const IRect src = IRect{f0.pos.x, f0.pos.y, f0.img.w, f0.img.h} & keep;
        if (src.empty()) return Full{Image(), Pt()};
        const Image part = f0.img.copy(src.moved(Pt{-f0.pos.x, -f0.pos.y}));
        const int x0 = int(std::floor(src.x * sx + 1e-6)), y0 = int(std::floor(src.y * sy + 1e-6));
        const int x1 = int(std::ceil(src.r() * sx - 1e-6)), y1 = int(std::ceil(src.b() * sy - 1e-6));
        return Full{scaled(part, std::max(1, x1 - x0), std::max(1, y1 - y0)), Pt{x0, y0}};
    });
}
void Editor::resizeCanvas(int nw, int nh, int anchor) {
    if (nw == d.w && nh == d.h) return;
    const Pt o{int(std::lround((nw - d.w) * (anchor % 3) / 2.0)), int(std::lround((nh - d.h) * (anchor / 3) / 2.0))};
    mapAll(nw, nh, [o](const Full &f) { return Full{f.img, f.pos + o}; });
}
void Editor::cropToSel() {
    const IRect bb = d.sel.null() ? IRect{} : alphaBounds(d.sel);
    if (bb.empty()) return toast(TR("선택 영역이 없습니다"));
    cropRect(bb);
}
void Editor::cropRect(IRect r) {
    if (r.w < 1 || r.h < 1 || r == docRect()) return;
    mapAll(r.w, r.h, [r](const Full &f) { return Full{f.img, f.pos - r.pos()}; });
}

// ---------- project files ----------
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string base64Encode(const std::string &in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        const unsigned b0 = uint8_t(in[i]), b1 = i + 1 < in.size() ? uint8_t(in[i + 1]) : 0, b2 = i + 2 < in.size() ? uint8_t(in[i + 2]) : 0;
        out += B64[b0 >> 2];
        out += B64[(b0 & 3) << 4 | b1 >> 4];
        out += i + 1 < in.size() ? B64[(b1 & 15) << 2 | b2 >> 6] : '=';
        out += i + 2 < in.size() ? B64[b2 & 63] : '=';
    }
    return out;
}
std::string base64Decode(const std::string &in) {
    int tbl[256];
    for (int &v : tbl) v = -1;
    for (int i = 0; i < 64; i++) tbl[uint8_t(B64[i])] = i;
    std::string out;
    unsigned acc = 0;
    int bits = 0;
    for (char ch : in) {
        const int v = tbl[uint8_t(ch)];
        if (v < 0) continue;
        acc = acc << 6 | unsigned(v);
        bits += 6;
        if (bits >= 8) { bits -= 8; out += char((acc >> bits) & 255); }
    }
    return out;
}
static std::string jsonStr(const std::string &s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (uint8_t(c) < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); o += buf; }
        else o += c;
    }
    return o + "\"";
}
std::string Editor::projectJson(const PngEncoder &encode) const {
    std::string o = "{\"app\":\"r-painter\",\"w\":" + std::to_string(d.w) + ",\"h\":" + std::to_string(d.h) + ",\"layers\":[";
    for (size_t i = 0; i < d.layers.size(); i++) {
        const Layer &l = d.layers[i];
        const Full F = fullOf(l);
        std::string css = "source-over";
        for (const auto &b : blendModes()) if (b.mode == l.blend) css = b.css;
        char num[64];
        snprintf(num, sizeof num, "%.4g", l.opacity);
        o += std::string(i ? "," : "") + "{\"name\":" + jsonStr(l.name) + ",\"opacity\":" + num + ",\"visible\":" + (l.visible ? "true" : "false")
            + ",\"blend\":\"" + css + "\",\"x\":" + std::to_string(F.pos.x) + ",\"y\":" + std::to_string(F.pos.y)
            + ",\"data\":\"data:image/png;base64," + base64Encode(encode(F.img)) + "\"}";
    }
    return o + "]}";
}
// Minimal JSON reader: enough for the project format (objects, arrays, strings, numbers, booleans).
namespace {
struct JVal {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    double n = 0;
    bool b = false;
    std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;
    const JVal *get(const char *k) const { for (const auto &p : o) if (p.first == k) return &p.second; return nullptr; }
};
struct JParser {
    const std::string &t;
    size_t i = 0;
    bool ok = true;
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\t' || t[i] == '\r')) i++; }
    std::string str() {
        std::string o;
        i++; // opening quote
        while (i < t.size() && t[i] != '"') {
            if (t[i] != '\\') { o += t[i++]; continue; }
            const char e = i + 1 < t.size() ? t[i + 1] : 0;
            i += 2;
            if (e == 'n') o += '\n';
            else if (e == 't') o += '\t';
            else if (e == 'r') o += '\r';
            else if (e == 'b' || e == 'f') o += ' ';
            else if (e == 'u' && i + 4 <= t.size()) {
                const unsigned cp = unsigned(strtoul(t.substr(i, 4).c_str(), nullptr, 16));
                i += 4;
                if (cp < 0x80) o += char(cp);
                else if (cp < 0x800) { o += char(0xc0 | cp >> 6); o += char(0x80 | (cp & 63)); }
                else { o += char(0xe0 | cp >> 12); o += char(0x80 | ((cp >> 6) & 63)); o += char(0x80 | (cp & 63)); }
            } else o += e;
        }
        if (i >= t.size()) ok = false;
        i++;
        return o;
    }
    JVal val() {
        JVal v;
        ws();
        if (i >= t.size()) { ok = false; return v; }
        const char c = t[i];
        if (c == '{') {
            v.t = JVal::Obj; i++; ws();
            if (i < t.size() && t[i] == '}') { i++; return v; }
            while (ok && i < t.size()) {
                ws();
                if (t[i] != '"') { ok = false; break; }
                std::string k = str();
                ws();
                if (i >= t.size() || t[i] != ':') { ok = false; break; }
                i++;
                v.o.emplace_back(k, val());
                ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == '}') { i++; break; }
                ok = false;
            }
        } else if (c == '[') {
            v.t = JVal::Arr; i++; ws();
            if (i < t.size() && t[i] == ']') { i++; return v; }
            while (ok && i < t.size()) {
                v.a.push_back(val());
                ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == ']') { i++; break; }
                ok = false;
            }
        } else if (c == '"') { v.t = JVal::Str; v.s = str(); }
        else if (t.compare(i, 4, "true") == 0) { v.t = JVal::Bool; v.b = true; i += 4; }
        else if (t.compare(i, 5, "false") == 0) { v.t = JVal::Bool; i += 5; }
        else if (t.compare(i, 4, "null") == 0) { i += 4; }
        else {
            char *end = nullptr;
            v.n = strtod(t.c_str() + i, &end);
            if (end == t.c_str() + i) ok = false;
            v.t = JVal::Num;
            i = size_t(end - t.c_str());
        }
        return v;
    }
};
}
bool Editor::loadProjectJson(const std::string &json, const PngDecoder &decode) {
    JParser p{json};
    const JVal root = p.val();
    const JVal *jw = root.get("w"), *jh = root.get("h"), *jl = root.get("layers");
    if (!p.ok || !jw || !jh || !jl || jl->a.empty()) return false;
    const int w = int(jw->n), h = int(jh->n);
    if (w < 1 || h < 1 || w > MAX_DIM || h > MAX_DIM) return false;
    std::vector<Image> imgs;
    for (const JVal &lv : jl->a) {
        const JVal *data = lv.get("data");
        if (!data) return false;
        const Image im = decode(base64Decode(data->s.substr(data->s.find(',') + 1)));
        if (im.null()) return false;
        imgs.push_back(im);
    }
    d.w = w; d.h = h;
    std::vector<Layer> layers;
    for (size_t i = 0; i < jl->a.size(); i++) {
        const JVal &lv = jl->a[i];
        const JVal *n = lv.get("name"), *op = lv.get("opacity"), *vis = lv.get("visible"), *bl = lv.get("blend"), *x = lv.get("x"), *y = lv.get("y");
        Layer l = makeLayer(n ? n->s : TR("레이어"));
        setFull(l, {imgs[i], Pt{x ? int(x->n) : 0, y ? int(y->n) : 0}});
        l.opacity = op ? op->n : 1;
        l.visible = vis ? vis->b : true;
        if (bl) for (const auto &b : blendModes()) if (bl->s == b.css) l.blend = b.mode;
        layers.push_back(l);
    }
    resetDoc(w, h, layers);
    return true;
}
