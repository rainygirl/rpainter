#include "mainwindow.h"
#include "colorpicker.h"
#include "icons.h"
#include "platform.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleFactory>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

struct ToolInfo { Tool tool; const char *label; const char *tip; const char *icon; bool dashed; };
static const ToolInfo TOOLS[] = {
    {Tool::Move, "이동", "이동 (V)", "M12 3 L12 21 M3 12 L21 12 M12 3 L9 6 M12 3 L15 6 M12 21 L9 18 M12 21 L15 18 M3 12 L6 9 M3 12 L6 15 M21 12 L18 9 M21 12 L18 15", false},
    {Tool::Rect, "사각형 선택", "사각형 선택 (M)", "R4 5 16 14", true},
    {Tool::Ellipse, "원형 선택", "원형 선택 (M)", "O12 12 8 7", true},
    {Tool::Lasso, "올가미", "올가미 (L)", "O12 9 8 5 M7 13 C5 15 6 17 8 17 C10 17 10 19 8 21", false},
    {Tool::PolyLasso, "다각형 올가미", "다각형 올가미 (L) - 클릭: 꼭짓점, Enter / 더블클릭: 닫기, Esc: 취소", "M5 8 L16 4 L20 13 L11 20 L4 15 Z M3.5 6.5 L6.5 6.5 L6.5 9.5 L3.5 9.5 Z M14.5 2.5 L17.5 2.5 L17.5 5.5 L14.5 5.5 Z M18.5 11.5 L21.5 11.5 L21.5 14.5 L18.5 14.5 Z", false},
    {Tool::Wand, "마술봉", "마술봉 - 자동 선택 (W)", "M4 20 L14 10 M16 3 L16 7 M14 5 L18 5 M20 9 L20 13 M18 11 L22 11 M8 3 L8 5 M7 4 L9 4", false},
    {Tool::Crop, "자르기", "자르기 (C) - 핸들: 크기 (Shift: 비율 유지), 안쪽: 이동, Enter / 더블클릭: 적용, Esc: 초기화", "M6 2 L6 18 L22 18 M2 6 L18 6 L18 22", false},
    {Tool::Brush, "브러시", "브러시 (B)", "M20 4 L11 13 M11 13 L9 15 C9 15 8 19 4 20 C7 21 11 20 12 17 L13 15 Z", false},
    {Tool::Eraser, "지우개", "지우개 (E)", "M9 20 L4 15 L14 5 L20 11 L11 20 Z M9 10 L15 16 M9 20 L20 20", false},
    {Tool::Bucket, "페인트 통", "페인트 통 (G)", "M4 13 L12 5 L19 12 L11 20 Z M12 5 L12 2 M4 13 L19 13 M20 16 C21 18 22 19 22 20 C22 21.1 21.1 22 20 22 C18.9 22 18 21.1 18 20 C18 19 19 18 20 16 Z", false},
    {Tool::Gradient, "그레이디언트", "그레이디언트 (G)", "R3 6 18 12 M8 6 L8 18 M12 6 L12 18 M15 6 L15 18 M17 6 L17 18 M19 6 L19 18", false},
    {Tool::Picker, "스포이드", "스포이드 (I)", "M15 5 L19 9 M17 3 L21 7 L18 10 L14 6 Z M14 8 L5 17 L5 20 L8 20 L17 11", false},
    {Tool::Hand, "손", "손 (H)", "M8 13 L8 6 C8 4.5 11 4.5 11 6 L11 11 M11 6 L11 4.5 C11 3 14 3 14 4.5 L14 11 M14 6 C14 4.5 17 4.5 17 6 L17 15 C17 19 15 21 11 21 C8 21 6 19 5 16 L3 12 C3 11 5 10 6 12 L8 14", false},
    {Tool::Zoom, "돋보기", "돋보기 (Z, Alt: 축소)", "O10 10 6 6 M15 15 L20 20 M8 10 L12 10 M10 8 L10 12", false},
};
static const char *ICON_TRANSFORM = "R6 6 12 12 R4 4 4 4 R16 4 4 4 R4 16 4 4 R16 16 4 4";

void applyTheme(QApplication &app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(51, 51, 51));
    pal.setColor(QPalette::WindowText, QColor(221, 221, 221));
    pal.setColor(QPalette::Base, QColor(30, 30, 30));
    pal.setColor(QPalette::AlternateBase, QColor(43, 43, 43));
    pal.setColor(QPalette::Text, QColor(221, 221, 221));
    pal.setColor(QPalette::Button, QColor(68, 68, 68));
    pal.setColor(QPalette::ButtonText, QColor(221, 221, 221));
    pal.setColor(QPalette::ToolTipBase, QColor(30, 30, 30));
    pal.setColor(QPalette::ToolTipText, QColor(221, 221, 221));
    pal.setColor(QPalette::Highlight, QColor(45, 127, 249));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    app.setPalette(pal);
}

MainWindow::MainWindow() {
    setWindowTitle("R Painter");
    // default 1440x900, but never larger than the screen (e.g. a 1280x800 display)
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(1440, screen.width()), qMin(900, screen.height() - 30));
    setAcceptDrops(true);
    canvas = new Canvas(&ed, this);
    setCentralWidget(canvas);

    buildMenus();
    buildTools();
    buildOptions();
    buildPanels();
    zoomLabel = new QLabel(this);
    sizeLabel = new QLabel(this);
    posLabel = new QLabel(this);
    for (QLabel *l : {zoomLabel, sizeLabel, posLabel}) statusBar()->addPermanentWidget(l);
    statusBar()->showMessage(K("Space+드래그: 화면 이동 / Ctrl+휠: 확대 / 선택 도구 Shift: 추가, Alt: 빼기"));

    ed.onChange = [this] { scheduleRefresh(); };
    ed.onDamage = [this](QRect r) { canvas->damage(r); };
    ed.onView = [this] { canvas->update(); };
    ed.onToast = [this](QString s) { statusBar()->showMessage(s, 3000); };
    canvas->onStatus = [this] { updateStatus(); };
    canvas->onMode = [this] { updateOptions(); };
    canvas->onColors = [this] { updateColors(); };

    ed.newDoc(1200, 800, Qt::white);
    setTool(Tool::Brush);
    updateColors();
    QTimer::singleShot(0, this, [this] { canvas->fit(); canvas->setFocus(); });
}

