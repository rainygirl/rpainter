// Document view: zoom/pan, selection outline, and all mouse tools.
#pragma once
#include "editor.h"
#include <QPainterPath>
#include <QTimer>
#include <QWidget>
#include <memory>

enum class Tool { Move, Rect, Ellipse, Lasso, PolyLasso, Wand, Crop, Brush, Eraser, Bucket, Gradient, Picker, Hand, Zoom };

struct ToolOpts {
    int size = 30, hardness = 100, opacity = 100, tolerance = 32, feather = 0;
    int selMode = 0; // SelMode used when no modifier key is held (Shift: add, Alt: subtract, both: intersect)
    bool contiguous = true, sampleAll = false, antiAlias = true, toTransparent = false;
};
// One mouse drag; positions are in document coordinates.
struct Drag {
    std::function<void(QPointF, Qt::KeyboardModifiers)> move;
    std::function<void()> flush;
    std::function<void(QPointF)> up;
    QPainterPath path; // selection preview
    QLineF line;       // gradient preview
    bool hasLine = false, hideSel = false;
};
struct XfState {
    bool on = false;
    Lifted L;
    QRect src, orig; // src in float coords; orig in doc coords
    QRectF dst;      // target box in doc coords, before rotation
    double angle = 0; // degrees, around dst's center
};

class Canvas : public QWidget {
public:
    explicit Canvas(Editor *ed, QWidget *parent = nullptr);

    Editor *ed;
    Tool tool = Tool::Brush;
    ToolOpts opt;
    double zoom = 1;
    QPointF off; // widget position of the document's top-left
    XfState xf;
    QPointF mouseDoc;
    std::function<void()> onStatus, onMode, onColors;

    bool dragging() const { return drag || panning; }
    void setTool(Tool t);
    void fit(double maxZoom = 1);
    void zoomAt(QPointF widgetPos, double factor);
    void zoomCenter(double factor) { zoomAt(QPointF(width() / 2.0, height() / 2.0), factor); }
    void startTransform();
    void commitXf();
    void cancelXf();
    void nudge(int dx, int dy);
    // polygonal lasso: click adds a corner; Enter, double-click or clicking the first corner closes it
    QPolygonF poly;
    bool polyOn = false;
    void finishPoly();
    void cancelPoly();
    // crop tool: box in doc coords, may extend past the canvas; Enter / double-click applies
    QRectF crop;
    void applyCrop();
    void damage(QRect r);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void leaveEvent(QEvent *) override;
    bool event(QEvent *) override;

private:
    QImage comp; // cached composite of all layers
    QRect dirty;
    bool allDirty = true, panning = false, space = false, hasMouse = false;
    QPointF panStart, panOff, mouseW;
    std::unique_ptr<Drag> drag;
    int antPhase = 0;
    QTimer antTimer;

    QPointF toDoc(QPointF w) const { return (w - off) / zoom; }
    void ensureComp();
    QPen antPen() const;
    void pick(QPointF p);
    void toolDown(QPointF p, QPointF w, Qt::KeyboardModifiers mods);
    void strokeDown(QPointF p, Qt::KeyboardModifiers mods, bool erase);
    void shapeDown(QPointF p, Qt::KeyboardModifiers mods, bool rect);
    void lassoDown(QPointF p, Qt::KeyboardModifiers mods);
    void polyDown(QPointF p, Qt::KeyboardModifiers mods);
    void cropDown(QPointF p, Qt::KeyboardModifiers mods);
    SelMode selModeFor(Qt::KeyboardModifiers mods) const;
    SelMode polyMode = SelMode::Replace;
    void floodDown(QPointF p, Qt::KeyboardModifiers mods, bool wand);
    void moveDown(QPointF p);
    void gradDown(QPointF p);
    void xfDown(QPointF p);
    int xfHit(QPointF p, int &hx, int &hy) const; // 0 = outside (rotate), 1 = inside (move), 2 = handle (resize)
    void updateCursor();
    void drawXf();
    void clickDeselect(SelMode mode);
};
