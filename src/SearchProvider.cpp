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

// Big enough for the shell's result rows without carrying more than it will
// ever draw.
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

// The name the shell knows us by, and where it looks. Both are repeated in
// the search provider's .ini file, which is how the shell is told to ask at
// all.
const char *kBusName = "fr.pdulvp.PMane";
const char *kObjectPath = "/fr/pdulvp/PMane/SearchProvider";

// A result says where its item is: the screen, the group and the place in
// it, and the name to check all three against. Separated by a character
// nothing in a name will be.
const QChar kSeparator = QLatin1Char('\x1f');

QString identifierFor(const QString &screen, int group, int item, const QString &name) {
    return screen + kSeparator + QString::number(group) + kSeparator +
           QString::number(item) + kSeparator + name;
}

// Every word has to appear somewhere, which is how a search narrows as more
// is typed rather than widening.
bool matches(const QString &haystack, const QStringList &terms) {
    for (const QString &term : terms) {
        if (!haystack.contains(term, Qt::CaseInsensitive)) return false;
    }
    return true;
}

} // namespace

SearchProvider::SearchProvider(MainWindow *window) : QObject(window), m_window(window) {
    // The shell wants an array of dictionaries back from GetResultMetas, and
    // Qt has to be told how to put one on the wire.
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
            // The group counts towards a match: it is what tells one DOOM
            // from another, and what someone would think to type.
            if (!matches(items[i].name + QLatin1Char(' ') + groupTitle, terms)) continue;
            results << identifierFor(screen, g, i, items[i].name);
        }
    }
    return results;
}

QStringList SearchProvider::GetSubsearchResultSet(const QStringList &previous,
                                                  const QStringList &terms) {
    // Narrowing what was already found, rather than looking again: the shell
    // hands back the earlier results for exactly this.
    QStringList results;
    for (const QString &identifier : previous) {
        const QStringList parts = identifier.split(kSeparator);
        if (parts.size() != 4) continue;
        if (matches(parts.at(3), terms)) results << identifier;
    }
    // A term that narrows nothing may still match a group it did not before.
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
            // Which group it is in, which is the useful thing to say about
            // an item: half of these are called dosbox-run.conf.
            meta.insert(QStringLiteral("description"), groups[g]->title());
            if (i >= 0 && i < items.size()) {
                // The icon the item is actually wearing, pixels and all: its
                // artwork is as often inside an executable as it is a file
                // the shell could open for itself.
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
    // Shown, not started: the desktop search says where something is.
    m_window->revealItem(parts.at(0), parts.at(1).toInt(), parts.at(2).toInt(), parts.at(3));
}

void SearchProvider::LaunchSearch(const QStringList &terms, uint timestamp) {
    Q_UNUSED(terms);
    Q_UNUSED(timestamp);
    // There is no search of our own to open, so the application itself is
    // what "show everything" means here.
    if (!m_window) return;
    if (m_window->isMinimized()) m_window->showNormal();
    m_window->raise();
    m_window->activateWindow();
}
