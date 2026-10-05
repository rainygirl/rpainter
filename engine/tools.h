// Mouse tools, free transform, crop and polygonal lasso.
// Toolkit-free: the view forwards pointer events in document coordinates and draws the overlays.
#pragma once
#include "doc.h"

enum class Tool { Move, Rect, Ellipse, Lasso, PolyLasso, Wand, Crop, Brush, Eraser, Bucket, Gradient, Picker, Hand, Zoom };
const int TOOL_COUNT = 14;

struct ToolOpts {
    int size = 30, hardness = 100, opacity = 100, tolerance = 32, feather = 0;
    int selMode = 0; // SelMode used when no modifier key is held (Shift: add, Alt: subtract, both: intersect)
    bool contiguous = true, sampleAll = false, antiAlias = true, toTransparent = false;
};
struct Mods { bool shift = false, alt = false; };
// One mouse drag; positions are in document coordinates.
struct Drag {
    std::function<void(PtF, Mods)> move;
    std::function<void()> flush;
    std::function<void(PtF)> up;
    std::vector<PtF> path; // selection preview outline (closed)
    PtF line0, line1;      // gradient preview
    bool hasLine = false, hideSel = false;
};
struct XfState {
    bool on = false;
    Lifted L;
    IRect src, orig;   // src in float coords; orig in doc coords
    RectF dst;        // target box in doc coords, before rotation
    double angle = 0; // degrees, around dst's center
};
enum class CursorKind { Cross, Move, Hand, ResizeH, ResizeV, ResizeFDiag, ResizeBDiag, Rotate };

class Tools {
public:
    explicit Tools(Editor *e) : ed(e) {}
    Editor *ed;
    Tool tool = Tool::Brush;
    ToolOpts opt;
    double zoom = 1; // view zoom, for handle hit tests in screen pixels
    XfState xf;
    std::vector<PtF> poly;
    bool polyOn = false;
    RectF crop;
    std::unique_ptr<Drag> drag;
    PtF mouseDoc;

    std::function<void(IRect)> onDamage;        // layer pixels changed during a drag (empty = everything)
    std::function<void()> onMode, onColors, onOverlay;
    std::function<void(PtF, double)> onZoom;   // zoom tool: doc point, factor
    std::function<void()> onFit;

    void setTool(Tool t);
    void down(PtF p, Mods m);
    void move(PtF p, Mods m);
    void up(PtF p);
    void doubleClick();
    bool enter();  // returns true if the key was used
    bool escape();
    void nudge(int dx, int dy);
    void startTransform();
    void commitXf();
    void cancelXf();
    void finishPoly();
    void cancelPoly();
    void applyCrop();
    void resetCrop() { crop = RectF(ed->docRect()); }
    CursorKind cursor() const;
    SelMode selModeFor(Mods m) const;

private:
    SelMode polyMode = SelMode::Replace;
    void damage(IRect r = IRect()) { if (onDamage) onDamage(r); }
    void pick(PtF p);
    void strokeDown(PtF p, Mods m, bool erase);
    void shapeDown(PtF p, Mods m, bool rect);
    void lassoDown(PtF p, Mods m);
    void polyDown(PtF p, Mods m);
    void floodDown(PtF p, Mods m, bool wand);
    void moveDown(PtF p);
    void gradDown(PtF p);
    void cropDown(PtF p);
    void xfDown(PtF p);
    void drawXf();
    int xfHit(PtF p, int &hx, int &hy) const;
};
