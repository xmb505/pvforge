#pragma once

#include "model/Content.h"

#include <QColor>
#include <QHash>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QFontComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace pvf::next {

// Inline editor for the static special-content kinds (comicText, radialLines,
// flowLines, halftoneDots, brandWall).  One page per kind lives in a stack;
// setContent() shows the active page and applyTo() writes only that page's
// members, so switching kinds never leaks values into the wrong struct.
//
// edited() fires after a field finishes (Enter, focus out, toggle, combo pick,
// colour pick).  PropertiesPanel copies the whole content struct and commits
// once per finished edit.  Keyframes are deliberately not offered here.
class SpecialContentEditor : public QWidget
{
    Q_OBJECT

public:
    explicit SpecialContentEditor(QWidget* parent = nullptr);

    static bool handles(ContentKind kind);

    void setContent(const model::Content& content);
    // Writes the active page's fields into `content`; other kind members stay
    // untouched so untouched data is preserved.
    void applyTo(model::Content& content) const;

    ContentKind kind() const { return m_kind; }

signals:
    void edited();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildPages();
    void buildComicPage();
    void buildRadialPage();
    void buildFlowPage();
    void buildHalftonePage();
    void buildBrandWallPage();

    QFormLayout* addPage(const QString& objectName);
    QSpinBox* addInt(QFormLayout* form, const QString& label,
                     const QString& objectName, int minimum, int maximum,
                     const QString& suffix = QString());
    QDoubleSpinBox* addDouble(QFormLayout* form, const QString& label,
                              const QString& objectName, double minimum,
                              double maximum, double step, int decimals,
                              const QString& suffix = QString());
    QCheckBox* addCheck(QFormLayout* form, const QString& label,
                        const QString& objectName);
    QComboBox* addCombo(QFormLayout* form, const QString& label,
                        const QString& objectName);
    QLineEdit* addLine(QFormLayout* form, const QString& label,
                       const QString& objectName);
    QPlainTextEdit* addPlain(QFormLayout* form, const QString& label,
                             const QString& objectName);
    QPushButton* addColor(QFormLayout* form, const QString& label,
                          const QString& objectName, const QColor& initial);

    void wireWidgets();
    void notifyEdited();
    void pickColor(QPushButton* button);
    void setColor(QPushButton* button, const QColor& color);
    // Loads a colour string verbatim (so an untouched "#fff" or named colour
    // round-trips unchanged) while showing the parsed colour on the button.
    void loadColor(QPushButton* button, const QString& value,
                   const QColor& fallback);
    QString colorTextValue(QPushButton* button, const QString& fallback) const;
    void updateComicPreview();

    void loadComic(const model::ComicTextContent& content);
    void loadRadial(const model::RadialLinesContent& content);
    void loadFlow(const model::FlowLinesContent& content);
    void loadHalftone(const model::HalftoneDotsContent& content);
    void loadBrandWall(const model::BrandWallContent& content);

    void saveComic(model::Content& content) const;
    void saveRadial(model::Content& content) const;
    void saveFlow(model::Content& content) const;
    void saveHalftone(model::Content& content) const;
    void saveBrandWall(model::Content& content) const;

    int pageIndexFor(ContentKind kind) const;

    bool m_loading = false;
    ContentKind m_kind = ContentKind::Empty;
    QStackedWidget* m_stack = nullptr;

    QVector<QSpinBox*> m_intSpins;
    QVector<QDoubleSpinBox*> m_doubleSpins;
    QVector<QComboBox*> m_combos;
    QVector<QCheckBox*> m_checks;
    QVector<QLineEdit*> m_lines;
    QVector<QPlainTextEdit*> m_plains;
    QVector<QPushButton*> m_colorButtons;
    QHash<QPushButton*, QColor> m_colorValues;
    QHash<QPushButton*, QString> m_colorTexts;

