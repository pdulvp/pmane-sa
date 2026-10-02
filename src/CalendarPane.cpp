#include "CalendarPane.h"

#include "CalendarEvents.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QScreen>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Six rows of seven covers any month however it falls.
constexpr int kRows = 6;
constexpr int kColumns = 7;

// The strip down the left that the dialogs all wear, and the band across
// the top that stands in for a title bar.
constexpr int kBannerWidth = 36;
constexpr int kHeaderHeight = 32;

// The month itself: a little roomier than it needs to be, a calendar being
// something to glance at rather than to squint at.
constexpr int kContentWidth = 350;
constexpr int kPaneWidth = kBannerWidth + kContentWidth;
constexpr int kWeekdayHeight = 20;
constexpr int kCellHeight = 47;
constexpr int kMargin = 8;

// The bars drawn across the days, and how many fit before a cell is full.
constexpr int kBarHeight = 7;
constexpr int kBarGap = 2;
constexpr int kMaxLanes = 3;
constexpr int kDayNumberHeight = 14;

// `weight` of `over`, the rest of `under`.
QColor blend(const QColor &under, const QColor &over, qreal weight) {
    const qreal keep = 1.0 - weight;
    return QColor(qRound(under.red() * keep + over.red() * weight),
                  qRound(under.green() * keep + over.green() * weight),
                  qRound(under.blue() * keep + over.blue() * weight));
}

// The last day an event covers. An all-day event ends at midnight on the
// day after the last one it is on.
QDate lastDay(const CalendarEvent &event) {
    return event.allDay ? event.end.date().addDays(-1) : event.end.date();
}

} // namespace

CalendarPane::CalendarPane(QWidget *parent) : QWidget(parent, Qt::Popup) {
    m_events = new CalendarEvents(this);
    connect(m_events, &CalendarEvents::changed, this, qOverload<>(&QWidget::update));

    m_previous = new QToolButton(this);
    m_previous->setArrowType(Qt::LeftArrow);
    m_previous->setAutoRaise(true);
    m_previous->setFocusPolicy(Qt::NoFocus);
    m_previous->setToolTip(tr("Previous month"));

    m_next = new QToolButton(this);
    m_next->setArrowType(Qt::RightArrow);
    m_next->setAutoRaise(true);
    m_next->setFocusPolicy(Qt::NoFocus);
    m_next->setToolTip(tr("Next month"));

    m_heading = new QLabel(this);
    m_heading->setAlignment(Qt::AlignCenter);
    QFont heading = m_heading->font();
    heading.setBold(true);
    m_heading->setFont(heading);

    // The band behind them is the caption colour, so what is on it has to
    // be the caption's ink rather than the window's.
    const Theme &theme = currentTheme();
    QPalette onCaption = palette();
    onCaption.setColor(QPalette::WindowText, theme.titleActiveText);
    onCaption.setColor(QPalette::ButtonText, theme.titleActiveText);
    onCaption.setColor(QPalette::Text, theme.titleActiveText);
    m_heading->setPalette(onCaption);
    m_previous->setPalette(onCaption);
    m_next->setPalette(onCaption);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(0);
    header->addWidget(m_previous);
    header->addWidget(m_heading, 1);
    header->addWidget(m_next);

    // The header sits in the band at the very top, right of the banner; the
    // grid is painted below it rather than laid out.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kBannerWidth, 0, 0, kMargin);
    layout->setSpacing(0);
    header->setContentsMargins(0, 0, 0, 0);
    auto *headerHolder = new QWidget(this);
    headerHolder->setFixedHeight(kHeaderHeight);
    headerHolder->setLayout(header);
    layout->addWidget(headerHolder);
    layout->addStretch();

    connect(m_previous, &QToolButton::clicked, this,
            [this] { showMonth(m_month.addMonths(-1)); });
    connect(m_next, &QToolButton::clicked, this,
            [this] { showMonth(m_month.addMonths(1)); });

    setFixedWidth(kPaneWidth);
    setFixedHeight(kHeaderHeight + kWeekdayHeight + kRows * kCellHeight + kMargin);
    showMonth(QDate::currentDate());
}

void CalendarPane::showMonth(const QDate &anyDayInMonth) {
    m_month = QDate(anyDayInMonth.year(), anyDayInMonth.month(), 1);

    // Back to the start of the week the month begins in, so the grid starts
    // on the weekday this locale starts weeks on.
    const int firstWeekday = QLocale().firstDayOfWeek();
    int back = m_month.dayOfWeek() - firstWeekday;
    if (back < 0) back += 7;
    m_first = m_month.addDays(-back);

    // Everything the grid can show, which is a little more than the month.
    m_events->setRange(m_first, m_first.addDays(kRows * kColumns - 1));
    refreshHeading();
    update();
}

void CalendarPane::refreshHeading() {
    m_heading->setText(QLocale().toString(m_month, QStringLiteral("MMMM yyyy")));
}

void CalendarPane::popUpAt(QWidget *anchor) {
    showMonth(QDate::currentDate());

    // Under the thing that opened it, and kept on the screen it is on.
    QPoint where = anchor->mapToGlobal(QPoint(0, anchor->height()));
    where.setX(where.x() + anchor->width() - width());
    const QRect screen = anchor->screen()->availableGeometry();
    if (where.x() + width() > screen.right()) where.setX(screen.right() - width());
    if (where.x() < screen.left()) where.setX(screen.left());
    move(where);
    show();
}

QRect CalendarPane::gridRect() const {
    // Right of the banner, below the band and the weekday names.
    return QRect(kBannerWidth + kMargin, kHeaderHeight + kWeekdayHeight,
                 width() - kBannerWidth - kMargin * 2, kRows * kCellHeight);
}

