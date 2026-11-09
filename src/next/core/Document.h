#pragma once

#include "assets/AssetStore.h"
#include "model/Model.h"

#include <QObject>
#include <QString>

namespace pvf::next {

// Shared editing context for the new UI. The app shell implements this with a
// store::ProjectStore + playhead + selection; panels only depend on this
// interface so they can be built and tested without the whole window.
//
// All writes go through commitEntity()/commitProject(): one user-visible
// operation = one store commit. Panels should mutate mutableProject(), then
// call the matching commit method with a "pvf: ..." message.
class Document : public QObject
{
    Q_OBJECT

public:
    explicit Document(QObject* parent = nullptr);
    ~Document() override;

    virtual const model::Project& project() const = 0;
    virtual model::Project& mutableProject() = 0;
    virtual assets::AssetStore& assets() = 0;

    virtual int playhead() const = 0;
    virtual void setPlayhead(int frame) = 0;

    virtual Uuid selection() const = 0;
    virtual void setSelection(const Uuid& uuid) = 0;

    virtual bool commitEntity(const Uuid& uuid, const QString& message,
                              QString* error = nullptr) = 0;
    virtual bool commitProject(const QString& message, QString* error = nullptr) = 0;

    virtual bool undo(QString* error = nullptr) = 0;
    virtual bool redo(QString* error = nullptr) = 0;

signals:
    void changed();
    void selectionChanged();
    void playheadChanged();
};

} // namespace pvf::next
