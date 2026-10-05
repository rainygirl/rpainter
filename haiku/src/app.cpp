// R Painter for Haiku: native user interface built on the Be API (Interface, Storage and Translation kits).
// The editing engine lives in image/doc/tools and has no toolkit dependency.
#include "i18n.h"
#include "tools.h"

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <Button.h>
#include <CheckBox.h>
#include <Clipboard.h>
#include <Control.h>
#include <Cursor.h>
#include <DataIO.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <GridLayout.h>
#include <GridView.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <LocaleRoster.h>
#include <ListItem.h>
#include <ListView.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <Shape.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <View.h>
#include <Window.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

enum {
    MSG_CMD = 'rpCM', MSG_TOOL = 'rpTL', MSG_OPT = 'rpOP', MSG_SELMODE = 'rpSM', MSG_LAYER_PICK = 'rpLP', MSG_LAYER_VIS = 'rpLV',
    MSG_LAYER_RENAME = 'rpLR', MSG_LAYER_OPACITY = 'rpLO', MSG_BLEND = 'rpBL', MSG_OPEN_REFS = 'rpOR', MSG_PLACE_REFS = 'rpPR',
    MSG_SAVE_REFS = 'rpSR', MSG_FORM = 'rpFM', MSG_FORM_CHANGED = 'rpFC', MSG_COLOR = 'rpCL', MSG_PALETTE = 'rpPA', MSG_REFRESH = 'rpRF',
    MSG_PICK_CHANGED = 'rpPC', MSG_LAYER_MOVE = 'rpLM',
};
enum Cmd {
    C_NEW, C_OPEN, C_PLACE, C_EXPORT, C_SAVE, C_QUIT, C_UNDO, C_REDO, C_CUT, C_COPY, C_COPY_MERGED, C_PASTE, C_DELETE, C_FILL_FG, C_FILL_BG,
    C_TRANSFORM, C_ADJUST, C_INVERT, C_GRAYSCALE, C_IMAGE_SIZE, C_CANVAS_SIZE, C_CROP_SEL, C_ROT_CW, C_ROT_CCW, C_ROT_180, C_FLIP_H, C_FLIP_V,
    C_LAYER_NEW, C_LAYER_DUP, C_LAYER_COPY, C_LAYER_CUT, C_LAYER_DEL, C_LAYER_UP, C_LAYER_DOWN, C_MERGE_DOWN, C_FLATTEN, C_LAYER_FLIP_H, C_LAYER_FLIP_V,
    C_LAYER_SEL_UP, C_LAYER_SEL_DOWN, C_SEL_ALL, C_SEL_NONE, C_RESELECT, C_SEL_INVERT, C_SEL_LAYER, C_SEL_FEATHER, C_SEL_EXPAND, C_SEL_CONTRACT,
    C_ZOOM_IN, C_ZOOM_OUT, C_FIT, C_ZOOM_100, C_SWAP, C_DEFAULT_COLORS, C_XF_APPLY, C_XF_CANCEL, C_CROP_APPLY, C_CROP_RESET, C_PICK_FG, C_PICK_BG,
    C_SIZE_DOWN, C_SIZE_UP, C_HARD_DOWN, C_HARD_UP,
};
enum { FORM_NEW = 1, FORM_IMAGE_SIZE, FORM_CANVAS_SIZE, FORM_EXPORT, FORM_ADJUST, FORM_FEATHER, FORM_EXPAND, FORM_CONTRACT, FORM_RENAME };

static BMessage *cmdMsg(int cmd) { BMessage *m = new BMessage(MSG_CMD); m->AddInt32("cmd", cmd); return m; }
static rgb_color toColor(Px rgb) { return make_color(uint8(pxR(rgb)), uint8(pxG(rgb)), uint8(pxB(rgb))); }

// ---------- Image <-> BBitmap, files ----------
static Image imageFromBitmap(const BBitmap *b) {
    if (!b || !b->IsValid()) return Image();
    const color_space cs = b->ColorSpace();
    if (cs != B_RGBA32 && cs != B_RGB32) {
        BBitmap conv(b->Bounds(), B_RGBA32);
        if (conv.ImportBits(b) != B_OK) return Image();
        return imageFromBitmap(&conv);
    }
    const int w = b->Bounds().IntegerWidth() + 1, h = b->Bounds().IntegerHeight() + 1;
    Image im(w, h);
    for (int y = 0; y < h; y++) {
        const uint32 *s = reinterpret_cast<const uint32 *>(static_cast<const uint8 *>(b->Bits()) + size_t(y) * b->BytesPerRow());
        Px *d = im.wrow(y);
        for (int x = 0; x < w; x++) d[x] = premul((s[x] >> 16) & 255, (s[x] >> 8) & 255, s[x] & 255, cs == B_RGB32 ? 255 : int(s[x] >> 24));
    }
    return im;
}
// straight-alpha B_RGBA32, or opaque B_RGB32 flattened onto white
static BBitmap *bitmapFromImage(const Image &im, bool opaque = false) {
    BBitmap *b = new BBitmap(BRect(0, 0, im.w - 1, im.h - 1), opaque ? B_RGB32 : B_RGBA32);
    for (int y = 0; y < im.h; y++) {
        uint32 *d = reinterpret_cast<uint32 *>(static_cast<uint8 *>(b->Bits()) + size_t(y) * b->BytesPerRow());
        const Px *s = im.row(y);
        for (int x = 0; x < im.w; x++) {
            if (!opaque) { d[x] = uint32(pxA(s[x])) << 24 | straightRgb(s[x]); continue; }
            const int ia = 255 - pxA(s[x]);
            d[x] = 0xff000000u | uint32(pxR(s[x]) + ia) << 16 | uint32(pxG(s[x]) + ia) << 8 | uint32(pxB(s[x]) + ia);
        }
    }
    return b;
}
static status_t translateImage(const Image &im, BPositionIO *out, uint32 format) {
    BBitmapStream stream(bitmapFromImage(im, format == B_JPEG_FORMAT)); // the stream owns the bitmap
    return BTranslatorRoster::Default()->Translate(&stream, NULL, NULL, out, format);
}
static std::string encodePng(const Image &im) {
    BMallocIO out;
    if (translateImage(im, &out, B_PNG_FORMAT) != B_OK) return std::string();
    return std::string(static_cast<const char *>(out.Buffer()), out.BufferLength());
}
static Image decodeImage(const std::string &bytes) {
    BMemoryIO io(bytes.data(), bytes.size());
    BBitmap *b = BTranslationUtils::GetBitmap(&io);
    const Image im = imageFromBitmap(b);
    delete b;
    return im;
}
static Image loadImageFile(const char *path) {
    BBitmap *b = BTranslationUtils::GetBitmap(path);
    const Image im = imageFromBitmap(b);
    delete b;
    return im;
}
static std::string readFile(const char *path) {
    BFile f(path, B_READ_ONLY);
    off_t size = 0;
    if (f.InitCheck() != B_OK || f.GetSize(&size) != B_OK) return std::string();
    std::string s(size_t(size), '\0');
    f.Read(&s[0], size_t(size));
    return s;
}
static bool writeFile(const char *path, const std::string &data, const char *mime) {
    BFile f(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (f.InitCheck() != B_OK || f.Write(data.data(), data.size()) != ssize_t(data.size())) return false;
    BNodeInfo(&f).SetType(mime);
    return true;
}
static std::string baseName(const char *path) {
    std::string n = BPath(path).Leaf() ? BPath(path).Leaf() : "";
    const size_t dot = n.rfind('.');
    return dot == std::string::npos || dot == 0 ? n : n.substr(0, dot);
}
static bool endsWith(const std::string &s, const char *suffix) {
    const size_t n = strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) if (tolower(s[s.size() - n + i]) != suffix[i]) return false;
    return true;
}

// ---------- icons ----------
// Same tiny path format as the other builds (24x24 grid): M/L/C/Z, O cx cy rx ry = ellipse, R x y w h = rect.
static void DrawIcon(BView *v, BPoint origin, float size, const char *spec, rgb_color color) {
    const float k = size / 24;
    BShape shape;
    char cmd = 0;
    std::vector<float> n;
    auto flush = [&] {
        if (cmd == 'M' && n.size() == 2) shape.MoveTo(BPoint(n[0] * k, n[1] * k));
        else if (cmd == 'L' && n.size() == 2) shape.LineTo(BPoint(n[0] * k, n[1] * k));
        else if (cmd == 'C' && n.size() == 6) shape.BezierTo(BPoint(n[0] * k, n[1] * k), BPoint(n[2] * k, n[3] * k), BPoint(n[4] * k, n[5] * k));
        else if (cmd == 'Z') shape.Close();
        else if (cmd == 'R' && n.size() == 4) {
            shape.MoveTo(BPoint(n[0] * k, n[1] * k));
            shape.LineTo(BPoint((n[0] + n[2]) * k, n[1] * k));
            shape.LineTo(BPoint((n[0] + n[2]) * k, (n[1] + n[3]) * k));
            shape.LineTo(BPoint(n[0] * k, (n[1] + n[3]) * k));
            shape.Close();
        } else if (cmd == 'O' && n.size() == 4) {
            const float cx = n[0] * k, cy = n[1] * k, rx = n[2] * k, ry = n[3] * k, c = 0.5523f;
            shape.MoveTo(BPoint(cx + rx, cy));
            shape.BezierTo(BPoint(cx + rx, cy + ry * c), BPoint(cx + rx * c, cy + ry), BPoint(cx, cy + ry));
            shape.BezierTo(BPoint(cx - rx * c, cy + ry), BPoint(cx - rx, cy + ry * c), BPoint(cx - rx, cy));
            shape.BezierTo(BPoint(cx - rx, cy - ry * c), BPoint(cx - rx * c, cy - ry), BPoint(cx, cy - ry));
            shape.BezierTo(BPoint(cx + rx * c, cy - ry), BPoint(cx + rx, cy - ry * c), BPoint(cx + rx, cy));
            shape.Close();
        }
        n.clear();
    };
    for (const char *p = spec; *p;) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (isalpha(static_cast<unsigned char>(*p))) { flush(); cmd = *p++; }
        if (*p && *p != ' ' && !isalpha(static_cast<unsigned char>(*p))) {
            char *end = NULL;
            n.push_back(strtof(p, &end));
            p = end;
        }
    }
    flush();
    v->PushState();
    v->SetHighColor(color);
    v->SetPenSize(std::max(1.2f, 1.6f * k));
    v->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
    v->MovePenTo(origin);
    v->StrokeShape(&shape);
    v->PopState();
}
struct ToolInfo { Tool tool; const char *label; const char *icon; };
static const ToolInfo TOOLS[TOOL_COUNT] = {
    {Tool::Move, "이동 (V)", "M12 3 L12 21 M3 12 L21 12 M12 3 L9 6 M12 3 L15 6 M12 21 L9 18 M12 21 L15 18 M3 12 L6 9 M3 12 L6 15 M21 12 L18 9 M21 12 L18 15"},
    {Tool::Rect, "사각형 선택 (M)", "R4 5 16 14"},
    {Tool::Ellipse, "원형 선택 (M)", "O12 12 8 7"},
    {Tool::Lasso, "올가미 (L)", "O12 9 8 5 M7 13 C5 15 6 17 8 17 C10 17 10 19 8 21"},
    {Tool::PolyLasso, "다각형 올가미 (L) - 클릭: 꼭짓점, Enter / 더블클릭: 닫기, Esc: 취소", "M5 8 L16 4 L20 13 L11 20 L4 15 Z R3.5 6.5 3 3 R14.5 2.5 3 3 R18.5 11.5 3 3"},
    {Tool::Wand, "마술봉 - 자동 선택 (W)", "M4 20 L14 10 M16 3 L16 7 M14 5 L18 5 M20 9 L20 13 M18 11 L22 11 M8 3 L8 5 M7 4 L9 4"},
    {Tool::Crop, "자르기 (C) - 핸들: 크기 (Shift: 비율 유지), 안쪽: 이동, Enter / 더블클릭: 적용, Esc: 초기화", "M6 2 L6 18 L22 18 M2 6 L18 6 L18 22"},
    {Tool::Brush, "브러시 (B)", "M20 4 L11 13 M11 13 L9 15 C9 15 8 19 4 20 C7 21 11 20 12 17 L13 15 Z"},
    {Tool::Eraser, "지우개 (E)", "M9 20 L4 15 L14 5 L20 11 L11 20 Z M9 10 L15 16 M9 20 L20 20"},
    {Tool::Bucket, "페인트 통 (G)", "M4 13 L12 5 L19 12 L11 20 Z M12 5 L12 2 M4 13 L19 13 M20 16 C21 18 22 19 22 20 C22 21.1 21.1 22 20 22 C18.9 22 18 21.1 18 20 C18 19 19 18 20 16 Z"},
    {Tool::Gradient, "그레이디언트 (G)", "R3 6 18 12 M8 6 L8 18 M12 6 L12 18 M15 6 L15 18 M17 6 L17 18 M19 6 L19 18"},
    {Tool::Picker, "스포이드 (I)", "M15 5 L19 9 M17 3 L21 7 L18 10 L14 6 Z M14 8 L5 17 L5 20 L8 20 L17 11"},
    {Tool::Hand, "손 (H)", "M8 13 L8 6 C8 4.5 11 4.5 11 6 L11 11 M11 6 L11 4.5 C11 3 14 3 14 4.5 L14 11 M14 6 C14 4.5 17 4.5 17 6 L17 15 C17 19 15 21 11 21 C8 21 6 19 5 16 L3 12 C3 11 5 10 6 12 L8 14"},
    {Tool::Zoom, "돋보기 (Z, Alt: 축소)", "O10 10 6 6 M15 15 L20 20 M8 10 L12 10 M10 8 L10 12"},
};
static const char *ICON_XF = "R6 6 12 12 R4 4 4 4 R16 4 4 4 R4 16 4 4 R16 16 4 4", *ICON_OK = "M5 12 L10 17 L19 7", *ICON_X = "M6 6 L18 18 M18 6 L6 18";
static const char *ICON_TRASH = "M5 7 L19 7 M9 7 L9 4 L15 4 L15 7 M7 7 L8 20 L16 20 L17 7 M10 10 L10 17 M14 10 L14 17";

