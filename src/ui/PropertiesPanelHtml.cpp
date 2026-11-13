#include "PropertiesPanel.h"

#include "CollapsibleSection.h"
#include "PropertiesPanelInternal.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "model/Content.h"
#include "model/Entity.h"
#include "next/properties/SpecialContentEditor.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSet>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf {

using namespace ui::detail;

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
// The asset store keeps the manifest's variables object verbatim.
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

void PropertiesPanel::buildSpecialSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(10, 8, 10, 8);
    box->setSpacing(6);

    m_specialEditor = new next::SpecialContentEditor(content);
    m_specialEditor->setObjectName(QStringLiteral("specialContentPage"));
    // Keep the stacked pages at their natural height instead of letting the
    // multi-line editors eat the whole scroll column.
    m_specialEditor->setSizePolicy(QSizePolicy::Preferred,
                                   QSizePolicy::Maximum);
    box->addWidget(m_specialEditor);
    connect(m_specialEditor, &next::SpecialContentEditor::edited, this,
            &PropertiesPanel::commitSpecialContentEdit);

    m_specialSection =
        new CollapsibleSection(QStringLiteral("特殊内容"), content, this);
    m_specialSection->setObjectName(QStringLiteral("specialContentSection"));
    m_specialSection->setExpanded(true);
    layout->insertWidget(layout->count() - 1, m_specialSection);
    m_specialSection->hide();
}

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
    m_sessionHtmlInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_sessionHtmlInfo->setMinimumWidth(0);
    box->addWidget(m_sessionHtmlInfo);

    m_sessionHtmlEmpty = new QLabel(QStringLiteral("（manifest 未声明变量）"), content);
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
    m_sessionHtmlWarning->setStyleSheet(QStringLiteral("color:#e0a050;"));
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
        nameLabel->setObjectName(QStringLiteral("sessionHtmlVarName_%1")
                                    .arg(spec.track));
        nameLabel->setToolTip(
            QStringLiteral("%1 · 默认值 %2")
                .arg(spec.track)
                .arg(spec.numeric ? QString::number(spec.defaultValue, 'g', 8)
                                  : spec.text));
        rowLayout->addWidget(nameLabel, 1);

        SessionHtmlRow row;
        row.name = spec.name;
        row.track = spec.track;
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
            spin->setObjectName(QStringLiteral("sessionHtmlVarValue_%1")
                                    .arg(spec.track));
            spin->setRange(spec.minimum, spec.maximum);
            spin->setDecimals(spec.decimals);
            spin->setSingleStep(spec.step);
            spin->setKeyboardTracking(false);
            spin->setToolTip(
                QStringLiteral("manifest 默认值 %1；修改后按自动关键帧规则写入 %2")
                    .arg(spec.defaultValue)
                    .arg(spec.track));
            rowLayout->addWidget(spin, 1);
            row.spin = spin;

            auto* key = new QToolButton(rowWidget);
            key->setObjectName(QStringLiteral("sessionHtmlVarDiamond_%1")
                                   .arg(spec.track));
            key->setFixedSize(18, 18);
            key->setAutoRaise(true);
            key->setText(QStringLiteral("◇"));
            key->setCursor(Qt::PointingHandCursor);
            rowLayout->addWidget(key);
            row.key = key;

            const QString track = spec.track;
            connect(key, &QToolButton::clicked, this, [this, track, spin] {
                toggleSessionHtmlKey(track, spin->value());
            });
            connect(spin, &QDoubleSpinBox::editingFinished, this,
                    [this, track, spin] {
                        editSessionHtmlVariable(track, spin->value());
                    });
        }
        else
        {
            auto* value = new QLabel(spec.text, rowWidget);
            value->setObjectName(QStringLiteral("sessionHtmlVarValue_%1")
                                     .arg(spec.track));
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            value->setToolTip(QStringLiteral("%1（只读）").arg(spec.track));
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
            m_sessionHtmlTimeDependent = stored->metadata
                                             .value(QStringLiteral("timeDependent"))
                                             .toBool(true);
            variables = stored->metadata.value(QStringLiteral("variables"))
                            .toObject();
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
            QString info = name.isEmpty() ? QStringLiteral("HTML")
                                          : name;
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
        declared.insert(row.track);
    QStringList orphans;
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
    {
        if (it.key().startsWith(QStringLiteral("var."))
            && !declared.contains(it.key()) && !it->isEmpty())
            orphans << it.key().mid(4);
    }
    if (!orphans.isEmpty())
        warnings << QStringLiteral("未使用的变量轨：%1")
                        .arg(orphans.join(QStringLiteral(", ")));
    m_sessionHtmlWarning->setText(warnings.join(QStringLiteral("\n")));
    m_sessionHtmlWarning->setVisible(!warnings.isEmpty());

    reloadSessionHtmlValues(entity);
}