QRect CalendarPane::cellRect(int row, int column) const {
    const QRect grid = gridRect();
    const int cellWidth = grid.width() / kColumns;
    return QRect(grid.left() + column * cellWidth, grid.top() + row * kCellHeight, cellWidth,
                 kCellHeight);
}

QDate CalendarPane::dateAt(int row, int column) const {
    return m_first.addDays(row * kColumns + column);
}

void CalendarPane::paintEvent(QPaintEvent *) {
    const Theme &theme = currentTheme();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // The month sits on the colour a group window's interior wears, so it
    // reads as something to look into rather than as another panel.
    painter.fillRect(rect(), theme.windowBg);

    // The strip down the left, as every dialog has: the caption colour with
    // what it is written up it.
    const QRect banner(0, 0, kBannerWidth, height());
    painter.fillRect(banner, theme.titleActive);

    QFont spine = font();
    if (spine.pointSizeF() > 0) {
        spine.setPointSizeF(qMax(7.0, spine.pointSizeF() * 0.92));
    }
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
                     Qt::AlignCenter, tr("Calendar"));
    painter.restore();

    // The band the month is named in, in the colour a title bar wears.
    painter.fillRect(QRect(kBannerWidth, 0, width() - kBannerWidth, kHeaderHeight),
                     theme.titleActive);

    painter.setPen(blend(theme.windowBg, theme.windowText, 0.35));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    // The days of the week, in this locale's order and its own short names.
    QFont small = font();
    small.setPointSizeF(qMax(7.0, font().pointSizeF() - 1.5));
    painter.setFont(small);
    painter.setPen(blend(theme.windowBg, theme.windowText, 0.55));
    for (int column = 0; column < kColumns; ++column) {
        const QRect cell = cellRect(0, column);
        const QRect header(cell.left(), kHeaderHeight, cell.width(), kWeekdayHeight);
        painter.drawText(header, Qt::AlignCenter,
                         QLocale().dayName(dateAt(0, column).dayOfWeek(), QLocale::ShortFormat));
    }

    const QDate today = QDate::currentDate();
    for (int row = 0; row < kRows; ++row) {
        // The bars for this week, each on a lane of its own where they
        // overlap, so one does not sit on top of another.
        const QDate weekStart = dateAt(row, 0);
        const QDate weekEnd = dateAt(row, kColumns - 1);

        QVector<CalendarEvent> inWeek;
        for (const CalendarEvent &event : m_events->events()) {
            if (lastDay(event) < weekStart || event.start.date() > weekEnd) continue;
            inWeek.append(event);
        }
        // Longest first, so a week-long bar takes the top lane and the short
        // ones arrange themselves underneath it.
        std::sort(inWeek.begin(), inWeek.end(),
                  [](const CalendarEvent &a, const CalendarEvent &b) {
                      const qint64 da = a.start.date().daysTo(lastDay(a));
                      const qint64 db = b.start.date().daysTo(lastDay(b));
                      if (da != db) return da > db;
                      return a.start < b.start;
                  });

        QVector<QDate> laneFreeFrom(kMaxLanes, weekStart);
        for (const CalendarEvent &event : inWeek) {
            const QDate from = std::max(event.start.date(), weekStart);
            const QDate to = std::min(lastDay(event), weekEnd);

            int lane = -1;
            for (int candidate = 0; candidate < kMaxLanes; ++candidate) {
                if (laneFreeFrom[candidate] > from) continue;
                lane = candidate;
                laneFreeFrom[candidate] = to.addDays(1);
                break;
            }
            if (lane < 0) continue; // more than fits; the day number still shows

            const QRect start = cellRect(row, int(weekStart.daysTo(from)));
            const QRect end = cellRect(row, int(weekStart.daysTo(to)));
            const int y = start.top() + kDayNumberHeight + lane * (kBarHeight + kBarGap);
            const QRect bar(start.left() + 2, y, end.right() - start.left() - 3, kBarHeight);

            painter.fillRect(bar, theme.titleActive);
            // The name on the bar where there is room for any of it, which
            // is what makes a long event worth drawing as a bar at all.
            if (bar.width() > 40) {
                painter.setPen(theme.titleActiveText);
                QFont tiny = small;
                tiny.setPointSizeF(qMax(6.0, small.pointSizeF() - 1));
                painter.setFont(tiny);
                painter.drawText(bar.adjusted(3, 0, -2, 0),
                                 Qt::AlignVCenter | Qt::AlignLeft,
                                 painter.fontMetrics().elidedText(
                                     event.summary, Qt::ElideRight, bar.width() - 5));
                painter.setFont(small);
            }
        }

        // The day numbers over the top, so a bar never hides one.
        for (int column = 0; column < kColumns; ++column) {
            const QDate date = dateAt(row, column);
            const QRect cell = cellRect(row, column);
            const QRect number(cell.left(), cell.top(), cell.width(), kDayNumberHeight);

            if (date == today) {
                painter.fillRect(number.adjusted(cell.width() / 2 - 11, 0,
                                                 -(cell.width() / 2 - 11), 0),
                                 theme.highlight);
                painter.setPen(theme.highlightText);
            } else if (date.month() != m_month.month()) {
                // The days either side of the month, faint: there to show
                // the shape of the weeks, not to be read.
                painter.setPen(blend(theme.windowBg, theme.windowText, 0.3));
            } else {
                painter.setPen(theme.windowText);
            }
            painter.drawText(number, Qt::AlignCenter, QString::number(date.day()));
        }
    }
}
