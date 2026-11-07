#include "BezierEditorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace pvf {

namespace {

QPointF bezierPoint(const EasingSpec& spec, double t)
{
    const double u = 1.0 - t;
    const double x = 3.0 * u * u * t * spec.inX + 3.0 * u * t * t * spec.outX
                     + t * t * t;
    const double y = 3.0 * u * u * t * spec.inY + 3.0 * u * t * t * spec.outY
                     + t * t * t;
    return QPointF(x, y);
}

} // namespace

class BezierGraph : public QWidget
{
public:
    BezierGraph(const EasingSpec& spec, std::function<void(const EasingSpec&)> changed,
                QWidget* parent = nullptr)
        : QWidget(parent)
        , m_spec(spec)
        , m_changed(std::move(changed))
    {
        setMinimumSize(260, 260);
        setMouseTracking(true);
    }

    void setSpec(const EasingSpec& spec)
    {
        m_spec = spec;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QPalette pal = palette();
        const QRectF area = graphRect();
        painter.fillRect(rect(), pal.color(QPalette::Base));
        painter.fillRect(area, pal.color(QPalette::AlternateBase));
        painter.setPen(QPen(pal.color(QPalette::Mid), 1.0));
        painter.drawRect(area);

        painter.setPen(QPen(pal.color(QPalette::Mid), 1.0, Qt::DotLine));
        for (int i = 1; i < 4; ++i)
        {
            const double f = i / 4.0;
            painter.drawLine(unitToScreen(area, QPointF(f, 0.0)),
                             unitToScreen(area, QPointF(f, 1.0)));
            painter.drawLine(unitToScreen(area, QPointF(0.0, f)),
                             unitToScreen(area, QPointF(1.0, f)));
        }

        QPainterPath curve;
        curve.moveTo(unitToScreen(area, bezierPoint(m_spec, 0.0)));
        for (int i = 1; i <= 120; ++i)
            curve.lineTo(unitToScreen(area, bezierPoint(m_spec, i / 120.0)));
        painter.setPen(QPen(pal.color(QPalette::Highlight), 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(curve);

        const QPointF start = unitToScreen(area, QPointF(0.0, 0.0));
        const QPointF end = unitToScreen(area, QPointF(1.0, 1.0));
        const QPointF in = unitToScreen(area, QPointF(m_spec.inX, m_spec.inY));
        const QPointF out = unitToScreen(area, QPointF(m_spec.outX, m_spec.outY));

        painter.setPen(QPen(pal.color(QPalette::PlaceholderText), 1.0, Qt::DashLine));
        painter.drawLine(start, in);
        painter.drawLine(end, out);

        painter.setPen(QPen(pal.color(QPalette::Text), 1.5));
        painter.setBrush(pal.color(QPalette::Highlight));
        painter.drawEllipse(in, 6.0, 6.0);
        painter.drawEllipse(out, 6.0, 6.0);
        painter.setBrush(pal.color(QPalette::Base));
        painter.drawEllipse(start, 3.5, 3.5);
        painter.drawEllipse(end, 3.5, 3.5);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        const QRectF area = graphRect();
        const QPointF in = unitToScreen(area, QPointF(m_spec.inX, m_spec.inY));
        const QPointF out = unitToScreen(area, QPointF(m_spec.outX, m_spec.outY));
        const double dIn = QLineF(event->position(), in).length();
        const double dOut = QLineF(event->position(), out).length();
        if (dIn <= 14.0 && dIn <= dOut)
            m_drag = Handle::In;
        else if (dOut <= 14.0)
            m_drag = Handle::Out;
        if (m_drag != Handle::None)
            setCursor(Qt::ClosedHandCursor);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_drag == Handle::None)
            return;
        const QPointF unit = screenToUnit(graphRect(), event->position());
        EasingSpec spec = m_spec;
        if (m_drag == Handle::In)
        {
            spec.inX = unit.x();
            spec.inY = unit.y();
            if (m_sync)
            {
                spec.outX = 1.0 - spec.inX;
                spec.outY = 1.0 - spec.inY;
            }
        }
        else
        {
            spec.outX = unit.x();
            spec.outY = unit.y();
            if (m_sync)
            {
                spec.inX = 1.0 - spec.outX;
                spec.inY = 1.0 - spec.outY;
            }
        }
        m_spec = spec;
        update();
        if (m_changed)
            m_changed(m_spec);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_drag = Handle::None;
            unsetCursor();
        }
    }

private:
    enum class Handle { None, In, Out };

    static QRectF graphRect(const QWidget* widget, int width, int height)
    {
        const double margin = 24.0;
        return QRectF(margin, margin, width - margin * 2.0, height - margin * 2.0);
    }

    QRectF graphRect() const { return graphRect(this, width(), height()); }

