#include "McpCommandService.h"

#include "core/History.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>

namespace pvf {

namespace {

constexpr double kMaxValue = 1e9;
constexpr int kMaxNameLength = 256;
constexpr int kMaxTextLength = 4096;

QString entityTypeName(EntityType type)
{
    switch (type)
    {
    case EntityType::Image:
        return QStringLiteral("image");
    case EntityType::Video:
        return QStringLiteral("video");
    case EntityType::Text:
        return QStringLiteral("text");
    case EntityType::Vector:
        return QStringLiteral("vector");
    case EntityType::HtmlLayer:
        return QStringLiteral("htmlLayer");
    case EntityType::Camera:
        return QStringLiteral("camera");
    case EntityType::Audio:
        return QStringLiteral("audio");
    case EntityType::Folder:
        return QStringLiteral("folder");
    case EntityType::ComicText:
        return QStringLiteral("comicText");
    case EntityType::RadialLines:
        return QStringLiteral("radialLines");
    case EntityType::FlowLines:
        return QStringLiteral("flowLines");
    case EntityType::BrandWall:
        return QStringLiteral("brandWall");
    case EntityType::HalftoneDots:
        return QStringLiteral("halftoneDots");
    }
    return QStringLiteral("image");
}

bool entityTypeFromName(const QString& text, EntityType* type)
{
    const QString name = text.trimmed().toLower();
    if (name == QLatin1String("image"))
        *type = EntityType::Image;
    else if (name == QLatin1String("video"))
        *type = EntityType::Video;
    else if (name == QLatin1String("text"))
        *type = EntityType::Text;
    else if (name == QLatin1String("vector"))
        *type = EntityType::Vector;
    else if (name == QLatin1String("camera"))
        *type = EntityType::Camera;
    else if (name == QLatin1String("audio"))
        *type = EntityType::Audio;
    else if (name == QLatin1String("folder"))
        *type = EntityType::Folder;
    else if (name == QLatin1String("comictext"))
        *type = EntityType::ComicText;
    else if (name == QLatin1String("radiallines"))
        *type = EntityType::RadialLines;
    else if (name == QLatin1String("flowlines"))
        *type = EntityType::FlowLines;
    else if (name == QLatin1String("brandwall"))
        *type = EntityType::BrandWall;
    else if (name == QLatin1String("halftonedots"))
        *type = EntityType::HalftoneDots;
    else
        return false;
    return true;
}

QString defaultEntityName(EntityType type)
{
    switch (type)
    {
    case EntityType::Text:
        return QStringLiteral("文字");
    case EntityType::Vector:
        return QStringLiteral("形状");
    case EntityType::Camera:
        return QStringLiteral("摄像机");
    case EntityType::Folder:
        return QStringLiteral("文件夹");
    case EntityType::ComicText:
        return QStringLiteral("漫画气泡");
    case EntityType::RadialLines:
        return QStringLiteral("放射线");
    case EntityType::FlowLines:
        return QStringLiteral("流动线");
    case EntityType::BrandWall:
        return QStringLiteral("品牌瓷砖墙");
    case EntityType::HalftoneDots:
        return QStringLiteral("半调网点");
    default:
        return QStringLiteral("实体");
    }
}

bool hasUnsafeControl(const QString& text, bool allowNewlines, QString* error)
{
    for (const QChar character : text)
    {
        const ushort code = character.unicode();
        if (code == 0 || code == 0x7f || (code >= 0x80 && code <= 0x9f))
        {
            if (error)
                *error = QStringLiteral("字符串包含控制字符");
            return true;
        }
        if (code < 0x20)
        {
            const bool lineBreak = allowNewlines
                                   && (code == '\n' || code == '\r' || code == '\t');
            if (!lineBreak)
            {
                if (error)
                    *error = QStringLiteral("字符串包含控制字符");
                return true;
            }
        }
        if ((code >= 0x202a && code <= 0x202e)
            || (code >= 0x2066 && code <= 0x2069))
        {
            if (error)
                *error = QStringLiteral("字符串包含双向文本控制字符");
            return true;
        }
    }
    return false;
}

bool validateName(const QString& name, bool allowEmpty, QString* error)
{
    if (name.isEmpty())
    {
        if (allowEmpty)
            return true;
        if (error)
            *error = QStringLiteral("名称不能为空");
        return false;
    }
    if (name.size() > kMaxNameLength)
    {
        if (error)
            *error = QStringLiteral("名称超过 %1 字符").arg(kMaxNameLength);
        return false;
    }
    if (hasUnsafeControl(name, false, error))
        return false;
    return true;
}

bool validateText(const QString& text, QString* error)
{
    if (text.size() > kMaxTextLength)
    {
        if (error)
            *error = QStringLiteral("字符串超过 %1 字符").arg(kMaxTextLength);
        return false;
    }
    return !hasUnsafeControl(text, true, error);
}

bool rejectUnknownKeys(const QJsonObject& object,
                       std::initializer_list<const char*> allowed, QString* error)
{
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        bool known = false;
        for (const char* key : allowed)
        {
            if (it.key() == QLatin1String(key))
            {
                known = true;
                break;
            }
        }
        if (!known)
        {
            if (error)
                *error = QStringLiteral("未知参数 %1").arg(it.key());
            return false;
        }
    }
    return true;
}

bool readFiniteDouble(const QJsonValue& value, double* out, const QString& label,
                      QString* error)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
    {
        if (error)
            *error = QStringLiteral("%1 必须是有限数字").arg(label);
        return false;
    }
    const double number = value.toDouble();
    if (number < -kMaxValue || number > kMaxValue)
    {
        if (error)
            *error = QStringLiteral("%1 超出允许范围").arg(label);
        return false;
    }
    *out = number;
    return true;
}

bool readInt(const QJsonValue& value, int minimum, int maximum, int* out,
             const QString& label, QString* error)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
    {
        if (error)
            *error = QStringLiteral("%1 必须是整数").arg(label);
        return false;
    }
    const double number = value.toDouble();
    if (std::floor(number) != number || number < minimum || number > maximum)
    {
        if (error)
            *error = QStringLiteral("%1 超出允许范围").arg(label);
        return false;
    }
    *out = int(number);
    return true;
}

bool readEntityId(const QJsonValue& value, bool allowRoot, EntityId* out,
                  const QString& label, QString* error)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
    {
        if (error)
            *error = QStringLiteral("%1 必须是整数实体 id").arg(label);
        return false;
    }
    const double number = value.toDouble();
    if (std::floor(number) != number || number > double(std::numeric_limits<int>::max()))
    {
        if (error)
            *error = QStringLiteral("%1 必须是整数实体 id").arg(label);
        return false;
    }
    const int id = int(number);
    if (id < 1 && !(allowRoot && id == kInvalidEntity))
    {
        if (error)
            *error = QStringLiteral("%1 必须是正整数或 -1").arg(label);
        return false;
    }
    *out = id;
    return true;
}

bool readTransform(const QJsonObject& object, QHash<QString, double>* values,
                   QString* error)
{
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        const QString key = it.key();
        if (key != QLatin1String("x") && key != QLatin1String("y")
            && key != QLatin1String("rotation") && key != QLatin1String("scaleX")
            && key != QLatin1String("scaleY") && key != QLatin1String("rotPointX")
            && key != QLatin1String("rotPointY") && key != QLatin1String("opacity"))
        {
            if (error)
                *error = QStringLiteral("不支持修改变换属性 %1").arg(key);
            return false;
        }
        double value = 0.0;
        if (!readFiniteDouble(it.value(), &value, key, error))
            return false;
        if (key == QLatin1String("opacity") && (value < 0.0 || value > 1.0))
        {
            if (error)
                *error = QStringLiteral("opacity 必须在 0..1 之间");
            return false;
        }
        values->insert(key, value);
    }
    return true;
}

bool lookupEasing(const QString& text, Easing* easing)
{
    const QString name = text.trimmed().toLower();
    for (int index = 0; index <= int(Easing::EaseInOutBounce); ++index)
    {
        if (easingName(Easing(index)) == name)
        {
            *easing = Easing(index);
            return true;
        }
    }
    return false;
}

bool readEasing(const QJsonValue& value, EasingSpec* spec, QString* error)
{
    *spec = EasingSpec();
    if (!value.isString() && !value.isObject())
    {
        if (error)
            *error = QStringLiteral("ease 必须是名称或对象");
        return false;
    }

    if (value.isString())
    {
        if (!lookupEasing(value.toString(), &spec->easing))
        {
            if (error)
                *error = QStringLiteral("未知缓动 %1").arg(value.toString());
            return false;
        }
        return true;
    }

    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        const QString key = it.key();
        if (key != QLatin1String("easing") && key != QLatin1String("name")
            && key != QLatin1String("inX") && key != QLatin1String("inY")
            && key != QLatin1String("outX") && key != QLatin1String("outY"))
        {
            if (error)
                *error = QStringLiteral("未知缓动字段 %1").arg(key);
            return false;
        }
    }

    QString name = object.value(QStringLiteral("easing")).toString();
    if (name.isEmpty())
        name = object.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = QStringLiteral("bezier");
    if (!lookupEasing(name, &spec->easing))
    {
        if (error)
            *error = QStringLiteral("未知缓动 %1").arg(name);
        return false;
    }

    const auto readHandle = [&](const QString& key, double* out) {
        if (!object.contains(key))
            return true;
        return readFiniteDouble(object.value(key), out, key, error);
    };
    if (!readHandle(QStringLiteral("inX"), &spec->inX)
        || !readHandle(QStringLiteral("inY"), &spec->inY)
        || !readHandle(QStringLiteral("outX"), &spec->outX)
        || !readHandle(QStringLiteral("outY"), &spec->outY))
        return false;
    return true;
}

