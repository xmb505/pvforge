#pragma once

#include "model/Project.h"

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace pvf::next {

// Trims and validates a marker in place:
//   title  1..256 characters
//   note   <= 4096 characters
//   color  a valid Qt color string, normalised to #rrggbb or #aarrggbb
//   tags   <= 32 unique entries, each <= 64 characters; empties are dropped
// Returns false and sets *error on the first violation.
bool normalizeMarker(model::Marker& marker, QString* error = nullptr);

// Splits a comma-separated tag list and applies the same tag rules as
// normalizeMarker(). Returns an empty list and sets *error when invalid.
QStringList parseMarkerTags(const QString& commaSeparated,
                            QString* error = nullptr);

// Modal editor for project.markers. The dialog validates on accept and only
// updates its stored marker after every rule passes.
class MarkerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MarkerDialog(QWidget* parent = nullptr);

    void setMarker(const model::Marker& marker);
    model::Marker marker() const { return m_marker; }

public slots:
    void accept() override;

private:
    void chooseColor();
    void updateColorButton();

    QLineEdit* m_title = nullptr;
    QPushButton* m_colorButton = nullptr;
    QPlainTextEdit* m_note = nullptr;
    QLineEdit* m_tags = nullptr;
    model::Marker m_marker;
};

} // namespace pvf::next
