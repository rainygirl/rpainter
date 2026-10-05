// R Painter for macOS: native AppKit user interface (Objective-C++). No Qt.
// The editing engine lives in ../engine (image / doc / tools) and has no toolkit dependency.
#import <Carbon/Carbon.h> // virtual key codes, so shortcuts work with any input source (e.g. Hangul)
#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "i18n.h"
#include "tools.h"
#include <map>
#include <webp/encode.h>

enum Cmd {
    C_NEW, C_OPEN, C_PLACE, C_EXPORT, C_SAVE, C_UNDO, C_REDO, C_CUT, C_COPY, C_COPY_MERGED, C_PASTE, C_DELETE, C_FILL_FG, C_FILL_BG,
    C_TRANSFORM, C_ADJUST, C_INVERT, C_GRAYSCALE, C_IMAGE_SIZE, C_CANVAS_SIZE, C_CROP_SEL, C_ROT_CW, C_ROT_CCW, C_ROT_180, C_FLIP_H, C_FLIP_V,
    C_LAYER_NEW, C_LAYER_DUP, C_LAYER_COPY, C_LAYER_CUT, C_LAYER_DEL, C_LAYER_UP, C_LAYER_DOWN, C_MERGE_DOWN, C_FLATTEN, C_LAYER_FLIP_H, C_LAYER_FLIP_V,
    C_LAYER_SEL_UP, C_LAYER_SEL_DOWN, C_SEL_ALL, C_SEL_NONE, C_RESELECT, C_SEL_INVERT, C_SEL_LAYER, C_SEL_FEATHER, C_SEL_EXPAND, C_SEL_CONTRACT,
    C_ZOOM_IN, C_ZOOM_OUT, C_FIT, C_ZOOM_100, C_SWAP, C_DEFAULT_COLORS, C_XF_APPLY, C_XF_CANCEL, C_CROP_APPLY, C_CROP_RESET, C_PICK_FG, C_PICK_BG,
    C_SIZE_DOWN, C_SIZE_UP, C_HARD_DOWN, C_HARD_UP, C_TOGGLE_UI,
};

static Editor ed;
static Tools *tools = nullptr;
static NSString *S(const std::string &s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
static NSString *S(const char *s) { return [NSString stringWithUTF8String:s] ?: @""; }
static NSString *L(const char *ko) { return S(trText(ko)); } // translated UI text
static NSColor *colorOf(Px rgb) { return [NSColor colorWithSRGBRed:pxR(rgb) / 255.0 green:pxG(rgb) / 255.0 blue:pxB(rgb) / 255.0 alpha:1]; }
static NSColor *gray(CGFloat w) { return [NSColor colorWithWhite:w alpha:1]; }

// ---------- Image <-> CGImage, files ----------
static CGImageRef cgFromImage(const Image &im) CF_RETURNS_RETAINED {
    NSMutableData *data = [NSMutableData dataWithLength:size_t(im.w) * im.h * 4];
    for (int y = 0; y < im.h; y++) memcpy((uint8_t *)data.mutableBytes + size_t(y) * im.w * 4, im.row(y), size_t(im.w) * 4);
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef img = CGImageCreate(im.w, im.h, 8, 32, size_t(im.w) * 4, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, provider, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(provider);
    return img;
}
static Image imageFromCG(CGImageRef cg) {
    if (!cg) return Image();
    const int w = int(CGImageGetWidth(cg)), h = int(CGImageGetHeight(cg));
    if (w < 1 || h < 1) return Image();
    std::vector<Px> buf(size_t(w) * h, 0);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(buf.data(), w, h, 8, size_t(w) * 4, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(cs);
    if (!ctx) return Image();
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cg);
    CGContextRelease(ctx);
    Image im(w, h);
    for (int y = 0; y < h; y++) memcpy(im.wrow(y), &buf[size_t(y) * w], size_t(w) * 4);
    return im;
}
static Image decodeImage(const std::string &bytes) {
    NSData *data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
    CGImageSourceRef src = CGImageSourceCreateWithData((__bridge CFDataRef)data, NULL);
    if (!src) return Image();
    CGImageRef cg = CGImageSourceCreateImageAtIndex(src, 0, NULL);
    const Image im = imageFromCG(cg);
    if (cg) CGImageRelease(cg);
    CFRelease(src);
    return im;
}
static Image flattenOnWhite(const Image &im) {
    Image out(im.w, im.h);
    out.fill(0xffffffffu);
    draw(out, im, 0, 0);
    return out;
}
// ext: "png", "jpg" or "webp". PNG and JPEG go through ImageIO; ImageIO cannot write WebP, so that uses libwebp.
static std::string encodeImage(const Image &im, const std::string &ext) {
    if (ext == "webp") {
        std::vector<uint8_t> rgba(size_t(im.w) * im.h * 4);
        for (int y = 0; y < im.h; y++) {
            const Px *s = im.row(y);
            uint8_t *d = &rgba[size_t(y) * im.w * 4];
            for (int x = 0; x < im.w; x++, d += 4) { const Px c = straightRgb(s[x]); d[0] = pxR(c); d[1] = pxG(c); d[2] = pxB(c); d[3] = pxA(s[x]); }
        }
        uint8_t *out = nullptr;
        const size_t n = WebPEncodeRGBA(rgba.data(), im.w, im.h, im.w * 4, 90, &out);
        std::string bytes(reinterpret_cast<char *>(out), n);
        WebPFree(out);
        return bytes;
    }
    const bool jpg = ext == "jpg";
    CGImageRef cg = cgFromImage(jpg ? flattenOnWhite(im) : im);
    NSMutableData *data = [NSMutableData data];
    CGImageDestinationRef dst = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, (__bridge CFStringRef)(jpg ? UTTypeJPEG.identifier : UTTypePNG.identifier), 1, NULL);
    bool ok = false;
    if (dst) {
        CGImageDestinationAddImage(dst, cg, (__bridge CFDictionaryRef) @{(__bridge NSString *)kCGImageDestinationLossyCompressionQuality: @0.9});
        ok = CGImageDestinationFinalize(dst);
        CFRelease(dst);
    }
    CGImageRelease(cg);
    return ok ? std::string(static_cast<const char *>(data.bytes), data.length) : std::string();
}
static std::string encodePng(const Image &im) { return encodeImage(im, "png"); }
static std::string readFile(NSString *path) {
    NSData *d = [NSData dataWithContentsOfFile:path];
    return d ? std::string(static_cast<const char *>(d.bytes), d.length) : std::string();
}
static bool writeFile(NSString *path, const std::string &bytes) {
    return !bytes.empty() && [[NSData dataWithBytes:bytes.data() length:bytes.size()] writeToFile:path atomically:YES];
}

// ---------- icons ----------
// Same tiny path format as the other builds (24x24 grid): M/L/C/Z, O cx cy rx ry = ellipse, R x y w h = rect.
static void drawIcon(const char *spec, NSPoint origin, CGFloat size, NSColor *color, bool dashed = false) {
    const CGFloat k = size / 24;
    NSBezierPath *path = [NSBezierPath bezierPath];
    char cmd = 0;
    std::vector<CGFloat> n;
    auto P = [&](CGFloat x, CGFloat y) { return NSMakePoint(origin.x + x * k, origin.y + y * k); };
    auto flush = [&] {
        if (cmd == 'M' && n.size() == 2) [path moveToPoint:P(n[0], n[1])];
        else if (cmd == 'L' && n.size() == 2) [path lineToPoint:P(n[0], n[1])];
        else if (cmd == 'C' && n.size() == 6) [path curveToPoint:P(n[4], n[5]) controlPoint1:P(n[0], n[1]) controlPoint2:P(n[2], n[3])];
        else if (cmd == 'Z') [path closePath];
        else if (cmd == 'R' && n.size() == 4) [path appendBezierPathWithRect:NSMakeRect(origin.x + n[0] * k, origin.y + n[1] * k, n[2] * k, n[3] * k)];
        else if (cmd == 'O' && n.size() == 4) [path appendBezierPathWithOvalInRect:NSMakeRect(origin.x + (n[0] - n[2]) * k, origin.y + (n[1] - n[3]) * k, 2 * n[2] * k, 2 * n[3] * k)];
        n.clear();
    };
    for (const char *p = spec; *p;) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (isalpha(static_cast<unsigned char>(*p))) { flush(); cmd = *p++; }
        if (*p && *p != ' ' && !isalpha(static_cast<unsigned char>(*p))) { char *end = nullptr; n.push_back(strtod(p, &end)); p = end; }
    }
    flush();
    path.lineWidth = 1.6 * k;
    path.lineCapStyle = NSLineCapStyleRound;
    path.lineJoinStyle = NSLineJoinStyleRound;
    if (dashed) { const CGFloat dash[2] = {3 * k, 2.5 * k}; [path setLineDash:dash count:2 phase:0]; }
    [color setStroke];
    [path stroke];
}
struct ToolInfo { Tool tool; const char *label; const char *icon; bool dashed; };
static const ToolInfo TOOLS[TOOL_COUNT] = {
    {Tool::Move, "이동 (V)", "M12 3 L12 21 M3 12 L21 12 M12 3 L9 6 M12 3 L15 6 M12 21 L9 18 M12 21 L15 18 M3 12 L6 9 M3 12 L6 15 M21 12 L18 9 M21 12 L18 15", false},
    {Tool::Rect, "사각형 선택 (M)", "R4 5 16 14", true},
    {Tool::Ellipse, "원형 선택 (M)", "O12 12 8 7", true},
    {Tool::Lasso, "올가미 (L)", "O12 9 8 5 M7 13 C5 15 6 17 8 17 C10 17 10 19 8 21", false},
    {Tool::PolyLasso, "다각형 올가미 (L) - 클릭: 꼭짓점, Enter / 더블클릭: 닫기, Esc: 취소", "M5 8 L16 4 L20 13 L11 20 L4 15 Z R3.5 6.5 3 3 R14.5 2.5 3 3 R18.5 11.5 3 3", false},
    {Tool::Wand, "마술봉 - 자동 선택 (W)", "M4 20 L14 10 M16 3 L16 7 M14 5 L18 5 M20 9 L20 13 M18 11 L22 11 M8 3 L8 5 M7 4 L9 4", false},
    {Tool::Crop, "자르기 (C) - 핸들: 크기 (Shift: 비율 유지), 안쪽: 이동, Enter / 더블클릭: 적용, Esc: 초기화", "M6 2 L6 18 L22 18 M2 6 L18 6 L18 22", false},
    {Tool::Brush, "브러시 (B)", "M20 4 L11 13 M11 13 L9 15 C9 15 8 19 4 20 C7 21 11 20 12 17 L13 15 Z", false},
    {Tool::Eraser, "지우개 (E)", "M9 20 L4 15 L14 5 L20 11 L11 20 Z M9 10 L15 16 M9 20 L20 20", false},
    {Tool::Bucket, "페인트 통 (G)", "M4 13 L12 5 L19 12 L11 20 Z M12 5 L12 2 M4 13 L19 13 M20 16 C21 18 22 19 22 20 C22 21.1 21.1 22 20 22 C18.9 22 18 21.1 18 20 C18 19 19 18 20 16 Z", false},
    {Tool::Gradient, "그레이디언트 (G)", "R3 6 18 12 M8 6 L8 18 M12 6 L12 18 M15 6 L15 18 M17 6 L17 18 M19 6 L19 18", false},
    {Tool::Picker, "스포이드 (I)", "M15 5 L19 9 M17 3 L21 7 L18 10 L14 6 Z M14 8 L5 17 L5 20 L8 20 L17 11", false},
    {Tool::Hand, "손 (H)", "M8 13 L8 6 C8 4.5 11 4.5 11 6 L11 11 M11 6 L11 4.5 C11 3 14 3 14 4.5 L14 11 M14 6 C14 4.5 17 4.5 17 6 L17 15 C17 19 15 21 11 21 C8 21 6 19 5 16 L3 12 C3 11 5 10 6 12 L8 14", false},
    {Tool::Zoom, "돋보기 (Z, Option: 축소)", "O10 10 6 6 M15 15 L20 20 M8 10 L12 10 M10 8 L10 12", false},
};
static const char *ICON_XF = "R6 6 12 12 R4 4 4 4 R16 4 4 4 R4 16 4 4 R16 16 4 4", *ICON_OK = "M5 12 L10 17 L19 7", *ICON_X = "M6 6 L18 18 M18 6 L6 18";
static const char *ICON_TRASH = "M5 7 L19 7 M9 7 L9 4 L15 4 L15 7 M7 7 L8 20 L16 20 L17 7 M10 10 L10 17 M14 10 L14 17";
static NSColor *iconColor() { return gray(0.87); }

@interface FlippedView : NSView
@end
@implementation FlippedView
- (BOOL)isFlipped { return YES; }
@end

// Runs a block when a control fires; controls keep it alive through objc_setAssociatedObject-free ownership below.
@interface BlockTarget : NSObject
@property(copy) void (^block)(id sender);
- (void)fire:(id)sender;
@end
@implementation BlockTarget
- (void)fire:(id)sender { if (self.block) self.block(sender); }
@end
static NSMutableArray *gTargets; // strong references for the lifetime of the app / dialog
static void onAction(NSControl *c, void (^block)(id)) {
    BlockTarget *t = [BlockTarget new];
    t.block = block;
    if (!gTargets) gTargets = [NSMutableArray array];
    [gTargets addObject:t];
    c.target = t;
    c.action = @selector(fire:);
}

