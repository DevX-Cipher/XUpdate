/* Copyright (c) 2022-2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "xupdate.h"
#include "ui_xupdate.h"
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QScrollBar>
#include <QTimer>
#include <QApplication>
#include <QProcess>
#include <QFileInfo>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

XUpdate::XUpdate(QWidget *parent, const QString &outputDir)
    : QMainWindow(parent)
    , ui(new Ui::XUpdate)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_totalFiles(0)
    , m_downloadedFiles(0)
    , m_failedFiles(0)
    , m_isCancelled(false)
    , m_zipDownloadReply(nullptr)
    , m_customOutputDir(outputDir)
{
    ui->setupUi(this);

    // Use custom directory if provided (for MSIX), otherwise use default
    if (!m_customOutputDir.isEmpty()) {
        m_outputDir = m_customOutputDir;
    } else {
        m_outputDir = QCoreApplication::applicationDirPath() + "/DIE_Data";
    }

    // Temp ZIP should always be in a writable location
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_tempZipPath = tempDir + "/temp_repo_die.zip";

    ui->dirDisplay->setText(m_outputDir);

    connect(ui->cancelBtn, &QPushButton::clicked, this, &XUpdate::onCancelDownload);

    log("Automatic downloader initialized");
    log("Target folders: db, yara_rules", true);
    log("Output directory: " + m_outputDir, true);

    QTimer::singleShot(500, this, &XUpdate::checkForUpdates);
}

XUpdate::~XUpdate()
{
    if (QFile::exists(m_tempZipPath)) {
        QFile::remove(m_tempZipPath);
    }
    delete ui;
}

void XUpdate::log(const QString &message, bool debugOnly)
{
#ifdef QT_NO_DEBUG
    // In release mode, skip debug messages
    if (debugOnly) return;
#else
    Q_UNUSED(debugOnly);
#endif

    ui->logText->append(message);
    ui->logText->verticalScrollBar()->setValue(ui->logText->verticalScrollBar()->maximum());
}

void XUpdate::updateStatus(const QString &status)
{
    ui->statusLabel->setText(status);
}

#ifdef Q_OS_WIN
bool XUpdate::downloadFileWithWinHTTP(const QString &url, const QString &outputPath)
{
    QUrl qurl(url);
    QString host = qurl.host();
    QString path = qurl.path();
    if (qurl.hasQuery()) {
        path += "?" + qurl.query();
    }

    std::wstring wHost = host.toStdWString();
    std::wstring wPath = path.toStdWString();

    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    bool success = false;

    log("Initializing WinHTTP...", true);

    // Initialize WinHTTP
    hSession = WinHttpOpen(L"DIE-Downloader/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) {
        log("ERROR: WinHttpOpen failed - Error: " + QString::number(GetLastError()));
        return false;
    }

    // Connect to server
    INTERNET_PORT port = (qurl.scheme() == "https") ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);

    if (!hConnect) {
        log("ERROR: WinHttpConnect failed - Error: " + QString::number(GetLastError()));
        WinHttpCloseHandle(hSession);
        return false;
    }

    log("Connected to: " + host, true);

    // Create request
    DWORD flags = (qurl.scheme() == "https") ? WINHTTP_FLAG_SECURE : 0;
    hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
                                  NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  flags);

    if (!hRequest) {
        log("ERROR: WinHttpOpenRequest failed - Error: " + QString::number(GetLastError()));
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Send request
    log("Sending request...", true);
    if (!WinHttpSendRequest(hRequest,
                            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0,
                            0, 0)) {
        log("ERROR: WinHttpSendRequest failed - Error: " + QString::number(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Receive response
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        log("ERROR: WinHttpReceiveResponse failed - Error: " + QString::number(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    log("Response received", true);

    // Check status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        NULL, &statusCode, &statusCodeSize, NULL);

    log("HTTP Status: " + QString::number(statusCode), true);

    // Handle redirects (GitHub uses 302)
    if (statusCode == 301 || statusCode == 302 || statusCode == 307 || statusCode == 308) {
        DWORD dwSize = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, NULL, NULL, &dwSize, NULL);

        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            wchar_t* szLocation = new wchar_t[dwSize / sizeof(wchar_t)];
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, NULL, szLocation, &dwSize, NULL)) {
                QString redirectUrl = QString::fromWCharArray(szLocation);
                delete[] szLocation;

                log("Following redirect to: " + redirectUrl, true);

                // Cleanup current handles
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);

                // Recursive call with redirect URL
                return downloadFileWithWinHTTP(redirectUrl, outputPath);
            }
            delete[] szLocation;
        }
    }

    if (statusCode != 200) {
        log("ERROR: Unexpected HTTP status code: " + QString::number(statusCode));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Get content length
    DWORD contentLength = 0;
    DWORD contentLengthSize = sizeof(contentLength);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                        NULL, &contentLength, &contentLengthSize, NULL);

    if (contentLength > 0) {
        log("Content length: " + QString::number(contentLength / 1024.0 / 1024.0, 'f', 2) + " MB", true);
    }

    // Open output file
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly)) {
        log("ERROR: Cannot open output file: " + file.errorString());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    log("Downloading data...", true);

    // Download data
    DWORD bytesRead = 0;
    DWORD totalBytesRead = 0;
    char buffer[8192];
    int lastPercentage = -1;

    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        file.write(buffer, bytesRead);
        totalBytesRead += bytesRead;

        // Update progress
        if (contentLength > 0) {
            int percentage = static_cast<int>((static_cast<double>(totalBytesRead) / contentLength) * 80.0);

            if (percentage != lastPercentage) {
                ui->progressBar->setValue(percentage);

                double mbReceived = totalBytesRead / (1024.0 * 1024.0);
                double mbTotal = contentLength / (1024.0 * 1024.0);

                ui->progressBar->setFormat(QString("Downloading: %1 MB / %2 MB (%3%)")
                                               .arg(mbReceived, 0, 'f', 1)
                                               .arg(mbTotal, 0, 'f', 1)
                                               .arg(percentage));

                lastPercentage = percentage;
            }
        } else {
            // Unknown size, just show bytes downloaded
            double mbReceived = totalBytesRead / (1024.0 * 1024.0);
            ui->progressBar->setFormat(QString("Downloading: %1 MB").arg(mbReceived, 0, 'f', 1));
        }

        QApplication::processEvents(); // Keep UI responsive
    }

    file.close();
    success = (totalBytesRead > 0);

    log("Downloaded " + QString::number(totalBytesRead / 1024.0 / 1024.0, 'f', 2) + " MB", true);

    // Cleanup
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return success;
}
#endif

void XUpdate::startAutoDownload()
{
    QDir().mkpath(m_outputDir);

    log("==================================================");
    log("Starting download...");
    log("Downloading repository ZIP using WinHTTP...");
    log("Output directory: " + m_outputDir, true);

    updateStatus("Downloading repository ZIP...");

    QString zipUrl = "https://github.com/horsicq/Detect-It-Easy/archive/refs/heads/master.zip";

#ifdef Q_OS_WIN
    if (downloadFileWithWinHTTP(zipUrl, m_tempZipPath)) {
        log("Download complete");

        QFileInfo fi(m_tempZipPath);
        log("ZIP file saved (" + QString::number(fi.size()) + " bytes)", true);

        // Set progress to 80% (download complete)
        ui->progressBar->setValue(80);
        ui->progressBar->setFormat("Download complete - Preparing extraction...");

        log("Extracting files...");

        // Update to 85% before starting extraction
        ui->progressBar->setValue(85);
        ui->progressBar->setFormat("Extracting files...");

        extractZipFile();
    } else {
        log("ERROR: Download failed!");
        QMessageBox::critical(this, "Error", "Download failed using WinHTTP.\n\nPlease check your internet connection.");
    }
#else
    log("ERROR: WinHTTP only available on Windows");
    QMessageBox::critical(this, "Error", "This feature requires Windows");
#endif
}

void XUpdate::checkForUpdates()
{
    log("==================================================");
    log("Checking for updates...");
    updateStatus("Starting download...");

    // Skip API check since we'd need WinHTTP for that too
    // Just go straight to download
    log("Proceeding to download...", true);

    QTimer::singleShot(100, this, &XUpdate::startAutoDownload);
}

void XUpdate::extractZipFile()
{
    updateStatus("Extracting files...");

    ui->progressBar->setValue(85);
    ui->progressBar->setFormat("Extracting files... (85%)");

    log("Extraction directory: " + m_outputDir, true);

    // Ensure output directory exists
    QDir().mkpath(m_outputDir);

    QProcess *process = new QProcess(this);

    // Create temp extraction path in user's temp directory (for MSIX compatibility)
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString tempExtractPath = tempDir + "/die_temp_extract";

    // Clean up any existing temp extraction folder
    QDir tempExtractDir(tempExtractPath);
    if (tempExtractDir.exists()) {
        tempExtractDir.removeRecursively();
    }

    QString psScript = QString(
                           "$zipPath = '%1'; "
                           "$destPath = '%2'; "
                           "$tempExtract = '%3'; "
                           "Add-Type -AssemblyName System.IO.Compression.FileSystem; "
                           "[System.IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $tempExtract); "
                           "if (Test-Path \"$tempExtract\\Detect-It-Easy-master\\db\") { "
                           "  Copy-Item -Path \"$tempExtract\\Detect-It-Easy-master\\db\" -Destination \"$destPath\\db\" -Recurse -Force; "
                           "} "
                           "if (Test-Path \"$tempExtract\\Detect-It-Easy-master\\yara_rules\") { "
                           "  Copy-Item -Path \"$tempExtract\\Detect-It-Easy-master\\yara_rules\" -Destination \"$destPath\\yara_rules\" -Recurse -Force; "
                           "} "
                           "Remove-Item -Path $tempExtract -Recurse -Force"
                           ).arg(m_tempZipPath)
                           .arg(m_outputDir)
                           .arg(tempExtractPath);

    log("Running extraction...", true);
    log("Temp extraction path: " + tempExtractPath, true);

    // Update progress periodically during extraction
    QTimer *progressTimer = new QTimer(this);
    connect(progressTimer, &QTimer::timeout, this, [this, progressTimer]() {
        int currentValue = ui->progressBar->value();
        if (currentValue < 95) {
            currentValue++;
            ui->progressBar->setValue(currentValue);
            ui->progressBar->setFormat(QString("Extracting files... (%1%)").arg(currentValue));
        }
    });
    progressTimer->start(800);

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        QString output = QString::fromLocal8Bit(process->readAllStandardOutput());
        if (!output.trimmed().isEmpty()) {
            log("PS Output: " + output.trimmed(), true);
        }
    });

    connect(process, &QProcess::readyReadStandardError, this, [this, process]() {
        QString error = QString::fromLocal8Bit(process->readAllStandardError());
        if (!error.trimmed().isEmpty()) {
            log("PS Error: " + error.trimmed(), true);
        }
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, progressTimer, tempExtractPath](int code, QProcess::ExitStatus status) {
                progressTimer->stop();
                progressTimer->deleteLater();

                log("Extraction process finished with code: " + QString::number(code), true);

                if (code == 0 && status == QProcess::NormalExit) {
                    ui->progressBar->setValue(95);
                    ui->progressBar->setFormat("Verifying files... (95%)");

                    log("Extraction successful!");

                    // Verify extraction
                    QDir outputDir(m_outputDir);
                    bool dbExists = outputDir.exists("db");
                    bool yaraExists = outputDir.exists("yara_rules");

                    log("db folder: " + QString(dbExists ? "✓" : "✗"), true);
                    log("yara_rules folder: " + QString(yaraExists ? "✓" : "✗"), true);

                    if (dbExists || yaraExists) {
                        downloadComplete();
                    } else {
                        log("WARNING: Folders not found after extraction!");
                        QMessageBox::warning(this, "Warning", "Extraction completed but folders not found. Please check the output directory.");
                    }
                } else {
                    log("ERROR: Extraction failed!");
                    QMessageBox::critical(this, "Error",
                                          "Extraction failed. Please ensure:\n"
                                          "1. You have write permissions\n"
                                          "2. PowerShell is available\n"
                                          "3. There's enough disk space");
                }

                // Cleanup temp files and folders
                log("Cleaning up...", true);

                if (QFile::exists(m_tempZipPath)) {
                    if (QFile::remove(m_tempZipPath)) {
                        log("Temporary ZIP file removed", true);
                    } else {
                        log("WARNING: Failed to delete ZIP file: " + m_tempZipPath, true);
                    }
                }

                QDir tempExtractDir(tempExtractPath);
                if (tempExtractDir.exists()) {
                    if (tempExtractDir.removeRecursively()) {
                        log("Temporary extraction folder removed", true);
                    } else {
                        log("WARNING: Failed to delete temp extraction folder", true);
                    }
                }

                process->deleteLater();
            });

    process->start("powershell.exe", QStringList() << "-NoProfile" << "-ExecutionPolicy" << "Bypass" << "-Command" << psScript);

    if (!process->waitForStarted(5000)) {
        progressTimer->stop();
        progressTimer->deleteLater();
        log("ERROR: Failed to start PowerShell!");
        log("Attempting fallback extraction method...", true);
        extractZipFileFallback();
        process->deleteLater();
    }
}

void XUpdate::extractZipFileFallback()
{
    log("Using fallback extraction method...", true);

    ui->progressBar->setValue(85);
    ui->progressBar->setFormat("Extracting files (fallback)... (85%)");

    QProcess *process = new QProcess(this);

    QTimer *progressTimer = new QTimer(this);
    connect(progressTimer, &QTimer::timeout, this, [this, progressTimer]() {
        int currentValue = ui->progressBar->value();
        if (currentValue < 95) {
            currentValue++;
            ui->progressBar->setValue(currentValue);
            ui->progressBar->setFormat(QString("Extracting files (fallback)... (%1%)").arg(currentValue));
        }
    });
    progressTimer->start(800);

    QStringList args;
    args << "/c" << "tar" << "-xf" << m_tempZipPath
         << "-C" << m_outputDir
         << "--strip-components=1"
         << "Detect-It-Easy-master/db"
         << "Detect-It-Easy-master/yara_rules";

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, progressTimer](int code, QProcess::ExitStatus) {
                progressTimer->stop();
                progressTimer->deleteLater();

                if (code == 0) {
                    ui->progressBar->setValue(95);
                    ui->progressBar->setFormat("Verifying files... (95%)");
                    log("Extraction successful!");
                    downloadComplete();
                } else {
                    log("ERROR: Extraction failed!");
                    QMessageBox::critical(this, "Error",
                                          "All extraction methods failed. You may need to manually extract:\n" +
                                              m_tempZipPath + "\n\nExtract the 'db' and 'yara_rules' folders to:\n" + m_outputDir);
                }

                log("Cleaning up...", true);
                if (QFile::exists(m_tempZipPath)) {
                    if (QFile::remove(m_tempZipPath)) {
                        log("Temporary files removed", true);
                    } else {
                        log("WARNING: Failed to delete ZIP file", true);
                    }
                }

                process->deleteLater();
            });

    process->start("cmd.exe", args);
}

void XUpdate::downloadComplete()
{
    ui->progressBar->setValue(100);
    ui->progressBar->setFormat("Complete!");
    updateStatus("Download complete!");

    log("==================================================");
    log("Download and extraction complete!");
    log("Files location: " + m_outputDir, true);
    log("==================================================");

    ui->cancelBtn->setText("Close");
    disconnect(ui->cancelBtn, &QPushButton::clicked, this, &XUpdate::onCancelDownload);
    connect(ui->cancelBtn, &QPushButton::clicked, this, &XUpdate::close);

    QMessageBox::information(this, "Done",
                             "Download and extraction complete!\n\n"
                             "Location: " + m_outputDir);
}

void XUpdate::onCancelDownload()
{
    m_isCancelled = true;
    if (m_zipDownloadReply) m_zipDownloadReply->abort();

    ui->cancelBtn->setText("Close");
    disconnect(ui->cancelBtn, &QPushButton::clicked, this, &XUpdate::onCancelDownload);
    connect(ui->cancelBtn, &QPushButton::clicked, this, &XUpdate::close);
}

void XUpdate::countFilesRecursively(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists()) return;

    QFileInfoList entries = dir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs,
        QDir::Name
        );

    for (const QFileInfo &entry : entries) {
        if (entry.isDir())
            countFilesRecursively(entry.absoluteFilePath());
        else
            m_totalFiles++;
    }
}

// Stub implementations for compatibility
void XUpdate::onUpdateCheckFinished()
{
}

void XUpdate::scanDirectory(const QString &path, const QString &localBase)
{
    Q_UNUSED(path);
    Q_UNUSED(localBase);
}

void XUpdate::onPageScanFinished()
{
}

void XUpdate::onApiReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
}

void XUpdate::processNextDownload()
{
}

void XUpdate::onDownloadProgress(qint64 received, qint64 total)
{
    Q_UNUSED(received);
    Q_UNUSED(total);
}

void XUpdate::onFileDownloadFinished(QNetworkReply *reply)
{
    reply->deleteLater();
}

void XUpdate::onZipDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    Q_UNUSED(bytesReceived);
    Q_UNUSED(bytesTotal);
}

void XUpdate::onZipDownloadFinished()
{
}

void XUpdate::fallbackToDirectDownload()
{
}

void XUpdate::extractWithPowerShell()
{
}
