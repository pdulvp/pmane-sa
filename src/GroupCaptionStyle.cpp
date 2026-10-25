#include "GroupCaptionStyle.h"

#include <QStyleOptionTitleBar>

namespace {

// The caption buttons removed from a group window.
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

// The caption as this desktop needs it. The style lays out from the window
// flags rather than from the sub-controls requested, so clearing the hints that
// reserve a slot both removes a button and closes the gap it would leave. The
// close box depends on the system menu hint, having no flag of its own. With
// the system menu box gone, the title starts at the left edge where that box
// used to be.
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
    // A button that is no longer drawn cannot be pressed: what is left in its
    // place is bare caption.
    if (control == CC_TitleBar && isRemoved(hit)) return SC_None;
    return hit;
}
