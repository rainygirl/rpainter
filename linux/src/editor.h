// Document model and pixel operations (no widgets).
#pragma once
#include <QColor>
#include <QImage>
#include <QLine>
#include <QPainter>
#include <QString>
#include <QVector>
#include <functional>

#include "i18n.h"
// UI text: Korean source string -> current language
#define K(s) QString::fromUtf8(trText(s))

typedef QVector<uchar> Alpha;

const int MAX_DIM = 10000;
const int PAD = 4096; // pixels further than this outside the canvas are dropped

// A layer keeps pixels that fall outside the canvas: img is the canvas-sized visible window,
// ext holds only the off-canvas pixels (positioned at extPos in doc coords).
struct Layer {
    int id = 0;
    QString name;
    QImage img, ext;
    QPoint extPos;
    double opacity = 1;
    bool visible = true;
    QPainter::CompositionMode blend = QPainter::CompositionMode_SourceOver;
};
// QImage is copy-on-write, so history snapshots are plain copies of this struct.
struct DocState {
    int w = 0, h = 0, activeId = 0;
    QVector<Layer> layers; // index 0 = bottom
    QImage sel;            // selection mask (white, alpha = coverage); null = no selection
    QVector<QLine> selEdges;
};
struct Full { QImage img; QPoint pos; };          // whole layer incl. off-canvas pixels
struct Lifted { QImage base, flt; QPoint pos; };  // selected pixels (flt) and the rest (base)
struct ColorMat { double m[9]; double off; };
enum class SelMode { Replace, Add, Sub, Intersect };
enum class DocXf { RotCW, RotCCW, Rot180, FlipH, FlipV };

struct BlendInfo { QPainter::CompositionMode mode; const char *css; const char *label; };
const QVector<BlendInfo> &blendModes();

QImage blankImage(int w, int h);
QImage toArgb(const QImage &im);
QRect alphaBounds(const QImage &im);
Alpha maskAlpha(const QImage &m);
QImage alphaToMask(const Alpha &a, int w, int h, QColor c = Qt::white);
void blurAlpha(Alpha &a, int w, int h, int r, int passes = 3);
void morphAlpha(Alpha &a, int w, int h, int r, bool expand);
Alpha floodMask(const QImage &img, int x, int y, int tol, bool contiguous);
ColorMat colorMatrix(double hue, double sat, double bri, double con);
ColorMat invertMatrix();
void applyColor(QImage &dst, const QImage &src, const ColorMat &cm, const QImage *sel);

class Editor {
public:
    DocState d;
    QString name = "r-painter";
    QColor fg = Qt::black, bg = Qt::white;
    QVector<DocState> hist;
    int histIdx = -1;
    bool modified = false;
    QImage clip, lastSel;
    QPoint clipPos;

    std::function<void()> onChange;          // layers / history changed
    std::function<void(QRect)> onDamage;     // pixels changed (null rect = everything)
    std::function<void()> onView;            // selection outline changed
    std::function<void(QString)> onToast;

    QRect docRect() const { return QRect(0, 0, d.w, d.h); }
    Layer *active();
    int activeIdx() const;
    Layer *editable();
    Layer makeLayer(const QString &name, const QImage &img = QImage());
    void addLayer(const Layer &l);
    void newDoc(int w, int h, QColor fill);
    void resetDoc(int w, int h, const QVector<Layer> &layers);
    void commit();
    void undo();
    void redo();
    void damage(QRect r = QRect()) { if (onDamage) onDamage(r); }
    void toast(const QString &s) { if (onToast) onToast(s); }

    void setSelection(QImage mask);
    void combineSel(const QImage &shape, SelMode mode);
    void finishSel(QImage mask, SelMode mode, int feather);
    QImage fullMask() const;
    void selectAll();
    void selectNone();
    void reselect();
    void invertSel();
    void selectLayerPixels();
    void modifySel(const std::function<void(Alpha &)> &fn);

    void composite(QImage &out, QRect clip) const;
    QImage flatten() const;
    void paintMasked(Layer &l, QImage src, double alpha = 1);
    Full fullOf(const Layer &l) const;
    void setFull(Layer &l, const Full &f);
    Lifted lift(const Layer &l) const;
    void drawParts(QImage &target, const Lifted &L, QRectF s, QRectF dst, QPoint origin, double angle = 0) const;
    void commitParts(Layer &l, const Lifted &L, QRect s, QRectF dst, double angle = 0);
    void moveBy(Layer &l, const Lifted &L, int dx, int dy);

    void deleteSel();
    void fill(QColor c);
    void quickColor(const ColorMat &cm);
    void colorExt(Layer &l, const ColorMat &cm);
    bool copySel(bool cut, bool merged = false);
    void pasteImage(const QImage &im, QPoint pos);
    void placeImage(const QImage &im, const QString &name);
    void newLayer();
    void layerViaCopy(bool cut);
    void duplicateLayer();
    void deleteLayer();
    void moveLayer(int dir);
    void mergeDown();
    void flattenAll();
    void layerFlip(DocXf x);
    void resizeImage(int w, int h);
    void resizeCanvas(int w, int h, int anchor);
    void cropToSel();
    void cropRect(QRect r); // r may extend past the canvas; off-canvas pixels are kept
    void transformDoc(DocXf x);

    bool openFile(const QString &path, bool asLayer);
    bool saveProject(const QString &path);
    bool loadProject(const QString &path);
    bool exportImage(const QString &path, const QByteArray &fmt, int quality);

private:
    int idSeq = 0;
    void mapAll(int nw, int nh, const std::function<Full(const Full &)> &f);
    Full xfFull(const Full &f, DocXf x) const;
};