    // comicText
    QPlainTextEdit* m_comicText = nullptr;
    QFontComboBox* m_comicFont = nullptr;
    QString m_comicFontFamily;
    QSpinBox* m_comicPixelSize = nullptr;
    QPushButton* m_comicColor = nullptr;
    QPushButton* m_comicStrokeColor = nullptr;
    QDoubleSpinBox* m_comicStrokeWidth = nullptr;
    QComboBox* m_comicAlign = nullptr;
    QDoubleSpinBox* m_comicLineHeight = nullptr;
    QCheckBox* m_comicBold = nullptr;
    QCheckBox* m_comicItalic = nullptr;
    QComboBox* m_comicShape = nullptr;
    QDoubleSpinBox* m_comicBoxWidth = nullptr;
    QDoubleSpinBox* m_comicBoxHeight = nullptr;
    QPushButton* m_comicFillColor = nullptr;
    QPushButton* m_comicBubbleStrokeColor = nullptr;
    QDoubleSpinBox* m_comicBubbleStrokeWidth = nullptr;
    QDoubleSpinBox* m_comicCornerRadius = nullptr;
    QCheckBox* m_comicTail = nullptr;
    QDoubleSpinBox* m_comicTailX = nullptr;
    QDoubleSpinBox* m_comicTailY = nullptr;
    QDoubleSpinBox* m_comicRotation = nullptr;
    QDoubleSpinBox* m_comicPadding = nullptr;
    QSpinBox* m_comicSeed = nullptr;
    QLabel* m_comicPreview = nullptr;

    // radialLines
    QSpinBox* m_radialCount = nullptr;
    QDoubleSpinBox* m_radialRadius = nullptr;
    QDoubleSpinBox* m_radialInnerRatio = nullptr;
    QDoubleSpinBox* m_radialWidth = nullptr;
    QDoubleSpinBox* m_radialAngle = nullptr;
    QPushButton* m_radialColor = nullptr;
    QSpinBox* m_radialSeed = nullptr;

    // flowLines
    QSpinBox* m_flowCount = nullptr;
    QDoubleSpinBox* m_flowLength = nullptr;
    QDoubleSpinBox* m_flowSpacing = nullptr;
    QDoubleSpinBox* m_flowThickness = nullptr;
    QDoubleSpinBox* m_flowArea = nullptr;
    QDoubleSpinBox* m_flowBend = nullptr;
    QDoubleSpinBox* m_flowAngle = nullptr;
    QDoubleSpinBox* m_flowPhase = nullptr;
    QPushButton* m_flowColor = nullptr;
    QSpinBox* m_flowSeed = nullptr;

    // halftoneDots
    QComboBox* m_halftoneLayout = nullptr;
    QDoubleSpinBox* m_halftoneArea = nullptr;
    QDoubleSpinBox* m_halftoneRadius = nullptr;
    QDoubleSpinBox* m_halftoneSpacing = nullptr;
    QDoubleSpinBox* m_halftoneDotSize = nullptr;
    QDoubleSpinBox* m_halftoneAngle = nullptr;
    QDoubleSpinBox* m_halftoneGradientAngle = nullptr;
    QDoubleSpinBox* m_halftoneMinRatio = nullptr;
    QDoubleSpinBox* m_halftoneJitter = nullptr;
    QPushButton* m_halftoneColor = nullptr;
    QSpinBox* m_halftoneSeed = nullptr;

    // brandWall
    QPlainTextEdit* m_brandLogos = nullptr;
    QSpinBox* m_brandColumns = nullptr;
    QSpinBox* m_brandRows = nullptr;
    QDoubleSpinBox* m_brandSpacing = nullptr;
    QDoubleSpinBox* m_brandLogoSize = nullptr;
    QLineEdit* m_brandColors = nullptr;
    QCheckBox* m_brandAlternate = nullptr;
    QDoubleSpinBox* m_brandAngle = nullptr;
    QLineEdit* m_brandBackground = nullptr;
    QSpinBox* m_brandSeed = nullptr;
};

} // namespace pvf::next