// Small square button showing an icon; optionally a toggle.
class IconButton : public BControl {
public:
    IconButton(const char *spec, const char *tip, BMessage *msg, bool toggle = false)
        : BControl(tip, "", msg, B_WILL_DRAW), fSpec(spec), fToggle(toggle) {
        SetToolTip(tip);
        SetExplicitMinSize(BSize(27, 27));
        SetExplicitMaxSize(BSize(27, 27));
    }
    void Draw(BRect) override {
        const rgb_color bg = ui_color(B_PANEL_BACKGROUND_COLOR);
        SetHighColor(Value() || fDown ? tint_color(bg, B_DARKEN_2_TINT) : bg);
        FillRect(Bounds());
        if (Value()) { SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR)); StrokeRect(Bounds()); }
        DrawIcon(this, BPoint(3, 3), 22, fSpec, ui_color(B_PANEL_TEXT_COLOR));
    }
    void MouseDown(BPoint) override {
        if (!IsEnabled()) return;
        if (fToggle) SetValue(!Value());
        fDown = true;
        Invalidate();
        Invoke();
    }
    void MouseUp(BPoint) override { fDown = false; Invalidate(); }

private:
    const char *fSpec;
    bool fToggle, fDown = false;
};
class IconLabel : public BView {
public:
    IconLabel(const char *spec, const char *tip) : BView("icon", B_WILL_DRAW), fSpec(spec) {
        SetToolTip(tip);
        SetExplicitMinSize(BSize(23, 23));
        SetExplicitMaxSize(BSize(23, 23));
        SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    }
    void SetSpec(const char *spec, const char *tip) { fSpec = spec; SetToolTip(tip); Invalidate(); }
    void Draw(BRect) override { DrawIcon(this, BPoint(1, 1), 22, fSpec, ui_color(B_PANEL_TEXT_COLOR)); }

private:
    const char *fSpec;
};

// ---------- canvas ----------
class CanvasView : public BView {
public:
    CanvasView(Editor *ed, Tools *tools)
        : BView("canvas", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE | B_PULSE_NEEDED | B_NAVIGABLE), fEd(ed), fTools(tools) {
        SetViewColor(B_TRANSPARENT_COLOR);
        SetExplicitMinSize(BSize(300, 200));
    }
    ~CanvasView() { delete fDisplay; }
    double zoom = 1;
    BPoint off; // view position of the document's top-left

    void Damage(IRect r) {
        if (r.empty()) fAllDirty = true;
        else fDirty = fDirty.united(r);
        Invalidate();
    }
    void Fit(double maxZoom = 1) {
        const BRect b = Bounds();
        zoom = std::min(maxZoom, std::max(0.02, std::min((b.Width() - 60.0) / fEd->d.w, (b.Height() - 60.0) / fEd->d.h)));
        off = BPoint(floor((b.Width() - fEd->d.w * zoom) / 2), floor((b.Height() - fEd->d.h * zoom) / 2));
        ZoomChanged();
    }
    void ZoomAt(BPoint w, double f) {
        const double nz = std::min(64.0, std::max(0.02, zoom * f));
        off = BPoint(w.x - (w.x - off.x) * nz / zoom, w.y - (w.y - off.y) * nz / zoom);
        zoom = nz;
        ZoomChanged();
    }
    void ZoomCenter(double f) { ZoomAt(BPoint(Bounds().Width() / 2, Bounds().Height() / 2), f); }
    void UpdateCursor();

    void AttachedToWindow() override { MakeFocus(true); }
    void Draw(BRect update) override;
    void MouseDown(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage *) override;
    void MouseUp(BPoint where) override;
    void KeyDown(const char *bytes, int32 numBytes) override;
    void KeyUp(const char *bytes, int32 numBytes) override { if (bytes[0] == B_SPACE) fSpace = false; else BView::KeyUp(bytes, numBytes); }
    void MessageReceived(BMessage *msg) override;
    void Pulse() override {
        if (fEd->d.selEdges || fTools->polyOn || (fTools->drag && (!fTools->drag->path.empty() || fTools->drag->hasLine))) {
            fAntPhase = (fAntPhase + 1) % 8;
            Invalidate();
        }
    }

private:
    Editor *fEd;
    Tools *fTools;
    Image fComp;            // composite of all layers (premultiplied)
    BBitmap *fDisplay = NULL; // composite over the checkerboard, what is actually drawn
    IRect fDirty;
    bool fAllDirty = true, fPanning = false, fSpace = false, fInside = false;
    BPoint fPanStart, fPanOff, fMouse;
    int fAntPhase = 0;
    CursorKind fCursor = CursorKind::Cross;

    PtF ToDoc(BPoint w) const { return PtF{(w.x - off.x) / zoom, (w.y - off.y) / zoom}; }
    BPoint ToView(PtF p) const { return BPoint(float(off.x + p.x * zoom), float(off.y + p.y * zoom)); }
    Mods CurrentMods() const {
        const uint32 m = modifiers();
        return Mods{(m & B_SHIFT_KEY) != 0, (m & (B_OPTION_KEY | B_CONTROL_KEY | B_COMMAND_KEY)) != 0};
    }
    void ZoomChanged() { fTools->zoom = zoom; Window()->PostMessage(MSG_REFRESH); Invalidate(); }
    void UpdateDisplay();
    pattern AntPattern() const {
        pattern p;
        for (int y = 0; y < 8; y++) { const int s = (y + fAntPhase) % 8; p.data[y] = uint8((0xf0 >> s) | (0xf0 << (8 - s))); }
        return p;
    }
    void StrokeAnts(BPoint a, BPoint b) { StrokeLine(a, b, AntPattern()); }
    void DrawBox(RectF r, double angle, rgb_color color);
};
void CanvasView::UpdateDisplay() {
    const int w = fEd->d.w, h = fEd->d.h;
    if (fComp.w != w || fComp.h != h || !fDisplay) {
        fComp = Image(w, h);
        delete fDisplay;
        fDisplay = new BBitmap(BRect(0, 0, w - 1, h - 1), B_RGB32);
        fAllDirty = true;
        fTools->resetCrop();
    }
    const IRect r = (fAllDirty ? fEd->docRect() : fDirty) & fEd->docRect();
    fAllDirty = false;
    fDirty = IRect();
    if (r.empty()) return;
    fEd->composite(fComp, r);
    for (int y = r.y; y < r.b(); y++) {
        const Px *s = fComp.row(y) + r.x;
        uint32 *d = reinterpret_cast<uint32 *>(static_cast<uint8 *>(fDisplay->Bits()) + size_t(y) * fDisplay->BytesPerRow()) + r.x;
        for (int x = 0; x < r.w; x++) {
            const int ia = 255 - pxA(s[x]), c = (((x + r.x) >> 3) + (y >> 3)) & 1 ? 0xc8 : 0xff, bgc = mul255(c, ia);
            d[x] = 0xff000000u | uint32(pxR(s[x]) + bgc) << 16 | uint32(pxG(s[x]) + bgc) << 8 | uint32(pxB(s[x]) + bgc);
        }
    }
}
void CanvasView::DrawBox(RectF r, double angle, rgb_color color) {
    const double rad = angle * M_PI / 180, cs = cos(rad), sn = sin(rad);
    const PtF c = r.center();
    auto at = [&](double lx, double ly) { return ToView(PtF{c.x + lx * cs - ly * sn, c.y + lx * sn + ly * cs}); };
    const BPoint corners[4] = {at(-r.w / 2, -r.h / 2), at(r.w / 2, -r.h / 2), at(r.w / 2, r.h / 2), at(-r.w / 2, r.h / 2)};
    SetHighColor(color);
    for (int i = 0; i < 4; i++) StrokeLine(corners[i], corners[(i + 1) % 4]);
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            if (!i && !j) continue;
            const BPoint h = at(i * r.w / 2, j * r.h / 2);
            const BRect box(h.x - 4, h.y - 4, h.x + 4, h.y + 4);
            SetHighColor(255, 255, 255);
            FillRect(box);
            SetHighColor(color);
            StrokeRect(box);
        }
}
void CanvasView::Draw(BRect update) {
    UpdateDisplay();
    const BRect b = Bounds();
    const BRect docRect(off.x, off.y, off.x + float(fEd->d.w * zoom) - 1, off.y + float(fEd->d.h * zoom) - 1);
    SetDrawingMode(B_OP_COPY);
    SetHighColor(27, 27, 27);
    // background around the document only (drawing under the bitmap would flicker)
    BRegion around(b);
    around.Exclude(docRect);
    FillRegion(&around);
    DrawBitmap(fDisplay, fDisplay->Bounds(), docRect, zoom < 1 ? B_FILTER_BITMAP_BILINEAR : 0);

    SetHighColor(0, 0, 0);
    SetLowColor(255, 255, 255);
    const bool hideSel = fTools->xf.on || (fTools->drag && fTools->drag->hideSel);
    if (fEd->d.selEdges && !hideSel) {
        for (const Line &l : *fEd->d.selEdges) {
            const BPoint p1 = ToView(PtF{double(l.x1), double(l.y1)}), p2 = ToView(PtF{double(l.x2), double(l.y2)});
            if (std::max(p1.x, p2.x) < update.left || std::min(p1.x, p2.x) > update.right || std::max(p1.y, p2.y) < update.top || std::min(p1.y, p2.y) > update.bottom) continue;
            StrokeAnts(p1, p2);
        }
    }
    if (fTools->drag && fTools->drag->path.size() > 1) {
        const std::vector<PtF> &pa = fTools->drag->path;
        for (size_t i = 0; i < pa.size(); i++) StrokeAnts(ToView(pa[i]), ToView(pa[(i + 1) % pa.size()]));
    }
    if (fTools->drag && fTools->drag->hasLine) StrokeAnts(ToView(fTools->drag->line0), ToView(fTools->drag->line1));
    if (fTools->polyOn) {
        const std::vector<PtF> &pa = fTools->poly;
        for (size_t i = 0; i + 1 < pa.size(); i++) StrokeAnts(ToView(pa[i]), ToView(pa[i + 1]));
        if (fInside) StrokeAnts(ToView(pa.back()), fMouse);
        const BPoint f = ToView(pa.front());
        StrokeRect(BRect(f.x - 3, f.y - 3, f.x + 3, f.y + 3));
    }
    if (fTools->tool == Tool::Crop && !fTools->xf.on) {
        const RectF c = fTools->crop;
        const BPoint tl = ToView(PtF{c.x, c.y}), br = ToView(PtF{c.x + c.w, c.y + c.h});
        const BRect cr(tl.x, tl.y, br.x, br.y);
        SetDrawingMode(B_OP_ALPHA);
        SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
        SetHighColor(0, 0, 0, 150);
        BRegion dim(b);
        dim.Exclude(cr);
        FillRegion(&dim);
        SetHighColor(255, 255, 255, 90);
        for (int i = 1; i <= 2; i++) {
            StrokeLine(BPoint(cr.left + cr.Width() * i / 3, cr.top), BPoint(cr.left + cr.Width() * i / 3, cr.bottom));
            StrokeLine(BPoint(cr.left, cr.top + cr.Height() * i / 3), BPoint(cr.right, cr.top + cr.Height() * i / 3));
        }
        SetDrawingMode(B_OP_COPY);
        DrawBox(c, 0, make_color(255, 255, 255));
    }
    if (fTools->xf.on) DrawBox(fTools->xf.dst, fTools->xf.angle, make_color(45, 127, 249));
    else if (fInside && (fTools->tool == Tool::Brush || fTools->tool == Tool::Eraser)) {
        const float r = float(std::max(1.0, fTools->opt.size * zoom / 2));
        SetDrawingMode(B_OP_INVERT);
        StrokeEllipse(fMouse, r, r);
        SetDrawingMode(B_OP_COPY);
    }
}
// circular arrow, white halo under a dark line, drawn with the engine's own brush primitive
static BCursor *MakeRotateCursor() {
    Image im(32, 32);
    for (int pass = 0; pass < 2; pass++) {
        const double r = pass ? 0.9 : 2.3;
        const Px color = pass ? 0x000000 : 0xffffff;
        PtF prev{16 + 8 * cos(-M_PI / 4), 16 + 8 * sin(-M_PI / 4)};
        for (int i = 1; i <= 42; i++) { // counter-clockwise from the upper right, 315 degrees
            const double a = -M_PI / 4 - i * (315.0 / 42) * M_PI / 180;
            const PtF p{16 + 8 * cos(a), 16 + 8 * sin(a)};
            if (pass) { Image seg(32, 32); capsule(seg, prev, p, r, color); draw(im, seg, 0, 0); } else capsule(im, prev, p, r, color);
            prev = p;
        }
        const PtF tip{24, 13}, l{20.5, 17}, rr{27.5, 17};
        for (PtF q : {l, rr}) { Image seg(32, 32); capsule(seg, tip, q, r, color); draw(im, seg, 0, 0); }
    }
    BBitmap *bmp = bitmapFromImage(im);
    BCursor *cursor = new BCursor(bmp, BPoint(16, 16));
    delete bmp;
    return cursor;
}
void CanvasView::UpdateCursor() {
    const CursorKind c = fPanning ? CursorKind::Hand : fTools->cursor();
    if (c == fCursor) return;
    fCursor = c;
    static const BCursor cross(B_CURSOR_ID_CROSS_HAIR), move(B_CURSOR_ID_MOVE), hand(B_CURSOR_ID_GRAB), ew(B_CURSOR_ID_RESIZE_EAST_WEST),
        ns(B_CURSOR_ID_RESIZE_NORTH_SOUTH), nwse(B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST), nesw(B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST);
    static const BCursor *rotPtr = MakeRotateCursor();
    const BCursor &rot = *rotPtr;
    SetViewCursor(c == CursorKind::Move ? &move : c == CursorKind::Hand ? &hand : c == CursorKind::ResizeH ? &ew : c == CursorKind::ResizeV ? &ns
                  : c == CursorKind::ResizeFDiag ? &nwse : c == CursorKind::ResizeBDiag ? &nesw : c == CursorKind::Rotate ? &rot : &cross);
}
void CanvasView::MouseDown(BPoint where) {
    MakeFocus(true);
    int32 buttons = B_PRIMARY_MOUSE_BUTTON, clicks = 1;
    if (BMessage *m = Window()->CurrentMessage()) { m->FindInt32("buttons", &buttons); m->FindInt32("clicks", &clicks); }
    if (buttons & B_SECONDARY_MOUSE_BUTTON) return;
    SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
    fMouse = where;
    if (fSpace || (buttons & B_TERTIARY_MOUSE_BUTTON) || fTools->tool == Tool::Hand) {
        fPanning = true; fPanStart = where; fPanOff = off;
        return;
    }
    if (clicks >= 2 && (fTools->xf.on || fTools->polyOn || fTools->tool == Tool::Crop)) { fTools->doubleClick(); UpdateCursor(); return; }
    fTools->down(ToDoc(where), CurrentMods());
    Invalidate();
}
void CanvasView::MouseMoved(BPoint where, uint32 transit, const BMessage *) {
    fMouse = where;
    fInside = transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW;
    if (fPanning) { off = BPoint(fPanOff.x + where.x - fPanStart.x, fPanOff.y + where.y - fPanStart.y); Invalidate(); return; }
    fTools->move(ToDoc(where), CurrentMods());
    if (!fTools->drag) UpdateCursor();
    BMessage m(MSG_REFRESH);
    m.AddBool("pos", true);
    Window()->PostMessage(&m);
    Invalidate();
}
void CanvasView::MouseUp(BPoint where) {
    if (fPanning) { fPanning = false; return; }
    fTools->up(ToDoc(where));
    UpdateCursor();
    Invalidate();
}
void CanvasView::KeyDown(const char *bytes, int32 numBytes) {
    const uint32 mods = modifiers();
    const bool shift = mods & B_SHIFT_KEY, alt = mods & (B_OPTION_KEY | B_CONTROL_KEY);
    auto post = [this](int cmd) { BMessage m(MSG_CMD); m.AddInt32("cmd", cmd); Window()->PostMessage(&m); };
    auto tool = [this](Tool t) { BMessage m(MSG_TOOL); m.AddInt32("tool", int32(t)); Window()->PostMessage(&m); };
    switch (bytes[0]) {
    case B_SPACE: fSpace = true; return;
    case B_ENTER: if (fTools->enter()) { UpdateCursor(); Invalidate(); } return;
    case B_ESCAPE: if (fTools->escape()) { UpdateCursor(); Invalidate(); } return;
    case B_LEFT_ARROW: case B_RIGHT_ARROW: case B_UP_ARROW: case B_DOWN_ARROW:
        if (fTools->tool == Tool::Move) {
            const int n = shift ? 10 : 1;
            fTools->nudge(bytes[0] == B_LEFT_ARROW ? -n : bytes[0] == B_RIGHT_ARROW ? n : 0, bytes[0] == B_UP_ARROW ? -n : bytes[0] == B_DOWN_ARROW ? n : 0);
        }
        return;
    case B_BACKSPACE: case B_DELETE: post(alt ? C_FILL_FG : shift ? C_FILL_BG : C_DELETE); return;
    case B_TAB: return;
    }
    const Tool cur = fTools->tool;
    switch (tolower(static_cast<unsigned char>(bytes[0]))) {
    case 'v': tool(Tool::Move); return;
    case 'm': tool(cur == Tool::Rect ? Tool::Ellipse : Tool::Rect); return;
    case 'l': tool(cur == Tool::Lasso ? Tool::PolyLasso : Tool::Lasso); return;
    case 'w': tool(Tool::Wand); return;
    case 'c': tool(Tool::Crop); return;
    case 'b': tool(Tool::Brush); return;
    case 'e': tool(Tool::Eraser); return;
    case 'g': tool(cur == Tool::Bucket ? Tool::Gradient : Tool::Bucket); return;
    case 'i': tool(Tool::Picker); return;
    case 'h': tool(Tool::Hand); return;
    case 'z': tool(Tool::Zoom); return;
    case 'x': post(C_SWAP); return;
    case 'd': post(C_DEFAULT_COLORS); return;
    case '[': post(C_SIZE_DOWN); return;
    case ']': post(C_SIZE_UP); return;
    case '{': post(C_HARD_DOWN); return;
    case '}': post(C_HARD_UP); return;
    }
    if (bytes[0] >= '0' && bytes[0] <= '9') {
        BMessage m(MSG_OPT);
        m.AddInt32("opacityKey", bytes[0] == '0' ? 100 : (bytes[0] - '0') * 10);
        Window()->PostMessage(&m);
        return;
    }
    BView::KeyDown(bytes, numBytes);
}
void CanvasView::MessageReceived(BMessage *msg) {
    if (msg->what == B_MOUSE_WHEEL_CHANGED) {
        float dx = 0, dy = 0;
        msg->FindFloat("be:wheel_delta_x", &dx);
        msg->FindFloat("be:wheel_delta_y", &dy);
        if (modifiers() & (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY)) ZoomAt(fMouse, pow(1.2, -dy));
        else { off = BPoint(off.x - dx * 40, off.y - dy * 40); Invalidate(); }
        return;
    }
    BView::MessageReceived(msg);
}

