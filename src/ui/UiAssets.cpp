#include "UiAssets.h"

#include <QImage>
#include <QLinearGradient>
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
        case UiAssets::Glyph::ChevronLeft:
            painter.drawLine(QPointF(16.0, 5.0), QPointF(9.0, 12.0));
            painter.drawLine(QPointF(9.0, 12.0), QPointF(16.0, 19.0));
            break;
        case UiAssets::Glyph::ArrowUp:
            painter.drawLine(QPointF(12.0, 19.0), QPointF(12.0, 5.0));
            painter.drawLine(QPointF(12.0, 5.0), QPointF(6.5, 10.5));
            painter.drawLine(QPointF(12.0, 5.0), QPointF(17.5, 10.5));
            break;
        case UiAssets::Glyph::NewFolder: {
            QPainterPath folder;
            folder.moveTo(2.5, 8.0);
            folder.lineTo(8.0, 8.0);
            folder.lineTo(10.0, 10.0);
            folder.lineTo(19.5, 10.0);
            folder.lineTo(18.5, 19.0);
            folder.lineTo(2.5, 19.0);
            folder.closeSubpath();
            painter.drawPath(folder);
            painter.drawLine(QPointF(16.5, 3.5), QPointF(16.5, 9.0));
            painter.drawLine(QPointF(13.75, 6.25), QPointF(19.25, 6.25));
            break;
        }
        case UiAssets::Glyph::ListView:
            for (qreal y : {6.0, 12.0, 18.0}) {
                painter.setBrush(color);
                painter.drawEllipse(QPointF(5.0, y), 1.0, 1.0);
                painter.setBrush(Qt::NoBrush);
                painter.drawLine(QPointF(9.0, y), QPointF(20.0, y));
            }
            break;
        case UiAssets::Glyph::DetailView:
            painter.drawRoundedRect(QRectF(3.0, 4.0, 18.0, 16.0), 1.0, 1.0);
            painter.drawLine(QPointF(3.0, 9.0), QPointF(21.0, 9.0));
            painter.drawLine(QPointF(9.0, 4.0), QPointF(9.0, 20.0));
            painter.drawLine(QPointF(15.0, 4.0), QPointF(15.0, 20.0));
            painter.drawLine(QPointF(3.0, 14.5), QPointF(21.0, 14.5));
            break;
        case UiAssets::Glyph::ChevronRight:
            painter.drawLine(QPointF(8.0, 5.0), QPointF(15.0, 12.0));
            painter.drawLine(QPointF(15.0, 12.0), QPointF(8.0, 19.0));
            break;
    }
}

} // namespace

QIcon UiAssets::icon(Glyph glyph, const QColor& color, int logicalSize) {
    QIcon result;
    // Supplying only a 2x pixmap makes Qt's non-native file dialog render the
    // glyph at half size on 1x/offscreen surfaces. Register both density
    // variants so every QToolButton receives the requested logical size.
    for (const qreal scale : {1.0, kDeviceScale}) {
        const int pixels = qMax(1, qRound(logicalSize * scale));
        QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(pixels / kDesignSize, pixels / kDesignSize);
        paintGlyph(painter, glyph, color);
        painter.end();

        QPixmap pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(scale);
        result.addPixmap(pixmap);
    }
    return result;
}

QPixmap UiAssets::logoMark(int logicalSize) {
    QPixmap brand(QStringLiteral(":/branding/Nebulens.png"));
    if (!brand.isNull()) {
        const int pixels = qMax(1, qRound(logicalSize * kDeviceScale));
        QPixmap scaled = brand.scaled(
            pixels, pixels, Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(kDeviceScale);
        return scaled;
    }

    const int pixels = qMax(1, qRound(logicalSize * kDeviceScale));
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(pixels / 32.0, pixels / 32.0);

    QLinearGradient plate(0.0, 0.0, 0.0, 32.0);
    plate.setColorAt(0.0, QColor("#292D2E"));
    plate.setColorAt(1.0, QColor("#151819"));
    painter.setPen(QPen(QColor("#414849"), 1.0));
    painter.setBrush(plate);
    painter.drawRoundedRect(QRectF(2.5, 2.5, 27.0, 27.0), 6.0, 6.0);

    // Offset registration frames describe alignment and stacking. The open
    // amber frame and cool registration point form the Nebulens mark
    // without relying on a camera, magic wand or decorative star icon.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(iconPen(QColor("#59605F"), 1.2));
    painter.drawRoundedRect(QRectF(10.0, 9.0, 14.0, 14.0), 3.0, 3.0);
    painter.setPen(iconPen(QColor("#777A74"), 1.2));
    painter.drawRoundedRect(QRectF(8.0, 7.0, 14.0, 14.0), 3.0, 3.0);

    painter.setPen(iconPen(QColor("#D5AA67"), 1.8));
    QPainterPath front;
    front.moveTo(18.5, 5.5);
    front.lineTo(6.0, 5.5);
    front.quadTo(4.5, 5.5, 4.5, 7.0);
    front.lineTo(4.5, 19.5);
    front.quadTo(4.5, 21.0, 6.0, 21.0);
    front.lineTo(18.5, 21.0);
    painter.drawPath(front);

    painter.setPen(QPen(QColor(111, 168, 163, 80), 4.5));
    painter.drawPoint(QPointF(14.0, 13.5));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#78A8A2"));
    painter.drawEllipse(QPointF(14.0, 13.5), 2.2, 2.2);
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