// Small square button showing an icon; `on` draws it as selected.
@interface IconButton : FlippedView
@property(nonatomic) const char *spec;
@property(nonatomic) BOOL on;
@property(copy) void (^action)(void);
@end
@implementation IconButton {
    BOOL _down;
}
+ (instancetype)buttonWithSpec:(const char *)spec tip:(NSString *)tip action:(void (^)(void))action {
    IconButton *b = [[IconButton alloc] initWithFrame:NSMakeRect(0, 0, 28, 28)];
    b.spec = spec;
    b.toolTip = tip;
    b.action = action;
    return b;
}
- (void)setOn:(BOOL)on { _on = on; self.needsDisplay = YES; }
- (void)drawRect:(NSRect)r {
    if (_on || _down) {
        [gray(0.12) setFill];
        NSBezierPath *bg = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 0.5, 0.5) xRadius:3 yRadius:3];
        [bg fill];
        if (_on) { [[NSColor colorWithSRGBRed:0.18 green:0.5 blue:0.98 alpha:1] setStroke]; [bg stroke]; }
    }
    drawIcon(_spec, NSMakePoint(3, 3), 22, iconColor());
}
- (void)mouseDown:(NSEvent *)e { _down = YES; self.needsDisplay = YES; }
- (void)mouseUp:(NSEvent *)e {
    _down = NO;
    self.needsDisplay = YES;
    if (NSPointInRect([self convertPoint:e.locationInWindow fromView:nil], self.bounds) && self.action) self.action();
}
@end

@interface IconLabel : FlippedView
@property(nonatomic) const char *spec;
@property(nonatomic) BOOL dashed;
@end
@implementation IconLabel
- (void)setSpec:(const char *)spec { _spec = spec; self.needsDisplay = YES; }
- (void)drawRect:(NSRect)r { if (_spec) drawIcon(_spec, NSMakePoint(1, 1), 22, iconColor(), _dashed); }
@end

static NSTextField *label(NSString *text, CGFloat size = 12, BOOL bold = NO) {
    NSTextField *l = [NSTextField labelWithString:text];
    l.font = bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];
    l.textColor = gray(0.87);
    return l;
}

// ---------- canvas ----------
@class AppController;
static AppController *gApp;
@interface AppController : NSObject <NSApplicationDelegate, NSWindowDelegate>
- (void)run:(int)cmd;
- (void)setTool:(Tool)t;
- (void)refresh;
- (void)updateOptions;
- (void)updateStatus;
- (void)openPath:(NSString *)path asLayer:(BOOL)asLayer;
- (void)opacityKey:(int)percent;
@end

@interface CanvasView : FlippedView
@property(nonatomic) double zoom;
@property(nonatomic) NSPoint off; // view position of the document's top-left
- (void)damage:(IRect)r;
- (void)fit:(double)maxZoom;
- (void)zoomAt:(NSPoint)w factor:(double)f;
- (void)zoomCenter:(double)f;
- (void)updateCursor;
@end
@implementation CanvasView {
    Image _comp;                // composite of all layers (premultiplied)
    std::vector<uint32_t> _disp; // composite over the checkerboard, what is actually drawn
    int _dispW, _dispH;
    IRect _dirty;
    BOOL _allDirty, _panning, _space, _inside;
    NSPoint _panStart, _panOff, _mouse;
    int _antPhase;
    CursorKind _cursorKind;
    NSTrackingArea *_tracking;
}
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        _zoom = 1;
        _allDirty = YES;
        _cursorKind = CursorKind::Cross;
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
        [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer *) {
            if (ed.d.selEdges || tools->polyOn || (tools->drag && (!tools->drag->path.empty() || tools->drag->hasLine))) {
                self->_antPhase = (self->_antPhase + 1) % 8;
                self.needsDisplay = YES;
            }
        }];
    }
    return self;
}
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
}
- (PtF)toDoc:(NSPoint)w { return PtF{(w.x - _off.x) / _zoom, (w.y - _off.y) / _zoom}; }
- (NSPoint)toView:(PtF)p { return NSMakePoint(_off.x + p.x * _zoom, _off.y + p.y * _zoom); }
- (Mods)mods:(NSEvent *)e { return Mods{(e.modifierFlags & NSEventModifierFlagShift) != 0, (e.modifierFlags & NSEventModifierFlagOption) != 0}; }
- (void)damage:(IRect)r {
    if (r.empty()) _allDirty = YES; else _dirty = _dirty.united(r);
    self.needsDisplay = YES;
}
- (void)zoomChanged { tools->zoom = _zoom; [gApp updateStatus]; self.needsDisplay = YES; }
- (void)fit:(double)maxZoom {
    const NSSize s = self.bounds.size;
    _zoom = std::min(maxZoom, std::max(0.02, std::min((s.width - 60.0) / ed.d.w, (s.height - 60.0) / ed.d.h)));
    _off = NSMakePoint(std::floor((s.width - ed.d.w * _zoom) / 2), std::floor((s.height - ed.d.h * _zoom) / 2));
    [self zoomChanged];
}
- (void)zoomAt:(NSPoint)w factor:(double)f {
    const double nz = std::min(64.0, std::max(0.02, _zoom * f));
    _off = NSMakePoint(w.x - (w.x - _off.x) * nz / _zoom, w.y - (w.y - _off.y) * nz / _zoom);
    _zoom = nz;
    [self zoomChanged];
}
- (void)zoomCenter:(double)f { [self zoomAt:NSMakePoint(NSMidX(self.bounds), NSMidY(self.bounds)) factor:f]; }
- (void)updateDisplay {
    const int w = ed.d.w, h = ed.d.h;
    if (_comp.w != w || _comp.h != h || _disp.empty()) {
        _comp = Image(w, h);
        _disp.assign(size_t(w) * h, 0);
        _dispW = w; _dispH = h;
        _allDirty = YES;
        tools->resetCrop();
    }
    const IRect r = (_allDirty ? ed.docRect() : _dirty) & ed.docRect();
    _allDirty = NO;
    _dirty = IRect();
    if (r.empty()) return;
    ed.composite(_comp, r);
    for (int y = r.y; y < r.b(); y++) {
        const Px *s = _comp.row(y) + r.x;
        uint32_t *d = &_disp[size_t(y) * w + r.x];
        for (int x = 0; x < r.w; x++) {
            const int ia = 255 - pxA(s[x]), c = (((x + r.x) >> 3) + (y >> 3)) & 1 ? 0xc8 : 0xff, bgc = mul255(c, ia);
            d[x] = 0xff000000u | uint32_t(pxR(s[x]) + bgc) << 16 | uint32_t(pxG(s[x]) + bgc) << 8 | uint32_t(pxB(s[x]) + bgc);
        }
    }
}
// black / white diagonal stripes; shifting the pattern phase makes the outline march
- (NSColor *)antColor {
    static NSColor *color;
    if (!color) {
        NSImage *img = [NSImage imageWithSize:NSMakeSize(8, 8) flipped:YES drawingHandler:^BOOL(NSRect) {
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    [((x + y) % 8 < 4 ? NSColor.blackColor : NSColor.whiteColor) setFill];
                    NSRectFill(NSMakeRect(x, y, 1, 1));
                }
            return YES;
        }];
        color = [NSColor colorWithPatternImage:img];
    }
    NSGraphicsContext.currentContext.patternPhase = NSMakePoint(_antPhase, 0);
    return color;
}
- (void)strokeAnts:(NSBezierPath *)p { p.lineWidth = 1; [[self antColor] setStroke]; [p stroke]; }
- (void)drawBox:(RectF)r angle:(double)angle color:(NSColor *)color {
    const double rad = angle * M_PI / 180, cs = cos(rad), sn = sin(rad);
    const PtF c = r.center();
    auto at = [&](double lx, double ly) { return [self toView:PtF{c.x + lx * cs - ly * sn, c.y + lx * sn + ly * cs}]; };
    NSBezierPath *box = [NSBezierPath bezierPath];
    [box moveToPoint:at(-r.w / 2, -r.h / 2)];
    [box lineToPoint:at(r.w / 2, -r.h / 2)];
    [box lineToPoint:at(r.w / 2, r.h / 2)];
    [box lineToPoint:at(-r.w / 2, r.h / 2)];
    [box closePath];
    [color setStroke];
    [box stroke];
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            if (!i && !j) continue;
            const NSPoint h = at(i * r.w / 2, j * r.h / 2);
            const NSRect hr = NSMakeRect(h.x - 4, h.y - 4, 8, 8);
            [NSColor.whiteColor setFill];
            NSRectFill(hr);
            [color setStroke];
            [NSBezierPath strokeRect:hr];
        }
}
- (void)drawRect:(NSRect)dirtyRect {
    [self updateDisplay];
    CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
    [gray(0.106) setFill];
    NSRectFill(self.bounds);
    // the display buffer is drawn without copying; it outlives this call
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, _disp.data(), _disp.size() * 4, NULL);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef img = CGImageCreate(_dispW, _dispH, 8, 32, size_t(_dispW) * 4, cs, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little, provider, NULL, false, kCGRenderingIntentDefault);
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, _off.x, _off.y + _dispH * _zoom); // the view is flipped, images are not
    CGContextScaleCTM(ctx, 1, -1);
    CGContextSetInterpolationQuality(ctx, _zoom < 1 ? kCGInterpolationHigh : kCGInterpolationNone);
    CGContextDrawImage(ctx, CGRectMake(0, 0, _dispW * _zoom, _dispH * _zoom), img);
    CGContextRestoreGState(ctx);
    CGImageRelease(img);
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(provider);

    const bool hideSel = tools->xf.on || (tools->drag && tools->drag->hideSel);
    if (ed.d.selEdges && !hideSel) {
        NSBezierPath *p = [NSBezierPath bezierPath];
        for (const Line &l : *ed.d.selEdges) {
            [p moveToPoint:[self toView:PtF{double(l.x1), double(l.y1)}]];
            [p lineToPoint:[self toView:PtF{double(l.x2), double(l.y2)}]];
        }
        [self strokeAnts:p];
    }
    if (tools->drag && tools->drag->path.size() > 1) {
        NSBezierPath *p = [NSBezierPath bezierPath];
        const std::vector<PtF> &pa = tools->drag->path;
        [p moveToPoint:[self toView:pa[0]]];
        for (size_t i = 1; i < pa.size(); i++) [p lineToPoint:[self toView:pa[i]]];
        [p closePath];
        [self strokeAnts:p];
    }
    if (tools->drag && tools->drag->hasLine) {
        NSBezierPath *p = [NSBezierPath bezierPath];
        [p moveToPoint:[self toView:tools->drag->line0]];
        [p lineToPoint:[self toView:tools->drag->line1]];
        [self strokeAnts:p];
    }
    if (tools->polyOn && !tools->poly.empty()) {
        NSBezierPath *p = [NSBezierPath bezierPath];
        [p moveToPoint:[self toView:tools->poly[0]]];
        for (size_t i = 1; i < tools->poly.size(); i++) [p lineToPoint:[self toView:tools->poly[i]]];
        if (_inside) [p lineToPoint:_mouse];
        const NSPoint f = [self toView:tools->poly[0]];
        [p appendBezierPathWithRect:NSMakeRect(f.x - 3, f.y - 3, 6, 6)];
        [self strokeAnts:p];
    }
    if (tools->tool == Tool::Crop && !tools->xf.on) {
        const RectF c = tools->crop;
        const NSPoint tl = [self toView:PtF{c.x, c.y}], br = [self toView:PtF{c.x + c.w, c.y + c.h}];
        const NSRect cr = NSMakeRect(tl.x, tl.y, br.x - tl.x, br.y - tl.y);
        NSBezierPath *dim = [NSBezierPath bezierPathWithRect:self.bounds];
        [dim appendBezierPathWithRect:cr];
        dim.windingRule = NSWindingRuleEvenOdd;
        [[NSColor colorWithWhite:0 alpha:0.6] setFill];
        [dim fill];
        NSBezierPath *thirds = [NSBezierPath bezierPath];
        for (int i = 1; i <= 2; i++) {
            [thirds moveToPoint:NSMakePoint(cr.origin.x + cr.size.width * i / 3, cr.origin.y)];
            [thirds lineToPoint:NSMakePoint(cr.origin.x + cr.size.width * i / 3, NSMaxY(cr))];
            [thirds moveToPoint:NSMakePoint(cr.origin.x, cr.origin.y + cr.size.height * i / 3)];
            [thirds lineToPoint:NSMakePoint(NSMaxX(cr), cr.origin.y + cr.size.height * i / 3)];
        }
        [[NSColor colorWithWhite:1 alpha:0.35] setStroke];
        [thirds stroke];
        [self drawBox:c angle:0 color:NSColor.whiteColor];
    }
    if (tools->xf.on) [self drawBox:tools->xf.dst angle:tools->xf.angle color:[NSColor colorWithSRGBRed:0.18 green:0.5 blue:0.98 alpha:1]];
    else if (_inside && (tools->tool == Tool::Brush || tools->tool == Tool::Eraser)) {
        const CGFloat r = std::max(1.0, tools->opt.size * _zoom / 2);
        CGContextSaveGState(ctx);
        CGContextSetBlendMode(ctx, kCGBlendModeDifference);
        [NSColor.whiteColor setStroke];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(_mouse.x - r, _mouse.y - r, 2 * r, 2 * r)] stroke];
        CGContextRestoreGState(ctx);
    }
}

