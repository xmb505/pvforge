#include "PropertiesPanel.h"

#include "CollapsibleSection.h"
#include "PropertiesPanelInternal.h"
#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "model/Easing.h"
#include "model/Entity.h"
#include "ui/BezierEditorDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf {

using namespace ui::detail;

void PropertiesPanel::sessionSetTrackValue(const QString& track, double value)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const bool camera = entity->content.kind == ContentKind::Camera;
    const bool cameraScale =
        camera && (track == QLatin1String("scaleX")
                   || track == QLatin1String("scaleY"));
    double stored = value;
    if (cameraScale)
        stored = std::clamp(std::abs(value), 0.05, 1.0);

    runSessionEdit(QStringLiteral("pvf: edit %1").arg(track),
                   [this, track, stored, cameraScale](model::Entity& e) {
                       if (cameraScale)
                       {
                           e.transform.scaleX = stored;
                           e.transform.scaleY = stored;
                       }
                       else if (!setStaticTrackValue(e, track, stored))
                       {
                           return false;
                       }

                       if (m_autoKey)
                       {
                           const int relative =
                               m_session->playhead() - e.start;
                           if (relative >= 0 && relative < e.duration()
                               && !trackBlockedAt(e, track, relative))
                           {
                               setKeyframeValue(e, track, relative, stored);
                               if (cameraScale)
                                   setKeyframeValue(e, QStringLiteral("scaleY"),
                                                    relative, stored);
                           }
                       }
                       return true;
                   });
}

void PropertiesPanel::sessionSetKeyValue(const QString& track, double value)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int relative = sessionRelativeFrame(*entity);
    if (relative < 0 || relative >= entity->duration())
        return;
    if (trackBlockedAt(*entity, track, relative))
    {
        reportSessionError(QStringLiteral("轨道 %1 的第 %2 帧由求解段驱动，"
                                          "不能写入普通关键帧")
                               .arg(track)
                               .arg(relative));
        return;
    }

    const bool cameraScale =
        entity->content.kind == ContentKind::Camera
        && (track == QLatin1String("scaleX")
            || track == QLatin1String("scaleY"));
    const double stored =
        cameraScale ? std::clamp(std::abs(value), 0.05, 1.0) : value;

    runSessionEdit(QStringLiteral("pvf: edit keyframe %1").arg(track),
                   [this, track, relative, stored, cameraScale](model::Entity& e) {
                       if (cameraScale)
                       {
                           e.transform.scaleX = stored;
                           e.transform.scaleY = stored;
                       }
                       else if (!setStaticTrackValue(e, track, stored))
                       {
                           return false;
                       }
                       setKeyframeValue(e, track, relative, stored);
                       if (cameraScale)
                           setKeyframeValue(e, QStringLiteral("scaleY"), relative,
                                            stored);
                       return true;
                   });
}

void PropertiesPanel::sessionToggleKey(const QString& track)
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
    if (trackBlockedAt(*entity, track, relative))
    {
        reportSessionError(QStringLiteral("轨道 %1 的第 %2 帧由求解段驱动，"
                                          "不能写入普通关键帧")
                               .arg(track)
                               .arg(relative));
        return;
    }

    m_editing = true;
    QString error;
    bool ok = false;
    if (hasKeyAt(*entity, track, relative))
    {
        ok = m_session->editActions()->deleteKeyframes(
            {KeyRef{m_sessionUuid, track, relative}}, &error);
    }
    else
    {
        ok = m_session->editActions()->addKeyframe(m_sessionUuid, track,
                                                   relative, &error);
    }
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
}

void PropertiesPanel::sessionAddTransformKeys()
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
    const bool camera = entity->content.kind == ContentKind::Camera;
    QStringList tracks;
    for (const Row& row : m_rows)
    {
        if (camera && (row.prop == QLatin1String("scaleY")
                       || row.prop == QLatin1String("opacity")))
            continue;
        tracks.append(row.prop);
    }

    runSessionEdit(QStringLiteral("pvf: key transform"),
                   [this, tracks, relative, camera](model::Entity& e) {
                       bool changed = false;
                       for (const QString& track : tracks)
                       {
                           if (trackBlockedAt(e, track, relative))
                               continue;
                           double value =
                               currentTrackValue(e, track, relative,
                                                 isStepTrack(track));
                           if (camera && track == QLatin1String("scaleX"))
                               value = std::clamp(std::abs(value), 0.05, 1.0);
                           setKeyframeValue(e, track, relative, value);
                           changed = true;
                           if (camera && track == QLatin1String("scaleX"))
                               setKeyframeValue(e, QStringLiteral("scaleY"),
                                                relative, value);
                       }
                       return changed;
                   });
}

