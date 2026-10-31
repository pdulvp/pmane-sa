#include "ClockPane.h"

#include "Theme.h"

#include <QDateTime>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// The strip down the left and the band across the top, as the month pane and
// the calculator have.
constexpr int kBannerWidth = 36;
constexpr int kHeaderHeight = 32;

// The dial is square, so the pane is as tall as it is wide below the band.
constexpr int kContentWidth = 240;
constexpr int kPaneWidth = kBannerWidth + kContentWidth;
constexpr int kMargin = 8;

// How far the hands reach, as a part of the dial's radius: the hour hand short
// and the minute hand long, as on a clock.
constexpr qreal kHourHand = 0.52;
constexpr qreal kMinuteHand = 0.76;
constexpr qreal kSecondHand = 0.84;
// The length behind the centre, which keeps a hand looking balanced.
constexpr qreal kHandTail = 0.14;

// The marks round the edge: all twelve reach the same rim, the quarters
// starting further in so they read as the quarters.
constexpr qreal kTickOuter = 0.94;
constexpr qreal kTickInner = 0.86;
constexpr qreal kQuarterInner = 0.78;

// Mixes `weight` of `over` into `under`.
QColor blend(const QColor &under, const QColor &over, qreal weight) {
    const qreal keep = 1.0 - weight;
    return QColor(qRound(under.red() * keep + over.red() * weight),
                  qRound(under.green() * keep + over.green() * weight),
                  qRound(under.blue() * keep + over.blue() * weight));
}

// One hand, drawn from the centre outwards. The painter is already centred on
// the dial; `degrees` is measured clockwise from twelve o'clock.
void drawHand(QPainter &painter, qreal degrees, qreal length, qreal radius, qreal width,
              const QColor &colour) {
    painter.save();
    painter.rotate(degrees);
    QPen pen(colour, width);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(QPointF(0, radius * kHandTail), QPointF(0, -radius * length));
    painter.restore();
}

} // namespace

ClockPane::ClockPane(QWidget *parent) : QWidget(parent, Qt::Popup) {
    m_heading = new QLabel(this);
    m_heading->setAlignment(Qt::AlignCenter);
    QFont heading = m_heading->font();
    heading.setBold(true);
    m_heading->setFont(heading);

    // The band behind it is the caption colour, so its text takes the caption
    // text colour rather than the window's.
    const Theme &theme = currentTheme();
    QPalette onCaption = palette();
    onCaption.setColor(QPalette::WindowText, theme.titleActiveText);
    onCaption.setColor(QPalette::Text, theme.titleActiveText);
    m_heading->setPalette(onCaption);

    // The band sits at the top right of the banner; the dial below it is
    // painted rather than laid out.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kBannerWidth, 0, 0, kMargin);
    layout->setSpacing(0);
    auto *headerHolder = new QWidget(this);
    headerHolder->setFixedHeight(kHeaderHeight);
    auto *header = new QVBoxLayout(headerHolder);
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(m_heading);
    layout->addWidget(headerHolder);
    layout->addStretch();

    // One second is the step the second hand moves in; the date in the band
    // changes once a day and is refreshed by the same tick.
    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, [this] {
        refreshHeading();
        update();
    });

    setFixedWidth(kPaneWidth);
    setFixedHeight(kHeaderHeight + kContentWidth + kMargin);
    refreshHeading();
}

void ClockPane::refreshHeading() {
    m_heading->setText(QLocale::system().toString(QDate::currentDate(), QLocale::LongFormat));
}

void ClockPane::popUpAt(QWidget *anchor) {
    refreshHeading();

    // Below the widget that opened it, kept within that screen, as the month
    // pane is.
    QPoint where = anchor->mapToGlobal(QPoint(0, anchor->height()));
    where.setX(where.x() + anchor->width() - width());
    const QRect screen = anchor->screen()->availableGeometry();
    if (where.x() + width() > screen.right()) where.setX(screen.right() - width());
    if (where.x() < screen.left()) where.setX(screen.left());
    move(where);
    show();
}

void ClockPane::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    m_tick->start();
}