// ----- cursors -----
// double-headed arrow at `angle` degrees (0 = horizontal), a 4-way arrow, or a circular arrow, with a white halo
static NSCursor *arrowCursor(int kind, double angle) {
    NSImage *img = [NSImage imageWithSize:NSMakeSize(24, 24) flipped:YES drawingHandler:^BOOL(NSRect) {
        NSBezierPath *p = [NSBezierPath bezierPath];
        auto arrow = [&](double deg) {
            const double a = deg * M_PI / 180, cx = 12, cy = 12, L = 9, hd = 3.5;
            for (int s = -1; s <= 1; s += 2) {
                const NSPoint tip = NSMakePoint(cx + s * L * cos(a), cy + s * L * sin(a));
                [p moveToPoint:NSMakePoint(cx, cy)];
                [p lineToPoint:tip];
                for (int side = -1; side <= 1; side += 2) {
                    const double b = a + M_PI + side * 0.6;
                    [p moveToPoint:tip];
                    [p lineToPoint:NSMakePoint(tip.x + s * hd * cos(b), tip.y + s * hd * sin(b))];
                }
            }
        };
        if (kind == 0) arrow(angle);
        else if (kind == 1) { arrow(0); arrow(90); }
        else {
            [p appendBezierPathWithArcWithCenter:NSMakePoint(12, 12) radius:7 startAngle:-45 endAngle:0 clockwise:YES];
            [p moveToPoint:NSMakePoint(15.8, 13.5)];
            [p lineToPoint:NSMakePoint(19, 10.5)];
            [p lineToPoint:NSMakePoint(22.2, 13.5)];
        }
        p.lineCapStyle = NSLineCapStyleRound;
        p.lineJoinStyle = NSLineJoinStyleRound;
        p.lineWidth = 4.5; [NSColor.whiteColor setStroke]; [p stroke];
        p.lineWidth = 1.8; [NSColor.blackColor setStroke]; [p stroke];
        return YES;
    }];
    return [[NSCursor alloc] initWithImage:img hotSpot:NSMakePoint(12, 12)];
}
- (NSCursor *)currentCursor {
    static NSCursor *moveC, *fdiag, *bdiag, *rot;
    if (!moveC) { moveC = arrowCursor(1, 0); fdiag = arrowCursor(0, 45); bdiag = arrowCursor(0, -45); rot = arrowCursor(2, 0); }
    switch (_panning ? CursorKind::Hand : _cursorKind) {
    case CursorKind::Move: return moveC;
    case CursorKind::Hand: return _panning ? NSCursor.closedHandCursor : NSCursor.openHandCursor;
    case CursorKind::ResizeH: return NSCursor.resizeLeftRightCursor;
    case CursorKind::ResizeV: return NSCursor.resizeUpDownCursor;
    case CursorKind::ResizeFDiag: return fdiag;
    case CursorKind::ResizeBDiag: return bdiag;
    case CursorKind::Rotate: return rot;
    default: return NSCursor.crosshairCursor;
    }
}
- (void)resetCursorRects { [self addCursorRect:self.bounds cursor:[self currentCursor]]; }
- (void)updateCursor {
    const CursorKind c = tools->cursor();
    if (c == _cursorKind && !_panning) return;
    _cursorKind = c;
    [self.window invalidateCursorRectsForView:self];
    if (_inside) [[self currentCursor] set];
}

// ----- mouse -----
- (void)mouseDown:(NSEvent *)e {
    [self.window makeFirstResponder:self];
    _mouse = [self convertPoint:e.locationInWindow fromView:nil];
    if (_space || tools->tool == Tool::Hand) { [self startPan]; return; }
    if (e.clickCount >= 2 && (tools->xf.on || tools->polyOn || tools->tool == Tool::Crop)) { tools->doubleClick(); [self updateCursor]; return; }
    tools->down([self toDoc:_mouse], [self mods:e]);
    self.needsDisplay = YES;
}
- (void)startPan { _panning = YES; _panStart = _mouse; _panOff = _off; [self.window invalidateCursorRectsForView:self]; [[self currentCursor] set]; }
- (void)otherMouseDown:(NSEvent *)e { _mouse = [self convertPoint:e.locationInWindow fromView:nil]; [self startPan]; }
- (void)otherMouseDragged:(NSEvent *)e { [self mouseDragged:e]; }
- (void)otherMouseUp:(NSEvent *)e { [self mouseUp:e]; }
- (void)mouseDragged:(NSEvent *)e {
    _mouse = [self convertPoint:e.locationInWindow fromView:nil];
    if (_panning) { _off = NSMakePoint(_panOff.x + _mouse.x - _panStart.x, _panOff.y + _mouse.y - _panStart.y); self.needsDisplay = YES; return; }
    tools->move([self toDoc:_mouse], [self mods:e]);
    [gApp updateStatus];
    self.needsDisplay = YES;
}
- (void)mouseMoved:(NSEvent *)e {
    _mouse = [self convertPoint:e.locationInWindow fromView:nil];
    _inside = YES;
    tools->move([self toDoc:_mouse], [self mods:e]);
    [self updateCursor];
    [gApp updateStatus];
    self.needsDisplay = YES;
}
- (void)mouseEntered:(NSEvent *)e { _inside = YES; self.needsDisplay = YES; }
- (void)mouseExited:(NSEvent *)e { _inside = NO; self.needsDisplay = YES; }
- (void)mouseUp:(NSEvent *)e {
    if (_panning) { _panning = NO; [self.window invalidateCursorRectsForView:self]; [[self currentCursor] set]; return; }
    tools->up([self toDoc:[self convertPoint:e.locationInWindow fromView:nil]]);
    [self updateCursor];
    self.needsDisplay = YES;
}
- (void)scrollWheel:(NSEvent *)e {
    const CGFloat k = e.hasPreciseScrollingDeltas ? 1 : 10;
    if (e.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagOption)) {
        [self zoomAt:[self convertPoint:e.locationInWindow fromView:nil] factor:pow(1.01, e.scrollingDeltaY * k)];
    } else {
        _off = NSMakePoint(_off.x + e.scrollingDeltaX * k, _off.y + e.scrollingDeltaY * k);
        self.needsDisplay = YES;
    }
}
- (void)magnifyWithEvent:(NSEvent *)e { [self zoomAt:[self convertPoint:e.locationInWindow fromView:nil] factor:1 + e.magnification]; }

// ----- keyboard -----
- (void)keyDown:(NSEvent *)e {
    const NSEventModifierFlags f = e.modifierFlags;
    const bool shift = f & NSEventModifierFlagShift, alt = f & NSEventModifierFlagOption, ctrl = f & NSEventModifierFlagControl, cmd = f & NSEventModifierFlagCommand;
    const Tool cur = tools->tool;
    if (cmd) { [super keyDown:e]; return; }
    switch (e.keyCode) {
    case kVK_Space: _space = YES; return;
    case kVK_Return: case kVK_ANSI_KeypadEnter: if (tools->enter()) { [self updateCursor]; self.needsDisplay = YES; } return;
    case kVK_Escape: if (tools->escape()) { [self updateCursor]; self.needsDisplay = YES; } return;
    case kVK_LeftArrow: case kVK_RightArrow: case kVK_UpArrow: case kVK_DownArrow:
        if (cur == Tool::Move) {
            const int n = shift ? 10 : 1;
            tools->nudge(e.keyCode == kVK_LeftArrow ? -n : e.keyCode == kVK_RightArrow ? n : 0, e.keyCode == kVK_UpArrow ? -n : e.keyCode == kVK_DownArrow ? n : 0);
        }
        return;
    case kVK_Delete: case kVK_ForwardDelete: [gApp run:alt ? C_FILL_FG : C_DELETE]; return;
    case kVK_Tab: [gApp run:C_TOGGLE_UI]; return;
    case kVK_ANSI_T: if (ctrl) { [gApp run:C_TRANSFORM]; return; } break; // Control+T as well as Cmd+T
    case kVK_ANSI_V: [gApp setTool:Tool::Move]; return;
    case kVK_ANSI_M: [gApp setTool:cur == Tool::Rect ? Tool::Ellipse : Tool::Rect]; return;
    case kVK_ANSI_L: [gApp setTool:cur == Tool::Lasso ? Tool::PolyLasso : Tool::Lasso]; return;
    case kVK_ANSI_W: [gApp setTool:Tool::Wand]; return;
    case kVK_ANSI_C: [gApp setTool:Tool::Crop]; return;
    case kVK_ANSI_B: [gApp setTool:Tool::Brush]; return;
    case kVK_ANSI_E: [gApp setTool:Tool::Eraser]; return;
    case kVK_ANSI_G: [gApp setTool:cur == Tool::Bucket ? Tool::Gradient : Tool::Bucket]; return;
    case kVK_ANSI_I: [gApp setTool:Tool::Picker]; return;
    case kVK_ANSI_H: [gApp setTool:Tool::Hand]; return;
    case kVK_ANSI_Z: [gApp setTool:Tool::Zoom]; return;
    case kVK_ANSI_X: [gApp run:C_SWAP]; return;
    case kVK_ANSI_D: [gApp run:C_DEFAULT_COLORS]; return;
    case kVK_ANSI_LeftBracket: [gApp run:alt ? C_LAYER_SEL_DOWN : shift ? C_HARD_DOWN : C_SIZE_DOWN]; return;
    case kVK_ANSI_RightBracket: [gApp run:alt ? C_LAYER_SEL_UP : shift ? C_HARD_UP : C_SIZE_UP]; return;
    case kVK_F6: if (shift) { [gApp run:C_SEL_FEATHER]; return; } break;
    case kVK_F7: if (shift) { [gApp run:C_SEL_INVERT]; return; } break;
    }
    static const int digits[10] = {kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4, kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9};
    for (int i = 0; i < 10; i++) if (e.keyCode == digits[i] && !ctrl && !alt) { [gApp opacityKey:i ? i * 10 : 100]; return; }
    [super keyDown:e];
}
- (void)keyUp:(NSEvent *)e { if (e.keyCode == kVK_Space) _space = NO; else [super keyUp:e]; }

// ----- dropped files -----
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { return NSDragOperationCopy; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray<NSURL *> *urls = [sender.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    if (!urls.count) return NO;
    [gApp openPath:urls.firstObject.path asLayer:ed.histIdx > 0]; // untouched document: open; otherwise add as a layer
    return YES;
}
@end

// ---------- tool bar ----------
@interface ToolBarView : FlippedView
@end
@implementation ToolBarView
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) for (int i = 0; i < TOOL_COUNT; i++) [self addToolTipRect:NSMakeRect(4, 6 + i * 34, 32, 32) owner:self userData:(void *)(intptr_t)i];
    return self;
}
- (NSString *)view:(NSView *)v stringForToolTip:(NSToolTipTag)tag point:(NSPoint)p userData:(void *)data { return L(TOOLS[(intptr_t)data].label); }
- (void)drawRect:(NSRect)r {
    for (int i = 0; i < TOOL_COUNT; i++) {
        const NSRect cell = NSMakeRect(4, 6 + i * 34, 32, 32);
        if (TOOLS[i].tool == tools->tool) {
            NSBezierPath *bg = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(cell, 0.5, 0.5) xRadius:3 yRadius:3];
            [gray(0.12) setFill]; [bg fill];
            [[NSColor colorWithSRGBRed:0.18 green:0.5 blue:0.98 alpha:1] setStroke]; [bg stroke];
        }
        drawIcon(TOOLS[i].icon, NSMakePoint(cell.origin.x + 4, cell.origin.y + 4), 24, iconColor(), TOOLS[i].dashed);
    }
}
- (void)mouseDown:(NSEvent *)e {
    const NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    const int i = int((p.y - 6) / 34);
    if (i >= 0 && i < TOOL_COUNT) [gApp setTool:TOOLS[i].tool];
}
@end

// ---------- color swatches, palette ----------
@interface Swatch : FlippedView
@property(nonatomic) Px color;
@property(copy) void (^action)(void);
@end
@implementation Swatch
- (void)setColor:(Px)c { _color = c; self.needsDisplay = YES; }
- (void)drawRect:(NSRect)r {
    [colorOf(_color) setFill]; NSRectFill(self.bounds);
    [gray(0.87) setStroke];
    NSBezierPath *p = [NSBezierPath bezierPathWithRect:NSInsetRect(self.bounds, 1, 1)];
    p.lineWidth = 2;
    [p stroke];
}
- (void)mouseUp:(NSEvent *)e { if (self.action) self.action(); }
@end
static const Px PALETTE[24] = {0x000000, 0x444444, 0x888888, 0xbbbbbb, 0xffffff, 0xe53935, 0xfb8c00, 0xfdd835, 0x43a047, 0x00acc1, 0x1e88e5, 0x8e24aa,
    0x6d4c41, 0xf06292, 0xffb74d, 0xfff176, 0xaed581, 0x4dd0e1, 0x64b5f6, 0xba68c8, 0x3e2723, 0xb71c1c, 0x1b5e20, 0x0d47a1};
