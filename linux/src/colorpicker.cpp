#include "colorpicker.h"
#include "editor.h"
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

static QColor hsvColor(double h, double s, double v) { return QColor::fromHsvF(qBound(0.0, h / 360, 0.9999), qBound(0.0, s, 1.0), qBound(0.0, v, 1.0)); }

void ColorArea::mousePressEvent(QMouseEvent *e) { mouseMoveEvent(e); }
void ColorArea::mouseMoveEvent(QMouseEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF p = e->position();
#else
    const QPointF p = e->localPos();
#endif
    if (picked) picked(qBound(0.0, p.x() / (width() - 1), 1.0), qBound(0.0, p.y() / (height() - 1), 1.0));
}

SvSquare::SvSquare(QWidget *parent) : ColorArea(parent) {
    setFixedSize(256, 256);
    setCursor(Qt::CrossCursor);
}
void SvSquare::paintEvent(QPaintEvent *) {
    if (cacheHue != hue || cache.size() != size()) {
        cache = QImage(size(), QImage::Format_RGB32);
        for (int y = 0; y < height(); y++) {
            QRgb *row = reinterpret_cast<QRgb *>(cache.scanLine(y));
            for (int x = 0; x < width(); x++) row[x] = hsvColor(hue, x / double(width() - 1), 1 - y / double(height() - 1)).rgb();
        }
        cacheHue = hue;
    }
    QPainter p(this);
    p.drawImage(0, 0, cache);
    const QPointF c(sat * (width() - 1), (1 - val) * (height() - 1));
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::black, 1));
    p.drawEllipse(c, 6, 6);
    p.setPen(QPen(Qt::white, 1.5));
    p.drawEllipse(c, 5, 5);
}

HueStrip::HueStrip(QWidget *parent) : ColorArea(parent) {
    setFixedSize(28, 256);
    setCursor(Qt::CrossCursor);
}
void HueStrip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    for (int y = 0; y < height(); y++) p.fillRect(3, y, width() - 6, 1, hsvColor(360.0 * y / (height() - 1), 1, 1));
    const int y = qRound(hue / 360 * (height() - 1));
    p.setPen(Qt::black);
    p.setBrush(Qt::white);
    p.drawRect(0, y - 2, width() - 1, 4);
}

ColorPicker::ColorPicker(const QString &title, QColor initial, QWidget *parent) : QDialog(parent) {
    setWindowTitle(title);
    QHBoxLayout *main = new QHBoxLayout(this);
    square = new SvSquare(this);
    strip = new HueStrip(this);
    main->addWidget(square);
    main->addWidget(strip);
    QVBoxLayout *side = new QVBoxLayout;
    main->addLayout(side);

    // new color over the previous one, like Photoshop
    newSwatch = new QLabel(this);
    QLabel *oldSwatch = new QLabel(this);
    for (QLabel *l : {newSwatch, oldSwatch}) l->setFixedSize(120, 34);
    newSwatch->setToolTip(K("새 색상"));
    oldSwatch->setToolTip(K("현재 색상"));
    oldSwatch->setStyleSheet(QString("background: %1; border: 1px solid #111; border-top: 0;").arg(initial.name()));
    side->addWidget(newSwatch);
    side->addWidget(oldSwatch);
    side->addSpacing(10);

    QGridLayout *grid = new QGridLayout;
    const char *names[6] = {"H", "S", "B", "R", "G", "B"};
    const int maxs[6] = {359, 100, 100, 255, 255, 255};
    const char *suffix[6] = {" °", " %", " %", "", "", ""};
    for (int i = 0; i < 6; i++) {
        QSpinBox *sp = new QSpinBox(this);
        sp->setRange(0, maxs[i]);
        sp->setSuffix(suffix[i]);
        (i < 3 ? hsv[i] : rgb[i - 3]) = sp;
        grid->addWidget(new QLabel(names[i], this), i, 0);
        grid->addWidget(sp, i, 1);
        connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, i] {
            if (syncing) return;
            if (i < 3) { h = hsv[0]->value(); s = hsv[1]->value() / 100.0; v = hsv[2]->value() / 100.0; sync(); }
            else setColor(QColor(rgb[0]->value(), rgb[1]->value(), rgb[2]->value()));
        });
    }
    hex = new QLineEdit(this);
    hex->setMaxLength(7);
    grid->addWidget(new QLabel("#", this), 6, 0);
    grid->addWidget(hex, 6, 1);
    connect(hex, &QLineEdit::editingFinished, this, [this] {
        QString t = hex->text().trimmed();
        if (!t.startsWith('#')) t.prepend('#');
        const QColor c(t);
        if (c.isValid() && (t.size() == 7 || t.size() == 4)) setColor(c); else sync();
    });
    side->addLayout(grid);
    side->addStretch(1);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(K("확인"));
    bb->button(QDialogButtonBox::Cancel)->setText(K("취소"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    side->addWidget(bb);

    square->picked = [this](double x, double y) { s = x; v = 1 - y; sync(); };
    strip->picked = [this](double, double y) { h = qMin(y * 360, 359.999); sync(); };
    setColor(initial);
}
QColor ColorPicker::color() const { return hsvColor(h, s, v); }
void ColorPicker::setColor(QColor c) {
    if (c.hsvHue() >= 0) h = c.hsvHueF() * 360; // gray has no hue: keep the current one
    s = c.hsvSaturationF();
    v = c.valueF();
    sync();
}
void ColorPicker::sync() {
    syncing = true;
    const QColor c = color();
    square->hue = strip->hue = h;
    square->sat = s;
    square->val = v;
    square->update();
    strip->update();
    hsv[0]->setValue(qRound(h) % 360);
    hsv[1]->setValue(qRound(s * 100));
    hsv[2]->setValue(qRound(v * 100));
    rgb[0]->setValue(c.red());
    rgb[1]->setValue(c.green());
    rgb[2]->setValue(c.blue());
    hex->setText(c.name());
    newSwatch->setStyleSheet(QString("background: %1; border: 1px solid #111;").arg(c.name()));
    syncing = false;
}
bool ColorPicker::pick(QWidget *parent, const QString &title, QColor &c) {
    ColorPicker dlg(title, c, parent);
    if (dlg.exec() != QDialog::Accepted) return false;
    c = dlg.color();
    return true;
}
