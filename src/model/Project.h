#pragma once

#include "Entity.h"
#include "JsonUtil.h"
#include "Migrations.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace pvf::model {

// The on-disk format version (kProjectFormatVersion) and the format markers
// live in Migrations.h: project.json and entities/*.json share one version.

struct Marker
{
    QString id;
    int frame = 0;
    QString title;
    QString note;
    QStringList tags;
    QString color = QStringLiteral("#ffbe3c");
    // Unknown keys, preserved verbatim so an external tool's annotations
    // survive a load/save cycle.
    QJsonObject extras;
};

struct ProjectSettings
{
    QString name = QStringLiteral("未命名项目");
    int width = 1920;
    int height = 1080;
    int fps = 30;
    QString background = QStringLiteral("#101014");
    QJsonObject extras;
};

// The project file stores settings and markers only. Entities live in
// entities/<uuid>.json and are loaded separately by the caller.
struct Project
{
    ProjectSettings settings;
    QHash<Uuid, Entity> entities;
    QVector<Marker> markers;
    QJsonObject extras;
};

// Parsing defaults to lenient: missing keys fall back to defaults. Opening a
// project uses ParseMode::Strict, where missing required keys are errors that
// name their JSON path.
QJsonObject toJson(const Marker& marker);
std::optional<Marker> markerFromJson(const QJsonObject& object,
                                     const ParseOptions& options,
                                     QString* error = nullptr);
std::optional<Marker> markerFromJson(const QJsonObject& object,
                                     QString* error = nullptr);

QJsonObject toJson(const ProjectSettings& settings);
std::optional<ProjectSettings> projectSettingsFromJson(
    const QJsonObject& object, const ParseOptions& options,
    QString* error = nullptr);
std::optional<ProjectSettings> projectSettingsFromJson(
    const QJsonObject& object, QString* error = nullptr);

// Checks every entity, unique UUIDs, parent existence, parent/child time
// ranges, parent cycles, marker frames and project settings.
bool validateProject(const Project& project, QString* error = nullptr);

// Writes format and the current kProjectFormatVersion. The caller migrates an
// older document to kProjectFormatVersion before calling projectFromJson().
QJsonObject toJson(const Project& project);
std::optional<Project> projectFromJson(const QJsonObject& object,
                                       const ParseOptions& options,
                                       QString* error = nullptr);
std::optional<Project> projectFromJson(const QJsonObject& object,
                                       QString* error = nullptr);

} // namespace pvf::model
