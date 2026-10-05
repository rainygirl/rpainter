// Built-in color picker: saturation/value square, hue strip, hex / RGB / HSV fields.
#pragma once
#include <QDialog>
#include <functional>

class QLabel;
class QLineEdit;
class QSpinBox;

// Shared base for the two click-and-drag color areas.
class ColorArea : public QWidget {
public:
    using QWidget::QWidget;
    std::function<void(double x, double y)> picked; // position in 0..1
protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
};
class SvSquare : public ColorArea {
public:
    explicit SvSquare(QWidget *parent);
    double hue = 0, sat = 0, val = 0;
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QImage cache;
    double cacheHue = -1;
};
class HueStrip : public ColorArea {
public:
    explicit HueStrip(QWidget *parent);
    double hue = 0;
protected:
    void paintEvent(QPaintEvent *) override;
};

class ColorPicker : public QDialog {
public:
    ColorPicker(const QString &title, QColor initial, QWidget *parent = nullptr);
    QColor color() const;
    void setColor(QColor c);
    // Returns false if the user cancelled; otherwise stores the chosen color in `c`.
    static bool pick(QWidget *parent, const QString &title, QColor &c);

    SvSquare *square;
    HueStrip *strip;
    QLineEdit *hex;
    bool keepHex = false; // while the hex field itself is being typed in
    QSpinBox *rgb[3], *hsv[3];

private:
    double h = 0, s = 0, v = 0; // hue kept separately so it survives gray colors
    bool syncing = false;
    QLabel *newSwatch;
    void sync();
};