void PropertiesPanel::sessionSetGroupEasing(const model::EasingSpec& easing)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;
    const int relative = sessionRelativeFrame(*entity);
    if (relative < 0 || relative >= entity->duration()
        || !hasAnyKeyAt(*entity, relative))
        return;

    // An empty track asks EditActions for the merged keyframe group: every
    // track key at this frame gets the same easing in one commit.
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->setKeyframeEasing(
        {KeyRef{m_sessionUuid, QString(), relative}}, easing, &error);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
}

void PropertiesPanel::sessionPromptGroupBezier()
{
    const model::Entity* entity = sessionEntity();
    if (!entity)
        return;
    const int relative = sessionRelativeFrame(*entity);
    model::EasingSpec current;
    bool mixed = false;
    if (!frameEasing(*entity, relative, &current, &mixed) || mixed)
        current = model::EasingSpec();

    EasingSpec legacy;
    legacy.easing = easingFromName(model::canonicalEasingName(current.type));
    legacy.inX = current.inX;
    legacy.inY = current.inY;
    legacy.outX = current.outX;
    legacy.outY = current.outY;
    if (legacy.easing != Easing::Bezier)
    {
        legacy.easing = Easing::Bezier;
        legacy.inX = 0.42;
        legacy.inY = 0.0;
        legacy.outX = 0.58;
        legacy.outY = 1.0;
    }

    BezierEditorDialog dialog(legacy, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        refresh();
        return;
    }
    const EasingSpec edited = dialog.easing();
    model::EasingSpec spec;
    spec.type = easingName(edited.easing);
    spec.inX = edited.inX;
    spec.inY = edited.inY;
    spec.outX = edited.outX;
    spec.outY = edited.outY;
    sessionSetGroupEasing(spec);
}

void PropertiesPanel::rebuildTrackRows(const model::Entity& entity)
{
    for (TrackRow& row : m_trackRows)
    {
        QWidget* container = row.diamond ? row.diamond->parentWidget() : nullptr;
        if (!container)
            container = row.name;
        if (!container)
            continue;
        for (QWidget* child : container->findChildren<QWidget*>())
            child->setObjectName(QString());
        container->setObjectName(QString());
        container->hide();
        container->deleteLater();
    }
    m_trackRows.clear();
    m_trackRowsEntity = entity.uuid;
    m_trackRowsKind = entity.content.kind;

    const QVector<TrackSpec> specs = trackSpecsFor(entity);
    for (const TrackSpec& spec : specs)
    {
        TrackRow row;
        row.track = spec.name;
        row.label = spec.label;
        row.step = spec.step;
        row.minimum = spec.minimum;
        row.maximum = spec.maximum;
        row.singleStep = spec.singleStep;
        row.decimals = spec.decimals;
        row.suffix = spec.suffix;

        auto* rowWidget = new QWidget(m_trackHost);
        auto* rowLayout = new QVBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);

        auto* topLine = new QHBoxLayout();
        topLine->setContentsMargins(0, 0, 0, 0);
        topLine->setSpacing(3);
        auto* labelWidget = new QLabel(spec.label, rowWidget);
        labelWidget->setObjectName(QStringLiteral("trackLabel_") + spec.name);
        topLine->addWidget(labelWidget);
        topLine->addStretch(1);

        auto* diamond = new QToolButton(rowWidget);
        diamond->setObjectName(QStringLiteral("trackDiamond_") + spec.name);
        diamond->setFixedSize(18, 18);
        diamond->setAutoRaise(true);
        diamond->setText(QStringLiteral("◇"));
        diamond->setCursor(Qt::PointingHandCursor);
        topLine->addWidget(diamond);

        auto* value = new QDoubleSpinBox(rowWidget);
        value->setObjectName(QStringLiteral("trackValue_") + spec.name);
        value->setRange(spec.minimum, spec.maximum);
        value->setDecimals(spec.decimals);
        value->setSingleStep(spec.singleStep);
        if (!spec.suffix.isEmpty())
            value->setSuffix(spec.suffix);
        value->setKeyboardTracking(false);
        topLine->addWidget(value, 1);
        rowLayout->addLayout(topLine);

        m_trackForm->addRow(rowWidget);

        const QString track = spec.name;
        connect(diamond, &QToolButton::clicked, this,
                [this, track] { sessionToggleKey(track); });
        connect(value, &QDoubleSpinBox::editingFinished, this,
                [this, track, value] {
                    if (!m_editing)
                        sessionSetKeyValue(track, value->value());
                });

        row.name = labelWidget;
        row.diamond = diamond;
        row.value = value;
        m_trackRows.append(row);
    }
}

