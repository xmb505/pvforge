#include "AssetLibraryPanel.h"

#include "AssetLibraryPanelInternal.h"

#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "assets/AssetTypeDetect.h"
#include "model/Query.h"
#include "next/app/ProjectDocument.h"

#include <QAbstractItemView>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <optional>

namespace pvf {

bool AssetLibraryPanel::importPaths(const QStringList& paths, QString* error)
{
    if (error)
        error->clear();
    if (!m_session)
    {
        if (error)
            *error = QStringLiteral("没有打开的工程");
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
            failures << QStringLiteral("%1：文件不存在").arg(rawPath);
            continue;
        }

        const QString path = info.absoluteFilePath();
        const auto type = assets::detectType(path);
        QString importError;
        std::optional<assets::StoredAsset> stored;

        const bool isBundle =
            info.isDir()
            || info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0
            || (type && *type == AssetType::Html);
        if (isBundle)
        {
            stored = m_session->assets().importPvhtml(path, &importError);
        }
        else if (!type)
        {
            failures << QStringLiteral("%1：不支持的素材类型").arg(info.fileName());
            continue;
        }
        else if (*type == AssetType::Video)
        {
            const auto video =
                m_session->assets().importVideoWithAudio(path, &importError);
            if (video)
            {
                stored = video->video;
                if (video->audio)
                    audioSplit = true;
                if (!video->audioError.isEmpty())
                    warnings << QStringLiteral("%1：音频拆分失败（%2）")
                                    .arg(info.fileName(), video->audioError);
            }
        }
        else
        {
            stored = m_session->assets().importFile(path, &importError);
        }

        if (!stored)
        {
            failures << QStringLiteral("%1：%2")
                            .arg(info.fileName(),
                                 importError.isEmpty() ? QStringLiteral("导入失败")
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
        QString message = imported.size() == 1
                              ? QStringLiteral("pvf: import asset \"%1\"")
                                    .arg(imported.first())
                              : QStringLiteral("pvf: import assets \"%1\"")
                                    .arg(imported.join(QStringLiteral("\", \"")));
        QString commitError;
        if (!m_session->document()->commitProject(message, &commitError))
            failures << QStringLiteral("提交失败：%1").arg(commitError);
        emit assetsChanged();

        QString status = imported.size() == 1
                             ? QStringLiteral("已导入素材 \"%1\"").arg(imported.first())
                             : QStringLiteral("已导入 %1 个素材").arg(imported.size());
        if (audioSplit)
            status += QStringLiteral("，已拆出 WAV 音频素材");
        if (!warnings.isEmpty())
            status += QStringLiteral("；") + warnings.join(QStringLiteral("；"));
        if (failures.isEmpty())
            setStatus(status);
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

bool AssetLibraryPanel::renameAsset(const AssetHash& hash, const QString& name,
                                    QString* error)
{
    if (!m_session)
        return false;
    QString storeError;
    if (!m_session->assets().rename(hash, name, &storeError))
    {
        if (error)
            *error = storeError;
        setStatus(storeError);
        return false;
    }
    const QString trimmed = name.trimmed();
    m_session->document()->commitProject(
        QStringLiteral("pvf: rename asset \"%1\"").arg(trimmed), nullptr);
    refresh();
    emit assetsChanged();
    setStatus(QStringLiteral("已重命名素材为 \"%1\"").arg(trimmed));
    return true;
}

bool AssetLibraryPanel::setAssetTags(const AssetHash& hash,
                                     const QStringList& tags, QString* error)
{
    if (!m_session)
        return false;
    QString storeError;
    if (!m_session->assets().setTags(hash, tags, &storeError))
    {
        if (error)
            *error = storeError;
        setStatus(storeError);
        return false;
    }
    m_session->document()->commitProject(
        QStringLiteral("pvf: edit asset tags \"%1\"").arg(hash.left(8)), nullptr);
    refresh();
    emit assetsChanged();
    setStatus(QStringLiteral("已更新素材标签"));
    return true;
}

bool AssetLibraryPanel::deleteAsset(const AssetHash& hash, QString* error)
{
    if (!m_session)
        return false;
    QString name = assetlibdetail::shortHash(hash);
    if (const auto asset = m_session->assets().getIncludingMissing(hash))
        name = asset->name;
    QString storeError;
    if (!m_session->assets().remove(hash, &storeError))
    {
        if (error)
            *error = storeError;
        setStatus(storeError);
        return false;
    }
    m_session->document()->commitProject(
        QStringLiteral("pvf: delete asset \"%1\"").arg(name), nullptr);
    refresh();
    emit assetsChanged();
    setStatus(QStringLiteral("已删除素材 \"%1\"；场景实体保留并显示素材缺失")
                  .arg(name));
    return true;
}

bool AssetLibraryPanel::reimportMissing(const AssetHash& hash, const QString& path,
                                        QString* error)
{
    if (!m_session)
        return false;
    QString importError;
    if (!importPaths({path}, &importError))
    {
        if (error)
            *error = importError;
        return false;
    }
    if (!m_session->assets().get(hash))
    {
        const QString message =
            QStringLiteral("导入的内容与缺失素材不同（期望 %1）").arg(hash.left(8));
        if (error)
            *error = message;
        setStatus(message);
        return false;
    }
    setStatus(QStringLiteral("已恢复素材 %1").arg(hash.left(8)));
    return true;
}

bool AssetLibraryPanel::replaceSelectedEntityWith(const AssetHash& hash,
                                                  QString* error)
{
    if (!m_session || !m_session->document())
    {
        if (error)
            *error = QStringLiteral("没有打开的工程");
        return false;
    }
    const Uuid selected = m_session->document()->selection();
    if (selected.isEmpty())
    {
        if (error)
            *error = QStringLiteral("未选择实体");
        return false;
    }
    if (!m_session->editActions()->replaceAsset(selected, hash, error))
        return false;
    setStatus(QStringLiteral("已更换素材"));
    emit assetsChanged();
    return true;
}

QMimeData* AssetLibraryPanel::assetMimeData(const AssetHash& hash) const
{
    if (!m_session || hash.isEmpty() || !m_session->assets().get(hash))
        return nullptr;
    auto* data = new QMimeData;
    data->setData(QStringLiteral("application/x-pvforge-asset"), hash.toUtf8());
    data->setText(hash);
    return data;
}

void AssetLibraryPanel::chooseSessionImportFiles()
{
    const QString filter = QStringLiteral(
        "支持的素材 (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff *.svg "
        "*.avif *.heic *.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mpeg *.mpg *.wav "
        "*.mp3 *.ogg *.flac *.aac *.m4a *.opus *.zip);;"
        "图片 (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff *.svg *.avif *.heic);;"
        "视频 (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mpeg *.mpg);;"
        "音频 (*.wav *.mp3 *.ogg *.flac *.aac *.m4a *.opus);;"
        "PVForge HTML (*.zip);;所有文件 (*)");
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("导入素材"), m_lastImportDir, filter);
    if (paths.isEmpty())
        return;
    m_lastImportDir = QFileInfo(paths.first()).absolutePath();
    QString error;
    if (!importPaths(paths, &error) && !error.isEmpty())
        setStatus(error);
}

void AssetLibraryPanel::chooseSessionImportHtml()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 .pvhtml 目录"), m_lastImportDir,
        QFileDialog::ShowDirsOnly);
    if (directory.isEmpty())
        return;
    m_lastImportDir = QFileInfo(directory).absolutePath();
    QString error;
    if (!importPaths({directory}, &error) && !error.isEmpty())
        setStatus(error);
}

void AssetLibraryPanel::renameSessionSelected()
{
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;
    const auto asset = m_session ? m_session->assets().get(hash) : std::nullopt;
    if (!asset)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("重命名素材"), QStringLiteral("素材名称："),
        QLineEdit::Normal, asset->name, &ok);
    if (!ok || name.trimmed().isEmpty() || name.trimmed() == asset->name)
        return;
    QString error;
    if (!renameAsset(hash, name, &error) && !error.isEmpty())
        QMessageBox::warning(this, QStringLiteral("重命名失败"), error);
}