// ---------- tool bar ----------
class ToolBar : public BView {
public:
    explicit ToolBar(Tools *tools) : BView("tools", B_WILL_DRAW), fTools(tools) {
        SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
        SetExplicitMinSize(BSize(37, TOOL_COUNT * 34 + 4));
        SetExplicitMaxSize(BSize(37, B_SIZE_UNLIMITED));
    }
    void Draw(BRect) override {
        const rgb_color bg = ui_color(B_PANEL_BACKGROUND_COLOR), fgc = ui_color(B_PANEL_TEXT_COLOR);
        for (int i = 0; i < TOOL_COUNT; i++) {
            const BRect cell(3, 3 + i * 34, 34, 3 + i * 34 + 31);
            if (TOOLS[i].tool == fTools->tool) {
                SetHighColor(tint_color(bg, B_DARKEN_2_TINT));
                FillRect(cell);
                SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
                StrokeRect(cell);
            }
            DrawIcon(this, BPoint(cell.left + 4, cell.top + 4), 24, TOOLS[i].icon, fgc);
        }
    }
    void MouseDown(BPoint where) override {
        const int i = int((where.y - 3) / 34);
        if (i < 0 || i >= TOOL_COUNT) return;
        BMessage m(MSG_TOOL);
        m.AddInt32("tool", int32(TOOLS[i].tool));
        Window()->PostMessage(&m);
    }
    void MouseMoved(BPoint where, uint32, const BMessage *) override {
        const int i = int((where.y - 3) / 34);
        if (i != fHover && i >= 0 && i < TOOL_COUNT) { fHover = i; SetToolTip(TR(TOOLS[i].label)); }
    }

private:
    Tools *fTools;
    int fHover = -1;
};

// Whole-number text field (the public API has no spin box). The message is sent on Enter or when focus leaves.
class NumField : public BTextControl {
public:
    NumField(const char *name, const char *label, BMessage *msg, int min, int max, int value)
        : BTextControl(name, label, "", msg), fMin(min), fMax(max) { SetValue(value); }
    int32 Value() const { return std::min(fMax, std::max(fMin, atoi(Text()))); }
    void SetValue(int v) {
        char buf[16];
        snprintf(buf, sizeof buf, "%d", std::min(fMax, std::max(fMin, v)));
        if (strcmp(Text(), buf)) SetText(buf);
    }

private:
    int fMin, fMax;
};

// ---------- generic form dialog ----------
struct Field {
    enum Type { Int, Slider, Choice, Check, Text, Note } type;
    const char *key, *label;
    int min, max, value;
    std::vector<const char *> choices;
    std::string text;
};
// Posts MSG_FORM (with "ok" or "cancel") to the target; with live = true also MSG_FORM_CHANGED while sliders move.
class FormWindow : public BWindow {
public:
    FormWindow(const char *title, int32 id, const std::vector<Field> &fields, BMessenger target, bool live = false, double aspect = 0)
        : BWindow(BRect(0, 0, 320, 100), title, B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
                  B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
          fId(id), fFields(fields), fTarget(target), fLive(live), fAspect(aspect) {
        BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING).SetInsets(B_USE_WINDOW_INSETS); // also gives the panel background
        BGroupLayout *root = static_cast<BGroupLayout *>(GetLayout());
        for (size_t i = 0; i < fFields.size(); i++) {
            const Field &f = fFields[i];
            BMessage *changed = new BMessage('fchg');
            changed->AddInt32("index", int32(i));
            BView *v = NULL;
            if (f.type == Field::Int) {
                v = new NumField(f.key, f.label, changed, f.min, f.max, f.value);
            } else if (f.type == Field::Slider) {
                BSlider *s = new BSlider(f.key, f.label, changed, f.min, f.max, B_HORIZONTAL);
                s->SetValue(f.value);
                s->SetModificationMessage(new BMessage(*changed));
                s->SetExplicitMinSize(BSize(280, B_SIZE_UNSET));
                v = s;
            } else if (f.type == Field::Choice) {
                BPopUpMenu *menu = new BPopUpMenu("choice");
                for (size_t c = 0; c < f.choices.size(); c++) {
                    BMenuItem *item = new BMenuItem(f.choices[c], new BMessage(*changed));
                    if (int(c) == f.value) item->SetMarked(true);
                    menu->AddItem(item);
                }
                delete changed;
                v = new BMenuField(f.key, f.label, menu);
            } else if (f.type == Field::Check) {
                BCheckBox *c = new BCheckBox(f.key, f.label, changed);
                c->SetValue(f.value);
                v = c;
            } else if (f.type == Field::Text) {
                delete changed;
                v = new BTextControl(f.key, f.label, f.text.c_str(), NULL);
            } else {
                delete changed;
                v = new BStringView(f.key, f.label);
            }
            fViews.push_back(v);
            root->AddView(v);
        }
        BButton *okButton = new BButton("ok", TR("확인"), new BMessage('fok '));
        BLayoutBuilder::Group<>(root).AddGroup(B_HORIZONTAL).AddGlue().Add(new BButton("cancel", TR("취소"), new BMessage('fcan'))).Add(okButton).End();
        SetDefaultButton(okButton);
        CenterOnScreen();
    }
    void MessageReceived(BMessage *msg) override {
        if (msg->what == 'fchg') {
            int32 index = -1;
            msg->FindInt32("index", &index);
            if (fAspect > 0 && index >= 0) { // image size: keep width and height in proportion
                NumField *w = dynamic_cast<NumField *>(FindView("w")), *h = dynamic_cast<NumField *>(FindView("h"));
                BCheckBox *lock = dynamic_cast<BCheckBox *>(FindView("lock"));
                if (w && h && lock && lock->Value()) {
                    if (!strcmp(fFields[index].key, "w")) h->SetValue(std::max(1, int(lround(w->Value() / fAspect))));
                    if (!strcmp(fFields[index].key, "h")) w->SetValue(std::max(1, int(lround(h->Value() * fAspect))));
                }
            }
            if (fLive) Send(MSG_FORM_CHANGED, NULL);
        } else if (msg->what == 'fok ') { fDone = true; Send(MSG_FORM, "ok"); Quit(); }
        else if (msg->what == 'fcan') { fDone = true; Send(MSG_FORM, "cancel"); Quit(); }
        else BWindow::MessageReceived(msg);
    }
    bool QuitRequested() override {
        if (!fDone) Send(MSG_FORM, "cancel");
        return true;
    }

private:
    int32 fId;
    std::vector<Field> fFields;
    std::vector<BView *> fViews;
    BMessenger fTarget;
    bool fLive, fDone = false;
    double fAspect;

    void Send(uint32 what, const char *flag) {
        BMessage m(what);
        m.AddInt32("id", fId);
        if (flag) m.AddBool(flag, true);
        for (size_t i = 0; i < fFields.size(); i++) {
            const Field &f = fFields[i];
            if (f.type == Field::Int) m.AddInt32(f.key, static_cast<NumField *>(fViews[i])->Value());
            else if (f.type == Field::Slider) m.AddInt32(f.key, static_cast<BSlider *>(fViews[i])->Value());
            else if (f.type == Field::Check) m.AddInt32(f.key, static_cast<BCheckBox *>(fViews[i])->Value());
            else if (f.type == Field::Text) m.AddString(f.key, static_cast<BTextControl *>(fViews[i])->Text());
            else if (f.type == Field::Choice) {
                BMenu *menu = static_cast<BMenuField *>(fViews[i])->Menu();
                m.AddInt32(f.key, menu->FindMarkedIndex());
            }
        }
        fTarget.SendMessage(&m);
    }
};