enum class DataKind
{
    Bool,
    Int,
    Double,
    Color,
    Text,
    Colors
};

const QHash<QString, DataKind>& dataKinds()
{
    static const QHash<QString, DataKind> kinds = {
        {QStringLiteral("rotPointCustom"), DataKind::Bool},
        {QStringLiteral("text"), DataKind::Text},
        {QStringLiteral("font"), DataKind::Text},
        {QStringLiteral("size"), DataKind::Int},
        {QStringLiteral("color"), DataKind::Color},
        {QStringLiteral("textStrokeColor"), DataKind::Color},
        {QStringLiteral("textStrokeWidth"), DataKind::Double},
        {QStringLiteral("align"), DataKind::Int},
        {QStringLiteral("lineHeight"), DataKind::Double},
        {QStringLiteral("letterSpacing"), DataKind::Double},
        {QStringLiteral("padding"), DataKind::Double},
        {QStringLiteral("boxWidth"), DataKind::Double},
        {QStringLiteral("boxHeight"), DataKind::Double},
        {QStringLiteral("bold"), DataKind::Bool},
        {QStringLiteral("italic"), DataKind::Bool},
        {QStringLiteral("underline"), DataKind::Bool},
        {QStringLiteral("vertical"), DataKind::Bool},
        {QStringLiteral("verticalRtl"), DataKind::Bool},
        {QStringLiteral("bubbleShape"), DataKind::Int},
        {QStringLiteral("bubbleTail"), DataKind::Bool},
        {QStringLiteral("fill"), DataKind::Color},
        {QStringLiteral("stroke"), DataKind::Color},
        {QStringLiteral("strokeWidth"), DataKind::Double},
        {QStringLiteral("bubbleRadius"), DataKind::Double},
        {QStringLiteral("bubbleTipX"), DataKind::Double},
        {QStringLiteral("bubbleTipY"), DataKind::Double},
        {QStringLiteral("bubbleSeed"), DataKind::Int},
        {QStringLiteral("shape"), DataKind::Int},
        {QStringLiteral("width"), DataKind::Double},
        {QStringLiteral("height"), DataKind::Double},
        {QStringLiteral("cornerRadius"), DataKind::Double},
        {QStringLiteral("sides"), DataKind::Int},
        {QStringLiteral("innerRatio"), DataKind::Double},
        {QStringLiteral("count"), DataKind::Int},
        {QStringLiteral("radius"), DataKind::Double},
        {QStringLiteral("spin"), DataKind::Double},
        {QStringLiteral("flicker"), DataKind::Double},
        {QStringLiteral("length"), DataKind::Double},
        {QStringLiteral("spacing"), DataKind::Double},
        {QStringLiteral("thickness"), DataKind::Double},
        {QStringLiteral("area"), DataKind::Double},
        {QStringLiteral("speed"), DataKind::Double},
        {QStringLiteral("angle"), DataKind::Double},
        {QStringLiteral("dotSize"), DataKind::Double},
        {QStringLiteral("gradientAngle"), DataKind::Double},
        {QStringLiteral("minRatio"), DataKind::Double},
        {QStringLiteral("jitter"), DataKind::Double},
        {QStringLiteral("columns"), DataKind::Int},
        {QStringLiteral("rows"), DataKind::Int},
        {QStringLiteral("gap"), DataKind::Double},
        {QStringLiteral("logoOpacity"), DataKind::Double},
        {QStringLiteral("tileColors"), DataKind::Colors},
        {QStringLiteral("seed"), DataKind::Int},
    };
    return kinds;
}

QSet<QString> allowedDataKeys(EntityType type)
{
    QSet<QString> keys{QStringLiteral("rotPointCustom")};
    const auto add = [&keys](std::initializer_list<const char*> names) {
        for (const char* name : names)
            keys.insert(QString::fromLatin1(name));
    };

    switch (type)
    {
    case EntityType::Text:
        add({"text", "font", "size", "color", "textStrokeColor", "textStrokeWidth",
             "align", "lineHeight", "letterSpacing", "padding", "boxWidth",
             "boxHeight", "bold", "italic", "underline", "vertical", "verticalRtl"});
        break;
    case EntityType::ComicText:
        add({"text", "font", "size", "color", "textStrokeColor", "textStrokeWidth",
             "align", "lineHeight", "letterSpacing", "padding", "boxWidth",
             "boxHeight", "bold", "italic", "underline", "vertical", "verticalRtl",
             "bubbleShape", "bubbleTail", "fill", "stroke", "strokeWidth",
             "bubbleRadius", "bubbleTipX", "bubbleTipY", "bubbleSeed"});
        break;
    case EntityType::Vector:
        add({"shape", "width", "height", "fill", "stroke", "strokeWidth",
             "cornerRadius", "sides", "innerRatio"});
        break;
    case EntityType::RadialLines:
        add({"count", "radius", "innerRatio", "width", "spin", "flicker", "color",
             "seed"});
        break;
    case EntityType::FlowLines:
        add({"count", "length", "spacing", "thickness", "area", "speed", "angle",
             "color", "seed"});
        break;
    case EntityType::HalftoneDots:
        add({"area", "spacing", "dotSize", "angle", "gradientAngle", "minRatio",
             "jitter", "color", "seed"});
        break;
    case EntityType::BrandWall:
        add({"width", "height", "columns", "rows", "gap", "logoOpacity",
             "tileColors"});
        break;
    default:
        break;
    }
    return keys;
}

bool readDataValue(const QString& key, const QJsonValue& value, QVariant* out,
                   QString* error)
{
    const DataKind kind = dataKinds().value(key, DataKind::Text);
    switch (kind)
    {
    case DataKind::Bool:
        if (!value.isBool())
        {
            if (error)
                *error = QStringLiteral("data.%1 必须是布尔值").arg(key);
            return false;
        }
        *out = value.toBool();
        return true;
    case DataKind::Int:
    {
        int number = 0;
        if (!readInt(value, -1000000, 1000000, &number,
                     QStringLiteral("data.%1").arg(key), error))
            return false;
        *out = number;
        return true;
    }
    case DataKind::Double:
    {
        double number = 0.0;
        if (!readFiniteDouble(value, &number, QStringLiteral("data.%1").arg(key),
                              error))
            return false;
        *out = number;
        return true;
    }
    case DataKind::Color:
    {
        if (!value.isString())
        {
            if (error)
                *error = QStringLiteral("data.%1 必须是颜色字符串").arg(key);
            return false;
        }
        const QColor color(value.toString());
        if (!color.isValid())
        {
            if (error)
                *error = QStringLiteral("data.%1 不是有效颜色").arg(key);
            return false;
        }
        *out = color;
        return true;
    }
    case DataKind::Text:
    {
        if (!value.isString())
        {
            if (error)
                *error = QStringLiteral("data.%1 必须是字符串").arg(key);
            return false;
        }
        const QString text = value.toString();
        if (!validateText(text, error))
            return false;
        *out = text;
        return true;
    }
    case DataKind::Colors:
    {
        if (!value.isArray())
        {
            if (error)
                *error = QStringLiteral("data.%1 必须是颜色数组").arg(key);
            return false;
        }
        QStringList colors;
        const QJsonArray array = value.toArray();
        for (const QJsonValue& entry : array)
        {
            if (!entry.isString() || !QColor(entry.toString()).isValid())
            {
                if (error)
                    *error = QStringLiteral("data.%1 包含无效颜色").arg(key);
                return false;
            }
            colors.append(entry.toString());
        }
        *out = colors;
        return true;
    }
    }
    return false;
}

bool readDataMap(EntityType type, const QJsonObject& object, QVariantMap* data,
                 QString* error)
{
    const QSet<QString> allowed = allowedDataKeys(type);
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        const QString key = it.key();
        if (!allowed.contains(key) || !dataKinds().contains(key))
        {
            if (error)
                *error = QStringLiteral("实体类型 %1 不允许 data.%2")
                             .arg(entityTypeName(type), key);
            return false;
        }
        QVariant value;
        if (!readDataValue(key, it.value(), &value, error))
            return false;
        data->insert(key, value);
    }
    return true;
}

bool readKeyframeObject(const QJsonObject& object, Keyframe* keyframe, QString* error)
{
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (it.key() != QLatin1String("frame") && it.key() != QLatin1String("value")
            && it.key() != QLatin1String("ease"))
        {
            if (error)
                *error = QStringLiteral("关键帧包含未知字段 %1").arg(it.key());
            return false;
        }
    }
    if (!object.contains(QStringLiteral("frame"))
        || !object.contains(QStringLiteral("value")))
    {
        if (error)
            *error = QStringLiteral("关键帧必须包含 frame 和 value");
        return false;
    }
    if (!readFiniteDouble(object.value(QStringLiteral("frame")), &keyframe->frame,
                          QStringLiteral("frame"), error))
        return false;
    if (keyframe->frame < 0.0 || keyframe->frame > McpCommandService::kMaxFrame)
    {
        if (error)
            *error = QStringLiteral("frame 必须在 0..%1 之间")
                         .arg(McpCommandService::kMaxFrame);
        return false;
    }
    if (!readFiniteDouble(object.value(QStringLiteral("value")), &keyframe->value,
                          QStringLiteral("value"), error))
        return false;
    if (object.contains(QStringLiteral("ease"))
        && !readEasing(object.value(QStringLiteral("ease")), &keyframe->ease, error))
        return false;
    return true;
}

