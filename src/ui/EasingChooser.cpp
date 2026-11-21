#include "EasingChooser.h"

#include "EasingPicker.h"

#include <QHash>
#include <QWidgetAction>

namespace pvf {

namespace {

QString familyName(const QString& suffix)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("quad"), QStringLiteral("二次")},
        {QStringLiteral("cubic"), QStringLiteral("三次")},
        {QStringLiteral("quart"), QStringLiteral("四次")},
        {QStringLiteral("quint"), QStringLiteral("五次")},
        {QStringLiteral("sine"), QStringLiteral("正弦")},
        {QStringLiteral("expo"), QStringLiteral("指数")},
        {QStringLiteral("circ"), QStringLiteral("圆形")},
        {QStringLiteral("elastic"), QStringLiteral("弹性")},
        {QStringLiteral("back"), QStringLiteral("回弹")},
        {QStringLiteral("bounce"), QStringLiteral("弹跳")},
    };
    return names.value(suffix, suffix);
}

} // namespace

QString easingDisplayName(const QString& type)
{
    const QString canonical = model::canonicalEasingName(type);
    if (canonical == QLatin1String("linear"))
        return QStringLiteral("线性");
    if (canonical == QLatin1String("instant"))
        return QStringLiteral("瞬时");
    if (canonical == QLatin1String("bezier"))
        return QStringLiteral("自定义贝塞尔");

    QString prefix;
    QString suffix;
    if (canonical.startsWith(QLatin1String("easeinout")))
    {
        prefix = QStringLiteral("缓入缓出");
        suffix = canonical.mid(9);
    }
    else if (canonical.startsWith(QLatin1String("easein")))
    {
        prefix = QStringLiteral("缓入");
        suffix = canonical.mid(6);
    }
    else if (canonical.startsWith(QLatin1String("easeout")))
    {
        prefix = QStringLiteral("缓出");
        suffix = canonical.mid(7);
    }
    if (prefix.isEmpty())
        return canonical;
    return QStringLiteral("%1 · %2").arg(prefix, familyName(suffix));
}

QString easingDisplayName(const model::EasingSpec& spec)
{
    return easingDisplayName(spec.type);
}

EasingPopup::EasingPopup(const model::EasingSpec& current, QWidget* parent)
    : QMenu(parent)
{
    setObjectName(QStringLiteral("easingPopup"));
    setAttribute(Qt::WA_DeleteOnClose);
    auto* action = new QWidgetAction(this);
    m_picker = new EasingPicker(current, this);
    action->setDefaultWidget(m_picker);
    addAction(action);
    connect(m_picker, &EasingPicker::picked, this,
            [this](const model::EasingSpec& spec) {
                close();
                emit picked(spec);
            });
    connect(m_picker, &EasingPicker::customRequested, this, [this] {
        close();
        emit customRequested();
    });
}

} // namespace pvf