// ---------- commands ----------
void MainWindow::run(const std::function<void()> &fn, bool viewCmd) {
    if (canvas->dragging()) return;
    if (canvas->xf.on && !viewCmd) canvas->commitXf();
    if (!viewCmd) canvas->cancelPoly();
    fn();
}
QAction *MainWindow::act(QMenu *menu, const QString &text, const QString &keys, std::function<void()> fn, bool viewCmd) {
    QAction *a = new QAction(text, this);
    if (!keys.isEmpty()) {
        QList<QKeySequence> ks;
        for (const QString &k : keys.split('|')) ks << QKeySequence(k);
        a->setShortcuts(ks);
    }
    connect(a, &QAction::triggered, this, [this, fn, viewCmd] { run(fn, viewCmd); });
    if (menu) menu->addAction(a); else addAction(a);
    return a;
}
void MainWindow::buildMenus() {
    QMenu *m = menuBar()->addMenu(K("파일"));
    act(m, K("새로 만들기..."), "Ctrl+N", [this] { newDialog(); });
    act(m, K("열기..."), "Ctrl+O", [this] { openDialog(false); });
    act(m, K("레이어로 가져오기..."), "", [this] { openDialog(true); });
    m->addSeparator();
    act(m, K("내보내기 (PNG / JPG / WebP)..."), "Ctrl+Alt+Shift+W|Ctrl+Alt+Shift+S", [this] { exportDialog(); });
    act(m, K("저장 (.rpaint 프로젝트)..."), "Ctrl+S|Ctrl+Shift+S", [this] { saveProjectDialog(); });
    m->addSeparator();
    QAction *quit = act(m, K("종료"), "Ctrl+Q", [this] { close(); });
    quit->setMenuRole(QAction::QuitRole);

    m = menuBar()->addMenu(K("편집"));
    act(m, K("실행 취소"), "Ctrl+Z|Ctrl+Alt+Z", [this] { ed.undo(); });
    act(m, K("다시 실행"), "Ctrl+Shift+Z|Ctrl+Y", [this] { ed.redo(); });
    m->addSeparator();
    act(m, K("잘라내기"), "Ctrl+X", [this] { copy(true); });
    act(m, K("복사"), "Ctrl+C", [this] { copy(false); });
    act(m, K("병합하여 복사"), "Ctrl+Shift+C", [this] { copy(false, true); });
    act(m, K("붙여넣기"), "Ctrl+V|Ctrl+Shift+V", [this] { paste(); });
    act(m, K("지우기"), "Del|Backspace", [this] { ed.deleteSel(); });
    m->addSeparator();
    act(m, K("전경색으로 채우기"), "Alt+Backspace|Alt+Del", [this] { ed.fill(ed.fg); });
    act(m, K("배경색으로 채우기"), "Ctrl+Backspace|Ctrl+Del", [this] { ed.fill(ed.bg); });
    m->addSeparator();
    act(m, K("자유 변형 (크기 조절 / 회전)"), platform::transformKeys(), [this] { canvas->startTransform(); });

    m = menuBar()->addMenu(K("이미지"));
    act(m, K("색조 / 채도 / 밝기 / 대비..."), "Ctrl+U", [this] { adjustDialog(); });
    act(m, K("색상 반전"), "Ctrl+I", [this] { ed.quickColor(invertMatrix()); });
    act(m, K("흑백 (채도 감소)"), "Ctrl+Shift+U", [this] { ed.quickColor(colorMatrix(0, -100, 0, 0)); });
    m->addSeparator();
    act(m, K("이미지 크기..."), "Ctrl+Alt+I", [this] { imageSizeDialog(); });
    act(m, K("캔버스 크기..."), "Ctrl+Alt+C", [this] { canvasSizeDialog(); });
    act(m, K("선택 영역으로 자르기"), "", [this] { ed.cropToSel(); canvas->fit(); });
    m->addSeparator();
    const QPair<const char *, DocXf> xforms[] = {{"시계 방향 90도 회전", DocXf::RotCW}, {"반시계 방향 90도 회전", DocXf::RotCCW},
        {"180도 회전", DocXf::Rot180}, {"가로로 뒤집기", DocXf::FlipH}, {"세로로 뒤집기", DocXf::FlipV}};
    for (const auto &x : xforms) act(m, K(x.first), "", [this, x] { ed.transformDoc(x.second); canvas->fit(); });

    m = menuBar()->addMenu(K("레이어"));
    act(m, K("새 레이어"), "Ctrl+Shift+N", [this] { ed.newLayer(); });
    act(m, K("레이어 복제"), "", [this] { ed.duplicateLayer(); });
    act(m, K("선택 영역을 새 레이어로 복사"), "Ctrl+J", [this] { ed.layerViaCopy(false); });
    act(m, K("선택 영역을 새 레이어로 잘라내기"), "Ctrl+Shift+J", [this] { ed.layerViaCopy(true); });
    act(m, K("레이어 삭제"), "", [this] { ed.deleteLayer(); });
    m->addSeparator();
    act(m, K("위로 이동"), "Ctrl+]", [this] { ed.moveLayer(1); });
    act(m, K("아래로 이동"), "Ctrl+[", [this] { ed.moveLayer(-1); });
    m->addSeparator();
    act(m, K("아래 레이어와 병합"), "Ctrl+E", [this] { ed.mergeDown(); });
    act(m, K("배경으로 병합 (전체)"), "Ctrl+Shift+E", [this] { ed.flattenAll(); });
    m->addSeparator();
    act(m, K("레이어 가로 뒤집기"), "", [this] { ed.layerFlip(DocXf::FlipH); });
    act(m, K("레이어 세로 뒤집기"), "", [this] { ed.layerFlip(DocXf::FlipV); });

    m = menuBar()->addMenu(K("선택"));
    act(m, K("전체 선택"), "Ctrl+A", [this] { ed.selectAll(); });
    act(m, K("선택 해제"), "Ctrl+D", [this] { ed.selectNone(); });
    act(m, K("다시 선택"), "Ctrl+Shift+D", [this] { ed.reselect(); });
    act(m, K("선택 반전"), "Ctrl+Shift+I|Shift+F7", [this] { ed.invertSel(); });
    act(m, K("레이어 픽셀 선택"), "", [this] { ed.selectLayerPixels(); });
    m->addSeparator();
    act(m, K("페더..."), "Shift+F6", [this] { selModifyDialog(K("페더 (가장자리 흐리게)"), 5, [this](Alpha &a, int r) { blurAlpha(a, ed.d.w, ed.d.h, r); }); });
    act(m, K("확장..."), "", [this] { selModifyDialog(K("선택 영역 확장"), 2, [this](Alpha &a, int r) { morphAlpha(a, ed.d.w, ed.d.h, r, true); }); });
    act(m, K("축소..."), "", [this] { selModifyDialog(K("선택 영역 축소"), 2, [this](Alpha &a, int r) { morphAlpha(a, ed.d.w, ed.d.h, r, false); }); });

    m = menuBar()->addMenu(K("보기"));
    act(m, K("확대"), "Ctrl+=|Ctrl++", [this] { canvas->zoomCenter(1.25); }, true);
    act(m, K("축소"), "Ctrl+-", [this] { canvas->zoomCenter(0.8); }, true);
    act(m, K("화면에 맞추기"), "Ctrl+0", [this] { canvas->fit(64); }, true);
    act(m, K("100%"), "Ctrl+1|Ctrl+Alt+0", [this] { canvas->zoomCenter(1 / canvas->zoom); }, true);

    // single-key shortcuts
    const QPair<const char *, Tool> keys[] = {{"V", Tool::Move}, {"C", Tool::Crop}, {"W", Tool::Wand}, {"B", Tool::Brush},
        {"E", Tool::Eraser}, {"I", Tool::Picker}, {"H", Tool::Hand}, {"Z", Tool::Zoom}};
    for (const auto &k : keys) act(nullptr, "", k.first, [this, k] { setTool(k.second); }, true);
    act(nullptr, "", "Shift+M", [this] { setTool(canvas->tool == Tool::Rect ? Tool::Ellipse : Tool::Rect); }, true);
    act(nullptr, "", "Shift+G", [this] { setTool(canvas->tool == Tool::Bucket ? Tool::Gradient : Tool::Bucket); }, true);
    act(nullptr, "", "L|Shift+L", [this] { setTool(canvas->tool == Tool::Lasso ? Tool::PolyLasso : Tool::Lasso); }, true);
    act(nullptr, "", "M", [this] { setTool(canvas->tool == Tool::Rect ? Tool::Ellipse : Tool::Rect); }, true);
    act(nullptr, "", "G", [this] { setTool(canvas->tool == Tool::Bucket ? Tool::Gradient : Tool::Bucket); }, true);
    act(nullptr, "", "X", [this] { qSwap(ed.fg, ed.bg); updateColors(); }, true);
    act(nullptr, "", "D", [this] { ed.fg = Qt::black; ed.bg = Qt::white; updateColors(); }, true);
    auto resize = [this](double f, int d) {
        canvas->opt.size = qBound(1, qRound(canvas->opt.size * f) + d, 500);
        for (const auto &s : optSyncers) s();
    };
    auto harden = [this](int d) {
        canvas->opt.hardness = qBound(0, canvas->opt.hardness + d, 100);
        for (const auto &s : optSyncers) s();
    };
    act(nullptr, "", "Shift+[|{", [harden] { harden(-25); }, true);
    act(nullptr, "", "Shift+]|}", [harden] { harden(25); }, true);
    act(nullptr, "", "Alt+]", [this] { selectLayer(1); });
    act(nullptr, "", "Alt+[", [this] { selectLayer(-1); });
    for (int i = 0; i <= 9; i++) act(nullptr, "", QString::number(i), [this, i] { setOpacityKey(i ? i * 10 : 100); });
    act(nullptr, "", "Tab", [this] {
        const QList<QWidget *> bars = findChildren<QWidget *>(QRegularExpression("^(tools|options|panels)$"));
        const bool show = !bars.isEmpty() && !bars.first()->isVisible();
        for (QWidget *b : bars) b->setVisible(show);
    }, true);
    act(nullptr, "", "[", [resize] { resize(0.8, -1); }, true);
    act(nullptr, "", "]", [resize] { resize(1.25, 1); }, true);
}