bool readKeys(const QJsonValue& value, QVector<Keyframe>* keys, QString* error)
{
    if (!value.isArray())
    {
        if (error)
            *error = QStringLiteral("keys 必须是数组");
        return false;
    }
    const QJsonArray array = value.toArray();
    keys->reserve(int(array.size()));
    for (const QJsonValue& entry : array)
    {
        if (!entry.isObject())
        {
            if (error)
                *error = QStringLiteral("关键帧必须是对象");
            return false;
        }
        Keyframe keyframe;
        if (!readKeyframeObject(entry.toObject(), &keyframe, error))
            return false;
        for (const Keyframe& existing : *keys)
        {
            if (std::abs(existing.frame - keyframe.frame) < 1e-6)
            {
                if (error)
                    *error = QStringLiteral("关键帧帧号重复：%1").arg(keyframe.frame);
                return false;
            }
        }
        keys->append(keyframe);
    }
    std::sort(keys->begin(), keys->end(),
              [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
    return true;
}

bool isTransformProperty(const QString& property)
{
    return property == QLatin1String("x") || property == QLatin1String("y")
           || property == QLatin1String("rotation") || property == QLatin1String("scaleX")
           || property == QLatin1String("scaleY") || property == QLatin1String("rotPointX")
           || property == QLatin1String("rotPointY") || property == QLatin1String("opacity");
}

bool readPropertyName(const QJsonValue& value, QString* property, QString* error)
{
    if (!value.isString())
    {
        if (error)
            *error = QStringLiteral("property 必须是字符串");
        return false;
    }
    const QString text = value.toString();
    if (text.isEmpty() || text.size() > 64 || hasUnsafeControl(text, false, error))
    {
        if (error && error->isEmpty())
            *error = QStringLiteral("property 无效");
        return false;
    }
    if (text.startsWith(QStringLiteral("var.")))
    {
        if (error)
            *error = QStringLiteral("HTML 变量轨尚未接入");
        return false;
    }
    if (!isTransformProperty(text))
    {
        if (error)
            *error = QStringLiteral("property %1 不可由 MCP 写入").arg(text);
        return false;
    }
    *property = text;
    return true;
}

bool readTracks(const QJsonObject& object,
                QVector<QPair<QString, QVector<Keyframe>>>* tracks, QString* error)
{
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        QString property;
        if (!readPropertyName(QJsonValue(it.key()), &property, error))
            return false;
        QVector<Keyframe> keys;
        if (!readKeys(it.value(), &keys, error))
            return false;
        tracks->append({property, keys});
    }
    return true;
}

QString historyText(const QString& operation)
{
    if (operation == QLatin1String("entity.create"))
        return QStringLiteral("AI 创建实体");
    if (operation == QLatin1String("entity.update"))
        return QStringLiteral("AI 修改实体");
    if (operation == QLatin1String("keyframes.set"))
        return QStringLiteral("AI 设置关键帧");
    if (operation == QLatin1String("easing.set"))
        return QStringLiteral("AI 调整关键帧缓动");
    if (operation == QLatin1String("entity.move"))
        return QStringLiteral("AI 移动实体");
    if (operation == QLatin1String("folder.create"))
        return QStringLiteral("AI 创建文件夹");
    if (operation == QLatin1String("entities.retire"))
        return QStringLiteral("AI 退休实体");
    if (operation == QLatin1String("scene.create"))
        return QStringLiteral("AI 创建场景");
    if (operation == QLatin1String("batch.run"))
        return QStringLiteral("AI 批量操作");
    return QStringLiteral("AI 编辑");
}

struct SceneCreationRecord
{
    QString sceneId;
    QString name;
    int startFrame = 0;
    int endFrame = 0;
    QColor color;
    QString note;
    EntityId folderId = kInvalidEntity;
};

// One MCP call is one undo step. The service has already applied the change
// when this command is pushed, so the first redo is a no-op. Later redos
// rebuild scenes that were removed by undo.
class McpEditCommand : public UndoCommand
{
public:
    McpEditCommand(QVector<Entity> before, QVector<Entity> after,
                   EntityId beforeCamera, EntityId afterCamera,
                   int beforeView, int afterView,
                   QVector<SceneCreationRecord> createdScenes, QString text)
        : m_before(std::move(before))
        , m_after(std::move(after))
        , m_beforeCamera(beforeCamera)
        , m_afterCamera(afterCamera)
        , m_beforeView(beforeView)
        , m_afterView(afterView)
        , m_createdScenes(std::move(createdScenes))
        , m_text(std::move(text))
    {
    }

    void undo(Project& project) override
    {
        for (const SceneCreationRecord& creation : m_createdScenes)
            project.removeScene(creation.sceneId);
        project.setEntities(m_before);
        project.activeCamera = m_beforeCamera;
        project.monitorView = m_beforeView;
    }

    void redo(Project& project) override
    {
        if (m_skipFirstRedo)
        {
            m_skipFirstRedo = false;
            return;
        }

        project.setEntities(m_after);
        project.activeCamera = m_afterCamera;
        project.monitorView = m_afterView;

        bool foldersChanged = false;
        for (SceneCreationRecord& creation : m_createdScenes)
        {
            if (project.sceneById(creation.sceneId))
                continue;

            QString error;
            Scene* scene = project.addScene(creation.name, creation.startFrame,
                                            creation.endFrame, creation.color,
                                            creation.note, &error);
            if (!scene)
                continue;

            const EntityId oldFolder = creation.folderId;
            const Entity* newFolder = project.entity(scene->folderId);
            if (newFolder)
            {
                const Entity copy = *newFolder;
                for (Entity& entity : m_after)
                {
                    if (entity.id == oldFolder)
                        entity = copy;
                    else if (entity.parent == oldFolder)
                        entity.parent = copy.id;
                }
                foldersChanged = true;
            }
            creation.sceneId = scene->id;
            creation.folderId = scene->folderId;
        }
        if (foldersChanged)
            project.setEntities(m_after);
    }

    QString text() const override { return m_text; }

private:
    QVector<Entity> m_before;
    QVector<Entity> m_after;
    EntityId m_beforeCamera = kInvalidEntity;
    EntityId m_afterCamera = kInvalidEntity;
    int m_beforeView = kWorkCameraView;
    int m_afterView = kWorkCameraView;
    QVector<SceneCreationRecord> m_createdScenes;
    QString m_text;
    bool m_skipFirstRedo = true;
};

} // namespace

McpWriteResult McpWriteResult::success(const QJsonObject& data)
{
    McpWriteResult result;
    result.ok = true;
    result.data = data;
    return result;
}

McpWriteResult McpWriteResult::failure(McpErrorCode code, const QString& message)
{
    McpWriteResult result;
    result.ok = false;
    result.error = code;
    result.message = message;
    return result;
}

QString McpWriteResult::codeName(McpErrorCode code)
{
    switch (code)
    {
    case McpErrorCode::None:
        return QStringLiteral("none");
    case McpErrorCode::InvalidArgument:
        return QStringLiteral("invalid_argument");
    case McpErrorCode::PermissionDenied:
        return QStringLiteral("permission_denied");
    case McpErrorCode::Conflict:
        return QStringLiteral("conflict");
    case McpErrorCode::LimitExceeded:
        return QStringLiteral("limit_exceeded");
    case McpErrorCode::StoppedByHuman:
        return QStringLiteral("stopped_by_human");
    case McpErrorCode::NotAvailable:
        return QStringLiteral("not_available");
    }
    return QStringLiteral("none");
}

QJsonObject McpWriteResult::toJson() const
{
    QJsonObject root;
    root[QStringLiteral("ok")] = ok;
    if (ok)
    {
        root[QStringLiteral("data")] = data;
    }
    else
    {
        QJsonObject errorObject;
        errorObject[QStringLiteral("code")] = codeName(error);
        errorObject[QStringLiteral("message")] = message;
        root[QStringLiteral("error")] = errorObject;
    }
    return root;
}

McpCommandService::McpCommandService(Project* project, History* history,
                                     EntityId sceneRootId, bool armed, bool stopped)
    : m_project(project)
    , m_history(history)
    , m_armed(armed && !stopped)
    , m_stopped(stopped)
    , m_sceneRootId(sceneRootId)
{
    if (m_stopped)
        ++m_epoch;

    if (m_project && sceneRootId != kInvalidEntity)
    {
        const Entity* root = m_project->entity(sceneRootId);
        if (root && root->isFolder())
        {
            m_sceneName = root->name;
            for (const Scene& scene : m_project->scenes())
            {
                if (scene.folderId == sceneRootId)
                {
                    m_sceneId = scene.id;
                    m_sceneName = scene.name;
                    break;
                }
            }
        }
    }
}

