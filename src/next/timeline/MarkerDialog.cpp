#include "next/timeline/MarkerDialog.h"

#include <QColor>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace pvf::next {

bool normalizeMarker(model::Marker& marker, QString* error)
{
    marker.title = marker.title.trimmed();
    if (marker.title.isEmpty())
    {
        if (error)
            *error = QStringLiteral("标记标题不能为空");
        return false;
    }
    if (marker.title.size() > 256)
    {
        if (error)
            *error = QStringLiteral("标记标题不能超过 256 个字符");
        return false;
    }
    if (marker.note.size() > 4096)
    {
        if (error)
            *error = QStringLiteral("备注不能超过 4096 个字符");
        return false;
    }

    const QColor color(marker.color);
    if (!color.isValid())
    {
        if (error)
            *error = QStringLiteral("标记颜色无效");
        return false;
    }
    marker.color = color.alpha() < 255 ? color.name(QColor::HexArgb)
                                       : color.name(QColor::HexRgb);

    QStringList tags;
    for (QString tag : marker.tags)
    {
        tag = tag.trimmed();
        if (tag.isEmpty())
            continue;
        if (tag.size() > 64)
        {
            if (error)
                *error = QStringLiteral("标签不能超过 64 个字符");
            return false;
        }
        if (!tags.contains(tag))
            tags.append(tag);
    }
    if (tags.size() > 32)
    {
        if (error)
            *error = QStringLiteral("标记最多 32 个标签");
        return false;
    }
    marker.tags = tags;
    return true;
}

QStringList parseMarkerTags(const QString& commaSeparated, QString* error)
{
    QStringList tags;
    QString text = commaSeparated;
    text.replace(QChar(0xFF0C), QLatin1Char(',')); // 中文逗号
    const QStringList pieces = text.split(QLatin1Char(','));
    for (QString tag : pieces)
    {
        tag = tag.trimmed();
        if (tag.isEmpty())
            continue;
        if (tag.size() > 64)
        {
            if (error)
                *error = QStringLiteral("标签不能超过 64 个字符");
            return {};
        }
        if (!tags.contains(tag))
            tags.append(tag);
    }
    if (tags.size() > 32)
    {
        if (error)
            *error = QStringLiteral("标记最多 32 个标签");
        return {};
    }
    if (error)
        error->clear();
    return tags;
}

MarkerDialog::MarkerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("标记"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_title = new QLineEdit(this);
    m_title->setMaxLength(256);
    form->addRow(tr("标题"), m_title);

    m_colorButton = new QPushButton(this);
    form->addRow(tr("颜色"), m_colorButton);

    m_tags = new QLineEdit(this);
    m_tags->setPlaceholderText(tr("用逗号分隔，最多 32 个"));
    form->addRow(tr("标签"), m_tags);

    m_note = new QPlainTextEdit(this);
    m_note->setPlaceholderText(tr("备注（最长 4096 字）"));
    m_note->setFixedHeight(120);
    form->addRow(tr("备注"), m_note);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                             | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &MarkerDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_colorButton, &QPushButton::clicked, this,
            &MarkerDialog::chooseColor);
    layout->addWidget(buttons);

    setMarker(model::Marker());
}

void MarkerDialog::setMarker(const model::Marker& marker)
{
    m_marker = marker;
    m_title->setText(marker.title);
    m_note->setPlainText(marker.note);
    m_tags->setText(marker.tags.join(QStringLiteral(", ")));
    updateColorButton();
}

void MarkerDialog::chooseColor()
{
    QColor initial(m_marker.color);
    if (!initial.isValid())
        initial = QColor(255, 190, 60);
    const QColor chosen = QColorDialog::getColor(
        initial, this, tr("标记颜色"), QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid())
        return;
    m_marker.color = chosen.alpha() < 255 ? chosen.name(QColor::HexArgb)
                                          : chosen.name(QColor::HexRgb);
    updateColorButton();
}

void MarkerDialog::updateColorButton()
{
    QColor color(m_marker.color);
    if (!color.isValid())
        color = QColor(255, 190, 60);

    QPixmap swatch(14, 14);
    swatch.fill(color);
    m_colorButton->setIcon(QIcon(swatch));
    m_colorButton->setText(m_marker.color);
}

void MarkerDialog::accept()
{
    model::Marker candidate = m_marker;
    candidate.title = m_title->text();
    candidate.note = m_note->toPlainText();

    QString error;
    candidate.tags = parseMarkerTags(m_tags->text(), &error);
    if (!error.isEmpty())
    {
        QMessageBox::warning(this, tr("标记"), error);
        return;
    }
    if (!normalizeMarker(candidate, &error))
    {
        QMessageBox::warning(this, tr("标记"), error);
        return;
    }

    m_marker = candidate;
    QDialog::accept();
}

} // namespace pvf::next