@interface PaletteView : FlippedView
@property(copy) void (^picked)(Px);
@end
@implementation PaletteView
- (void)drawRect:(NSRect)r {
    for (int i = 0; i < 24; i++) {
        const NSRect c = NSMakeRect((i % 12) * 20, (i / 12) * 20, 18, 18);
        [colorOf(PALETTE[i]) setFill]; NSRectFill(c);
        [gray(0.07) setStroke]; [NSBezierPath strokeRect:NSInsetRect(c, 0.5, 0.5)];
    }
}
- (void)mouseDown:(NSEvent *)e {
    const NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    const int i = int(p.y / 20) * 12 + int(p.x / 20);
    if (i >= 0 && i < 24 && self.picked) self.picked(PALETTE[i]);
}
@end

// ---------- layer list ----------
@interface LayerListView : FlippedView
- (void)reload;
@end
@implementation LayerListView {
    NSMutableArray<NSImage *> *_thumbs;
    int _dragRow, _dropGap; // row being dragged, and the gap (0..n) it would be dropped into; -1 = none
    CGFloat _downY;
}
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) { _dragRow = -1; _dropGap = -1; }
    return self;
}
- (void)reload {
    _thumbs = [NSMutableArray array];
    const double sc = std::min(40.0 / ed.d.w, 30.0 / ed.d.h);
    for (int i = int(ed.d.layers.size()) - 1; i >= 0; i--) {
        const Image t = scaled(ed.d.layers[i].img, std::max(1, int(ed.d.w * sc)), std::max(1, int(ed.d.h * sc)));
        CGImageRef cg = cgFromImage(flattenOnWhite(t));
        [_thumbs addObject:[[NSImage alloc] initWithCGImage:cg size:NSMakeSize(t.w, t.h)]];
        CGImageRelease(cg);
    }
    [self setFrameSize:NSMakeSize(self.superview ? self.superview.bounds.size.width : 248, std::max<CGFloat>(ed.d.layers.size() * 36, 10))];
    self.needsDisplay = YES;
}
- (void)drawRect:(NSRect)r {
    const int n = int(ed.d.layers.size());
    for (int row = 0; row < n && row < int(_thumbs.count); row++) {
        const Layer &l = ed.d.layers[n - 1 - row];
        const NSRect fr = NSMakeRect(0, row * 36, self.bounds.size.width, 36);
        if (l.id == ed.d.activeId) { [[NSColor colorWithSRGBRed:0.18 green:0.5 blue:0.98 alpha:1] setFill]; NSRectFill(fr); }
        const NSRect box = NSMakeRect(8, fr.origin.y + 11, 14, 14); // visibility checkbox
        [NSColor.whiteColor setFill]; NSRectFill(box);
        if (l.visible) drawIcon("M5 12 L10 17 L19 7", NSMakePoint(box.origin.x + 1, box.origin.y + 1), 12, gray(0.1));
        NSImage *th = _thumbs[row];
        const NSRect tr = NSMakeRect(30 + (40 - th.size.width) / 2, fr.origin.y + 3 + (30 - th.size.height) / 2, th.size.width, th.size.height);
        [th drawInRect:tr fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
        [gray(0.07) setStroke]; [NSBezierPath strokeRect:NSInsetRect(tr, -0.5, -0.5)];
        [S(l.name) drawAtPoint:NSMakePoint(78, fr.origin.y + 10) withAttributes:@{NSFontAttributeName: [NSFont systemFontOfSize:12], NSForegroundColorAttributeName: gray(0.92)}];
    }
    if (_dropGap >= 0) { // where the dragged layer will land
        [NSColor.whiteColor setFill];
        NSRectFill(NSMakeRect(0, std::max(0, _dropGap * 36 - 1), self.bounds.size.width, 2));
    }
}
- (void)mouseDown:(NSEvent *)e {
    const NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    const int row = int(p.y / 36), n = int(ed.d.layers.size());
    if (row < 0 || row >= n || tools->drag) return;
    Layer &l = ed.d.layers[n - 1 - row];
    if (p.x < 26) { l.visible = !l.visible; ed.commit(); ed.damage(); return; }
    if (l.id != ed.d.activeId) {
        if (tools->xf.on) tools->commitXf();
        ed.d.activeId = l.id;
        [gApp refresh];
    }
    if (e.clickCount >= 2) { [gApp performSelector:@selector(renameLayer) withObject:nil afterDelay:0]; return; }
    _dragRow = row; _downY = p.y; _dropGap = -1;
}
// drag a row up or down to change the layer order
- (void)mouseDragged:(NSEvent *)e {
    if (_dragRow < 0) return;
    const NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    if (_dropGap < 0 && fabs(p.y - _downY) < 5) return;
    _dropGap = std::min(int(ed.d.layers.size()), std::max(0, int(lround(p.y / 36))));
    [self autoscroll:e];
    self.needsDisplay = YES;
}
- (void)mouseUp:(NSEvent *)e {
    const int r = _dragRow, gap = _dropGap, n = int(ed.d.layers.size());
    _dragRow = _dropGap = -1;
    self.needsDisplay = YES;
    if (r < 0 || gap < 0 || gap == r || gap == r + 1) return;
    if (tools->xf.on) tools->commitXf();
    const int newRow = gap > r ? gap - 1 : gap; // rows are listed top layer first
    ed.moveLayerTo(n - 1 - r, n - 1 - newRow);
}
@end

// ---------- dialogs ----------
struct Field {
    enum Type { Int, Slider, Choice, Check, Text, Note } type;
    std::string key, label;
    int min = 0, max = 0, value = 0;
    std::vector<std::string> choices;
    std::string text;
};
// Modal form built from fields. Returns true on OK with the values written back into `fields`.
// `live` is called while sliders move (values are already updated); `aspect` > 0 links fields "w" and "h".
static bool runForm(NSString *title, std::vector<Field> &fields, void (^live)(void) = nil, double aspect = 0) {
    NSAlert *alert = [NSAlert new];
    alert.messageText = title;
    [alert addButtonWithTitle:L("확인")];
    [alert addButtonWithTitle:L("취소")];
    FlippedView *acc = [[FlippedView alloc] initWithFrame:NSMakeRect(0, 0, 340, fields.size() * 32)];
    NSMutableArray<NSView *> *controls = [NSMutableArray array];
    Field *fp = fields.data();
    for (size_t i = 0; i < fields.size(); i++) {
        Field &f = fields[i];
        const CGFloat y = i * 32;
        NSTextField *lab = [NSTextField labelWithString:S(f.type == Field::Check ? "" : f.label)];
        lab.frame = NSMakeRect(0, y + 5, f.type == Field::Note ? 340 : 110, 20);
        [acc addSubview:lab];
        NSView *c = nil;
        if (f.type == Field::Int || f.type == Field::Text) {
            NSTextField *t = [[NSTextField alloc] initWithFrame:NSMakeRect(115, y + 3, 220, 22)];
            t.stringValue = f.type == Field::Int ? [NSString stringWithFormat:@"%d", f.value] : S(f.text);
            c = t;
        } else if (f.type == Field::Slider) {
            NSSlider *s = [NSSlider sliderWithValue:f.value minValue:f.min maxValue:f.max target:nil action:nil];
            s.frame = NSMakeRect(115, y + 3, 180, 22);
            s.continuous = YES;
            NSTextField *val = [NSTextField labelWithString:[NSString stringWithFormat:@"%d", f.value]];
            val.frame = NSMakeRect(300, y + 5, 40, 20);
            [acc addSubview:val];
            onAction(s, ^(NSSlider *sender) {
                fp[i].value = int(lround(sender.doubleValue));
                val.stringValue = [NSString stringWithFormat:@"%d", fp[i].value];
                if (live) live();
            });
            c = s;
        } else if (f.type == Field::Choice) {
            NSPopUpButton *p = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(113, y + 1, 224, 26) pullsDown:NO];
            for (const auto &ch : f.choices) [p addItemWithTitle:S(ch)];
            [p selectItemAtIndex:f.value];
            c = p;
        } else if (f.type == Field::Check) {
            NSButton *b = [NSButton checkboxWithTitle:S(f.label) target:nil action:nil];
            b.frame = NSMakeRect(113, y + 4, 220, 22);
            b.state = f.value ? NSControlStateValueOn : NSControlStateValueOff;
            c = b;
        } else c = [[NSView alloc] initWithFrame:NSZeroRect];
        [acc addSubview:c];
        [controls addObject:c];
    }
    if (aspect > 0) { // image size: keep width and height in proportion (applied when a field is committed)
        NSTextField *w = nil, *h = nil;
        NSButton *lock = nil;
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].key == "w") w = (NSTextField *)controls[i];
            if (fields[i].key == "h") h = (NSTextField *)controls[i];
            if (fields[i].key == "lock") lock = (NSButton *)controls[i];
        }
        if (w && h && lock) {
            onAction(w, ^(id) { if (lock.state == NSControlStateValueOn) h.stringValue = [NSString stringWithFormat:@"%d", std::max(1, int(lround(w.intValue / aspect)))]; });
            onAction(h, ^(id) { if (lock.state == NSControlStateValueOn) w.stringValue = [NSString stringWithFormat:@"%d", std::max(1, int(lround(h.intValue * aspect)))]; });
        }
    }
    alert.accessoryView = acc;
    for (NSView *c in controls) if ([c isKindOfClass:NSTextField.class]) { alert.window.initialFirstResponder = c; break; }
    const bool ok = [alert runModal] == NSAlertFirstButtonReturn;
    [alert.window makeFirstResponder:nil]; // commit a field that is still being edited
    for (size_t i = 0; i < fields.size(); i++) {
        Field &f = fields[i];
        NSView *c = controls[i];
        if (f.type == Field::Int) f.value = std::min(f.max, std::max(f.min, ((NSTextField *)c).intValue));
        else if (f.type == Field::Text) f.text = ((NSTextField *)c).stringValue.UTF8String;
        else if (f.type == Field::Choice) f.value = int(((NSPopUpButton *)c).indexOfSelectedItem);
        else if (f.type == Field::Check) f.value = ((NSButton *)c).state == NSControlStateValueOn;
    }
    return ok;
}
static int fieldValue(const std::vector<Field> &f, const char *key) { for (const auto &x : f) if (x.key == key) return x.value; return 0; }

