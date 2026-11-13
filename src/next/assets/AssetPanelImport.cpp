#include "next/assets/AssetPanel.h"

#include "AssetPanelInternal.h"
#include "assets/AssetTypeDetect.h"

#include <QAction>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSaveFile>
#include <QUrl>

#include <optional>

namespace pvf::next {

bool AssetPanel::importPaths(const QStringList& paths, QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("没有打开的工程");
        return false;
    }

    QStringList imported;
    QStringList failures;
    QStringList warnings;
    bool audioSplit = false;

    for (const QString& rawPath : paths)
    {
        const QFileInfo info(rawPath);
        if (!info.exists())
        {
            failures << tr("%1：文件不存在").arg(rawPath);
            continue;
        }

        const QString path = info.absoluteFilePath();
        const auto type = assets::detectType(path);
        QString importError;
        std::optional<assets::StoredAsset> stored;

        const bool isBundle = info.isDir()
                              || info.suffix().compare(QStringLiteral("zip"),
                                                       Qt::CaseInsensitive) == 0
                              || (type && *type == AssetType::Html);
        if (isBundle)
        {
            stored = m_document->assets().importPvhtml(path, &importError);
        }
        else if (!type)
        {
            failures << tr("%1：不支持的素材类型").arg(info.fileName());
            continue;
        }
        else if (*type == AssetType::Video)
        {
            const auto video = m_document->assets().importVideoWithAudio(path, &importError);
            if (video)
            {
                stored = video->video;
                if (video->audio)
                    audioSplit = true;
                if (!video->audioError.isEmpty())
                    warnings << tr("%1：音频拆分失败（%2）")
                                    .arg(info.fileName(), video->audioError);
            }
        }
        else
        {
            stored = m_document->assets().importFile(path, &importError);
        }

        if (!stored)
        {
            failures << tr("%1：%2").arg(info.fileName(),
                                        importError.isEmpty() ? tr("导入失败")
                                                              : importError);
            continue;
        }
        imported.append(stored->name);
        if (stored->type == AssetType::Video)
            enqueueVideoExtraction(*stored);
    }

    if (!imported.isEmpty())
    {
        refresh();

        QString message;
        if (imported.size() == 1)
            message = QStringLiteral("pvf: import asset \"%1\"").arg(imported.first());
        else
            message = QStringLiteral("pvf: import assets \"%1\"")
                          .arg(imported.join(QStringLiteral("\", \"")));

        QString commitError;
        if (!m_document->commitProject(message, &commitError))
        {
            failures << tr("提交失败：%1").arg(commitError);
        }
        else
        {
            QString status = imported.size() == 1
                                 ? tr("已导入素材 \"%1\"").arg(imported.first())
                                 : tr("已导入 %1 个素材").arg(imported.size());
            if (audioSplit)
                status += tr("，已拆出音频素材");
            if (!warnings.isEmpty())
                status += tr("；%1").arg(warnings.join(QStringLiteral("；")));
            setStatus(status);
        }
        emit assetsChanged();
    }

    if (!failures.isEmpty())
    {
        const QString text = failures.join(QLatin1Char('\n'));
        if (error)
            *error = text;
        setStatus(text);
        return false;
    }
    return true;
}

bool AssetPanel::renameAsset(const AssetHash& hash, const QString& newName,
                             QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("没有打开的工程");
        return false;
    }

    const QString name = newName.trimmed();
    if (name.isEmpty())
    {
        if (error)
            *error = tr("素材名称不能为空");
        return false;
    }

    const auto asset = m_document->assets().get(hash);
    if (!asset)
    {
        if (error)
            *error = tr("素材不存在或内容缺失：%1").arg(hash);
        return false;
    }
    if (asset->name == name)
        return true;

    QJsonObject metadata = asset->metadata;
    metadata.insert(QStringLiteral("name"), name);

    const QString metadataPath = asset->directory + QStringLiteral("/asset.json");
    QSaveFile file(metadataPath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = tr("无法写入 %1：%2").arg(metadataPath, file.errorString());
        return false;
    }
    const QByteArray bytes = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
    {
        if (error)
            *error = tr("无法写入 %1").arg(metadataPath);
        return false;
    }

    QString commitError;
    const bool committed = m_document->commitProject(
        QStringLiteral("pvf: rename asset \"%1\" -> \"%2\"").arg(asset->name, name),
        &commitError);

    refresh();
    emit assetsChanged();
    setStatus(tr("已重命名素材为 \"%1\"").arg(name));

    if (!committed)
    {
        if (error)
            *error = tr("重命名已写入，但提交失败：%1").arg(commitError);
        return false;
    }
    return true;
}

