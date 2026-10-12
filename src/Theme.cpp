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

    // Each size the icon offers, painted through: the shape is kept and the
    // colour thrown away, which is what tinting a one-colour icon means.
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
        // Steel blue over pale grey-blue: the one it opens on, so the quiet
        // one. Everything else in the list is a departure from this.
        make("Harbour",   "#3f6076", "#1d3a52", "#ffffff", "#7d94a6", "#38434b", "#fbfdfe", "#14212b", "#ccd8e0", "#14212b", "#1d3a52", "#ffffff"),
        // Paper and sepia ink. The warm end of neutral, for anyone who finds
        // a coloured desktop tiring.
        make("Vellum",    "#c9b596", "#6b4f2f", "#fdf6e8", "#b4a287", "#584f42", "#fffdf7", "#2e2416", "#e8dcc4", "#2e2416", "#6b4f2f", "#fdf6e8"),
        // Leaf green with a plum caption: the two sides of a fruit tree.
        make("Orchard",   "#8fae7d", "#5c3a5e", "#ffffff", "#a9b9a0", "#586053", "#fbfff8", "#1f2a1a", "#d8e4cf", "#1f2a1a", "#5c3a5e", "#ffffff"),
        // Saturated blue, the loudest the list gets without leaving one hue.
        make("Cobalt",    "#33539e", "#15255c", "#ffffff", "#7f8fb8", "#394053", "#fafbff", "#111a33", "#c8d2ea", "#111a33", "#33539e", "#ffffff"),
        // Stalk and leaf: a pink desktop under a green caption, which ought
        // to clash and somehow does not.
        make("Rhubarb",   "#c76b7e", "#3f6b43", "#ffffff", "#d3a3ad", "#6c5358", "#fffafb", "#2b1a1e", "#f1d7dd", "#2b1a1e", "#3f6b43", "#ffffff"),
        // Road grey and lane-marking yellow. The caption carries the yellow
        // as text, and the yellow is also what selects.
        make("Tarmac",    "#4a4e52", "#26292c", "#f2c230", "#7a7f84", "#303233", "#f7f8f8", "#1a1c1e", "#cfd3d6", "#1a1c1e", "#f2c230", "#1a1c1e"),
        // Burnt orange and dark earth: the warm scheme with colour actually
        // in it, where Vellum is the warm scheme without.
        make("Ochre",     "#c08a4a", "#6d3f16", "#fff4e4", "#cbab85", "#685744", "#fffcf6", "#32200d", "#ecd9bd", "#32200d", "#6d3f16", "#fff4e4"),
        // Lavender, a long way from grey without being bright about it.
        make("Lupin",     "#8e86b8", "#45397a", "#ffffff", "#a9a3c6", "#545263", "#fdfcff", "#211c35", "#dcd8ee", "#211c35", "#45397a", "#ffffff"),
        // Deep red with gold lettering, after lacquerware. The only scheme
        // whose caption text is not simply black or white.
        make("Lacquer",   "#8c2f2a", "#4a1310", "#f0c75e", "#b0736f", "#482f2e", "#fffaf9", "#2b1110", "#e6cfcb", "#2b1110", "#8c2f2a", "#f0c75e"),
        // Ice: the palest scheme, and the one that holds up in a bright room.
        make("Glacier",   "#a8c6d4", "#3e6b80", "#ffffff", "#bfd3dc", "#677277", "#fdffff", "#16282f", "#e0edf2", "#16282f", "#3e6b80", "#ffffff"),
        // No colour at all, which is what makes it worth having: it is the
        // one scheme that never argues with an icon.
        make("Graphite",  "#6e7175", "#2f3134", "#ffffff", "#9b9ea1", "#4a4c4d", "#fcfcfc", "#1b1c1e", "#d4d5d7", "#1b1c1e", "#2f3134", "#ffffff"),
        // Tangerine, magenta, lemon and mint, all at once and on purpose.
        // Every list of colour schemes needs the one nobody admits to using.
        make("Sherbet",   "#ff7a4d", "#d6166b", "#fff6cf", "#ffb08f", "#7a2a10", "#fffdf2", "#3a1020", "#ffe08a", "#3a1020", "#2bbf9e", "#0b2b24"),
    };
    return themes;
}

