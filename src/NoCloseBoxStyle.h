#pragma once

#include <QProxyStyle>

// Takes the close box out of a window's caption, leaving the system menu
// box and the minimize/maximize buttons where they were.
//
// A QMdiSubWindow draws its caption buttons through the style rather than
// with widgets of its own, and it offers a close box whenever the window
// has a system menu at all -- there is no window flag for one without the
// other. Dropping the close sub-control on the way through the style is
// what removes it: the same call decides where the buttons sit and which
// one a click landed on, so the button stops being drawn, stops taking up
// room, and stops responding, all at once.
class NoCloseBoxStyle : public QProxyStyle {
    Q_OBJECT
public:
    using QProxyStyle::QProxyStyle;

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                         SubControl subControl, const QWidget *widget) const override;
    SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                     const QPoint &pos, const QWidget *widget) const override;
};