bool AssetPanel::setAssetTags(const AssetHash& hash, const QStringList& tags,
                              QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("没有打开的工程");
        return false;
    }

    const auto asset = m_document->assets().get(hash);
    if (!asset)
    {
        if (error)
            *error = tr("素材不存在或内容缺失：%1").arg(hash);
        return false;
    }

    QString tagsError;
    if (!m_document->assets().setTags(hash, tags, &tagsError))
    {
        if (error)
            *error = tagsError.isEmpty() ? tr("无法写入素材标签：%1").arg(hash)
                                         : tagsError;
        return false;
    }

    QString commitError;
    const bool committed = m_document->commitProject(
        QStringLiteral("pvf: edit asset tags \"%1\"").arg(asset->name), &commitError);

    refresh();
    emit assetsChanged();
    setStatus(tr("已更新素材 \"%1\" 的标签").arg(asset->name));

    if (!committed)
    {
        if (error)
            *error = tr("标签已写入，但提交失败：%1").arg(commitError);
        return false;
    }
    return true;
}

bool AssetPanel::deleteAsset(const AssetHash& hash, QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("没有打开的工程");
        return false;
    }

    QString name = detail::shortHash(hash);
    if (const auto asset = m_document->assets().get(hash))
        name = asset->name;

    QString removeError;
    if (!m_document->assets().remove(hash, &removeError))
    {
        if (error)
            *error = removeError.isEmpty() ? tr("无法删除素材：%1").arg(hash)
                                           : removeError;
        return false;
    }

    QString commitError;
    const bool committed = m_document->commitProject(
        QStringLiteral("pvf: delete asset \"%1\"").arg(name), &commitError);

    refresh();
    emit assetsChanged();
    setStatus(tr("已删除素材 \"%1\"").arg(name));

    if (!committed)
    {
        if (error)
            *error = tr("素材已从磁盘删除，但提交失败：%1").arg(commitError);
        return false;
    }
    return true;
}

QMimeData* AssetPanel::assetMimeData(const AssetHash& hash) const
{
    if (!m_document || hash.isEmpty())
        return nullptr;
    if (!m_document->assets().get(hash))
        return nullptr;

    auto* data = new QMimeData;
    data->setData(QString::fromLatin1(detail::kAssetMimeType), hash.toUtf8());
    data->setText(hash);
    return data;
}

int AssetPanel::referenceCount(const AssetHash& hash) const
{
    if (!m_document)
        return 0;
    int count = 0;
    const auto& entities = m_document->project().entities;
    for (auto it = entities.cbegin(); it != entities.cend(); ++it)
    {
        const model::Content& content = it.value().content;
        if (content.kind == ContentKind::Asset && content.asset.hash == hash)
            ++count;
    }
    return count;
}

void AssetPanel::chooseImportFiles()
{
    const QString filter = tr(
        "支持的素材 (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff *.svg "
        "*.avif *.heic *.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mpeg *.mpg *.wav "
        "*.mp3 *.ogg *.flac *.aac *.m4a *.opus *.zip *.pvhtml);;"
        "图片 (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff *.svg *.avif *.heic);;"
        "视频 (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mpeg *.mpg);;"
        "音频 (*.wav *.mp3 *.ogg *.flac *.aac *.m4a *.opus);;"
        "PVForge HTML (*.zip *.pvhtml);;"
        "所有文件 (*)");

    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("导入素材"), m_lastImportDir, filter);
    if (paths.isEmpty())
        return;

    m_lastImportDir = QFileInfo(paths.first()).absolutePath();
    QString error;
    if (!importPaths(paths, &error) && !error.isEmpty())
        setStatus(error);
}

