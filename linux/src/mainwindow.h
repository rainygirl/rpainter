// Application window: menus, tool bar, tool options, color and layer panels, dialogs.
#pragma once
#include "canvas.h"
#include <QMainWindow>

class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QListWidget;
class QMenu;
class QPushButton;
class QSlider;
class QToolBar;

class QApplication;
void applyTheme(QApplication &app);

class MainWindow : public QMainWindow {
public:
    MainWindow();

    Editor ed;
    Canvas *canvas;
    bool openPath(const QString &path, bool asLayer);
    void setTool(Tool t);
    void refresh();

protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    struct OptWidget { QAction *action; QVector<Tool> tools; bool xfOnly; };
    QVector<OptWidget> optWidgets;
    QVector<std::function<void()>> optSyncers;
    QVector<QAction *> toolActions;
    QToolBar *optBar = nullptr;
    QLabel *toolLabel = nullptr, *zoomLabel = nullptr, *sizeLabel = nullptr, *posLabel = nullptr, *opacityLabel = nullptr;
    QPushButton *fgBtn = nullptr, *bgBtn = nullptr;
    QListWidget *layerList = nullptr;
    QComboBox *blendBox = nullptr;
    QSlider *opacitySlider = nullptr;
    bool refreshing = false, refreshPending = false;

    QAction *act(QMenu *menu, const QString &text, const QString &keys, std::function<void()> fn, bool viewCmd = false);
    void run(const std::function<void()> &fn, bool viewCmd = false);
    void buildMenus();
    void buildTools();
    void buildOptions();
    void buildPanels();
    void scheduleRefresh();
    void syncLayerCtl();
    void updateOptions();
    void updateColors();
    void updateStatus();
    bool confirmDiscard();

    void newDialog();
    void openDialog(bool asLayer);
    void exportDialog();
    void saveProjectDialog();
    void adjustDialog();
    void imageSizeDialog();
    void canvasSizeDialog();
    void selModifyDialog(const QString &title, int def, const std::function<void(Alpha &, int)> &fn);
    void copy(bool cut, bool merged = false);
    void selectLayer(int dir);
    void setOpacityKey(int percent);
    void paste();
};
