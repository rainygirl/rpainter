// (named raster.h, not image.h: Haiku has a system header called image.h)
// Toolkit-free raster engine: premultiplied ARGB images and the pixel operations
// the editor needs (compositing, antialiased shapes, resampling, flood fill, color matrix).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

typedef uint32_t Px; // 0xAARRGGBB, color premultiplied by alpha
typedef std::vector<uint8_t> Alpha;

struct Pt {
    int x = 0, y = 0;
    Pt operator+(Pt o) const { return {x + o.x, y + o.y}; }
    Pt operator-(Pt o) const { return {x - o.x, y - o.y}; }
    bool operator==(Pt o) const { return x == o.x && y == o.y; }
};
struct PtF {
    double x = 0, y = 0;
    PtF operator+(PtF o) const { return {x + o.x, y + o.y}; }
    PtF operator-(PtF o) const { return {x - o.x, y - o.y}; }
    PtF operator*(double k) const { return {x * k, y * k}; }
};
// (named IRect / CursorKind because macOS system headers already define Rect and Cursor)
struct IRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
    int r() const { return x + w; }
    int b() const { return y + h; }
    Pt pos() const { return {x, y}; }
    IRect moved(Pt d) const { return {x + d.x, y + d.y, w, h}; }
    IRect operator&(IRect o) const {
        const int x0 = std::max(x, o.x), y0 = std::max(y, o.y), x1 = std::min(r(), o.r()), y1 = std::min(b(), o.b());
        return x1 > x0 && y1 > y0 ? IRect{x0, y0, x1 - x0, y1 - y0} : IRect{};
    }
    IRect united(IRect o) const {
        if (empty()) return o;
        if (o.empty()) return *this;
        const int x0 = std::min(x, o.x), y0 = std::min(y, o.y);
        return {x0, y0, std::max(r(), o.r()) - x0, std::max(b(), o.b()) - y0};
    }
    bool contains(Pt p) const { return p.x >= x && p.y >= y && p.x < r() && p.y < b(); }
    bool contains(IRect o) const { return o.x >= x && o.y >= y && o.r() <= r() && o.b() <= b(); }
    bool operator==(IRect o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(IRect o) const { return !(*this == o); }
};
struct RectF {
    double x = 0, y = 0, w = 0, h = 0;
    RectF() {}
    RectF(double x_, double y_, double w_, double h_) : x(x_), y(y_), w(w_), h(h_) {}
    RectF(IRect r) : x(r.x), y(r.y), w(r.w), h(r.h) {}
    PtF center() const { return {x + w / 2, y + h / 2}; }
    RectF moved(PtF d) const { return {x + d.x, y + d.y, w, h}; }
    bool contains(PtF p) const { return p.x >= x && p.y >= y && p.x <= x + w && p.y <= y + h; }
    bool operator==(const RectF &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    IRect toRect() const { return {int(std::lround(x)), int(std::lround(y)), int(std::lround(w)), int(std::lround(h))}; }
};

inline int pxA(Px p) { return p >> 24; }
inline int pxR(Px p) { return (p >> 16) & 255; }
inline int pxG(Px p) { return (p >> 8) & 255; }
inline int pxB(Px p) { return p & 255; }
inline int mul255(int a, int b) { const int t = a * b + 128; return (t + (t >> 8)) >> 8; }
// straight (non-premultiplied) r,g,b,a -> premultiplied pixel
inline Px premul(int r, int g, int b, int a) { return Px(a) << 24 | Px(mul255(r, a)) << 16 | Px(mul255(g, a)) << 8 | Px(mul255(b, a)); }
// premultiplied pixel -> straight 0x00RRGGBB (alpha dropped)
inline Px straightRgb(Px p) {
    const int a = pxA(p);
    if (a == 0) return 0;
    if (a == 255) return p & 0xffffff;
    return Px(std::min(255, (pxR(p) * 255 + a / 2) / a)) << 16 | Px(std::min(255, (pxG(p) * 255 + a / 2) / a)) << 8 | Px(std::min(255, (pxB(p) * 255 + a / 2) / a));
}

// Copy-on-write pixel buffer: copies are cheap and share memory until one of them is written to,
// which is what makes history snapshots affordable.
class Image {
public:
    int w = 0, h = 0;
    Image() {}
    Image(int w_, int h_) : w(std::max(1, w_)), h(std::max(1, h_)), d(std::make_shared<std::vector<Px>>(size_t(w) * h, 0)) {}
    bool null() const { return !d; }
    IRect rect() const { return {0, 0, w, h}; }
    const Px *row(int y) const { return d->data() + size_t(y) * w; }
    Px *wrow(int y) { detach(); return d->data() + size_t(y) * w; }
    Px pixel(int x, int y) const { return row(y)[x]; }
    void fill(Px p) { detach(); std::fill(d->begin(), d->end(), p); }
    Image copy(IRect r) const;
    const void *key() const { return d.get(); } // identity of the shared buffer
    size_t bytes() const { return d ? d->size() * 4 : 0; }

private:
    std::shared_ptr<std::vector<Px>> d;
    void detach() { if (d.use_count() > 1) d = std::make_shared<std::vector<Px>>(*d); }
};

enum class Op { Over, Copy, In, Out }; // In / Out: keep / remove destination where the source is opaque
enum class Blend { Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn, HardLight, SoftLight, Difference, Exclusion };
struct BlendInfo { Blend mode; const char *css; const char *label; };
const std::vector<BlendInfo> &blendModes();

// Draws src (or its sub-rect) with its top-left at (dx, dy). Only the covered area of dst changes.
void draw(Image &dst, const Image &src, int dx, int dy, Op op = Op::Over, double alpha = 1, Blend blend = Blend::Normal, const IRect *srcRect = nullptr);
void clearRect(Image &im, IRect r);
// Source rect s of src drawn into rect d of dst, rotated by angle degrees around d's center (bilinear, source-over).
void drawTransformed(Image &dst, const Image &src, RectF s, RectF d, double angle);
Image scaled(const Image &src, int nw, int nh); // area average when shrinking, bilinear when enlarging
Image rot90(const Image &s, bool cw);
Image mirrored(const Image &s, bool horizontal, bool vertical);
IRect alphaBounds(const Image &im);

Alpha maskAlpha(const Image &m);
Image alphaToMask(const Alpha &a, int w, int h, Px rgb = 0xffffff);
void blurAlpha(Alpha &a, int w, int h, int r, int passes = 3);
void morphAlpha(Alpha &a, int w, int h, int r, bool expand);
Alpha floodMask(const Image &img, int x, int y, int tol, bool contiguous);

// antialiased white masks, canvas sized
Image rectMask(int w, int h, IRect r);
Image ellipseMask(int w, int h, RectF r);
Image polygonMask(int w, int h, const std::vector<PtF> &pts);

// brush primitives drawn into a stroke buffer
IRect capsule(Image &buf, PtF a, PtF b, double radius, Px rgb);                           // hard round segment
IRect softDab(Image &buf, PtF c, double size, double hardness, Px rgb, double opacity);   // soft round stamp
void linearGradient(Image &im, PtF a, PtF b, Px rgb0, int a0, Px rgb1, int a1);

struct ColorMat { double m[9]; double off; };
ColorMat colorMatrix(double hue, double sat, double bri, double con);
ColorMat invertMatrix();
void applyColor(Image &dst, const Image &src, const ColorMat &cm, const Image *sel);
