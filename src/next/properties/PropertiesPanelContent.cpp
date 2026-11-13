#include "PropertiesPanel.h"

#include "PropertiesPanelInternal.h"
#include "SpecialContentEditor.h"
#include "TextLayoutDialog.h"
#include "common/Types.h"
#include "next/core/EntityLock.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace pvf::next {

namespace {

struct AlignOption
{
    const char* label;
    int value;
};

const AlignOption kAlignOptions[] = {
    {"左上", Qt::AlignLeft | Qt::AlignTop},
    {"上中", Qt::AlignHCenter | Qt::AlignTop},
    {"右上", Qt::AlignRight | Qt::AlignTop},
    {"左中", Qt::AlignLeft | Qt::AlignVCenter},
    {"居中", Qt::AlignHCenter | Qt::AlignVCenter},
    {"右中", Qt::AlignRight | Qt::AlignVCenter},
    {"左下", Qt::AlignLeft | Qt::AlignBottom},
    {"下中", Qt::AlignHCenter | Qt::AlignBottom},
    {"右下", Qt::AlignRight | Qt::AlignBottom},
};

struct ShapeOption
{
    const char* label;
    int value;
};

// Same shape numbering as the legacy vector editor; the new schema has not
// frozen the enum yet.
const ShapeOption kShapeOptions[] = {
    {"矩形", 0}, {"圆角矩形", 3}, {"椭圆", 1}, {"直线", 2}, {"三角形", 4},
    {"心形", 5}, {"星形", 6}, {"多边形", 7}, {"箭头", 8},
};

void selectComboData(QComboBox* combo, int value)
{
    int index = combo->findData(value);
    if (index < 0)
    {
        combo->addItem(QStringLiteral("自定义 (%1)").arg(value), value);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

void styleColorButton(QPushButton* button, const QColor& color)
{
    if (!color.isValid())
        return;
    const QColor text = color.lightnessF() > 0.55 ? QColor(0, 0, 0) : QColor(255, 255, 255);
    button->setText(color.name(QColor::HexRgb));
    button->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2;"
                       " border: 1px solid #888; border-radius: 0; padding: 2px 6px; }")
            .arg(color.name(QColor::HexRgb), text.name(QColor::HexRgb)));
}

QString assetTypeLabel(AssetType type)
{
    switch (type)
    {
    case AssetType::Image:
        return QStringLiteral("图片");
    case AssetType::Video:
        return QStringLiteral("视频");
    case AssetType::Audio:
        return QStringLiteral("音频");
    case AssetType::Html:
        return QStringLiteral("HTML");
    }
    return QStringLiteral("素材");
}

// docs/AI/ENTITY_MODEL.md 6.2: images, video and HTML swap freely; audio only
// swaps with audio.
bool isVisualAsset(AssetType type)
{
    return type == AssetType::Image || type == AssetType::Video
        || type == AssetType::Html;
}

bool isTimedAsset(AssetType type)
{
    return type == AssetType::Video || type == AssetType::Audio;
}

bool sameAssetGroup(AssetType a, AssetType b)
{
    if (a == AssetType::Audio || b == AssetType::Audio)
        return a == b;
    return isVisualAsset(a) && isVisualAsset(b);
}

QString assetContentPath(const assets::StoredAsset& asset)
{
    const QStringList files = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    return asset.directory + QLatin1Char('/') + files.first();
}

// Decoded thumbnail capped at maxSize on its longest edge. Null for missing or
// non-image assets.
QPixmap assetThumbnail(const assets::StoredAsset& asset, int maxSize)
{
    if (asset.type != AssetType::Image)
        return {};
    const QString path = assetContentPath(asset);
    if (path.isEmpty())
        return {};
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull())
        return {};
    return QPixmap::fromImage(
        image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QString assetInfoText(const assets::StoredAsset& asset)
{
    QStringList parts;
    parts.append(assetTypeLabel(asset.type));
    const int width = asset.metadata.value(QStringLiteral("width")).toInt(0);
    const int height = asset.metadata.value(QStringLiteral("height")).toInt(0);
    if (width > 0 && height > 0)
        parts.append(QStringLiteral("%1×%2").arg(width).arg(height));
    const int frames = asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
    if (frames > 0)
        parts.append(QStringLiteral("%1 帧").arg(frames));
    else
    {
        const double seconds =
            asset.metadata.value(QStringLiteral("durationSeconds")).toDouble(0.0);
        if (seconds > 0.0)
            parts.append(QStringLiteral("%1 秒").arg(seconds, 0, 'f', 2));
    }
    return parts.join(QStringLiteral(" · "));
}

// Mirrors the MCP asset.assign sizing rule (src/mcp2/AssetTools.cpp applyAsset):
// timed assets clamp sourceIn into 0..frames-1 and trim the entity when the
// remaining material is shorter; images and HTML keep the entity duration.
struct AssetReplacementPlan
{
    int sourceIn = 0;
    int duration = 1;
    bool needsTrim = false;
};

AssetReplacementPlan planAssetReplacement(const model::Entity& entity,
                                          const assets::StoredAsset& asset)
{
    AssetReplacementPlan plan;
    plan.duration = entity.duration();

    if (!isTimedAsset(asset.type))
        return plan;

    if (entity.content.kind == ContentKind::Asset
        && isTimedAsset(entity.content.asset.type))
    {
        plan.sourceIn = entity.content.asset.sourceIn;
    }

    const int frames = asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
    if (frames <= 0)
        return plan;

    plan.sourceIn = qBound(0, plan.sourceIn, qMax(0, frames - 1));
    const int available = frames - plan.sourceIn;
    if (entity.duration() > available)
        plan.duration = available;
    plan.needsTrim = plan.duration != entity.duration();
    return plan;
}

void applyAssetReplacement(model::Entity& entity, const assets::StoredAsset& asset,
                           const AssetReplacementPlan& plan)
{
    if (plan.needsTrim)
    {
        entity.end = entity.start + plan.duration;
        // docs/AI/ENTITY_MODEL.md 6.2: trimming end drops keyframes at or past the
        // new duration.
        for (auto it = entity.tracks.begin(); it != entity.tracks.end();)
        {
            QVector<model::Keyframe>& keys = it.value().keys;
            keys.erase(std::remove_if(keys.begin(), keys.end(),
                                      [duration = plan.duration](
                                          const model::Keyframe& keyframe) {
                                          return keyframe.frame >= duration;
                                      }),
                       keys.end());
            if (keys.isEmpty())
                it = entity.tracks.erase(it);
            else
                ++it;
        }

        // docs/AI/ENTITY_MODEL.md 5.4: a trim that cuts into a solver moves its
        // end marker to the new last frame; segments entirely outside are
        // dropped. Legacy solver endFrame must stay inside the clip.
        QVector<model::SolverSegment> solvers;
        for (model::SolverSegment& solver : entity.solvers)
        {
            if (solver.startFrame >= plan.duration)
                continue;
            if (solver.endFrame != -1 && solver.endFrame >= plan.duration)
                solver.endFrame = plan.duration - 1;
            solvers.append(solver);
        }
        entity.solvers = solvers;
    }

    entity.content.kind = ContentKind::Asset;
    entity.content.asset.hash = asset.hash;
    entity.content.asset.type = asset.type;
    entity.content.asset.sourceIn = plan.sourceIn;
}

// QListWidget that forwards hovered items so the asset picker can show a
// thumbnail preview without selecting the row.
class HoverListWidget : public QListWidget
{
public:
    explicit HoverListWidget(QWidget* parent = nullptr)
        : QListWidget(parent)
    {
    }

    std::function<void(QListWidgetItem*)> hovered;

protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        QListWidget::mouseMoveEvent(event);
        QListWidgetItem* item = itemAt(event->position().toPoint());
        if (item == m_hovered)
            return;
        m_hovered = item;
        if (hovered)
            hovered(item);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hovered = nullptr;
        QListWidget::leaveEvent(event);
    }

private:
    QListWidgetItem* m_hovered = nullptr;
};

} // namespace

