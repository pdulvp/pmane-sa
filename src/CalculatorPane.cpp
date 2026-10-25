#include "CalculatorPane.h"

#include "Theme.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QGridLayout>
#include <QListWidget>
#include <QPushButton>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>

#include <cmath>

namespace {

// The same layout as the month pane beside it: a strip down the left with the
// pane's name written up it, a band across the top, and the working area in the
// colour of a group window's interior.
constexpr int kBannerWidth = 36;
constexpr int kHeaderHeight = 32;
constexpr int kContentWidth = 290;
constexpr int kPaneWidth = kBannerWidth + kContentWidth;
constexpr int kPaneHeight = 384;

// The keys: four per row, and tall enough to be easy targets.
constexpr int kKeyHeight = 28;
constexpr int kKeySpacing = 4;
constexpr int kMargin = 8;

// How many results are kept: enough to look back over recent calculations, not
// so many that the list needs scrolling.
constexpr int kRemembered = 12;

QColor blend(const QColor &under, const QColor &over, qreal weight) {
    const qreal keep = 1.0 - weight;
    return QColor(qRound(under.red() * keep + over.red() * weight),
                  qRound(under.green() * keep + over.green() * weight),
                  qRound(under.blue() * keep + over.blue() * weight));
}

// A cursor over the expression, which is all the state the parser needs.
struct Reader {
    QString text;
    int at = 0;
    QString problem;

    void skipSpace() {
        while (at < text.size() && text.at(at).isSpace()) ++at;
    }
    bool looking(QChar c) {
        skipSpace();
        return at < text.size() && text.at(at) == c;
    }
    bool take(QChar c) {
        if (!looking(c)) return false;
        ++at;
        return true;
    }
    bool done() {
        skipSpace();
        return at >= text.size();
    }
};

double readSum(Reader &reader);

// A number, a bracketed expression, or either with a leading minus; any of them
// may be followed by a % sign.
double readTerm(Reader &reader) {
    if (reader.take(QLatin1Char('-'))) return -readTerm(reader);
    if (reader.take(QLatin1Char('+'))) return readTerm(reader);

    double value = 0.0;
    if (reader.take(QLatin1Char('('))) {
        value = readSum(reader);
        if (!reader.take(QLatin1Char(')')) && reader.problem.isEmpty()) {
            reader.problem = CalculatorPane::tr("a bracket is not closed");
        }
    } else {
        reader.skipSpace();
        const int from = reader.at;
        while (reader.at < reader.text.size() &&
               (reader.text.at(reader.at).isDigit() ||
                reader.text.at(reader.at) == QLatin1Char('.') ||
                reader.text.at(reader.at) == QLatin1Char(','))) {
            ++reader.at;
        }
        if (from == reader.at) {
            if (reader.problem.isEmpty()) {
                reader.problem = CalculatorPane::tr("something is missing");
            }
            return 0.0;
        }
        // A comma is a decimal point in most locales and means nothing else
        // here, so it is accepted as one.
        QString number = reader.text.mid(from, reader.at - from);
        number.replace(QLatin1Char(','), QLatin1Char('.'));
        bool read = false;
        value = number.toDouble(&read);
        if (!read && reader.problem.isEmpty()) {
            reader.problem = CalculatorPane::tr("'%1' is not a number").arg(number);
        }
    }

    // Trailing percent signs, however many are written: 50% is 0.5.
    while (reader.take(QLatin1Char('%'))) value /= 100.0;
    return value;
}

double readProduct(Reader &reader) {
    double value = readTerm(reader);
    while (reader.problem.isEmpty()) {
        if (reader.take(QLatin1Char('*'))) {
            value *= readTerm(reader);
        } else if (reader.take(QLatin1Char('/'))) {
            const double by = readTerm(reader);
            if (by == 0.0) {
                reader.problem = CalculatorPane::tr("division by zero");
                return 0.0;
            }
            value /= by;
        } else {
            break;
        }
    }
    return value;
}

double readSum(Reader &reader) {
    double value = readProduct(reader);
    while (reader.problem.isEmpty()) {
        if (reader.take(QLatin1Char('+'))) {
            value += readProduct(reader);
        } else if (reader.take(QLatin1Char('-'))) {
            value -= readProduct(reader);
        } else {
            break;
        }
    }
    return value;
}

} // namespace

