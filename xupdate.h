#ifndef XUPDATE_H
#define XUPDATE_H

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winhttp.h>
#endif

QT_BEGIN_NAMESPACE
namespace Ui { class XUpdate; }
QT_END_NAMESPACE

class XUpdate : public QMainWindow
{
    Q_OBJECT

public:
    explicit XUpdate(QWidget *parent = nullptr, const QString &outputDir = QString());
    ~XUpdate();

private slots:
    void checkForUpdates();
    void onUpdateCheckFinished();
    void startAutoDownload();
    void onCancelDownload();
    void onZipDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onZipDownloadFinished();
    void onApiReplyFinished(QNetworkReply *reply);
    void onDownloadProgress(qint64 received, qint64 total);
    void onFileDownloadFinished(QNetworkReply *reply);
    void onPageScanFinished();

private:
    void log(const QString &message, bool debugOnly = false);
    void updateStatus(const QString &status);
    void extractZipFile();
    void extractWithPowerShell();
    void countFilesRecursively(const QString &dirPath);
    void downloadComplete();
    void fallbackToDirectDownload();
    void extractZipFileFallback();
    void scanDirectory(const QString &path, const QString &localBase);
    void processNextDownload();

#ifdef Q_OS_WIN
    bool downloadFileWithWinHTTP(const QString &url, const QString &outputPath);
#endif

    Ui::XUpdate *ui;
    QNetworkAccessManager *m_networkManager;
    QString m_outputDir;
    QString m_tempZipPath;
    QString m_latestCommitSha;
    QNetworkReply *m_zipDownloadReply;
    int m_totalFiles = 0;
    int m_downloadedFiles = 0;
    int m_failedFiles = 0;
    bool m_isCancelled = false;
    QString m_customOutputDir;
};

#endif // XUPDATE_H
