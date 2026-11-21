#include "PropertiesPanel.h"

#include "CollapsibleSection.h"
#include "PropertiesPanelInternal.h"
#include "WholeFrameEdit.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "model/Content.h"
#include "model/Entity.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf {

using namespace ui::detail;
namespace wholeframe = ui::wholeframe;

namespace {

QString jsonValueText(const QJsonValue& value)
{
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 8);
    if (value.isString())
        return value.toString();
    if (value.isNull() || value.isUndefined())
        return QStringLiteral("null");
    return QString::fromUtf8(
        QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

// Manifest variables may be plain defaults ({"speed": 2.5}) or descriptor
// objects ({"speed": {"type": "number", "default": 2.5, "min": 0, "max": 10}}).
struct HtmlVarSpec
{
    QString name;
    bool numeric = false;
    double defaultValue = 0.0;
    double minimum = -1000000000.0;
    double maximum = 1000000000.0;
    double step = 0.01;
    int decimals = 3;
    QString text;
};

HtmlVarSpec htmlVarSpec(const QString& name, const QJsonValue& value)
{
    HtmlVarSpec spec;
    spec.name = name;

    QJsonValue defaultValue = value;
    if (value.isObject())
    {
        const QJsonObject object = value.toObject();
        if (object.contains(QStringLiteral("default")))
            defaultValue = object.value(QStringLiteral("default"));
        const double minimum =
            object.value(QStringLiteral("min")).toDouble(spec.minimum);
        const double maximum =
            object.value(QStringLiteral("max")).toDouble(spec.maximum);
        if (maximum > minimum)
        {
            spec.minimum = minimum;
            spec.maximum = maximum;
        }
        const bool integer =
            object.value(QStringLiteral("integer")).toBool(false);
        spec.decimals = integer ? 0 : 3;
        spec.step = integer ? 1.0 : 0.01;
        if (object.value(QStringLiteral("type")).toString()
            == QLatin1String("number"))
            spec.numeric = true;
    }

    if (defaultValue.isDouble())
        spec.numeric = true;
    if (spec.numeric)
        spec.defaultValue = defaultValue.toDouble();
    else
        spec.text = jsonValueText(defaultValue);
    return spec;
}

} // namespace

void PropertiesPanel::buildSessionHtmlSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(10, 8, 10, 8);
    box->setSpacing(6);

    m_sessionHtmlInfo = new QLabel(content);
    m_sessionHtmlInfo->setObjectName(QStringLiteral("sessionHtmlInfo"));
    m_sessionHtmlInfo->setWordWrap(true);
    m_sessionHtmlInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_sessionHtmlInfo->setSizePolicy(QSizePolicy::Ignored,
                                      QSizePolicy::Preferred);
    m_sessionHtmlInfo->setMinimumWidth(0);
    styleHintLabel(m_sessionHtmlInfo);
    box->addWidget(m_sessionHtmlInfo);

    m_sessionHtmlEmpty =
        new QLabel(QStringLiteral("（manifest 未声明变量）"), content);
    m_sessionHtmlEmpty->setObjectName(QStringLiteral("sessionHtmlEmpty"));
    m_sessionHtmlEmpty->setWordWrap(true);
    box->addWidget(m_sessionHtmlEmpty);

    m_sessionHtmlVariables = new QWidget(content);
    m_sessionHtmlVariablesLayout = new QVBoxLayout(m_sessionHtmlVariables);
    m_sessionHtmlVariablesLayout->setContentsMargins(0, 0, 0, 0);
    m_sessionHtmlVariablesLayout->setSpacing(4);
    box->addWidget(m_sessionHtmlVariables);

    m_sessionHtmlWarning = new QLabel(content);
    m_sessionHtmlWarning->setObjectName(QStringLiteral("sessionHtmlWarning"));
    m_sessionHtmlWarning->setWordWrap(true);
    m_sessionHtmlWarning->setSizePolicy(QSizePolicy::Ignored,
                                         QSizePolicy::Preferred);
    m_sessionHtmlWarning->setMinimumWidth(0);
    styleHintLabel(m_sessionHtmlWarning);
    m_sessionHtmlWarning->hide();
    box->addWidget(m_sessionHtmlWarning);

    m_sessionHtmlSection =
        new CollapsibleSection(QStringLiteral("HTML 变量"), content, this);
    m_sessionHtmlSection->setObjectName(QStringLiteral("sessionHtmlSection"));
    m_sessionHtmlSection->setExpanded(true);
    layout->insertWidget(layout->count() - 1, m_sessionHtmlSection);
    m_sessionHtmlSection->hide();
}

