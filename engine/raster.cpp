#include "raster.h"
#include <cstring>

const std::vector<BlendInfo> &blendModes() {
    static const std::vector<BlendInfo> v = {
        {Blend::Normal, "source-over", "표준"}, {Blend::Multiply, "multiply", "곱하기"}, {Blend::Screen, "screen", "스크린"},
        {Blend::Overlay, "overlay", "오버레이"}, {Blend::Darken, "darken", "어둡게"}, {Blend::Lighten, "lighten", "밝게"},
        {Blend::ColorDodge, "color-dodge", "색상 닷지"}, {Blend::ColorBurn, "color-burn", "색상 번"}, {Blend::HardLight, "hard-light", "하드 라이트"},
        {Blend::SoftLight, "soft-light", "소프트 라이트"}, {Blend::Difference, "difference", "차이"}, {Blend::Exclusion, "exclusion", "제외"},
    };
    return v;
}

Image Image::copy(IRect r) const {
    Image out(r.w, r.h);
    const IRect c = r & rect();
    for (int y = c.y; y < c.b(); y++) memcpy(out.wrow(y - r.y) + (c.x - r.x), row(y) + c.x, size_t(c.w) * 4);
    return out;
}

// ---------- compositing ----------
static inline Px scalePx(Px p, int a) {
    return Px(mul255(pxA(p), a)) << 24 | Px(mul255(pxR(p), a)) << 16 | Px(mul255(pxG(p), a)) << 8 | Px(mul255(pxB(p), a));
}
static inline Px overPx(Px s, Px d) {
    const int ia = 255 - pxA(s);
    if (ia == 0) return s;
    if (ia == 255) return d;
    return s + scalePx(d, ia);
}
// separable blend function on straight colors in 0..1 (cb = backdrop, cs = source)
static inline double blendFn(Blend m, double cb, double cs) {
    switch (m) {
    case Blend::Multiply: return cb * cs;
    case Blend::Screen: return cb + cs - cb * cs;
    case Blend::Overlay: return cb <= 0.5 ? 2 * cb * cs : 1 - 2 * (1 - cb) * (1 - cs);
    case Blend::Darken: return std::min(cb, cs);
    case Blend::Lighten: return std::max(cb, cs);
    case Blend::ColorDodge: return cb <= 0 ? 0 : cs >= 1 ? 1 : std::min(1.0, cb / (1 - cs));
    case Blend::ColorBurn: return cb >= 1 ? 1 : cs <= 0 ? 0 : 1 - std::min(1.0, (1 - cb) / cs);
    case Blend::HardLight: return cs <= 0.5 ? 2 * cb * cs : 1 - 2 * (1 - cb) * (1 - cs);
    case Blend::SoftLight: {
        if (cs <= 0.5) return cb - (1 - 2 * cs) * cb * (1 - cb);
        const double g = cb <= 0.25 ? ((16 * cb - 12) * cb + 4) * cb : std::sqrt(cb);
        return cb + (2 * cs - 1) * (g - cb);
    }
    case Blend::Difference: return std::fabs(cb - cs);
    case Blend::Exclusion: return cb + cs - 2 * cb * cs;
    default: return cs;
    }
}
static inline Px blendPx(Blend m, Px s, Px d) {
    const double as = pxA(s) / 255.0, ab = pxA(d) / 255.0;
    if (as <= 0) return d;
    if (ab <= 0) return s;
    const double ao = as + ab - as * ab;
    Px out = Px(std::lround(ao * 255)) << 24;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const double sp = ((s >> shift) & 255) / 255.0, dp = ((d >> shift) & 255) / 255.0; // premultiplied
        const double co = sp * (1 - ab) + dp * (1 - as) + as * ab * blendFn(m, dp / ab, sp / as);
        out |= Px(std::min(255L, std::max(0L, std::lround(co * 255)))) << shift;
    }
    return out;
}
void draw(Image &dst, const Image &src, int dx, int dy, Op op, double alpha, Blend blend, const IRect *srcRect) {
    if (src.null() || dst.null()) return;
    const IRect sr = srcRect ? (*srcRect & src.rect()) : src.rect();
    if (srcRect) { dx += sr.x - srcRect->x; dy += sr.y - srcRect->y; }
    const IRect area = IRect{dx, dy, sr.w, sr.h} & dst.rect();
    if (area.empty()) return;
    const int a8 = int(std::lround(std::min(1.0, std::max(0.0, alpha)) * 255));
    for (int y = area.y; y < area.b(); y++) {
        const Px *s = src.row(y - dy + sr.y) + (area.x - dx + sr.x);
        Px *d = dst.wrow(y) + area.x;
        for (int x = 0; x < area.w; x++) {
            const Px sp = a8 == 255 ? s[x] : scalePx(s[x], a8);
            switch (op) {
            case Op::Over: d[x] = blend == Blend::Normal ? overPx(sp, d[x]) : blendPx(blend, sp, d[x]); break;
            case Op::Copy: d[x] = sp; break;
            case Op::In: d[x] = scalePx(d[x], pxA(sp)); break;
            case Op::Out: d[x] = scalePx(d[x], 255 - pxA(sp)); break;
            }
        }
    }
}
void clearRect(Image &im, IRect r) {
    const IRect c = r & im.rect();
    for (int y = c.y; y < c.b(); y++) memset(im.wrow(y) + c.x, 0, size_t(c.w) * 4);
}

