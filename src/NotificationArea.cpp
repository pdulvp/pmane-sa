#include "NotificationArea.h"

#include "Theme.h"

#include <QDesktopServices>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// How long a notice stays on screen: long enough to be read twice.
constexpr int kLifetimeMs = 9000;
// Notices fade in and out rather than appearing abruptly, matching the rest of
// the desktop's animations.
constexpr int kFadeMs = 180;

// Wide enough for a sentence to wrap sensibly, narrow enough to stay a corner
// rather than a panel.
constexpr int kNoticeWidth = 340;
// The gap kept between the notices and the edges of the desktop.
constexpr int kInset = 16;
constexpr int kGap = 8;

// At most this many at once; more would turn the corner into a log.
constexpr int kMaxNotices = 3;

// The stripe down a notice's left edge, in the caption colour, which is what
// marks a dialog as this application's.
constexpr int kStripe = 4;

} // namespace

NotificationArea::NotificationArea(QWidget *parent) : QWidget(parent) {
    // Nothing of its own is drawn, so the desktop shows through between the
    // notices. It is kept tight around them, see reposition(), so it covers
    // little more than the notices themselves: a larger area would swallow
    // clicks meant for a group.
    setAttribute(Qt::WA_NoSystemBackground);

    m_stack = new QVBoxLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->setSpacing(kGap);
    m_stack->addStretch();

    if (parent) parent->installEventFilter(this);
    hide();
}

bool NotificationArea::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) updatePosition();
    return QWidget::eventFilter(watched, event);
}

// Moves it back into the corner, no taller than the available space.
void NotificationArea::updatePosition() {
    QWidget *host = parentWidget();
    if (!host) return;

    const int width = kNoticeWidth;

    // Summed here rather than taken from the layout: the detail line wraps, so a
    // notice's height depends on its width, and the layout returns a cached
    // height instead of recomputing it. The notices are few, so the sum is cheap
    // and exact.
    int wanted = 0;
    for (QWidget *notice : m_notices) {
        wanted += notice->hasHeightForWidth() ? notice->heightForWidth(width)
                                              : notice->sizeHint().height();
    }
    wanted += kGap * qMax(0, m_notices.size() - 1);

    const int height = qBound(0, wanted, host->height() - 2 * kInset);
    resize(width, height);
    move(host->width() - width - kInset, host->height() - height - kInset);
    raise();
}

void NotificationArea::post(const QString &title, const QString &detail,
                            const QString &logPath) {
    QWidget *notice = buildNotice(title, detail, logPath);
    m_notices.append(notice);
    // Inserted before the stretch, so the notices hang from the bottom with the
    // newest nearest the corner.
    m_stack->addWidget(notice);

    while (m_notices.size() > kMaxNotices) dismiss(m_notices.first());

    show();
    updatePosition();

    // Faded in rather than shown outright: see kFadeMs.
    auto *fade = new QGraphicsOpacityEffect(notice);
    fade->setOpacity(0);
    notice->setGraphicsEffect(fade);
    auto *in = new QPropertyAnimation(fade, "opacity", notice);
    in->setDuration(kFadeMs);
    in->setEndValue(1.0);
    in->start(QAbstractAnimation::DeleteWhenStopped);

    QTimer::singleShot(kLifetimeMs, notice, [this, notice] { dismiss(notice); });
}