// ----- color picker -----
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
@interface PickArea : FlippedView
@property(nonatomic) int mode;
@property(nonatomic) double hue, sat, val;
@property(copy) void (^picked)(double x, double y);
@end
@implementation PickArea
- (void)drawRect:(NSRect)r {
    const int w = _mode ? 24 : 256;
    Image im(w, 256);
    for (int y = 0; y < 256; y++) {
        Px *row = im.wrow(y);
        for (int x = 0; x < w; x++) {
            int cr, cg, cb;
            if (_mode) hsvToRgb(y * 360.0 / 256, 1, 1, cr, cg, cb); else hsvToRgb(_hue, x / 255.0, 1 - y / 255.0, cr, cg, cb);
            row[x] = 0xff000000u | Px(cr) << 16 | Px(cg) << 8 | Px(cb);
        }
    }
    CGImageRef cg = cgFromImage(im);
    [[[NSImage alloc] initWithCGImage:cg size:NSMakeSize(w, 256)] drawInRect:self.bounds fromRect:NSZeroRect operation:NSCompositingOperationCopy fraction:1 respectFlipped:YES hints:nil];
    CGImageRelease(cg);
    if (_mode) {
        const NSRect m = NSMakeRect(0, _hue / 360 * 255 - 2, 24, 4);
        [NSColor.whiteColor setFill]; NSRectFill(m);
        [NSColor.blackColor setStroke]; [NSBezierPath strokeRect:NSInsetRect(m, 0.5, 0.5)];
    } else {
        const NSPoint c = NSMakePoint(_sat * 255, (1 - _val) * 255);
        [NSColor.blackColor setStroke]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 6, c.y - 6, 12, 12)] stroke];
        [NSColor.whiteColor setStroke]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 5, c.y - 5, 10, 10)] stroke];
    }
}
- (void)report:(NSEvent *)e {
    const NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    if (self.picked) self.picked(std::min(1.0, std::max(0.0, p.x / 255.0)), std::min(1.0, std::max(0.0, p.y / 255.0)));
}
- (void)mouseDown:(NSEvent *)e { [self report:e]; }
- (void)mouseDragged:(NSEvent *)e { [self report:e]; }
@end
static bool pickColor(NSString *title, Px &color) {
    __block double h = 0, s = 0, v = 0; // hue kept separately so it survives gray colors
    rgbToHsv(pxR(color), pxG(color), pxB(color), h, s, v);
    NSAlert *alert = [NSAlert new];
    alert.messageText = title;
    [alert addButtonWithTitle:L("확인")];
    [alert addButtonWithTitle:L("취소")];
    FlippedView *acc = [[FlippedView alloc] initWithFrame:NSMakeRect(0, 0, 430, 258)];
    PickArea *sq = [[PickArea alloc] initWithFrame:NSMakeRect(0, 1, 256, 256)], *strip = [[PickArea alloc] initWithFrame:NSMakeRect(266, 1, 24, 256)];
    strip.mode = 1;
    Swatch *newSw = [[Swatch alloc] initWithFrame:NSMakeRect(302, 1, 126, 30)], *oldSw = [[Swatch alloc] initWithFrame:NSMakeRect(302, 30, 126, 30)];
    oldSw.color = color;
    oldSw.toolTip = L("현재 색상");
    newSw.toolTip = L("새 색상");
    for (NSView *sub in @[sq, strip, newSw, oldSw]) [acc addSubview:sub];
    const char *names[7] = {"H", "S", "B", "R", "G", "B", "#"};
    NSMutableArray<NSTextField *> *tf = [NSMutableArray array];
    for (int i = 0; i < 7; i++) {
        NSTextField *lab = [NSTextField labelWithString:S(names[i])];
        lab.frame = NSMakeRect(302, 70 + i * 27, 16, 20);
        NSTextField *t = [[NSTextField alloc] initWithFrame:NSMakeRect(322, 68 + i * 27, 106, 22)];
        [acc addSubview:lab];
        [acc addSubview:t];
        [tf addObject:t];
    }
    Px (^current)(void) = ^Px { int r, g, b; hsvToRgb(h, s, v, r, g, b); return Px(r) << 16 | Px(g) << 8 | Px(b); };
    void (^setRgb)(int, int, int) = ^(int r, int g, int b) { double nh, ns, nv; rgbToHsv(r, g, b, nh, ns, nv); if (ns > 0 && nv > 0) h = nh; s = ns; v = nv; };
    __block bool keepHex = false; // while the hex field itself is being typed in
    void (^sync)(void) = ^{
        const Px c = current();
        sq.hue = strip.hue = h; sq.sat = s; sq.val = v;
        sq.needsDisplay = strip.needsDisplay = YES;
        const int vals[6] = {int(lround(h)) % 360, int(lround(s * 100)), int(lround(v * 100)), pxR(c), pxG(c), pxB(c)};
        for (int i = 0; i < 6; i++) tf[i].stringValue = [NSString stringWithFormat:@"%d", vals[i]];
        if (!keepHex) tf[6].stringValue = [NSString stringWithFormat:@"%06x", unsigned(c)];
        newSw.color = c;
    };
    sq.picked = ^(double x, double y) { s = x; v = 1 - y; sync(); };
    strip.picked = ^(double, double y) { h = std::min(y * 360, 359.999); sync(); };
    for (int i = 0; i < 3; i++) onAction(tf[i], ^(id) { h = std::min(359, std::max(0, tf[0].intValue)); s = std::min(100, std::max(0, tf[1].intValue)) / 100.0; v = std::min(100, std::max(0, tf[2].intValue)) / 100.0; sync(); });
    for (int i = 3; i < 6; i++) onAction(tf[i], ^(id) { auto cl = [](int x) { return std::min(255, std::max(0, x)); }; setRgb(cl(tf[3].intValue), cl(tf[4].intValue), cl(tf[5].intValue)); sync(); });
    onAction(tf[6], ^(id) {
        NSString *t = [tf[6].stringValue stringByReplacingOccurrencesOfString:@"#" withString:@""];
        unsigned val = 0;
        if (t.length == 6 && [[NSScanner scannerWithString:t] scanHexInt:&val]) setRgb((val >> 16) & 255, (val >> 8) & 255, val & 255);
        sync();
    });
    // a complete hex value applies as it is typed; the '#' is a label, so it cannot be deleted
    id hexObserver = [NSNotificationCenter.defaultCenter addObserverForName:NSControlTextDidChangeNotification object:tf[6] queue:nil usingBlock:^(NSNotification *) {
        NSString *raw = tf[6].stringValue;
        NSMutableString *t = [NSMutableString string];
        for (NSUInteger i = 0; i < raw.length && t.length < 6; i++) {
            const unichar ch = [raw characterAtIndex:i];
            if (ch < 128 && isxdigit(ch)) [t appendFormat:@"%C", ch];
        }
        if (![t isEqualToString:raw]) tf[6].stringValue = t;
        unsigned val = 0;
        if (t.length == 6 && [[NSScanner scannerWithString:t] scanHexInt:&val]) {
            setRgb((val >> 16) & 255, (val >> 8) & 255, val & 255);
            keepHex = true; sync(); keepHex = false;
        }
    }];
    sync();
    alert.accessoryView = acc;
    const bool ok = [alert runModal] == NSAlertFirstButtonReturn;
    [NSNotificationCenter.defaultCenter removeObserver:hexObserver];
    [alert.window makeFirstResponder:nil];
    if (ok) color = current();
    return ok;
}

// ---------- application ----------
struct OptGroup { NSView *__strong view; std::vector<Tool> tools; bool xfOnly; };

@implementation AppController {
    NSWindow *_window;
    CanvasView *_canvas;
    ToolBarView *_toolBar;
    FlippedView *_optBar, *_panel;
    IconLabel *_toolIcon;
    std::vector<OptGroup> _opts;
    std::map<std::string, NSSlider *> _sliders;
    std::map<std::string, NSTextField *> _sliderLabels;
    std::map<std::string, IconButton *> _toggles;
    NSMutableArray<IconButton *> *_modeButtons;
    Swatch *_fg, *_bg;
    LayerListView *_layers;
    NSPopUpButton *_blend;
    NSSlider *_opacity;
    NSTextField *_opacityLabel, *_status, *_info;
    BOOL _uiHidden;
    int _lastW, _lastH;
    NSString *_pendingOpen, *_selfTestDir;
}

