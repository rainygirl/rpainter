// Drives the real window and tools with synthetic mouse events and checks pixels.
#include "colorpicker.h"
#include "mainwindow.h"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QImageWriter>
#include <QLineEdit>
#include <QSpinBox>
#include <QMouseEvent>
#include <QSet>
#include <cmath>
#include <cstdio>

static int fails = 0;
static void ok(const char *name, bool cond) {
    printf("%s %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) fails++;
}
static QString px(const Layer *l, int x, int y) {
    const QColor c = l->img.pixelColor(x, y);
    return QString("%1,%2,%3,%4").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}
static int alphaAt(const Layer *l, int x, int y) { return l->img.pixelColor(x, y).alpha(); }
static void mouse(QWidget *c, QEvent::Type t, QPointF p, Qt::KeyboardModifiers m) {
    QMouseEvent e(t, p, QPointF(c->mapToGlobal(p.toPoint())), Qt::LeftButton, t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, m);
    QApplication::sendEvent(c, &e);
}
static void dragTool(MainWindow &w, Tool t, QPointF a, QPointF b, Qt::KeyboardModifiers m = Qt::NoModifier) {
    w.setTool(t);
    mouse(w.canvas, QEvent::MouseButtonPress, a, m);
    mouse(w.canvas, QEvent::MouseMove, b, m);
    mouse(w.canvas, QEvent::MouseButtonRelease, b, m);
}
static void tap(Canvas *c, QPointF a) {
    mouse(c, QEvent::MouseButtonPress, a, Qt::NoModifier);
    mouse(c, QEvent::MouseButtonRelease, a, Qt::NoModifier);
}
static void click(MainWindow &w, Tool t, QPointF a, Qt::KeyboardModifiers m = Qt::NoModifier) {
    w.setTool(t);
    mouse(w.canvas, QEvent::MouseButtonPress, a, m);
    mouse(w.canvas, QEvent::MouseButtonRelease, a, m);
}

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); // results show up immediately, even when piped
    QApplication app(argc, argv);
    applyTheme(app);
    MainWindow w;
    w.show();
    QApplication::processEvents();
    Editor &ed = w.ed;
    Canvas *cv = w.canvas;
    cv->zoom = 1; cv->off = QPointF(0, 0); // widget coords == document coords

    dragTool(w, Tool::Brush, {100, 100}, {300, 100});
    ok("brush", px(ed.active(), 200, 100) == "0,0,0,255" && ed.hist.size() == 2);
    cv->opt.hardness = 50;
    dragTool(w, Tool::Brush, {100, 300}, {300, 300});
    cv->opt.hardness = 100;
    ok("soft brush", ed.active()->img.pixelColor(200, 300).red() < 10 && ed.active()->img.pixelColor(200, 314).red() > 60);
    ed.newLayer(); ed.fill(Qt::red);
    ed.active()->opacity = 0.5;
    { const QColor c = ed.flatten().pixelColor(10, 10); ok("layer opacity composite", c.red() == 255 && qAbs(c.green() - 128) < 3); }
    ed.active()->blend = QPainter::CompositionMode_Multiply; ed.active()->opacity = 1;
    { const QColor c = ed.flatten().pixelColor(200, 100); ok("multiply blend", c.red() == 0 && c.green() == 0); }
    ed.active()->blend = QPainter::CompositionMode_SourceOver;
    dragTool(w, Tool::Rect, {50, 50}, {150, 150});
    ok("rect selection", !ed.d.sel.isNull() && !ed.d.selEdges.isEmpty());
    ed.fill(Qt::blue);
    ok("fill only in selection", px(ed.active(), 100, 100) == "0,0,255,255" && px(ed.active(), 10, 10) == "255,0,0,255");
    dragTool(w, Tool::Brush, {20, 100}, {200, 100});
    ok("brush clipped to selection", px(ed.active(), 100, 100) == "0,0,0,255" && px(ed.active(), 30, 100) == "255,0,0,255");
    ed.undo();
    ok("undo brush", px(ed.active(), 100, 100) == "0,0,255,255");
    dragTool(w, Tool::Eraser, {60, 60}, {70, 60});
    ok("eraser clipped", alphaAt(ed.active(), 65, 60) == 0 && alphaAt(ed.active(), 20, 60) == 255);
    ed.undo();
    ed.selectNone();
    click(w, Tool::Wand, {10, 10});
    ok("wand selects bg not square", qAlpha(ed.d.sel.pixel(10, 10)) == 255 && qAlpha(ed.d.sel.pixel(100, 100)) == 0);
    ed.deleteSel();
    ok("cutout (delete bg)", alphaAt(ed.active(), 10, 10) == 0 && px(ed.active(), 100, 100) == "0,0,255,255");
    ed.invertSel();
    ok("invert sel", qAlpha(ed.d.sel.pixel(100, 100)) == 255 && qAlpha(ed.d.sel.pixel(10, 10)) == 0);
    ed.layerViaCopy(false);
    ok("layer via copy", ed.d.layers.size() == 3 && alphaAt(ed.active(), 100, 100) == 255 && alphaAt(ed.active(), 10, 10) == 0);
    const ColorMat id = colorMatrix(0, 0, 0, 0);
    ok("identity matrix", qAbs(id.m[0] - 1) < 1e-3 && qAbs(id.m[1]) < 1e-3 && qAbs(id.off) < 1e-6);
    ed.quickColor(colorMatrix(120, 0, 0, 0));
    { const QColor c = ed.active()->img.pixelColor(100, 100); ok("hue shift", c.red() > 100 && c.blue() < 100); }
    const int b0 = ed.active()->img.pixelColor(100, 100).blue();
    ed.quickColor(colorMatrix(0, 0, 50, 0));
    ok("brightness", ed.active()->img.pixelColor(100, 100).blue() > b0);
    ed.quickColor(colorMatrix(0, 0, 0, 40));
    ed.quickColor(invertMatrix()); ed.quickColor(colorMatrix(0, -100, 0, 0));
    { const QColor c = ed.active()->img.pixelColor(100, 100); ok("grayscale", qAbs(c.red() - c.green()) < 3 && qAbs(c.green() - c.blue()) < 3); }
    ed.selectNone();
    cv->startTransform();
    ok("transform start", cv->xf.on && cv->xf.src.width() >= 100 && cv->xf.src.width() <= 102);
    mouse(cv, QEvent::MouseButtonPress, {150, 150}, Qt::NoModifier);
    mouse(cv, QEvent::MouseMove, {250, 250}, Qt::NoModifier);
    mouse(cv, QEvent::MouseButtonRelease, {250, 250}, Qt::NoModifier);
    cv->commitXf();
    ok("transform scaled x2", alphaAt(ed.active(), 240, 240) == 255 && alphaAt(ed.active(), 260, 260) == 0 && !cv->xf.on);
    dragTool(w, Tool::Move, {10, 10}, {60, 30});
    ok("move", alphaAt(ed.active(), 250, 200) == 255 && alphaAt(ed.active(), 55, 55) == 0);
    dragTool(w, Tool::Ellipse, {300, 300}, {500, 500});
    dragTool(w, Tool::Lasso, {600, 100}, {700, 200}, Qt::ShiftModifier);
    ed.selectNone();
    dragTool(w, Tool::Ellipse, {300, 300}, {500, 500});
    ed.modifySel([&](Alpha &a) { morphAlpha(a, ed.d.w, ed.d.h, 5, true); });
    ok("expand", qAlpha(ed.d.sel.pixel(297, 400)) == 255);
    ed.modifySel([&](Alpha &a) { morphAlpha(a, ed.d.w, ed.d.h, 10, false); });
    ok("contract", qAlpha(ed.d.sel.pixel(302, 400)) == 0 && qAlpha(ed.d.sel.pixel(400, 400)) == 255);
    ed.modifySel([&](Alpha &a) { blurAlpha(a, ed.d.w, ed.d.h, 5); });
    dragTool(w, Tool::Gradient, {300, 400}, {500, 400});
    { const QColor c = ed.active()->img.pixelColor(400, 400); ok("gradient in selection", c.alpha() == 255 && c.red() > 90 && c.red() < 170 && alphaAt(ed.active(), 600, 400) == 0); }
    ed.selectNone();
    click(w, Tool::Bucket, {1000, 700});
    ok("bucket", px(ed.active(), 1000, 700) == "0,0,0,255");
    click(w, Tool::Picker, {1000, 700});
    ok("picker", ed.fg == QColor(0, 0, 0));
    dragTool(w, Tool::Rect, {900, 600}, {1000, 700});
    ok("copy", ed.copySel(false) && ed.clip.size() == QSize(100, 100));
    ed.pasteImage(ed.clip, ed.clipPos);
    ok("paste", ed.d.layers.size() == 4);
    ed.mergeDown();
    ok("merge down", ed.d.layers.size() == 3);
    ed.cropToSel();
    ok("crop", ed.d.w == 100 && ed.d.h == 100);
    ed.undo();
    ok("undo crop", ed.d.w == 1200 && ed.active()->img.width() == 1200);
    ed.selectNone();
    ed.resizeImage(600, 400);
    bool sized = ed.d.w == 600;
    for (const auto &l : ed.d.layers) sized = sized && l.img.size() == QSize(600, 400);
    ok("image resize", sized);
    ed.transformDoc(DocXf::RotCW);
    ok("rotate", ed.d.w == 400 && ed.d.h == 600);
    ed.transformDoc(DocXf::FlipH); ed.layerFlip(DocXf::FlipV);
    ed.undo(); ed.undo(); ed.undo(); ed.undo();
    ok("undo resize", ed.d.w == 1200 && ed.d.h == 800);
    ed.redo();

    // off-canvas preservation
    ed.newDoc(400, 300, Qt::transparent); ed.fill(Qt::green);
    cv->nudge(-150, -100);
    ok("moved off: ext kept", !ed.active()->ext.isNull() && alphaAt(ed.active(), 300, 250) == 0 && px(ed.active(), 100, 100) == "0,255,0,255");
    cv->nudge(150, 100);
    ok("moved back: restored", ed.active()->ext.isNull() && px(ed.active(), 5, 5) == "0,255,0,255" && px(ed.active(), 395, 295) == "0,255,0,255");
    dragTool(w, Tool::Move, {10, 10}, {310, 10});
    dragTool(w, Tool::Move, {10, 10}, {-290, 10});
    ok("drag off and back", px(ed.active(), 5, 5) == "0,255,0,255" && px(ed.active(), 395, 100) == "0,255,0,255" && ed.active()->ext.isNull());
    ed.undo();
    ok("undo restores ext", !ed.active()->ext.isNull() && alphaAt(ed.active(), 50, 100) == 0);
    ed.redo();
    cv->nudge(200, 0);
    ed.resizeCanvas(800, 300, 0);
    ok("canvas grow reveals hidden", px(ed.active(), 590, 100) == "0,255,0,255" && ed.active()->ext.isNull());
    ed.resizeCanvas(400, 300, 0);
    ok("canvas shrink keeps ext", !ed.active()->ext.isNull());
    ed.transformDoc(DocXf::RotCW); ed.transformDoc(DocXf::RotCCW);
    ed.transformDoc(DocXf::FlipH); ed.transformDoc(DocXf::FlipH);
    ed.transformDoc(DocXf::Rot180); ed.transformDoc(DocXf::Rot180);
    cv->nudge(-200, 0);
    ok("rotate/flip round trips keep ext", px(ed.active(), 0, 0) == "0,255,0,255" && px(ed.active(), 399, 299) == "0,255,0,255" && ed.active()->ext.isNull());
    cv->nudge(300, 0);
    cv->startTransform();
    ok("transform bbox covers off-canvas", cv->xf.orig == QRect(300, 0, 400, 300));
    mouse(cv, QEvent::MouseButtonPress, {350, 150}, Qt::NoModifier);
    mouse(cv, QEvent::MouseMove, {50, 150}, Qt::NoModifier);
    mouse(cv, QEvent::MouseButtonRelease, {50, 150}, Qt::NoModifier);
    cv->commitXf();
    ok("transform move back", px(ed.active(), 0, 0) == "0,255,0,255" && px(ed.active(), 399, 299) == "0,255,0,255" && ed.active()->ext.isNull());
    cv->nudge(300, 0); ed.quickColor(invertMatrix()); cv->nudge(-300, 0);
    ok("adjust covers ext", px(ed.active(), 350, 100) == "255,0,255,255" && px(ed.active(), 50, 100) == "255,0,255,255");
    cv->nudge(300, 0); ed.duplicateLayer(); ed.mergeDown();
    const QString proj = QDir::temp().filePath("rpainter-selftest.rpaint");
    ok("save project", ed.saveProject(proj));
    ed.newDoc(50, 50, Qt::white);
    ok("load project", ed.loadProject(proj) && ed.d.w == 400 && ed.d.layers.size() == 1 && !ed.active()->ext.isNull());
    cv->nudge(-300, 0);
    ok("project keeps off-canvas pixels", px(ed.active(), 350, 100) == "255,0,255,255" && px(ed.active(), 5, 100) == "255,0,255,255");
    ed.resizeImage(200, 150); cv->nudge(1, 0); cv->nudge(-1, 0);
    ok("resize after", ed.d.w == 200 && alphaAt(ed.active(), 100, 75) == 255);

    // shortcuts: every key sequence must be unique, and the Photoshop ones must exist
    {
        QSet<QString> seen;
        bool unique = true;
        for (QAction *a : w.findChildren<QAction *>())
            for (const QKeySequence &k : a->shortcuts()) {
                const QString s = k.toString();
                if (seen.contains(s)) { unique = false; printf("duplicate shortcut %s\n", qPrintable(s)); }
                seen.insert(s);
            }
        ok("shortcuts unique", unique);
        bool all = true;
        for (const char *k : {"Ctrl+T", "Ctrl+Z", "Ctrl+Shift+Z", "Ctrl+J", "Ctrl+E", "Ctrl+Shift+E", "Ctrl+U", "Ctrl+I", "Ctrl+Shift+U", "Ctrl+Alt+I", "Ctrl+Alt+C",
                              "Ctrl+A", "Ctrl+D", "Ctrl+Shift+D", "Ctrl+Shift+I", "Shift+F6", "Ctrl+S", "Ctrl+Alt+Shift+S", "Ctrl+0", "Ctrl+1", "Alt+Backspace", "Ctrl+Backspace",
                              "V", "M", "L", "W", "B", "E", "G", "I", "H", "Z", "X", "D", "[", "]", "5", "Alt+[", "Ctrl+Shift+N", "Ctrl+Shift+C"})
            if (!seen.contains(QKeySequence(k).toString())) { all = false; printf("missing shortcut %s\n", k); }
        ok("photoshop shortcuts present", all);
    }
    // free transform: Alt scales around the center, Shift frees the aspect ratio
    ed.newDoc(400, 300, Qt::transparent);
    dragTool(w, Tool::Rect, {100, 100}, {200, 200}); ed.fill(Qt::red); ed.selectNone();
    cv->startTransform();
    mouse(cv, QEvent::MouseButtonPress, {200, 200}, Qt::AltModifier);
    mouse(cv, QEvent::MouseMove, {220, 220}, Qt::AltModifier);
    mouse(cv, QEvent::MouseButtonRelease, {220, 220}, Qt::AltModifier);
    ok("transform alt = from center", cv->xf.dst == QRect(80, 80, 140, 140));
    mouse(cv, QEvent::MouseButtonPress, {220, 220}, Qt::ShiftModifier);
    mouse(cv, QEvent::MouseMove, {300, 230}, Qt::ShiftModifier);
    mouse(cv, QEvent::MouseButtonRelease, {300, 230}, Qt::ShiftModifier);
    ok("transform shift = free aspect", cv->xf.dst == QRect(80, 80, 220, 150));
    mouse(cv, QEvent::MouseButtonPress, {300, 155}, Qt::NoModifier);
    mouse(cv, QEvent::MouseMove, {350, 155}, Qt::NoModifier);
    mouse(cv, QEvent::MouseButtonRelease, {350, 155}, Qt::NoModifier);
    ok("transform edge = one axis", cv->xf.dst == QRect(80, 80, 270, 150));
    cv->commitXf();
    ok("transform result", px(ed.active(), 340, 220) == "255,0,0,255" && alphaAt(ed.active(), 360, 150) == 0 && alphaAt(ed.active(), 70, 150) == 0);
    // free transform rotation: drag outside the box, Shift snaps to 15 degrees
    ed.newDoc(400, 300, Qt::transparent);
    dragTool(w, Tool::Rect, {100, 100}, {300, 150}); ed.fill(Qt::red); ed.selectNone();
    cv->startTransform();
    mouse(cv, QEvent::MouseButtonPress, {350, 125}, Qt::ShiftModifier);
    mouse(cv, QEvent::MouseMove, {205, 275}, Qt::ShiftModifier);
    ok("rotate snaps to 90", cv->xf.angle == 90);
    mouse(cv, QEvent::MouseButtonRelease, {205, 275}, Qt::ShiftModifier);
    cv->commitXf();
    ok("rotated 90: tall instead of wide", px(ed.active(), 200, 40) == "255,0,0,255" && px(ed.active(), 200, 210) == "255,0,0,255" && alphaAt(ed.active(), 120, 125) == 0 && alphaAt(ed.active(), 280, 125) == 0);
    ed.undo();
    ok("undo rotation", px(ed.active(), 120, 125) == "255,0,0,255" && alphaAt(ed.active(), 200, 40) == 0);
    cv->startTransform();
    mouse(cv, QEvent::MouseButtonPress, {350, 125}, Qt::NoModifier);
    mouse(cv, QEvent::MouseMove, {350, 275}, Qt::NoModifier); // 45 degrees around (200, 125)
    mouse(cv, QEvent::MouseButtonRelease, {350, 275}, Qt::NoModifier);
    ok("rotate free angle", qAbs(cv->xf.angle - 45) < 0.01);
    // cursors follow the rotated box (corner handle of the 200x50 box at 45 degrees)
    {
        const QPointF c(200, 125), ux(std::sqrt(0.5), std::sqrt(0.5)), uy(-std::sqrt(0.5), std::sqrt(0.5));
        mouse(cv, QEvent::MouseMove, c + ux * 100, Qt::NoModifier);
        ok("cursor: rotated right handle = diagonal", cv->cursor().shape() == Qt::SizeFDiagCursor);
        mouse(cv, QEvent::MouseMove, c + uy * 25, Qt::NoModifier);
        ok("cursor: rotated bottom handle = other diagonal", cv->cursor().shape() == Qt::SizeBDiagCursor);
        mouse(cv, QEvent::MouseMove, c + ux * 100 + uy * 25, Qt::NoModifier);
        ok("cursor: rotated corner handle = vertical", cv->cursor().shape() == Qt::SizeVerCursor);
        mouse(cv, QEvent::MouseMove, c + ux * 40, Qt::NoModifier);
        ok("cursor: inside = move", cv->cursor().shape() == Qt::SizeAllCursor);
        mouse(cv, QEvent::MouseMove, {200, 30}, Qt::NoModifier);
        ok("cursor: outside = rotate", cv->cursor().shape() == Qt::BitmapCursor);
    }
    // resize the rotated box from its right-middle handle (at 45 degrees from the center), along its own axis
    const double k = 100 / std::sqrt(2.0);
    mouse(cv, QEvent::MouseButtonPress, {200 + k, 125 + k}, Qt::NoModifier);
    mouse(cv, QEvent::MouseMove, {200 + 2 * k, 125 + 2 * k}, Qt::NoModifier);
    mouse(cv, QEvent::MouseButtonRelease, {200 + 2 * k, 125 + 2 * k}, Qt::NoModifier);
    ok("rotated resize keeps far edge", qAbs(cv->xf.dst.width() - 300) <= 1 && qAbs(cv->xf.dst.height() - 50) <= 1
        && QLineF(cv->xf.dst.center(), QPointF(200 + k / 2, 125 + k / 2)).length() < 1.5);
    cv->cancelXf();
    ok("cursor: back to tool cursor after transform", cv->cursor().shape() != Qt::BitmapCursor);
    cv->startTransform();
    mouse(cv, QEvent::MouseMove, {300, 150}, Qt::NoModifier);
    ok("cursor: unrotated corner = diagonal", cv->cursor().shape() == Qt::SizeFDiagCursor);
    mouse(cv, QEvent::MouseMove, {300, 125}, Qt::NoModifier);
    ok("cursor: unrotated right edge = horizontal", cv->cursor().shape() == Qt::SizeHorCursor);
    mouse(cv, QEvent::MouseMove, {200, 100}, Qt::NoModifier);
    ok("cursor: unrotated top edge = vertical", cv->cursor().shape() == Qt::SizeVerCursor);
    cv->cancelXf();
    ok("cancel restores", px(ed.active(), 120, 125) == "255,0,0,255" && alphaAt(ed.active(), 200, 40) == 0 && cv->xf.angle == 0);
    dragTool(w, Tool::Rect, {100, 100}, {300, 150});
    cv->startTransform();
    mouse(cv, QEvent::MouseButtonPress, {350, 125}, Qt::ShiftModifier);
    mouse(cv, QEvent::MouseMove, {200, 275}, Qt::ShiftModifier);
    mouse(cv, QEvent::MouseButtonRelease, {200, 275}, Qt::ShiftModifier);
    cv->commitXf();
    ok("selection rotates with pixels", qAlpha(ed.d.sel.pixel(200, 40)) == 255 && qAlpha(ed.d.sel.pixel(120, 125)) == 0 && px(ed.active(), 200, 40) == "255,0,0,255");
    ed.selectNone();
    ed.selectAll(); ed.selectNone(); ed.reselect();
    ok("reselect", !ed.d.sel.isNull());
    ed.selectNone();

    // polygonal lasso + selection modes
    ed.newDoc(400, 300, Qt::transparent); ed.fill(Qt::green);
    w.setTool(Tool::PolyLasso);
    for (QPointF pt : {QPointF(50, 50), QPointF(150, 50), QPointF(150, 150), QPointF(50, 150)}) tap(cv, pt);
    ok("poly lasso pending", cv->polyOn && cv->poly.size() == 4 && ed.d.sel.isNull());
    cv->finishPoly();
    ok("poly lasso selects polygon", qAlpha(ed.d.sel.pixel(100, 100)) == 255 && qAlpha(ed.d.sel.pixel(200, 100)) == 0 && !cv->polyOn);
    tap(cv, {300, 200}); tap(cv, {350, 200}); tap(cv, {350, 250});
    tap(cv, {301, 201}); // back on the first corner closes it
    ok("poly lasso closes on first corner", !cv->polyOn && qAlpha(ed.d.sel.pixel(340, 215)) == 255 && qAlpha(ed.d.sel.pixel(100, 100)) == 0);
    ed.undo();
    cv->opt.selMode = 1;
    dragTool(w, Tool::Rect, {200, 50}, {250, 100});
    ok("mode button: add", qAlpha(ed.d.sel.pixel(100, 100)) == 255 && qAlpha(ed.d.sel.pixel(225, 75)) == 255);
    cv->opt.selMode = 2;
    dragTool(w, Tool::Ellipse, {80, 80}, {120, 120});
    ok("mode button: subtract", qAlpha(ed.d.sel.pixel(100, 100)) == 0 && qAlpha(ed.d.sel.pixel(60, 60)) == 255);
    cv->opt.selMode = 3;
    dragTool(w, Tool::Rect, {40, 40}, {70, 70});
    ok("mode button: intersect", qAlpha(ed.d.sel.pixel(60, 60)) == 255 && qAlpha(ed.d.sel.pixel(225, 75)) == 0 && qAlpha(ed.d.sel.pixel(140, 140)) == 0);
    cv->opt.selMode = 0;
    dragTool(w, Tool::Rect, {60, 60}, {100, 100}, Qt::ShiftModifier);
    dragTool(w, Tool::Rect, {40, 40}, {55, 55}, Qt::AltModifier);
    ok("shift adds, alt subtracts", qAlpha(ed.d.sel.pixel(90, 90)) == 255 && qAlpha(ed.d.sel.pixel(50, 50)) == 0 && qAlpha(ed.d.sel.pixel(65, 65)) == 255);
    ed.selectNone();

    // crop tool
    w.setTool(Tool::Crop);
    ok("crop starts on whole canvas", cv->crop == QRectF(0, 0, 400, 300));
    mouse(cv, QEvent::MouseMove, {400, 300}, Qt::NoModifier);
    ok("crop cursor on corner", cv->cursor().shape() == Qt::SizeFDiagCursor);
    mouse(cv, QEvent::MouseButtonPress, {400, 300}, Qt::NoModifier);
    mouse(cv, QEvent::MouseMove, {300, 200}, Qt::NoModifier);
    mouse(cv, QEvent::MouseButtonRelease, {300, 200}, Qt::NoModifier);
    ok("crop handle resize", cv->crop == QRectF(0, 0, 300, 200));
    mouse(cv, QEvent::MouseButtonPress, {100, 100}, Qt::NoModifier);
    mouse(cv, QEvent::MouseMove, {150, 140}, Qt::NoModifier);
    mouse(cv, QEvent::MouseButtonRelease, {150, 140}, Qt::NoModifier);
    ok("crop move", cv->crop == QRectF(50, 40, 300, 200));
    mouse(cv, QEvent::MouseButtonPress, {350, 240}, Qt::ShiftModifier);
    mouse(cv, QEvent::MouseMove, {200, 200}, Qt::ShiftModifier);
    mouse(cv, QEvent::MouseButtonRelease, {200, 200}, Qt::ShiftModifier);
    ok("crop shift keeps aspect", cv->crop == QRectF(50, 40, 240, 160));
    cv->applyCrop();
    ok("crop applied", ed.d.w == 240 && ed.d.h == 160 && cv->crop == QRectF(0, 0, 240, 160) && !ed.active()->ext.isNull());
    cv->zoom = 1; cv->off = QPointF(0, 0);
    ed.undo();
    ok("crop undo", ed.d.w == 400 && ed.d.h == 300 && px(ed.active(), 395, 295) == "0,255,0,255");
    QApplication::processEvents();
    mouse(cv, QEvent::MouseButtonPress, {-20, -20}, Qt::NoModifier); // outside the box: draw a new one, past the canvas
    mouse(cv, QEvent::MouseMove, {100, 100}, Qt::NoModifier);
    mouse(cv, QEvent::MouseButtonRelease, {100, 100}, Qt::NoModifier);
    cv->applyCrop();
    cv->zoom = 1; cv->off = QPointF(0, 0);
    ok("crop can extend the canvas", ed.d.w == 120 && ed.d.h == 120 && alphaAt(ed.active(), 5, 5) == 0 && px(ed.active(), 30, 30) == "0,255,0,255");

    // built-in color picker
    {
        ColorPicker cp("test", QColor("#336699"), &w);
        cp.show();
        QApplication::processEvents();
        ok("picker shows initial color", cp.color().rgb() == QColor("#336699").rgb() && cp.hex->text() == "#336699" && cp.rgb[0]->value() == 0x33 && cp.hsv[0]->value() == 210);
        mouse(cp.square, QEvent::MouseButtonPress, {255, 0}, Qt::NoModifier);
        ok("picker square: top-right = pure hue", cp.color().rgb() == QColor::fromHsv(210, 255, 255).rgb());
        mouse(cp.strip, QEvent::MouseButtonPress, {10, 85}, Qt::NoModifier); // 85/255 * 360 = 120
        ok("picker hue strip", cp.color().rgb() == QColor(0, 255, 0).rgb() && cp.hex->text() == "#00ff00");
        mouse(cp.square, QEvent::MouseMove, {0, 255}, Qt::NoModifier);
        ok("picker keeps hue at black", cp.color().rgb() == QColor(0, 0, 0).rgb() && cp.hsv[0]->value() == 120);
        cp.rgb[0]->setValue(255);
        ok("picker rgb field", cp.color().rgb() == QColor(255, 0, 0).rgb() && cp.hsv[0]->value() == 0 && cp.hsv[1]->value() == 100);
        cp.hsv[0]->setValue(240);
        ok("picker hsv field", cp.color().rgb() == QColor(0, 0, 255).rgb());
        cp.hex->setText("fb8c00"); emit cp.hex->editingFinished();
        ok("picker hex field", cp.color().rgb() == QColor("#fb8c00").rgb() && cp.rgb[1]->value() == 0x8c);
        if (argc > 1) cp.grab().save(QString(argv[1]) + ".picker.png");
    }

    for (const char *fmt : {"png", "jpg", "webp"}) {
        if (!QImageWriter::supportedImageFormats().contains(fmt)) { printf("SKIP export %s (no Qt image plugin)\n", fmt); continue; }
        const QString path = QDir::temp().filePath(QString("rpainter-selftest.") + fmt);
        const bool saved = ed.exportImage(path, fmt, 90);
        ok(qPrintable(QString("export ") + fmt), saved && QImage(path).size() == QSize(ed.d.w, ed.d.h));
    }

    // demo drawing: also used for the README screenshots (third argument = .rpaint file to write)
    ed.newDoc(1200, 800, Qt::white);
    ed.active()->name = K("배경");
    ed.fg = QColor("#fdd835"); ed.bg = QColor("#fb8c00");
    cv->opt.opacity = 100;
    dragTool(w, Tool::Gradient, {0, 0}, {1200, 800});
    ed.newLayer(); ed.active()->name = K("원");
    dragTool(w, Tool::Ellipse, {520, 160}, {960, 600});
    ed.fill(QColor("#1e88e5"));
    ed.selectNone();
    ed.newLayer(); ed.active()->name = K("붓질");
    ed.fg = QColor("#e53935");
    cv->opt.size = 46; cv->opt.hardness = 100;
    w.setTool(Tool::Brush);
    mouse(cv, QEvent::MouseButtonPress, {170, 560}, Qt::NoModifier);
    for (int i = 1; i <= 60; i++) mouse(cv, QEvent::MouseMove, {170 + i * 9.0, 560 - 190 * std::sin(i / 60.0 * 3.14159 * 1.5) - i * 2.0}, Qt::NoModifier);
    mouse(cv, QEvent::MouseButtonRelease, {710, 440}, Qt::NoModifier);
    ed.newLayer(); ed.active()->name = K("하이라이트"); ed.active()->blend = QPainter::CompositionMode_Screen; ed.active()->opacity = 0.8;
    ed.fg = QColor("#ffffff");
    cv->opt.size = 150; cv->opt.hardness = 0;
    dragTool(w, Tool::Brush, {640, 270}, {700, 250});
    cv->opt.size = 30; cv->opt.hardness = 100;
    ed.commit();
    ed.fg = QColor("#e53935"); ed.bg = QColor("#ffffff");
    if (argc > 2) ok("demo project written", ed.saveProject(argv[2]));
    dragTool(w, Tool::Ellipse, {520, 160}, {960, 600});
    w.refresh();
    cv->fit();
    QApplication::processEvents();
    ok("ui built", w.findChildren<QAction *>().size() > 60);
    if (argc > 1) ok("screenshot", w.grab().save(argv[1]));
    printf("%s (%d failed)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
