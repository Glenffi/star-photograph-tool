#include "UiAssets.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

namespace {

constexpr qreal kDesignSize = 24.0;
constexpr qreal kDeviceScale = 2.0;

QPen iconPen(const QColor& color, qreal width = 1.7) {
    QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    return pen;
}

void drawSpark(QPainter& painter, const QPointF& center, qreal outer,
               qreal inner, const QColor& color) {
    QPainterPath path;
    path.moveTo(center.x(), center.y() - outer);
    path.lineTo(center.x() + inner, center.y() - inner);
    path.lineTo(center.x() + outer, center.y());
    path.lineTo(center.x() + inner, center.y() + inner);
    path.lineTo(center.x(), center.y() + outer);
    path.lineTo(center.x() - inner, center.y() + inner);
    path.lineTo(center.x() - outer, center.y());
    path.lineTo(center.x() - inner, center.y() - inner);
    path.closeSubpath();
    painter.fillPath(path, color);
}

void paintGlyph(QPainter& painter, UiAssets::Glyph glyph,
                const QColor& color) {
    painter.setPen(iconPen(color));
    painter.setBrush(Qt::NoBrush);

    switch (glyph) {
        case UiAssets::Glyph::AddPhotos:
            painter.drawRoundedRect(QRectF(3.5, 6.5, 14.0, 12.0), 2.0, 2.0);
            painter.drawLine(QPointF(7.0, 3.5), QPointF(18.5, 3.5));
            painter.drawLine(QPointF(20.0, 7.0), QPointF(20.0, 13.0));
            painter.drawLine(QPointF(17.0, 10.0), QPointF(23.0, 10.0));
            break;
        case UiAssets::Glyph::Add:
            painter.drawLine(QPointF(12.0, 4.5), QPointF(12.0, 19.5));
            painter.drawLine(QPointF(4.5, 12.0), QPointF(19.5, 12.0));
            break;
        case UiAssets::Glyph::Folder: {
            QPainterPath folder;
            folder.moveTo(3.0, 7.0);
            folder.lineTo(8.5, 7.0);
            folder.lineTo(10.5, 9.0);
            folder.lineTo(21.0, 9.0);
            folder.lineTo(20.0, 19.0);
            folder.lineTo(3.0, 19.0);
            folder.closeSubpath();
            painter.drawPath(folder);
            painter.drawLine(QPointF(3.0, 7.0), QPointF(3.0, 5.0));
            painter.drawLine(QPointF(3.0, 5.0), QPointF(9.0, 5.0));
            break;
        }
        case UiAssets::Glyph::Trash:
            painter.drawLine(QPointF(5.0, 7.0), QPointF(19.0, 7.0));
            painter.drawLine(QPointF(9.0, 4.0), QPointF(15.0, 4.0));
            painter.drawRoundedRect(QRectF(7.0, 7.0, 10.0, 13.0), 1.5, 1.5);
            painter.drawLine(QPointF(10.0, 10.5), QPointF(10.0, 16.5));
            painter.drawLine(QPointF(14.0, 10.5), QPointF(14.0, 16.5));
            break;
        case UiAssets::Glyph::Export:
            painter.drawRoundedRect(QRectF(4.0, 14.0, 16.0, 6.0), 1.5, 1.5);
            painter.drawLine(QPointF(12.0, 16.0), QPointF(12.0, 4.0));
            painter.drawLine(QPointF(8.0, 8.0), QPointF(12.0, 4.0));
            painter.drawLine(QPointF(16.0, 8.0), QPointF(12.0, 4.0));
            break;
        case UiAssets::Glyph::Play: {
            QPolygonF triangle;
            triangle << QPointF(8.0, 5.0) << QPointF(19.0, 12.0)
                     << QPointF(8.0, 19.0);
            painter.setBrush(color);
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(triangle);
            break;
        }
        case UiAssets::Glyph::Sliders:
            painter.drawLine(QPointF(4.0, 6.0), QPointF(20.0, 6.0));
            painter.drawLine(QPointF(4.0, 12.0), QPointF(20.0, 12.0));
            painter.drawLine(QPointF(4.0, 18.0), QPointF(20.0, 18.0));
            painter.setBrush(color);
            painter.drawEllipse(QPointF(9.0, 6.0), 2.0, 2.0);
            painter.drawEllipse(QPointF(15.0, 12.0), 2.0, 2.0);
            painter.drawEllipse(QPointF(7.0, 18.0), 2.0, 2.0);
            break;
        case UiAssets::Glyph::Info:
            painter.drawEllipse(QPointF(12.0, 12.0), 8.0, 8.0);
            painter.drawLine(QPointF(12.0, 10.5), QPointF(12.0, 16.0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(12.0, 7.0), 1.1, 1.1);
            break;
        case UiAssets::Glyph::Fit:
            painter.drawLine(QPointF(4.0, 9.0), QPointF(4.0, 4.0));
            painter.drawLine(QPointF(4.0, 4.0), QPointF(9.0, 4.0));
            painter.drawLine(QPointF(15.0, 4.0), QPointF(20.0, 4.0));
            painter.drawLine(QPointF(20.0, 4.0), QPointF(20.0, 9.0));
            painter.drawLine(QPointF(20.0, 15.0), QPointF(20.0, 20.0));
            painter.drawLine(QPointF(20.0, 20.0), QPointF(15.0, 20.0));
            painter.drawLine(QPointF(9.0, 20.0), QPointF(4.0, 20.0));
            painter.drawLine(QPointF(4.0, 20.0), QPointF(4.0, 15.0));
            break;
        case UiAssets::Glyph::ZoomIn:
        case UiAssets::Glyph::ZoomOut:
            painter.drawEllipse(QPointF(10.0, 10.0), 5.5, 5.5);
            painter.drawLine(QPointF(14.0, 14.0), QPointF(20.0, 20.0));
            painter.drawLine(QPointF(7.0, 10.0), QPointF(13.0, 10.0));
            if (glyph == UiAssets::Glyph::ZoomIn) {
                painter.drawLine(QPointF(10.0, 7.0), QPointF(10.0, 13.0));
            }
            break;
        case UiAssets::Glyph::Result:
            painter.drawRoundedRect(QRectF(3.5, 5.5, 15.0, 13.0), 2.0, 2.0);
            drawSpark(painter, QPointF(18.0, 7.0), 4.5, 1.3, color);
            break;
    }
}

} // namespace

QIcon UiAssets::icon(Glyph glyph, const QColor& color, int logicalSize) {
    const int pixels = qMax(1, qRound(logicalSize * kDeviceScale));
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(pixels / kDesignSize, pixels / kDesignSize);
    paintGlyph(painter, glyph, color);
    painter.end();

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(kDeviceScale);
    return QIcon(pixmap);
}

QPixmap UiAssets::logoMark(int logicalSize) {
    const int pixels = qMax(1, qRound(logicalSize * kDeviceScale));
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(pixels / 32.0, pixels / 32.0);

    // Two offset frames express stack alignment; the star identifies the
    // subject without relying on a generic camera silhouette.
    painter.setPen(iconPen(QColor("#6FA8FF"), 1.8));
    painter.setBrush(QColor(111, 168, 255, 34));
    painter.drawRoundedRect(QRectF(8.0, 9.0, 17.0, 17.0), 4.0, 4.0);
    painter.setPen(iconPen(QColor("#4ED7AE"), 2.0));
    painter.setBrush(QColor(78, 215, 174, 28));
    painter.drawRoundedRect(QRectF(5.0, 6.0, 17.0, 17.0), 4.0, 4.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#4ED7AE"));
    painter.drawEllipse(QPointF(13.5, 14.5), 3.2, 3.2);
    drawSpark(painter, QPointF(23.5, 7.5), 5.0, 1.4, QColor("#F2B65A"));
    painter.end();

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(kDeviceScale);
    return pixmap;
}

QIcon UiAssets::appIcon() {
    QIcon result;
    for (int size : {16, 32, 64, 128}) {
        result.addPixmap(logoMark(size));
    }
    return result;
}
