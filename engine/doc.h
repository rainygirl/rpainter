// Document model and operations (no UI toolkit).
#pragma once
#include "raster.h"
#include <functional>
#include <string>

const int MAX_DIM = 10000;
const int PAD = 4096; // pixels further than this outside the canvas are dropped

struct Line { int x1, y1, x2, y2; };

// A layer keeps pixels that fall outside the canvas: img is the canvas-sized visible window,
// ext holds only the off-canvas pixels (positioned at extPos in doc coords).
struct Layer {
    int id = 0;
    std::string name;
    Image img, ext;
    Pt extPos;
    double opacity = 1;
    bool visible = true;
    Blend blend = Blend::Normal;
};
// Images are copy-on-write, so history snapshots are plain copies of this struct.
struct DocState {
    int w = 0, h = 0, activeId = 0;
    std::vector<Layer> layers; // index 0 = bottom
    Image sel;                 // selection mask (white, alpha = coverage); null = no selection
    std::shared_ptr<const std::vector<Line>> selEdges;
};
struct Full { Image img; Pt pos; };          // whole layer incl. off-canvas pixels
struct Lifted { Image base, flt; Pt pos; };  // selected pixels (flt) and the rest (base)
enum class SelMode { Replace, Add, Sub, Intersect };
enum class DocXf { RotCW, RotCCW, Rot180, FlipH, FlipV };

typedef std::function<std::string(const Image &)> PngEncoder;
typedef std::function<Image(const std::string &)> PngDecoder;

class Editor {
public:
    DocState d;
    std::string name = "r-painter";
    Px fg = 0x000000, bg = 0xffffff; // 0xRRGGBB
    std::vector<DocState> hist;
    int histIdx = -1;
    bool modified = false;
    Image clip, lastSel;
    Pt clipPos;

    std::function<void()> onChange;              // layers / history changed
    std::function<void(IRect)> onDamage;          // pixels changed (empty rect = everything)
    std::function<void()> onView;                // selection outline changed
    std::function<void(const std::string &)> onToast;

    IRect docRect() const { return {0, 0, d.w, d.h}; }
    Layer *active();
    int activeIdx() const;
    Layer *editable();
    Layer makeLayer(const std::string &name, const Image &img = Image());
    void addLayer(const Layer &l);
    void newDoc(int w, int h, bool fill, Px rgb = 0xffffff);
    void resetDoc(int w, int h, const std::vector<Layer> &layers);
    void commit();
    void undo();
    void redo();
    void damage(IRect r = IRect()) { if (onDamage) onDamage(r); }
    void toast(const std::string &s) { if (onToast) onToast(s); }
    bool hasSel() const { return !d.sel.null(); }

    void setSelection(Image mask);
    void combineSel(const Image &shape, SelMode mode);
    void finishSel(Image mask, SelMode mode, int feather);
    void selectAll();
    void selectNone();
    void reselect();
    void invertSel();
    void selectLayerPixels();
    void modifySel(const std::function<void(Alpha &)> &fn);

    void composite(Image &out, IRect clip) const;
    Image flatten() const;
    void paintMasked(Layer &l, Image src, double alpha = 1);
    Full fullOf(const Layer &l) const;
    void setFull(Layer &l, const Full &f);
    Lifted lift(const Layer &l) const;
    void drawParts(Image &target, const Lifted &L, RectF s, RectF dst, Pt origin, double angle = 0) const;
    void commitParts(Layer &l, const Lifted &L, IRect s, RectF dst, double angle = 0);
    void moveBy(Layer &l, const Lifted &L, int dx, int dy);

    void deleteSel();
    void fill(Px rgb);
    void quickColor(const ColorMat &cm);
    void colorExt(Layer &l, const ColorMat &cm);
    bool copySel(bool cut, bool merged = false);
    void pasteImage(const Image &im, Pt pos);
    void placeImage(const Image &im, const std::string &name);
    void openImage(const Image &im, const std::string &name);
    void newLayer();
    void layerViaCopy(bool cut);
    void duplicateLayer();
    void deleteLayer();
    void moveLayer(int dir);
    void moveLayerTo(int from, int to); // indices into d.layers (0 = bottom)
    void mergeDown();
    void flattenAll();
    void layerFlip(DocXf x);
    void resizeImage(int w, int h);
    void resizeCanvas(int w, int h, int anchor);
    void cropToSel();
    void cropRect(IRect r); // r may extend past the canvas; off-canvas pixels are kept
    void transformDoc(DocXf x);

    // Same JSON format as the web and Qt versions, so .rpaint files are interchangeable.
    std::string projectJson(const PngEncoder &encode) const;
    bool loadProjectJson(const std::string &json, const PngDecoder &decode);

private:
    int idSeq = 0;
    void mapAll(int nw, int nh, const std::function<Full(const Full &)> &f);
    Full xfFull(const Full &f, DocXf x) const;
};

std::string base64Encode(const std::string &bytes);
std::string base64Decode(const std::string &text);