QWidget *NotificationArea::buildNotice(const QString &title, const QString &detail,
                                       const QString &logPath) {
    auto *notice = new QWidget(this);
    notice->setObjectName(QStringLiteral("notice"));
    notice->setFixedWidth(kNoticeWidth);
    // The detail line wraps, so a notice's height depends on its width. A plain
    // widget does not report that, and without it the layout asks for the
    // height of a single line.
    QSizePolicy policy = notice->sizePolicy();
    policy.setHeightForWidth(true);
    policy.setVerticalPolicy(QSizePolicy::Minimum);
    notice->setSizePolicy(policy);

    auto *rows = new QVBoxLayout(notice);
    rows->setContentsMargins(kStripe + 12, 10, 10, 12);
    rows->setSpacing(4);

    auto *heading = new QHBoxLayout;
    heading->setSpacing(8);
    auto *titleLabel = new QLabel(title, notice);
    titleLabel->setObjectName(QStringLiteral("noticeTitle"));
    titleLabel->setWordWrap(true);
    heading->addWidget(titleLabel, 1);

    // Clicking dismisses a notice before its timeout.
    auto *close = new QPushButton(QStringLiteral("×"), notice);
    close->setObjectName(QStringLiteral("noticeClose"));
    close->setFlat(true);
    close->setCursor(Qt::ArrowCursor);
    close->setFixedSize(20, 20);
    close->setToolTip(tr("Dismiss"));
    heading->addWidget(close, 0, Qt::AlignTop);
    rows->addLayout(heading);

    if (!detail.isEmpty()) {
        auto *detailLabel = new QLabel(detail, notice);
        detailLabel->setObjectName(QStringLiteral("noticeDetail"));
        detailLabel->setWordWrap(true);
        detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rows->addWidget(detailLabel);
    }

    if (!logPath.isEmpty()) {
        // The program's own output, which is all a failed launch leaves behind.
        auto *details = new QPushButton(tr("What it printed"), notice);
        details->setObjectName(QStringLiteral("noticeAction"));
        details->setFlat(true);
        details->setCursor(Qt::PointingHandCursor);
        connect(details, &QPushButton::clicked, notice, [logPath] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));
        });
        auto *actions = new QHBoxLayout;
        actions->setContentsMargins(0, 2, 0, 0);
        actions->addWidget(details);
        actions->addStretch();
        rows->addLayout(actions);
    }

    connect(close, &QPushButton::clicked, notice, [this, notice] { dismiss(notice); });

    dress(notice);
    return notice;
}

void NotificationArea::dress(QWidget *notice) {
    const Theme &theme = currentTheme();

    // The dialogs' surface colour and caption stripe, so a notice reads as part
    // of this application rather than a system notification.
    const QColor face = theme.buttonFace;
    const QColor ink = theme.buttonText;
    QColor edge = darkMode() ? face.lighter(135) : face.darker(120);
    // The detail line in the same colour, lightened, like a dialog's hint line:
    // an aside rather than a second heading.
    const QString aside = QStringLiteral("rgba(%1, %2, %3, 0.67)")
                              .arg(ink.red())
                              .arg(ink.green())
                              .arg(ink.blue());

    notice->setStyleSheet(
        QStringLiteral("#notice { background: %1; border: 1px solid %2;"
                       " border-left: %3px solid %4; border-radius: 6px; }"
                       "#noticeTitle { color: %5; font-weight: bold; }"
                       "#noticeDetail { color: %6; }"
                       "#noticeAction { color: %4; border: none; padding: 0;"
                       " text-align: left; text-decoration: underline; }"
                       "#noticeClose { color: %6; border: none; font-size: 14px; }"
                       "#noticeClose:hover { color: %5; }")
            .arg(face.name(), edge.name())
            .arg(kStripe)
            .arg(theme.titleActive.name(), ink.name(), aside));
}

void NotificationArea::applyTheme() {
    for (QWidget *notice : m_notices) dress(notice);
}

void NotificationArea::dismiss(QWidget *notice) {
    if (!m_notices.removeOne(notice)) return;

    // Faded out as it was faded in. The widget leaves the layout first, so the
    // remaining notices move up while it is still fading.
    m_stack->removeWidget(notice);
    auto *fade = new QGraphicsOpacityEffect(notice);
    notice->setGraphicsEffect(fade);
    auto *out = new QPropertyAnimation(fade, "opacity", notice);
    out->setDuration(kFadeMs);
    out->setEndValue(0.0);
    connect(out, &QPropertyAnimation::finished, notice, &QObject::deleteLater);
    out->start(QAbstractAnimation::DeleteWhenStopped);

    if (m_notices.isEmpty()) {
        // Nothing left to show, so the area is hidden rather than left as an
        // invisible overlay on the corner of the desktop.
        QTimer::singleShot(kFadeMs, this, [this] {
            if (m_notices.isEmpty()) hide();
        });
        return;
    }
    updatePosition();
}