// ---------- tools and options ----------
void MainWindow::buildTools() {
    QToolBar *tb = new QToolBar(K("도구"), this);
    tb->setObjectName("tools");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->setIconSize(QSize(24, 24));
    addToolBar(Qt::LeftToolBarArea, tb);
    QActionGroup *group = new QActionGroup(this);
    for (const ToolInfo &t : TOOLS) {
        QAction *a = tb->addAction(makeIcon(t.icon, t.dashed), K(t.label));
        a->setCheckable(true);
        a->setToolTip(K(t.tip));
        group->addAction(a);
        const Tool tool = t.tool;
        connect(a, &QAction::triggered, this, [this, tool] { setTool(tool); });
        toolActions.append(a);
    }
}
void MainWindow::setTool(Tool t) {
    if (canvas->dragging()) return;
    canvas->setTool(t);
    toolActions[int(t)]->setChecked(true);
    updateOptions();
}
void MainWindow::buildOptions() {
    optBar = new QToolBar(K("도구 옵션"), this);
    optBar->setObjectName("options");
    optBar->setMovable(false);
    optBar->setIconSize(QSize(22, 22));
    addToolBar(Qt::TopToolBarArea, optBar);
    toolLabel = new QLabel(this);
    toolLabel->setContentsMargins(8, 0, 8, 0);
    toolLabel->setMinimumHeight(32);
    optBar->addWidget(toolLabel);
    optBar->addSeparator();

    auto add = [this](QWidget *w, QVector<Tool> tools, bool xfOnly = false) { optWidgets.append({optBar->addWidget(w), tools, xfOnly}); };
    auto num = [this, add](const char *icon, const QString &tip, int ToolOpts::*member, int min, int max, const QString &suffix, QVector<Tool> tools) {
        QWidget *w = new QWidget(this);
        QHBoxLayout *h = new QHBoxLayout(w);
        h->setContentsMargins(6, 0, 6, 0);
        QLabel *label = new QLabel(w);
        label->setPixmap(makeIcon(icon).pixmap(22, 22));
        QSlider *s = new QSlider(Qt::Horizontal, w);
        QSpinBox *sp = new QSpinBox(w);
        s->setRange(min, max);
        s->setFixedWidth(110);
        s->setFocusPolicy(Qt::NoFocus);
        sp->setRange(min, max);
        sp->setSuffix(suffix);
        sp->setFocusPolicy(Qt::ClickFocus);
        sp->setFixedWidth(84);
        w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        w->setToolTip(tip);
        h->addWidget(label);
        h->addWidget(s);
        h->addWidget(sp);
        connect(s, &QSlider::valueChanged, this, [this, sp, member](int v) {
            canvas->opt.*member = v;
            sp->blockSignals(true); sp->setValue(v); sp->blockSignals(false);
            canvas->update();
        });
        connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), s, &QSlider::setValue);
        connect(sp, &QSpinBox::editingFinished, this, [this] { canvas->setFocus(); });
        optSyncers.append([this, s, member] { s->setValue(canvas->opt.*member); });
        add(w, tools);
    };
    auto iconButton = [this](const char *icon, const QString &tip) {
        QToolButton *b = new QToolButton(this);
        b->setIcon(makeIcon(icon));
        b->setIconSize(QSize(22, 22));
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    auto check = [this, add, iconButton](const char *icon, const QString &tip, bool ToolOpts::*member, QVector<Tool> tools) {
        QToolButton *c = iconButton(icon, tip);
        c->setCheckable(true);
        c->setChecked(canvas->opt.*member);
        connect(c, &QToolButton::toggled, this, [this, member](bool v) { canvas->opt.*member = v; });
        add(c, tools);
    };
    auto button = [this, add, iconButton](const char *icon, const QString &tip, std::function<void()> fn, QVector<Tool> tools, bool xfOnly = false) {
        QToolButton *b = iconButton(icon, tip);
        connect(b, &QToolButton::clicked, this, [this, fn, xfOnly] { run(fn, xfOnly); });
        add(b, tools, xfOnly);
    };
    const QVector<Tool> paint = {Tool::Brush, Tool::Eraser}, sel = {Tool::Rect, Tool::Ellipse, Tool::Lasso, Tool::PolyLasso, Tool::Wand};
    {
        // selection mode, like Photoshop's four buttons; Shift / Alt override it while dragging
        QWidget *w = new QWidget(this);
        QHBoxLayout *h = new QHBoxLayout(w);
        h->setContentsMargins(6, 0, 6, 0);
        h->setSpacing(2);
        QButtonGroup *group = new QButtonGroup(w);
        const QPair<const char *, const char *> modes[] = {
            {"R5 5 14 14", "새 선택 영역"},
            {"M4 4 L15 4 L15 9 L20 9 L20 20 L9 20 L9 15 L4 15 Z", "선택 영역에 추가 (Shift+드래그)"},
            {"M4 4 L15 4 L15 9 L9 9 L9 15 L4 15 Z M13 18 L20 18", "선택 영역에서 빼기 (Alt+드래그)"},
            {"R4 4 11 11 R9 9 11 11 M10.5 13.5 L13.5 10.5 M12 15 L15 12", "선택 영역과 교차 (Shift+Alt+드래그)"}};
        for (int i = 0; i < 4; i++) {
            QToolButton *b = iconButton(modes[i].first, K(modes[i].second));
            b->setCheckable(true);
            b->setChecked(i == 0);
            group->addButton(b);
            h->addWidget(b);
            connect(b, &QToolButton::clicked, this, [this, i] { canvas->opt.selMode = i; });
        }
        add(w, sel);
    }
    num("O7 15 3 3 O16 11 6 6", K("크기 ( [ / ] )"), &ToolOpts::size, 1, 500, " px", paint);
    num("O12 12 9 9 O12 12 5 5 O12 12 1.5 1.5", K("경도 (가장자리 선명도)"), &ToolOpts::hardness, 0, 100, " %", paint);
    num("M4 8 L10 8 M7 5 L7 11 M14 16 L20 16 M18 4 L6 20", K("허용치 (색 차이 범위)"), &ToolOpts::tolerance, 0, 255, "", {Tool::Bucket, Tool::Wand});
    num("M12 3 C12 3 5 11 5 15 C5 19 8 21 12 21 C16 21 19 19 19 15 C19 11 12 3 12 3 Z M9 15 C9 17 10 18 12 18", K("불투명도"), &ToolOpts::opacity, 1, 100, " %", {Tool::Brush, Tool::Eraser, Tool::Bucket, Tool::Gradient});
    check("R4 8 8 8 R12 8 8 8", K("인접 픽셀만"), &ToolOpts::contiguous, {Tool::Bucket, Tool::Wand});
    check("M12 4 L20 8 L12 12 L4 8 Z M4 12 L12 16 L20 12 M4 16 L12 20 L20 16", K("모든 레이어 샘플링"), &ToolOpts::sampleAll, {Tool::Bucket, Tool::Wand});
    check("M4 20 L4 14 L10 14 L10 8 L16 8 L16 4 M8 20 C14 20 20 14 20 8", K("앤티앨리어스 (가장자리 부드럽게)"), &ToolOpts::antiAlias, {Tool::Wand});
    check("R4 4 16 16 M4 20 L20 4 M12 20 L20 12 M4 12 L12 4", K("전경색 -> 투명"), &ToolOpts::toTransparent, {Tool::Gradient});
    num("M5 19 L14 10 M19 5 C12 5 8 9 8 16 C15 16 19 12 19 5 Z", K("페더 (선택 가장자리 흐리게)"), &ToolOpts::feather, 0, 100, " px", sel);
    button("R4 4 16 16 M8 16 L16 8 M16 8 L12 8 M16 8 L16 12 M8 16 L12 16 M8 16 L8 12", K("선택 반전"), [this] { ed.invertSel(); }, sel);
    button("M5 7 L19 7 M9 7 L9 4 L15 4 L15 7 M7 7 L8 20 L16 20 L17 7 M10 10 L10 17 M14 10 L14 17", K("선택 영역 삭제"), [this] { ed.deleteSel(); }, sel);
    button("R4 8 12 12 M8 8 L8 4 L20 4 L20 16 L16 16 M10 11 L10 17 M7 14 L13 14", K("선택 영역을 새 레이어로 복사"), [this] { ed.layerViaCopy(false); }, sel);
    button("M5 12 L10 17 L19 7", K("자르기 적용 (Enter)"), [this] { canvas->applyCrop(); }, {Tool::Crop});
    button("M6 6 L18 18 M18 6 L6 18", K("자르기 초기화 (Esc)"), [this] { canvas->crop = ed.docRect(); canvas->update(); }, {Tool::Crop});
    button(ICON_TRANSFORM, K("자유 변형 - 크기 조절"), [this] { canvas->startTransform(); }, {Tool::Move});
    button("M5 12 L10 17 L19 7", K("변형 적용 (Enter)"), [this] { canvas->commitXf(); }, {}, true);
    button("M6 6 L18 18 M18 6 L6 18", K("변형 취소 (Esc)"), [this] { canvas->cancelXf(); }, {}, true);
    for (const auto &s : optSyncers) s();
}
void MainWindow::updateOptions() {
    const bool xf = canvas->xf.on;
    const ToolInfo &t = TOOLS[int(canvas->tool)];
    toolLabel->setPixmap((xf ? makeIcon(ICON_TRANSFORM) : makeIcon(t.icon, t.dashed)).pixmap(22, 22));
    toolLabel->setToolTip(xf ? K("자유 변형 - 모서리: 비율 유지 (Shift: 자유 비율, Alt: 중심 기준), 변: 한 방향, 안쪽 드래그: 이동, 바깥쪽 드래그: 회전 (Shift: 15도 단위), Enter: 적용, Esc: 취소") : K(t.label));
    for (const auto &o : optWidgets) o.action->setVisible(xf ? o.xfOnly : !o.xfOnly && o.tools.contains(canvas->tool));
}