void ClockPane::hideEvent(QHideEvent *event) {
    m_tick->stop();
    QWidget::hideEvent(event);
}

QRectF ClockPane::faceRect() const {
    // The space below the band, right of the banner, with the dial as large as
    // fits in it.
    const QRectF area(kBannerWidth + kMargin, kHeaderHeight + kMargin,
                      width() - kBannerWidth - kMargin * 2,
                      height() - kHeaderHeight - kMargin * 2);
    const qreal side = qMin(area.width(), area.height());
    return QRectF(area.center().x() - side / 2, area.center().y() - side / 2, side, side);
}

void ClockPane::paintEvent(QPaintEvent *) {
    const Theme &theme = currentTheme();
    QPainter painter(this);

    painter.fillRect(rect(), theme.windowBg);

    // The strip down the left, as every dialog and pane has: the caption colour
    // with the pane's name written up it.
    const QRect banner(0, 0, kBannerWidth, height());
    painter.fillRect(banner, theme.titleActive);

    QFont spine = font();
    if (spine.pointSizeF() > 0) spine.setPointSizeF(qMax(7.0, spine.pointSizeF() * 0.92));
    spine.setBold(true);
    spine.setCapitalization(QFont::AllUppercase);
    spine.setLetterSpacing(QFont::PercentageSpacing, 140);
    painter.save();
    painter.setFont(spine);
    painter.setPen(theme.titleActiveText);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.translate(banner.width() / 2.0, banner.height() / 2.0);
    painter.rotate(-90);
    painter.drawText(QRectF(-banner.height() / 2.0, -banner.width() / 2.0, banner.height(),
                            banner.width()),
                     Qt::AlignCenter, tr("Clock"));
    painter.restore();

    // The band holding the date, in the title bar colour.
    painter.fillRect(QRect(kBannerWidth, 0, width() - kBannerWidth, kHeaderHeight),
                     theme.titleActive);
    painter.setPen(blend(theme.windowBg, theme.windowText, 0.35));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    // The dial itself, which is round and so is drawn with antialiasing while
    // the chrome above is not.
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF face = faceRect();
    const qreal radius = face.width() / 2;
    const QColor ink = theme.windowText;
    painter.setPen(QPen(blend(theme.windowBg, ink, 0.35), 1));
    painter.setBrush(blend(theme.windowBg, ink, 0.06));
    painter.drawEllipse(face);

    painter.save();
    painter.translate(face.center());

    // Twelve marks, the quarters longer and heavier.
    for (int mark = 0; mark < 12; ++mark) {
        const bool quarter = mark % 3 == 0;
        painter.save();
        painter.rotate(mark * 30.0);
        painter.setPen(QPen(blend(theme.windowBg, ink, quarter ? 0.85 : 0.45),
                            quarter ? 3.0 : 1.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(0, -radius * (quarter ? kQuarterInner : kTickInner)),
                         QPointF(0, -radius * kTickOuter));
        painter.restore();
    }

    // The hands, at the time the strip is showing. The hour hand follows the
    // minutes and the minute hand the seconds, so they move as a clock's do
    // rather than jumping once an hour.
    const QTime now = QTime::currentTime();
    const qreal seconds = now.second() + now.msec() / 1000.0;
    const qreal minutes = now.minute() + seconds / 60.0;
    const qreal hours = (now.hour() % 12) + minutes / 60.0;

    drawHand(painter, hours * 30.0, kHourHand, radius, qMax(3.0, radius * 0.075), ink);
    drawHand(painter, minutes * 6.0, kMinuteHand, radius, qMax(2.0, radius * 0.05), ink);
    // The second hand in the caption colour: the one thing on the face that
    // moves while it is watched.
    drawHand(painter, seconds * 6.0, kSecondHand, radius, qMax(1.0, radius * 0.02),
             theme.titleActive);

    // The cap over the pinned ends of the hands.
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    const qreal cap = qMax(3.0, radius * 0.05);
    painter.drawEllipse(QPointF(0, 0), cap, cap);
    painter.setBrush(theme.titleActive);
    painter.drawEllipse(QPointF(0, 0), cap / 2.4, cap / 2.4);
    painter.restore();
}