void PropertiesPanel::reloadTrackRows(const model::Entity& entity)
{
    const QVector<TrackSpec> specs = trackSpecsFor(entity);
    bool sameStructure = entity.uuid == m_trackRowsEntity
        && entity.content.kind == m_trackRowsKind
        && m_trackRows.size() == specs.size();
    for (int i = 0; sameStructure && i < specs.size(); ++i)
    {
        sameStructure = m_trackRows.at(i).track == specs.at(i).name
            && m_trackRows.at(i).step == specs.at(i).step;
    }
    if (!sameStructure)
        rebuildTrackRows(entity);

    const int relative = sessionRelativeFrame(entity);
    const bool inRange = relative >= 0 && relative < entity.duration();
    const bool editable = sessionEditable();
    for (int i = 0; i < m_trackRows.size() && i < specs.size(); ++i)
    {
        TrackRow& row = m_trackRows[i];
        const TrackSpec& spec = specs.at(i);
        const bool hasKey = inRange && hasKeyAt(entity, row.track, relative);
        const bool blocked =
            inRange && trackBlockedAt(entity, row.track, relative);

        row.diamond->setText(hasKey ? QStringLiteral("◆")
                                    : QStringLiteral("◇"));
        row.diamond->setEnabled(editable && inRange && !blocked);
        if (!inRange)
            row.diamond->setToolTip(
                QStringLiteral("播放头不在实体范围内：相对帧 %1，允许 0..%2")
                    .arg(relative)
                    .arg(entity.duration() - 1));
        else if (blocked)
            row.diamond->setToolTip(
                QStringLiteral("该帧由求解段驱动，不能写入普通关键帧"));
        else
            row.diamond->setToolTip(
                hasKey ? QStringLiteral("删除当前帧（%1）的「%2」关键帧")
                             .arg(relative)
                             .arg(spec.label)
                       : QStringLiteral("在当前帧（%1）插入「%2」关键帧")
                             .arg(relative)
                             .arg(spec.label));

        setSpinIfIdle(row.value, currentTrackValue(entity, row.track, relative,
                                                   spec.step));
        row.value->setEnabled(editable && inRange && !blocked);
    }

    if (m_groupEasing)
    {
        const bool hasGroup = inRange && hasAnyKeyAt(entity, relative);
        const int previous =
            inRange ? previousGroupKeyframeFrame(entity, relative) : -1;
        model::EasingSpec current;
        bool mixed = false;
        if (hasGroup)
            frameEasing(entity, relative, &current, &mixed);
        const QSignalBlocker blocker(m_groupEasing);
        if (hasGroup && previous >= 0 && !mixed)
        {
            const QString type = model::canonicalEasingName(current.type);
            int index = m_groupEasing->findData(type);
            if (index < 0)
            {
                m_groupEasing->addItem(type, type);
                index = m_groupEasing->count() - 1;
            }
            m_groupEasing->setCurrentIndex(index);
        }
        else
        {
            m_groupEasing->setCurrentIndex(-1);
        }
        m_groupEasing->setPlaceholderText(
            !hasGroup
                ? QStringLiteral("当前帧无关键帧")
                : previous < 0 ? QStringLiteral("首帧无进入段")
                               : mixed     ? QStringLiteral("混合")
                                           : QStringLiteral("选择缓动"));
        m_groupEasing->setEnabled(editable && hasGroup && previous >= 0);
        m_groupEasing->setToolTip(
            !hasGroup
                ? QStringLiteral("当前帧没有可编辑的关键帧")
                : previous < 0
                    ? QStringLiteral("首帧没有进入段；请先在前一帧打关键帧")
                    : QStringLiteral("修改当前帧所有轨道关键帧的进入段曲线；"
                                     "自定义贝塞尔仍可选"));
    }
}

} // namespace pvf