// ---------- panels ----------
void MainWindow::buildPanels() {
    QDockWidget *dock = new QDockWidget(this);
    dock->setObjectName("panels");
    // no draggable gap between the canvas and the panel: a 1px line instead
    setStyleSheet("QMainWindow::separator { width: 1px; height: 1px; background: #111; }"
                  "QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 2px; }"
                  "QToolButton:hover { background: #4a4a4a; }"
                  "QToolButton:pressed { background: #262626; }"
                  "QToolButton:checked { background: #1e1e1e; border: 1px solid #2d7ff9; }");
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget(dock));
    QWidget *panel = new QWidget(dock);
    dock->setFixedWidth(264); // fix the dock, not the panel, so the panel fills it edge to edge
    dock->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *v = new QVBoxLayout(panel);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    // each section: a title bar that separates it from the one above, then its own padded layout
    auto section = [panel, v](const QString &title, int stretch) {
        QLabel *head = new QLabel(title, panel);
        head->setStyleSheet("background: #262626; color: #eee; font-weight: bold; padding: 6px 10px; border-top: 1px solid #111; border-bottom: 1px solid #111;");
        v->addWidget(head);
        QVBoxLayout *body = new QVBoxLayout;
        body->setContentsMargins(10, 10, 10, 12);
        body->setSpacing(8);
        v->addLayout(body, stretch);
        return body;
    };
    QVBoxLayout *cs = section(K("색상"), 0);
    QHBoxLayout *colors = new QHBoxLayout;
    fgBtn = new QPushButton(panel);
    bgBtn = new QPushButton(panel);
    fgBtn->setToolTip(K("전경색"));
    bgBtn->setToolTip(K("배경색"));
    for (QPushButton *b : {fgBtn, bgBtn}) { b->setFixedSize(40, 40); b->setFocusPolicy(Qt::NoFocus); colors->addWidget(b); }
    connect(fgBtn, &QPushButton::clicked, this, [this] { if (ColorPicker::pick(this, K("전경색 선택"), ed.fg)) updateColors(); });
    connect(bgBtn, &QPushButton::clicked, this, [this] { if (ColorPicker::pick(this, K("배경색 선택"), ed.bg)) updateColors(); });
    QToolButton *swap = new QToolButton(panel);
    swap->setIcon(makeIcon("M5 9 L18 9 M15 6 L18 9 L15 12 M19 15 L6 15 M9 12 L6 15 L9 18"));
    swap->setIconSize(QSize(22, 22));
    swap->setToolTip(K("전경색 / 배경색 전환 (X)"));
    swap->setFocusPolicy(Qt::NoFocus);
    connect(swap, &QToolButton::clicked, this, [this] { qSwap(ed.fg, ed.bg); updateColors(); });
    colors->addWidget(swap);
    colors->addStretch(1);
    cs->addLayout(colors);

    static const char *PALETTE[] = {"#000000", "#444444", "#888888", "#bbbbbb", "#ffffff", "#e53935", "#fb8c00", "#fdd835", "#43a047", "#00acc1", "#1e88e5", "#8e24aa",
        "#6d4c41", "#f06292", "#ffb74d", "#fff176", "#aed581", "#4dd0e1", "#64b5f6", "#ba68c8", "#3e2723", "#b71c1c", "#1b5e20", "#0d47a1"};
    QGridLayout *pal = new QGridLayout;
    pal->setSpacing(2);
    for (int i = 0; i < 24; i++) {
        QPushButton *b = new QPushButton(panel);
        b->setFixedSize(18, 18);
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(QString("background: %1; border: 1px solid #111;").arg(PALETTE[i]));
        const QColor c(PALETTE[i]);
        connect(b, &QPushButton::clicked, this, [this, c] { ed.fg = c; updateColors(); });
        pal->addWidget(b, i / 12, i % 12);
    }
    cs->addLayout(pal);

    QVBoxLayout *ls = section(K("레이어"), 1);
    blendBox = new QComboBox(panel);
    blendBox->setFocusPolicy(Qt::NoFocus);
    for (const auto &b : blendModes()) blendBox->addItem(K(b.label));
    connect(blendBox, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        run([this, i] { if (Layer *l = ed.active()) { l->blend = blendModes()[i].mode; ed.commit(); ed.damage(); } });
    });
    ls->addWidget(blendBox);
    QHBoxLayout *op = new QHBoxLayout;
    opacitySlider = new QSlider(Qt::Horizontal, panel);
    opacitySlider->setRange(0, 100);
    opacitySlider->setFocusPolicy(Qt::NoFocus);
    opacityLabel = new QLabel(panel);
    opacityLabel->setFixedWidth(40);
    op->addWidget(new QLabel(K("불투명도"), panel));
    op->addWidget(opacitySlider);
    op->addWidget(opacityLabel);
    ls->addLayout(op);
    connect(opacitySlider, &QSlider::valueChanged, this, [this](int val) {
        opacityLabel->setText(QString("%1%").arg(val));
        Layer *l = ed.active();
        if (refreshing || !l) return;
        l->opacity = val / 100.0;
        ed.damage();
        if (!opacitySlider->isSliderDown()) ed.commit();
    });
    connect(opacitySlider, &QSlider::sliderReleased, this, [this] { ed.commit(); });

    layerList = new QListWidget(panel);
    layerList->setIconSize(QSize(40, 30));
    layerList->setDragDropMode(QAbstractItemView::InternalMove);
    layerList->setFocusPolicy(Qt::NoFocus);
    layerList->setToolTip(K("체크: 표시/숨김, 더블클릭: 이름 변경, 드래그: 순서 변경"));
    ls->addWidget(layerList, 1);
    connect(layerList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (refreshing || row < 0) return;
        const int id = layerList->item(row)->data(Qt::UserRole).toInt();
        if (id == ed.d.activeId || canvas->dragging()) return;
        if (canvas->xf.on) canvas->commitXf();
        ed.d.activeId = id;
        syncLayerCtl();
    });
    connect(layerList, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
        if (refreshing) return;
        const int id = it->data(Qt::UserRole).toInt();
        const bool vis = it->checkState() == Qt::Checked;
        for (auto &l : ed.d.layers) {
            if (l.id != id) continue;
            if (l.visible != vis) { l.visible = vis; ed.commit(); ed.damage(); }
            else if (l.name != it->text() && !it->text().trimmed().isEmpty()) { l.name = it->text().trimmed(); ed.commit(); }
        }
    });
    connect(layerList->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (refreshing) return;
        QVector<Layer> order;
        for (int row = layerList->count() - 1; row >= 0; row--) {
            const int id = layerList->item(row)->data(Qt::UserRole).toInt();
            for (const auto &l : ed.d.layers) if (l.id == id) order.append(l);
        }
        if (order.size() != ed.d.layers.size()) return;
        ed.d.layers = order;
        ed.commit();
        ed.damage();
    });

    QHBoxLayout *btns = new QHBoxLayout;
    btns->setSpacing(3);
    struct LayerButton { const char *icon; const char *tip; std::function<void()> fn; };
    const LayerButton lb[] = {
        {"R5 5 14 14 M12 8 L12 16 M8 12 L16 12", "새 레이어", [this] { ed.newLayer(); }},
        {"R9 9 11 11 M5 15 L5 5 L15 5", "레이어 복제", [this] { ed.duplicateLayer(); }},
        {"M4 5 L20 5 M4 19 L20 19 M12 8 L12 15 M9 12 L12 15 L15 12", "아래 레이어와 병합", [this] { ed.mergeDown(); }},
        {"M5 7 L19 7 M9 7 L9 4 L15 4 L15 7 M7 7 L8 20 L16 20 L17 7 M10 10 L10 17 M14 10 L14 17", "레이어 삭제", [this] { ed.deleteLayer(); }},
    };
    for (const auto &b : lb) {
        QToolButton *tb = new QToolButton(panel);
        tb->setIcon(makeIcon(b.icon));
        tb->setIconSize(QSize(20, 20));
        tb->setToolTip(K(b.tip));
        tb->setFocusPolicy(Qt::NoFocus);
        tb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const auto fn = b.fn;
        connect(tb, &QToolButton::clicked, this, [this, fn] { run(fn); });
        btns->addWidget(tb);
    }
    ls->addLayout(btns);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}
