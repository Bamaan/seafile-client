#include <QDir>
#include <sqlite3.h>
#include <errno.h>
#include <stdio.h>
#include <QDateTime>

#include "utils/file-utils.h"
#include "utils/utils.h"
#include "configurator.h"
#include "seafile-applet.h"
#include "file-browser-requests.h"
#include "tasks.h"

#include "data-cache.h"
#include "data-mgr.h"

namespace {

const char *kFileCacheTopDirName = "file-cache";
const int kPasswordCacheExpirationMSecs = 30 * 60 * 1000;

} // namespace

/**
 * Cache loaded dirents. But default cache expires after 1 minute.
 */

DataManager::DataManager(const Account &account)
    : account_(account),
      dirents_cache_(DirentsCache::instance()),
      filecache_db_(FileCacheDB::instance()),
      get_dirents_req_(NULL)
{
}

DataManager::~DataManager()
{
    if (get_dirents_req_)
        delete get_dirents_req_;
}

bool DataManager::getDirents(const QString& repo_id,
                             const QString& path,
                             QList<SeafDirent> *dirents)
{
    QList<SeafDirent> *l = dirents_cache_->getCachedDirents(repo_id, path);
    if (l != 0) {
        dirents->append(*l);
        return true;
    } else {
        return false;
    }
}

void DataManager::getDirentsFromServer(const QString& repo_id,
                                       const QString& path)
{
    if (get_dirents_req_)
        delete get_dirents_req_;

    get_dirents_req_ = new GetDirentsRequest(account_, repo_id, path);
    connect(get_dirents_req_, SIGNAL(success(const QList<SeafDirent>&)),
            this, SLOT(onGetDirentsSuccess(const QList<SeafDirent>&)));
    connect(get_dirents_req_, SIGNAL(failed(const ApiError&)),
            this, SIGNAL(getDirentsFailed(const ApiError&)));
    get_dirents_req_->send();
}

void DataManager::onGetDirentsSuccess(const QList<SeafDirent> &dirents)
{
    dirents_cache_->saveCachedDirents(get_dirents_req_->repoId(),
                                      get_dirents_req_->path(),
                                      dirents);

    emit getDirentsSuccess(dirents);
}

QString DataManager::getLocalCachedFile(const QString& repo_id,
                                        const QString& fpath,
                                        const QString& file_id)
{
    QString local_file_path = getLocalCacheFilePath(repo_id, fpath);
    if (!QFileInfo(local_file_path).exists()) {
        return "";
    }

    QString cached_file_id = filecache_db_->getCachedFileId(repo_id, fpath);
    return cached_file_id == file_id ? local_file_path : "";
}

FileDownloadTask* DataManager::createDownloadTask(const QString& repo_id,
                                                  const QString& path)
{
    QString local_path = getLocalCacheFilePath(repo_id, path);
    FileDownloadTask *task = new FileDownloadTask(account_, repo_id, path, local_path);
    connect(task, SIGNAL(finished(bool)),
            this, SLOT(onFileDownloadFinished(bool)));

    return task;
}

void DataManager::onFileDownloadFinished(bool success)
{
    FileDownloadTask *task = (FileDownloadTask *)sender();
    if (success) {
        filecache_db_->saveCachedFileId(task->repoId(),
                                        task->path(),
                                        task->fileId());
    }
}

FileUploadTask* DataManager::createUploadTask(const QString& repo_id,
                                              const QString& path,
                                              const QString& local_path)
{
    FileUploadTask *task = new FileUploadTask(account_, repo_id, path, local_path);
    return task;
}

QString DataManager::getLocalCacheFilePath(const QString& repo_id,
                                        const QString& path)
{
    QString seafdir = seafApplet->configurator()->seafileDir();
    return ::pathJoin(seafdir, kFileCacheTopDirName, repo_id, path);
}

QHash<QString, qint64> DataManager::passwords_cache_;

bool DataManager::isRepoPasswordSet(const QString& repo_id) const
{
    if (!passwords_cache_.contains(repo_id)) {
        return false;
    }
    qint64 expiration_time = passwords_cache_[repo_id];
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    return now < expiration_time;
}

void DataManager::setRepoPasswordSet(const QString& repo_id)
{
    passwords_cache_[repo_id] =
        QDateTime::currentMSecsSinceEpoch() + kPasswordCacheExpirationMSecs;
}

QString DataManager::getRepoCacheFolder(const QString& repo_id) const
{
    QString seafdir = seafApplet->configurator()->seafileDir();
    return ::pathJoin(seafdir, kFileCacheTopDirName, repo_id);
}
