#include "Theme.h"

#include <QHash>
#include <cmath>
#include <QPainter>
#include <QPixmap>

namespace {

Theme make(const QString &name, const char *desktop, const char *titleActive,
           const char *titleActiveText, const char *titleInactive, const char *titleInactiveText,
           const char *windowBg, const char *windowText, const char *buttonFace,
           const char *buttonText, const char *highlight, const char *highlightText) {
    Theme t;
    t.name = name;
    t.desktop = QColor(desktop);
    t.titleActive = QColor(titleActive);
    t.titleActiveText = QColor(titleActiveText);
    t.titleInactive = QColor(titleInactive);
    t.titleInactiveText = QColor(titleInactiveText);
    t.windowBg = QColor(windowBg);
    t.windowText = QColor(windowText);
    t.buttonFace = QColor(buttonFace);
    t.buttonText = QColor(buttonText);
    t.highlight = QColor(highlight);
    t.highlightText = QColor(highlightText);
    return t;
}

} // namespace

QColor readableTextOn(const QColor &background) {
    return (background.isValid() && background.lightnessF() > 0.55) ? Qt::black : Qt::white;
}

QIcon tintedIcon(const QStringList &names, const QColor &ink) {
    QIcon source;
    for (const QString &name : names) {
        source = QIcon::fromTheme(name);
        if (!source.isNull()) break;
    }
    if (source.isNull()) return {};

    // Every size the icon provides is repainted: the shape is kept and the
    // colour replaced, which is what tinting a monochrome icon amounts to.
    QIcon tinted;
    for (int size : {16, 22, 24, 32, 48}) {
        QPixmap artwork = source.pixmap(size, size);
        if (artwork.isNull()) continue;

        QPixmap coloured(artwork.size());
        coloured.setDevicePixelRatio(artwork.devicePixelRatio());
        coloured.fill(Qt::transparent);

        QPainter painter(&coloured);
        painter.drawPixmap(0, 0, artwork);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(coloured.rect(), ink);
        painter.end();

        tinted.addPixmap(coloured);
    }
    return tinted;
}

const QVector<Theme> &builtinThemes() {
    static const QVector<Theme> themes = {
        //                desktop    titleAct   titleActTx titleInact titleInactTx windowBg   windowTx   btnFace    btnText    highlight  highlightTx
        // Steel blue over pale grey-blue. The default, and the most neutral of
        // the coloured schemes.
        make("Harbour",   "#3f6076", "#1d3a52", "#ffffff", "#7d94a6", "#38434b", "#fbfdfe", "#14212b", "#ccd8e0", "#14212b", "#1d3a52", "#ffffff"),
        // Paper and sepia ink: warm but unsaturated, for a desktop with no
        // strong colour in it.
        make("Vellum",    "#c9b596", "#6b4f2f", "#fdf6e8", "#b4a287", "#584f42", "#fffdf7", "#2e2416", "#e8dcc4", "#2e2416", "#6b4f2f", "#fdf6e8"),
        // Leaf green with a plum caption.
        make("Orchard",   "#8fae7d", "#5c3a5e", "#ffffff", "#a9b9a0", "#586053", "#fbfff8", "#1f2a1a", "#d8e4cf", "#1f2a1a", "#5c3a5e", "#ffffff"),
        // Saturated blue, the strongest single-hue scheme in the list.
        make("Cobalt",    "#33539e", "#15255c", "#ffffff", "#7f8fb8", "#394053", "#fafbff", "#111a33", "#c8d2ea", "#111a33", "#33539e", "#ffffff"),
        // A pink desktop under a green caption.
        make("Rhubarb",   "#c76b7e", "#3f6b43", "#ffffff", "#d3a3ad", "#6c5358", "#fffafb", "#2b1a1e", "#f1d7dd", "#2b1a1e", "#3f6b43", "#ffffff"),
        // Grey with a yellow accent: the caption text and the highlight are
        // both yellow.
        make("Tarmac",    "#4a4e52", "#26292c", "#f2c230", "#7a7f84", "#303233", "#f7f8f8", "#1a1c1e", "#cfd3d6", "#1a1c1e", "#f2c230", "#1a1c1e"),
        // Burnt orange and dark brown: the warm scheme with saturation, where
        // Vellum is the warm scheme without.
        make("Ochre",     "#c08a4a", "#6d3f16", "#fff4e4", "#cbab85", "#685744", "#fffcf6", "#32200d", "#ecd9bd", "#32200d", "#6d3f16", "#fff4e4"),
        // Lavender: clearly coloured but not bright.
        make("Lupin",     "#8e86b8", "#45397a", "#ffffff", "#a9a3c6", "#545263", "#fdfcff", "#211c35", "#dcd8ee", "#211c35", "#45397a", "#ffffff"),
        // Deep red with gold lettering. The only scheme whose caption text is
        // neither black nor white.
        make("Lacquer",   "#8c2f2a", "#4a1310", "#f0c75e", "#b0736f", "#482f2e", "#fffaf9", "#2b1110", "#e6cfcb", "#2b1110", "#8c2f2a", "#f0c75e"),
        // Pale ice blue: the lightest scheme, and the one that works best in a
        // bright room.
        make("Glacier",   "#a8c6d4", "#3e6b80", "#ffffff", "#bfd3dc", "#677277", "#fdffff", "#16282f", "#e0edf2", "#16282f", "#3e6b80", "#ffffff"),
        // Neutral grey throughout, so it never clashes with an icon.
        make("Graphite",  "#6e7175", "#2f3134", "#ffffff", "#9b9ea1", "#4a4c4d", "#fcfcfc", "#1b1c1e", "#d4d5d7", "#1b1c1e", "#2f3134", "#ffffff"),
        // Deliberately garish: orange, magenta, yellow and mint together.
        make("Sherbet",   "#ff7a4d", "#d6166b", "#fff6cf", "#ffb08f", "#7a2a10", "#fffdf2", "#3a1020", "#ffe08a", "#3a1020", "#2bbf9e", "#0b2b24"),
    };
    return themes;
}