void PropertiesPanel::buildContentSection()
{
    auto* group = new QGroupBox(QStringLiteral("内容"), m_content);
    m_contentGroup = group;
    auto* outer = new QVBoxLayout(group);
    outer->setContentsMargins(10, 8, 10, 8);
    outer->setSpacing(6);
    m_contentLayout->addWidget(group);

    auto addPage = [this, outer](const QString& objectName) {
        auto* page = new QWidget(m_contentGroup);
        page->setObjectName(objectName);
        auto* form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);
        form->setVerticalSpacing(6);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        outer->addWidget(page);
        page->setVisible(false);
        return form;
    };

    // empty
    QFormLayout* emptyForm = addPage(QStringLiteral("emptyPage"));
    m_emptyPage = emptyForm->parentWidget();
    auto* emptyLabel = new QLabel(
        QStringLiteral("空实体没有内容；可以当父级，也可以之后挂素材或内联内容。"),
        m_emptyPage);
    emptyLabel->setWordWrap(true);
    emptyForm->addRow(emptyLabel);

    // text
    QFormLayout* textForm = addPage(QStringLiteral("textPage"));
    m_textPage = textForm->parentWidget();

    m_textEdit = new QLineEdit(m_textPage);
    m_textEdit->setObjectName(QStringLiteral("textContentEdit"));
    textForm->addRow(QStringLiteral("文本"), m_textEdit);
    addEditable(m_textEdit);
    connect(m_textEdit, &QLineEdit::editingFinished, this, [this] {
        const QString text = m_textEdit->text();
        commitEdit(QStringLiteral("content.text.text"), [text](model::Entity& entity) {
            entity.content.text.text = text;
            return true;
        });
    });

    m_textFont = new QFontComboBox(m_textPage);
    m_textFont->setObjectName(QStringLiteral("textFontCombo"));
    // Only a dropdown pick changes the font, so one pick is one commit.
    m_textFont->setEditable(false);
    textForm->addRow(QStringLiteral("字体"), m_textFont);
    addEditable(m_textFont);
    connect(m_textFont, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
        const QString family = font.family();
        commitEdit(QStringLiteral("content.text.fontFamily"), [family](model::Entity& entity) {
            entity.content.text.fontFamily = family;
            return true;
        });
    });

    m_textPixelSize = new QSpinBox(m_textPage);
    m_textPixelSize->setObjectName(QStringLiteral("textPixelSizeSpin"));
    m_textPixelSize->setRange(1, 100000);
    m_textPixelSize->setSuffix(QStringLiteral(" px"));
    m_textPixelSize->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("字号"), m_textPixelSize);
    addEditable(m_textPixelSize);
    connect(m_textPixelSize, &QSpinBox::editingFinished, this, [this] {
        const int value = m_textPixelSize->value();
        commitEdit(QStringLiteral("content.text.pixelSize"), [this, value](model::Entity& entity) {
            entity.content.text.pixelSize = value;
            applyAutoKey(entity, QStringLiteral("fontSize"), value);
            return true;
        });
    });

    m_textColorButton = new QPushButton(m_textPage);
    m_textColorButton->setObjectName(QStringLiteral("textColorButton"));
    textForm->addRow(QStringLiteral("颜色"), m_textColorButton);
    addEditable(m_textColorButton);
    connect(m_textColorButton, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(m_textColorValue, this, QStringLiteral("文字颜色"));
        if (!chosen.isValid())
            return;
        const QString color = chosen.name(QColor::HexRgb);
        commitEdit(QStringLiteral("content.text.color"), [color](model::Entity& entity) {
            entity.content.text.color = color;
            return true;
        });
    });

    m_textAlign = new QComboBox(m_textPage);
    m_textAlign->setObjectName(QStringLiteral("textAlignCombo"));
    for (const AlignOption& option : kAlignOptions)
        m_textAlign->addItem(QString::fromUtf8(option.label), option.value);
    textForm->addRow(QStringLiteral("对齐"), m_textAlign);
    addEditable(m_textAlign);
    connect(m_textAlign, &QComboBox::currentIndexChanged, this, [this](int) {
        const int value = m_textAlign->currentData().toInt();
        commitEdit(QStringLiteral("content.text.align"), [value](model::Entity& entity) {
            entity.content.text.align = value;
            return true;
        });
    });

    m_textLineHeight = new QDoubleSpinBox(m_textPage);
    m_textLineHeight->setObjectName(QStringLiteral("textLineHeightSpin"));
    m_textLineHeight->setRange(0.01, 100.0);
    m_textLineHeight->setDecimals(3);
    m_textLineHeight->setSingleStep(0.1);
    m_textLineHeight->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("行高"), m_textLineHeight);
    addEditable(m_textLineHeight);
    connect(m_textLineHeight, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_textLineHeight->value();
        commitEdit(QStringLiteral("content.text.lineHeight"), [this, value](model::Entity& entity) {
            entity.content.text.lineHeight = value;
            applyAutoKey(entity, QStringLiteral("lineHeight"), value);
            return true;
        });
    });

    m_textLetterSpacing = new QDoubleSpinBox(m_textPage);
    m_textLetterSpacing->setObjectName(QStringLiteral("textLetterSpacingSpin"));
    m_textLetterSpacing->setRange(-10000.0, 10000.0);
    m_textLetterSpacing->setDecimals(2);
    m_textLetterSpacing->setSingleStep(1.0);
    m_textLetterSpacing->setSuffix(QStringLiteral(" px"));
    m_textLetterSpacing->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("字距"), m_textLetterSpacing);
    addEditable(m_textLetterSpacing);
    connect(m_textLetterSpacing, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_textLetterSpacing->value();
        commitEdit(QStringLiteral("content.text.letterSpacing"), [this, value](model::Entity& entity) {
            entity.content.text.letterSpacing = value;
            applyAutoKey(entity, QStringLiteral("letterSpacing"), value);
            return true;
        });
    });

    m_textStrokeWidth = new QDoubleSpinBox(m_textPage);
    m_textStrokeWidth->setObjectName(QStringLiteral("textStrokeWidthSpin"));
    m_textStrokeWidth->setRange(0.0, 10000.0);
    m_textStrokeWidth->setDecimals(2);
    m_textStrokeWidth->setSingleStep(0.5);
    m_textStrokeWidth->setSuffix(QStringLiteral(" px"));
    m_textStrokeWidth->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("描边宽度"), m_textStrokeWidth);
    addEditable(m_textStrokeWidth);
    connect(m_textStrokeWidth, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_textStrokeWidth->value();
        commitEdit(QStringLiteral("content.text.strokeWidth"), [this, value](model::Entity& entity) {
            entity.content.text.strokeWidth = value;
            applyAutoKey(entity, QStringLiteral("strokeWidth"), value);
            return true;
        });
    });

    m_textLayoutButton = new QPushButton(QStringLiteral("排版…"), m_textPage);
    m_textLayoutButton->setObjectName(QStringLiteral("textLayoutButton"));
    m_textLayoutButton->setToolTip(
        QStringLiteral("打开文字排版：多行文本、字体、描边、粗体/斜体/下划线、"
                       "对齐、行高/字距/内边距、框宽高、竖排，并实时预览。"
                       "确定后一次性写回。"));
    textForm->addRow(m_textLayoutButton);
    addEditable(m_textLayoutButton);
    connect(m_textLayoutButton, &QPushButton::clicked, this,
            &PropertiesPanel::openTextLayoutDialog);

    // vector
    QFormLayout* vectorForm = addPage(QStringLiteral("vectorPage"));
    m_vectorPage = vectorForm->parentWidget();

    m_vectorShape = new QComboBox(m_vectorPage);
    m_vectorShape->setObjectName(QStringLiteral("vectorShapeCombo"));
    for (const ShapeOption& option : kShapeOptions)
        m_vectorShape->addItem(QString::fromUtf8(option.label), option.value);
    vectorForm->addRow(QStringLiteral("形状"), m_vectorShape);
    addEditable(m_vectorShape);
    connect(m_vectorShape, &QComboBox::currentIndexChanged, this, [this](int) {
        const int value = m_vectorShape->currentData().toInt();
        commitEdit(QStringLiteral("content.vector.shape"), [value](model::Entity& entity) {
            entity.content.vector.shape = value;
            return true;
        });
    });

    m_vectorWidth = new QDoubleSpinBox(m_vectorPage);
    m_vectorWidth->setObjectName(QStringLiteral("vectorWidthSpin"));
    m_vectorWidth->setRange(0.0, 1000000000.0);
    m_vectorWidth->setDecimals(2);
    m_vectorWidth->setSingleStep(1.0);
    m_vectorWidth->setSuffix(QStringLiteral(" px"));
    m_vectorWidth->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("宽度"), m_vectorWidth);
    addEditable(m_vectorWidth);
    connect(m_vectorWidth, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorWidth->value();
        commitEdit(QStringLiteral("content.vector.width"), [this, value](model::Entity& entity) {
            entity.content.vector.width = value;
            applyAutoKey(entity, QStringLiteral("width"), value);
            return true;
        });
    });

    m_vectorHeight = new QDoubleSpinBox(m_vectorPage);
    m_vectorHeight->setObjectName(QStringLiteral("vectorHeightSpin"));
    m_vectorHeight->setRange(0.0, 1000000000.0);
    m_vectorHeight->setDecimals(2);
    m_vectorHeight->setSingleStep(1.0);
    m_vectorHeight->setSuffix(QStringLiteral(" px"));
    m_vectorHeight->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("高度"), m_vectorHeight);
    addEditable(m_vectorHeight);
    connect(m_vectorHeight, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorHeight->value();
        commitEdit(QStringLiteral("content.vector.height"), [this, value](model::Entity& entity) {
            entity.content.vector.height = value;
            applyAutoKey(entity, QStringLiteral("height"), value);
            return true;
        });
    });

    m_vectorCorner = new QDoubleSpinBox(m_vectorPage);
    m_vectorCorner->setObjectName(QStringLiteral("vectorCornerRadiusSpin"));
    m_vectorCorner->setRange(0.0, 1000000000.0);
    m_vectorCorner->setDecimals(2);
    m_vectorCorner->setSingleStep(1.0);
    m_vectorCorner->setSuffix(QStringLiteral(" px"));
    m_vectorCorner->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("圆角"), m_vectorCorner);
    addEditable(m_vectorCorner);
    connect(m_vectorCorner, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorCorner->value();
        commitEdit(QStringLiteral("content.vector.cornerRadius"), [this, value](model::Entity& entity) {
            entity.content.vector.cornerRadius = value;
            applyAutoKey(entity, QStringLiteral("cornerRadius"), value);
            return true;
        });
    });

    m_vectorFillButton = new QPushButton(m_vectorPage);
    m_vectorFillButton->setObjectName(QStringLiteral("vectorFillButton"));
    vectorForm->addRow(QStringLiteral("填充色"), m_vectorFillButton);
    addEditable(m_vectorFillButton);
    connect(m_vectorFillButton, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(m_vectorFillValue, this, QStringLiteral("矢量填充色"));
        if (!chosen.isValid())
            return;
        const QString color = chosen.name(QColor::HexRgb);
        commitEdit(QStringLiteral("content.vector.fillColor"), [color](model::Entity& entity) {
            entity.content.vector.fillColor = color;
            return true;
        });
    });

    m_vectorStrokeButton = new QPushButton(m_vectorPage);
    m_vectorStrokeButton->setObjectName(QStringLiteral("vectorStrokeColorButton"));
    vectorForm->addRow(QStringLiteral("描边色"), m_vectorStrokeButton);
    addEditable(m_vectorStrokeButton);
    connect(m_vectorStrokeButton, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(m_vectorStrokeValue, this, QStringLiteral("矢量描边色"));
        if (!chosen.isValid())
            return;
        const QString color = chosen.name(QColor::HexRgb);
        commitEdit(QStringLiteral("content.vector.strokeColor"), [color](model::Entity& entity) {
            entity.content.vector.strokeColor = color;
            return true;
        });
    });

    m_vectorStrokeWidth = new QDoubleSpinBox(m_vectorPage);
    m_vectorStrokeWidth->setObjectName(QStringLiteral("vectorStrokeWidthSpin"));
    m_vectorStrokeWidth->setRange(0.0, 1000000000.0);
    m_vectorStrokeWidth->setDecimals(2);
    m_vectorStrokeWidth->setSingleStep(0.5);
    m_vectorStrokeWidth->setSuffix(QStringLiteral(" px"));
    m_vectorStrokeWidth->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("描边宽度"), m_vectorStrokeWidth);
    addEditable(m_vectorStrokeWidth);
    connect(m_vectorStrokeWidth, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorStrokeWidth->value();
        commitEdit(QStringLiteral("content.vector.strokeWidth"), [this, value](model::Entity& entity) {
            entity.content.vector.strokeWidth = value;
            applyAutoKey(entity, QStringLiteral("strokeWidth"), value);
            return true;
        });
    });

    // asset
    QFormLayout* assetForm = addPage(QStringLiteral("assetPage"));
    m_assetPage = assetForm->parentWidget();

    m_assetHash = new QLineEdit(m_assetPage);
    m_assetHash->setObjectName(QStringLiteral("assetHashEdit"));
    m_assetHash->setReadOnly(true);
    assetForm->addRow(QStringLiteral("素材 hash"), m_assetHash);

    m_assetPreview = new QLabel(m_assetPage);
    m_assetPreview->setObjectName(QStringLiteral("assetPreviewLabel"));
    m_assetPreview->setAlignment(Qt::AlignCenter);
    m_assetPreview->setWordWrap(true);
    m_assetPreview->setVisible(false);
    assetForm->addRow(QStringLiteral("预览"), m_assetPreview);

    m_assetType = new QComboBox(m_assetPage);
    m_assetType->setObjectName(QStringLiteral("assetTypeCombo"));
    m_assetType->addItem(QStringLiteral("图片"), int(AssetType::Image));
    m_assetType->addItem(QStringLiteral("视频"), int(AssetType::Video));
    m_assetType->addItem(QStringLiteral("音频"), int(AssetType::Audio));
    m_assetType->addItem(QStringLiteral("HTML"), int(AssetType::Html));
    assetForm->addRow(QStringLiteral("素材类型"), m_assetType);
    addEditable(m_assetType);
    connect(m_assetType, &QComboBox::currentIndexChanged, this, [this](int) {
        const int value = m_assetType->currentData().toInt();
        commitEdit(QStringLiteral("content.asset.assetType"), [value](model::Entity& entity) {
            entity.content.asset.type = AssetType(value);
            return true;
        });
    });

    m_assetSourceIn = new QSpinBox(m_assetPage);
    m_assetSourceIn->setObjectName(QStringLiteral("assetSourceInSpin"));
    m_assetSourceIn->setRange(0, 1000000);
    m_assetSourceIn->setSuffix(QStringLiteral(" 帧"));
    m_assetSourceIn->setKeyboardTracking(false);
    assetForm->addRow(QStringLiteral("源起点"), m_assetSourceIn);
    addEditable(m_assetSourceIn);
    connect(m_assetSourceIn, &QSpinBox::editingFinished, this, [this] {
        const int value = m_assetSourceIn->value();
        commitEdit(QStringLiteral("content.asset.sourceIn"), [value](model::Entity& entity) {
            entity.content.asset.sourceIn = value;
            return true;
        });
    });

    m_assetVolume = new QDoubleSpinBox(m_assetPage);
    m_assetVolume->setObjectName(QStringLiteral("assetVolumeSpin"));
    m_assetVolume->setRange(0.0, 1000.0);
    m_assetVolume->setDecimals(3);
    m_assetVolume->setSingleStep(0.05);
    m_assetVolume->setKeyboardTracking(false);
    assetForm->addRow(QStringLiteral("音量"), m_assetVolume);
    addEditable(m_assetVolume);
    connect(m_assetVolume, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_assetVolume->value();
        commitEdit(QStringLiteral("content.asset.volume"), [this, value](model::Entity& entity) {
            entity.content.asset.volume = value;
            applyAutoKey(entity, QStringLiteral("volume"), value);
            return true;
        });
    });

    m_assetReplaceButton = new QPushButton(QStringLiteral("更换素材…"), m_assetPage);
    m_assetReplaceButton->setObjectName(QStringLiteral("assetReplaceButton"));
    m_assetReplaceButton->setToolTip(
        QStringLiteral("从素材箱里选一个同组素材（视觉：图片/视频/HTML；音频：音频）。"
                       "视频/音频比实体短时，实体结束帧会按素材长度裁短。"));
    assetForm->addRow(m_assetReplaceButton);
    addEditable(m_assetReplaceButton);
    connect(m_assetReplaceButton, &QPushButton::clicked, this,
            &PropertiesPanel::chooseReplacementAsset);

    // camera
    QFormLayout* cameraForm = addPage(QStringLiteral("cameraPage"));
    m_cameraPage = cameraForm->parentWidget();
    auto* cameraHint = new QLabel(
        QStringLiteral("摄像机使用 transform 取景：x/y 是取景框中心，scaleX/scaleY 是画布占比"
                       "（0.05..1），rotation 是旋转。"),
        m_cameraPage);
    cameraHint->setWordWrap(true);
    cameraForm->addRow(cameraHint);

    // Static special content (comicText / radialLines / flowLines /
    // halftoneDots / brandWall).  One page per kind is inside the editor, so
    // this is a single content page here.
    m_specialEditor = new SpecialContentEditor(m_contentGroup);
    m_specialEditor->setObjectName(QStringLiteral("specialContentPage"));
    m_specialEditor->setVisible(false);
    outer->addWidget(m_specialEditor);
    addEditable(m_specialEditor);
    connect(m_specialEditor, &SpecialContentEditor::edited, this,
            &PropertiesPanel::commitSpecialContentEdit);
}