// ---------- color picker ----------
static void hsvToRgb(double h, double s, double v, int &r, int &g, int &b) {
    auto f = [&](double n) { const double k = fmod(n + h / 60, 6); return int(lround(255 * (v - v * s * std::max(0.0, std::min(std::min(k, 4 - k), 1.0))))); };
    r = f(5); g = f(3); b = f(1);
}
static void rgbToHsv(int ri, int gi, int bi, double &h, double &s, double &v) {
    const double r = ri / 255.0, g = gi / 255.0, b = bi / 255.0, mx = std::max(r, std::max(g, b)), d = mx - std::min(r, std::min(g, b));
    h = 0;
    if (d > 0) h = 60 * (mx == r ? fmod((g - b) / d + 6, 6) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4);
    s = mx > 0 ? d / mx : 0;
    v = mx;
}
// Saturation/brightness square (mode 0) or hue strip (mode 1); reports clicks and drags as 0..1 positions.
class PickArea : public BView {
public:
    PickArea(int mode) : BView("area", B_WILL_DRAW), fMode(mode) {
        const BSize size(mode ? 23 : 255, 255);
        SetExplicitMinSize(size);
        SetExplicitMaxSize(size);
        SetViewColor(B_TRANSPARENT_COLOR);
    }
    ~PickArea() { delete fBitmap; }
    double hue = 0, sat = 0, val = 0;
    void Draw(BRect) override {
        const int w = fMode ? 24 : 256;
        if (!fBitmap) fBitmap = new BBitmap(BRect(0, 0, w - 1, 255), B_RGB32);
        if (fMode == 1 ? fBitmapHue < 0 : fBitmapHue != hue) {
            for (int y = 0; y < 256; y++) {
                uint32 *row = reinterpret_cast<uint32 *>(static_cast<uint8 *>(fBitmap->Bits()) + size_t(y) * fBitmap->BytesPerRow());
                for (int x = 0; x < w; x++) {
                    int r, g, b;
                    if (fMode) hsvToRgb(y * 360.0 / 256, 1, 1, r, g, b); else hsvToRgb(hue, x / 255.0, 1 - y / 255.0, r, g, b);
                    row[x] = 0xff000000u | uint32(r) << 16 | uint32(g) << 8 | uint32(b);
                }
            }
            fBitmapHue = hue;
        }
        DrawBitmap(fBitmap, BPoint(0, 0));
        if (fMode) {
            const float y = float(hue / 360 * 255);
            SetHighColor(255, 255, 255); FillRect(BRect(0, y - 2, 23, y + 2));
            SetHighColor(0, 0, 0); StrokeRect(BRect(0, y - 2, 23, y + 2));
        } else {
            const BPoint c(float(sat * 255), float((1 - val) * 255));
            SetHighColor(0, 0, 0); StrokeEllipse(c, 6, 6);
            SetHighColor(255, 255, 255); StrokeEllipse(c, 5, 5);
        }
    }
    void MouseDown(BPoint where) override { SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS); fDrag = true; Report(where); }
    void MouseMoved(BPoint where, uint32, const BMessage *) override { if (fDrag) Report(where); }
    void MouseUp(BPoint) override { fDrag = false; }

private:
    int fMode;
    BBitmap *fBitmap = NULL;
    double fBitmapHue = -1;
    bool fDrag = false;
    void Report(BPoint p) {
        BMessage m(MSG_PICK_CHANGED);
        m.AddInt32("mode", fMode);
        m.AddDouble("x", std::min(1.0, std::max(0.0, p.x / 255.0)));
        m.AddDouble("y", std::min(1.0, std::max(0.0, p.y / 255.0)));
        Window()->PostMessage(&m);
    }
};
class Swatch : public BView {
public:
    Swatch(Px rgb, float w, float h, BMessage *msg = NULL) : BView("swatch", B_WILL_DRAW), color(rgb), fMsg(msg) {
        SetExplicitMinSize(BSize(w, h));
        SetExplicitMaxSize(BSize(w, h));
    }
    ~Swatch() { delete fMsg; }
    Px color;
    void SetColor(Px rgb) { color = rgb; Invalidate(); }
    void Draw(BRect) override {
        SetHighColor(toColor(color)); FillRect(Bounds());
        SetHighColor(20, 20, 20); StrokeRect(Bounds());
    }
    void MouseDown(BPoint) override { if (fMsg) Window()->PostMessage(fMsg); }

private:
    BMessage *fMsg;
};
class ColorPickerWindow : public BWindow {
public:
    ColorPickerWindow(const char *title, Px initial, int32 which, BMessenger target)
        : BWindow(BRect(0, 0, 100, 100), title, B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
                  B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
          fWhich(which), fTarget(target) {
        fSquare = new PickArea(0);
        fStrip = new PickArea(1);
        fNew = new Swatch(initial, 120, 30);
        const char *names[6] = {"H", "S", "B", "R", "G", "B"};
        const int maxs[6] = {359, 100, 100, 255, 255, 255};
        BGroupView *sideView = new BGroupView(B_VERTICAL, 4);
        BGroupLayout *side = sideView->GroupLayout();
        side->AddView(fNew);
        side->AddView(new Swatch(initial, 120, 30));
        for (int i = 0; i < 6; i++) {
            BMessage *m = new BMessage('pfld');
            m->AddInt32("i", i);
            fSpin[i] = new NumField("num", names[i], m, 0, maxs[i], 0);
            side->AddView(fSpin[i]);
        }
        fHex = new BTextControl("hex", "#", "", new BMessage('phex'));
        side->AddView(fHex);
        BButton *okButton = new BButton("ok", TR("확인"), new BMessage('pok '));
        BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING).SetInsets(B_USE_WINDOW_INSETS)
            .AddGroup(B_HORIZONTAL).Add(fSquare).Add(fStrip).Add(sideView).End()
            .AddGroup(B_HORIZONTAL).AddGlue().Add(new BButton("cancel", TR("취소"), new BMessage('pcan'))).Add(okButton).End();
        SetDefaultButton(okButton);
        SetRgb(pxR(initial), pxG(initial), pxB(initial));
        Sync();
        CenterOnScreen();
    }
    void MessageReceived(BMessage *msg) override {
        switch (msg->what) {
        case MSG_PICK_CHANGED: {
            int32 mode = 0;
            double x = 0, y = 0;
            msg->FindInt32("mode", &mode); msg->FindDouble("x", &x); msg->FindDouble("y", &y);
            if (mode) fH = std::min(y * 360, 359.999); else { fS = x; fV = 1 - y; }
            Sync();
            break;
        }
        case 'pfld': {
            if (fSyncing) break;
            int32 i = 0;
            msg->FindInt32("i", &i);
            if (i < 3) { fH = fSpin[0]->Value(); fS = fSpin[1]->Value() / 100.0; fV = fSpin[2]->Value() / 100.0; }
            else SetRgb(fSpin[3]->Value(), fSpin[4]->Value(), fSpin[5]->Value());
            Sync();
            break;
        }
        case 'phex': {
            const char *t = fHex->Text();
            if (*t == '#') t++;
            if (strlen(t) == 6 && strspn(t, "0123456789abcdefABCDEF") == 6) { const long v = strtol(t, NULL, 16); SetRgb(int(v >> 16) & 255, int(v >> 8) & 255, int(v) & 255); }
            Sync();
            break;
        }
        case 'pok ': {
            BMessage m(MSG_COLOR);
            m.AddInt32("which", fWhich);
            m.AddInt32("rgb", int32(Current()));
            fTarget.SendMessage(&m);
            Quit();
            break;
        }
        case 'pcan': Quit(); break;
        default: BWindow::MessageReceived(msg);
        }
    }

private:
    int32 fWhich;
    BMessenger fTarget;
    PickArea *fSquare, *fStrip;
    Swatch *fNew;
    NumField *fSpin[6];
    BTextControl *fHex;
    double fH = 0, fS = 0, fV = 0; // hue kept separately so it survives gray colors
    bool fSyncing = false;

    Px Current() const { int r, g, b; hsvToRgb(fH, fS, fV, r, g, b); return Px(r) << 16 | Px(g) << 8 | Px(b); }
    void SetRgb(int r, int g, int b) {
        double h, s, v;
        rgbToHsv(r, g, b, h, s, v);
        if (s > 0 && v > 0) fH = h;
        fS = s; fV = v;
    }
    void Sync() {
        fSyncing = true;
        const Px c = Current();
        fSquare->hue = fStrip->hue = fH;
        fSquare->sat = fS; fSquare->val = fV;
        fSquare->Invalidate(); fStrip->Invalidate();
        const int vals[6] = {int(lround(fH)) % 360, int(lround(fS * 100)), int(lround(fV * 100)), pxR(c), pxG(c), pxB(c)};
        for (int i = 0; i < 6; i++) fSpin[i]->SetValue(vals[i]);
        char buf[16];
        snprintf(buf, sizeof buf, "%06x", unsigned(c));
        fHex->SetText(buf);
        fNew->SetColor(c);
        fSyncing = false;
    }
};

// ---------- layer list ----------
class LayerItem : public BListItem {
public:
    LayerItem(const Layer &l, int docW, int docH) : id(l.id), fName(l.name), fVisible(l.visible) {
        const double sc = std::min(40.0 / docW, 30.0 / docH);
        const Image thumb = scaled(l.img, std::max(1, int(docW * sc)), std::max(1, int(docH * sc)));
        fThumb = bitmapFromImage(thumb, true);
    }
    ~LayerItem() { delete fThumb; }
    int id;
    void Update(BView *owner, const BFont *font) override { BListItem::Update(owner, font); SetHeight(36); }
    void DrawItem(BView *owner, BRect frame, bool) override {
        const rgb_color bg = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR) : ui_color(B_LIST_BACKGROUND_COLOR);
        owner->SetHighColor(bg);
        owner->FillRect(frame);
        const BRect box(frame.left + 6, frame.top + 11, frame.left + 19, frame.top + 24); // visibility checkbox
        owner->SetHighColor(255, 255, 255);
        owner->FillRect(box);
        owner->SetHighColor(60, 60, 60);
        owner->StrokeRect(box);
        if (fVisible) DrawIcon(owner, BPoint(box.left + 1, box.top + 1), 12, "M5 12 L10 17 L19 7", make_color(20, 20, 20));
        const float tw = fThumb->Bounds().Width(), th = fThumb->Bounds().Height();
        const BPoint tp(frame.left + 26 + (40 - tw) / 2, frame.top + 3 + (30 - th) / 2);
        owner->DrawBitmap(fThumb, tp);
        owner->SetHighColor(60, 60, 60);
        owner->StrokeRect(BRect(tp.x - 1, tp.y - 1, tp.x + tw + 1, tp.y + th + 1));
        owner->SetHighColor(IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR) : ui_color(B_LIST_ITEM_TEXT_COLOR));
        owner->SetLowColor(bg);
        owner->DrawString(fName.c_str(), BPoint(frame.left + 74, frame.top + 23));
    }

private:
    std::string fName;
    bool fVisible;
    BBitmap *fThumb;
};
class LayerList : public BListView {
public:
    LayerList() : BListView("layers", B_SINGLE_SELECTION_LIST) {}
    void MouseDown(BPoint where) override {
        const int32 index = IndexOf(where);
        if (index >= 0 && where.x < 24) { // click on the checkbox toggles visibility
            BMessage m(MSG_LAYER_VIS);
            m.AddInt32("id", static_cast<LayerItem *>(ItemAt(index))->id);
            Window()->PostMessage(&m);
            return;
        }
        fDragRow = index; fDownY = where.y; fDropGap = -1;
        SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
        BListView::MouseDown(where);
    }
    // drag a row up or down to change the layer order
    void MouseMoved(BPoint where, uint32 transit, const BMessage *drag) override {
        int32 buttons = 0;
        if (BMessage *m = Window()->CurrentMessage()) m->FindInt32("buttons", &buttons);
        if (fDragRow < 0 || !buttons) { BListView::MouseMoved(where, transit, drag); return; }
        if (fDropGap < 0 && fabs(where.y - fDownY) < 5) return;
        int gap = 0;
        for (int32 i = 0; i < CountItems(); i++) { const BRect f = ItemFrame(i); if (where.y > (f.top + f.bottom) / 2) gap = i + 1; }
        if (gap != fDropGap) { fDropGap = gap; Invalidate(); }
    }
    void MouseUp(BPoint where) override {
        const int row = fDragRow, gap = fDropGap;
        fDragRow = fDropGap = -1;
        Invalidate();
        if (row >= 0 && gap >= 0 && gap != row && gap != row + 1) {
            BMessage m(MSG_LAYER_MOVE);
            m.AddInt32("row", row);
            m.AddInt32("gap", gap);
            Window()->PostMessage(&m);
        }
        BListView::MouseUp(where);
    }
    void Draw(BRect update) override {
        BListView::Draw(update);
        if (fDropGap < 0 || CountItems() == 0) return; // where the dragged layer will land
        const float y = fDropGap < CountItems() ? ItemFrame(fDropGap).top : ItemFrame(CountItems() - 1).bottom + 1;
        SetHighColor(45, 127, 249);
        FillRect(BRect(0, y - 1, Bounds().right, y + 1));
    }

private:
    int fDragRow = -1, fDropGap = -1;
    float fDownY = 0;
};

