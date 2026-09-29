#include "IconEffect.h"

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QTransform>
#include <QVector>

namespace {

// One pixel taller than the 32px layout box: the artwork keeps its full 32px
// and the spare pixel is where the shadow goes, rather than the shadow eating
// into the art.
constexpr int kCanvas = 33;
constexpr qreal kRecedeX = 0.13;   // how far the right edge pulls in
constexpr qreal kRecedeY = 0.10;   // and how much it shortens
constexpr int kShadowDx = 1;
constexpr int kShadowDy = 1;
constexpr int kShadowBlur = 1;
constexpr int kShadowAlpha = 140;

// The tilt is built at this multiple of the final size and resampled down
// once at the very end. Warping artwork that is already only ~30px wide
// smears it; doing the work large and downsampling once keeps the edges
// crisp.
constexpr int kSupersample = 4;

// Full size, flush with the top of the canvas, so a styled icon's artwork
// matches an un-styled 32px one exactly and only the shadow occupies the
// extra pixel below it.
constexpr int kArt = 32;

// Separable box blur over the alpha channel only; the shadow is flat black,
// so the colour channels never change.
void blurAlpha(QImage &image, int radius) {
    if (radius <= 0) return;

    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0) return;

    QVector<int> pass(w * h, 0);

    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            int count = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = x + k;
                if (sx < 0 || sx >= w) continue;
                sum += qAlpha(line[sx]);
                ++count;
            }
            pass[y * w + x] = count ? sum / count : 0;
        }
    }

    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            int sum = 0;
            int count = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = y + k;
                if (sy < 0 || sy >= h) continue;
                sum += pass[sy * w + x];
                ++count;
            }
            QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            line[x] = qRgba(0, 0, 0, count ? sum / count : 0);
        }
    }
}

} // namespace

QIcon xpStyleIcon(const QIcon &source) {
    if (source.isNull()) return source;

    const QList<QSize> sizes = source.availableSizes();
    const QSize nativeSize = sizes.isEmpty() ? QSize(32, 32) : sizes.first();
    QImage art = source.pixmap(nativeSize).toImage();
    if (art.isNull()) return source;

    // Nearest-neighbour on the way up: it preserves the hard pixel edges of
    // the original artwork instead of pre-blurring them. The single smooth
    // downscale at the end is what does the antialiasing. Artwork bigger than
    // the working size is coming down rather than up, though -- a 256px icon
    // the user asked to style -- and nearest-neighbour would drop every other
    // pixel of it.
    const int working = kArt * kSupersample;
    art = art.convertToFormat(QImage::Format_ARGB32)
              .scaled(working, working, Qt::KeepAspectRatio,
                      art.width() > working ? Qt::SmoothTransformation
                                            : Qt::FastTransformation);

    // Pull the right-hand edge in and shorten it: the eye reads the result
    // as a square tipped away from the viewer.
    const qreal w = art.width();
    const qreal h = art.height();
    const qreal dx = w * kRecedeX;
    const qreal dy = h * kRecedeY;

    QPolygonF from;
    from << QPointF(0, 0) << QPointF(w, 0) << QPointF(w, h) << QPointF(0, h);
    QPolygonF to;
    to << QPointF(0, 0) << QPointF(w - dx, dy / 2) << QPointF(w - dx, h - dy / 2)
       << QPointF(0, h);

    QTransform transform;
    if (!QTransform::quadToQuad(from, to, transform)) return source;

    const QImage warped = art.transformed(transform, Qt::SmoothTransformation);
    if (warped.isNull()) return source;

    // Silhouette of the warped art, padded so the blur is not clipped.
    const int blurRadius = kShadowBlur * kSupersample;
    const int margin = blurRadius * 2;
    QImage shadow(warped.width() + margin * 2, warped.height() + margin * 2,
                  QImage::Format_ARGB32);
    shadow.fill(Qt::transparent);
    for (int y = 0; y < warped.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(warped.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(shadow.scanLine(y + margin));
        for (int x = 0; x < warped.width(); ++x) {
            dst[x + margin] = qRgba(0, 0, 0, qAlpha(src[x]) * kShadowAlpha / 255);
        }
    }
    blurAlpha(shadow, blurRadius);

    QImage canvas(kCanvas * kSupersample, kCanvas * kSupersample,
                  QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const int x = (canvas.width() - warped.width()) / 2;
        const int y = 0; // flush with the top, matching un-styled icons
        painter.drawImage(x - margin + kShadowDx * kSupersample,
                          y - margin + kShadowDy * kSupersample, shadow);
        painter.drawImage(x, y, warped);
    }

    return QIcon(QPixmap::fromImage(
        canvas.scaled(kCanvas, kCanvas, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
}