QPixmap PropertiesPanel::cachedAssetThumbnail(const AssetHash& hash,
                                             const assets::StoredAsset& asset)
{
    const auto cached = m_assetThumbnails.constFind(hash);
    if (cached != m_assetThumbnails.constEnd())
        return *cached;
    const QPixmap pixmap = assetThumbnail(asset, 320);
    m_assetThumbnails.insert(hash, pixmap);
    return pixmap;
}

void PropertiesPanel::reloadContent(const model::Entity& entity)
{
    const model::Content& content = entity.content;
    const ContentKind kind = content.kind;
    m_contentGroup->setTitle(QStringLiteral("内容 · %1").arg(detail::contentKindLabel(kind)));

    m_emptyPage->setVisible(kind == ContentKind::Empty);
    m_textPage->setVisible(kind == ContentKind::Text);
    m_vectorPage->setVisible(kind == ContentKind::Vector);
    m_assetPage->setVisible(kind == ContentKind::Asset);
    m_cameraPage->setVisible(kind == ContentKind::Camera);
    const bool special = SpecialContentEditor::handles(kind);
    m_specialEditor->setVisible(special);
    if (special)
        m_specialEditor->setContent(content);

    const model::TextContent& text = content.text;
    m_textEdit->setText(text.text);
    m_textFont->setCurrentFont(text.fontFamily.isEmpty() ? QFont() : QFont(text.fontFamily));
    m_textPixelSize->setValue(text.pixelSize);
    m_textColorValue = QColor(text.color);
    styleColorButton(m_textColorButton, m_textColorValue);
    selectComboData(m_textAlign, text.align);
    m_textLineHeight->setValue(text.lineHeight);
    m_textLetterSpacing->setValue(text.letterSpacing);
    m_textStrokeWidth->setValue(text.strokeWidth);

    const model::VectorContent& vector = content.vector;
    selectComboData(m_vectorShape, vector.shape);
    m_vectorWidth->setValue(vector.width);
    m_vectorHeight->setValue(vector.height);
    m_vectorCorner->setValue(vector.cornerRadius);
    m_vectorFillValue = QColor(vector.fillColor);
    m_vectorStrokeValue = QColor(vector.strokeColor);
    styleColorButton(m_vectorFillButton, m_vectorFillValue);
    styleColorButton(m_vectorStrokeButton, m_vectorStrokeValue);
    m_vectorStrokeWidth->setValue(vector.strokeWidth);

    const model::AssetContent& asset = content.asset;
    m_assetHash->setText(asset.hash);
    selectComboData(m_assetType, int(asset.type));
    m_assetSourceIn->setValue(asset.sourceIn);
    m_assetVolume->setValue(asset.volume);

    const std::optional<assets::StoredAsset> stored =
        m_document ? m_document->assets().get(asset.hash) : std::nullopt;
    m_assetMissing = !stored;
    if (stored && stored->type == AssetType::Image)
    {
        QPixmap thumbnail = cachedAssetThumbnail(asset.hash, *stored);
        if (thumbnail.width() > 280 || thumbnail.height() > 280)
        {
            thumbnail = thumbnail.scaled(280, 280, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
        }
        m_assetPreview->setPixmap(thumbnail);
        m_assetPreview->setText(thumbnail.isNull()
                                    ? QStringLiteral("图片无法解码")
                                    : QString());
        m_assetPreview->setToolTip(stored->name);
        m_assetPreview->setVisible(true);
    }
    else
    {
        m_assetPreview->clear();
        m_assetPreview->setToolTip(QString());
        m_assetPreview->setVisible(false);
    }
}

bool PropertiesPanel::replaceAsset(const AssetHash& hash, QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = QStringLiteral("没有打开的工程");
        return false;
    }

    model::Entity* entity = selectedEntity();
    if (!entity)
    {
        if (error)
            *error = QStringLiteral("未选择实体");
        return false;
    }
    if (isEntityLocked(m_document->project(), entity->uuid))
    {
        if (error)
            *error = QStringLiteral("实体或其祖先已锁定");
        setStatus(QStringLiteral("实体或其祖先已锁定，不能更换素材"));
        return false;
    }
    if (entity->content.kind != ContentKind::Asset)
    {
        if (error)
            *error = QStringLiteral("实体不是素材内容");
        return false;
    }

    const auto stored = m_document->assets().get(hash);
    if (!stored)
    {
        if (error)
            *error = QStringLiteral("素材不存在：%1").arg(hash);
        return false;
    }
    if (!sameAssetGroup(entity->content.asset.type, stored->type))
    {
        if (error)
            *error = QStringLiteral("素材类型不兼容：%1 → %2")
                         .arg(assetTypeLabel(entity->content.asset.type),
                              assetTypeLabel(stored->type));
        setStatus(QStringLiteral("不能把 %1 换成 %2 素材")
                      .arg(assetTypeLabel(entity->content.asset.type),
                           assetTypeLabel(stored->type)));
        return false;
    }
    if (hash == entity->content.asset.hash)
    {
        if (error)
            *error = QStringLiteral("已经使用该素材");
        return false;
    }

    const AssetReplacementPlan plan = planAssetReplacement(*entity, *stored);
    if (plan.needsTrim)
    {
        // MCP rejects the swap when trimming would delete child entities; do
        // the same here instead of leaving children outside the new range.
        const Uuid self = entity->uuid;
        const int newEnd = entity->start + plan.duration;
        for (auto it = m_document->project().entities.constBegin();
             it != m_document->project().entities.constEnd(); ++it)
        {
            if (it.value().parentUuid != self)
                continue;
            if (it.value().end > newEnd)
            {
                if (error)
                    *error = QStringLiteral(
                        "换素材会裁剪子实体 %1，请先裁剪子实体").arg(it.value().name);
                setStatus(QStringLiteral(
                    "换素材会裁剪子实体，请先裁剪子实体：%1").arg(it.value().name));
                return false;
            }
        }
    }

    const assets::StoredAsset asset = *stored;
    const bool ok = commitEdit(
        QStringLiteral("content.asset"), [asset, plan](model::Entity& target) {
            applyAssetReplacement(target, asset, plan);
            return true;
        });
    if (ok)
    {
        setStatus(QStringLiteral("已更换素材：%1%2")
                      .arg(asset.name,
                           plan.needsTrim
                               ? QStringLiteral("（时长裁到 %1 帧）").arg(plan.duration)
                               : QString()));
    }
    return ok;
}

