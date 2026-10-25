#include "SearchProvider.h"

#include "GroupWindow.h"
#include "MainWindow.h"

#include <QDBusConnection>
#include <QDBusMetaType>
#include <QIcon>
#include <QImage>

QDBusArgument &operator<<(QDBusArgument &argument, const SearchIconData &icon) {
    argument.beginStructure();
    argument << icon.width << icon.height << icon.rowstride << icon.hasAlpha
             << icon.bitsPerSample << icon.channels << icon.pixels;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SearchIconData &icon) {
    argument.beginStructure();
    argument >> icon.width >> icon.height >> icon.rowstride >> icon.hasAlpha >>
        icon.bitsPerSample >> icon.channels >> icon.pixels;
    argument.endStructure();
    return argument;
}

namespace {

// Big enough for the shell's result rows without sending more pixels than it
// will draw.
constexpr int kIconSize = 48;

// An item's artwork as pixels. Null when it has none worth sending.
SearchIconData iconDataFor(const QIcon &icon) {
    SearchIconData data;
    if (icon.isNull()) return data;

    const QImage image = icon.pixmap(kIconSize, kIconSize)
                             .toImage()
                             .convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) return data;

    data.width = image.width();
    data.height = image.height();
    data.rowstride = image.bytesPerLine();
    data.pixels = QByteArray(reinterpret_cast<const char *>(image.constBits()),
                             int(image.sizeInBytes()));
    return data;
}

// The bus name and object path the shell uses. Both are repeated in the search
// provider's .ini file, which is what tells the shell to query this application
// at all.
const char *kBusName = "fr.pdulvp.PMane";
const char *kObjectPath = "/fr/pdulvp/PMane/SearchProvider";

// A result identifier encodes where its item is: the screen, the group, the
// index within it, and the name to validate all three against. Separated by a
// character that cannot occur in a name.
const QChar kSeparator = QLatin1Char('\x1f');

QString identifierFor(const QString &screen, int group, int item, const QString &name) {
    return screen + kSeparator + QString::number(group) + kSeparator +
           QString::number(item) + kSeparator + name;
}

// Every term has to match something, so a search narrows as more is typed
// rather than widening.
bool matches(const QString &haystack, const QStringList &terms) {
    for (const QString &term : terms) {
        if (!haystack.contains(term, Qt::CaseInsensitive)) return false;
    }
    return true;
}

} // namespace

SearchProvider::SearchProvider(MainWindow *window) : QObject(window), m_window(window) {
    // The shell expects an array of dictionaries from GetResultMetas, and Qt
    // has to be told how to marshal one.
    qDBusRegisterMetaType<QVariantMap>();
    qDBusRegisterMetaType<QList<QVariantMap>>();
    qDBusRegisterMetaType<SearchIconData>();
}

bool SearchProvider::registerOnBus() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    if (!bus.registerService(QLatin1String(kBusName))) return false;
    return bus.registerObject(QLatin1String(kObjectPath), this,
                              QDBusConnection::ExportAllSlots);
}

QStringList SearchProvider::GetInitialResultSet(const QStringList &terms) {
    QStringList results;
    if (!m_window || terms.isEmpty()) return results;

    const QString screen = m_window->currentScreen();
    const QVector<GroupWindow *> groups = m_window->groups();
    for (int g = 0; g < groups.size(); ++g) {
        const QString groupTitle = groups[g]->title();
        const QVector<ProgramItem> &items = groups[g]->items();
        for (int i = 0; i < items.size(); ++i) {
            // The group name counts towards a match: it is what distinguishes
            // two items with the same name, and a plausible thing to type.
            if (!matches(items[i].name + QLatin1Char(' ') + groupTitle, terms)) continue;
            results << identifierFor(screen, g, i, items[i].name);
        }
    }
    return results;
}

QStringList SearchProvider::GetSubsearchResultSet(const QStringList &previous,
                                                  const QStringList &terms) {
    // Narrows the previous results rather than searching again; the shell
    // passes them back for exactly this.
    QStringList results;
    for (const QString &identifier : previous) {
        const QStringList parts = identifier.split(kSeparator);
        if (parts.size() != 4) continue;
        if (matches(parts.at(3), terms)) results << identifier;
    }
    // A term that narrows nothing may still match a group it did not match
    // before.
    return results.isEmpty() ? GetInitialResultSet(terms) : results;
}

QList<QVariantMap> SearchProvider::GetResultMetas(const QStringList &identifiers) {
    QList<QVariantMap> metas;
    if (!m_window) return metas;

    const QVector<GroupWindow *> groups = m_window->groups();
    for (const QString &identifier : identifiers) {
        const QStringList parts = identifier.split(kSeparator);
        if (parts.size() != 4) continue;
        const int g = parts.at(1).toInt();
        const int i = parts.at(2).toInt();

        QVariantMap meta;
        meta.insert(QStringLiteral("id"), identifier);
        meta.insert(QStringLiteral("name"), parts.at(3));

        if (g >= 0 && g < groups.size()) {
            const QVector<ProgramItem> &items = groups[g]->items();
            // The group it is in, which is the useful detail: many items are
            // called dosbox-run.conf.
            meta.insert(QStringLiteral("description"), groups[g]->title());
            if (i >= 0 && i < items.size()) {
                // The item's actual icon, sent as pixels: its artwork is as
                // often inside an executable as in a file the shell could open
                // itself.
                const SearchIconData data = iconDataFor(items[i].icon);
                if (data.width > 0) {
                    meta.insert(QStringLiteral("icon-data"), QVariant::fromValue(data));
                }
            }
        }
        metas.append(meta);
    }
    return metas;
}

void SearchProvider::ActivateResult(const QString &identifier, const QStringList &terms,
                                    uint timestamp) {
    Q_UNUSED(terms);
    Q_UNUSED(timestamp);
    if (!m_window) return;

    const QStringList parts = identifier.split(kSeparator);
    if (parts.size() != 4) return;
    // Revealed, not started: the desktop search locates an item.
    m_window->revealItem(parts.at(0), parts.at(1).toInt(), parts.at(2).toInt(), parts.at(3));
}

void SearchProvider::LaunchSearch(const QStringList &terms, uint timestamp) {
    Q_UNUSED(terms);
    Q_UNUSED(timestamp);
    // There is no search window of this application's own to open, so "show all
    // results" raises the application itself.
    if (!m_window) return;
    if (m_window->isMinimized()) m_window->showNormal();
    m_window->raise();
    m_window->activateWindow();
}
