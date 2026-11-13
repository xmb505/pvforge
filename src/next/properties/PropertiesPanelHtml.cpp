#include "PropertiesPanel.h"

#include "PropertiesPanelInternal.h"
#include "common/Types.h"
#include "model/Easing.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace pvf::next {

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
// ENTITY_MODEL.md 6.4 only promises an object, so accept both shapes.
struct HtmlVarSpec
{
    QString name;
    QString track;
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
    spec.track = QStringLiteral("var.") + name;

    QJsonValue defaultValue = value;
    if (value.isObject())
    {
        const QJsonObject object = value.toObject();
        if (object.contains(QStringLiteral("default")))
            defaultValue = object.value(QStringLiteral("default"));
        const double minimum = object.value(QStringLiteral("min"))
                                   .toDouble(spec.minimum);
        const double maximum = object.value(QStringLiteral("max"))
                                   .toDouble(spec.maximum);
        if (maximum > minimum)
        {
            spec.minimum = minimum;
            spec.maximum = maximum;
        }
        const bool integer = object.value(QStringLiteral("integer")).toBool(false);
        spec.decimals = integer ? 0 : 3;
        spec.step = integer ? 1.0 : 0.01;
        if (object.value(QStringLiteral("type")).toString() == QLatin1String("number"))
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

void PropertiesPanel::buildHtmlVariablesSection()
{
    auto* group = new QGroupBox(QStringLiteral("HTML 变量"), m_content);
    m_htmlGroup = group;
    auto* outer = new QVBoxLayout(group);
    outer->setContentsMargins(10, 8, 10, 8);
    outer->setSpacing(6);

    m_htmlStatus = new QLabel(group);
    m_htmlStatus->setObjectName(QStringLiteral("htmlTimeDependentLabel"));
    m_htmlStatus->setWordWrap(true);
    m_htmlStatus->setToolTip(
        QStringLiteral("timeDependent=true 的 HTML 每帧渲染；false 时可以只缓存一张图。"
                       "变量值来自 .pvhtml manifest，轨道名是 var.<变量名>。"));
    outer->addWidget(m_htmlStatus);

    m_htmlEmpty = new QLabel(QStringLiteral("（manifest 未声明变量）"), group);
    m_htmlEmpty->setObjectName(QStringLiteral("htmlVariablesEmptyLabel"));
    m_htmlEmpty->setWordWrap(true);
    outer->addWidget(m_htmlEmpty);

    m_htmlGrid = new QGridLayout();
    m_htmlGrid->setContentsMargins(0, 0, 0, 0);
    m_htmlGrid->setHorizontalSpacing(6);
    m_htmlGrid->setVerticalSpacing(4);
    m_htmlGrid->setColumnStretch(2, 1);
    const QStringList headers = {QStringLiteral("变量"), QStringLiteral("关键帧"),
                                 QStringLiteral("当前值")};
    for (int column = 0; column < headers.size(); ++column)
    {
        auto* header = new QLabel(headers.at(column), group);
        QFont headerFont = header->font();
        headerFont.setBold(true);
        header->setFont(headerFont);
        m_htmlGrid->addWidget(header, 0, column);
    }
    outer->addLayout(m_htmlGrid);

    group->setVisible(false);
    m_contentLayout->addWidget(group);
}

void PropertiesPanel::reloadHtmlVariables(const model::Entity& entity)
{
    const bool isHtml = entity.content.kind == ContentKind::Asset
        && entity.content.asset.type == AssetType::Html;
    m_htmlGroup->setVisible(isHtml);
    if (!isHtml)
    {
        m_htmlUuid.clear();
        m_htmlHash.clear();
        m_htmlVariableDefs = QJsonObject();
        return;
    }

    const AssetHash hash = entity.content.asset.hash;
    if (entity.uuid != m_htmlUuid || hash != m_htmlHash)
    {
        m_htmlUuid = entity.uuid;
        m_htmlHash = hash;
        m_htmlVariableDefs = QJsonObject();
        m_htmlAssetPresent = false;
        if (m_document)
        {
            if (const auto stored = m_document->assets().get(hash))
            {
                m_htmlAssetPresent = true;
                m_htmlTimeDependent = stored->metadata
                                          .value(QStringLiteral("timeDependent"))
                                          .toBool(true);
                m_htmlVariableDefs = stored->metadata
                                         .value(QStringLiteral("variables"))
                                         .toObject();
            }
        }
        rebuildHtmlVariableRows();
    }

    if (!m_htmlAssetPresent)
    {
        m_htmlStatus->setText(QStringLiteral("timeDependent：未知（素材缺失）"));
        m_htmlEmpty->setText(QStringLiteral("素材缺失，无法读取 manifest 变量。"));
        m_htmlEmpty->setVisible(true);
    }
    else
    {
        m_htmlStatus->setText(
            QStringLiteral("timeDependent：%1")
                .arg(m_htmlTimeDependent ? QStringLiteral("是（每帧渲染）")
                                         : QStringLiteral("否（可静态缓存）")));
        m_htmlEmpty->setText(QStringLiteral("（manifest 未声明变量）"));
        m_htmlEmpty->setVisible(m_htmlRows.isEmpty());
    }

    const int relativeFrame = m_document ? m_document->playhead() - entity.start : -1;
    const int duration = entity.duration();
    const bool inRange = relativeFrame >= 0 && relativeFrame < duration;
    const int sampleFrame = qBound(0, relativeFrame, qMax(0, duration - 1));

    for (HtmlVariableRow& row : m_htmlRows)
    {
        row.inRange = inRange;
        const auto trackIt = entity.tracks.constFind(row.track);
        if (!row.numeric)
        {
            QString text = row.readOnlyText;
            if (trackIt != entity.tracks.constEnd())
            {
                if (const auto value =
                        model::evaluateTrack(trackIt.value(), sampleFrame, false))
                {
                    text = QString::number(*value, 'g', 8);
                }
            }
            row.readOnlyValue->setText(text);
            continue;
        }

        double value = row.defaultValue;
        if (trackIt != entity.tracks.constEnd())
        {
            if (const auto evaluated =
                    model::evaluateTrack(trackIt.value(), sampleFrame, false))
            {
                value = *evaluated;
            }
        }

        const bool hasKey = inRange && detail::hasKeyAt(entity, row.track, relativeFrame);
        {
            QSignalBlocker blocker(row.value);
            row.value->setValue(value);
        }
        row.diamond->setText(hasKey ? QStringLiteral("◆") : QStringLiteral("◇"));
        if (!inRange)
        {
            row.diamond->setToolTip(
                QStringLiteral("播放头不在实体范围内（相对帧 %1），不能打关键帧")
                    .arg(relativeFrame));
        }
        else if (hasKey)
        {
            row.diamond->setToolTip(
                QStringLiteral("删除当前帧（%1）的「%2」关键帧")
                    .arg(relativeFrame)
                    .arg(row.name));
        }
        else
        {
            row.diamond->setToolTip(
                QStringLiteral("在当前帧（%1）给「%2」打关键帧，初值为 manifest 默认 %3")
                    .arg(relativeFrame)
                    .arg(row.name)
                    .arg(row.defaultValue));
        }
    }

    applyKeyframeEnabledState();
}

void PropertiesPanel::rebuildHtmlVariableRows()
{
    // Clear every row below the header; widgets are owned by the grid.
    for (int row = m_htmlGrid->rowCount() - 1; row >= 1; --row)
    {
        for (int column = 0; column < m_htmlGrid->columnCount(); ++column)
        {
            QLayoutItem* item = m_htmlGrid->itemAtPosition(row, column);
            if (!item)
                continue;
            m_htmlGrid->removeItem(item);
            delete item->widget();
            delete item;
        }
    }
    m_htmlRows.clear();

    int gridRow = 1;
    for (auto it = m_htmlVariableDefs.constBegin();
         it != m_htmlVariableDefs.constEnd(); ++it)
    {
        const HtmlVarSpec spec = htmlVarSpec(it.key(), it.value());
        HtmlVariableRow row;
        row.name = spec.name;
        row.track = spec.track;
        row.numeric = spec.numeric;
        row.defaultValue = spec.defaultValue;
        row.minimum = spec.minimum;
        row.maximum = spec.maximum;
        row.step = spec.step;
        row.decimals = spec.decimals;
        row.readOnlyText = spec.text;

        auto* nameLabel = new QLabel(spec.name, m_htmlGroup);
        nameLabel->setObjectName(QStringLiteral("htmlVarName_%1").arg(spec.track));
        nameLabel->setToolTip(spec.track);
        m_htmlGrid->addWidget(nameLabel, gridRow, 0);

        if (spec.numeric)
        {
            row.diamond = new QPushButton(QStringLiteral("◇"), m_htmlGroup);
            row.diamond->setObjectName(QStringLiteral("htmlVarDiamond_%1").arg(spec.track));
            row.diamond->setFixedWidth(28);
            m_htmlGrid->addWidget(row.diamond, gridRow, 1);

            row.value = new QDoubleSpinBox(m_htmlGroup);
            row.value->setObjectName(QStringLiteral("htmlVarValue_%1").arg(spec.track));
            row.value->setRange(spec.minimum, spec.maximum);
            row.value->setDecimals(spec.decimals);
            row.value->setSingleStep(spec.step);
            row.value->setKeyboardTracking(false);
            row.value->setToolTip(
                QStringLiteral("manifest 默认值 %1；修改后按 Auto-key 规则写入 %2 轨道。")
                    .arg(spec.defaultValue)
                    .arg(spec.track));
            m_htmlGrid->addWidget(row.value, gridRow, 2);

            const QString track = spec.track;
            const double fallback = spec.defaultValue;
            connect(row.diamond, &QPushButton::clicked, this, [this, track, fallback] {
                toggleKeyframe(track, fallback);
            });
            connect(row.value, &QDoubleSpinBox::editingFinished, this,
                    [this, track, spin = row.value] {
                        if (m_loading)
                            return;
                        const model::Entity* entity = selectedEntity();
                        if (!entity || !m_document)
                            return;
                        const int relativeFrame =
                            m_document->playhead() - entity->start;
                        if (relativeFrame < 0
                            || relativeFrame >= entity->duration())
                        {
                            setStatus(QStringLiteral(
                                "播放头不在实体范围内，无法修改变量"));
                            refresh();
                            return;
                        }
                        if (!m_autoKey && !detail::hasKeyAt(*entity, track, relativeFrame))
                        {
                            setStatus(QStringLiteral(
                                "HTML 变量没有静态值：先点菱形打关键帧，或开启 Auto-key"));
                            refresh();
                            return;
                        }
                        editKeyframeValue(track, spin->value());
                    });
        }
        else
        {
            auto* placeholder = new QLabel(QStringLiteral("—"), m_htmlGroup);
            placeholder->setObjectName(
                QStringLiteral("htmlVarDiamondPlaceholder_%1").arg(spec.track));
            placeholder->setAlignment(Qt::AlignCenter);
            m_htmlGrid->addWidget(placeholder, gridRow, 1);
            // The placeholder only exists for column alignment; not tracked.

            row.readOnlyValue = new QLabel(spec.text, m_htmlGroup);
            row.readOnlyValue->setObjectName(
                QStringLiteral("htmlVarReadOnly_%1").arg(spec.track));
            row.readOnlyValue->setToolTip(
                QStringLiteral("布尔/字符串变量第一版只读；数值变量才能打关键帧。"));
            m_htmlGrid->addWidget(row.readOnlyValue, gridRow, 2);
        }

        m_htmlRows.append(row);
        ++gridRow;
    }
}

} // namespace pvf::next