void PropertiesPanel::openTextLayoutDialog()
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity
        || isEntityLocked(m_document->project(), entity->uuid)
        || entity->content.kind != ContentKind::Text)
        return;

    const Uuid uuid = entity->uuid;
    TextLayoutDialog dialog(entity->content.text, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // The modal dialog cannot change selection through the UI, but guard
    // against a document signal having replaced the selected entity.
    model::Entity* current = selectedEntity();
    if (!current || current->uuid != uuid)
        return;

    const model::TextContent content = dialog.content();
    const QJsonObject before = model::toJson(*current);
    const model::Entity original = *current;
    model::Entity updated = *current;
    updated.content.text = content;

    QString error;
    if (!model::validateEntity(updated, &error))
    {
        setStatus(QStringLiteral("未修改：%1").arg(error));
        refresh();
        return;
    }
    if (model::toJson(updated) == before)
        return;

    *current = updated;
    m_committing = true;
    const bool ok =
        m_document->commitEntity(uuid, QStringLiteral("pvf: edit text layout"),
                                 &error);
    m_committing = false;

    if (!ok)
    {
        model::Entity* latest = selectedEntity();
        if (latest && latest->uuid == original.uuid)
            *latest = original;
        showSaveError(error);
        refresh();
        return;
    }

    setStatus(QStringLiteral("已保存：文字排版"));
    refresh();
}

