#pragma once

#include "Entity.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace pvf::model {

inline constexpr int kProjectFormatVersion = 1;
inline constexpr char kProjectFormat[] = "pvforge-project";

struct Marker
{
    QString id;
    int frame = 0;
    QString title;
    QString note;
    QStringList tags;
    QString color = QStringLiteral("#ffbe3c");
};

struct ProjectSettings
{
    QString name = QStringLiteral("未命名项目");
    int width = 1920;
    int height = 1080;
    int fps = 30;
    QString background = QStringLiteral("#101014");
};

// The project file stores settings and markers only. Entities live in
// entities/<uuid>.json and are loaded separately by the caller.
struct Project
{
    ProjectSettings settings;
    QHash<Uuid, Entity> entities;
    QVector<Marker> markers;
};

QJsonObject toJson(const Marker& marker);
std::optional<Marker> markerFromJson(const QJsonObject& object,
                                     QString* error = nullptr);

QJsonObject toJson(const ProjectSettings& settings);
std::optional<ProjectSettings> projectSettingsFromJson(const QJsonObject& object,
                                                       QString* error = nullptr);

// Checks every entity, unique UUIDs, parent existence, parent/child time
// ranges, parent cycles, marker frames and project settings.
bool validateProject(const Project& project, QString* error = nullptr);

QJsonObject toJson(const Project& project);
std::optional<Project> projectFromJson(const QJsonObject& object,
                                       QString* error = nullptr);

} // namespace pvf::model
