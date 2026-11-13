#pragma once

// Internal helpers shared by the SettingsDialog translation units.  They were
// an anonymous namespace inside SettingsDialog.cpp before the page builders
// were split out; inline definitions keep every page using the same widgets.

#include "SystemInfo.h"
#include "media/DecoderInfo.h"

#include <QIcon>
#include <QLabel>
#include <QSizePolicy>
#include <QString>
#include <QStringList>

namespace pvf {
namespace settings_detail {

#ifndef PVFORGE_MCP_BINARY
#define PVFORGE_MCP_BINARY "pvforge-mcp"
#endif

inline QIcon themeIcon(const QStringList& names)
{
    for (const QString& name : names)
    {
        const QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull())
            return icon;
    }
    return QIcon();
}

inline QLabel* infoLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    label->setMinimumWidth(0);
    return label;
}

inline QString aboutText()
{
    QString text = collectSystemInfo().toText();
    text += QStringLiteral("\n\n【解码】\n") + decoderInfo().toText();
    text += QStringLiteral(
        "\n\n【项目】\n"
        "主页：https://github.com/xmb505/pvforge\n"
        "许可证：MIT");
    return text;
}

} // namespace settings_detail
} // namespace pvf
