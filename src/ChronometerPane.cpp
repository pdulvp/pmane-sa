#include "ChronometerPane.h"

#include "Theme.h"

#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {

// The same layout as the calculator and the month pane: a strip down the left
// with the pane's name written up it, a band across the top, and the working
// area in the colour of a group window's interior.
constexpr int kBannerWidth = 36;
constexpr int kHeaderHeight = 32;
constexpr int kContentWidth = 290;
constexpr int kPaneWidth = kBannerWidth + kContentWidth;
// The readout, the step row, the buttons and the laps list.
constexpr int kPaneHeight = 392;
constexpr int kMargin = 8;

// The countdown steps: 30 s each, a minute being two of them, and the ceiling
// a target can be stepped up to.
constexpr qint64 kStep = 30000;
constexpr qint64 kMaxTarget = 24 * 3600 * 1000;

// The readout is refreshed often enough that the centiseconds move visibly
// without the label repainting more than a screen can use.
constexpr int kRefreshInterval = 40;

// How many laps are kept: enough to look back over a session, not so many that
// the list needs scrolling.
constexpr int kRemembered = 12;

QColor blend(const QColor &under, const QColor &over, qreal weight) {
    const qreal keep = 1.0 - weight;
    return QColor(qRound(under.red() * keep + over.red() * weight),
                  qRound(under.green() * keep + over.green() * weight),
                  qRound(under.blue() * keep + over.blue() * weight));
}

} // namespace

QString formatElapsed(qint64 ms) {
    if (ms < 0) ms = 0;
    const qint64 centiseconds = ms / 10;
    const qint64 hours = centiseconds / 360000;
    const qint64 minutes = (centiseconds / 6000) % 60;
    const qint64 seconds = (centiseconds / 100) % 60;
    const qint64 hundredths = centiseconds % 100;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(hundredths, 2, 10, QLatin1Char('0'));
}

ChronometerPane::ChronometerPane(QWidget *parent) : QWidget(parent, Qt::Popup) {
    m_time = new QLabel(this);
    m_time->setAlignment(Qt::AlignCenter);
    QFont readout = m_time->font();
    readout.setBold(true);
    readout.setPointSizeF(qMax(20.0, readout.pointSizeF() * 2.2));
    readout.setFamily(QStringLiteral("monospace"));
    m_time->setFont(readout);
    m_time->setFixedHeight(56);

    m_laps = new QListWidget(this);
    m_laps->setFrameShape(QFrame::NoFrame);
    m_laps->setSelectionMode(QAbstractItemView::NoSelection);
    m_laps->setFocusPolicy(Qt::NoFocus);

    m_startStop = new QPushButton(tr("Start"), this);
    m_lap = new QPushButton(tr("Lap"), this);
    m_reset = new QPushButton(tr("Reset"), this);
    for (QPushButton *button : {m_startStop, m_lap, m_reset}) {
        button->setFixedHeight(28);
        button->setFocusPolicy(Qt::NoFocus);
        button->setAutoDefault(false);
    }
    m_startStop->setToolTip(tr("Start or stop the watch (Space)"));
    m_lap->setToolTip(tr("Record the time since the last lap (L)"));
    m_reset->setToolTip(tr("Back to zero, keeping the target (R)"));

    // The countdown steps: −1:00, −0:30, +0:30, +1:00. Setting a target turns
    // the pane into a reverse chronometer; stepping down to zero turns it
    // back into a plain one.
    auto *stepsRow = new QHBoxLayout;
    stepsRow->setSpacing(4);
    const QPair<QString, qint64> stepDefs[4] = {
        {QStringLiteral("\u22121:00"), -2 * kStep},
        {QStringLiteral("\u22120:30"), -kStep},
        {QStringLiteral("+0:30"), kStep},
        {QStringLiteral("+1:00"), 2 * kStep},
    };
    for (const auto &def : stepDefs) {
        auto *button = new QPushButton(def.first, this);
        button->setFixedHeight(24);
        button->setFocusPolicy(Qt::NoFocus);
        button->setAutoDefault(false);
        button->setToolTip(def.second < 0
                               ? tr("Less countdown time, to zero for a plain chronometer")
                               : tr("More countdown time"));
        m_steps.append(button);
        stepsRow->addWidget(button);
        // The step is carried on the button, so refresh() can tell whether a
        // minus step can still be taken whole.
        button->setProperty("stepDelta", def.second);
        const qint64 delta = def.second;
        connect(button, &QPushButton::clicked, this, [this, delta] { adjustTarget(delta); });
    }

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(kMargin);
    buttons->addWidget(m_startStop, 1);
    buttons->addWidget(m_lap);
    buttons->addWidget(m_reset);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kBannerWidth + kMargin, kHeaderHeight + kMargin / 2, kMargin,
                               kMargin);
    layout->setSpacing(kMargin);
    layout->addWidget(m_time);
    layout->addLayout(stepsRow);
    layout->addLayout(buttons);
    layout->addWidget(m_laps, 1);

    connect(m_startStop, &QPushButton::clicked, this, &ChronometerPane::toggleStart);
    connect(m_lap, &QPushButton::clicked, this, &ChronometerPane::takeLap);
    connect(m_reset, &QPushButton::clicked, this, &ChronometerPane::reset);

    m_watch = new QElapsedTimer;
    m_display = new QTimer(this);
    m_display->setInterval(kRefreshInterval);
    connect(m_display, &QTimer::timeout, this, [this] { refresh(); });

    setFixedSize(kPaneWidth, kPaneHeight);
    wearTheScheme();
    refresh();
}

