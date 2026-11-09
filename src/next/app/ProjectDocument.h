#pragma once

#include "next/core/Document.h"
#include "store/Store.h"

#include <memory>

namespace pvf::next {

// Document backed by a store::ProjectStore. Owns the store and exposes the
// playhead/selection state used by every panel.
class ProjectDocument : public Document
{
    Q_OBJECT

public:
    static std::unique_ptr<ProjectDocument> create(const QString& projectDir,
                                                   const model::ProjectSettings& settings,
                                                   QString* error = nullptr);
    static std::unique_ptr<ProjectDocument> open(const QString& projectDir,
                                                 QString* error = nullptr);

    ~ProjectDocument() override;

    const model::Project& project() const override;
    model::Project& mutableProject() override;
    assets::AssetStore& assets() override;

    int playhead() const override;
    void setPlayhead(int frame) override;

    Uuid selection() const override;
    void setSelection(const Uuid& uuid) override;

    bool commitEntity(const Uuid& uuid, const QString& message,
                      QString* error = nullptr) override;
    bool commitProject(const QString& message, QString* error = nullptr) override;

    bool undo(QString* error = nullptr) override;
    bool redo(QString* error = nullptr) override;

    store::ProjectStore* store() { return m_store.get(); }
    const store::ProjectStore* store() const { return m_store.get(); }

    QString root() const;
    void reloadAfterHistory(QString* error = nullptr);

private:
    explicit ProjectDocument(std::unique_ptr<store::ProjectStore> store,
                             QObject* parent = nullptr);

    std::unique_ptr<store::ProjectStore> m_store;
    int m_playhead = 0;
    Uuid m_selection;
};

} // namespace pvf::next