// ---------- main window ----------
struct OptGroup { BView *view; std::vector<Tool> tools; bool xfOnly; bool hidden; };

class MainWindow : public BWindow {
public:
    MainWindow();
    ~MainWindow() { delete fOpenPanel; delete fSavePanel; }
    void MessageReceived(BMessage *msg) override;
    bool QuitRequested() override {
        if (!ConfirmDiscard()) return false;
        be_app->PostMessage(B_QUIT_REQUESTED);
        return true;
    }
    void OpenPath(const char *path, bool asLayer);
    void SelfTest(const char *dir);

    Editor ed;
    Tools tools;
    CanvasView *canvas;

private:
    ToolBar *fToolBar;
    IconLabel *fToolIcon;
    std::vector<OptGroup> fOpts;
    std::map<std::string, BSlider *> fSliders;
    std::vector<IconButton *> fModeButtons;
    Swatch *fFg, *fBg;
    LayerList *fLayers;
    BMenuField *fBlend;
    BSlider *fOpacity;
    BStringView *fStatus, *fInfo;
    BFilePanel *fOpenPanel = NULL, *fSavePanel = NULL;
    bool fRefreshing = false, fAdjusting = false, fSaveIsExport = false;
    int fLastW = 0, fLastH = 0;
    std::string fExportExt;
    Image fAdjSrc, fAdjSel;
    std::vector<LayerItem *> fItems;

    void BuildMenus(BMenuBar *bar);
    BView *BuildOptions();
    BView *BuildPanel();
    void Run(int cmd);
    void SetTool(Tool t);
    void Refresh();
    void UpdateOptions();
    void UpdateStatus();
    bool ConfirmDiscard();
    void Form(const char *title, int32 id, const std::vector<Field> &fields, bool live = false, double aspect = 0) {
        (new FormWindow(title, id, fields, BMessenger(this), live, aspect))->Show();
    }
    void HandleForm(BMessage *msg, bool live);
    void HandleOpt(BMessage *msg);
    void Save(BMessage *msg);
    void Copy(bool cut, bool merged);
    void Paste();
    void ShowOpen(bool asLayer);
    void ShowSave(const char *name);
    ColorMat AdjustMatrix(BMessage *msg) {
        return colorMatrix(msg->GetInt32("hue", 0), msg->GetInt32("sat", 0), msg->GetInt32("bri", 0), msg->GetInt32("con", 0));
    }
};

MainWindow::MainWindow()
    : BWindow(BRect(60, 60, 1260, 860), "R Painter", B_DOCUMENT_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS), tools(&ed) {
    const BRect screen = BScreen(this).Frame();
    ResizeTo(std::min(1440.0f, screen.Width() - 80), std::min(900.0f, screen.Height() - 100));
    CenterOnScreen();
    SetPulseRate(100000);

    BMenuBar *bar = new BMenuBar("menu");
    BuildMenus(bar);
    canvas = new CanvasView(&ed, &tools);
    fToolBar = new ToolBar(&tools);
    fStatus = new BStringView("status", TR("Space+드래그: 화면 이동 / Alt+휠: 확대 / 선택 도구 Shift: 추가, Ctrl: 빼기"));
    fInfo = new BStringView("info", "");
    fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .Add(bar)
        .Add(BuildOptions())
        .AddGroup(B_HORIZONTAL, 0, 1.0f).Add(fToolBar).Add(canvas, 1.0f).Add(BuildPanel()).End()
        .AddGroup(B_HORIZONTAL, 8).SetInsets(8, 2, 8, 2).Add(fStatus, 1.0f).Add(fInfo).End();

    ed.onChange = [this] { PostMessage(MSG_REFRESH); };
    ed.onDamage = [this](IRect r) { canvas->Damage(r); };
    ed.onView = [this] { canvas->Invalidate(); };
    ed.onToast = [this](const std::string &s) { fStatus->SetText(s.c_str()); };
    tools.onDamage = [this](IRect r) { canvas->Damage(r); };
    tools.onMode = [this] { UpdateOptions(); canvas->UpdateCursor(); canvas->Invalidate(); };
    tools.onColors = [this] { fFg->SetColor(ed.fg); fBg->SetColor(ed.bg); };
    tools.onOverlay = [this] { canvas->Invalidate(); };
    tools.onZoom = [this](PtF p, double f) { canvas->ZoomAt(BPoint(float(canvas->off.x + p.x * canvas->zoom), float(canvas->off.y + p.y * canvas->zoom)), f); };
    tools.onFit = [this] { canvas->Fit(); };

    ed.newDoc(1200, 800, true);
    tools.resetCrop();
    SetTool(Tool::Brush);
    Refresh();
    PostMessage('fit1'); // handled once the window is shown and laid out
}
static BMenuItem *item(const char *label, int cmd, char key = 0, uint32 mods = 0) { return new BMenuItem(label, cmdMsg(cmd), key, mods); }
void MainWindow::BuildMenus(BMenuBar *bar) {
    BMenu *m = new BMenu(TR("파일"));
    m->AddItem(item(TR("새로 만들기..."), C_NEW, 'N'));
    m->AddItem(item(TR("열기..."), C_OPEN, 'O'));
    m->AddItem(item(TR("레이어로 가져오기..."), C_PLACE));
    m->AddSeparatorItem();
    m->AddItem(item(TR("내보내기 (PNG / JPG / WebP)..."), C_EXPORT, 'S', B_SHIFT_KEY | B_CONTROL_KEY));
    m->AddItem(item(TR("저장 (.rpaint 프로젝트)..."), C_SAVE, 'S'));
    m->AddSeparatorItem();
    m->AddItem(item(TR("종료"), C_QUIT, 'Q'));
    bar->AddItem(m);

    m = new BMenu(TR("편집"));
    m->AddItem(item(TR("실행 취소"), C_UNDO, 'Z'));
    m->AddItem(item(TR("다시 실행"), C_REDO, 'Z', B_SHIFT_KEY));
    m->AddSeparatorItem();
    m->AddItem(item(TR("잘라내기"), C_CUT, 'X'));
    m->AddItem(item(TR("복사"), C_COPY, 'C'));
    m->AddItem(item(TR("병합하여 복사"), C_COPY_MERGED, 'C', B_SHIFT_KEY));
    m->AddItem(item(TR("붙여넣기"), C_PASTE, 'V'));
    m->AddItem(item(TR("지우기 (Delete)"), C_DELETE));
    m->AddSeparatorItem();
    m->AddItem(item(TR("전경색으로 채우기 (Ctrl+Backspace)"), C_FILL_FG));
    m->AddItem(item(TR("배경색으로 채우기 (Shift+Backspace)"), C_FILL_BG));
    m->AddSeparatorItem();
    m->AddItem(item(TR("자유 변형 (크기 조절 / 회전)"), C_TRANSFORM, 'T'));
    m->AddSeparatorItem();
    m->AddItem(item(TR("전경색 선택..."), C_PICK_FG, 'K'));
    m->AddItem(item(TR("배경색 선택..."), C_PICK_BG, 'K', B_SHIFT_KEY));
    bar->AddItem(m);

    m = new BMenu(TR("이미지"));
    m->AddItem(item(TR("색조 / 채도 / 밝기 / 대비..."), C_ADJUST, 'U'));
    m->AddItem(item(TR("색상 반전"), C_INVERT, 'I'));
    m->AddItem(item(TR("흑백 (채도 감소)"), C_GRAYSCALE, 'U', B_SHIFT_KEY));
    m->AddSeparatorItem();
    m->AddItem(item(TR("이미지 크기..."), C_IMAGE_SIZE, 'I', B_CONTROL_KEY));
    m->AddItem(item(TR("캔버스 크기..."), C_CANVAS_SIZE, 'C', B_CONTROL_KEY));
    m->AddItem(item(TR("선택 영역으로 자르기"), C_CROP_SEL));
    m->AddSeparatorItem();
    m->AddItem(item(TR("시계 방향 90도 회전"), C_ROT_CW));
    m->AddItem(item(TR("반시계 방향 90도 회전"), C_ROT_CCW));
    m->AddItem(item(TR("180도 회전"), C_ROT_180));
    m->AddItem(item(TR("가로로 뒤집기"), C_FLIP_H));
    m->AddItem(item(TR("세로로 뒤집기"), C_FLIP_V));
    bar->AddItem(m);

    m = new BMenu(TR("레이어"));
    m->AddItem(item(TR("새 레이어"), C_LAYER_NEW, 'N', B_SHIFT_KEY));
    m->AddItem(item(TR("레이어 복제"), C_LAYER_DUP));
    m->AddItem(item(TR("선택 영역을 새 레이어로 복사"), C_LAYER_COPY, 'J'));
    m->AddItem(item(TR("선택 영역을 새 레이어로 잘라내기"), C_LAYER_CUT, 'J', B_SHIFT_KEY));
    m->AddItem(item(TR("레이어 삭제"), C_LAYER_DEL));
    m->AddSeparatorItem();
    m->AddItem(item(TR("위로 이동"), C_LAYER_UP, ']'));
    m->AddItem(item(TR("아래로 이동"), C_LAYER_DOWN, '['));
    m->AddItem(item(TR("위 레이어 선택"), C_LAYER_SEL_UP, ']', B_CONTROL_KEY));
    m->AddItem(item(TR("아래 레이어 선택"), C_LAYER_SEL_DOWN, '[', B_CONTROL_KEY));
    m->AddSeparatorItem();
    m->AddItem(item(TR("아래 레이어와 병합"), C_MERGE_DOWN, 'E'));
    m->AddItem(item(TR("배경으로 병합 (전체)"), C_FLATTEN, 'E', B_SHIFT_KEY));
    m->AddSeparatorItem();
    m->AddItem(item(TR("레이어 가로 뒤집기"), C_LAYER_FLIP_H));
    m->AddItem(item(TR("레이어 세로 뒤집기"), C_LAYER_FLIP_V));
    bar->AddItem(m);

    m = new BMenu(TR("선택"));
    m->AddItem(item(TR("전체 선택"), C_SEL_ALL, 'A'));
    m->AddItem(item(TR("선택 해제"), C_SEL_NONE, 'D'));
    m->AddItem(item(TR("다시 선택"), C_RESELECT, 'D', B_SHIFT_KEY));
    m->AddItem(item(TR("선택 반전"), C_SEL_INVERT, 'I', B_SHIFT_KEY));
    m->AddItem(item(TR("레이어 픽셀 선택"), C_SEL_LAYER));
    m->AddSeparatorItem();
    m->AddItem(item(TR("페더..."), C_SEL_FEATHER));
    m->AddItem(item(TR("확장..."), C_SEL_EXPAND));
    m->AddItem(item(TR("축소..."), C_SEL_CONTRACT));
    bar->AddItem(m);

    m = new BMenu(TR("보기"));
    m->AddItem(item(TR("확대"), C_ZOOM_IN, '+'));
    m->AddItem(item(TR("축소"), C_ZOOM_OUT, '-'));
    m->AddItem(item(TR("화면에 맞추기"), C_FIT, '0'));
    m->AddItem(item("100%", C_ZOOM_100, '1'));
    bar->AddItem(m);
    AddShortcut('=', B_COMMAND_KEY, cmdMsg(C_ZOOM_IN));
    AddShortcut('Y', B_COMMAND_KEY, cmdMsg(C_REDO));
}
BView *MainWindow::BuildOptions() {
    BGroupView *bar = new BGroupView(B_HORIZONTAL, 4);
    bar->GroupLayout()->SetInsets(6, 3, 6, 3);
    fToolIcon = new IconLabel(TOOLS[0].icon, "");
    bar->AddChild(fToolIcon);
    auto add = [this, bar](BView *v, std::vector<Tool> t, bool xfOnly = false) { bar->AddChild(v); fOpts.push_back({v, t, xfOnly, false}); };
    auto slider = [this, add](const char *icon, const char *tip, const char *key, int min, int max, int value, std::vector<Tool> t) {
        BMessage *m = new BMessage(MSG_OPT);
        m->AddString("key", key);
        BSlider *s = new BSlider(key, NULL, m, min, max, B_HORIZONTAL);
        s->SetModificationMessage(new BMessage(*m));
        s->SetValue(value);
        s->SetExplicitMinSize(BSize(120, B_SIZE_UNSET));
        s->SetExplicitMaxSize(BSize(120, B_SIZE_UNSET));
        s->SetToolTip(tip);
        BGroupView *g = new BGroupView(B_HORIZONTAL, 2);
        g->AddChild(new IconLabel(icon, tip));
        g->AddChild(s);
        BStringView *val = new BStringView("value", "");
        val->SetExplicitMinSize(BSize(34, B_SIZE_UNSET));
        g->AddChild(val);
        fSliders[key] = s;
        add(g, t);
    };
    auto toggle = [this, add](const char *icon, const char *tip, const char *key, bool on, std::vector<Tool> t) {
        BMessage *m = new BMessage(MSG_OPT);
        m->AddString("toggle", key);
        IconButton *b = new IconButton(icon, tip, m, true);
        b->SetValue(on);
        add(b, t);
    };
    auto button = [this, add](const char *icon, const char *tip, int cmd, std::vector<Tool> t, bool xfOnly = false) { add(new IconButton(icon, tip, cmdMsg(cmd)), t, xfOnly); };
    const std::vector<Tool> paint = {Tool::Brush, Tool::Eraser}, sel = {Tool::Rect, Tool::Ellipse, Tool::Lasso, Tool::PolyLasso, Tool::Wand};
    const char *modeIcons[4] = {"R5 5 14 14", "M4 4 L15 4 L15 9 L20 9 L20 20 L9 20 L9 15 L4 15 Z", "M4 4 L15 4 L15 9 L9 9 L9 15 L4 15 Z M13 18 L20 18", "R4 4 11 11 R9 9 11 11 M10.5 13.5 L13.5 10.5 M12 15 L15 12"};
    const char *modeTips[4] = {TR("새 선택 영역"), TR("선택 영역에 추가 (Shift+드래그)"), TR("선택 영역에서 빼기 (Ctrl+드래그)"), TR("선택 영역과 교차 (Shift+Ctrl+드래그)")};
    for (int i = 0; i < 4; i++) {
        BMessage *m = new BMessage(MSG_SELMODE);
        m->AddInt32("mode", i);
        IconButton *b = new IconButton(modeIcons[i], modeTips[i], m);
        b->SetValue(i == 0);
        fModeButtons.push_back(b);
        add(b, sel);
    }
    slider("O7 15 3 3 O16 11 6 6", TR("크기 ( [ / ] )"), "size", 1, 500, tools.opt.size, paint);
    slider("O12 12 9 9 O12 12 5 5 O12 12 1.5 1.5", TR("경도 (가장자리 선명도)"), "hardness", 0, 100, tools.opt.hardness, paint);
    slider("M4 8 L10 8 M7 5 L7 11 M14 16 L20 16 M18 4 L6 20", TR("허용치 (색 차이 범위)"), "tolerance", 0, 255, tools.opt.tolerance, {Tool::Bucket, Tool::Wand});
    slider("M12 3 C12 3 5 11 5 15 C5 19 8 21 12 21 C16 21 19 19 19 15 C19 11 12 3 12 3 Z M9 15 C9 17 10 18 12 18", TR("불투명도"), "opacity", 1, 100, tools.opt.opacity, {Tool::Brush, Tool::Eraser, Tool::Bucket, Tool::Gradient});
    toggle("R4 8 8 8 R12 8 8 8", TR("인접 픽셀만"), "contiguous", tools.opt.contiguous, {Tool::Bucket, Tool::Wand});
    toggle("M12 4 L20 8 L12 12 L4 8 Z M4 12 L12 16 L20 12 M4 16 L12 20 L20 16", TR("모든 레이어 샘플링"), "sampleAll", tools.opt.sampleAll, {Tool::Bucket, Tool::Wand});
    toggle("M4 20 L4 14 L10 14 L10 8 L16 8 L16 4 M8 20 C14 20 20 14 20 8", TR("앤티앨리어스 (가장자리 부드럽게)"), "antiAlias", tools.opt.antiAlias, {Tool::Wand});
    toggle("R4 4 16 16 M4 20 L20 4 M12 20 L20 12 M4 12 L12 4", TR("전경색 -> 투명"), "toTransparent", tools.opt.toTransparent, {Tool::Gradient});
    slider("M5 19 L14 10 M19 5 C12 5 8 9 8 16 C15 16 19 12 19 5 Z", TR("페더 (선택 가장자리 흐리게)"), "feather", 0, 100, tools.opt.feather, sel);
    button("R4 4 16 16 M8 16 L16 8 M16 8 L12 8 M16 8 L16 12 M8 16 L12 16 M8 16 L8 12", TR("선택 반전"), C_SEL_INVERT, sel);
    button(ICON_TRASH, TR("선택 영역 삭제"), C_DELETE, sel);
    button("R4 8 12 12 M8 8 L8 4 L20 4 L20 16 L16 16 M10 11 L10 17 M7 14 L13 14", TR("선택 영역을 새 레이어로 복사"), C_LAYER_COPY, sel);
    button(ICON_XF, TR("자유 변형 - 크기 조절 / 회전"), C_TRANSFORM, {Tool::Move});
    button(ICON_OK, TR("자르기 적용 (Enter)"), C_CROP_APPLY, {Tool::Crop});
    button(ICON_X, TR("자르기 초기화 (Esc)"), C_CROP_RESET, {Tool::Crop});
    button(ICON_OK, TR("변형 적용 (Enter)"), C_XF_APPLY, {}, true);
    button(ICON_X, TR("변형 취소 (Esc)"), C_XF_CANCEL, {}, true);
    bar->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
    bar->SetExplicitMinSize(BSize(B_SIZE_UNSET, 33));
    return bar;
}
BView *MainWindow::BuildPanel() {
    fFg = new Swatch(ed.fg, 40, 40, cmdMsg(C_PICK_FG));
    fBg = new Swatch(ed.bg, 40, 40, cmdMsg(C_PICK_BG));
    fFg->SetToolTip(TR("전경색"));
    fBg->SetToolTip(TR("배경색"));
    BGridView *palette = new BGridView(2, 2);
    static const Px PALETTE[24] = {0x000000, 0x444444, 0x888888, 0xbbbbbb, 0xffffff, 0xe53935, 0xfb8c00, 0xfdd835, 0x43a047, 0x00acc1, 0x1e88e5, 0x8e24aa,
        0x6d4c41, 0xf06292, 0xffb74d, 0xfff176, 0xaed581, 0x4dd0e1, 0x64b5f6, 0xba68c8, 0x3e2723, 0xb71c1c, 0x1b5e20, 0x0d47a1};
    for (int i = 0; i < 24; i++) {
        BMessage *m = new BMessage(MSG_PALETTE);
        m->AddInt32("rgb", int32(PALETTE[i]));
        palette->GridLayout()->AddView(new Swatch(PALETTE[i], 17, 17, m), i % 12, i / 12);
    }
    BPopUpMenu *blendMenu = new BPopUpMenu("blend");
    for (size_t i = 0; i < blendModes().size(); i++) {
        BMessage *m = new BMessage(MSG_BLEND);
        m->AddInt32("index", int32(i));
        blendMenu->AddItem(new BMenuItem(TR(blendModes()[i].label), m));
    }
    fBlend = new BMenuField("blend", NULL, blendMenu);
    BMessage *om = new BMessage(MSG_LAYER_OPACITY);
    om->AddBool("done", true);
    fOpacity = new BSlider("opacity", TR("불투명도"), om, 0, 100, B_HORIZONTAL);
    fOpacity->SetModificationMessage(new BMessage(MSG_LAYER_OPACITY));
    fLayers = new LayerList();
    fLayers->SetSelectionMessage(new BMessage(MSG_LAYER_PICK));
    fLayers->SetInvocationMessage(new BMessage(MSG_LAYER_RENAME));
    fLayers->SetToolTip(TR("체크 상자: 표시/숨김, 드래그: 순서 변경, 더블클릭: 이름 변경"));
    BScrollView *scroll = new BScrollView("scroll", fLayers, 0, false, true);

    auto title = [](const char *text) {
        BStringView *v = new BStringView("title", text);
        BFont font(be_bold_font);
        v->SetFont(&font);
        return v;
    };
    BGroupView *panel = new BGroupView(B_VERTICAL, 6);
    BLayoutBuilder::Group<>(panel).SetInsets(8, 6, 8, 8)
        .Add(title(TR("색상")))
        .AddGroup(B_HORIZONTAL, 6).Add(fFg).Add(fBg).Add(new IconButton("M5 9 L18 9 M15 6 L18 9 L15 12 M19 15 L6 15 M9 12 L6 15 L9 18", TR("전경색 / 배경색 전환 (X)"), cmdMsg(C_SWAP))).AddGlue().End()
        .Add(palette)
        .Add(new BSeparatorView(B_HORIZONTAL))
        .Add(title(TR("레이어")))
        .Add(fBlend)
        .Add(fOpacity)
        .Add(scroll, 1.0f)
        .AddGroup(B_HORIZONTAL, 3)
            .Add(new IconButton("R5 5 14 14 M12 8 L12 16 M8 12 L16 12", TR("새 레이어"), cmdMsg(C_LAYER_NEW)))
            .Add(new IconButton("R9 9 11 11 M5 15 L5 5 L15 5", TR("레이어 복제"), cmdMsg(C_LAYER_DUP)))
            .Add(new IconButton("M4 5 L20 5 M4 19 L20 19 M12 8 L12 15 M9 12 L12 15 L15 12", TR("아래 레이어와 병합"), cmdMsg(C_MERGE_DOWN)))
            .Add(new IconButton(ICON_TRASH, TR("레이어 삭제"), cmdMsg(C_LAYER_DEL)))
            .AddGlue()
        .End();
    panel->SetExplicitMinSize(BSize(250, B_SIZE_UNSET));
    panel->SetExplicitMaxSize(BSize(250, B_SIZE_UNLIMITED));
    return panel;
}