const Theme &themeByName(const QString &name) {
    const QVector<Theme> &themes = builtinThemes();
    for (const Theme &t : themes) {
        if (t.name == name) return t;
    }

    // A desktop saved when the schemes were the Windows 3.1 Control Panel's
    // names a scheme that is no longer in the list. Each old name maps to the
    // closest scheme above, so such a desktop comes back in roughly the colours
    // it was left in rather than all of them falling back to the first scheme.
    static const QHash<QString, QString> replaced = {
        {QStringLiteral("Windows Default"), QStringLiteral("Harbour")},
        {QStringLiteral("Wheat"), QStringLiteral("Vellum")},
        {QStringLiteral("Mint"), QStringLiteral("Orchard")},
        {QStringLiteral("Emerald City"), QStringLiteral("Orchard")},
        {QStringLiteral("The Blues"), QStringLiteral("Cobalt")},
        {QStringLiteral("Rose"), QStringLiteral("Rhubarb")},
        {QStringLiteral("Arizona"), QStringLiteral("Ochre")},
        {QStringLiteral("Pastel"), QStringLiteral("Lupin")},
        {QStringLiteral("Sky"), QStringLiteral("Glacier")},
        {QStringLiteral("Ocean"), QStringLiteral("Glacier")},
        {QStringLiteral("Monochrome"), QStringLiteral("Graphite")},
        {QStringLiteral("Hotdog Stand"), QStringLiteral("Sherbet")},
    };
    const QString now = replaced.value(name);
    if (!now.isEmpty()) {
        for (const Theme &t : themes) {
            if (t.name == now) return t;
        }
    }
    return themes.first();
}