void PropertiesPanel::rebuildSessionHtmlRows(const QJsonObject& variables)
{
    m_sessionHtmlRows.clear();
    if (!m_sessionHtmlVariablesLayout)
        return;

    while (QLayoutItem* item = m_sessionHtmlVariablesLayout->takeAt(0))
    {
        if (QWidget* widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it)
    {
        const HtmlVarSpec spec = htmlVarSpec(it.key(), it.value());
        auto* rowWidget = new QWidget(m_sessionHtmlVariables);
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto* nameLabel = new QLabel(spec.name, rowWidget);
        nameLabel->setObjectName(
            QStringLiteral("sessionHtmlVarName_var.%1").arg(spec.name));
        nameLabel->setToolTip(
            QStringLiteral("var.%1 · 默认值 %2")
                .arg(spec.name)
                .arg(spec.numeric ? QString::number(spec.defaultValue, 'g', 8)
                                  : spec.text));
        rowLayout->addWidget(nameLabel, 1);

        SessionHtmlRow row;
        row.name = spec.name;
        row.numeric = spec.numeric;
        row.defaultValue = spec.defaultValue;
        row.minimum = spec.minimum;
        row.maximum = spec.maximum;
        row.step = spec.step;
        row.decimals = spec.decimals;
        row.readOnlyText = spec.text;
        row.nameLabel = nameLabel;

        if (spec.numeric)
        {
            auto* spin = new QDoubleSpinBox(rowWidget);
            spin->setObjectName(
                QStringLiteral("sessionHtmlVarValue_var.%1").arg(spec.name));
            spin->setRange(spec.minimum, spec.maximum);
            spin->setDecimals(spec.decimals);
            spin->setSingleStep(spec.step);
            spin->setKeyboardTracking(false);
            spin->setToolTip(
                QStringLiteral("manifest 默认值 %1；修改后按自动整帧关键帧规则写入")
                    .arg(spec.defaultValue));
            rowLayout->addWidget(spin, 1);
            row.spin = spin;
            connect(spin, &QDoubleSpinBox::editingFinished, this,
                    [this, name = spec.name, spin] {
                        editSessionHtmlVariable(name, spin->value());
                    });
        }
        else
        {
            auto* value = new QLabel(spec.text, rowWidget);
            value->setObjectName(
                QStringLiteral("sessionHtmlVarValue_var.%1").arg(spec.name));
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            value->setToolTip(QStringLiteral("var.%1（只读）").arg(spec.name));
            rowLayout->addWidget(value, 1);
            row.readOnlyValue = value;
        }

        m_sessionHtmlVariablesLayout->addWidget(rowWidget);
        m_sessionHtmlRows.append(row);
    }
}

void PropertiesPanel::reloadSessionHtml(const model::Entity& entity)
{
    const bool isHtml = entity.content.kind == ContentKind::Asset
        && entity.content.asset.type == AssetType::Html;
    m_sessionHtmlSection->setVisible(isHtml);
    if (!isHtml)
    {
        m_sessionHtmlUuid.clear();
        m_sessionHtmlHash.clear();
        m_sessionHtmlRows.clear();
        return;
    }

    const AssetHash hash = entity.content.asset.hash;
    if (entity.uuid != m_sessionHtmlUuid || hash != m_sessionHtmlHash)
    {
        m_sessionHtmlUuid = entity.uuid;
        m_sessionHtmlHash = hash;
        m_sessionHtmlPresent = false;
        m_sessionHtmlTimeDependent = true;
        QJsonObject variables;
        QString name;
        int width = 0;
        int height = 0;
        if (const auto stored = m_session->assets().get(hash))
        {
            m_sessionHtmlPresent = true;
            m_sessionHtmlTimeDependent =
                stored->metadata.value(QStringLiteral("timeDependent"))
                    .toBool(true);
            variables =
                stored->metadata.value(QStringLiteral("variables")).toObject();
            name = stored->name;
            width = stored->metadata.value(QStringLiteral("width")).toInt(0);
            height = stored->metadata.value(QStringLiteral("height")).toInt(0);
        }
        rebuildSessionHtmlRows(variables);

        if (!m_sessionHtmlPresent)
        {
            m_sessionHtmlInfo->setText(
                QStringLiteral("timeDependent：未知（素材缺失）"));
        }
        else
        {
            QString info = name.isEmpty() ? QStringLiteral("HTML") : name;
            if (width > 0 && height > 0)
                info += QStringLiteral(" · %1×%2").arg(width).arg(height);
            info += QStringLiteral(" · timeDependent：%1")
                        .arg(m_sessionHtmlTimeDependent
                                 ? QStringLiteral("是（每帧渲染）")
                                 : QStringLiteral("否（可静态缓存）"));
            m_sessionHtmlInfo->setText(info);
        }
    }

    m_sessionHtmlEmpty->setVisible(m_sessionHtmlRows.isEmpty());

    QStringList warnings;
    if (!m_sessionHtmlPresent)
        warnings << QStringLiteral("素材缺失，无法读取 manifest 变量。");

    QSet<QString> declared;
    for (const SessionHtmlRow& row : m_sessionHtmlRows)
        declared.insert(row.name);
    QStringList orphans;
    QSet<QString> seen;
    for (const model::Keyframe& key : entity.keyframes)
    {
        for (auto it = key.htmlVars.constBegin(); it != key.htmlVars.constEnd();
             ++it)
        {
            if (!declared.contains(it.key()) && !seen.contains(it.key()))
            {
                seen.insert(it.key());
                orphans << it.key();
            }
        }
    }
    if (!orphans.isEmpty())
        warnings << QStringLiteral("未使用的 HTML 变量：%1")
                        .arg(orphans.join(QStringLiteral(", ")));
    m_sessionHtmlWarning->setText(warnings.join(QStringLiteral("\n")));
    m_sessionHtmlWarning->setVisible(!warnings.isEmpty());

    reloadSessionHtmlValues(entity);
}

void PropertiesPanel::reloadSessionHtmlValues(const model::Entity& entity)
{
    const int relative = activeDisplayKeyFrame(entity);
    const bool inRange = relative >= 0 && relative < entity.duration();
    const bool editable = sessionEditable();

    model::Keyframe key;
    if (inRange)
        key = wholeframe::keyAt(entity, relative, m_session);

    for (SessionHtmlRow& row : m_sessionHtmlRows)
    {
        if (!row.numeric)
        {
            row.readOnlyValue->setText(row.readOnlyText);
            continue;
        }
        double value = row.defaultValue;
        if (inRange && key.htmlVars.value(row.name).isDouble())
            value = key.htmlVars.value(row.name).toDouble();
        setSpinIfIdle(row.spin, value);
        row.spin->setEnabled(editable && inRange);
    }
}

void PropertiesPanel::editSessionHtmlVariable(const QString& name, double value)
{
    if (m_editing)
        return;
    writeHtmlVariable(name, value);
}

} // namespace pvf
