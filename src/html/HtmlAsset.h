#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QVector>

namespace pvf {
namespace html {

// One variable declared by an HTML component manifest.
struct HtmlVariable
{
    QString name;
    double min = 0.0;
    double max = 1.0;
    double defaultValue = 0.0;
    bool integer = false;
};

struct HtmlComponent
{
    QString id;
    QString name;
    QVector<HtmlVariable> variables;
};

// Parsed `pvforge-html` asset. The manifest is a contract between the AI that
// wrote the document and the host; validation happens before any browser work.
struct HtmlAsset
{
    QString html;
    QSize logicalSize;
    bool transparent = true;
    bool timeDependent = false;
    QVector<HtmlComponent> components;

    // Keeps the original size so callers can enforce the 5 MB asset limit
    // without reparsing.
    int sourceBytes = 0;

    static HtmlAsset parse(const QByteArray& json, QString* error);
    static HtmlAsset parseFile(const QString& path, QString* error);

    // Canonical `component.variable` keys, used by frame requests.
    QVector<QString> variableKeys() const;
    const HtmlVariable* findVariable(const QString& key) const;
};

// Limits from the M6 plan. Kept public so the asset importer and the MCP
// tools can quote the same numbers.
namespace Limits {
constexpr int kMaxHtmlBytes = 2 * 1024 * 1024;
constexpr int kMaxAssetBytes = 5 * 1024 * 1024;
constexpr int kMaxComponents = 16;
constexpr int kMaxEdge = 4096;
constexpr int kMaxVariablesPerComponent = 256;
} // namespace Limits

} // namespace html
} // namespace pvf