namespace {

// A surface in the scheme's own colour at a given brightness: the hue comes
// from the scheme and the saturation is set directly rather than scaled.
// Scaling it turned the dark variants into a row of near-blacks, a soft caption
// having little saturation to begin with.
QColor tintedSurface(const QColor &hueFrom, qreal value, qreal saturation) {
    const QColor hsv = hueFrom.toHsv();
    if (hsv.hue() < 0) {
        // A scheme with no colour stays colourless: Graphite's dark variant
        // should be grey, not faintly red.
        return QColor::fromHsvF(0.0, 0.0, qBound(0.0, value, 1.0));
    }
    return QColor::fromHsvF(hsv.hue() / 360.0, qBound(0.0, saturation, 1.0),
                            qBound(0.0, value, 1.0));
}

// How distinct two surfaces look, which is not the difference in their
// lightness: perceived brightness weighs green well above red and red above
// blue, so a dark pink and a dark green of identical lightness are
// indistinguishable. The ratios match the accessibility guidelines: 1.0 is the
// same colour twice, and a visible edge needs something above 1.2.
qreal relativeLuminance(const QColor &colour) {
    const auto channel = [](qreal c) {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(colour.redF()) + 0.7152 * channel(colour.greenF()) +
           0.0722 * channel(colour.blueF());
}

qreal contrast(const QColor &a, const QColor &b) {
    const qreal la = relativeLuminance(a);
    const qreal lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor atValue(const QColor &colour, qreal value, qreal saturation) {
    const QColor hsv = colour.toHsv();
    const int hue = hsv.hue() < 0 ? 0 : hsv.hue();
    // Divided by 360, not 359: the hue wheel wraps, and dividing by one less
    // shifts every hue slightly, which showed up as Rhubarb coming back one
    // degree off itself.
    return QColor::fromHsvF(hue / 360.0, qBound(0.0, hsv.saturationF() * saturation, 1.0),
                            qBound(0.0, value, 1.0));
}

// The scheme last applied, the first built-in one until then, and whether its
// dark variant is in use.
QString g_currentThemeName;
bool g_dark = false;
Theme g_worn;
bool g_wornBuilt = false;

void rebuildWorn() {
    const Theme &base = themeByName(g_currentThemeName);
    g_worn = g_dark ? darkVariant(base) : base;
    g_wornBuilt = true;
}

} // namespace

namespace {
// How far apart the dark desktop and the groups on it have to be for a group's
// edge to be clearly visible.
constexpr qreal kDarkSurfaceGap = 1.25;
} // namespace

Theme darkVariant(const Theme &light) {
    Theme dark = light;

    // A scheme is recognised by its caption colour, so the dark surfaces are
    // tinted with that, unless the caption is colourless, in which case the
    // desktop colour identifies the scheme. Hotdog Stand's caption is black and
    // its desktop red, and the red is the part worth keeping.
    QColor character = light.titleActive;
    if (character.toHsv().saturationF() < 0.15) character = light.desktop;

    // The desktop is the lighter surface and the groups sit darker on it, so a
    // group reads as a window cut into the desktop rather than a panel laid
    // over it. Set below, once the group colour is known.
    dark.desktop = atValue(light.desktop, 0.26, 1.0);

    // Background and text swap roles, each keeping a trace of the scheme, which
    // is easier on the eye than flat grey on flat black.
    dark.buttonFace = tintedSurface(character, 0.25, 0.30);
    dark.buttonText = tintedSurface(character, 0.92, 0.10);
    dark.windowBg = tintedSurface(character, 0.20, 0.34);
    dark.windowText = tintedSurface(character, 0.95, 0.08);

    // The desktop is then lightened until it is clearly lighter than the groups
    // on it. A strong colour looks much darker than its lightness suggests, a
    // saturated teal or red at the same lightness as a grey appearing darker
    // than it, so the gap is measured perceptually rather than assumed:
    // lightness alone leaves a scheme whose caption and desktop are far apart
    // in hue, such as a green caption over a pink desktop, with its groups
    // invisible against the desktop.
    for (qreal value = 0.26;
         value < 0.80 && (dark.desktop.lightnessF() < dark.windowBg.lightnessF() + 0.05 ||
                          contrast(dark.desktop, dark.windowBg) < kDarkSurfaceGap);
         value += 0.03) {
        dark.desktop = atValue(light.desktop, value, 1.0);
    }

    // A caption that is already dark cannot be darkened further and still be
    // seen against the window behind it; a bright one is left alone.
    const qreal captionLightness = light.titleActive.lightnessF();
    dark.titleActive = captionLightness < 0.30
                           ? atValue(light.titleActive, 0.45, 1.0)
                           : light.titleActive;
    dark.titleActiveText = readableTextOn(dark.titleActive);
    dark.titleInactive = atValue(light.titleInactive, 0.28, 0.7);
    dark.titleInactiveText = readableTextOn(dark.titleInactive);

    // The highlight keeps its colour: it is meant to stand apart from the rest
    // of the scheme.
    dark.highlight = light.highlight;
    dark.highlightText = readableTextOn(light.highlight);
    return dark;
}

Theme blendThemes(const Theme &from, const Theme &to, qreal t) {
    const qreal mix = qBound(0.0, t, 1.0);
    const auto between = [mix](const QColor &a, const QColor &b) {
        return QColor(qRound(a.red() + (b.red() - a.red()) * mix),
                      qRound(a.green() + (b.green() - a.green()) * mix),
                      qRound(a.blue() + (b.blue() - a.blue()) * mix));
    };

    Theme blended = to; // the name and any non-colour field comes from `to`
    blended.desktop = between(from.desktop, to.desktop);
    blended.titleActive = between(from.titleActive, to.titleActive);
    blended.titleActiveText = between(from.titleActiveText, to.titleActiveText);
    blended.titleInactive = between(from.titleInactive, to.titleInactive);
    blended.titleInactiveText = between(from.titleInactiveText, to.titleInactiveText);
    blended.windowBg = between(from.windowBg, to.windowBg);
    blended.windowText = between(from.windowText, to.windowText);
    blended.buttonFace = between(from.buttonFace, to.buttonFace);
    blended.buttonText = between(from.buttonText, to.buttonText);
    blended.highlight = between(from.highlight, to.highlight);
    blended.highlightText = between(from.highlightText, to.highlightText);
    return blended;
}

void setCurrentThemeColours(const Theme &theme) {
    g_worn = theme;
    g_wornBuilt = true;
}

bool darkMode() { return g_dark; }

void setDarkMode(bool dark) {
    if (dark == g_dark && g_wornBuilt) return;
    g_dark = dark;
    rebuildWorn();
}

const Theme &currentTheme() {
    if (!g_wornBuilt) rebuildWorn();
    return g_worn;
}

void setCurrentTheme(const QString &name) {
    g_currentThemeName = themeByName(name).name;
    rebuildWorn();
}