void MainWindow::scheduleRefresh() {
    if (refreshPending) return;
    refreshPending = true; // deferred: commits can come from inside the layer list's own signals
    QTimer::singleShot(0, this, [this] { refresh(); });
}
void MainWindow::refresh() {
    refreshPending = false;
    refreshing = true;
    layerList->clear();
    for (int i = ed.d.layers.size() - 1; i >= 0; i--) {
        const Layer &l = ed.d.layers[i];
        QImage th(40, 30, QImage::Format_RGB32);
        th.fill(QColor(200, 200, 200));
        {
            const QImage s = l.img.scaled(40, 30, Qt::KeepAspectRatio, Qt::FastTransformation);
            QPainter p(&th);
            p.fillRect(QRect((40 - s.width()) / 2, (30 - s.height()) / 2, s.width(), s.height()), Qt::white);
            p.drawImage((40 - s.width()) / 2, (30 - s.height()) / 2, s);
        }
        QListWidgetItem *it = new QListWidgetItem(QIcon(QPixmap::fromImage(th)), l.name, layerList);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
        it->setCheckState(l.visible ? Qt::Checked : Qt::Unchecked);
        it->setData(Qt::UserRole, l.id);
        if (l.id == ed.d.activeId) layerList->setCurrentItem(it);
    }
    refreshing = false;
    syncLayerCtl();
    updateStatus();
    setWindowTitle("R Painter");
}
void MainWindow::syncLayerCtl() {
    Layer *l = ed.active();
    if (!l) return;
    refreshing = true;
    for (int i = 0; i < blendModes().size(); i++) if (blendModes()[i].mode == l->blend) blendBox->setCurrentIndex(i);
    opacitySlider->setValue(qRound(l->opacity * 100));
    refreshing = false;
}
void MainWindow::updateColors() {
    fgBtn->setStyleSheet(QString("background: %1; border: 2px solid #ddd;").arg(ed.fg.name()));
    bgBtn->setStyleSheet(QString("background: %1; border: 2px solid #ddd;").arg(ed.bg.name()));
}
void MainWindow::updateStatus() {
    zoomLabel->setText(QString("%1%  ").arg(qRound(canvas->zoom * 100)));
    sizeLabel->setText(QString("%1 x %2 px  ").arg(ed.d.w).arg(ed.d.h));
    posLabel->setText(QString("X %1, Y %2  ").arg(int(std::floor(canvas->mouseDoc.x()))).arg(int(std::floor(canvas->mouseDoc.y()))));
}