void AssetLibraryPanel::editSessionTags()
{
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;
    const auto asset = m_session ? m_session->assets().get(hash) : std::nullopt;
    if (!asset)
        return;
    bool ok = false;
    QString text = QInputDialog::getText(
        this, QStringLiteral("编辑标签"),
        QStringLiteral("标签（用逗号分隔，最多 32 个）："), QLineEdit::Normal,
        asset->tags.join(QStringLiteral(", ")), &ok);
    if (!ok)
        return;
    text.replace(QChar(0xFF0C), QLatin1Char(','));
    QStringList tags;
    for (const QString& part : text.split(QLatin1Char(',')))
    {
        const QString tag = part.trimmed();
        if (!tag.isEmpty())
            tags.append(tag);
    }
    QString error;
    if (!setAssetTags(hash, tags, &error) && !error.isEmpty())
        QMessageBox::warning(this, QStringLiteral("编辑标签失败"), error);
}

void AssetLibraryPanel::deleteSessionSelected()
{
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;
    const auto asset = m_session ? m_session->assets().get(hash) : std::nullopt;
    const QString name =
        asset ? asset->name : assetlibdetail::shortHash(hash);
    const int refs = referenceCount(hash);
    QString text = QStringLiteral("确定删除素材 \"%1\"？").arg(name);
    if (refs > 0)
        text += QStringLiteral("\n\n%1 个实体正在引用它。删除后实体会显示“素材缺失”，"
                               "重新导入相同内容即可恢复。")
                    .arg(refs);
    if (QMessageBox::question(this, QStringLiteral("删除素材"), text,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No)
        != QMessageBox::Yes)
        return;
    QString error;
    if (!deleteAsset(hash, &error) && !error.isEmpty())
        QMessageBox::warning(this, QStringLiteral("删除素材"), error);
}