bool McpCommandService::arm(EntityId sceneRootId, QString* error)
{
    if (!m_project)
    {
        if (error)
            *error = QStringLiteral("工程不可用");
        return false;
    }
    const Entity* root = m_project->entity(sceneRootId);
    if (!root || !root->isFolder())
    {
        if (error)
            *error = QStringLiteral("解锁范围必须是一个文件夹实体");
        return false;
    }

    m_sceneRootId = sceneRootId;
    m_sceneId.clear();
    m_sceneName = root->name;
    for (const Scene& scene : m_project->scenes())
    {
        if (scene.folderId == sceneRootId)
        {
            m_sceneId = scene.id;
            m_sceneName = scene.name;
            break;
        }
    }
    m_unscoped = false;
    m_armed = true;
    m_stopped = false;
    ++m_epoch;
    return true;
}

bool McpCommandService::armUnscoped(QString* error)
{
    if (!m_project)
    {
        if (error)
            *error = QStringLiteral("工程不可用");
        return false;
    }
    m_sceneRootId = kInvalidEntity;
    m_sceneId.clear();
    m_sceneName = QStringLiteral("未分场景");
    m_unscoped = true;
    m_armed = true;
    m_stopped = false;
    ++m_epoch;
    return true;
}

void McpCommandService::disarm()
{
    m_armed = false;
    ++m_epoch;
}

void McpCommandService::stop()
{
    m_stopped = true;
    m_armed = false;
    ++m_epoch;
}

QStringList McpCommandService::operationNames()
{
    return {QStringLiteral("entity.create"), QStringLiteral("entity.update"),
            QStringLiteral("keyframes.set"), QStringLiteral("easing.set"),
            QStringLiteral("entity.move"), QStringLiteral("folder.create"),
            QStringLiteral("entities.retire"), QStringLiteral("scene.create"),
            QStringLiteral("batch.run")};
}

McpWriteResult McpCommandService::execute(const QString& operation,
                                          const QJsonObject& args)
{
    if (!m_project)
        return McpWriteResult::failure(McpErrorCode::NotAvailable,
                                       QStringLiteral("工程不可用"));
    if (m_stopped)
        return McpWriteResult::failure(McpErrorCode::StoppedByHuman,
                                       QStringLiteral("人类已急停 AI 操作"));
    if (!m_armed)
        return McpWriteResult::failure(McpErrorCode::PermissionDenied,
                                       QStringLiteral("写权限未解锁"));

    if (operation == QLatin1String("batch.run"))
        return executeBatch(args);

    if (operation.startsWith(QStringLiteral("html."))
        || operation == QLatin1String("variables.set_keyframes")
        || operation == QLatin1String("assets.import_blob"))
    {
        return McpWriteResult::failure(McpErrorCode::NotAvailable,
                                       QStringLiteral("HTML/素材写入尚未接入"));
    }
    if (!operationNames().contains(operation))
    {
        return McpWriteResult::failure(
            McpErrorCode::InvalidArgument,
            QStringLiteral("未知写操作 %1").arg(operation));
    }

    const int startEpoch = m_epoch;
    const Snapshot before = capture();
    QVector<SceneCreation> createdScenes;
    McpWriteResult result = applyOperation(operation, args, &createdScenes);
    if (!result.ok)
    {
        restore(before);
        return result;
    }

    McpWriteResult stopped;
    if (!checkTransaction(startEpoch, &stopped))
    {
        restore(before);
        return stopped;
    }

    commit(before, createdScenes, historyText(operation));

    // A Project::changed slot may have called stop() while History::push()
    // re-emitted the new state. Roll the committed step back too.
    if (!checkTransaction(startEpoch, &stopped))
    {
        if (m_history && m_history->canUndo())
            m_history->undo(*m_project);
        else
            restore(before);
        return stopped;
    }
    return result;
}

McpWriteResult McpCommandService::executeBatch(const QJsonObject& args)
{
    QString unknownError;
    if (!rejectUnknownKeys(args, {"operations"}, &unknownError))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, unknownError);

    const QJsonValue operationsValue = args.value(QStringLiteral("operations"));
    if (!operationsValue.isArray())
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       QStringLiteral("batch.run 需要 operations 数组"));
    const QJsonArray operations = operationsValue.toArray();
    if (operations.size() > kMaxBatchOperations)
        return McpWriteResult::failure(
            McpErrorCode::LimitExceeded,
            QStringLiteral("单批最多 %1 个操作").arg(kMaxBatchOperations));
    if (operations.isEmpty())
    {
        QJsonObject data;
        data[QStringLiteral("count")] = 0;
        data[QStringLiteral("results")] = QJsonArray();
        return McpWriteResult::success(data);
    }

    const int startEpoch = m_epoch;
    const Snapshot before = capture();
    QVector<SceneCreation> createdScenes;
    QJsonArray results;

    for (int index = 0; index < int(operations.size()); ++index)
    {
        McpWriteResult stopped;
        if (!checkTransaction(startEpoch, &stopped))
        {
            restore(before);
            return stopped;
        }

        const QJsonValue entry = operations.at(index);
        if (!entry.isObject())
        {
            restore(before);
            return McpWriteResult::failure(
                McpErrorCode::InvalidArgument,
                QStringLiteral("第 %1 个操作不是对象").arg(index + 1));
        }
        const QJsonObject object = entry.toObject();
        if (!rejectUnknownKeys(object, {"op", "operation", "args"}, &unknownError))
        {
            restore(before);
            return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                           unknownError);
        }
        QString operation = object.value(QStringLiteral("op")).toString();
        if (operation.isEmpty())
            operation = object.value(QStringLiteral("operation")).toString();
        if (operation.isEmpty())
        {
            restore(before);
            return McpWriteResult::failure(
                McpErrorCode::InvalidArgument,
                QStringLiteral("第 %1 个操作缺少 op").arg(index + 1));
        }
        if (operation == QLatin1String("batch.run"))
        {
            restore(before);
            return McpWriteResult::failure(
                McpErrorCode::InvalidArgument,
                QStringLiteral("batch.run 不能嵌套"));
        }
        const QJsonValue opArgsValue = object.value(QStringLiteral("args"));
        if (!opArgsValue.isUndefined() && !opArgsValue.isObject())
        {
            restore(before);
            return McpWriteResult::failure(
                McpErrorCode::InvalidArgument,
                QStringLiteral("第 %1 个操作的 args 必须是对象").arg(index + 1));
        }
        const QJsonObject opArgs = opArgsValue.toObject();

        McpWriteResult result = applyOperation(operation, opArgs, &createdScenes);
        if (!result.ok)
        {
            restore(before);
            result.message = QStringLiteral("第 %1 个操作失败：%2")
                                 .arg(index + 1)
                                 .arg(result.message);
            return result;
        }

        QJsonObject entryResult;
        entryResult[QStringLiteral("op")] = operation;
        entryResult[QStringLiteral("data")] = result.data;
        results.append(entryResult);
    }

    McpWriteResult stopped;
    if (!checkTransaction(startEpoch, &stopped))
    {
        restore(before);
        return stopped;
    }

    commit(before, createdScenes, historyText(QStringLiteral("batch.run")));

    if (!checkTransaction(startEpoch, &stopped))
    {
        if (m_history && m_history->canUndo())
            m_history->undo(*m_project);
        else
            restore(before);
        return stopped;
    }

    QJsonObject data;
    data[QStringLiteral("count")] = int(operations.size());
    data[QStringLiteral("results")] = results;
    return McpWriteResult::success(data);
}

McpWriteResult McpCommandService::applyOperation(
    const QString& operation, const QJsonObject& args,
    QVector<SceneCreation>* createdScenes)
{
    if (operation == QLatin1String("entity.create"))
        return createEntity(args, createdScenes);
    if (operation == QLatin1String("entity.update"))
        return updateEntity(args);
    if (operation == QLatin1String("keyframes.set"))
        return setKeyframes(args);
    if (operation == QLatin1String("easing.set"))
        return setEasing(args);
    if (operation == QLatin1String("entity.move"))
        return moveEntity(args);
    if (operation == QLatin1String("folder.create"))
        return createFolder(args);
    if (operation == QLatin1String("entities.retire"))
        return retireEntity(args);
    if (operation == QLatin1String("scene.create"))
        return createScene(args, createdScenes);
    if (operation.startsWith(QStringLiteral("html."))
        || operation == QLatin1String("variables.set_keyframes")
        || operation == QLatin1String("assets.import_blob"))
    {
        return McpWriteResult::failure(McpErrorCode::NotAvailable,
                                       QStringLiteral("HTML/素材写入尚未接入"));
    }
    return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                   QStringLiteral("未知写操作 %1").arg(operation));
}

McpCommandService::Snapshot McpCommandService::capture() const
{
    Snapshot snapshot;
    if (!m_project)
        return snapshot;
    snapshot.entities = m_project->entities();
    snapshot.scenes = m_project->scenes();
    // AI write tools never touch annotations in v1. They are captured with
    // the rest of the transaction so a future annotation tool can roll back
    // without changing the snapshot shape.
    snapshot.annotations = m_project->annotations();
    snapshot.activeCamera = m_project->activeCamera;
    snapshot.monitorView = m_project->monitorView;
    snapshot.modified = m_project->modified;
    return snapshot;
}