- (NSMenuItem *)item:(NSString *)title cmd:(int)cmd key:(NSString *)key mask:(NSEventModifierFlags)mask menu:(NSMenu *)m {
    NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:title action:@selector(menuCmd:) keyEquivalent:key];
    it.keyEquivalentModifierMask = mask;
    it.target = self;
    it.tag = cmd;
    [m addItem:it];
    return it;
}
- (void)buildMenus {
    const NSEventModifierFlags C = NSEventModifierFlagCommand, SH = NSEventModifierFlagShift, O = NSEventModifierFlagOption;
    NSMenu *bar = [NSMenu new];
    auto add = [&](NSString *title) { NSMenuItem *top = [NSMenuItem new]; NSMenu *m = [[NSMenu alloc] initWithTitle:title]; top.submenu = m; [bar addItem:top]; return m; };
    NSMenu *m = add(@"R Painter");
    [m addItemWithTitle:L("R Painter 정보") action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [m addItem:NSMenuItem.separatorItem];
    [m addItemWithTitle:L("R Painter 가리기") action:@selector(hide:) keyEquivalent:@"h"];
    [m addItemWithTitle:L("R Painter 종료") action:@selector(terminate:) keyEquivalent:@"q"];
    m = add(L("파일"));
    [self item:L("새로 만들기...") cmd:C_NEW key:@"n" mask:C menu:m];
    [self item:L("열기...") cmd:C_OPEN key:@"o" mask:C menu:m];
    [self item:L("레이어로 가져오기...") cmd:C_PLACE key:@"" mask:0 menu:m];
    [m addItem:NSMenuItem.separatorItem];
    [self item:L("내보내기 (PNG / JPG / WebP)...") cmd:C_EXPORT key:@"s" mask:C | SH | O menu:m];
    [self item:L("저장 (.rpaint 프로젝트)...") cmd:C_SAVE key:@"s" mask:C menu:m];
    m = add(L("편집"));
    [self item:L("실행 취소") cmd:C_UNDO key:@"z" mask:C menu:m];
    [self item:L("다시 실행") cmd:C_REDO key:@"z" mask:C | SH menu:m];
    [m addItem:NSMenuItem.separatorItem];
    [self item:L("잘라내기") cmd:C_CUT key:@"x" mask:C menu:m];
    [self item:L("복사") cmd:C_COPY key:@"c" mask:C menu:m];
    [self item:L("병합하여 복사") cmd:C_COPY_MERGED key:@"c" mask:C | SH menu:m];
    [self item:L("붙여넣기") cmd:C_PASTE key:@"v" mask:C menu:m];
    [self item:L("지우기 (Delete)") cmd:C_DELETE key:@"" mask:0 menu:m];
    [m addItem:NSMenuItem.separatorItem];
    [self item:L("전경색으로 채우기 (Option+Delete)") cmd:C_FILL_FG key:@"" mask:0 menu:m];
    [self item:L("배경색으로 채우기") cmd:C_FILL_BG key:@"\b" mask:C menu:m];
    [m addItem:NSMenuItem.separatorItem];
    [self item:L("자유 변형 (크기 조절 / 회전)") cmd:C_TRANSFORM key:@"t" mask:C menu:m];
    m = add(L("이미지"));
    [self item:L("색조 / 채도 / 밝기 / 대비...") cmd:C_ADJUST key:@"u" mask:C menu:m];
    [self item:L("색상 반전") cmd:C_INVERT key:@"i" mask:C menu:m];
    [self item:L("흑백 (채도 감소)") cmd:C_GRAYSCALE key:@"u" mask:C | SH menu:m];
    [m addItem:NSMenuItem.separatorItem];
    [self item:L("이미지 크기...") cmd:C_IMAGE_SIZE key:@"i" mask:C | O menu:m];
    [self item:L("캔버스 크기...") cmd:C_CANVAS_SIZE key:@"c" mask:C | O menu:m];
    [self item:L("선택 영역으로 자르기") cmd:C_CROP_SEL key:@"" mask:0 menu:m];
    [m addItem:NSMenuItem.separatorItem];
    [self item:L("시계 방향 90도 회전") cmd:C_ROT_CW key:@"" mask:0 menu:m];
    [self item:L("반시계 방향 90도 회전") cmd:C_ROT_CCW key:@"" mask:0 menu:m];
    [self item:L("180도 회전") cmd:C_ROT_180 key:@"" mask:0 menu:m];
    [self item:L("가로로 뒤집기") cmd:C_FLIP_H key:@"" mask:0 menu:m];
    [self item:L("세로로 뒤집기") cmd:C_FLIP_V key:@"" mask:0 menu:m];
    m = add(L("레이어"));
    [self item:L("새 레이어") cmd:C_LAYER_NEW key:@"n" mask:C | SH menu:m];
    [self item:L("레이어 복제") cmd:C_LAYER_DUP key:@"" mask:0 menu:m];
    [self item:L("선택 영역을 새 레이어로 복사") cmd:C_LAYER_COPY key:@"j" mask:C menu:m];
    [self item:L("선택 영역을 새 레이어로 잘라내기") cmd:C_LAYER_CUT key:@"j" mask:C | SH menu:m];
    [self item:L("레이어 삭제") cmd:C_LAYER_DEL key:@"" mask:0 menu:m];
    [m addItem:NSMenuItem.separatorItem];
    [self item:L("위로 이동") cmd:C_LAYER_UP key:@"]" mask:C menu:m];
    [self item:L("아래로 이동") cmd:C_LAYER_DOWN key:@"[" mask:C menu:m];
    [m addItem:NSMenuItem.separatorItem];
    [self item:L("아래 레이어와 병합") cmd:C_MERGE_DOWN key:@"e" mask:C menu:m];
    [self item:L("배경으로 병합 (전체)") cmd:C_FLATTEN key:@"e" mask:C | SH menu:m];
    [m addItem:NSMenuItem.separatorItem];
    [self item:L("레이어 가로 뒤집기") cmd:C_LAYER_FLIP_H key:@"" mask:0 menu:m];
    [self item:L("레이어 세로 뒤집기") cmd:C_LAYER_FLIP_V key:@"" mask:0 menu:m];
    m = add(L("선택"));
    [self item:L("전체 선택") cmd:C_SEL_ALL key:@"a" mask:C menu:m];
    [self item:L("선택 해제") cmd:C_SEL_NONE key:@"d" mask:C menu:m];
    [self item:L("다시 선택") cmd:C_RESELECT key:@"d" mask:C | SH menu:m];
    [self item:L("선택 반전") cmd:C_SEL_INVERT key:@"i" mask:C | SH menu:m];
    [self item:L("레이어 픽셀 선택") cmd:C_SEL_LAYER key:@"" mask:0 menu:m];
    [m addItem:NSMenuItem.separatorItem];
    [self item:L("페더... (Shift+F6)") cmd:C_SEL_FEATHER key:@"" mask:0 menu:m];
    [self item:L("확장...") cmd:C_SEL_EXPAND key:@"" mask:0 menu:m];
    [self item:L("축소...") cmd:C_SEL_CONTRACT key:@"" mask:0 menu:m];
    m = add(L("보기"));
    [self item:L("확대") cmd:C_ZOOM_IN key:@"=" mask:C menu:m];
    [self item:L("축소") cmd:C_ZOOM_OUT key:@"-" mask:C menu:m];
    [self item:L("화면에 맞추기") cmd:C_FIT key:@"0" mask:C menu:m];
    [self item:@"100%" cmd:C_ZOOM_100 key:@"1" mask:C menu:m];
    NSApp.mainMenu = bar;
}
// Copy / paste / undo / select all belong to the text field while one is being edited (dialogs).
- (void)menuCmd:(NSMenuItem *)sender {
    const int cmd = int(sender.tag);
    if ([NSApp.keyWindow.firstResponder isKindOfClass:NSText.class]) {
        SEL sel = cmd == C_COPY ? @selector(copy:) : cmd == C_CUT ? @selector(cut:) : cmd == C_PASTE ? @selector(paste:) : cmd == C_SEL_ALL ? @selector(selectAll:) : cmd == C_UNDO ? NSSelectorFromString(@"undo:") : NULL;
        if (sel) { [NSApp sendAction:sel to:nil from:self]; return; }
    }
    if (NSApp.modalWindow) return;
    [self run:cmd];
}

- (void)buildOptions {
    _optBar = [[FlippedView alloc] initWithFrame:NSZeroRect];
    _toolIcon = [[IconLabel alloc] initWithFrame:NSMakeRect(10, 7, 24, 24)];
    [_optBar addSubview:_toolIcon];
    _modeButtons = [NSMutableArray array];
    auto add = [&](NSView *v, std::vector<Tool> t, bool xfOnly = false) { [_optBar addSubview:v]; _opts.push_back({v, t, xfOnly}); };
    auto slider = [&](const char *icon, NSString *tip, const char *key, int min, int max, int *target, std::vector<Tool> t) {
        FlippedView *g = [[FlippedView alloc] initWithFrame:NSMakeRect(0, 0, 200, 38)];
        IconLabel *ic = [[IconLabel alloc] initWithFrame:NSMakeRect(0, 7, 24, 24)];
        ic.spec = icon;
        NSSlider *s = [NSSlider sliderWithValue:*target minValue:min maxValue:max target:nil action:nil];
        s.frame = NSMakeRect(28, 8, 120, 22);
        s.continuous = YES;
        s.refusesFirstResponder = YES;
        NSTextField *val = label([NSString stringWithFormat:@"%d", *target]);
        val.frame = NSMakeRect(152, 10, 46, 18);
        g.toolTip = ic.toolTip = s.toolTip = tip;
        for (NSView *sub in @[ic, s, val]) [g addSubview:sub];
        const std::string k = key;
        onAction(s, ^(NSSlider *sender) { *target = int(lround(sender.doubleValue)); [gApp updateOptions]; self->_canvas.needsDisplay = YES; });
        _sliders[k] = s;
        _sliderLabels[k] = val;
        add(g, t);
    };
    auto toggle = [&](const char *icon, NSString *tip, const char *key, bool *target, std::vector<Tool> t) {
        IconButton *b = [IconButton buttonWithSpec:icon tip:tip action:^{ *target = !*target; [gApp updateOptions]; }];
        _toggles[key] = b;
        add(b, t);
    };
    auto button = [&](const char *icon, NSString *tip, int cmd, std::vector<Tool> t, bool xfOnly = false) { add([IconButton buttonWithSpec:icon tip:tip action:^{ [gApp run:cmd]; }], t, xfOnly); };
    const std::vector<Tool> paint = {Tool::Brush, Tool::Eraser}, sel = {Tool::Rect, Tool::Ellipse, Tool::Lasso, Tool::PolyLasso, Tool::Wand};
    const char *modeIcons[4] = {"R5 5 14 14", "M4 4 L15 4 L15 9 L20 9 L20 20 L9 20 L9 15 L4 15 Z", "M4 4 L15 4 L15 9 L9 9 L9 15 L4 15 Z M13 18 L20 18", "R4 4 11 11 R9 9 11 11 M10.5 13.5 L13.5 10.5 M12 15 L15 12"};
    NSArray *modeTips = @[L("새 선택 영역"), L("선택 영역에 추가 (Shift+드래그)"), L("선택 영역에서 빼기 (Option+드래그)"), L("선택 영역과 교차 (Shift+Option+드래그)")];
    for (int i = 0; i < 4; i++) {
        IconButton *b = [IconButton buttonWithSpec:modeIcons[i] tip:modeTips[i] action:^{ tools->opt.selMode = i; [gApp updateOptions]; }];
        [_modeButtons addObject:b];
        add(b, sel);
    }
    ToolOpts &o = tools->opt;
    slider("O7 15 3 3 O16 11 6 6", L("크기 ( [ / ] )"), "size", 1, 500, &o.size, paint);
    slider("O12 12 9 9 O12 12 5 5 O12 12 1.5 1.5", L("경도 (가장자리 선명도)"), "hardness", 0, 100, &o.hardness, paint);
    slider("M4 8 L10 8 M7 5 L7 11 M14 16 L20 16 M18 4 L6 20", L("허용치 (색 차이 범위)"), "tolerance", 0, 255, &o.tolerance, {Tool::Bucket, Tool::Wand});
    slider("M12 3 C12 3 5 11 5 15 C5 19 8 21 12 21 C16 21 19 19 19 15 C19 11 12 3 12 3 Z M9 15 C9 17 10 18 12 18", L("불투명도"), "opacity", 1, 100, &o.opacity, {Tool::Brush, Tool::Eraser, Tool::Bucket, Tool::Gradient});
    toggle("R4 8 8 8 R12 8 8 8", L("인접 픽셀만"), "contiguous", &o.contiguous, {Tool::Bucket, Tool::Wand});
    toggle("M12 4 L20 8 L12 12 L4 8 Z M4 12 L12 16 L20 12 M4 16 L12 20 L20 16", L("모든 레이어 샘플링"), "sampleAll", &o.sampleAll, {Tool::Bucket, Tool::Wand});
    toggle("M4 20 L4 14 L10 14 L10 8 L16 8 L16 4 M8 20 C14 20 20 14 20 8", L("앤티앨리어스 (가장자리 부드럽게)"), "antiAlias", &o.antiAlias, {Tool::Wand});
    toggle("R4 4 16 16 M4 20 L20 4 M12 20 L20 12 M4 12 L12 4", L("전경색 -> 투명"), "toTransparent", &o.toTransparent, {Tool::Gradient});
    slider("M5 19 L14 10 M19 5 C12 5 8 9 8 16 C15 16 19 12 19 5 Z", L("페더 (선택 가장자리 흐리게)"), "feather", 0, 100, &o.feather, sel);
    button("R4 4 16 16 M8 16 L16 8 M16 8 L12 8 M16 8 L16 12 M8 16 L12 16 M8 16 L8 12", L("선택 반전"), C_SEL_INVERT, sel);
    button(ICON_TRASH, L("선택 영역 삭제"), C_DELETE, sel);
    button("R4 8 12 12 M8 8 L8 4 L20 4 L20 16 L16 16 M10 11 L10 17 M7 14 L13 14", L("선택 영역을 새 레이어로 복사"), C_LAYER_COPY, sel);
    button(ICON_XF, L("자유 변형 - 크기 조절 / 회전"), C_TRANSFORM, {Tool::Move});
    button(ICON_OK, L("자르기 적용 (Enter)"), C_CROP_APPLY, {Tool::Crop});
    button(ICON_X, L("자르기 초기화 (Esc)"), C_CROP_RESET, {Tool::Crop});
    button(ICON_OK, L("변형 적용 (Enter)"), C_XF_APPLY, {}, true);
    button(ICON_X, L("변형 취소 (Esc)"), C_XF_CANCEL, {}, true);
}
- (NSView *)sectionTitle:(NSString *)text y:(CGFloat)y {
    FlippedView *bar = [[FlippedView alloc] initWithFrame:NSMakeRect(0, y, 264, 28)];
    bar.wantsLayer = YES;
    bar.layer.backgroundColor = gray(0.15).CGColor;
    NSTextField *l = label(text, 12, YES);
    l.frame = NSMakeRect(10, 6, 200, 16);
    [bar addSubview:l];
    return bar;
}
- (void)buildPanel {
    _panel = [[FlippedView alloc] initWithFrame:NSZeroRect];
    _panel.wantsLayer = YES;
    _panel.layer.backgroundColor = gray(0.2).CGColor;
    [_panel addSubview:[self sectionTitle:L("색상") y:0]];
    _fg = [[Swatch alloc] initWithFrame:NSMakeRect(10, 38, 40, 40)];
    _bg = [[Swatch alloc] initWithFrame:NSMakeRect(58, 38, 40, 40)];
    _fg.toolTip = L("전경색");
    _bg.toolTip = L("배경색");
    _fg.action = ^{ [gApp run:C_PICK_FG]; };
    _bg.action = ^{ [gApp run:C_PICK_BG]; };
    IconButton *swap = [IconButton buttonWithSpec:"M5 9 L18 9 M15 6 L18 9 L15 12 M19 15 L6 15 M9 12 L6 15 L9 18" tip:L("전경색 / 배경색 전환 (X)") action:^{ [gApp run:C_SWAP]; }];
    swap.frame = NSMakeRect(106, 44, 28, 28);
    PaletteView *pal = [[PaletteView alloc] initWithFrame:NSMakeRect(10, 86, 240, 40)];
    pal.picked = ^(Px c) { ed.fg = c; [gApp refresh]; };
    [_panel addSubview:[self sectionTitle:L("레이어") y:136]];
    _blend = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(8, 172, 248, 26) pullsDown:NO];
    for (const auto &b : blendModes()) [_blend addItemWithTitle:L(b.label)];
    _blend.refusesFirstResponder = YES;
    onAction(_blend, ^(NSPopUpButton *sender) {
        if (Layer *l = ed.active()) {
            if (tools->xf.on) tools->commitXf();
            l->blend = blendModes()[size_t(sender.indexOfSelectedItem)].mode;
            ed.commit();
            ed.damage();
        }
    });
    NSTextField *ol = label(L("불투명도"));
    ol.frame = NSMakeRect(10, 206, 60, 18);
    _opacity = [NSSlider sliderWithValue:100 minValue:0 maxValue:100 target:nil action:nil];
    _opacity.frame = NSMakeRect(72, 204, 138, 22);
    _opacity.continuous = YES;
    _opacity.refusesFirstResponder = YES;
    _opacityLabel = label(@"100%");
    _opacityLabel.frame = NSMakeRect(214, 206, 44, 18);
    onAction(_opacity, ^(NSSlider *sender) {
        Layer *l = ed.active();
        if (!l) return;
        l->opacity = lround(sender.doubleValue) / 100.0;
        self->_opacityLabel.stringValue = [NSString stringWithFormat:@"%ld%%", lround(sender.doubleValue)];
        ed.damage();
        if (NSApp.currentEvent.type == NSEventTypeLeftMouseUp) ed.commit(); // one history step per drag
    });
    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(8, 234, 248, 200)];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = YES;
    scroll.backgroundColor = gray(0.12);
    scroll.autoresizingMask = NSViewHeightSizable;
    _layers = [[LayerListView alloc] initWithFrame:NSMakeRect(0, 0, 248, 36)];
    _layers.toolTip = L("체크 상자: 표시/숨김, 드래그: 순서 변경, 더블클릭: 이름 변경");
    scroll.documentView = _layers;
    scroll.identifier = @"layerScroll";
    for (NSView *v in @[_fg, _bg, swap, pal, _blend, ol, _opacity, _opacityLabel, scroll]) [_panel addSubview:v];
    struct { const char *icon; NSString *tip; int cmd; } lb[] = {
        {"R5 5 14 14 M12 8 L12 16 M8 12 L16 12", L("새 레이어"), C_LAYER_NEW}, {"R9 9 11 11 M5 15 L5 5 L15 5", L("레이어 복제"), C_LAYER_DUP},
        {"M4 5 L20 5 M4 19 L20 19 M12 8 L12 15 M9 12 L12 15 L15 12", L("아래 레이어와 병합"), C_MERGE_DOWN}, {ICON_TRASH, L("레이어 삭제"), C_LAYER_DEL}};
    for (int i = 0; i < 4; i++) {
        const int cmd = lb[i].cmd;
        IconButton *b = [IconButton buttonWithSpec:lb[i].icon tip:lb[i].tip action:^{ [gApp run:cmd]; }];
        b.frame = NSMakeRect(14 + i * 41, 0, 28, 28);
        b.identifier = @"layerButton";
        [_panel addSubview:b];
    }
}
// manual layout: options bar on top, tools left, panel right, status line at the bottom, canvas in the middle
- (void)layout {
    const NSSize s = _window.contentView.bounds.size;
    const CGFloat top = _uiHidden ? 0 : 38, left = _uiHidden ? 0 : 40, right = _uiHidden ? 0 : 264, bottom = 22;
    _optBar.hidden = _toolBar.hidden = _panel.hidden = _uiHidden;
    _optBar.frame = NSMakeRect(0, 0, s.width, 38);
    _toolBar.frame = NSMakeRect(0, top, 40, s.height - top - bottom);
    _panel.frame = NSMakeRect(s.width - 264, top, 264, s.height - top - bottom);
    _canvas.frame = NSMakeRect(left, top, s.width - left - right, s.height - top - bottom);
    _status.frame = NSMakeRect(8, s.height - 19, s.width - 320, 16);
    _info.frame = NSMakeRect(s.width - 308, s.height - 19, 300, 16);
    const CGFloat ph = _panel.frame.size.height;
    for (NSView *v in _panel.subviews) {
        if ([v.identifier isEqualToString:@"layerScroll"]) v.frame = NSMakeRect(8, 234, 248, std::max<CGFloat>(40, ph - 234 - 44));
        if ([v.identifier isEqualToString:@"layerButton"]) [v setFrameOrigin:NSMakePoint(v.frame.origin.x, ph - 36)];
    }
    [_layers setFrameSize:NSMakeSize(232, _layers.frame.size.height)];
}
- (void)windowDidResize:(NSNotification *)n { [self layout]; }

