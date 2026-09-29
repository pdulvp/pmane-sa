#include "NoCloseBoxStyle.h"

#include <QStyleOptionTitleBar>

namespace {

// The buttons at the right-hand end of a caption, which shuffle along when
// the close box goes; the system menu box and the title are not among them.
bool isCaptionButton(QStyle::SubControl subControl) {
    switch (subControl) {
    case QStyle::SC_TitleBarMinButton:
    case QStyle::SC_TitleBarMaxButton:
    case QStyle::SC_TitleBarNormalButton:
    case QStyle::SC_TitleBarShadeButton:
    case QStyle::SC_TitleBarUnshadeButton:
    case QStyle::SC_TitleBarContextHelpButton:
        return true;
    default:
        return false;
    }
}

} // namespace

void NoCloseBoxStyle::drawComplexControl(ComplexControl control,
                                         const QStyleOptionComplex *option, QPainter *painter,
                                         const QWidget *widget) const {
    if (const auto *bar = qstyleoption_cast<const QStyleOptionTitleBar *>(option)) {
        if (control == CC_TitleBar) {
            QStyleOptionTitleBar copy = *bar;
            copy.subControls &= ~SC_TitleBarCloseButton;
            QProxyStyle::drawComplexControl(control, &copy, painter, widget);
            return;
        }
    }
    if (control == CC_MdiControls) {
        QStyleOptionComplex copy = *option;
        copy.subControls &= ~SC_MdiCloseButton;
        QProxyStyle::drawComplexControl(control, &copy, painter, widget);
        return;
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

QRect NoCloseBoxStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                                      SubControl subControl, const QWidget *widget) const {
    if (const auto *bar = qstyleoption_cast<const QStyleOptionTitleBar *>(option)) {
        if (control == CC_TitleBar) {
            if (subControl == SC_TitleBarCloseButton) return {};
            if (isCaptionButton(subControl)) {
                // Where the caption buttons sit is worked out from the window
                // flags rather than from the sub-controls asked for, so simply
                // not drawing the close box would leave its slot empty at the
                // right end. Clearing the flag that puts a close box there is
                // what closes the gap up; the system menu box and the title
                // keep the places they had, being measured from the left.
                QStyleOptionTitleBar copy = *bar;
                copy.titleBarFlags &= ~Qt::WindowSystemMenuHint;
                copy.subControls &= ~SC_TitleBarCloseButton;
                return QProxyStyle::subControlRect(control, &copy, subControl, widget);
            }
            return QProxyStyle::subControlRect(control, option, subControl, widget);
        }
    }
    if (control == CC_MdiControls) {
        if (subControl == SC_MdiCloseButton) return {};
        QStyleOptionComplex copy = *option;
        copy.subControls &= ~SC_MdiCloseButton;
        return QProxyStyle::subControlRect(control, &copy, subControl, widget);
    }
    return QProxyStyle::subControlRect(control, option, subControl, widget);
}

QStyle::SubControl NoCloseBoxStyle::hitTestComplexControl(ComplexControl control,
                                                          const QStyleOptionComplex *option,
                                                          const QPoint &pos,
                                                          const QWidget *widget) const {
    const SubControl hit = QProxyStyle::hitTestComplexControl(control, option, pos, widget);
    if (control == CC_TitleBar && hit == SC_TitleBarCloseButton) return SC_None;
    if (control == CC_MdiControls && hit == SC_MdiCloseButton) return SC_None;
    return hit;
}