// ---------- resampling ----------
// bilinear sample of the source rect s; everything outside s counts as transparent, which antialiases the edges
static inline Px sampleBilinear(const Image &src, const RectF &s, double u, double v) {
    u -= 0.5; v -= 0.5;
    const int x0 = int(std::floor(u)), y0 = int(std::floor(v));
    const double fx = u - x0, fy = v - y0;
    const int sx0 = int(std::floor(s.x)), sy0 = int(std::floor(s.y)), sx1 = int(std::ceil(s.x + s.w)), sy1 = int(std::ceil(s.y + s.h));
    double acc[4] = {0, 0, 0, 0};
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            const int x = x0 + i, y = y0 + j;
            if (x < sx0 || y < sy0 || x >= sx1 || y >= sy1 || x < 0 || y < 0 || x >= src.w || y >= src.h) continue;
            const double wgt = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
            const Px p = src.row(y)[x];
            acc[0] += pxA(p) * wgt; acc[1] += pxR(p) * wgt; acc[2] += pxG(p) * wgt; acc[3] += pxB(p) * wgt;
        }
    return Px(std::lround(acc[0])) << 24 | Px(std::lround(acc[1])) << 16 | Px(std::lround(acc[2])) << 8 | Px(std::lround(acc[3]));
}
void drawTransformed(Image &dst, const Image &src, RectF s, RectF d, double angle) {
    if (src.null() || d.w <= 0 || d.h <= 0) return;
    const double rad = angle * M_PI / 180, cs = std::cos(rad), sn = std::sin(rad);
    const PtF c = d.center();
    // bounding box of the rotated destination rect
    const double bw = std::fabs(d.w * cs) + std::fabs(d.h * sn), bh = std::fabs(d.w * sn) + std::fabs(d.h * cs);
    const IRect box = IRect{int(std::floor(c.x - bw / 2)) - 1, int(std::floor(c.y - bh / 2)) - 1, int(std::ceil(bw)) + 3, int(std::ceil(bh)) + 3} & dst.rect();
    for (int y = box.y; y < box.b(); y++) {
        Px *row = dst.wrow(y);
        for (int x = box.x; x < box.r(); x++) {
            const double px = x + 0.5 - c.x, py = y + 0.5 - c.y;
            // undo the rotation, then map the box-local position into the source rect
            const double lx = px * cs + py * sn + d.w / 2, ly = -px * sn + py * cs + d.h / 2;
            if (lx < -1 || ly < -1 || lx > d.w + 1 || ly > d.h + 1) continue;
            const Px p = sampleBilinear(src, s, s.x + lx / d.w * s.w, s.y + ly / d.h * s.h);
            if (pxA(p)) row[x] = overPx(p, row[x]);
        }
    }
}
// per destination index: source indices and weights
static void resampleWeights(int sn, int dn, std::vector<int> &start, std::vector<std::vector<float>> &wts) {
    const double f = double(sn) / dn;
    start.resize(dn); wts.resize(dn);
    for (int i = 0; i < dn; i++) {
        if (f >= 1) { // shrinking: box average over [i*f, (i+1)*f)
            const double a = i * f, b = (i + 1) * f;
            const int i0 = int(std::floor(a)), i1 = std::min(sn - 1, int(std::ceil(b)) - 1);
            start[i] = i0;
            for (int k = i0; k <= i1; k++) wts[i].push_back(float((std::min(b, k + 1.0) - std::max(a, double(k))) / f));
        } else { // enlarging: linear interpolation between the two nearest source pixels
            const double c = (i + 0.5) * f - 0.5;
            const int i0 = std::min(sn - 1, std::max(0, int(std::floor(c))));
            const int i1 = std::min(sn - 1, i0 + 1);
            const double t = std::min(1.0, std::max(0.0, c - i0));
            start[i] = i0;
            wts[i].push_back(float(i1 == i0 ? 1 : 1 - t));
            if (i1 != i0) wts[i].push_back(float(t));
        }
    }
}
Image scaled(const Image &src, int nw, int nh) {
    nw = std::max(1, nw); nh = std::max(1, nh);
    if (nw == src.w && nh == src.h) return src;
    std::vector<int> sx, sy;
    std::vector<std::vector<float>> wx, wy;
    resampleWeights(src.w, nw, sx, wx);
    resampleWeights(src.h, nh, sy, wy);
    std::vector<float> tmp(size_t(nw) * src.h * 4); // horizontal pass
    for (int y = 0; y < src.h; y++) {
        const Px *s = src.row(y);
        float *t = &tmp[size_t(y) * nw * 4];
        for (int x = 0; x < nw; x++, t += 4) {
            float a = 0, r = 0, g = 0, b = 0;
            for (size_t k = 0; k < wx[x].size(); k++) {
                const Px p = s[sx[x] + k];
                const float wgt = wx[x][k];
                a += pxA(p) * wgt; r += pxR(p) * wgt; g += pxG(p) * wgt; b += pxB(p) * wgt;
            }
            t[0] = a; t[1] = r; t[2] = g; t[3] = b;
        }
    }
    Image out(nw, nh);
    for (int y = 0; y < nh; y++) {
        Px *o = out.wrow(y);
        for (int x = 0; x < nw; x++) {
            float a = 0, r = 0, g = 0, b = 0;
            for (size_t k = 0; k < wy[y].size(); k++) {
                const float *t = &tmp[(size_t(sy[y] + k) * nw + x) * 4], wgt = wy[y][k];
                a += t[0] * wgt; r += t[1] * wgt; g += t[2] * wgt; b += t[3] * wgt;
            }
            const int ia = std::min(255, int(a + 0.5f));
            o[x] = Px(ia) << 24 | Px(std::min(ia, int(r + 0.5f))) << 16 | Px(std::min(ia, int(g + 0.5f))) << 8 | Px(std::min(ia, int(b + 0.5f)));
        }
    }
    return out;
}
Image rot90(const Image &s, bool cw) {
    Image o(s.h, s.w);
    for (int y = 0; y < s.w; y++) {
        Px *p = o.wrow(y);
        for (int x = 0; x < s.h; x++) p[x] = cw ? s.pixel(y, s.h - 1 - x) : s.pixel(s.w - 1 - y, x);
    }
    return o;
}
Image mirrored(const Image &s, bool horizontal, bool vertical) {
    Image o(s.w, s.h);
    for (int y = 0; y < s.h; y++) {
        const Px *in = s.row(vertical ? s.h - 1 - y : y);
        Px *p = o.wrow(y);
        for (int x = 0; x < s.w; x++) p[x] = in[horizontal ? s.w - 1 - x : x];
    }
    return o;
}
IRect alphaBounds(const Image &im) {
    if (im.null()) return {};
    int x0 = im.w, y0 = im.h, x1 = -1, y1 = -1;
    for (int y = 0; y < im.h; y++) {
        const Px *p = im.row(y);
        for (int x = 0; x < im.w; x++) {
            if (pxA(p[x])) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    return x1 < 0 ? IRect{} : IRect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

// ---------- alpha masks ----------
Alpha maskAlpha(const Image &m) {
    Alpha a(size_t(m.w) * m.h);
    for (int y = 0; y < m.h; y++) {
        const Px *p = m.row(y);
        for (int x = 0; x < m.w; x++) a[size_t(y) * m.w + x] = uint8_t(pxA(p[x]));
    }
    return a;
}
Image alphaToMask(const Alpha &a, int w, int h, Px rgb) {
    Image m(w, h);
    for (int y = 0; y < h; y++) {
        Px *p = m.wrow(y);
        for (int x = 0; x < w; x++) p[x] = premul(pxR(rgb), pxG(rgb), pxB(rgb), a[size_t(y) * w + x]);
    }
    return m;
}
// 1D box pass with replicated edges; fn(sum, n) maps the window sum to the output value
template <class F>
static void boxPass(const uint8_t *s, uint8_t *d, int w, int h, int r, bool horiz, F fn) {
    const int n = 2 * r + 1, len = horiz ? w : h, lines = horiz ? h : w, step = horiz ? 1 : w;
    for (int l = 0; l < lines; l++) {
        const int base = horiz ? l * w : l;
        int sum = 0;
        for (int i = -r; i <= r; i++) sum += s[base + std::min(len - 1, std::max(0, i)) * step];
        for (int i = 0; i < len; i++) {
            d[base + i * step] = fn(sum, n);
            sum += s[base + std::min(len - 1, i + r + 1) * step] - s[base + std::max(0, i - r) * step];
        }
    }
}
void blurAlpha(Alpha &a, int w, int h, int r, int passes) {
    if (r <= 0) return;
    Alpha t(a.size());
    auto avg = [](int s, int n) { return uint8_t((s + n / 2) / n); };
    for (int i = 0; i < passes; i++) {
        boxPass(a.data(), t.data(), w, h, r, true, avg);
        boxPass(t.data(), a.data(), w, h, r, false, avg);
    }
}
void morphAlpha(Alpha &a, int w, int h, int r, bool expand) {
    for (auto &v : a) v = v > 127 ? 255 : 0;
    Alpha t(a.size());
    auto fn = [expand](int s, int n) { return uint8_t(expand ? (s > 0 ? 255 : 0) : (s == 255 * n ? 255 : 0)); };
    boxPass(a.data(), t.data(), w, h, r, true, fn);
    boxPass(t.data(), a.data(), w, h, r, false, fn);
}
Alpha floodMask(const Image &img, int x, int y, int tol, bool contiguous) {
    const int w = img.w, h = img.h;
    Alpha out(size_t(w) * h, 0);
    const Px *px = img.row(0);
    const Px c0 = px[size_t(y) * w + x];
    auto match = [&](int p) {
        const Px c = px[p];
        return std::abs(pxR(c) - pxR(c0)) <= tol && std::abs(pxG(c) - pxG(c0)) <= tol && std::abs(pxB(c) - pxB(c0)) <= tol && std::abs(pxA(c) - pxA(c0)) <= tol;
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

// ---------- antialiased shapes ----------
Image rectMask(int w, int h, IRect r) {
    Image m(w, h);
    const IRect c = r & m.rect();
    for (int y = c.y; y < c.b(); y++) std::fill(m.wrow(y) + c.x, m.wrow(y) + c.r(), 0xffffffffu);
    return m;
}
Image ellipseMask(int w, int h, RectF r) {
    Image m(w, h);
    const double cx = r.x + r.w / 2, cy = r.y + r.h / 2, rx = r.w / 2, ry = r.h / 2;
    if (rx <= 0 || ry <= 0) return m;
    const IRect box = IRect{int(std::floor(r.x)), int(std::floor(r.y)), int(std::ceil(r.w)) + 1, int(std::ceil(r.h)) + 1} & m.rect();
    for (int y = box.y; y < box.b(); y++) {
        Px *p = m.wrow(y);
        for (int x = box.x; x < box.r(); x++) {
            int hits = 0; // 4x4 supersampling
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    const double nx = (x + (i + 0.5) / 4 - cx) / rx, ny = (y + (j + 0.5) / 4 - cy) / ry;
                    if (nx * nx + ny * ny <= 1) hits++;
                }
            if (hits) { const int a = hits * 255 / 16; p[x] = premul(255, 255, 255, a); }
        }
    }
    return m;
}
// even-odd scanline fill, 4 sub-scanlines per row with exact horizontal coverage
Image polygonMask(int w, int h, const std::vector<PtF> &pts) {
    Image m(w, h);
    const size_t n = pts.size();
    if (n < 3) return m;
    std::vector<float> acc(w);
    std::vector<double> xs;
    for (int y = 0; y < h; y++) {
        std::fill(acc.begin(), acc.end(), 0.f);
        bool any = false;
        for (int sub = 0; sub < 4; sub++) {
            const double sy = y + (sub + 0.5) / 4;
            xs.clear();
            for (size_t i = 0; i < n; i++) {
                const PtF a = pts[i], b = pts[(i + 1) % n];
                if ((a.y <= sy) == (b.y <= sy)) continue;
                xs.push_back(a.x + (sy - a.y) / (b.y - a.y) * (b.x - a.x));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t i = 0; i + 1 < xs.size(); i += 2) {
                const double x0 = std::max(0.0, xs[i]), x1 = std::min(double(w), xs[i + 1]);
                if (x1 <= x0) continue;
                any = true;
                const int i0 = int(std::floor(x0)), i1 = std::min(w - 1, int(std::floor(x1)));
                for (int x = i0; x <= i1; x++) acc[x] += float((std::min(x1, x + 1.0) - std::max(x0, double(x))) * 0.25);
            }
        }
        if (!any) continue;
        Px *p = m.wrow(y);
        for (int x = 0; x < w; x++) if (acc[x] > 0) p[x] = premul(255, 255, 255, std::min(255, int(acc[x] * 255 + 0.5f)));
    }
    return m;
}

// ---------- brush ----------
IRect capsule(Image &buf, PtF a, PtF b, double radius, Px rgb) {
    const double r = std::max(0.5, radius);
    const IRect box = IRect{int(std::floor(std::min(a.x, b.x) - r)) - 1, int(std::floor(std::min(a.y, b.y) - r)) - 1,
                          int(std::ceil(std::fabs(a.x - b.x) + 2 * r)) + 3, int(std::ceil(std::fabs(a.y - b.y) + 2 * r)) + 3} & buf.rect();
    const double vx = b.x - a.x, vy = b.y - a.y, len2 = vx * vx + vy * vy;
    for (int y = box.y; y < box.b(); y++) {
        Px *p = buf.wrow(y);
        for (int x = box.x; x < box.r(); x++) {
            const double px = x + 0.5 - a.x, py = y + 0.5 - a.y;
            const double t = len2 > 0 ? std::min(1.0, std::max(0.0, (px * vx + py * vy) / len2)) : 0;
            const double dist = std::hypot(px - t * vx, py - t * vy);
            const int cov = int(std::min(1.0, std::max(0.0, r - dist + 0.5)) * 255 + 0.5);
            if (cov > pxA(p[x])) p[x] = premul(pxR(rgb), pxG(rgb), pxB(rgb), cov); // union with what is already there
        }
    }
    return box;
}
IRect softDab(Image &buf, PtF c, double size, double hardness, Px rgb, double opacity) {
    const double r = std::max(0.5, size / 2), inner = r * std::min(0.99, std::max(0.0, hardness));
    const IRect box = IRect{int(std::floor(c.x - r)) - 1, int(std::floor(c.y - r)) - 1, int(std::ceil(2 * r)) + 3, int(std::ceil(2 * r)) + 3} & buf.rect();
    for (int y = box.y; y < box.b(); y++) {
        Px *p = buf.wrow(y);
        for (int x = box.x; x < box.r(); x++) {
            const double dist = std::hypot(x + 0.5 - c.x, y + 0.5 - c.y);
            if (dist >= r) continue;
            const double f = dist <= inner ? 1 : (r - dist) / (r - inner);
            p[x] = overPx(premul(pxR(rgb), pxG(rgb), pxB(rgb), int(f * opacity * 255 + 0.5)), p[x]);
        }
    }
    return box;
}
void linearGradient(Image &im, PtF a, PtF b, Px rgb0, int a0, Px rgb1, int a1) {
    const double vx = b.x - a.x, vy = b.y - a.y, len2 = std::max(1e-9, vx * vx + vy * vy);
    for (int y = 0; y < im.h; y++) {
        Px *p = im.wrow(y);
        for (int x = 0; x < im.w; x++) {
            const double t = std::min(1.0, std::max(0.0, ((x + 0.5 - a.x) * vx + (y + 0.5 - a.y) * vy) / len2));
            // interpolate premultiplied, like canvas / Qt gradients
            const Px p0 = premul(pxR(rgb0), pxG(rgb0), pxB(rgb0), a0), p1 = premul(pxR(rgb1), pxG(rgb1), pxB(rgb1), a1);
            Px out = 0;
            for (int s = 24; s >= 0; s -= 8) out |= Px(std::lround(((p0 >> s) & 255) * (1 - t) + ((p1 >> s) & 255) * t)) << s;
            p[x] = out;
        }
    }
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

void applyColor(Image &dst, const Image &src, const ColorMat &cm, const Image *sel) {
    auto cl = [](double v) { return int(std::min(255.0, std::max(0.0, v + 0.5))); };
    auto bound = [](double v) { return std::min(255.0, std::max(0.0, v)); };
    const double *m = cm.m;
    if (dst.w != src.w || dst.h != src.h) dst = Image(src.w, src.h);
    for (int y = 0; y < src.h; y++) {
        const Px *s = src.row(y);
        const Px *k = sel ? sel->row(y) : nullptr;
        Px *o = dst.wrow(y);
        for (int x = 0; x < src.w; x++) {
            const int a = pxA(s[x]);
            if (!a) { o[x] = 0; continue; }
            const Px u = straightRgb(s[x]);
            const double r = pxR(u), g = pxG(u), b = pxB(u);
            double nr = m[0] * r + m[1] * g + m[2] * b + cm.off, ng = m[3] * r + m[4] * g + m[5] * b + cm.off, nb = m[6] * r + m[7] * g + m[8] * b + cm.off;
            if (k) {
                const double t = pxA(k[x]) / 255.0;
                nr = r + (bound(nr) - r) * t; ng = g + (bound(ng) - g) * t; nb = b + (bound(nb) - b) * t;
            }
            o[x] = premul(cl(nr), cl(ng), cl(nb), a);
        }
    }
}