// ---------- state -> UI ----------
void MainWindow::SetTool(Tool t) {
    if (tools.drag) return;
    tools.setTool(t);
    fToolBar->Invalidate();
    UpdateOptions();
    canvas->UpdateCursor();
    canvas->Invalidate();
}
void MainWindow::UpdateOptions() {
    const bool xf = tools.xf.on;
    const ToolInfo &t = TOOLS[int(tools.tool)];
    fToolIcon->SetSpec(xf ? ICON_XF : t.icon, xf ? TR("자유 변형 - 모서리: 비율 유지 (Shift: 자유 비율, Ctrl: 중심 기준), 안쪽 드래그: 이동, 바깥쪽 드래그: 회전 (Shift: 15도 단위), Enter: 적용, Esc: 취소") : TR(t.label));
    for (OptGroup &o : fOpts) {
        bool show = xf ? o.xfOnly : !o.xfOnly;
        if (show && !o.xfOnly) { show = false; for (Tool tt : o.tools) if (tt == tools.tool) show = true; }
        // own flag: BView::IsHidden() is also true while the window itself is not shown yet
        if (show && o.hidden) { o.view->Show(); o.hidden = false; }
        if (!show && !o.hidden) { o.view->Hide(); o.hidden = true; }
    }
    for (size_t i = 0; i < fModeButtons.size(); i++) fModeButtons[i]->SetValue(int(i) == tools.opt.selMode);
    const std::pair<const char *, int> vals[] = {{"size", tools.opt.size}, {"hardness", tools.opt.hardness}, {"tolerance", tools.opt.tolerance}, {"opacity", tools.opt.opacity}, {"feather", tools.opt.feather}};
    for (const auto &v : vals) {
        BSlider *s = fSliders[v.first];
        if (s->Value() != v.second) s->SetValue(v.second);
        char buf[16];
        snprintf(buf, sizeof buf, "%d", v.second);
        if (BStringView *label = dynamic_cast<BStringView *>(s->Parent()->FindView("value"))) label->SetText(buf);
    }
}
void MainWindow::Refresh() {
    fRefreshing = true;
    fLayers->MakeEmpty(); // the list does not own its items
    for (LayerItem *it : fItems) delete it;
    fItems.clear();
    int selIndex = -1;
    for (int i = int(ed.d.layers.size()) - 1; i >= 0; i--) {
        LayerItem *it = new LayerItem(ed.d.layers[i], ed.d.w, ed.d.h);
        fItems.push_back(it);
        fLayers->AddItem(it);
        if (ed.d.layers[i].id == ed.d.activeId) selIndex = fLayers->CountItems() - 1;
    }
    if (selIndex >= 0) fLayers->Select(selIndex);
    if (Layer *l = ed.active()) {
        for (size_t i = 0; i < blendModes().size(); i++)
            if (blendModes()[i].mode == l->blend) if (BMenuItem *mi = fBlend->Menu()->ItemAt(int32(i))) mi->SetMarked(true);
        fOpacity->SetValue(int32(lround(l->opacity * 100)));
    }
    fRefreshing = false;
    if (fLastW != ed.d.w || fLastH != ed.d.h) { // canvas size changed (crop, resize, undo...): show all of it again
        if (fLastW) canvas->Fit();
        fLastW = ed.d.w; fLastH = ed.d.h;
    }
    fFg->SetColor(ed.fg);
    fBg->SetColor(ed.bg);
    UpdateStatus();
}
void MainWindow::UpdateStatus() {
    char buf[128];
    snprintf(buf, sizeof buf, "%d%%   %d x %d px   X %d, Y %d", int(lround(canvas->zoom * 100)), ed.d.w, ed.d.h, int(floor(tools.mouseDoc.x)), int(floor(tools.mouseDoc.y)));
    fInfo->SetText(buf);
}
bool MainWindow::ConfirmDiscard() {
    if (!ed.modified) return true;
    BAlert *alert = new BAlert("R Painter", TR("저장하지 않은 변경 사항이 있습니다. 계속할까요?"), TR("취소"), TR("계속"), NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
    return alert->Go() == 1;
}

// ---------- files, clipboard ----------
void MainWindow::OpenPath(const char *path, bool asLayer) {
    const std::string base = baseName(path);
    bool ok = false;
    if (endsWith(path, ".rpaint")) {
        ok = ed.loadProjectJson(readFile(path), decodeImage);
        if (ok) ed.name = base;
    } else {
        const Image im = loadImageFile(path);
        if (!im.null() && (asLayer || (im.w <= MAX_DIM && im.h <= MAX_DIM))) {
            if (asLayer) ed.placeImage(im, base); else ed.openImage(im, base);
            ok = true;
        }
    }
    if (!ok) { (new BAlert("R Painter", (std::string(TR("파일을 열 수 없습니다: ")) + path).c_str(), TR("확인")))->Go(); return; }
    if (!asLayer) { tools.resetCrop(); canvas->Fit(); }
}
void MainWindow::ShowOpen(bool asLayer) {
    delete fOpenPanel;
    fOpenPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL, B_FILE_NODE, false, new BMessage(asLayer ? MSG_PLACE_REFS : MSG_OPEN_REFS));
    fOpenPanel->Show();
}
void MainWindow::ShowSave(const char *name) {
    delete fSavePanel;
    fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL, B_FILE_NODE, false, new BMessage(MSG_SAVE_REFS));
    fSavePanel->SetSaveText(name);
    fSavePanel->Show();
}
void MainWindow::Save(BMessage *msg) {
    entry_ref dir;
    const char *name = NULL;
    if (msg->FindRef("directory", &dir) != B_OK || msg->FindString("name", &name) != B_OK) return;
    BPath path(&dir);
    path.Append(name);
    std::string p = path.Path();
    bool ok = false;
    if (fSaveIsExport) {
        if (!endsWith(p, ("." + fExportExt).c_str())) p += "." + fExportExt;
        const uint32 format = fExportExt == "jpg" ? B_JPEG_FORMAT : fExportExt == "webp" ? B_WEBP_FORMAT : B_PNG_FORMAT;
        BFile file(p.c_str(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        ok = file.InitCheck() == B_OK && translateImage(ed.flatten(), &file, format) == B_OK;
        if (ok) BNodeInfo(&file).SetType(fExportExt == "jpg" ? "image/jpeg" : fExportExt == "webp" ? "image/webp" : "image/png");
    } else {
        if (!endsWith(p, ".rpaint")) p += ".rpaint";
        ok = writeFile(p.c_str(), ed.projectJson(encodePng), "application/json");
        if (ok) ed.modified = false;
    }
    if (ok) fStatus->SetText((std::string(TR("저장됨: ")) + p).c_str());
    else (new BAlert("R Painter", (std::string(TR("저장하지 못했습니다: ")) + p).c_str(), TR("확인")))->Go();
}
void MainWindow::Copy(bool cut, bool merged) {
    if (!ed.copySel(cut, merged) || !be_clipboard->Lock()) return;
    be_clipboard->Clear();
    if (BMessage *clip = be_clipboard->Data()) {
        BBitmap *bmp = bitmapFromImage(ed.clip);
        BMessage archive;
        if (bmp->Archive(&archive) == B_OK) clip->AddMessage("image/bitmap", &archive);
        delete bmp;
        be_clipboard->Commit();
    }
    be_clipboard->Unlock();
}
void MainWindow::Paste() {
    Image im;
    if (be_clipboard->Lock()) {
        BMessage archive;
        if (BMessage *clip = be_clipboard->Data()) {
            if (clip->FindMessage("image/bitmap", &archive) == B_OK) {
                BBitmap bmp(&archive);
                im = imageFromBitmap(&bmp);
            }
        }
        be_clipboard->Unlock();
    }
    // our own copy comes back with the same size: paste it in place
    if (!ed.clip.null() && (im.null() || (im.w == ed.clip.w && im.h == ed.clip.h))) ed.pasteImage(ed.clip, ed.clipPos);
    else if (!im.null()) ed.placeImage(im, TR("붙여넣기"));
    else ed.toast(TR("클립보드가 비어 있습니다"));
}

// ---------- commands ----------
void MainWindow::Run(int cmd) {
    const bool viewCmd = cmd == C_ZOOM_IN || cmd == C_ZOOM_OUT || cmd == C_FIT || cmd == C_ZOOM_100 || cmd == C_XF_APPLY || cmd == C_XF_CANCEL
        || cmd == C_SWAP || cmd == C_DEFAULT_COLORS || cmd == C_PICK_FG || cmd == C_PICK_BG || cmd == C_SIZE_DOWN || cmd == C_SIZE_UP || cmd == C_HARD_DOWN || cmd == C_HARD_UP;
    if (tools.drag) return;
    if (tools.xf.on && !viewCmd) tools.commitXf();
    if (!viewCmd && cmd != C_CROP_APPLY && cmd != C_CROP_RESET) tools.cancelPoly();
    auto dims = [this]() { return std::vector<Field>{{Field::Int, "w", TR("너비 (px)"), 1, MAX_DIM, ed.d.w, {}, ""}, {Field::Int, "h", TR("높이 (px)"), 1, MAX_DIM, ed.d.h, {}, ""}}; };
    switch (cmd) {
    case C_NEW:
        Form(TR("새로 만들기"), FORM_NEW, {{Field::Int, "w", TR("너비 (px)"), 1, MAX_DIM, 1200, {}, ""}, {Field::Int, "h", TR("높이 (px)"), 1, MAX_DIM, 800, {}, ""},
                                       {Field::Choice, "bg", TR("배경"), 0, 0, 0, {TR("흰색"), TR("투명"), TR("배경색")}, ""}});
        break;
    case C_OPEN: if (ConfirmDiscard()) ShowOpen(false); break;
    case C_PLACE: ShowOpen(true); break;
    case C_EXPORT: Form(TR("내보내기"), FORM_EXPORT, {{Field::Choice, "fmt", TR("형식"), 0, 0, 0, {TR("PNG (투명 지원)"), "JPG", "WebP"}, ""}}); break;
    case C_SAVE: fSaveIsExport = false; ShowSave((ed.name + ".rpaint").c_str()); break;
    case C_QUIT: PostMessage(B_QUIT_REQUESTED); break;
    case C_UNDO: ed.undo(); break;
    case C_REDO: ed.redo(); break;
    case C_CUT: Copy(true, false); break;
    case C_COPY: Copy(false, false); break;
    case C_COPY_MERGED: Copy(false, true); break;
    case C_PASTE: Paste(); break;
    case C_DELETE: ed.deleteSel(); break;
    case C_FILL_FG: ed.fill(ed.fg); break;
    case C_FILL_BG: ed.fill(ed.bg); break;
    case C_TRANSFORM: tools.startTransform(); break;
    case C_ADJUST:
        if (Layer *l = ed.editable()) {
            fAdjSrc = l->img; fAdjSel = ed.d.sel; fAdjusting = true;
            Form(TR("색조 / 채도 / 밝기 / 대비"), FORM_ADJUST, {{Field::Slider, "hue", TR("색조 (Hue)"), -180, 180, 0, {}, ""}, {Field::Slider, "sat", TR("채도"), -100, 100, 0, {}, ""},
                {Field::Slider, "bri", TR("밝기"), -100, 100, 0, {}, ""}, {Field::Slider, "con", TR("대비"), -100, 100, 0, {}, ""},
                {Field::Note, "note", ed.hasSel() ? TR("선택 영역에만 적용됩니다") : TR("현재 레이어 전체에 적용됩니다"), 0, 0, 0, {}, ""}}, true);
        }
        break;
    case C_INVERT: ed.quickColor(invertMatrix()); break;
    case C_GRAYSCALE: ed.quickColor(colorMatrix(0, -100, 0, 0)); break;
    case C_IMAGE_SIZE: {
        std::vector<Field> f = dims();
        f.push_back({Field::Check, "lock", TR("비율 유지"), 0, 1, 1, {}, ""});
        Form(TR("이미지 크기"), FORM_IMAGE_SIZE, f, false, double(ed.d.w) / ed.d.h);
        break;
    }
    case C_CANVAS_SIZE: {
        std::vector<Field> f = dims();
        f.push_back({Field::Choice, "anchor", TR("기준점"), 0, 0, 4, {TR("왼쪽 위"), TR("위"), TR("오른쪽 위"), TR("왼쪽"), TR("가운데"), TR("오른쪽"), TR("왼쪽 아래"), TR("아래"), TR("오른쪽 아래")}, ""});
        Form(TR("캔버스 크기"), FORM_CANVAS_SIZE, f);
        break;
    }
    case C_CROP_SEL: ed.cropToSel(); canvas->Fit(); break;
    case C_ROT_CW: ed.transformDoc(DocXf::RotCW); canvas->Fit(); break;
    case C_ROT_CCW: ed.transformDoc(DocXf::RotCCW); canvas->Fit(); break;
    case C_ROT_180: ed.transformDoc(DocXf::Rot180); break;
    case C_FLIP_H: ed.transformDoc(DocXf::FlipH); break;
    case C_FLIP_V: ed.transformDoc(DocXf::FlipV); break;
    case C_LAYER_NEW: ed.newLayer(); break;
    case C_LAYER_DUP: ed.duplicateLayer(); break;
    case C_LAYER_COPY: ed.layerViaCopy(false); break;
    case C_LAYER_CUT: ed.layerViaCopy(true); break;
    case C_LAYER_DEL: ed.deleteLayer(); break;
    case C_LAYER_UP: ed.moveLayer(1); break;
    case C_LAYER_DOWN: ed.moveLayer(-1); break;
    case C_LAYER_SEL_UP: case C_LAYER_SEL_DOWN: {
        const int j = ed.activeIdx() + (cmd == C_LAYER_SEL_UP ? 1 : -1);
        if (j >= 0 && j < int(ed.d.layers.size())) { ed.d.activeId = ed.d.layers[j].id; Refresh(); }
        break;
    }
    case C_MERGE_DOWN: ed.mergeDown(); break;
    case C_FLATTEN: ed.flattenAll(); break;
    case C_LAYER_FLIP_H: ed.layerFlip(DocXf::FlipH); break;
    case C_LAYER_FLIP_V: ed.layerFlip(DocXf::FlipV); break;
    case C_SEL_ALL: ed.selectAll(); break;
    case C_SEL_NONE: ed.selectNone(); break;
    case C_RESELECT: ed.reselect(); break;
    case C_SEL_INVERT: ed.invertSel(); break;
    case C_SEL_LAYER: ed.selectLayerPixels(); break;
    case C_SEL_FEATHER: case C_SEL_EXPAND: case C_SEL_CONTRACT:
        if (!ed.hasSel()) { ed.toast(TR("선택 영역이 없습니다")); break; }
        Form(cmd == C_SEL_FEATHER ? TR("페더 (가장자리 흐리게)") : cmd == C_SEL_EXPAND ? TR("선택 영역 확장") : TR("선택 영역 축소"),
             cmd == C_SEL_FEATHER ? FORM_FEATHER : cmd == C_SEL_EXPAND ? FORM_EXPAND : FORM_CONTRACT, {{Field::Int, "r", TR("반경 (px)"), 1, 100, cmd == C_SEL_FEATHER ? 5 : 2, {}, ""}});
        break;
    case C_ZOOM_IN: canvas->ZoomCenter(1.25); break;
    case C_ZOOM_OUT: canvas->ZoomCenter(0.8); break;
    case C_FIT: canvas->Fit(64); break;
    case C_ZOOM_100: canvas->ZoomCenter(1 / canvas->zoom); break;
    case C_SWAP: std::swap(ed.fg, ed.bg); fFg->SetColor(ed.fg); fBg->SetColor(ed.bg); break;
    case C_DEFAULT_COLORS: ed.fg = 0x000000; ed.bg = 0xffffff; fFg->SetColor(ed.fg); fBg->SetColor(ed.bg); break;
    case C_XF_APPLY: tools.commitXf(); break;
    case C_XF_CANCEL: tools.cancelXf(); break;
    case C_CROP_APPLY: if (tools.tool == Tool::Crop) tools.applyCrop(); break;
    case C_CROP_RESET: tools.resetCrop(); canvas->Invalidate(); break;
    case C_PICK_FG: (new ColorPickerWindow(TR("전경색 선택"), ed.fg, 0, BMessenger(this)))->Show(); break;
    case C_PICK_BG: (new ColorPickerWindow(TR("배경색 선택"), ed.bg, 1, BMessenger(this)))->Show(); break;
    case C_SIZE_DOWN: tools.opt.size = std::max(1, int(lround(tools.opt.size * 0.8)) - 1); UpdateOptions(); canvas->Invalidate(); break;
    case C_SIZE_UP: tools.opt.size = std::min(500, int(lround(tools.opt.size * 1.25)) + 1); UpdateOptions(); canvas->Invalidate(); break;
    case C_HARD_DOWN: tools.opt.hardness = std::max(0, tools.opt.hardness - 25); UpdateOptions(); break;
    case C_HARD_UP: tools.opt.hardness = std::min(100, tools.opt.hardness + 25); UpdateOptions(); break;
    }
    canvas->UpdateCursor();
}
void MainWindow::HandleOpt(BMessage *msg) {
    const char *key = NULL;
    int32 pct = 0;
    if (msg->FindInt32("opacityKey", &pct) == B_OK) { // Photoshop: number keys set brush opacity with a painting tool, layer opacity otherwise
        const Tool t = tools.tool;
        if (t == Tool::Brush || t == Tool::Eraser || t == Tool::Bucket || t == Tool::Gradient) { tools.opt.opacity = pct; UpdateOptions(); }
        else if (Layer *l = ed.active()) { if (!tools.xf.on && !tools.drag) { l->opacity = pct / 100.0; ed.commit(); ed.damage(); } }
    } else if (msg->FindString("toggle", &key) == B_OK) {
        const std::string k = key;
        bool *target = k == "contiguous" ? &tools.opt.contiguous : k == "sampleAll" ? &tools.opt.sampleAll : k == "antiAlias" ? &tools.opt.antiAlias : &tools.opt.toTransparent;
        *target = !*target;
    } else if (msg->FindString("key", &key) == B_OK) {
        const std::string k = key;
        const int v = fSliders[k]->Value();
        int *target = k == "size" ? &tools.opt.size : k == "hardness" ? &tools.opt.hardness : k == "tolerance" ? &tools.opt.tolerance : k == "opacity" ? &tools.opt.opacity : &tools.opt.feather;
        *target = v;
        UpdateOptions();
        canvas->Invalidate();
    }
}
void MainWindow::HandleForm(BMessage *msg, bool live) {
    const int32 id = msg->GetInt32("id", 0);
    const bool ok = msg->GetBool("ok", false);
    if (id == FORM_ADJUST) {
        Layer *l = ed.active();
        if (!fAdjusting || !l) return;
        if (!live && !ok) { l->img = fAdjSrc; ed.damage(); }
        else {
            const ColorMat cm = AdjustMatrix(msg);
            applyColor(l->img, fAdjSrc, cm, fAdjSel.null() ? NULL : &fAdjSel);
            ed.damage();
            if (ok) { ed.colorExt(*l, cm); ed.commit(); }
        }
        if (!live) { fAdjusting = false; fAdjSrc = Image(); fAdjSel = Image(); }
        return;
    }
    if (live || !ok) return;
    const int w = msg->GetInt32("w", 1), h = msg->GetInt32("h", 1), r = msg->GetInt32("r", 1);
    switch (id) {
    case FORM_NEW: {
        if (!ConfirmDiscard()) break;
        const int bgMode = msg->GetInt32("bg", 0);
        ed.name = "r-painter";
        ed.newDoc(w, h, bgMode != 1, bgMode == 2 ? ed.bg : 0xffffff);
        tools.resetCrop();
        canvas->Fit();
        break;
    }
    case FORM_IMAGE_SIZE: ed.resizeImage(w, h); canvas->Fit(); break;
    case FORM_CANVAS_SIZE: ed.resizeCanvas(w, h, msg->GetInt32("anchor", 4)); canvas->Fit(); break;
    case FORM_EXPORT: {
        const int f = msg->GetInt32("fmt", 0);
        fExportExt = f == 1 ? "jpg" : f == 2 ? "webp" : "png";
        fSaveIsExport = true;
        ShowSave((ed.name + "." + fExportExt).c_str());
        break;
    }
    case FORM_FEATHER: ed.modifySel([&](Alpha &a) { blurAlpha(a, ed.d.w, ed.d.h, r); }); break;
    case FORM_EXPAND: ed.modifySel([&](Alpha &a) { morphAlpha(a, ed.d.w, ed.d.h, r, true); }); break;
    case FORM_CONTRACT: ed.modifySel([&](Alpha &a) { morphAlpha(a, ed.d.w, ed.d.h, r, false); }); break;
    case FORM_RENAME:
        if (Layer *l = ed.active()) {
            const char *n = msg->GetString("name", "");
            if (*n) { l->name = n; ed.commit(); }
        }
        break;
    }
}
void MainWindow::MessageReceived(BMessage *msg) {
    switch (msg->what) {
    case 'fit1': canvas->Fit(); break;
    case 'test': SelfTest(msg->GetString("dir", "/tmp")); break;
    case MSG_CMD: Run(msg->GetInt32("cmd", -1)); break;
    case MSG_TOOL: SetTool(Tool(msg->GetInt32("tool", 0))); break;
    case MSG_OPT: HandleOpt(msg); break;
    case MSG_SELMODE: tools.opt.selMode = msg->GetInt32("mode", 0); UpdateOptions(); break;
    case MSG_REFRESH: if (msg->GetBool("pos", false)) UpdateStatus(); else Refresh(); break;
    case MSG_FORM: HandleForm(msg, false); break;
    case MSG_FORM_CHANGED: HandleForm(msg, true); break;
    case MSG_COLOR: {
        const Px rgb = Px(msg->GetInt32("rgb", 0)) & 0xffffff;
        if (msg->GetInt32("which", 0) == 0) ed.fg = rgb; else ed.bg = rgb;
        fFg->SetColor(ed.fg); fBg->SetColor(ed.bg);
        break;
    }
    case MSG_PALETTE: ed.fg = Px(msg->GetInt32("rgb", 0)) & 0xffffff; fFg->SetColor(ed.fg); break;
    case MSG_LAYER_PICK: {
        if (fRefreshing || tools.drag) break;
        LayerItem *it = dynamic_cast<LayerItem *>(fLayers->ItemAt(fLayers->CurrentSelection()));
        if (!it || it->id == ed.d.activeId) break;
        // selection messages arrive late: the list may have been rebuilt for another document meanwhile
        bool exists = false;
        for (const auto &l : ed.d.layers) if (l.id == it->id) exists = true;
        if (!exists) break;
        if (tools.xf.on) tools.commitXf();
        ed.d.activeId = it->id;
        Refresh();
        break;
    }
    case MSG_LAYER_VIS:
        for (auto &l : ed.d.layers) if (l.id == msg->GetInt32("id", -1)) { l.visible = !l.visible; ed.commit(); ed.damage(); break; }
        break;
    case MSG_LAYER_MOVE: {
        const int row = msg->GetInt32("row", -1), gap = msg->GetInt32("gap", -1), n = int(ed.d.layers.size());
        if (row < 0 || gap < 0 || row >= n || tools.drag) break;
        if (tools.xf.on) tools.commitXf();
        const int newRow = gap > row ? gap - 1 : gap; // rows are listed top layer first
        ed.moveLayerTo(n - 1 - row, n - 1 - newRow);
        break;
    }
    case MSG_LAYER_RENAME:
        if (Layer *l = ed.active()) Form(TR("레이어 이름"), FORM_RENAME, {{Field::Text, "name", TR("이름"), 0, 0, 0, {}, l->name}});
        break;
    case MSG_LAYER_OPACITY:
        if (!fRefreshing) if (Layer *l = ed.active()) {
            l->opacity = fOpacity->Value() / 100.0;
            ed.damage();
            if (msg->GetBool("done", false)) ed.commit();
        }
        break;
    case MSG_BLEND:
        if (Layer *l = ed.active()) {
            if (tools.xf.on) tools.commitXf();
            l->blend = blendModes()[size_t(msg->GetInt32("index", 0))].mode;
            ed.commit();
            ed.damage();
        }
        break;
    case MSG_OPEN_REFS: case MSG_PLACE_REFS: case B_REFS_RECEIVED: case B_SIMPLE_DATA: {
        entry_ref ref;
        if (msg->FindRef("refs", &ref) != B_OK) break;
        BPath path(&ref);
        // dropped files: an untouched document is replaced, otherwise the image becomes a new layer
        const bool asLayer = msg->what == MSG_PLACE_REFS || ((msg->what == B_SIMPLE_DATA || msg->what == B_REFS_RECEIVED) && ed.histIdx > 0);
        OpenPath(path.Path(), asLayer);
        break;
    }
    case MSG_SAVE_REFS: Save(msg); break;
    default: BWindow::MessageReceived(msg);
    }
}

// ---------- self-test (RPainter --selftest <dir>) ----------
// Exercises the Haiku-specific parts in the running app: Translation Kit codecs, project files with real PNG
// data, the clipboard, and the command handlers. Leaves a small drawing on screen and prints PASS / FAIL lines.
void MainWindow::SelfTest(const char *dir) {
    int fails = 0;
    auto ok = [&](const char *name, bool cond) { printf("%s %s\n", cond ? "PASS" : "FAIL", name); fflush(stdout); if (!cond) fails++; };
    auto dragTool = [&](Tool t, PtF a, PtF b) { SetTool(t); tools.down(a, Mods()); tools.move(b, Mods()); tools.up(b); };
    auto pixel = [&](int x, int y) { const Px p = ed.active()->img.pixel(x, y); return Px(pxA(p)) << 24 | straightRgb(p); };
    const std::string base = dir;

    ed.fg = 0xe53935;
    dragTool(Tool::Brush, {200, 200}, {700, 500});
    ok("brush stroke", pixel(450, 350) == 0xffe53935u && ed.hist.size() == 2);
    Run(C_LAYER_NEW);
    dragTool(Tool::Ellipse, {500, 150}, {900, 550});
    ed.fill(0x1e88e5);
    ok("ellipse selection + fill", pixel(700, 350) == 0xff1e88e5u && pxA(ed.active()->img.pixel(510, 160)) == 0);
    ok("layer list rebuilt", ed.d.layers.size() == 2);

    // Translation Kit: export, read back
    const Image flat = ed.flatten();
    struct Fmt { const char *ext; uint32 format; } fmts[] = {{"png", B_PNG_FORMAT}, {"jpg", B_JPEG_FORMAT}, {"webp", B_WEBP_FORMAT}};
    for (const Fmt &f : fmts) {
        const std::string path = base + "/rpainter-selftest." + f.ext;
        BFile file(path.c_str(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        const bool wrote = file.InitCheck() == B_OK && translateImage(flat, &file, f.format) == B_OK;
        file.Unset();
        const Image back = loadImageFile(path.c_str());
        const Px c = back.null() ? 0 : straightRgb(back.pixel(700, 350));
        const bool closeColor = abs(pxR(c) - 0x1e) < 12 && abs(pxG(c) - 0x88) < 12 && abs(pxB(c) - 0xe5) < 12;
        ok((std::string("export + reload ") + f.ext).c_str(), wrote && back.w == 1200 && back.h == 800 && closeColor);
    }
    {
        const Image back = loadImageFile((base + "/rpainter-selftest.png").c_str());
        ok("png is lossless incl. white background", !back.null() && back.pixel(700, 350) == 0xff1e88e5u && back.pixel(5, 5) == 0xffffffffu);
        const Image rt = decodeImage(encodePng(ed.active()->img));
        ok("png keeps transparency", !rt.null() && pxA(rt.pixel(10, 10)) == 0 && rt.pixel(700, 350) == 0xff1e88e5u);
    }

    // project file with off-canvas pixels, through the real PNG codec
    ed.selectNone();
    tools.setTool(Tool::Move);
    tools.nudge(400, 0);
    ok("moved partly off canvas", !ed.active()->ext.null());
    const std::string proj = base + "/rpainter-selftest.rpaint";
    ok("project saved", writeFile(proj.c_str(), ed.projectJson(encodePng), "application/json"));
    ed.newDoc(64, 64, true);
    OpenPath(proj.c_str(), false);
    ok("project loaded", ed.d.w == 1200 && ed.d.layers.size() == 2 && !ed.active()->ext.null());
    tools.nudge(-400, 0);
    ok("off-canvas pixels survive the round trip", pixel(700, 350) == 0xff1e88e5u && pixel(520, 350) == 0xff1e88e5u && ed.active()->ext.null());

    // clipboard
    dragTool(Tool::Rect, {600, 250}, {800, 450});
    Copy(false, false);
    Paste();
    ok("clipboard copy / paste in place", ed.d.layers.size() == 3 && pixel(700, 350) == 0xff1e88e5u && pxA(ed.active()->img.pixel(590, 350)) == 0);
    Run(C_UNDO);
    ok("undo paste", ed.d.layers.size() == 2);

    // command handlers
    Run(C_INVERT);
    ok("invert (selection only)", pixel(700, 350) == 0xffe1771au && pixel(520, 350) == 0xff1e88e5u);
    Run(C_UNDO);
    Run(C_ROT_CW);
    ok("rotate canvas", ed.d.w == 800 && ed.d.h == 1200);
    Run(C_UNDO);
    Run(C_SEL_NONE);
    Run(C_TRANSFORM);
    ok("free transform starts", tools.xf.on);
    tools.down({950, 350}, Mods{true, false}); tools.move({700, 600}, Mods{true, false}); tools.up({700, 600});
    ok("rotate 90 with shift", tools.xf.angle == 90);
    Run(C_XF_CANCEL);
    ok("transform cancelled", !tools.xf.on && pixel(700, 350) == 0xff1e88e5u);
    SetTool(Tool::Wand);
    dragTool(Tool::Rect, {560, 210}, {840, 490});
    UpdateOptions();
    Refresh();
    printf("%s (%d failed)\n", fails ? "FAILED" : "ALL PASSED", fails);
    fflush(stdout);
}

// ---------- application ----------
class App : public BApplication {
public:
    App() : BApplication("application/x-vnd.rpainter") {
        // interface language: RPAINTER_LANG if set, otherwise the first preferred system language
        const char *env = getenv("RPAINTER_LANG"), *first = NULL;
        BMessage langs;
        if (env && *env) setLanguage(env);
        else if (BLocaleRoster::Default()->GetPreferredLanguages(&langs) == B_OK && langs.FindString("language", &first) == B_OK && first) setLanguage(first);
        else setLanguage("en");
    }
    void ReadyToRun() override {
        if (!fWindow) { fWindow = new MainWindow(); fWindow->Show(); }
    }
    void RefsReceived(BMessage *msg) override {
        ReadyToRun();
        fWindow->PostMessage(msg);
    }
    void ArgvReceived(int32 argc, char **argv) override {
        ReadyToRun();
        if (argc > 2 && !strcmp(argv[1], "--selftest")) {
            BMessage m('test');
            m.AddString("dir", argv[2]);
            fWindow->PostMessage(&m);
        } else if (argc > 1) {
            BEntry entry(argv[1]);
            entry_ref ref;
            if (entry.GetRef(&ref) == B_OK) { BMessage m(MSG_OPEN_REFS); m.AddRef("refs", &ref); fWindow->PostMessage(&m); }
        }
    }

private:
    MainWindow *fWindow = NULL;
};

int main() {
    App app;
    app.Run();
    return 0;
}