void AssetLibraryPanel::reimportSessionSelected()
{
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("重新导入缺失素材"), m_lastImportDir,
        QStringLiteral("所有文件 (*)"));
    if (path.isEmpty())
        return;
    m_lastImportDir = QFileInfo(path).absolutePath();
    QString error;
    if (!reimportMissing(hash, path, &error) && !error.isEmpty())
        QMessageBox::warning(this, QStringLiteral("恢复素材失败"), error);
}

void AssetLibraryPanel::replaceSelectedEntityFromMenu()
{
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;
    QString error;
    if (!replaceSelectedEntityWith(hash, &error) && !error.isEmpty())
        setStatus(error);
}

void AssetLibraryPanel::showSessionContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_list ? m_list->itemAt(pos) : nullptr;
    if (!item)
        return;
    m_list->setCurrentItem(item);
    const bool missing = !item->data(Qt::UserRole + 2).toString().isEmpty();
    const QString hash = item->data(Qt::UserRole).toString();

    bool canReplace = false;
    if (m_session && m_session->document())
    {
        const Uuid selected = m_session->document()->selection();
        const auto it = m_session->project().entities.constFind(selected);
        if (it != m_session->project().entities.constEnd())
        {
            AssetType current = AssetType::Image;
            if (it->content.kind == ContentKind::Asset)
                current = it->content.asset.type;
            if (assetlibdetail::assetCompatible(it->content.kind, current,
                                                entryTypeForHash(hash)))
                canReplace = !missing;
        }
    }

    QMenu menu(this);
    QAction* renameAction = menu.addAction(QStringLiteral("重命名…"));
    QAction* tagsAction = menu.addAction(QStringLiteral("编辑标签…"));
    QAction* deleteAction = menu.addAction(QStringLiteral("删除…"));
    QAction* replaceAction = menu.addAction(QStringLiteral("替换选中实体…"));
    QAction* reimportAction = menu.addAction(QStringLiteral("重新导入…"));
    const bool hasStoreEntry =
        m_session && m_session->assets().getIncludingMissing(hash).has_value();
    renameAction->setEnabled(!missing);
    tagsAction->setEnabled(!missing);
    deleteAction->setEnabled(!missing || hasStoreEntry);
    replaceAction->setEnabled(canReplace);
    reimportAction->setVisible(missing);

    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == renameAction)
        renameSessionSelected();
    else if (chosen == tagsAction)
        editSessionTags();
    else if (chosen == deleteAction)
        deleteSessionSelected();
    else if (chosen == replaceAction)
        replaceSelectedEntityFromMenu();
    else if (chosen == reimportAction)
        reimportSessionSelected();
}

void AssetLibraryPanel::createSelectedSession()
{
    if (!m_session || !m_session->editActions())
        return;
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;

    QString error;
    const QVector<Uuid> created = m_session->editActions()->addAssetEntities(
        hash, m_session->playhead(), -1, Uuid(), &error);
    if (created.isEmpty())
    {
        setStatus(error.isEmpty() ? QStringLiteral("无法创建实体") : error);
        return;
    }
    if (m_session->document())
        m_session->document()->setSelection(created.first());
    const auto asset = m_session->assets().get(hash);
    const QString name = asset && !asset->name.isEmpty() ? asset->name : hash;
    setStatus(QStringLiteral("已创建实体 \"%1\"").arg(name));
}

void AssetLibraryPanel::removeSelectedSession()
{
    deleteSessionSelected();
}