// ----- state -> UI -----
- (void)setTool:(Tool)t {
    if (tools->drag) return;
    tools->setTool(t);
    _toolBar.needsDisplay = YES;
    [self updateOptions];
    [_canvas updateCursor];
    _canvas.needsDisplay = YES;
}
- (void)updateOptions {
    const bool xf = tools->xf.on;
    const ToolInfo &t = TOOLS[int(tools->tool)];
    _toolIcon.dashed = !xf && t.dashed;
    _toolIcon.spec = xf ? ICON_XF : t.icon;
    _toolIcon.toolTip = xf ? L("자유 변형 - 모서리: 비율 유지 (Shift: 자유 비율, Option: 중심 기준), 안쪽 드래그: 이동, 바깥쪽 드래그: 회전 (Shift: 15도 단위), Enter: 적용, Esc: 취소") : L(t.label);
    CGFloat x = 44;
    for (OptGroup &o : _opts) {
        bool show = xf ? o.xfOnly : !o.xfOnly;
        if (show && !o.xfOnly) { show = false; for (Tool tt : o.tools) if (tt == tools->tool) show = true; }
        o.view.hidden = !show;
        if (!show) continue;
        const NSSize sz = o.view.frame.size;
        o.view.frame = NSMakeRect(x, (38 - sz.height) / 2, sz.width, sz.height);
        x += sz.width + 6;
    }
    for (NSUInteger i = 0; i < _modeButtons.count; i++) _modeButtons[i].on = int(i) == tools->opt.selMode;
    const ToolOpts &op = tools->opt;
    const std::pair<const char *, int> vals[] = {{"size", op.size}, {"hardness", op.hardness}, {"tolerance", op.tolerance}, {"opacity", op.opacity}, {"feather", op.feather}};
    for (const auto &v : vals) { _sliders[v.first].doubleValue = v.second; _sliderLabels[v.first].stringValue = [NSString stringWithFormat:@"%d", v.second]; }
    _toggles["contiguous"].on = op.contiguous; _toggles["sampleAll"].on = op.sampleAll; _toggles["antiAlias"].on = op.antiAlias; _toggles["toTransparent"].on = op.toTransparent;
}
- (void)refresh {
    [_layers reload];
    if (Layer *l = ed.active()) {
        for (size_t i = 0; i < blendModes().size(); i++) if (blendModes()[i].mode == l->blend) [_blend selectItemAtIndex:NSInteger(i)];
        _opacity.doubleValue = lround(l->opacity * 100);
        _opacityLabel.stringValue = [NSString stringWithFormat:@"%ld%%", lround(l->opacity * 100)];
    }
    if (_lastW != ed.d.w || _lastH != ed.d.h) { // canvas size changed (crop, resize, undo...): show all of it again
        if (_lastW) [_canvas fit:1];
        _lastW = ed.d.w; _lastH = ed.d.h;
    }
    _fg.color = ed.fg;
    _bg.color = ed.bg;
    _window.documentEdited = ed.modified;
    [self updateStatus];
}
- (void)updateStatus {
    _info.stringValue = [NSString stringWithFormat:@"%d%%   %d x %d px   X %d, Y %d", int(lround(_canvas.zoom * 100)), ed.d.w, ed.d.h, int(floor(tools->mouseDoc.x)), int(floor(tools->mouseDoc.y))];
}
- (BOOL)confirmDiscard {
    if (!ed.modified) return YES;
    NSAlert *a = [NSAlert new];
    a.messageText = L("저장하지 않은 변경 사항이 있습니다. 계속할까요?");
    [a addButtonWithTitle:L("계속")];
    [a addButtonWithTitle:L("취소")];
    return [a runModal] == NSAlertFirstButtonReturn;
}
- (void)opacityKey:(int)percent { // Photoshop: number keys set brush opacity with a painting tool, layer opacity otherwise
    const Tool t = tools->tool;
    if (t == Tool::Brush || t == Tool::Eraser || t == Tool::Bucket || t == Tool::Gradient) { tools->opt.opacity = percent; [self updateOptions]; }
    else if (Layer *l = ed.active()) { if (!tools->xf.on && !tools->drag) { l->opacity = percent / 100.0; ed.commit(); ed.damage(); } }
}
- (void)renameLayer {
    Layer *l = ed.active();
    if (!l) return;
    std::vector<Field> f = {{Field::Text, "name", TR("이름"), 0, 0, 0, {}, l->name}};
    if (runForm(L("레이어 이름"), f) && !f[0].text.empty()) { ed.active()->name = f[0].text; ed.commit(); }
}

// ----- files, clipboard -----
- (void)openPath:(NSString *)path asLayer:(BOOL)asLayer {
    const std::string base = path.lastPathComponent.stringByDeletingPathExtension.UTF8String;
    bool ok = false;
    if ([path.pathExtension.lowercaseString isEqualToString:@"rpaint"]) {
        ok = ed.loadProjectJson(readFile(path), decodeImage);
        if (ok) ed.name = base;
    } else {
        const Image im = decodeImage(readFile(path));
        if (!im.null() && (asLayer || (im.w <= MAX_DIM && im.h <= MAX_DIM))) {
            if (asLayer) ed.placeImage(im, base); else ed.openImage(im, base);
            ok = true;
        }
    }
    if (!ok) { NSAlert *a = [NSAlert new]; a.messageText = [L("파일을 열 수 없습니다: ") stringByAppendingString:path]; [a runModal]; return; }
    if (!asLayer) { tools->resetCrop(); [_canvas fit:1]; }
}
- (void)showOpen:(BOOL)asLayer {
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.allowedContentTypes = @[UTTypeImage, [UTType typeWithFilenameExtension:@"rpaint"] ?: UTTypeData];
    if ([p runModal] == NSModalResponseOK) [self openPath:p.URL.path asLayer:asLayer];
}
- (void)save:(NSString *)ext {
    NSSavePanel *p = [NSSavePanel savePanel];
    p.nameFieldStringValue = [NSString stringWithFormat:@"%@.%@", S(ed.name), ext];
    if ([p runModal] != NSModalResponseOK) return;
    NSString *path = p.URL.path;
    if (![path.pathExtension.lowercaseString isEqualToString:ext]) path = [path stringByAppendingPathExtension:ext];
    bool ok;
    if ([ext isEqualToString:@"rpaint"]) { ok = writeFile(path, ed.projectJson(encodePng)); if (ok) ed.modified = false; }
    else ok = writeFile(path, encodeImage(ed.flatten(), ext.UTF8String));
    if (ok) { _status.stringValue = [L("저장됨: ") stringByAppendingString:path]; [self refresh]; }
    else { NSAlert *a = [NSAlert new]; a.messageText = [L("저장하지 못했습니다: ") stringByAppendingString:path]; [a runModal]; }
}
- (void)copy:(BOOL)cut merged:(BOOL)merged {
    if (!ed.copySel(cut, merged)) return;
    const std::string png = encodePng(ed.clip);
    NSPasteboard *pb = NSPasteboard.generalPasteboard;
    [pb clearContents];
    [pb setData:[NSData dataWithBytes:png.data() length:png.size()] forType:NSPasteboardTypePNG];
}
- (void)paste {
    Image im;
    NSPasteboard *pb = NSPasteboard.generalPasteboard;
    NSData *data = [pb dataForType:NSPasteboardTypePNG] ?: [pb dataForType:NSPasteboardTypeTIFF];
    if (data) im = decodeImage(std::string(static_cast<const char *>(data.bytes), data.length));
    // our own copy comes back with the same size: paste it in place
    if (!ed.clip.null() && (im.null() || (im.w == ed.clip.w && im.h == ed.clip.h))) ed.pasteImage(ed.clip, ed.clipPos);
    else if (!im.null()) ed.placeImage(im, TR("붙여넣기"));
    else ed.toast(TR("클립보드가 비어 있습니다"));
}

// ----- commands -----
- (void)run:(int)cmd {
    const bool viewCmd = cmd == C_ZOOM_IN || cmd == C_ZOOM_OUT || cmd == C_FIT || cmd == C_ZOOM_100 || cmd == C_XF_APPLY || cmd == C_XF_CANCEL || cmd == C_SWAP
        || cmd == C_DEFAULT_COLORS || cmd == C_PICK_FG || cmd == C_PICK_BG || cmd == C_SIZE_DOWN || cmd == C_SIZE_UP || cmd == C_HARD_DOWN || cmd == C_HARD_UP || cmd == C_TOGGLE_UI;
    if (tools->drag) return;
    if (tools->xf.on && !viewCmd) tools->commitXf();
    if (!viewCmd && cmd != C_CROP_APPLY && cmd != C_CROP_RESET) tools->cancelPoly();
    auto dims = [] { return std::vector<Field>{{Field::Int, "w", TR("너비 (px)"), 1, MAX_DIM, ed.d.w}, {Field::Int, "h", TR("높이 (px)"), 1, MAX_DIM, ed.d.h}}; };
    switch (cmd) {
    case C_NEW: {
        std::vector<Field> f = {{Field::Int, "w", TR("너비 (px)"), 1, MAX_DIM, 1200}, {Field::Int, "h", TR("높이 (px)"), 1, MAX_DIM, 800}, {Field::Choice, "bg", TR("배경"), 0, 0, 0, {TR("흰색"), TR("투명"), TR("배경색")}}};
        if (!runForm(L("새로 만들기"), f) || ![self confirmDiscard]) break;
        const int bgMode = fieldValue(f, "bg");
        ed.name = "r-painter";
        ed.newDoc(fieldValue(f, "w"), fieldValue(f, "h"), bgMode != 1, bgMode == 2 ? ed.bg : 0xffffff);
        tools->resetCrop();
        [_canvas fit:1];
        break;
    }
    case C_OPEN: if ([self confirmDiscard]) [self showOpen:NO]; break;
    case C_PLACE: [self showOpen:YES]; break;
    case C_EXPORT: {
        std::vector<Field> f = {{Field::Choice, "fmt", TR("형식"), 0, 0, 0, {TR("PNG (투명 지원)"), "JPG", "WebP"}}};
        if (runForm(L("내보내기"), f)) [self save:@[@"png", @"jpg", @"webp"][NSUInteger(fieldValue(f, "fmt"))]];
        break;
    }
    case C_SAVE: [self save:@"rpaint"]; break;
    case C_UNDO: ed.undo(); break;
    case C_REDO: ed.redo(); break;
    case C_CUT: [self copy:YES merged:NO]; break;
    case C_COPY: [self copy:NO merged:NO]; break;
    case C_COPY_MERGED: [self copy:NO merged:YES]; break;
    case C_PASTE: [self paste]; break;
    case C_DELETE: ed.deleteSel(); break;
    case C_FILL_FG: ed.fill(ed.fg); break;
    case C_FILL_BG: ed.fill(ed.bg); break;
    case C_TRANSFORM: tools->startTransform(); break;
    case C_ADJUST:
        if (Layer *l = ed.editable()) {
            const Image src = l->img, sel = ed.d.sel;
            __block std::vector<Field> f = {{Field::Slider, "hue", TR("색조 (Hue)"), -180, 180, 0}, {Field::Slider, "sat", TR("채도"), -100, 100, 0}, {Field::Slider, "bri", TR("밝기"), -100, 100, 0},
                {Field::Slider, "con", TR("대비"), -100, 100, 0}, {Field::Note, "note", ed.hasSel() ? TR("선택 영역에만 적용됩니다") : TR("현재 레이어 전체에 적용됩니다")}};
            const Image *srcP = &src, *selP = &sel;
            std::vector<Field> *fpv = &f;
            ColorMat (^matrix)(void) = ^{ return colorMatrix(fieldValue(*fpv, "hue"), fieldValue(*fpv, "sat"), fieldValue(*fpv, "bri"), fieldValue(*fpv, "con")); };
            void (^apply)(void) = ^{ applyColor(ed.active()->img, *srcP, matrix(), selP->null() ? nullptr : selP); ed.damage(); };
            if (runForm(L("색조 / 채도 / 밝기 / 대비"), f, apply)) { apply(); ed.colorExt(*ed.active(), matrix()); ed.commit(); }
            else { ed.active()->img = src; ed.damage(); }
        }
        break;
    case C_INVERT: ed.quickColor(invertMatrix()); break;
    case C_GRAYSCALE: ed.quickColor(colorMatrix(0, -100, 0, 0)); break;
    case C_IMAGE_SIZE: {
        std::vector<Field> f = dims();
        f.push_back({Field::Check, "lock", TR("비율 유지"), 0, 1, 1});
        if (runForm(L("이미지 크기"), f, nil, double(ed.d.w) / ed.d.h)) { ed.resizeImage(fieldValue(f, "w"), fieldValue(f, "h")); [_canvas fit:1]; }
        break;
    }
    case C_CANVAS_SIZE: {
        std::vector<Field> f = dims();
        f.push_back({Field::Choice, "anchor", TR("기준점"), 0, 0, 4, {TR("왼쪽 위"), TR("위"), TR("오른쪽 위"), TR("왼쪽"), TR("가운데"), TR("오른쪽"), TR("왼쪽 아래"), TR("아래"), TR("오른쪽 아래")}});
        if (runForm(L("캔버스 크기"), f)) { ed.resizeCanvas(fieldValue(f, "w"), fieldValue(f, "h"), fieldValue(f, "anchor")); [_canvas fit:1]; }
        break;
    }
    case C_CROP_SEL: ed.cropToSel(); break;
    case C_ROT_CW: ed.transformDoc(DocXf::RotCW); break;
    case C_ROT_CCW: ed.transformDoc(DocXf::RotCCW); break;
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
        if (j >= 0 && j < int(ed.d.layers.size())) { ed.d.activeId = ed.d.layers[j].id; [self refresh]; }
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
    case C_SEL_FEATHER: case C_SEL_EXPAND: case C_SEL_CONTRACT: {
        if (!ed.hasSel()) { ed.toast(TR("선택 영역이 없습니다")); break; }
        std::vector<Field> f = {{Field::Int, "r", TR("반경 (px)"), 1, 100, cmd == C_SEL_FEATHER ? 5 : 2}};
        if (!runForm(cmd == C_SEL_FEATHER ? L("페더 (가장자리 흐리게)") : cmd == C_SEL_EXPAND ? L("선택 영역 확장") : L("선택 영역 축소"), f)) break;
        const int r = f[0].value;
        if (cmd == C_SEL_FEATHER) ed.modifySel([&](Alpha &a) { blurAlpha(a, ed.d.w, ed.d.h, r); });
        else ed.modifySel([&](Alpha &a) { morphAlpha(a, ed.d.w, ed.d.h, r, cmd == C_SEL_EXPAND); });
        break;
    }
    case C_ZOOM_IN: [_canvas zoomCenter:1.25]; break;
    case C_ZOOM_OUT: [_canvas zoomCenter:0.8]; break;
    case C_FIT: [_canvas fit:64]; break;
    case C_ZOOM_100: [_canvas zoomCenter:1 / _canvas.zoom]; break;
    case C_SWAP: std::swap(ed.fg, ed.bg); [self refresh]; break;
    case C_DEFAULT_COLORS: ed.fg = 0x000000; ed.bg = 0xffffff; [self refresh]; break;
    case C_XF_APPLY: tools->commitXf(); break;
    case C_XF_CANCEL: tools->cancelXf(); break;
    case C_CROP_APPLY: if (tools->tool == Tool::Crop) tools->applyCrop(); break;
    case C_CROP_RESET: tools->resetCrop(); _canvas.needsDisplay = YES; break;
    case C_PICK_FG: if (pickColor(L("전경색 선택"), ed.fg)) [self refresh]; break;
    case C_PICK_BG: if (pickColor(L("배경색 선택"), ed.bg)) [self refresh]; break;
    case C_SIZE_DOWN: tools->opt.size = std::max(1, int(lround(tools->opt.size * 0.8)) - 1); [self updateOptions]; _canvas.needsDisplay = YES; break;
    case C_SIZE_UP: tools->opt.size = std::min(500, int(lround(tools->opt.size * 1.25)) + 1); [self updateOptions]; _canvas.needsDisplay = YES; break;
    case C_HARD_DOWN: tools->opt.hardness = std::max(0, tools->opt.hardness - 25); [self updateOptions]; break;
    case C_HARD_UP: tools->opt.hardness = std::min(100, tools->opt.hardness + 25); [self updateOptions]; break;
    case C_TOGGLE_UI: _uiHidden = !_uiHidden; [self layout]; break;
    }
    [_canvas updateCursor];
}

