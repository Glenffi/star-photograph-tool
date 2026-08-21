#include "UiAssets.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

namespace {

constexpr qreal kDesignSize = 24.0;
constexpr qreal kDeviceScale = 2.0;

QPen iconPen(const QColor& color, qreal width = 1.5) {
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
        case UiAssets::Glyph::Scenes:
            painter.drawRoundedRect(QRectF(3.5, 4.0, 7.0, 7.0), 1.5, 1.5);
            painter.drawRoundedRect(QRectF(13.5, 4.0, 7.0, 7.0), 1.5, 1.5);
            painter.drawRoundedRect(QRectF(3.5, 14.0, 7.0, 6.0), 1.5, 1.5);
            painter.drawRoundedRect(QRectF(13.5, 14.0, 7.0, 6.0), 1.5, 1.5);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(7.0, 7.5), 1.2, 1.2);
            painter.drawEllipse(QPointF(17.0, 17.0), 1.2, 1.2);
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
        case UiAssets::Glyph::SingleFrame:
            painter.drawRoundedRect(QRectF(3.5, 4.5, 17.0, 15.0), 2.5, 2.5);
            painter.drawEllipse(QPointF(9.0, 10.0), 2.2, 2.2);
            painter.drawLine(QPointF(5.5, 17.0), QPointF(10.0, 13.0));
            painter.drawLine(QPointF(10.0, 13.0), QPointF(13.0, 15.5));
            painter.drawLine(QPointF(13.0, 15.5), QPointF(17.0, 12.0));
            painter.drawLine(QPointF(17.0, 12.0), QPointF(20.0, 15.0));
            break;
        case UiAssets::Glyph::Nightscape: {
            QPainterPath ridge;
            ridge.moveTo(3.0, 18.5);
            ridge.lineTo(7.0, 14.0);
            ridge.lineTo(10.0, 16.5);
            ridge.lineTo(14.0, 11.5);
            ridge.lineTo(21.0, 18.5);
            painter.drawPath(ridge);
            painter.drawLine(QPointF(3.0, 19.5), QPointF(21.0, 19.5));
            drawSpark(painter, QPointF(7.0, 7.0), 3.2, 0.9, color);
            painter.drawEllipse(QPointF(16.5, 6.5), 1.0, 1.0);
            break;
        }
        case UiAssets::Glyph::DeepSky:
            painter.drawEllipse(QPointF(12.0, 12.0), 3.0, 3.0);
            painter.save();
            painter.translate(12.0, 12.0);
            painter.rotate(-28.0);
            painter.drawEllipse(QPointF(0.0, 0.0), 9.0, 4.8);
            painter.restore();
            drawSpark(painter, QPointF(18.5, 5.5), 3.0, 0.8, color);
            painter.drawEllipse(QPointF(5.0, 17.5), 1.0, 1.0);
            break;
        case UiAssets::Glyph::SkyGround:
            painter.drawLine(QPointF(3.0, 12.0), QPointF(21.0, 12.0));
            painter.drawLine(QPointF(3.0, 18.5), QPointF(8.0, 15.0));
            painter.drawLine(QPointF(8.0, 15.0), QPointF(11.5, 17.0));
            painter.drawLine(QPointF(11.5, 17.0), QPointF(16.0, 13.5));
            painter.drawLine(QPointF(16.0, 13.5), QPointF(21.0, 18.5));
            drawSpark(painter, QPointF(8.0, 6.5), 3.0, 0.9, color);
            painter.drawEllipse(QPointF(17.0, 6.0), 1.0, 1.0);
            break;
        case UiAssets::Glyph::StarTrail:
            painter.drawArc(QRectF(3.0, 3.0, 18.0, 18.0), 25 * 16, 82 * 16);
            painter.drawArc(QRectF(6.0, 6.0, 12.0, 12.0), 34 * 16, 88 * 16);
            painter.drawArc(QRectF(9.0, 9.0, 6.0, 6.0), 42 * 16, 96 * 16);
            painter.drawEllipse(QPointF(19.0, 8.0), 1.1, 1.1);
            painter.drawEllipse(QPointF(16.0, 11.5), 0.8, 0.8);
            painter.drawEllipse(QPointF(13.5, 13.0), 0.6, 0.6);
            break;
        case UiAssets::Glyph::Timelapse:
            painter.drawRoundedRect(QRectF(2.5, 7.0, 10.0, 9.0), 1.5, 1.5);
            painter.drawRoundedRect(QRectF(6.5, 5.0, 10.0, 11.0), 1.5, 1.5);
            painter.drawRoundedRect(QRectF(10.5, 7.0, 10.0, 9.0), 1.5, 1.5);
            painter.drawLine(QPointF(8.0, 19.0), QPointF(16.0, 19.0));
            painter.drawLine(QPointF(14.0, 17.0), QPointF(16.0, 19.0));
            painter.drawLine(QPointF(14.0, 21.0), QPointF(16.0, 19.0));
            drawSpark(painter, QPointF(12.0, 10.5), 2.5, 0.7, color);
            break;
        case UiAssets::Glyph::Eyedropper:
            painter.drawRoundedRect(QRectF(13.0, 3.5, 7.0, 7.0), 1.5, 1.5);
            painter.drawLine(QPointF(14.5, 9.0), QPointF(7.0, 16.5));
            painter.drawLine(QPointF(11.0, 5.5), QPointF(18.0, 12.5));
            painter.drawLine(QPointF(7.0, 16.5), QPointF(4.0, 19.5));
            painter.drawLine(QPointF(4.0, 19.5), QPointF(8.5, 18.5));
            break;
        case UiAssets::Glyph::Brush:
            painter.drawLine(QPointF(14.5, 4.0), QPointF(7.5, 14.0));
            painter.drawLine(QPointF(18.5, 7.0), QPointF(10.5, 16.0));
            painter.drawLine(QPointF(14.5, 4.0), QPointF(18.5, 7.0));
            painter.drawRoundedRect(QRectF(4.0, 14.0, 7.0, 6.0), 2.5, 2.5);
            break;
        case UiAssets::Glyph::Undo:
            painter.drawLine(QPointF(4.0, 9.0), QPointF(9.0, 5.0));
            painter.drawLine(QPointF(4.0, 9.0), QPointF(9.0, 13.0));
            painter.drawArc(QRectF(5.0, 6.0, 15.0, 13.0), 25 * 16,
                            245 * 16);
            break;
        case UiAssets::Glyph::Reset:
            painter.drawArc(QRectF(4.0, 4.0, 16.0, 16.0), 35 * 16,
                            290 * 16);
            painter.drawLine(QPointF(4.0, 5.0), QPointF(4.0, 10.0));
            painter.drawLine(QPointF(4.0, 5.0), QPointF(9.0, 5.0));
            break;
        case UiAssets::Glyph::Done:
            painter.drawLine(QPointF(4.0, 12.5), QPointF(9.5, 18.0));
            painter.drawLine(QPointF(9.5, 18.0), QPointF(20.0, 6.0));
            break;
        case UiAssets::Glyph::ChevronRight:
            painter.drawLine(QPointF(8.0, 5.0), QPointF(15.0, 12.0));
            painter.drawLine(QPointF(15.0, 12.0), QPointF(8.0, 19.0));
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