// ---------- dialogs ----------
static bool execForm(QDialog &dlg, QFormLayout *f) {
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText(K("확인"));
    bb->button(QDialogButtonBox::Cancel)->setText(K("취소"));
    f->addRow(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    return dlg.exec() == QDialog::Accepted;
}
static QSpinBox *dimSpin(QWidget *parent, int value) {
    QSpinBox *s = new QSpinBox(parent);
    s->setRange(1, MAX_DIM);
    s->setValue(value);
    s->setSuffix(" px");
    return s;
}
bool MainWindow::confirmDiscard() {
    if (!ed.modified) return true;
    return QMessageBox::question(this, "R Painter", K("저장하지 않은 변경 사항이 있습니다. 계속할까요?")) == QMessageBox::Yes;
}
void MainWindow::newDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(K("새로 만들기"));
    QFormLayout *f = new QFormLayout(&dlg);
    QSpinBox *w = dimSpin(&dlg, 1200), *h = dimSpin(&dlg, 800);
    QComboBox *fillBox = new QComboBox(&dlg);
    fillBox->addItems({K("흰색"), K("투명"), K("배경색")});
    f->addRow(K("너비"), w);
    f->addRow(K("높이"), h);
    f->addRow(K("배경"), fillBox);
    if (!execForm(dlg, f) || !confirmDiscard()) return;
    ed.name = "r-painter";
    ed.newDoc(w->value(), h->value(), fillBox->currentIndex() == 0 ? QColor(Qt::white) : fillBox->currentIndex() == 1 ? QColor(Qt::transparent) : ed.bg);
    canvas->fit();
}
bool MainWindow::openPath(const QString &path, bool asLayer) {
    if (!ed.openFile(path, asLayer)) {
        QMessageBox::warning(this, "R Painter", K("파일을 열 수 없습니다: %1").arg(path));
        return false;
    }
    if (!asLayer) canvas->fit();
    return true;
}
void MainWindow::openDialog(bool asLayer) {
    if (!asLayer && !confirmDiscard()) return;
    const QString path = QFileDialog::getOpenFileName(this, asLayer ? K("레이어로 가져오기") : K("열기"), QString(),
        K("이미지 및 프로젝트 (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff *.rpaint);;모든 파일 (*)"));
    if (!path.isEmpty()) openPath(path, asLayer);
}
void MainWindow::exportDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(K("내보내기"));
    QFormLayout *f = new QFormLayout(&dlg);
    QComboBox *fmt = new QComboBox(&dlg);
    fmt->addItem(K("PNG (투명 지원)"), "png");
    fmt->addItem("JPG", "jpg");
    if (QImageWriter::supportedImageFormats().contains("webp")) fmt->addItem("WebP", "webp");
    QSlider *q = new QSlider(Qt::Horizontal, &dlg);
    q->setRange(1, 100);
    q->setValue(90);
    q->setEnabled(false);
    QLabel *ql = new QLabel("90", &dlg);
    connect(q, &QSlider::valueChanged, ql, [ql](int v) { ql->setNum(v); });
    connect(fmt, QOverload<int>::of(&QComboBox::currentIndexChanged), q, [q](int i) { q->setEnabled(i != 0); });
    QHBoxLayout *qrow = new QHBoxLayout;
    qrow->addWidget(q);
    qrow->addWidget(ql);
    f->addRow(K("형식"), fmt);
    f->addRow(K("품질"), qrow);
    if (!execForm(dlg, f)) return;
    const QString ext = fmt->currentData().toString();
    QString path = QFileDialog::getSaveFileName(this, K("내보내기"), ed.name + "." + ext, QString("%1 (*.%2)").arg(ext.toUpper(), ext));
    if (path.isEmpty()) return;
    if (!path.endsWith("." + ext, Qt::CaseInsensitive)) path += "." + ext;
    if (!ed.exportImage(path, ext.toLatin1(), q->value())) QMessageBox::warning(this, "R Painter", K("저장하지 못했습니다: %1").arg(path));
    else statusBar()->showMessage(K("저장됨: %1").arg(path), 4000);
}
void MainWindow::saveProjectDialog() {
    QString path = QFileDialog::getSaveFileName(this, K("프로젝트 저장"), ed.name + ".rpaint", K("R Painter 프로젝트 (*.rpaint)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".rpaint", Qt::CaseInsensitive)) path += ".rpaint";
    if (!ed.saveProject(path)) QMessageBox::warning(this, "R Painter", K("저장하지 못했습니다: %1").arg(path));
    else { statusBar()->showMessage(K("저장됨: %1").arg(path), 4000); refresh(); }
}
void MainWindow::adjustDialog() {
    Layer *l = ed.editable();
    if (!l) return;
    const QImage src = l->img, sel = ed.d.sel;
    QDialog dlg(this);
    dlg.setWindowTitle(K("색조 / 채도 / 밝기 / 대비"));
    QFormLayout *f = new QFormLayout(&dlg);
    QSlider *s[4];
    const QPair<const char *, int> defs[4] = {{"색조 (Hue)", 180}, {"채도", 100}, {"밝기", 100}, {"대비", 100}};
    auto matrix = [&] { return colorMatrix(s[0]->value(), s[1]->value(), s[2]->value(), s[3]->value()); };
    auto apply = [&] {
        Layer *a = ed.active();
        a->img = src;
        applyColor(a->img, src, matrix(), sel.isNull() ? nullptr : &sel);
        ed.damage();
    };
    for (int i = 0; i < 4; i++) {
        s[i] = new QSlider(Qt::Horizontal, &dlg);
        s[i]->setRange(-defs[i].second, defs[i].second);
        s[i]->setMinimumWidth(260);
        QLabel *val = new QLabel("0", &dlg);
        val->setFixedWidth(36);
        QHBoxLayout *row = new QHBoxLayout;
        row->addWidget(s[i]);
        row->addWidget(val);
        f->addRow(K(defs[i].first), row);
        connect(s[i], &QSlider::valueChanged, &dlg, [&apply, val](int v) { val->setNum(v); apply(); });
    }
    f->addRow(new QLabel(sel.isNull() ? K("현재 레이어 전체에 적용됩니다") : K("선택 영역에만 적용됩니다"), &dlg));
    if (execForm(dlg, f)) {
        apply();
        ed.colorExt(*ed.active(), matrix());
        ed.commit();
    } else {
        ed.active()->img = src;
        ed.damage();
    }
}
void MainWindow::imageSizeDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(K("이미지 크기"));
    QFormLayout *f = new QFormLayout(&dlg);
    QSpinBox *w = dimSpin(&dlg, ed.d.w), *h = dimSpin(&dlg, ed.d.h);
    QCheckBox *lock = new QCheckBox(K("비율 유지"), &dlg);
    lock->setChecked(true);
    const double ar = double(ed.d.w) / ed.d.h;
    connect(w, QOverload<int>::of(&QSpinBox::valueChanged), &dlg, [=](int v) { if (lock->isChecked()) { h->blockSignals(true); h->setValue(qMax(1, qRound(v / ar))); h->blockSignals(false); } });
    connect(h, QOverload<int>::of(&QSpinBox::valueChanged), &dlg, [=](int v) { if (lock->isChecked()) { w->blockSignals(true); w->setValue(qMax(1, qRound(v * ar))); w->blockSignals(false); } });
    f->addRow(K("너비"), w);
    f->addRow(K("높이"), h);
    f->addRow(lock);
    if (!execForm(dlg, f)) return;
    ed.resizeImage(w->value(), h->value());
    canvas->fit();
}
void MainWindow::canvasSizeDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(K("캔버스 크기"));
    QFormLayout *f = new QFormLayout(&dlg);
    QSpinBox *w = dimSpin(&dlg, ed.d.w), *h = dimSpin(&dlg, ed.d.h);
    QComboBox *anchor = new QComboBox(&dlg);
    anchor->addItems({K("왼쪽 위"), K("위"), K("오른쪽 위"), K("왼쪽"), K("가운데"), K("오른쪽"), K("왼쪽 아래"), K("아래"), K("오른쪽 아래")});
    anchor->setCurrentIndex(4);
    f->addRow(K("너비"), w);
    f->addRow(K("높이"), h);
    f->addRow(K("기준점"), anchor);
    if (!execForm(dlg, f)) return;
    ed.resizeCanvas(w->value(), h->value(), anchor->currentIndex());
    canvas->fit();
}
void MainWindow::selModifyDialog(const QString &title, int def, const std::function<void(Alpha &, int)> &fn) {
    if (ed.d.sel.isNull()) return ed.toast(K("선택 영역이 없습니다"));
    bool ok = false;
    const int r = QInputDialog::getInt(this, title, K("반경 (px)"), def, 1, 100, 1, &ok);
    if (ok) ed.modifySel([&](Alpha &a) { fn(a, r); });
}
// Photoshop: number keys set brush opacity with a painting tool, layer opacity otherwise
void MainWindow::setOpacityKey(int percent) {
    const Tool t = canvas->tool;
    if (t == Tool::Brush || t == Tool::Eraser || t == Tool::Bucket || t == Tool::Gradient) {
        canvas->opt.opacity = percent;
        for (const auto &s : optSyncers) s();
    } else if (Layer *l = ed.active()) {
        l->opacity = percent / 100.0;
        ed.commit();
        ed.damage();
    }
}
void MainWindow::selectLayer(int dir) {
    const int j = ed.activeIdx() + dir;
    if (j < 0 || j >= ed.d.layers.size()) return;
    ed.d.activeId = ed.d.layers[j].id;
    refresh();
}
void MainWindow::copy(bool cut, bool merged) {
    if (ed.copySel(cut, merged)) QApplication::clipboard()->setImage(ed.clip);
}
void MainWindow::paste() {
    const QImage im = QApplication::clipboard()->image();
    // our own copy comes back with the same size: paste it in place
    if (!ed.clip.isNull() && (im.isNull() || im.size() == ed.clip.size())) ed.pasteImage(ed.clip, ed.clipPos);
    else if (!im.isNull()) ed.placeImage(im, K("붙여넣기"));
    else ed.toast(K("클립보드가 비어 있습니다"));
}

// ---------- window events ----------
void MainWindow::closeEvent(QCloseEvent *e) {
    if (confirmDiscard()) e->accept(); else e->ignore();
}
void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *e) {
    const QList<QUrl> urls = e->mimeData()->urls();
    if (urls.isEmpty() || !urls.first().isLocalFile()) return;
    openPath(urls.first().toLocalFile(), ed.histIdx > 0); // untouched document: open; otherwise add as a layer
}