QString AssetLibraryPanel::pickAsset(QWidget* parent, EditorSession* session,
                                     ContentKind contentKind, AssetType currentType,
                                     const AssetHash& currentHash)
{
    if (!session)
        return QString();

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("更换素材"));
    auto* layout = new QVBoxLayout(&dialog);

    auto* hint = new QLabel(
        QStringLiteral("从素材库选择，或导入新文件。只显示与当前实体兼容的素材。"),
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* list = new QListWidget(&dialog);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(96, 72));
    list->setGridSize(QSize(136, 110));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setMinimumSize(420, 260);

    auto addAsset = [&list, contentKind, currentType, currentHash](const assets::StoredAsset& asset) {
        if (!assetlibdetail::assetCompatible(contentKind, currentType, asset.type))
            return;
        QPixmap pixmap(96, 72);
        pixmap.fill(QColor(18, 20, 26));
        if (asset.type == AssetType::Image)
        {
            const QStringList files = QDir(asset.directory).entryList(
                {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
            if (!files.isEmpty())
            {
                QImageReader reader(asset.directory + QLatin1Char('/')
                                    + files.first());
                reader.setAutoTransform(true);
                const QImage image = reader.read();
                if (!image.isNull())
                {
                    const QImage scaled = image.scaled(
                        pixmap.size(), Qt::KeepAspectRatio,
                        Qt::SmoothTransformation);
                    QPainter painter(&pixmap);
                    painter.drawImage((pixmap.width() - scaled.width()) / 2,
                                      (pixmap.height() - scaled.height()) / 2,
                                      scaled);
                }
            }
        }
        else if (asset.type == AssetType::Video)
        {
            QPainter painter(&pixmap);
            painter.setPen(QPen(QColor(127, 209, 255), 2));
            painter.drawRect(QRect(24, 22, 48, 28));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(127, 209, 255));
            QPolygon play;
            play << QPoint(42, 28) << QPoint(42, 44) << QPoint(58, 36);
            painter.drawPolygon(play);
        }
        else if (asset.type == AssetType::Audio)
        {
            QPainter painter(&pixmap);
            painter.setPen(QPen(QColor(127, 209, 255), 2));
            for (int x = 16; x < 80; x += 6)
            {
                const int height = 8 + (x * 37 % 40);
                painter.drawLine(QPoint(x, 36 - height / 2),
                                 QPoint(x, 36 + height / 2));
            }
        }
        else
        {
            QPainter painter(&pixmap);
            QFont font = painter.font();
            font.setPixelSize(20);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(QColor(255, 207, 107));
            painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("</>"));
        }

        auto* item = new QListWidgetItem(QIcon(pixmap), asset.name, list);
        item->setData(Qt::UserRole, asset.hash);
        QString tooltip =
            QStringLiteral("%1 · %2\n%3")
                .arg(assetlibdetail::assetTypeLabel(asset.type), asset.name,
                     asset.hash);
        if (asset.hash == currentHash)
            tooltip += QStringLiteral("\n（当前素材）");
        item->setToolTip(tooltip);
    };

    for (const assets::StoredAsset& asset : session->assets().list())
        addAsset(asset);
    if (list->count() > 0)
        list->setCurrentRow(0);
    layout->addWidget(list, 1);

    auto* buttons = new QHBoxLayout();
    auto* importButton = new QPushButton(QStringLiteral("导入新文件…"), &dialog);
    buttons->addWidget(importButton);
    buttons->addStretch(1);
    auto* cancelButton = new QPushButton(QStringLiteral("取消"), &dialog);
    buttons->addWidget(cancelButton);
    auto* useButton = new QPushButton(QStringLiteral("使用选中素材"), &dialog);
    useButton->setDefault(true);
    useButton->setEnabled(list->count() > 0);
    buttons->addWidget(useButton);
    layout->addLayout(buttons);

    QString importedHash;
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(useButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog,
                     [&dialog](QListWidgetItem*) { dialog.accept(); });
    QObject::connect(importButton, &QPushButton::clicked, &dialog, [&] {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("更换素材"), QString(),
            QStringLiteral("支持的素材 (*.png *.jpg *.jpeg *.gif *.bmp *.webp "
                           "*.svg *.mp4 *.mov *.mkv *.webm *.avi *.wav *.mp3 "
                           "*.ogg *.flac *.aac *.m4a *.zip);;所有文件 (*)"));
        if (path.isEmpty())
            return;

        const QFileInfo info(path);
        const auto type = assets::detectType(path);
        QString importError;
        std::optional<assets::StoredAsset> stored;
        const bool isBundle =
            info.isDir()
            || info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0
            || (type && *type == AssetType::Html);
        if (isBundle)
            stored = session->assets().importPvhtml(path, &importError);
        else if (type && *type == AssetType::Video)
        {
            const auto video = session->assets().importVideoWithAudio(path,
                                                                      &importError);
            if (video)
                stored = video->video;
        }
        else if (type)
            stored = session->assets().importFile(path, &importError);

        if (!stored)
        {
            QMessageBox::warning(&dialog, QStringLiteral("导入素材失败"),
                                 importError.isEmpty() ? QStringLiteral("导入失败")
                                                       : importError);
            return;
        }
        if (!assetlibdetail::assetCompatible(contentKind, currentType, stored->type))
        {
            QMessageBox::warning(
                &dialog, QStringLiteral("素材类型不兼容"),
                QStringLiteral("不能把当前实体换成 %1 素材")
                    .arg(assetlibdetail::assetTypeLabel(stored->type)));
            return;
        }
        session->document()->commitProject(
            QStringLiteral("pvf: import asset \"%1\"").arg(stored->name), nullptr);
        importedHash = stored->hash;
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted)
        return QString();
    if (!importedHash.isEmpty())
        return importedHash;

    const QListWidgetItem* item = list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

} // namespace pvf