// ----- self-test (RPainter --selftest <dir>) -----
// Exercises the macOS-specific parts in the running app: image codecs, project files with real PNG data,
// the pasteboard and the command handlers. Leaves a drawing on screen, saves a snapshot of the window.
- (void)selfTest {
    __block int fails = 0;
    void (^ok)(const char *, bool) = ^(const char *name, bool cond) { printf("%s %s\n", cond ? "PASS" : "FAIL", name); fflush(stdout); if (!cond) fails++; };
    void (^dragTool)(Tool, PtF, PtF) = ^(Tool t, PtF a, PtF b) { [self setTool:t]; tools->down(a, Mods()); tools->move(b, Mods()); tools->up(b); };
    Px (^pixel)(int, int) = ^Px(int x, int y) { const Px p = ed.active()->img.pixel(x, y); return Px(pxA(p)) << 24 | straightRgb(p); };
    NSString *dir = _selfTestDir;

    ed.fg = 0xe53935;
    dragTool(Tool::Brush, {200, 200}, {700, 500});
    ok("brush stroke", pixel(450, 350) == 0xffe53935u && ed.hist.size() == 2);
    [self run:C_LAYER_NEW];
    dragTool(Tool::Ellipse, {500, 150}, {900, 550});
    ed.fill(0x1e88e5);
    ok("ellipse selection + fill", pixel(700, 350) == 0xff1e88e5u && pxA(ed.active()->img.pixel(510, 160)) == 0);

    const Image flat = ed.flatten();
    for (NSString *ext in @[@"png", @"jpg", @"webp"]) {
        NSString *path = [dir stringByAppendingPathComponent:[@"rpainter-selftest." stringByAppendingString:ext]];
        const bool wrote = writeFile(path, encodeImage(flat, ext.UTF8String));
        const Image back = decodeImage(readFile(path));
        const Px c = back.null() ? 0 : straightRgb(back.pixel(700, 350));
        const bool closeColor = abs(pxR(c) - 0x1e) < 12 && abs(pxG(c) - 0x88) < 12 && abs(pxB(c) - 0xe5) < 12;
        ok([@"export + reload " stringByAppendingString:ext].UTF8String, wrote && back.w == 1200 && back.h == 800 && closeColor);
    }
    {
        const Image back = decodeImage(readFile([dir stringByAppendingPathComponent:@"rpainter-selftest.png"]));
        ok("png is lossless incl. white background", !back.null() && back.pixel(700, 350) == 0xff1e88e5u && back.pixel(5, 5) == 0xffffffffu);
        const Image rt = decodeImage(encodePng(ed.active()->img));
        ok("png keeps transparency", !rt.null() && pxA(rt.pixel(10, 10)) == 0 && rt.pixel(700, 350) == 0xff1e88e5u);
    }
    ed.selectNone();
    tools->setTool(Tool::Move);
    tools->nudge(400, 0);
    ok("moved partly off canvas", !ed.active()->ext.null());
    NSString *proj = [dir stringByAppendingPathComponent:@"rpainter-selftest.rpaint"];
    ok("project saved", writeFile(proj, ed.projectJson(encodePng)));
    ed.newDoc(64, 64, true);
    [self openPath:proj asLayer:NO];
    ok("project loaded", ed.d.w == 1200 && ed.d.layers.size() == 2 && !ed.active()->ext.null());
    tools->nudge(-400, 0);
    ok("off-canvas pixels survive the round trip", pixel(700, 350) == 0xff1e88e5u && pixel(520, 350) == 0xff1e88e5u && ed.active()->ext.null());

    dragTool(Tool::Rect, {600, 250}, {800, 450});
    [self copy:NO merged:NO];
    [self paste];
    ok("pasteboard copy / paste in place", ed.d.layers.size() == 3 && pixel(700, 350) == 0xff1e88e5u && pxA(ed.active()->img.pixel(590, 350)) == 0);
    [self run:C_UNDO];
    ok("undo paste", ed.d.layers.size() == 2);
    [self run:C_INVERT];
    ok("invert (selection only)", pixel(700, 350) == 0xffe1771au && pixel(520, 350) == 0xff1e88e5u);
    [self run:C_UNDO];
    [self run:C_ROT_CW];
    ok("rotate canvas", ed.d.w == 800 && ed.d.h == 1200);
    [self run:C_UNDO];
    [self run:C_SEL_NONE];
    [self run:C_TRANSFORM];
    ok("free transform starts", tools->xf.on);
    tools->down({950, 350}, Mods{true, false}); tools->move({700, 600}, Mods{true, false}); tools->up({700, 600});
    ok("rotate 90 with shift", tools->xf.angle == 90);
    [self run:C_XF_CANCEL];
    ok("transform cancelled", !tools->xf.on && pixel(700, 350) == 0xff1e88e5u);
    NSString *demo = [dir stringByAppendingPathComponent:@"demo.rpaint"];
    if ([NSFileManager.defaultManager fileExistsAtPath:demo]) {
        [self openPath:demo asLayer:NO];
        ok("demo project from the Qt build opens", ed.d.layers.size() == 4 && ed.active()->blend == Blend::Screen && fabs(ed.active()->opacity - 0.8) < 0.01);
    }
    [self setTool:Tool::Wand];
    dragTool(Tool::Ellipse, {520, 160}, {960, 600});
    [self refresh];
    [_canvas fit:1];
    [_window displayIfNeeded];
    NSView *cv = _window.contentView;
    NSBitmapImageRep *rep = [cv bitmapImageRepForCachingDisplayInRect:cv.bounds];
    [cv cacheDisplayInRect:cv.bounds toBitmapImageRep:rep];
    ok("window snapshot", [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[dir stringByAppendingPathComponent:@"window.png"] atomically:YES]);
    printf("%s (%d failed)\n", fails ? "FAILED" : "ALL PASSED", fails);
    fflush(stdout);
    ed.modified = false;
    [NSApp terminate:nil];
}

// ----- application delegate -----
- (void)applicationDidFinishLaunching:(NSNotification *)n {
    gApp = self;
    // interface language: RPAINTER_LANG if set, otherwise the first preferred system language
    const char *env = getenv("RPAINTER_LANG");
    setLanguage(env && *env ? env : (NSLocale.preferredLanguages.firstObject ?: @"en").UTF8String);
    tools = new Tools(&ed);
    NSRect vis = NSScreen.mainScreen.visibleFrame;
    const NSSize size = NSMakeSize(std::min<CGFloat>(1440, vis.size.width), std::min<CGFloat>(900, vis.size.height - 28));
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered defer:NO];
    _window.title = @"R Painter";
    _window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    _window.minSize = NSMakeSize(900, 600);
    _window.delegate = self;
    _window.releasedWhenClosed = NO;
    FlippedView *content = [[FlippedView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    content.wantsLayer = YES;
    content.layer.backgroundColor = gray(0.2).CGColor;
    _window.contentView = content;

    [self buildMenus];
    [self buildOptions];
    [self buildPanel];
    _canvas = [[CanvasView alloc] initWithFrame:NSZeroRect];
    _toolBar = [[ToolBarView alloc] initWithFrame:NSZeroRect];
    _status = label(L("Space+드래그: 화면 이동 / Cmd+휠: 확대 / 선택 도구 Shift: 추가, Option: 빼기"), 11);
    _info = label(@"", 11);
    _info.alignment = NSTextAlignmentRight;
    for (NSView *v in @[_canvas, _optBar, _toolBar, _panel, _status, _info]) [content addSubview:v];

    ed.onChange = [self] { [self refresh]; };
    ed.onDamage = [self](IRect r) { [self->_canvas damage:r]; };
    ed.onView = [self] { self->_canvas.needsDisplay = YES; };
    ed.onToast = [self](const std::string &s) { self->_status.stringValue = S(s); };
    tools->onDamage = [self](IRect r) { [self->_canvas damage:r]; };
    tools->onMode = [self] { [self updateOptions]; [self->_canvas updateCursor]; self->_canvas.needsDisplay = YES; };
    tools->onColors = [self] { [self refresh]; };
    tools->onOverlay = [self] { self->_canvas.needsDisplay = YES; };
    tools->onZoom = [self](PtF p, double f) { CanvasView *c = self->_canvas; [c zoomAt:NSMakePoint(c.off.x + p.x * c.zoom, c.off.y + p.y * c.zoom) factor:f]; };
    tools->onFit = [self] { [self->_canvas fit:1]; };

    [self layout];
    ed.newDoc(1200, 800, true);
    tools->resetCrop();
    [self setTool:Tool::Brush];
    [_canvas fit:1];
    [self refresh];
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:_canvas];
    [NSApp activateIgnoringOtherApps:YES];

    NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
    if (args.count > 2 && [args[1] isEqualToString:@"--selftest"]) { _selfTestDir = args[2]; [self performSelector:@selector(selfTest) withObject:nil afterDelay:0.3]; }
    else if (_pendingOpen) [self openPath:_pendingOpen asLayer:NO];
    else if (args.count > 1 && ![args[1] hasPrefix:@"-"]) [self openPath:args[1] asLayer:NO];
}
- (BOOL)application:(NSApplication *)app openFile:(NSString *)path {
    if (!_window) { _pendingOpen = path; return YES; }
    [self openPath:path asLayer:NO];
    return YES;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app { return YES; }
- (BOOL)windowShouldClose:(NSWindow *)w { return [self confirmDiscard]; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app {
    return !_window.visible || [self confirmDiscard] ? NSTerminateNow : NSTerminateCancel;
}
- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app { return YES; }
@end

int main(int argc, const char **argv) {
    @autoreleasepool {
        NSApplication *app = NSApplication.sharedApplication;
        static AppController *controller;
        controller = [AppController new];
        app.delegate = controller;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