CalculationResult calculate(const QString &expression) {
    CalculationResult result;
    if (expression.trimmed().isEmpty()) return result;

    Reader reader{expression, 0, QString()};
    const double value = readSum(reader);

    if (!reader.problem.isEmpty()) {
        result.problem = reader.problem;
        return result;
    }
    if (!reader.done()) {
        result.problem = CalculatorPane::tr("'%1' is in the way")
                             .arg(reader.text.mid(reader.at).trimmed());
        return result;
    }
    if (!std::isfinite(value)) {
        result.problem = CalculatorPane::tr("that does not come to a number");
        return result;
    }

    result.ok = true;
    result.value = value;
    return result;
}

QString formatNumber(double value) {
    // Up to ten significant digits, with trailing zeroes removed: 1/4 is 0.25,
    // not 0.2500000000.
    QString text = QString::number(value, 'g', 10);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) text.chop(1);
        if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    }
    return text;
}

CalculatorPane::CalculatorPane(QWidget *parent) : QWidget(parent, Qt::Popup) {
    m_history = new QListWidget(this);
    m_history->setFrameShape(QFrame::NoFrame);
    m_history->setSelectionMode(QAbstractItemView::NoSelection);
    m_history->setFocusPolicy(Qt::NoFocus);

    m_entry = new QLineEdit(this);
    m_entry->setPlaceholderText(tr("(12 + 8) * 5%"));
    m_entry->setClearButtonEnabled(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kBannerWidth + kMargin, kHeaderHeight + kMargin / 2, kMargin,
                               kMargin);
    layout->setSpacing(kMargin);
    layout->addWidget(m_history, 1);
    layout->addWidget(m_entry);
    layout->addWidget(buildKeys());

    connect(m_entry, &QLineEdit::returnPressed, this, &CalculatorPane::evaluateEntry);

    setFixedSize(kPaneWidth, kPaneHeight);
    wearTheScheme();
}

void CalculatorPane::type(const QString &what) {
    // Replaces the selection, which is how the answer left in the entry is
    // overwritten by a new expression rather than appended to.
    m_entry->insert(what);
    m_entry->setFocus();
}

QWidget *CalculatorPane::buildKeys() {
    auto *keys = new QWidget(this);
    auto *grid = new QGridLayout(keys);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(kKeySpacing);

    // Each key's label and what it inserts: the symbols users expect are not
    // the ones the parser reads.
    struct Key {
        const char *face;
        const char *types;
    };
    static const Key rows[5][4] = {
        {{"(", "("}, {")", ")"}, {"%", "%"}, {"C", nullptr}},
        {{"7", "7"}, {"8", "8"}, {"9", "9"}, {"\u00f7", "/"}},
        {{"4", "4"}, {"5", "5"}, {"6", "6"}, {"\u00d7", "*"}},
        {{"1", "1"}, {"2", "2"}, {"3", "3"}, {"\u2212", "-"}},
        {{"0", "0"}, {".", "."}, {"\u232b", nullptr}, {"+", "+"}},
    };

    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 4; ++column) {
            const Key &key = rows[row][column];
            auto *button = new QPushButton(QString::fromUtf8(key.face), keys);
            button->setFixedHeight(kKeyHeight);
            // A key that took focus would take the keyboard away from the entry
            // field.
            button->setFocusPolicy(Qt::NoFocus);
            button->setAutoDefault(false);
            m_keys.append(button);
            grid->addWidget(button, row, column);

            if (key.types) {
                const QString types = QString::fromUtf8(key.types);
                connect(button, &QPushButton::clicked, this, [this, types] { type(types); });
            } else if (QString::fromUtf8(key.face) == QStringLiteral("C")) {
                button->setToolTip(tr("Clear"));
                connect(button, &QPushButton::clicked, this, [this] {
                    m_entry->clear();
                    m_entry->setFocus();
                });
            } else {
                button->setToolTip(tr("Back"));
                connect(button, &QPushButton::clicked, this, [this] {
                    m_entry->backspace();
                    m_entry->setFocus();
                });
            }
        }
    }

    auto *equals = new QPushButton(QStringLiteral("="), keys);
    equals->setFixedHeight(kKeyHeight);
    equals->setFocusPolicy(Qt::NoFocus);
    equals->setAutoDefault(false);
    m_keys.append(equals);
    grid->addWidget(equals, 5, 0, 1, 4);
    connect(equals, &QPushButton::clicked, this, &CalculatorPane::evaluateEntry);

    return keys;
}