qint64 ChronometerPane::elapsedMs() const {
    return m_settled + (m_running ? m_watch->elapsed() : 0);
}

qint64 ChronometerPane::shownMs() const {
    if (m_target > 0) return qMax<qint64>(0, m_target - elapsedMs());
    return elapsedMs();
}

void ChronometerPane::toggleStart() {
    if (m_running) {
        m_settled += m_watch->elapsed();
        m_running = false;
    } else {
        m_watch->start();
        m_running = true;
    }
    refresh();
}

void ChronometerPane::takeLap() {
    if (!m_running) return;
    const qint64 total = elapsedMs();
    const qint64 lap = total - m_lastLap;
    m_lastLap = total;
    ++m_lapCount;

    auto *row = new QListWidgetItem(
        tr("Lap %1   +%2   %3").arg(m_lapCount).arg(formatElapsed(lap), formatElapsed(total)),
        m_laps);
    m_laps->scrollToItem(row);
    while (m_laps->count() > kRemembered) delete m_laps->takeItem(0);
    refresh();
}

void ChronometerPane::reset() {
    // The target stays: reset re-arms a countdown rather than switching back
    // to counting up, which stepping down to zero does.
    m_settled = 0;
    m_running = false;
    m_watch->invalidate();
    m_lastLap = 0;
    m_lapCount = 0;
    m_laps->clear();
    refresh();
}

void ChronometerPane::adjustTarget(qint64 deltaMs) {
    if (m_running) return;
    m_target = qBound<qint64>(0, m_target + deltaMs, kMaxTarget);
    // A new target is a new run: the watch re-arms, and the laps go with the
    // run they belonged to.
    m_settled = 0;
    m_watch->invalidate();
    m_lastLap = 0;
    m_lapCount = 0;
    m_laps->clear();
    refresh();
}

void ChronometerPane::refresh() {
    // A countdown that has run out stops on its own, at zero.
    if (m_running && m_target > 0 && elapsedMs() >= m_target) {
        m_settled = m_target;
        m_running = false;
    }
    m_time->setText(formatElapsed(shownMs()));
    m_startStop->setText(m_running ? tr("Stop") : tr("Start"));
    // A lap of a stopped watch is no lap, and a reset of a fresh one is
    // nothing to do.
    m_lap->setEnabled(m_running);
    m_reset->setEnabled(!m_running && (elapsedMs() > 0 || m_target > 0));
    // The steps set the target, so they wait for the watch to be stopped; and
    // a minus step is offered only while the target still holds it whole, so
    // at zero — or at 0:30 for −1:00 — there is nothing for it to subtract.
    for (QPushButton *step : m_steps) {
        const qint64 delta = step->property("stepDelta").toLongLong();
        step->setEnabled(!m_running && (delta > 0 || m_target >= -delta));
    }
    updateReadoutColour();
}