void PropertiesPanel::reloadSessionHtmlValues(const model::Entity& entity)
{
    const int relative = sessionRelativeFrame(entity);
    const bool inRange = relative >= 0 && relative < entity.duration();
    const int sampleFrame = std::clamp(relative, 0, std::max(0, entity.duration() - 1));
    const bool editable = sessionEditable();
    for (SessionHtmlRow& row : m_sessionHtmlRows)
    {
        if (!row.numeric)
        {
            QString text = row.readOnlyText;
            const auto trackIt = entity.tracks.constFind(row.track);
            if (trackIt != entity.tracks.constEnd())
            {
                if (const auto value = model::evaluateTrack(trackIt.value(),
                                                            sampleFrame, false))
                    text = QString::number(*value, 'g', 8);
            }
            row.readOnlyValue->setText(text);
            continue;
        }

        double value = row.defaultValue;
        const auto trackIt = entity.tracks.constFind(row.track);
        if (trackIt != entity.tracks.constEnd())
        {
            if (const auto evaluated = model::evaluateTrack(trackIt.value(),
                                                            sampleFrame, false))
                value = *evaluated;
        }
        setSpinIfIdle(row.spin, value);

        const bool hasKey = inRange && hasKeyAt(entity, row.track, relative);
        row.key->setText(hasKey ? QStringLiteral("◆") : QStringLiteral("◇"));
        row.key->setToolTip(
            hasKey ? QStringLiteral("删除当前帧的「%1」关键帧").arg(row.name)
                   : QStringLiteral("在当前帧给「%1」打关键帧，初值为 %2")
                         .arg(row.name)
                         .arg(row.spin->value()));
        row.key->setEnabled(editable && inRange);
        row.spin->setEnabled(editable && inRange);
    }
}

void PropertiesPanel::editSessionHtmlVariable(const QString& track, double value)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int relative = sessionRelativeFrame(*entity);
    if (relative < 0 || relative >= entity->duration())
    {
        reportSessionError(QStringLiteral("播放头不在实体范围内，无法修改变量"));
        refresh();
        return;
    }
    if (!m_autoKey && !hasKeyAt(*entity, track, relative))
    {
        reportSessionError(
            QStringLiteral("HTML 变量没有静态值：先点菱形打关键帧，或开启自动关键帧"));
        refresh();
        return;
    }
    runSessionEdit(QStringLiteral("pvf: edit HTML variable"),
                   [track, relative, value](model::Entity& e) {
                       setKeyframeValue(e, track, relative, value);
                       return true;
                   });
}

void PropertiesPanel::toggleSessionHtmlKey(const QString& track, double fallback)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int relative = sessionRelativeFrame(*entity);
    if (relative < 0 || relative >= entity->duration())
    {
        reportSessionError(QStringLiteral("播放头不在实体范围内：相对帧 %1，允许 0..%2")
                               .arg(relative)
                               .arg(entity->duration() - 1));
        return;
    }
    if (hasKeyAt(*entity, track, relative))
    {
        runSessionEdit(QStringLiteral("pvf: delete HTML variable key"),
                       [track, relative](model::Entity& e) {
                           removeKeyframeAt(e, track, relative);
                           return true;
                       });
        return;
    }
    runSessionEdit(QStringLiteral("pvf: add HTML variable key"),
                   [track, relative, fallback](model::Entity& e) {
                       setKeyframeValue(e, track, relative, fallback);
                       return true;
                   });
}

void PropertiesPanel::commitSpecialContentEdit()
{
    const model::Entity* entity = sessionEntity();
    if (!m_session || m_editing || !m_specialEditor || !entity
        || !next::SpecialContentEditor::handles(entity->content.kind))
        return;
    if (m_specialEditor->kind() != entity->content.kind)
        return;

    model::Content content = entity->content;
    m_specialEditor->applyTo(content);

    // The model accepts an empty brand-wall label list (it draws empty tiles),
    // but saving an editor page with no label is meaningless.
    if (content.kind == ContentKind::BrandWall
        && content.brandWall.logos.isEmpty())
    {
        reportSessionError(QStringLiteral("未修改：品牌墙至少需要一个标签"));
        refresh();
        return;
    }

    runSessionContentEdit(
        QStringLiteral("pvf: edit %1 content")
            .arg(contentKindName(content.kind)),
        [content](model::Content& updated) {
            if (updated.kind != content.kind)
                return false;
            updated = content;
            return true;
        });
}

} // namespace pvf
