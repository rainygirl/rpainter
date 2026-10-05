// Headless test of the shared engine and tools (no UI toolkit needed, runs on any OS).
#include "../i18n.h"
#include "../tools.h"
#include <cstdio>

static int fails = 0;
static void ok(const char *name, bool cond) {
    printf("%s %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) fails++;
}
static std::string px(const Layer *l, int x, int y) {
    const Px p = l->img.pixel(x, y), c = straightRgb(p);
    char b[32];
    snprintf(b, sizeof b, "%d,%d,%d,%d", pxR(c), pxG(c), pxB(c), pxA(p));
    return b;
}
static int alphaAt(const Layer *l, int x, int y) { return pxA(l->img.pixel(x, y)); }
static int selAt(Editor &ed, int x, int y) { return ed.d.sel.null() ? -1 : pxA(ed.d.sel.pixel(x, y)); }
static Tools *T;
static void dragTool(Tool t, PtF a, PtF b, Mods m = Mods()) { T->setTool(t); T->down(a, m); T->move(b, m); T->up(b); }
static void click(Tool t, PtF a, Mods m = Mods()) { T->setTool(t); T->down(a, m); T->up(a); }
static void xfDrag(PtF a, PtF b, Mods m = Mods()) { T->down(a, m); T->move(b, m); T->up(b); }

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Editor ed;
    Tools tools(&ed);
    T = &tools;
    ed.newDoc(1200, 800, true);
    const Mods shift{true, false}, alt{false, true};

    dragTool(Tool::Brush, {100, 100}, {300, 100});
    ok("brush", px(ed.active(), 200, 100) == "0,0,0,255" && ed.hist.size() == 2);
    tools.opt.hardness = 50;
    dragTool(Tool::Brush, {100, 300}, {300, 300});
    tools.opt.hardness = 100;
    ok("soft brush", pxR(ed.active()->img.pixel(200, 300)) < 10 && pxR(ed.active()->img.pixel(200, 314)) > 60);
    ed.newLayer(); ed.fill(0xff0000);
    ed.active()->opacity = 0.5;
    { const Px c = ed.flatten().pixel(10, 10); ok("layer opacity composite", pxR(c) == 255 && std::abs(pxG(c) - 128) < 3); }
    ed.active()->blend = Blend::Multiply; ed.active()->opacity = 1;
    { const Px c = ed.flatten().pixel(200, 100); ok("multiply blend", pxR(c) == 0 && pxG(c) == 0); }
    { const Px c = ed.flatten().pixel(10, 10); ok("multiply on white", pxR(c) == 255 && pxG(c) == 0); }
    ed.active()->blend = Blend::Normal;
    dragTool(Tool::Rect, {50, 50}, {150, 150});
    ok("rect selection", ed.hasSel() && ed.d.selEdges && !ed.d.selEdges->empty());
    ed.fill(0x0000ff);
    ok("fill only in selection", px(ed.active(), 100, 100) == "0,0,255,255" && px(ed.active(), 10, 10) == "255,0,0,255");
    dragTool(Tool::Brush, {20, 100}, {200, 100});
    ok("brush clipped to selection", px(ed.active(), 100, 100) == "0,0,0,255" && px(ed.active(), 30, 100) == "255,0,0,255");
    ed.undo();
    ok("undo brush", px(ed.active(), 100, 100) == "0,0,255,255");
    dragTool(Tool::Eraser, {60, 60}, {70, 60});
    ok("eraser clipped", alphaAt(ed.active(), 65, 60) == 0 && alphaAt(ed.active(), 20, 60) == 255);
    ed.undo();
    ed.selectNone();
    click(Tool::Wand, {10, 10});
    ok("wand selects bg not square", selAt(ed, 10, 10) == 255 && selAt(ed, 100, 100) == 0);
    ed.deleteSel();
    ok("cutout (delete bg)", alphaAt(ed.active(), 10, 10) == 0 && px(ed.active(), 100, 100) == "0,0,255,255");
    ed.invertSel();
    ok("invert sel", selAt(ed, 100, 100) == 255 && selAt(ed, 10, 10) == 0);
    ed.layerViaCopy(false);
    ok("layer via copy", ed.d.layers.size() == 3 && alphaAt(ed.active(), 100, 100) == 255 && alphaAt(ed.active(), 10, 10) == 0);
    const ColorMat id = colorMatrix(0, 0, 0, 0);
    ok("identity matrix", std::fabs(id.m[0] - 1) < 1e-3 && std::fabs(id.m[1]) < 1e-3 && std::fabs(id.off) < 1e-6);
    ed.quickColor(colorMatrix(120, 0, 0, 0));
    { const Px c = straightRgb(ed.active()->img.pixel(100, 100)); ok("hue shift", pxR(c) > 100 && pxB(c) < 100); }
    const int b0 = pxB(ed.active()->img.pixel(100, 100));
    ed.quickColor(colorMatrix(0, 0, 50, 0));
    ok("brightness", pxB(ed.active()->img.pixel(100, 100)) > b0);
    ed.quickColor(invertMatrix()); ed.quickColor(colorMatrix(0, -100, 0, 0));
    { const Px c = ed.active()->img.pixel(100, 100); ok("grayscale", std::abs(pxR(c) - pxG(c)) < 3 && std::abs(pxG(c) - pxB(c)) < 3); }
    ed.selectNone();
    tools.startTransform();
    ok("transform start", tools.xf.on && tools.xf.src.w >= 100 && tools.xf.src.w <= 102);
    xfDrag({150, 150}, {250, 250});
    tools.commitXf();
    ok("transform scaled x2", alphaAt(ed.active(), 240, 240) == 255 && alphaAt(ed.active(), 260, 260) == 0 && !tools.xf.on);
    dragTool(Tool::Move, {10, 10}, {60, 30});
    ok("move", alphaAt(ed.active(), 250, 200) == 255 && alphaAt(ed.active(), 55, 55) == 0);
    dragTool(Tool::Ellipse, {300, 300}, {500, 500});
    ok("ellipse selection", selAt(ed, 400, 400) == 255 && selAt(ed, 305, 305) == 0 && selAt(ed, 302, 400) == 255);
    T->setTool(Tool::Lasso); T->down({600, 100}, shift); T->move({700, 100}, shift); T->move({700, 200}, shift); T->up({700, 200});
    ok("lasso adds triangle", selAt(ed, 680, 120) == 255 && selAt(ed, 620, 180) == 0 && selAt(ed, 400, 400) == 255);
    ed.selectNone();
    dragTool(Tool::Ellipse, {300, 300}, {500, 500});
    ed.modifySel([&](Alpha &a) { morphAlpha(a, ed.d.w, ed.d.h, 5, true); });
    ok("expand", selAt(ed, 297, 400) == 255);
    ed.modifySel([&](Alpha &a) { morphAlpha(a, ed.d.w, ed.d.h, 10, false); });
    ok("contract", selAt(ed, 302, 400) == 0 && selAt(ed, 400, 400) == 255);
    ed.modifySel([&](Alpha &a) { blurAlpha(a, ed.d.w, ed.d.h, 5); });
    dragTool(Tool::Gradient, {300, 400}, {500, 400});
    { const Px p = ed.active()->img.pixel(400, 400); const Px c = straightRgb(p); ok("gradient in selection", pxA(p) == 255 && pxR(c) > 90 && pxR(c) < 170 && alphaAt(ed.active(), 600, 400) == 0); }
    ed.selectNone();
    click(Tool::Bucket, {1000, 700});
    ok("bucket", px(ed.active(), 1000, 700) == "0,0,0,255");
    ed.fg = 0x123456;
    click(Tool::Picker, {1000, 700});
    ok("picker", ed.fg == 0x000000);
    dragTool(Tool::Rect, {900, 600}, {1000, 700});
    ok("copy", ed.copySel(false) && ed.clip.w == 100 && ed.clip.h == 100);
    ed.pasteImage(ed.clip, ed.clipPos);
    ok("paste", ed.d.layers.size() == 4);
    ed.mergeDown();
    ok("merge down", ed.d.layers.size() == 3);
    ed.cropToSel();
    ok("crop to selection", ed.d.w == 100 && ed.d.h == 100);
    ed.undo();
    ok("undo crop", ed.d.w == 1200 && ed.active()->img.w == 1200);
    ed.selectNone();
    ed.resizeImage(600, 400);
    bool sized = ed.d.w == 600;
    for (const auto &l : ed.d.layers) sized = sized && l.img.w == 600 && l.img.h == 400;
    ok("image resize", sized);
    ed.transformDoc(DocXf::RotCW);
    ok("rotate", ed.d.w == 400 && ed.d.h == 600);
    ed.transformDoc(DocXf::FlipH); ed.layerFlip(DocXf::FlipV);
    ed.undo(); ed.undo(); ed.undo(); ed.undo();
    ok("undo resize", ed.d.w == 1200 && ed.d.h == 800);
    ed.redo();

    // off-canvas preservation
    ed.newDoc(400, 300, false); ed.fill(0x00ff00);
    tools.nudge(-150, -100);
    ok("moved off: ext kept", !ed.active()->ext.null() && alphaAt(ed.active(), 300, 250) == 0 && px(ed.active(), 100, 100) == "0,255,0,255");
    tools.nudge(150, 100);
    ok("moved back: restored", ed.active()->ext.null() && px(ed.active(), 5, 5) == "0,255,0,255" && px(ed.active(), 395, 295) == "0,255,0,255");
    dragTool(Tool::Move, {10, 10}, {310, 10});
    dragTool(Tool::Move, {10, 10}, {-290, 10});
    ok("drag off and back", px(ed.active(), 5, 5) == "0,255,0,255" && px(ed.active(), 395, 100) == "0,255,0,255" && ed.active()->ext.null());
    ed.undo();
    ok("undo restores ext", !ed.active()->ext.null() && alphaAt(ed.active(), 50, 100) == 0);
    ed.redo();
    tools.nudge(200, 0);
    ed.resizeCanvas(800, 300, 0);
    ok("canvas grow reveals hidden", px(ed.active(), 590, 100) == "0,255,0,255" && ed.active()->ext.null());
    ed.resizeCanvas(400, 300, 0);
    ok("canvas shrink keeps ext", !ed.active()->ext.null());
    ed.transformDoc(DocXf::RotCW); ed.transformDoc(DocXf::RotCCW);
    ed.transformDoc(DocXf::FlipH); ed.transformDoc(DocXf::FlipH);
    ed.transformDoc(DocXf::Rot180); ed.transformDoc(DocXf::Rot180);
    tools.nudge(-200, 0);
    ok("rotate/flip round trips keep ext", px(ed.active(), 0, 0) == "0,255,0,255" && px(ed.active(), 399, 299) == "0,255,0,255" && ed.active()->ext.null());
    tools.nudge(300, 0);
    tools.setTool(Tool::Move);
    tools.startTransform();
    ok("transform bbox covers off-canvas", tools.xf.orig == IRect{300, 0, 400, 300});
    xfDrag({350, 150}, {50, 150});
    tools.commitXf();
    ok("transform move back", px(ed.active(), 0, 0) == "0,255,0,255" && px(ed.active(), 399, 299) == "0,255,0,255" && ed.active()->ext.null());
    tools.nudge(300, 0); ed.quickColor(invertMatrix()); tools.nudge(-300, 0);
    ok("adjust covers ext", px(ed.active(), 350, 100) == "255,0,255,255" && px(ed.active(), 50, 100) == "255,0,255,255");
    tools.nudge(300, 0); ed.duplicateLayer(); ed.mergeDown();
    // project round trip with a stand-in "PNG" codec (raw pixels); the real codec is the Translation Kit
    const PngEncoder enc = [](const Image &im) {
        std::string s(8 + im.bytes(), '\0');
        memcpy(&s[0], &im.w, 4); memcpy(&s[4], &im.h, 4);
        for (int y = 0; y < im.h; y++) memcpy(&s[8 + size_t(y) * im.w * 4], im.row(y), size_t(im.w) * 4);
        return s;
    };
    const PngDecoder dec = [](const std::string &s) {
        int w, h;
        memcpy(&w, &s[0], 4); memcpy(&h, &s[4], 4);
        Image im(w, h);
        for (int y = 0; y < h; y++) memcpy(im.wrow(y), &s[8 + size_t(y) * w * 4], size_t(w) * 4);
        return im;
    };
    ed.active()->name = "한글 \"레이어\"";
    const std::string json = ed.projectJson(enc);
    ed.newDoc(50, 50, true);
    ok("project round trip", ed.loadProjectJson(json, dec) && ed.d.w == 400 && ed.d.layers.size() == 1 && !ed.active()->ext.null() && ed.active()->name == "한글 \"레이어\"");
    tools.nudge(-300, 0);
    ok("project keeps off-canvas pixels", px(ed.active(), 350, 100) == "255,0,255,255" && px(ed.active(), 5, 100) == "255,0,255,255");
    ok("base64", base64Decode(base64Encode(std::string("\0\1\2hello\xff", 9))) == std::string("\0\1\2hello\xff", 9) && base64Encode("Man") == "TWFu" && base64Encode("Ma") == "TWE=");

    // polygonal lasso + selection modes
    ed.newDoc(400, 300, false); ed.fill(0x00ff00);
    tools.setTool(Tool::PolyLasso);
    for (PtF pt : {PtF{50, 50}, PtF{150, 50}, PtF{150, 150}, PtF{50, 150}}) { tools.down(pt, Mods()); tools.up(pt); }
    ok("poly lasso pending", tools.polyOn && tools.poly.size() == 4 && !ed.hasSel());
    tools.finishPoly();
    ok("poly lasso selects polygon", selAt(ed, 100, 100) == 255 && selAt(ed, 200, 100) == 0 && !tools.polyOn);
    for (PtF pt : {PtF{300, 200}, PtF{350, 200}, PtF{350, 250}, PtF{301, 201}}) { tools.down(pt, Mods()); tools.up(pt); }
    ok("poly lasso closes on first corner", !tools.polyOn && selAt(ed, 340, 215) == 255 && selAt(ed, 100, 100) == 0);
    ed.undo();
    tools.opt.selMode = 1; dragTool(Tool::Rect, {200, 50}, {250, 100});
    ok("mode: add", selAt(ed, 100, 100) == 255 && selAt(ed, 225, 75) == 255);
    tools.opt.selMode = 2; dragTool(Tool::Ellipse, {80, 80}, {120, 120});
    ok("mode: subtract", selAt(ed, 100, 100) == 0 && selAt(ed, 60, 60) == 255);
    tools.opt.selMode = 3; dragTool(Tool::Rect, {40, 40}, {70, 70});
    ok("mode: intersect", selAt(ed, 60, 60) == 255 && selAt(ed, 225, 75) == 0 && selAt(ed, 140, 140) == 0);
    tools.opt.selMode = 0;
    dragTool(Tool::Rect, {60, 60}, {100, 100}, shift); dragTool(Tool::Rect, {40, 40}, {55, 55}, alt);
    ok("shift adds, alt subtracts", selAt(ed, 90, 90) == 255 && selAt(ed, 50, 50) == 0 && selAt(ed, 65, 65) == 255);
    ed.selectNone(); ed.reselect();
    ok("reselect", ed.hasSel());
    ed.selectNone();

    // crop tool
    tools.setTool(Tool::Crop);
    ok("crop starts on whole canvas", tools.crop == RectF(0, 0, 400, 300));
    tools.move({400, 300}, Mods());
    ok("crop cursor on corner", tools.cursor() == CursorKind::ResizeFDiag);
    xfDrag({400, 300}, {300, 200});
    ok("crop handle resize", tools.crop == RectF(0, 0, 300, 200));
    xfDrag({100, 100}, {150, 140});
    ok("crop move", tools.crop == RectF(50, 40, 300, 200));
    xfDrag({350, 240}, {200, 200}, shift);
    ok("crop shift keeps aspect", tools.crop == RectF(50, 40, 240, 160));
    tools.applyCrop();
    ok("crop applied", ed.d.w == 240 && ed.d.h == 160 && tools.crop == RectF(0, 0, 240, 160) && !ed.active()->ext.null());
    ed.undo(); tools.resetCrop();
    ok("crop undo", ed.d.w == 400 && px(ed.active(), 395, 295) == "0,255,0,255");
    xfDrag({-20, -20}, {100, 100}); tools.applyCrop();
    ok("crop can extend the canvas", ed.d.w == 120 && ed.d.h == 120 && alphaAt(ed.active(), 5, 5) == 0 && px(ed.active(), 30, 30) == "0,255,0,255");

    // free transform: rotation, modifiers, cursors
    ed.newDoc(400, 300, false);
    dragTool(Tool::Rect, {100, 100}, {300, 150}); ed.fill(0xff0000); ed.selectNone();
    tools.startTransform();
    T->down({350, 125}, shift); T->move({205, 275}, shift);
    ok("rotate snaps to 90", tools.xf.angle == 90);
    T->up({205, 275}); tools.commitXf();
    ok("rotated 90: tall instead of wide", px(ed.active(), 200, 40) == "255,0,0,255" && px(ed.active(), 200, 210) == "255,0,0,255" && alphaAt(ed.active(), 120, 125) == 0 && alphaAt(ed.active(), 280, 125) == 0);
    ed.undo();
    ok("undo rotation", px(ed.active(), 120, 125) == "255,0,0,255" && alphaAt(ed.active(), 200, 40) == 0);
    tools.startTransform();
    xfDrag({350, 125}, {350, 275});
    ok("rotate free angle", std::fabs(tools.xf.angle - 45) < 0.01);
    const double k = 100 / std::sqrt(2.0), q = std::sqrt(0.5);
    tools.move({200 + 100 * q, 125 + 100 * q}, Mods());
    ok("cursor: rotated right handle = diagonal", tools.cursor() == CursorKind::ResizeFDiag);
    tools.move({200 + 100 * q - 25 * q, 125 + 100 * q + 25 * q}, Mods());
    ok("cursor: rotated corner handle = vertical", tools.cursor() == CursorKind::ResizeV);
    tools.move({200 + 40 * q, 125 + 40 * q}, Mods());
    ok("cursor: inside = move", tools.cursor() == CursorKind::Move);
    tools.move({200, 30}, Mods());
    ok("cursor: outside = rotate", tools.cursor() == CursorKind::Rotate);
    xfDrag({200 + k, 125 + k}, {200 + 2 * k, 125 + 2 * k});
    ok("rotated resize keeps far edge", std::fabs(tools.xf.dst.w - 300) <= 1 && std::fabs(tools.xf.dst.h - 50) <= 1
        && std::hypot(tools.xf.dst.center().x - (200 + k / 2), tools.xf.dst.center().y - (125 + k / 2)) < 1.5);
    tools.cancelXf();
    ok("cancel restores", px(ed.active(), 120, 125) == "255,0,0,255" && alphaAt(ed.active(), 200, 40) == 0 && tools.xf.angle == 0 && tools.cursor() != CursorKind::Rotate);
    tools.startTransform();
    xfDrag({300, 150}, {320, 160}, alt);
    ok("alt = from center", tools.xf.dst == RectF(60, 90, 280, 70));
    tools.cancelXf();
    tools.startTransform();
    xfDrag({300, 150}, {330, 200}, shift);
    ok("shift = free aspect", tools.xf.dst == RectF(100, 100, 230, 100));
    tools.cancelXf();
    dragTool(Tool::Rect, {100, 100}, {300, 150});
    tools.startTransform();
    xfDrag({350, 125}, {200, 275}, shift);
    tools.commitXf();
    ok("selection rotates with pixels", selAt(ed, 200, 40) == 255 && selAt(ed, 120, 125) == 0 && px(ed.active(), 200, 40) == "255,0,0,255");

    // layer reordering (drag and drop in the layer list)
    ed.newDoc(100, 100, true);
    ed.newLayer(); ed.newLayer();
    { const int a = ed.d.layers[0].id, b = ed.d.layers[1].id, c = ed.d.layers[2].id;
      ed.moveLayerTo(2, 0);
      ok("move layer to bottom", ed.d.layers[0].id == c && ed.d.layers[1].id == a && ed.d.layers[2].id == b);
      ed.moveLayerTo(0, 5);
      ok("move layer to top (clamped)", ed.d.layers[2].id == c && ed.d.layers[0].id == a);
      const size_t steps = ed.hist.size();
      ed.moveLayerTo(1, 1);
      ok("no-op move adds no history", ed.hist.size() == steps); }

    // translations
    setLanguage("fr_FR");
    ok("french", std::string(TR("실행 취소")) == "Annuler" && language() == "fr");
    ok("shortcut hint kept", std::string(TR("이동 (V)")) == "Déplacement (V)" && std::string(TR("지우기 (Delete)")) == "Effacer (Delete)");
    ok("spaces and %1 kept", std::string(TR(" 복사본")) == " copie" && std::string(TR("저장됨: %1")) == "Enregistré : %1" && std::string(TR("파일을 열 수 없습니다: ")) == "Impossible d'ouvrir le fichier : ");
    setLanguage("ja");
    ok("japanese", std::string(TR("새 레이어")) == "新規レイヤー" && std::string(TR("선택 영역에서 빼기 (Option+드래그)")) == "選択範囲から一部削除 (Option+ドラッグ)");
    setLanguage("it-IT");
    ed.newDoc(10, 10, true); ed.newLayer();
    ok("italian + default layer names", std::string(TR("파일")) == "File" && ed.d.layers[0].name == "Sfondo" && ed.d.layers[1].name == "Livello 2");
    setLanguage("de_DE");
    ok("unsupported language falls back to english", language() == "en" && std::string(TR("열기...")) == "Open...");
    setLanguage("ko_KR");
    ok("korean is the source", std::string(TR("열기...")) == "열기..." && language() == "ko");

    // resampling
    {
        Image big(100, 100);
        big.fill(0xff0000ffu);
        const Image small = scaled(big, 10, 10), large = scaled(big, 250, 250);
        ok("scaled keeps solid color", small.pixel(5, 5) == 0xff0000ffu && large.pixel(249, 249) == 0xff0000ffu && small.w == 10 && large.w == 250);
        const Image r = rot90(Image(30, 20), true);
        ok("rot90 swaps size", r.w == 20 && r.h == 30);
    }
    printf("%s (%d failed)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
