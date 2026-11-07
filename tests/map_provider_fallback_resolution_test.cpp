#include "map/core/map_provider_fallback_resolution.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <cstdio>

int main()
{
    const QVector<MapProviderEntry> entries = {
        {QStringLiteral("a"), QStringLiteral("A"), {}, 0, 17},
        {QStringLiteral("b"), QStringLiteral("B"), {}, 0, 18},
        {QStringLiteral("c"), QStringLiteral("C"), {}, 0, 20},
        {QStringLiteral("disabled"), QStringLiteral("D"), {}, 0, 25, false},
        {QStringLiteral("gap"), QStringLiteral("Gap"), {}, 20, 24},
        {QStringLiteral("short"), QStringLiteral("Short"), {}, 0, 12}
    };
    const QHash<QString, QString> chain = {
        {QStringLiteral("a"), QStringLiteral("b")},
        {QStringLiteral("b"), QStringLiteral("c")},
        // Malformed preference must not recurse when zoom ceilings decrease.
        {QStringLiteral("c"), QStringLiteral("a")}
    };
    struct Case { int zoom; const char *expected; };
    const Case cases[] = {
        {0, "a"}, {16, "a"}, {17, "a"},
        {18, "b"}, {19, "c"}, {20, "c"}, {22, "c"}
    };
    for (const Case &test : cases)
    {
        const QString actual = MapProviderFallbackResolution::sourceForZoom(
            entries, chain, QStringLiteral("a"), test.zoom);
        if (actual != QString::fromLatin1(test.expected))
        {
            std::fprintf(stderr, "zoom %d: got %s, expected %s\n",
                         test.zoom, actual.toUtf8().constData(), test.expected);
            return 1;
        }
    }
    if (MapProviderFallbackResolution::maximumSourceZoom(
            entries, chain, QStringLiteral("a")) != 20)
        return 2;
    if (MapProviderFallbackResolution::maximumSourceZoom(
            entries, {}, QStringLiteral("a")) != 17)
        return 3;
    if (MapProviderFallbackResolution::sourceForZoom(
            entries, {}, QStringLiteral("a"), 25) != QStringLiteral("a"))
        return 4;
    if (MapProviderFallbackResolution::maximumSourceZoom(
            entries, {{QStringLiteral("a"), QStringLiteral("disabled")}},
            QStringLiteral("a")) != 17)
        return 5;
    if (MapProviderFallbackResolution::maximumSourceZoom(
            entries, {{QStringLiteral("a"), QStringLiteral("gap")}},
            QStringLiteral("a")) != 17)
        return 6;
    if (MapProviderFallbackResolution::maximumSourceZoom(
            entries, {{QStringLiteral("a"), QStringLiteral("short")}},
            QStringLiteral("a")) != 17)
        return 7;
    if (!MapProviderFallbackResolution::sourceForZoom(
            entries, chain, QStringLiteral("missing"), 10).isEmpty())
        return 8;
    if (MapProviderFallbackResolution::maximumSourceZoom(entries,
            {{QStringLiteral("a"), QStringLiteral("b")},
             {QStringLiteral("b"), QStringLiteral("gap")}},
            QStringLiteral("a")) != 18)
        return 9;
    if (!MapProviderFallbackResolution::sourceForZoom(
            entries, chain, QStringLiteral("disabled"), 10).isEmpty())
        return 10;
    return 0;
}