void McpCommandService::restore(const Snapshot& snapshot)
{
    if (!m_project)
        return;

    QSet<QString> beforeSceneIds;
    for (const Scene& scene : snapshot.scenes)
        beforeSceneIds.insert(scene.id);

    const QVector<Scene> currentScenes = m_project->scenes();
    for (int index = int(currentScenes.size()) - 1; index >= 0; --index)
    {
        if (!beforeSceneIds.contains(currentScenes[index].id))
            m_project->removeScene(currentScenes[index].id);
    }

    m_project->setEntities(snapshot.entities);
    m_project->activeCamera = snapshot.activeCamera;
    m_project->monitorView = snapshot.monitorView;
    m_project->modified = snapshot.modified;
}

void McpCommandService::commit(const Snapshot& before,
                               const QVector<SceneCreation>& createdScenes,
                               const QString& text)
{
    if (!m_history || !m_project)
        return;

    QVector<SceneCreationRecord> records;
    records.reserve(createdScenes.size());
    for (const SceneCreation& creation : createdScenes)
    {
        SceneCreationRecord record;
        record.sceneId = creation.sceneId;
        record.name = creation.name;
        record.startFrame = creation.startFrame;
        record.endFrame = creation.endFrame;
        record.color = creation.color;
        record.note = creation.note;
        record.folderId = creation.folderId;
        records.append(record);
    }

    auto command = std::make_unique<McpEditCommand>(
        before.entities, m_project->entities(), before.activeCamera,
        m_project->activeCamera, before.monitorView, m_project->monitorView,
        std::move(records), text);
    m_history->push(std::move(command), *m_project);
}

bool McpCommandService::checkWritable(McpWriteResult* error) const
{
    if (!m_project)
    {
        if (error)
            *error = McpWriteResult::failure(McpErrorCode::NotAvailable,
                                             QStringLiteral("工程不可用"));
        return false;
    }
    if (m_stopped)
    {
        if (error)
            *error = McpWriteResult::failure(McpErrorCode::StoppedByHuman,
                                             QStringLiteral("人类已急停 AI 操作"));
        return false;
    }
    if (!m_armed)
    {
        if (error)
            *error = McpWriteResult::failure(McpErrorCode::PermissionDenied,
                                             QStringLiteral("写权限未解锁"));
        return false;
    }
    return true;
}

bool McpCommandService::checkTransaction(int startEpoch, McpWriteResult* error) const
{
    if (!checkWritable(error))
        return false;
    if (m_epoch != startEpoch)
    {
        if (error)
        {
            if (m_stopped)
                *error = McpWriteResult::failure(McpErrorCode::StoppedByHuman,
                                                 QStringLiteral("人类已急停 AI 操作"));
            else
                *error = McpWriteResult::failure(
                    McpErrorCode::PermissionDenied,
                    QStringLiteral("写权限在操作期间发生变化"));
        }
        return false;
    }
    return true;
}

EntityId McpCommandService::rootAncestor(EntityId id) const
{
    if (!m_project || id == kInvalidEntity)
        return kInvalidEntity;

    QSet<EntityId> seen;
    EntityId current = id;
    while (current != kInvalidEntity)
    {
        if (seen.contains(current))
            return kInvalidEntity;
        seen.insert(current);
        const Entity* entity = m_project->entity(current);
        if (!entity)
            return kInvalidEntity;
        if (entity->parent == kInvalidEntity)
            return current;
        current = entity->parent;
    }
    return kInvalidEntity;
}

bool McpCommandService::isGlobalObject(EntityId id) const
{
    if (!m_project)
        return false;
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return false;
    if (entity->isCamera() || entity->isAudio())
        return true;

    // Root entities are global objects (logos, the retired archive, ...)
    // except the scene roots themselves. A scene root is owned by its scene,
    // not by the global unlock.
    if (entity->parent != kInvalidEntity || id == m_sceneRootId)
        return false;
    for (const Scene& scene : m_project->scenes())
        if (scene.folderId == id)
            return false;
    return true;
}

bool McpCommandService::scopeAllows(EntityId id) const
{
    if (!m_project)
        return false;
    if (m_unscoped)
        return true;
    if (m_sceneRootId == kInvalidEntity)
        return false;
    const Entity* root = m_project->entity(m_sceneRootId);
    if (!root || !root->isFolder())
        return false;
    return rootAncestor(id) == m_sceneRootId;
}

bool McpCommandService::ensureEntityWritable(EntityId id, McpWriteResult* error,
                                             bool allowSceneRoot) const
{
    if (!m_project)
    {
        if (error)
            *error = McpWriteResult::failure(McpErrorCode::NotAvailable,
                                             QStringLiteral("工程不可用"));
        return false;
    }
    const Entity* entity = m_project->entity(id);
    if (!entity)
    {
        if (error)
            *error = McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                             QStringLiteral("实体 %1 不存在").arg(id));
        return false;
    }

    if (isGlobalObject(id))
    {
        if (!m_allowGlobal)
        {
            if (error)
                *error = McpWriteResult::failure(
                    McpErrorCode::PermissionDenied,
                    QStringLiteral("全局对象（摄像机/音频/根层对象）需要 allowGlobal"));
            return false;
        }
    }
    else if (!scopeAllows(id))
    {
        if (error)
        {
            const QString scope = m_unscoped ? QStringLiteral("未分场景")
                                             : (m_sceneName.isEmpty()
                                                    ? QStringLiteral("未解锁场景")
                                                    : m_sceneName);
            *error = McpWriteResult::failure(
                McpErrorCode::PermissionDenied,
                QStringLiteral("实体 %1 不在当前解锁范围「%2」内").arg(id).arg(scope));
        }
        return false;
    }

    if (isSceneRoot(id) && !allowSceneRoot)
    {
        if (error)
            *error = McpWriteResult::failure(
                McpErrorCode::PermissionDenied,
                QStringLiteral("场景根文件夹不允许移动或退休"));
        return false;
    }

    if (m_project->isEffectivelyLocked(id))
    {
        if (error)
            *error = McpWriteResult::failure(McpErrorCode::Conflict,
                                             QStringLiteral("实体 %1 已锁定").arg(id));
        return false;
    }
    return true;
}

bool McpCommandService::ensureEntityCapacity(int added, McpWriteResult* error) const
{
    if (!m_project)
    {
        if (error)
            *error = McpWriteResult::failure(McpErrorCode::NotAvailable,
                                             QStringLiteral("工程不可用"));
        return false;
    }
    if (added <= 0)
        return true;
    if (m_project->entities().size() + added > kMaxProjectEntities)
    {
        if (error)
            *error = McpWriteResult::failure(
                McpErrorCode::LimitExceeded,
                QStringLiteral("工程实体数不能超过 %1").arg(kMaxProjectEntities));
        return false;
    }
    return true;
}

McpWriteResult McpCommandService::createEntity(
    const QJsonObject& args, QVector<SceneCreation>* createdScenes)
{
    Q_UNUSED(createdScenes);

    QString unknownError;
    if (!rejectUnknownKeys(args,
                           {"type", "name", "parent", "data", "transform", "frame"},
                           &unknownError))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, unknownError);

    EntityType type = EntityType::Text;
    const QString typeText = args.value(QStringLiteral("type")).toString();
    if (typeText.isEmpty())
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       QStringLiteral("entity.create 需要 type"));
    if (!entityTypeFromName(typeText, &type))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       QStringLiteral("未知实体类型 %1").arg(typeText));

    if (type == EntityType::Image || type == EntityType::Video
        || type == EntityType::Audio)
    {
        return McpWriteResult::failure(
            McpErrorCode::NotAvailable,
            QStringLiteral("MCP v1 不接受文件路径，暂不支持创建 image/video/audio"));
    }
    if (type == EntityType::Camera && !m_allowGlobal)
    {
        return McpWriteResult::failure(
            McpErrorCode::PermissionDenied,
            QStringLiteral("创建摄像机需要 allowGlobal"));
    }

    QString name = args.value(QStringLiteral("name")).toString();
    if (args.contains(QStringLiteral("name"))
        && !args.value(QStringLiteral("name")).isString())
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       QStringLiteral("name 必须是字符串"));
    if (name.isEmpty())
        name = defaultEntityName(type);
    QString nameError;
    if (!validateName(name, false, &nameError))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, nameError);

    QVariantMap data;
    if (args.contains(QStringLiteral("data")))
    {
        const QJsonValue dataValue = args.value(QStringLiteral("data"));
        if (!dataValue.isObject())
            return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                           QStringLiteral("data 必须是对象"));
        QString dataError;
        if (!readDataMap(type, dataValue.toObject(), &data, &dataError))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, dataError);
    }

    QHash<QString, double> transform;
    if (args.contains(QStringLiteral("transform")))
    {
        const QJsonValue transformValue = args.value(QStringLiteral("transform"));
        if (!transformValue.isObject())
            return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                           QStringLiteral("transform 必须是对象"));
        QString transformError;
        if (!readTransform(transformValue.toObject(), &transform, &transformError))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                           transformError);
    }

    bool hasFrame = false;
    double frame = 0.0;
    if (args.contains(QStringLiteral("frame")))
    {
        QString frameError;
        if (!readFiniteDouble(args.value(QStringLiteral("frame")), &frame,
                              QStringLiteral("frame"), &frameError))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, frameError);
        if (frame < 0.0 || frame > kMaxFrame)
            return McpWriteResult::failure(
                McpErrorCode::InvalidArgument,
                QStringLiteral("frame 必须在 0..%1 之间").arg(kMaxFrame));
        hasFrame = true;
    }

    bool parentProvided = args.contains(QStringLiteral("parent"));
    EntityId parent = kInvalidEntity;
    if (parentProvided)
    {
        QString parentError;
        if (!readEntityId(args.value(QStringLiteral("parent")), true, &parent,
                          QStringLiteral("parent"), &parentError))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, parentError);
    }
    else if (!m_unscoped && m_sceneRootId != kInvalidEntity)
    {
        parent = m_sceneRootId;
    }

    if (parent != kInvalidEntity)
    {
        const Entity* parentEntity = m_project->entity(parent);
        if (!parentEntity || !parentEntity->isFolder())
            return McpWriteResult::failure(
                McpErrorCode::InvalidArgument,
                QStringLiteral("parent 必须是已存在的文件夹实体"));
        if (m_project->isEffectivelyLocked(parent))
            return McpWriteResult::failure(McpErrorCode::Conflict,
                                           QStringLiteral("parent 文件夹已锁定"));
        if (!scopeAllows(parent) && !(m_allowGlobal && isGlobalObject(parent)))
        {
            return McpWriteResult::failure(
                McpErrorCode::PermissionDenied,
                QStringLiteral("parent 不在当前解锁范围内"));
        }
    }
    else if (!m_unscoped && !m_allowGlobal)
    {
        return McpWriteResult::failure(
            McpErrorCode::PermissionDenied,
            QStringLiteral("在根层创建对象需要 allowGlobal"));
    }

    McpWriteResult limitError;
    if (!ensureEntityCapacity(1, &limitError))
        return limitError;

    Entity* entity = nullptr;
    if (type == EntityType::Folder)
        entity = m_project->addFolder(name, parent);
    else if (type == EntityType::Camera)
        entity = m_project->addCamera(name);
    else
        entity = m_project->addEntity(type, name, data);

    if (!entity)
        return McpWriteResult::failure(McpErrorCode::Conflict,
                                       QStringLiteral("无法创建实体"));

    if (type != EntityType::Folder && parent != kInvalidEntity)
        m_project->setEntityParent(entity->id, parent);
    if ((type == EntityType::Camera || type == EntityType::Folder) && !data.isEmpty())
        m_project->setEntityDataMap(entity->id, data);

    for (auto it = transform.constBegin(); it != transform.constEnd(); ++it)
        m_project->setValue(entity->id, it.key(), frame, it.value(), hasFrame);

    QJsonObject result;
    result[QStringLiteral("id")] = entity->id;
    result[QStringLiteral("type")] = entityTypeName(type);
    result[QStringLiteral("name")] = entity->name;
    result[QStringLiteral("parent")] = entity->parent;
    return McpWriteResult::success(result);
}

