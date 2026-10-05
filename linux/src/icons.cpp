#include "icons.h"
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QVector>

QIcon makeIcon(const char *spec, bool dashed) {
    QPainterPath path;
    QChar cmd;
    QVector<double> n;
    auto flush = [&] {
        if (cmd == 'M' && n.size() == 2) path.moveTo(n[0], n[1]);
        else if (cmd == 'L' && n.size() == 2) path.lineTo(n[0], n[1]);
        else if (cmd == 'C' && n.size() == 6) path.cubicTo(n[0], n[1], n[2], n[3], n[4], n[5]);
        else if (cmd == 'Z') path.closeSubpath();
        else if (cmd == 'O' && n.size() == 4) path.addEllipse(QPointF(n[0], n[1]), n[2], n[3]);
        else if (cmd == 'R' && n.size() == 4) path.addRect(n[0], n[1], n[2], n[3]);
        n.clear();
    };
    const QStringList tokens = QString::fromLatin1(spec).split(' ');
    for (QString t : tokens) {
        if (t.isEmpty()) continue;
        if (t[0].isLetter()) { flush(); cmd = t[0]; t.remove(0, 1); }
        if (!t.isEmpty()) n.append(t.toDouble());
    }
    flush();

    QPixmap pm(48, 48); // 2x for high-DPI screens
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(221, 221, 221), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (dashed) pen.setDashPattern({2, 1.6});
    p.setPen(pen);
    p.drawPath(path);
    return QIcon(pm);
}