    static QPointF unitToScreen(const QRectF& area, const QPointF& unit)
    {
        return QPointF(area.left() + unit.x() * area.width(),
                       area.bottom() - unit.y() * area.height());
    }

    static QPointF screenToUnit(const QRectF& area, const QPointF& screen)
    {
        return QPointF(std::clamp((screen.x() - area.left()) / area.width(), 0.0, 1.0),
                       std::clamp((area.bottom() - screen.y()) / area.height(), -1.0,
                                  2.0));
    }

    EasingSpec m_spec;
    std::function<void(const EasingSpec&)> m_changed;
    Handle m_drag = Handle::None;

public:
    bool m_sync = false;
};

BezierEditorDialog::BezierEditorDialog(const EasingSpec& spec, QWidget* parent)
    : QDialog(parent)
    , m_spec(spec)
{
    setWindowTitle(QStringLiteral("关键帧曲线"));
    m_spec.easing = Easing::Bezier;

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    m_graph = new BezierGraph(m_spec, [this](const EasingSpec& changed) {
        m_spec = changed;
        m_spec.easing = Easing::Bezier;
        updateSpins();
    });
    layout->addWidget(m_graph);

    auto* right = new QVBoxLayout();
    auto* form = new QFormLayout();
    auto spin = [this](double value) {
        auto* box = new QDoubleSpinBox(this);
        box->setRange(-200.0, 300.0);
        box->setDecimals(1);
        box->setSingleStep(1.0);
        box->setSuffix(QStringLiteral("%"));
        box->setValue(value * 100.0);
        box->setKeyboardTracking(false);
        return box;
    };
    m_inX = spin(m_spec.inX);
    m_inY = spin(m_spec.inY);
    m_outX = spin(m_spec.outX);
    m_outY = spin(m_spec.outY);
    m_inX->setRange(0.0, 100.0);
    m_outX->setRange(0.0, 100.0);
    form->addRow(QStringLiteral("入点 X"), m_inX);
    form->addRow(QStringLiteral("入点 Y"), m_inY);
    form->addRow(QStringLiteral("出点 X"), m_outX);
    form->addRow(QStringLiteral("出点 Y"), m_outY);
    right->addLayout(form);

    m_sync = new QCheckBox(QStringLiteral("对称拖动"), this);
    m_sync->setToolTip(QStringLiteral("拖动一侧手柄时，另一侧按 1-x / 1-y 镜像"));
    right->addWidget(m_sync);
    right->addWidget(new QLabel(
        QStringLiteral("X 越大越晚变化，Y 为负会先反向再回来。\n"
                       "曲线控制当前关键帧到下一关键帧的过渡。"),
        this));
    right->addStretch(1);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(QStringLiteral("确定"));
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    right->addWidget(buttons);
    layout->addLayout(right);

    connect(m_inX, &QDoubleSpinBox::valueChanged, this, &BezierEditorDialog::updateFromSpins);
    connect(m_inY, &QDoubleSpinBox::valueChanged, this, &BezierEditorDialog::updateFromSpins);
    connect(m_outX, &QDoubleSpinBox::valueChanged, this, &BezierEditorDialog::updateFromSpins);
    connect(m_outY, &QDoubleSpinBox::valueChanged, this, &BezierEditorDialog::updateFromSpins);
    connect(m_sync, &QCheckBox::toggled, this, [this](bool on) {
        if (m_graph)
            m_graph->m_sync = on;
    });
    updateSpins();
    resize(620, 360);
}

void BezierEditorDialog::updateSpins()
{
    m_updating = true;
    const QSignalBlocker b1(m_inX);
    const QSignalBlocker b2(m_inY);
    const QSignalBlocker b3(m_outX);
    const QSignalBlocker b4(m_outY);
    m_inX->setValue(m_spec.inX * 100.0);
    m_inY->setValue(m_spec.inY * 100.0);
    m_outX->setValue(m_spec.outX * 100.0);
    m_outY->setValue(m_spec.outY * 100.0);
    m_updating = false;
}

void BezierEditorDialog::updateFromSpins()
{
    if (m_updating)
        return;
    EasingSpec spec;
    spec.easing = Easing::Bezier;
    spec.inX = m_inX->value() / 100.0;
    spec.inY = m_inY->value() / 100.0;
    spec.outX = m_outX->value() / 100.0;
    spec.outY = m_outY->value() / 100.0;
    if (m_sync->isChecked())
    {
        auto* editor = qobject_cast<QDoubleSpinBox*>(sender());
        if (editor == m_outX || editor == m_outY)
        {
            spec.inX = 1.0 - spec.outX;
            spec.inY = 1.0 - spec.outY;
        }
        else
        {
            spec.outX = 1.0 - spec.inX;
            spec.outY = 1.0 - spec.inY;
        }
    }
    m_spec = spec;
    if (m_graph)
        m_graph->setSpec(m_spec);
    updateSpins();
}

} // namespace pvf