McpWriteResult McpCommandService::updateEntity(const QJsonObject& args)
{
    QString unknownError;
    if (!rejectUnknownKeys(
            args, {"id", "name", "data", "transform", "tracks", "frame", "keyframe"},
            &unknownError))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, unknownError);

    EntityId id = kInvalidEntity;
    QString idError;
    if (!args.contains(QStringLiteral("id"))
        || !readEntityId(args.value(QStringLiteral("id")), false, &id,
                         QStringLiteral("id"), &idError))
    {
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       idError.isEmpty() ? QStringLiteral("entity.update 需要 id")
                                                         : idError);
    }

    McpWriteResult writable;
    if (!ensureEntityWritable(id, &writable, true))
        return writable;

    const bool sceneRoot = isSceneRoot(id);
    const bool hasName = args.contains(QStringLiteral("name"));
    const bool hasData = args.contains(QStringLiteral("data"));
    const bool hasTransform = args.contains(QStringLiteral("transform"));
    const bool hasTracks = args.contains(QStringLiteral("tracks"));
    if (!hasName && !hasData && !hasTransform && !hasTracks)
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       QStringLiteral("entity.update 没有要修改的字段"));
    if (sceneRoot && (hasData || hasTransform || hasTracks))
        return McpWriteResult::failure(
            McpErrorCode::PermissionDenied,
            QStringLiteral("场景根文件夹只允许改名"));

    QString name;
    if (hasName)
    {
        name = args.value(QStringLiteral("name")).toString();
        QString nameError;
        if (!validateName(name, false, &nameError))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, nameError);
    }

    QVariantMap data;
    if (hasData)
    {
        const QJsonValue dataValue = args.value(QStringLiteral("data"));
        if (!dataValue.isObject())
            return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                           QStringLiteral("data 必须是对象"));
        const Entity* entity = m_project->entity(id);
        QString dataError;
        if (!entity
            || !readDataMap(entity->type, dataValue.toObject(), &data, &dataError))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, dataError);
    }

    QHash<QString, double> transform;
    if (hasTransform)
    {
        const QJsonValue transformValue = args.value(QStringLiteral("transform"));
        if (!transformValue.isObject())
            return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                           QStringLiteral("transform 必须是对象"));
        QString transformError;
        if (!readTransform(transformValue.toObject(), &transform, &transformError))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                           transformError);
    }

    QVector<QPair<QString, QVector<Keyframe>>> tracks;
    if (hasTracks)
    {
        const QJsonValue tracksValue = args.value(QStringLiteral("tracks"));
        if (!tracksValue.isObject())
            return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                           QStringLiteral("tracks 必须是对象"));
        const QJsonObject tracksObject = tracksValue.toObject();
        for (auto it = tracksObject.begin(); it != tracksObject.end(); ++it)
        {
            if (it.key().startsWith(QStringLiteral("var.")))
                return McpWriteResult::failure(
                    McpErrorCode::NotAvailable,
                    QStringLiteral("HTML 变量轨尚未接入"));
        }
        QString tracksError;
        if (!readTracks(tracksObject, &tracks, &tracksError))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, tracksError);
    }

    bool hasFrame = false;
    double frame = 0.0;
    if (args.contains(QStringLiteral("frame")))
    {
        QString frameError;
        if (!readFiniteDouble(args.value(QStringLiteral("frame")), &frame,
                              QStringLiteral("frame"), &frameError))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, frameError);
        if (frame < 0.0 || frame > kMaxFrame)
            return McpWriteResult::failure(
                McpErrorCode::InvalidArgument,
                QStringLiteral("frame 必须在 0..%1 之间").arg(kMaxFrame));
        hasFrame = true;
    }
    bool keyframe = hasFrame;
    if (args.contains(QStringLiteral("keyframe")))
    {
        if (!args.value(QStringLiteral("keyframe")).isBool())
            return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                           QStringLiteral("keyframe 必须是布尔值"));
        keyframe = args.value(QStringLiteral("keyframe")).toBool();
    }

    if (hasName)
        m_project->setEntityName(id, name);
    if (hasData)
        m_project->setEntityDataMap(id, data);

    if (hasTracks)
    {
        for (const auto& track : tracks)
        {
            const Entity* entity = m_project->entity(id);
            if (!entity)
                break;
            const QVector<Keyframe> existing = entity->tracks.value(track.first).keys;
            for (const Keyframe& key : existing)
                m_project->removeKeyframe(id, track.first, key.frame);
            for (const Keyframe& key : track.second)
                m_project->addKeyframe(id, track.first, key);
        }
    }

    for (auto it = transform.constBegin(); it != transform.constEnd(); ++it)
        m_project->setValue(id, it.key(), frame, it.value(), keyframe);

    QJsonObject result;
    result[QStringLiteral("id")] = id;
    result[QStringLiteral("name")] = m_project->entity(id) ? m_project->entity(id)->name
                                                          : QString();
    QJsonArray fields;
    if (hasName)
        fields.append(QStringLiteral("name"));
    if (hasData)
        fields.append(QStringLiteral("data"));
    if (hasTransform)
        fields.append(QStringLiteral("transform"));
    if (hasTracks)
        fields.append(QStringLiteral("tracks"));
    result[QStringLiteral("fields")] = fields;
    return McpWriteResult::success(result);
}