const Theme &themeByName(const QString &name) {
    const QVector<Theme> &themes = builtinThemes();
    for (const Theme &t : themes) {
        if (t.name == name) return t;
    }

    // A desktop saved while the schemes were the Windows 3.1 Control Panel's
    // names the one it was wearing, and that name is no longer in the list.
    // Each is sent to whichever of the schemes above stands closest to it, so
    // such a desktop comes back in roughly the colours it was left in rather
    // than all of them coming back in the first scheme.
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

// A colour at a given brightness, keeping the hue it is recognisable by and
// easing the saturation: a surface carries colour less well the darker it
// gets, and a dark grey with a trace of rose in it reads as rose where a
// fully saturated one reads as a mistake.
// A surface in the scheme's own colour: the hue taken from the scheme, the
// saturation set outright rather than scaled. Scaling it is what turned the
// dark halves into a row of near-blacks -- a soft caption has little
// saturation to begin with, and a fraction of little is nothing.
QColor tintedSurface(const QColor &hueFrom, qreal value, qreal saturation) {
    const QColor hsv = hueFrom.toHsv();
    if (hsv.hue() < 0) {
        // A scheme with no colour in it stays without: Graphite's dark
        // half should be grey, not faintly red.
        return QColor::fromHsvF(0.0, 0.0, qBound(0.0, value, 1.0));
    }
    return QColor::fromHsvF(hsv.hue() / 360.0, qBound(0.0, saturation, 1.0),
                            qBound(0.0, value, 1.0));
}

// How far apart two surfaces actually look, which is not how far apart their
// brightness says they are: the eye weighs green far above red and red above
// blue, so a dark pink and a dark green of the very same lightness come out
// one on top of the other. Ratios here read as they do in the accessibility
// guidelines -- 1.0 is the same colour twice, and a surface a person can see
// the edge of wants something over 1.2.
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
    // Over 360, not 359: the wheel closes, and dividing by one less walks
    // every hue a little way round -- which showed up as Rhubarb coming back
    // one degree off itself.
    return QColor::fromHsvF(hue / 360.0, qBound(0.0, hsv.saturationF() * saturation, 1.0),
                            qBound(0.0, value, 1.0));
}

// Whatever was applied last, the first built-in until then, and whether its
// dark half is the one being worn.
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
// How far the dark desktop has to stand off the groups lying on it before the
// edge of a group is visible without looking for it.
constexpr qreal kDarkSurfaceGap = 1.25;
} // namespace

Theme darkVariant(const Theme &light) {
    Theme dark = light;

    // The scheme is known by its caption colour, so that is what the dark
    // surfaces are tinted with -- unless the caption has no colour in it, in
    // which case the desktop says what the scheme is. Hotdog Stand's caption
    // is black and its desktop is red; the red is the part worth keeping.
    QColor character = light.titleActive;
    if (character.toHsv().saturationF() < 0.15) character = light.desktop;

    // The desktop is the lighter surface and the groups sit darker on it,
    // so a group reads as a window cut into the desktop rather than as a
    // panel laid over it. Set below, once there is a group to clear.
    dark.desktop = atValue(light.desktop, 0.26, 1.0);

    // The paper and the ink change places, each keeping a trace of the
    // scheme: easier on the eye than a flat grey on a flat black.
    dark.buttonFace = tintedSurface(character, 0.25, 0.30);
    dark.buttonText = tintedSurface(character, 0.92, 0.10);
    dark.windowBg = tintedSurface(character, 0.20, 0.34);
    dark.windowText = tintedSurface(character, 0.95, 0.08);

    // And now the desktop is lifted until it is plainly lighter than the
    // groups on it. A strong colour reads far darker than its brightness
    // suggests -- a saturated teal or red at the same brightness as a grey
    // comes out below it -- so the gap is measured rather than assumed, and
    // measured as the eye takes it: lightness alone leaves a scheme whose
    // caption and desktop are far apart in hue, a green caption over a pink
    // desktop, with its groups invisible against the desktop they sit on.
    for (qreal value = 0.26;
         value < 0.80 && (dark.desktop.lightnessF() < dark.windowBg.lightnessF() + 0.05 ||
                          contrast(dark.desktop, dark.windowBg) < kDarkSurfaceGap);
         value += 0.03) {
        dark.desktop = atValue(light.desktop, value, 1.0);
    }

    // A caption that is already dark cannot go darker and still be seen
    // against the window behind it; one that is bright is left alone.
    const qreal captionLightness = light.titleActive.lightnessF();
    dark.titleActive = captionLightness < 0.30
                           ? atValue(light.titleActive, 0.45, 1.0)
                           : light.titleActive;
    dark.titleActiveText = readableTextOn(dark.titleActive);
    dark.titleInactive = atValue(light.titleInactive, 0.28, 0.7);
    dark.titleInactiveText = readableTextOn(dark.titleInactive);

    // What is selected keeps the colour it is selected in: the point of a
    // highlight is that it is the one thing not in the scheme's own key.
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

    Theme blended = to; // the name and anything not a colour comes from the end
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
