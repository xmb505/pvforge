#pragma once

// The private-timeline popup that floats above a clip.  It is a friend of
// TimelinePanel, so its inline code may touch the panel's private state.

#include "ui/TimelinePanel.h"
#include "ui/TimelinePanelInternal.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace pvf {

class ClipPopup : public QWidget
{
public:
    ClipPopup(EntityId ownerId, TimelinePanel* source, TimelineAdapter* adapter)
        : ClipPopup(adapter, ownerId, source)
    {
    }

private:
    ClipPopup(TimelineAdapter* adapter, EntityId ownerId, TimelinePanel* source)
        : QWidget(source->window())
        , m_ownerId(ownerId)
        , m_source(source)
    {
        setObjectName(QStringLiteral("clipPopup"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral(
            "QWidget#clipPopup { background: #1e1f24; border: 1px solid #4a4d55;"
            " border-radius: 6px; }"
            "QWidget#clipPopupHeader { background: #2a2c33; border: none;"
            " border-bottom: 1px solid #3a3d45; border-top-left-radius: 5px;"
            " border-top-right-radius: 5px; }"
            "QWidget#clipPopup QLabel { background: transparent; color: #d8dce4; }"
            "QWidget#clipPopup QToolButton { background: #32353d; color: #e8eaf0;"
            " border: 1px solid #4a4d55; border-radius: 3px; padding: 1px 7px; }"
            "QWidget#clipPopup QToolButton:hover { background: #3c404a; }"));
        setMinimumWidth(160);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* header = new QWidget(this);
        header->setObjectName(QStringLiteral("clipPopupHeader"));
        header->setMinimumHeight(28);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(8, 3, 4, 3);
        headerLayout->setSpacing(4);
        m_title = new QLabel(header);
        QFont titleFont = m_title->font();
        titleFont.setBold(true);
        m_title->setFont(titleFont);
        headerLayout->addWidget(m_title, 1);
        m_timeMode = new QToolButton(header);
        m_timeMode->setCheckable(true);
        m_timeMode->setText(QStringLiteral("相对"));
        m_timeMode->setToolTip(QStringLiteral("切换相对 / 绝对时间"));
        headerLayout->addWidget(m_timeMode);
        auto* close = new QToolButton(header);
        const QPixmap closeGlyph =
            ui::detail::themedGlyph({QStringLiteral("window-close"),
                                     QStringLiteral("window-close-symbolic")},
                                    16, QColor(232, 234, 240));
        if (!closeGlyph.isNull())
        {
            close->setIcon(QIcon(closeGlyph));
            close->setIconSize(QSize(16, 16));
        }
        else
        {
            close->setText(QStringLiteral("×"));
        }
        close->setToolTip(QStringLiteral("关闭私有时间线"));
        headerLayout->addWidget(close);
        layout->addWidget(header);

        m_panel = adapter ? new TimelinePanel(adapter, this) : nullptr;
        m_panel->m_owner = ownerId;
        m_panel->m_popup = true;
        m_panel->m_sourcePanel = source;
        m_panel->m_relativeTime = true;
        m_panel->m_pixelsPerFrame = source->m_pixelsPerFrame;
        m_panel->assignMissingRows();
        m_panel->setEditMode(source->m_editMode);
        if (m_panel->m_unitButton)
            m_panel->m_unitButton->hide();
        if (m_panel->m_modeButton)
            m_panel->m_modeButton->hide();
        layout->addWidget(m_panel, 1);

        connect(m_timeMode, &QToolButton::toggled, this, [this](bool absolute) {
            m_timeMode->setText(absolute ? QStringLiteral("绝对")
                                         : QStringLiteral("相对"));
            m_panel->m_relativeTime = !absolute;
            m_panel->update();
        });
        connect(close, &QToolButton::clicked, this, [this] {
            m_source->removePopup(m_ownerId);
            deleteLater();
        });

        // The popup is its own TimelinePanel. Forward edit/selection signals
        // up the chain so the main panel's MainWindow connections fire once.
        connect(m_panel, &TimelinePanel::frameRequested, m_source,
                &TimelinePanel::frameRequested);
        connect(m_panel, &TimelinePanel::scrubStarted, m_source,
                &TimelinePanel::scrubStarted);
        connect(m_panel, &TimelinePanel::entitySelected, m_source,
                &TimelinePanel::entitySelected);
        connect(m_panel, &TimelinePanel::deleteKeyframesRequested, m_source,
                &TimelinePanel::deleteKeyframesRequested);
        connect(m_panel, &TimelinePanel::deleteEntityRequested, m_source,
                &TimelinePanel::deleteEntityRequested);
        connect(m_panel, &TimelinePanel::audioMoved, m_source,
                &TimelinePanel::audioMoved);
        connect(m_panel, &TimelinePanel::keyframesMoveRequested, m_source,
                &TimelinePanel::keyframesMoveRequested);
        connect(m_panel, &TimelinePanel::snapshotEditRequested, m_source,
                &TimelinePanel::snapshotEditRequested);
        connect(m_panel, &TimelinePanel::keyframeAddRequested, m_source,
                &TimelinePanel::keyframeAddRequested);
        connect(m_panel, &TimelinePanel::keyframeSelectionChanged, m_source,
                &TimelinePanel::adoptKeyframeSelection);
        connect(m_panel, &TimelinePanel::editCommitted, m_source,
                &TimelinePanel::editCommitted);
        connect(m_panel, &TimelinePanel::editFailed, m_source,
                &TimelinePanel::editFailed);

        syncGeometry();
    }

public:
    void syncGeometry()
    {
        const Entity* entity = m_source->entityById(m_ownerId);
        const QRect clip = m_source->clipRectFor(m_ownerId);
        if (!entity || clip.isEmpty() || m_source->window() == nullptr)
        {
            hide();
            return;
        }

        m_panel->m_pixelsPerFrame = m_source->m_pixelsPerFrame;
        if (m_source->sessionMode())
        {
            m_panel->setReadOnlySnapshot(m_source->m_snapshot);
        }
        else if (m_source->m_dragging)
        {
            m_panel->m_externalPreview = m_source->m_preview;
            m_panel->m_hasExternalPreview = true;
        }
        else
        {
            m_panel->m_hasExternalPreview = false;
            m_panel->m_externalPreview.clear();
        }
        m_title->setText(entity->name + QStringLiteral(" · 私有时间线"));

        QWidget* host = window();
        const int sourceRight =
            m_source->mapTo(host, QPoint(std::max(0, m_source->width()
                                                    - TimelinePanel::kScrollBarSize),
                                         0))
                .x();
        const int left = m_source->mapTo(host, QPoint(std::max(0, clip.left()),
                                                      clip.top()))
                             .x();
        const int right =
            std::min(m_source->mapTo(host, QPoint(clip.right() + 1, 0)).x(),
                     sourceRight);
        const int width = right - left;
        if (width < 90)
        {
            hide();
            return;
        }

        const int rows = std::max(1, int(m_panel->rowIds().size()));
        int height = 30 + TimelinePanel::kRulerHeight
                     + rows * TimelinePanel::kRowHeight
                     + TimelinePanel::kScrollBarSize;
        height = std::clamp(height, 140, 360);
        int y = m_source->mapTo(host, QPoint(0, clip.top())).y() - height - 6;
        if (y < 0)
            y = m_source->mapTo(host, QPoint(0, clip.bottom())).y() + 6;
        y = std::clamp(y, 0, std::max(0, host->height() - height));

        setGeometry(left, y, width, height);
        m_panel->m_pixelsPerFrame = m_source->m_pixelsPerFrame;
        // If the parent clip starts left of the visible main timeline, the
        // popup's left edge is clamped to the viewport. Scroll the popup to
        // the global frame under that edge so rulers, clips and playhead line
        // up with the main timeline instead of restarting at the clip start.
        const int sourceX =
            m_source->mapFrom(host,
                              QPoint(left + m_panel->nameWidth(), clip.top()))
                .x();
        const int sourceFrame = m_source->frameFromX(sourceX);
        const int contentStart = m_panel->contentStart();
        m_panel->m_scrollX = std::max(
            0, int(std::lround((std::max(contentStart, sourceFrame)
                                - contentStart)
                               * m_panel->m_pixelsPerFrame)));
        m_panel->clampScroll();
        show();
        raise();
        m_panel->update();
    }

    TimelinePanel* panel() const { return m_panel; }

private:
    EntityId m_ownerId = kInvalidEntity;
    TimelinePanel* m_source = nullptr;
    TimelinePanel* m_panel = nullptr;
    QLabel* m_title = nullptr;
    QToolButton* m_timeMode = nullptr;
};

} // namespace pvf