McpWriteResult McpCommandService::setKeyframes(const QJsonObject& args)
{
    QString unknownError;
    if (!rejectUnknownKeys(args, {"id", "property", "keys", "frame", "value", "ease"},
                           &unknownError))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, unknownError);

    EntityId id = kInvalidEntity;
    QString error;
    if (!args.contains(QStringLiteral("id"))
        || !readEntityId(args.value(QStringLiteral("id")), false, &id,
                         QStringLiteral("id"), &error))
    {
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       error.isEmpty() ? QStringLiteral("keyframes.set 需要 id")
                                                       : error);
    }

    McpWriteResult writable;
    if (!ensureEntityWritable(id, &writable))
        return writable;

    QString property;
    if (args.contains(QStringLiteral("property"))
        && args.value(QStringLiteral("property")).toString().startsWith(
            QStringLiteral("var.")))
    {
        return McpWriteResult::failure(McpErrorCode::NotAvailable,
                                       QStringLiteral("HTML 变量轨尚未接入"));
    }
    if (!args.contains(QStringLiteral("property"))
        || !readPropertyName(args.value(QStringLiteral("property")), &property, &error))
    {
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       error.isEmpty()
                                           ? QStringLiteral("keyframes.set 需要 property")
                                           : error);
    }

    QVector<Keyframe> keys;
    if (args.contains(QStringLiteral("keys")))
    {
        if (!readKeys(args.value(QStringLiteral("keys")), &keys, &error))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, error);
    }
    else if (args.contains(QStringLiteral("frame"))
             && args.contains(QStringLiteral("value")))
    {
        Keyframe keyframe;
        if (!readFiniteDouble(args.value(QStringLiteral("frame")), &keyframe.frame,
                              QStringLiteral("frame"), &error)
            || !readFiniteDouble(args.value(QStringLiteral("value")), &keyframe.value,
                                 QStringLiteral("value"), &error))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, error);
        if (keyframe.frame < 0.0 || keyframe.frame > kMaxFrame)
            return McpWriteResult::failure(
                McpErrorCode::InvalidArgument,
                QStringLiteral("frame 必须在 0..%1 之间").arg(kMaxFrame));
        if (args.contains(QStringLiteral("ease"))
            && !readEasing(args.value(QStringLiteral("ease")), &keyframe.ease, &error))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, error);
        keys.append(keyframe);
    }
    else
    {
        return McpWriteResult::failure(
            McpErrorCode::InvalidArgument,
            QStringLiteral("keyframes.set 需要 keys 数组或 frame/value"));
    }

    const Entity* entity = m_project->entity(id);
    if (!entity)
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       QStringLiteral("实体 %1 不存在").arg(id));
    const QVector<Keyframe> existing = entity->tracks.value(property).keys;
    for (const Keyframe& key : existing)
        m_project->removeKeyframe(id, property, key.frame);
    for (const Keyframe& key : keys)
        m_project->addKeyframe(id, property, key);

    QJsonObject result;
    result[QStringLiteral("id")] = id;
    result[QStringLiteral("property")] = property;
    result[QStringLiteral("count")] = int(keys.size());
    return McpWriteResult::success(result);
}

McpWriteResult McpCommandService::setEasing(const QJsonObject& args)
{
    QString unknownError;
    if (!rejectUnknownKeys(args, {"id", "property", "frame", "ease"},
                           &unknownError))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, unknownError);

    EntityId id = kInvalidEntity;
    QString error;
    if (!args.contains(QStringLiteral("id"))
        || !readEntityId(args.value(QStringLiteral("id")), false, &id,
                         QStringLiteral("id"), &error))
    {
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       error.isEmpty() ? QStringLiteral("easing.set 需要 id")
                                                       : error);
    }

    McpWriteResult writable;
    if (!ensureEntityWritable(id, &writable))
        return writable;

    QString property;
    if (args.contains(QStringLiteral("property"))
        && args.value(QStringLiteral("property")).toString().startsWith(
            QStringLiteral("var.")))
    {
        return McpWriteResult::failure(McpErrorCode::NotAvailable,
                                       QStringLiteral("HTML 变量轨尚未接入"));
    }
    if (!args.contains(QStringLiteral("property"))
        || !readPropertyName(args.value(QStringLiteral("property")), &property, &error))
    {
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       error.isEmpty()
                                           ? QStringLiteral("easing.set 需要 property")
                                           : error);
    }

    double frame = 0.0;
    if (!args.contains(QStringLiteral("frame"))
        || !readFiniteDouble(args.value(QStringLiteral("frame")), &frame,
                             QStringLiteral("frame"), &error))
    {
        return McpWriteResult::failure(
            McpErrorCode::InvalidArgument,
            error.isEmpty() ? QStringLiteral("easing.set 需要 frame") : error);
    }
    if (frame < 0.0 || frame > kMaxFrame)
        return McpWriteResult::failure(
            McpErrorCode::InvalidArgument,
            QStringLiteral("frame 必须在 0..%1 之间").arg(kMaxFrame));

    EasingSpec ease;
    if (!args.contains(QStringLiteral("ease"))
        || !readEasing(args.value(QStringLiteral("ease")), &ease, &error))
    {
        return McpWriteResult::failure(
            McpErrorCode::InvalidArgument,
            error.isEmpty() ? QStringLiteral("easing.set 需要 ease") : error);
    }

    const Keyframe* keyframe = m_project->keyframe(id, property, frame);
    if (!keyframe)
        return McpWriteResult::failure(
            McpErrorCode::Conflict,
            QStringLiteral("实体 %1 的属性 %2 没有帧 %3 的关键帧")
                .arg(id)
                .arg(property)
                .arg(frame));

    const EasingSpec oldEase = keyframe->ease;
    if (!m_project->setKeyframeEasing(id, property, frame, ease))
        return McpWriteResult::failure(McpErrorCode::Conflict,
                                       QStringLiteral("无法设置关键帧缓动"));

    QJsonObject result;
    result[QStringLiteral("id")] = id;
    result[QStringLiteral("property")] = property;
    result[QStringLiteral("frame")] = frame;
    result[QStringLiteral("oldEase")] = easingName(oldEase.easing);
    result[QStringLiteral("ease")] = easingName(ease.easing);
    return McpWriteResult::success(result);
}

McpWriteResult McpCommandService::moveEntity(const QJsonObject& args)
{
    QString unknownError;
    if (!rejectUnknownKeys(args, {"id", "parent", "index"}, &unknownError))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, unknownError);

    EntityId id = kInvalidEntity;
    QString error;
    if (!args.contains(QStringLiteral("id"))
        || !readEntityId(args.value(QStringLiteral("id")), false, &id,
                         QStringLiteral("id"), &error))
    {
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       error.isEmpty() ? QStringLiteral("entity.move 需要 id")
                                                       : error);
    }

    McpWriteResult writable;
    if (!ensureEntityWritable(id, &writable))
        return writable;

    EntityId parent = m_unscoped ? kInvalidEntity : m_sceneRootId;
    if (args.contains(QStringLiteral("parent")))
    {
        if (!readEntityId(args.value(QStringLiteral("parent")), true, &parent,
                          QStringLiteral("parent"), &error))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, error);
    }

    if (parent != kInvalidEntity)
    {
        const Entity* parentEntity = m_project->entity(parent);
        if (!parentEntity || !parentEntity->isFolder())
            return McpWriteResult::failure(
                McpErrorCode::InvalidArgument,
                QStringLiteral("parent 必须是已存在的文件夹实体"));
        if (m_project->isEffectivelyLocked(parent))
            return McpWriteResult::failure(McpErrorCode::Conflict,
                                           QStringLiteral("parent 文件夹已锁定"));
        if (!scopeAllows(parent)
            && !(m_allowGlobal && isGlobalObject(id) && isGlobalObject(parent)))
        {
            return McpWriteResult::failure(
                McpErrorCode::PermissionDenied,
                QStringLiteral("不能把实体移出当前解锁场景"));
        }
    }
    else if (!m_unscoped
             && !(m_allowGlobal && isGlobalObject(id)))
    {
        return McpWriteResult::failure(
            McpErrorCode::PermissionDenied,
            QStringLiteral("不能把实体移到根层"));
    }

    if (parent != kInvalidEntity && m_project->isDescendantOf(parent, id))
        return McpWriteResult::failure(McpErrorCode::Conflict,
                                       QStringLiteral("不能把实体移入自身子树"));

    int index = int(m_project->entities().size());
    if (args.contains(QStringLiteral("index")))
    {
        if (!readInt(args.value(QStringLiteral("index")), 0,
                     std::numeric_limits<int>::max(), &index,
                     QStringLiteral("index"), &error))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, error);
    }

    if (!m_project->moveEntity(id, parent, index))
        return McpWriteResult::failure(McpErrorCode::Conflict,
                                       QStringLiteral("移动实体失败"));

    QJsonObject result;
    result[QStringLiteral("id")] = id;
    result[QStringLiteral("parent")] = parent;
    result[QStringLiteral("index")] = index;
    return McpWriteResult::success(result);
}

McpWriteResult McpCommandService::createFolder(const QJsonObject& args)
{
    QString unknownError;
    if (!rejectUnknownKeys(args, {"name", "parent"}, &unknownError))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, unknownError);

    QString name = args.value(QStringLiteral("name")).toString();
    if (args.contains(QStringLiteral("name"))
        && !args.value(QStringLiteral("name")).isString())
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       QStringLiteral("name 必须是字符串"));
    if (name.isEmpty())
        name = defaultEntityName(EntityType::Folder);
    QString error;
    if (!validateName(name, false, &error))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, error);

    EntityId parent = m_unscoped ? kInvalidEntity : m_sceneRootId;
    if (args.contains(QStringLiteral("parent")))
    {
        if (!readEntityId(args.value(QStringLiteral("parent")), true, &parent,
                          QStringLiteral("parent"), &error))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, error);
    }

    if (parent != kInvalidEntity)
    {
        const Entity* parentEntity = m_project->entity(parent);
        if (!parentEntity || !parentEntity->isFolder())
            return McpWriteResult::failure(
                McpErrorCode::InvalidArgument,
                QStringLiteral("parent 必须是已存在的文件夹实体"));
        if (m_project->isEffectivelyLocked(parent))
            return McpWriteResult::failure(McpErrorCode::Conflict,
                                           QStringLiteral("parent 文件夹已锁定"));
        if (!scopeAllows(parent) && !(m_allowGlobal && isGlobalObject(parent)))
            return McpWriteResult::failure(
                McpErrorCode::PermissionDenied,
                QStringLiteral("parent 不在当前解锁范围内"));
    }
    else if (!m_unscoped && !m_allowGlobal)
    {
        return McpWriteResult::failure(
            McpErrorCode::PermissionDenied,
            QStringLiteral("在根层创建文件夹需要 allowGlobal"));
    }

    McpWriteResult limitError;
    if (!ensureEntityCapacity(1, &limitError))
        return limitError;

    Entity* folder = m_project->addFolder(name, parent);
    if (!folder)
        return McpWriteResult::failure(McpErrorCode::Conflict,
                                       QStringLiteral("无法创建文件夹"));

    QJsonObject result;
    result[QStringLiteral("id")] = folder->id;
    result[QStringLiteral("name")] = folder->name;
    result[QStringLiteral("parent")] = folder->parent;
    return McpWriteResult::success(result);
}

