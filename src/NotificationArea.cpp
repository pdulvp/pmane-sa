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

// How long a notice stays. Long enough to read twice, since whoever is
// reading it has just had something fail and will want to read it again.
constexpr int kLifetimeMs = 9000;
// Fading in and out rather than appearing: the desktop eases its colours
// and its windows, and a notice that blinks into existence reads as a fault.
constexpr int kFadeMs = 180;

// Wide enough for a sentence to break well, narrow enough to stay a corner
// rather than a panel.
constexpr int kNoticeWidth = 340;
// What the notices keep clear of the edges of the desktop.
constexpr int kInset = 16;
constexpr int kGap = 8;

// At most this many at once: the rest of a burst would be a log up the side
// of the screen.
constexpr int kMaxNotices = 3;

// The stripe down a notice's left edge, which is the caption colour -- the
// same thing that marks a dialog as this application's.
constexpr int kStripe = 4;

} // namespace

NotificationArea::NotificationArea(QWidget *parent) : QWidget(parent) {
    // Nothing of its own is drawn, so the desktop shows through between the
    // notices. It is kept snug around them -- see reposition() -- so that
    // what it covers of the desktop is the notices themselves and little
    // else: a lid over the corner would swallow clicks meant for a group.
    setAttribute(Qt::WA_NoSystemBackground);

    m_stack = new QVBoxLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->setSpacing(kGap);
    m_stack->addStretch();

    if (parent) parent->installEventFilter(this);
    hide();
}

bool NotificationArea::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) reposition();
    return QWidget::eventFilter(watched, event);
}

void NotificationArea::reposition() {
    QWidget *host = parentWidget();
    if (!host) return;

    const int width = kNoticeWidth;

    // Added up here rather than asked of the layout: the detail line wraps,
    // so a notice's height depends on its width, and a layout asked for a
    // height it has already worked out once answers with the one it
    // remembers. The notices are few and the sum is exact.
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
    // Added before the stretch would push them up from the corner: they are
    // hung off the bottom instead, newest nearest it.
    m_stack->addWidget(notice);

    while (m_notices.size() > kMaxNotices) dismiss(m_notices.first());

    show();
    reposition();

    // In rather than on: see kFadeMs.
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
    // The detail line wraps, so how tall a notice is depends on how wide it
    // is. A plain widget does not say so on its own, and a layout that is
    // not told asks for the height of a single line.
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

    // Gone when it is read rather than when it times out, for whoever has
    // seen enough.
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
        // What the program itself said, which is the one thing a launch that
        // fails leaves behind and the one thing nobody could see before.
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

    // The dialogs' surface and the dialogs' caption stripe, so a notice
    // reads as part of the same desktop rather than as the system's own.
    const QColor face = theme.buttonFace;
    const QColor ink = theme.buttonText;
    QColor edge = darkMode() ? face.lighter(135) : face.darker(120);
    // The detail line in the same ink, lightened, the way a dialog's hint
    // line is: an aside rather than a second heading.
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

    // Out the way it came in. The widget is taken out of the layout first,
    // so the ones left move up while it is still fading rather than after.
    m_stack->removeWidget(notice);
    auto *fade = new QGraphicsOpacityEffect(notice);
    notice->setGraphicsEffect(fade);
    auto *out = new QPropertyAnimation(fade, "opacity", notice);
    out->setDuration(kFadeMs);
    out->setEndValue(0.0);
    connect(out, &QPropertyAnimation::finished, notice, &QObject::deleteLater);
    out->start(QAbstractAnimation::DeleteWhenStopped);

    if (m_notices.isEmpty()) {
        // Nothing left to show: the area goes rather than sitting there as an
        // invisible lid over the corner of the desktop.
        QTimer::singleShot(kFadeMs, this, [this] {
            if (m_notices.isEmpty()) hide();
        });
        return;
    }
    reposition();
}