void PropertiesPanel::commitSpecialContentEdit()
{
    if (m_loading || m_committing || !m_document || !m_specialEditor)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity || !SpecialContentEditor::handles(entity->content.kind))
        return;
    if (m_specialEditor->kind() != entity->content.kind)
        return;

    model::Content content = entity->content;
    m_specialEditor->applyTo(content);

    // model::validateContent permits an empty brand-wall logo list (the
    // renderer just draws empty tiles), but an editor save without a logo is
    // meaningless.  Reject it here before the commit.
    if (content.kind == ContentKind::BrandWall
        && content.brandWall.logos.isEmpty())
    {
        setStatus(QStringLiteral("未修改：品牌墙至少需要一个 logo"));
        refresh();
        return;
    }

    commitEditWithMessage(
        QStringLiteral("%1 内容").arg(detail::contentKindLabel(content.kind)),
        QStringLiteral("pvf: edit %1 content")
            .arg(contentKindName(content.kind)),
        [content](model::Entity& updated) {
            updated.content = content;
            return true;
        });
}

void PropertiesPanel::chooseReplacementAsset()
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity
        || isEntityLocked(m_document->project(), entity->uuid)
        || entity->content.kind != ContentKind::Asset)
        return;

    const AssetType currentType = entity->content.asset.type;
    if (!m_document->assets().get(entity->content.asset.hash))
    {
        setStatus(QStringLiteral("素材缺失，无法更换；请先把素材放回 assets/"));
        refresh();
        return;
    }

    QVector<assets::StoredAsset> candidates;
    for (const assets::StoredAsset& asset : m_document->assets().list())
    {
        if (sameAssetGroup(currentType, asset.type))
            candidates.append(asset);
    }
    if (candidates.isEmpty())
    {
        QMessageBox::information(
            this, QStringLiteral("更换素材"),
            QStringLiteral("素材箱里没有与当前实体兼容的素材。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("更换素材 · %1").arg(entity->name));
    dialog.resize(560, 520);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    auto* search = new QLineEdit(&dialog);
    search->setObjectName(QStringLiteral("assetPickSearch"));
    search->setPlaceholderText(QStringLiteral("搜索名称、类型或 hash…"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);

    auto* list = new HoverListWidget(&dialog);
    list->setObjectName(QStringLiteral("assetPickList"));
    list->setIconSize(QSize(64, 40));
    layout->addWidget(list, 1);

    auto* preview = new QLabel(&dialog);
    preview->setObjectName(QStringLiteral("assetPickPreview"));
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumSize(200, 160);
    preview->setText(QStringLiteral("选择或悬停一个素材查看预览"));
    layout->addWidget(preview);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("更换"));

    auto makeItem = [](const assets::StoredAsset& asset) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(asset.name, assetInfoText(asset)));
        item->setData(Qt::UserRole, asset.hash);
        item->setToolTip(asset.name);
        return item;
    };
    for (const assets::StoredAsset& asset : candidates)
        list->addItem(makeItem(asset));

    auto showPreview = [this, preview, list](QListWidgetItem* item) {
        if (!item || !m_document)
        {
            preview->setPixmap(QPixmap());
            preview->setText(QStringLiteral("选择或悬停一个素材查看预览"));
            return;
        }
        const auto asset = m_document->assets().get(item->data(Qt::UserRole).toString());
        if (!asset || asset->type != AssetType::Image)
        {
            preview->setPixmap(QPixmap());
            preview->setText(asset
                                 ? QStringLiteral("%1 素材没有图片预览").arg(
                                       assetTypeLabel(asset->type))
                                 : QStringLiteral("素材缺失"));
            return;
        }
        const QPixmap thumbnail = cachedAssetThumbnail(asset->hash, *asset);
        if (thumbnail.isNull())
        {
            preview->setPixmap(QPixmap());
            preview->setText(QStringLiteral("图片无法解码"));
            return;
        }
        preview->setText(QString());
        preview->setPixmap(thumbnail);
    };
    list->hovered = showPreview;

    auto currentHash = [list]() -> AssetHash {
        QListWidgetItem* item = list->currentItem();
        if (!item || item->isHidden())
            return {};
        return item->data(Qt::UserRole).toString();
    };
    auto updateOk = [buttons, currentHash]() {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!currentHash().isEmpty());
    };

    connect(list, &QListWidget::currentItemChanged, this,
            [showPreview, updateOk](QListWidgetItem* current, QListWidgetItem*) {
                showPreview(current);
                updateOk();
            });
    connect(list, &QListWidget::itemActivated, &dialog, [&dialog, currentHash] {
        if (!currentHash().isEmpty())
            dialog.accept();
    });
    connect(search, &QLineEdit::textChanged, &dialog,
            [list, updateOk](const QString& text) {
                const QString query = text.trimmed();
                for (int i = 0; i < list->count(); ++i)
                {
                    QListWidgetItem* item = list->item(i);
                    item->setHidden(!query.isEmpty()
                                    && !item->text().contains(query,
                                                              Qt::CaseInsensitive));
                }
                if (QListWidgetItem* current = list->currentItem();
                    current && current->isHidden())
                {
                    current->setSelected(false);
                    list->setCurrentItem(nullptr);
                }
                updateOk();
            });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, currentHash] {
        if (!currentHash().isEmpty())
            dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (!list->count())
        return;
    list->setCurrentRow(0);
    updateOk();

    if (dialog.exec() != QDialog::Accepted)
        return;

    const AssetHash chosen = currentHash();
    if (chosen.isEmpty())
        return;
    QString error;
    if (!replaceAsset(chosen, &error) && !error.isEmpty())
        setStatus(QStringLiteral("更换素材失败：%1").arg(error));
}

} // namespace pvf::next