McpWriteResult McpCommandService::retireEntity(const QJsonObject& args)
{
    QString unknownError;
    if (!rejectUnknownKeys(args, {"id"}, &unknownError))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, unknownError);

    EntityId id = kInvalidEntity;
    QString error;
    if (!args.contains(QStringLiteral("id"))
        || !readEntityId(args.value(QStringLiteral("id")), false, &id,
                         QStringLiteral("id"), &error))
    {
        return McpWriteResult::failure(
            McpErrorCode::InvalidArgument,
            error.isEmpty() ? QStringLiteral("entities.retire 需要 id") : error);
    }

    McpWriteResult writable;
    if (!ensureEntityWritable(id, &writable))
        return writable;

    if (isGlobalObject(id) && !m_allowGlobal)
        return McpWriteResult::failure(
            McpErrorCode::PermissionDenied,
            QStringLiteral("退休全局对象需要 allowGlobal"));

    const Entity* target = m_project->entity(id);
    if (!target)
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       QStringLiteral("实体 %1 不存在").arg(id));

    EntityId retiredRoot = kInvalidEntity;
    for (const Entity& entity : m_project->entities())
    {
        if (entity.isFolder() && entity.parent == kInvalidEntity
            && entity.name == QLatin1String("_retired"))
        {
            retiredRoot = entity.id;
            break;
        }
    }

    const QString sceneFolderName =
        m_sceneName.isEmpty() ? QStringLiteral("未分场景") : m_sceneName;
    EntityId sceneFolder = kInvalidEntity;
    if (retiredRoot != kInvalidEntity)
    {
        for (const Entity& entity : m_project->entities())
        {
            if (entity.isFolder() && entity.parent == retiredRoot
                && entity.name == sceneFolderName)
            {
                sceneFolder = entity.id;
                break;
            }
        }
    }

    int added = 0;
    if (retiredRoot == kInvalidEntity)
        ++added;
    if (sceneFolder == kInvalidEntity)
        ++added;
    McpWriteResult limitError;
    if (!ensureEntityCapacity(added, &limitError))
        return limitError;

    if (retiredRoot == kInvalidEntity)
    {
        Entity* folder = m_project->addFolder(QStringLiteral("_retired"),
                                              kInvalidEntity);
        if (!folder)
            return McpWriteResult::failure(McpErrorCode::Conflict,
                                           QStringLiteral("无法创建 _retired 文件夹"));
        retiredRoot = folder->id;
        // addFolder uniquifies names; the archive name itself is fixed.
        m_project->setEntityName(retiredRoot, QStringLiteral("_retired"));
    }
    if (Entity* root = m_project->entity(retiredRoot))
        root->values[QStringLiteral("opacity")] = 0.0;

    if (sceneFolder == kInvalidEntity)
    {
        Entity* folder = m_project->addFolder(sceneFolderName, retiredRoot);
        if (!folder)
            return McpWriteResult::failure(
                McpErrorCode::Conflict,
                QStringLiteral("无法创建 _retired 场景文件夹"));
        sceneFolder = folder->id;
        // Keep the scene name even when a scene folder with that name exists.
        m_project->setEntityName(sceneFolder, sceneFolderName);
    }
    if (Entity* folder = m_project->entity(sceneFolder))
        folder->values[QStringLiteral("opacity")] = 0.0;

    if (!m_project->moveEntity(id, sceneFolder, int(m_project->entities().size())))
        return McpWriteResult::failure(McpErrorCode::Conflict,
                                       QStringLiteral("无法把实体移入 _retired"));

    if (Entity* entity = m_project->entity(id))
        entity->values[QStringLiteral("opacity")] = 0.0;
    m_project->setValue(id, QStringLiteral("opacity"), 0.0, 0.0, false);
    m_project->setEntityFlag(id, EntityFlag::Locked, true);

    QJsonObject result;
    result[QStringLiteral("id")] = id;
    result[QStringLiteral("retiredFolder")] = sceneFolder;
    result[QStringLiteral("sceneFolder")] = sceneFolderName;
    result[QStringLiteral("opacity")] = 0.0;
    result[QStringLiteral("locked")] = true;
    result[QStringLiteral("deleted")] = false;
    return McpWriteResult::success(result);
}

McpWriteResult McpCommandService::createScene(
    const QJsonObject& args, QVector<SceneCreation>* createdScenes)
{
    QString unknownError;
    if (!rejectUnknownKeys(
            args, {"name", "startFrame", "endFrame", "color", "note"},
            &unknownError))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, unknownError);

    if (!m_unscoped)
    {
        if (m_sceneRootId == kInvalidEntity)
            return McpWriteResult::failure(
                McpErrorCode::PermissionDenied,
                QStringLiteral("没有解锁场景，不能创建场景"));
        const Entity* root = m_project->entity(m_sceneRootId);
        if (!root || !root->isFolder())
            return McpWriteResult::failure(
                McpErrorCode::Conflict,
                QStringLiteral("解锁的场景根文件夹已不存在"));
    }

    int startFrame = -1;
    if (args.contains(QStringLiteral("startFrame")))
    {
        QString error;
        if (!readInt(args.value(QStringLiteral("startFrame")), 0, int(kMaxFrame),
                     &startFrame, QStringLiteral("startFrame"), &error))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, error);
    }
    else if (!m_project->scenes().isEmpty())
    {
        startFrame = m_project->scenes().last().endFrame + 1;
    }
    else
    {
        startFrame = 0;
    }

    int endFrame = m_project->timelineLength();
    if (args.contains(QStringLiteral("endFrame")))
    {
        QString error;
        if (!readInt(args.value(QStringLiteral("endFrame")), 0, int(kMaxFrame),
                     &endFrame, QStringLiteral("endFrame"), &error))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, error);
    }

    if (!m_project->scenes().isEmpty()
        && startFrame <= m_project->scenes().last().endFrame)
    {
        return McpWriteResult::failure(
            McpErrorCode::Conflict,
            QStringLiteral("scene.create 只能追加到现有场景之后"));
    }
    if (endFrame < startFrame)
        return McpWriteResult::failure(
            McpErrorCode::InvalidArgument,
            QStringLiteral("endFrame 不能早于 startFrame"));
    if (endFrame > m_project->timelineLength())
        return McpWriteResult::failure(
            McpErrorCode::Conflict,
            QStringLiteral("场景结束帧 %1 超出时间轴长度 %2")
                .arg(endFrame)
                .arg(m_project->timelineLength()));

    QString name = args.value(QStringLiteral("name")).toString();
    if (args.contains(QStringLiteral("name"))
        && !args.value(QStringLiteral("name")).isString())
        return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                       QStringLiteral("name 必须是字符串"));
    if (name.isEmpty())
        name = QStringLiteral("场景 %1").arg(m_project->scenes().size() + 1);
    QString error;
    if (!validateName(name, false, &error))
        return McpWriteResult::failure(McpErrorCode::InvalidArgument, error);

    QColor color;
    if (args.contains(QStringLiteral("color")))
    {
        const QJsonValue colorValue = args.value(QStringLiteral("color"));
        if (!colorValue.isString() || !QColor(colorValue.toString()).isValid())
            return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                           QStringLiteral("color 不是有效颜色"));
        color = QColor(colorValue.toString());
    }

    QString note;
    if (args.contains(QStringLiteral("note")))
    {
        const QJsonValue noteValue = args.value(QStringLiteral("note"));
        if (!noteValue.isString())
            return McpWriteResult::failure(McpErrorCode::InvalidArgument,
                                           QStringLiteral("note 必须是字符串"));
        note = noteValue.toString();
        if (!validateText(note, &error))
            return McpWriteResult::failure(McpErrorCode::InvalidArgument, error);
    }

    McpWriteResult limitError;
    if (!ensureEntityCapacity(1, &limitError))
        return limitError;

    Scene* scene = m_project->addScene(name, startFrame, endFrame, color, note, &error);
    if (!scene)
        return McpWriteResult::failure(McpErrorCode::Conflict, error);

    SceneCreation creation;
    creation.sceneId = scene->id;
    creation.name = scene->name;
    creation.startFrame = scene->startFrame;
    creation.endFrame = scene->endFrame;
    creation.color = scene->color;
    creation.note = scene->note;
    creation.folderId = scene->folderId;
    createdScenes->append(creation);

    QJsonObject result;
    result[QStringLiteral("id")] = scene->id;
    result[QStringLiteral("folderId")] = scene->folderId;
    result[QStringLiteral("name")] = scene->name;
    result[QStringLiteral("startFrame")] = scene->startFrame;
    result[QStringLiteral("endFrame")] = scene->endFrame;
    return McpWriteResult::success(result);
}

} // namespace pvf