void ChronometerPane::wearTheScheme() {
    const Theme &theme = currentTheme();
    const QColor edge = blend(theme.windowBg, theme.windowText, 0.30);

    m_laps->setStyleSheet(QStringLiteral("QListWidget { background: %1; color: %2; "
                                         "border: 1px solid %3; border-radius: 3px; }"
                                         "QListWidget::item { padding: 1px 2px; }")
                              .arg(theme.windowBg.name(), theme.windowText.name(),
                                   edge.name()));

    // The buttons use the scheme's button colours, as a dialog's do.
    const QString buttonLook =
        QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                       "border-radius: 3px; }"
                       "QPushButton:hover { border: 1px solid %4; }"
                       "QPushButton:pressed { background: %5; }"
                       "QPushButton:disabled { color: %6; }")
            .arg(theme.buttonFace.name(), theme.buttonText.name(), edge.name(),
                 theme.titleActive.name(), blend(theme.buttonFace, theme.buttonText, 0.12).name(),
                 blend(theme.buttonText, theme.windowBg, 0.45).name());
    for (QPushButton *button : {m_startStop, m_lap, m_reset}) button->setStyleSheet(buttonLook);
    for (QPushButton *step : m_steps) step->setStyleSheet(buttonLook);

    updateReadoutColour();
}

void ChronometerPane::updateReadoutColour() {
    const Theme &theme = currentTheme();
    // The readout takes the caption colour while the watch runs or a target is
    // set, so a running chronometer and a countdown read differently from a
    // stopped, target-less one.
    QPalette readout = m_time->palette();
    readout.setColor(QPalette::WindowText,
                     (m_running || m_target > 0) ? theme.titleActive : theme.windowText);
    m_time->setPalette(readout);
}

void ChronometerPane::showEvent(QShowEvent *) {
    wearTheScheme();
    refresh();
    m_display->start();
    setFocus();
}

void ChronometerPane::hideEvent(QHideEvent *) {
    m_display->stop();
}

void ChronometerPane::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space) {
        toggleStart();
        event->accept();
    } else if (event->key() == Qt::Key_L) {
        takeLap();
        event->accept();
    } else if (event->key() == Qt::Key_R) {
        reset();
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void ChronometerPane::popUpAt(QWidget *anchor) {
    // The scheme may have changed since it was last shown.
    wearTheScheme();
    refresh();

    QPoint where = anchor->mapToGlobal(QPoint(0, anchor->height()));
    where.setX(where.x() + anchor->width() - width());
    const QRect screen = anchor->screen()->availableGeometry();
    if (where.x() + width() > screen.right()) where.setX(screen.right() - width());
    if (where.x() < screen.left()) where.setX(screen.left());
    move(where);
    show();
}

void ChronometerPane::paintEvent(QPaintEvent *) {
    const Theme &theme = currentTheme();
    QPainter painter(this);

    painter.fillRect(rect(), theme.windowBg);

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
                     Qt::AlignCenter, tr("Chronometer"));
    painter.restore();

    const QRect header(kBannerWidth, 0, width() - kBannerWidth, kHeaderHeight);
    painter.fillRect(header, theme.titleActive);
    QFont heading = font();
    heading.setBold(true);
    painter.setFont(heading);
    painter.setPen(theme.titleActiveText);
    painter.drawText(header, Qt::AlignCenter, tr("Chronometer"));

    painter.setPen(blend(theme.windowBg, theme.windowText, 0.35));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}
