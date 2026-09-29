#include "GroupCaptionStyle.h"

#include <QStyleOptionTitleBar>

namespace {

// The buttons taken out of a group's caption.
bool isRemoved(QStyle::SubControl subControl) {
    switch (subControl) {
    case QStyle::SC_TitleBarSysMenu:
    case QStyle::SC_TitleBarCloseButton:
    case QStyle::SC_TitleBarMaxButton:
    case QStyle::SC_TitleBarNormalButton:
        return true;
    default:
        return false;
    }
}

// The caption as this desktop wants it. What the style lays out comes from
// the window flags rather than from the sub-controls it is asked for, so
// clearing the hints that reserve a slot is what both removes a button and
// closes up the gap it would have left -- the close box hangs off the system
// menu hint, there being no flag of its own for it. With the system menu box
// gone the title starts at the left edge, where the box used to be.
QStyleOptionTitleBar groupCaption(const QStyleOptionTitleBar &bar) {
    QStyleOptionTitleBar caption = bar;
    caption.titleBarFlags &= ~(Qt::WindowSystemMenuHint | Qt::WindowMaximizeButtonHint);
    caption.subControls &= ~(QStyle::SC_TitleBarSysMenu | QStyle::SC_TitleBarCloseButton |
                             QStyle::SC_TitleBarMaxButton | QStyle::SC_TitleBarNormalButton);
    return caption;
}

} // namespace

void GroupCaptionStyle::drawComplexControl(ComplexControl control,
                                           const QStyleOptionComplex *option, QPainter *painter,
                                           const QWidget *widget) const {
    if (const auto *bar = qstyleoption_cast<const QStyleOptionTitleBar *>(option)) {
        if (control == CC_TitleBar) {
            const QStyleOptionTitleBar caption = groupCaption(*bar);
            QProxyStyle::drawComplexControl(control, &caption, painter, widget);
            return;
        }
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

QRect GroupCaptionStyle::subControlRect(ComplexControl control,
                                        const QStyleOptionComplex *option, SubControl subControl,
                                        const QWidget *widget) const {
    if (const auto *bar = qstyleoption_cast<const QStyleOptionTitleBar *>(option)) {
        if (control == CC_TitleBar) {
            if (isRemoved(subControl)) return {};
            const QStyleOptionTitleBar caption = groupCaption(*bar);
            return QProxyStyle::subControlRect(control, &caption, subControl, widget);
        }
    }
    return QProxyStyle::subControlRect(control, option, subControl, widget);
}

QStyle::SubControl GroupCaptionStyle::hitTestComplexControl(ComplexControl control,
                                                            const QStyleOptionComplex *option,
                                                            const QPoint &pos,
                                                            const QWidget *widget) const {
    const SubControl hit = QProxyStyle::hitTestComplexControl(control, option, pos, widget);
    // A button that is no longer drawn is no longer there to be pressed --
    // the caption's bare strip is all that is left where it was.
    if (control == CC_TitleBar && isRemoved(hit)) return SC_None;
    return hit;
}