void AssetPanel::chooseImportPvhtml()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("选择 .pvhtml 目录"), m_lastImportDir, QFileDialog::ShowDirsOnly);
    if (directory.isEmpty())
        return;

    m_lastImportDir = QFileInfo(directory).absolutePath();
    QString error;
    if (!importPaths({directory}, &error) && !error.isEmpty())
        setStatus(error);
}

void AssetPanel::renameSelected()
{
    QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    if (!item || item->data(detail::kMissingRole).toBool())
        return;

    const QString hash = item->data(detail::kHashRole).toString();
    const QString currentName = item->data(detail::kNameRole).toString();
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("重命名素材"), tr("素材名称："), QLineEdit::Normal, currentName, &ok);
    if (!ok || name.trimmed().isEmpty() || name.trimmed() == currentName)
        return;

    QString error;
    if (!renameAsset(hash, name, &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("重命名失败"), error);
}

void AssetPanel::editSelectedTags()
{
    QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    if (!item || item->data(detail::kMissingRole).toBool())
        return;

    const QString hash = item->data(detail::kHashRole).toString();
    QString initial;
    if (m_document)
    {
        if (const auto asset = m_document->assets().get(hash))
            initial = asset->tags.join(QStringLiteral(", "));
    }

    bool ok = false;
    QString text = QInputDialog::getText(this, tr("编辑标签"),
                                         tr("标签（用逗号分隔，最多 32 个）："),
                                         QLineEdit::Normal, initial, &ok);
    if (!ok)
        return;

    text.replace(QChar(0xFF0C), QLatin1Char(',')); // full-width comma
    QStringList tags;
    for (const QString& part : text.split(QLatin1Char(',')))
    {
        const QString tag = part.trimmed();
        if (!tag.isEmpty())
            tags.append(tag);
    }

    QString error;
    if (!setAssetTags(hash, tags, &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("编辑标签失败"), error);
}

void AssetPanel::deleteSelected()
{
    QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    if (!item || item->data(detail::kMissingRole).toBool())
        return;

    const QString hash = item->data(detail::kHashRole).toString();
    const QString name = item->data(detail::kNameRole).toString();
    const int refs = referenceCount(hash);

    QString text = tr("确定删除素材 \"%1\"？").arg(name);
    if (refs > 0)
    {
        text += tr("\n\n%1 个实体正在引用它。删除后这些实体会显示“素材缺失”，"
                   "重新导入相同内容即可恢复。")
                    .arg(refs);
    }
    else
    {
        text += tr("\n\n删除后可通过 git 历史恢复。");
    }

    const auto answer = QMessageBox::question(
        this, tr("删除素材"), text, QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!deleteAsset(hash, &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("删除素材"), error);
}

void AssetPanel::showContextMenu(const QPoint& pos)
{
    if (!m_list)
        return;
    QListWidgetItem* item = m_list->itemAt(pos);
    if (!item)
        return;

    m_list->setCurrentItem(item);
    const bool missing = item->data(detail::kMissingRole).toBool();

    QMenu menu(this);
    QAction* renameAction = menu.addAction(tr("重命名…"));
    QAction* tagsAction = menu.addAction(tr("编辑标签…"));
    QAction* deleteAction = menu.addAction(tr("删除…"));
    renameAction->setEnabled(!missing);
    tagsAction->setEnabled(!missing);
    deleteAction->setEnabled(!missing);

    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == renameAction)
        renameSelected();
    else if (chosen == tagsAction)
        editSelectedTags();
    else if (chosen == deleteAction)
        deleteSelected();
}

void AssetPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
    {
        event->ignore();
        return;
    }
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile())
        {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void AssetPanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

void AssetPanel::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    if (paths.isEmpty())
    {
        event->ignore();
        return;
    }

    QString error;
    if (!importPaths(paths, &error) && !error.isEmpty())
        setStatus(error);
    event->acceptProposedAction();
}

} // namespace pvf::next