void CalculatorPane::wearTheScheme() {
    const Theme &theme = currentTheme();
    const QColor edge = blend(theme.windowBg, theme.windowText, 0.30);

    const QString dressed =
        QStringLiteral("QListWidget, QLineEdit { background: %1; color: %2; "
                       "border: 1px solid %3; border-radius: 3px; }"
                       "QLineEdit { padding: 4px 6px; }"
                       "QListWidget::item { padding: 1px 2px; }")
            .arg(theme.windowBg.name(), theme.windowText.name(), edge.name());
    m_history->setStyleSheet(dressed);
    m_entry->setStyleSheet(dressed);

    // The keys use the scheme's button colours, as a dialog's do.
    const QString keyLook =
        QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                       "border-radius: 3px; }"
                       "QPushButton:hover { border: 1px solid %4; }"
                       "QPushButton:pressed { background: %5; }")
            .arg(theme.buttonFace.name(), theme.buttonText.name(), edge.name(),
                 theme.titleActive.name(),
                 blend(theme.buttonFace, theme.buttonText, 0.12).name());
    for (QPushButton *key : m_keys) key->setStyleSheet(keyLook);
}

void CalculatorPane::evaluateEntry() {
    const QString expression = m_entry->text().trimmed();
    if (expression.isEmpty()) return;

    const CalculationResult result = calculate(expression);
    const QString line = result.ok
                             ? tr("%1  =  %2").arg(expression, formatNumber(result.value))
                             : tr("%1  --  %2").arg(expression, result.problem);

    auto *row = new QListWidgetItem(line, m_history);
    if (!result.ok) row->setForeground(currentTheme().titleActive);
    m_history->scrollToItem(row);

    // Only the last few are kept; this is not a record.
    while (m_history->count() > kRemembered) delete m_history->takeItem(0);

    // The answer is left in the entry, selected, so it can be used in the next
    // expression or typed over.
    if (result.ok) m_entry->setText(formatNumber(result.value));
    m_entry->selectAll();
}

void CalculatorPane::popUpAt(QWidget *anchor) {
    // The scheme may have changed since it was last shown.
    wearTheScheme();

    QPoint where = anchor->mapToGlobal(QPoint(0, anchor->height()));
    where.setX(where.x() + anchor->width() - width());
    const QRect screen = anchor->screen()->availableGeometry();
    if (where.x() + width() > screen.right()) where.setX(screen.right() - width());
    if (where.x() < screen.left()) where.setX(screen.left());
    move(where);
    show();
    m_entry->setFocus();
    m_entry->selectAll();
}

void CalculatorPane::paintEvent(QPaintEvent *) {
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
                     Qt::AlignCenter, tr("Calculator"));
    painter.restore();

    const QRect header(kBannerWidth, 0, width() - kBannerWidth, kHeaderHeight);
    painter.fillRect(header, theme.titleActive);
    QFont heading = font();
    heading.setBold(true);
    painter.setFont(heading);
    painter.setPen(theme.titleActiveText);
    painter.drawText(header, Qt::AlignCenter, tr("Calculator"));

    painter.setPen(blend(theme.windowBg, theme.windowText, 0.35));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}
