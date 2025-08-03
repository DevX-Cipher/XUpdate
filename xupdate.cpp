/* Copyright (c) 2022-2025 hors<horsicq@gmail.com>
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
#include "qdialog.h"
#include "qdialogbuttonbox.h"
#include "qpushbutton.h"
#include "qradiobutton.h"
#include "ui_xupdate.h"
#include "desktopintegrationhelper.h"
#include <zlib.h>
#include <QFile>
#include <QStandardPaths>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSysInfo>
#include <QProcess>
#include <QVBoxLayout>


XUpdate::XUpdate(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::XUpdate)
    , networkManager(new QNetworkAccessManager(this))
{
    ui->setupUi(this);
    DesktopIntegrationHelper::Initialize(this);
    setFixedSize(width(), height());
    ui->label->setVisible(false);

    ui->progressBar->setValue(0);
    ui->progressBar->setRange(0, 100);
}

XUpdate::~XUpdate()
{
    delete ui;
    delete networkManager;
}

void XUpdate::setTargetVersion(const QString &versionTag)
{
    m_targetVersion = versionTag;
    qDebug() << "Update target version set to:" << m_targetVersion;
}

void XUpdate::startUpdate()
{
    QUrl releaseUrl("https://api.github.com/repos/horsicq/DIE-engine/releases");
    QNetworkRequest request(releaseUrl);
    QNetworkReply* reply = networkManager->get(request);

    qDebug() << "Fetching release information for version:" << m_targetVersion;
    connect(reply, &QNetworkReply::finished, this, &XUpdate::handleReleaseInfo);
}

void XUpdate::updateDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesTotal > 0) {
        int progress = static_cast<int>((bytesReceived * 100) / bytesTotal);
        ui->progressBar->setValue(progress);

        if (!ui->label->isVisible())
            ui->label->setVisible(true);

        qDebug() << "Download progress: " << progress << "%";
#ifdef Q_OS_WIN
        DesktopIntegrationHelper::SetProgressState(TBPF_NORMAL);
        DesktopIntegrationHelper::SetProgressValue(static_cast<int>(bytesReceived), static_cast<int>(bytesTotal));
#endif
    }
}

#ifdef Q_OS_WIN
bool XUpdate::replace_self(const void* newImageData, size_t newImageSize)
{
    // Get current executable path using Qt
    QString currentExe = QCoreApplication::applicationFilePath();
    QString backupExe = currentExe + ".backup.exe";
    QString tempExe = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/die_temp.exe";

    // Write new image data to temporary file
    QFile tempFile(tempExe);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open temp file for writing:" << tempFile.errorString();
        return false;
    }
    qint64 bytesWritten = tempFile.write(static_cast<const char*>(newImageData), static_cast<qint64>(newImageSize));
    tempFile.close();

    if (bytesWritten != static_cast<qint64>(newImageSize)) {
        qDebug() << "Failed to write complete image data to temp file";
        QFile::remove(tempExe);
        return false;
    }

    // Backup current executable
    QFile backupFile(backupExe);
    if (backupFile.exists()) {
        if (!backupFile.remove()) {
            qDebug() << "Failed to remove existing backup file:" << backupFile.errorString();
            QFile::remove(tempExe);
            return false;
        }
    }
    QFile currentFile(currentExe);
    if (!currentFile.rename(backupExe)) {
        qDebug() << "Failed to backup current executable:" << currentFile.errorString();
        QFile::remove(tempExe);
        return false;
    }

    // Replace current executable with new version
    if (!tempFile.rename(currentExe)) {
        qDebug() << "Failed to replace current executable:" << tempFile.errorString();
        // Rollback
        QFile::rename(backupExe, currentExe);
        QFile::remove(tempExe);
        return false;
    }

    // Launch the new executable
    QProcess *process = new QProcess;
    process->setProgram(currentExe);
    process->setArguments(QStringList());
    qint64 pid;
    bool launched = process->startDetached(&pid);
    if (!launched) {
        qDebug() << "Failed to relaunch updated executable:" << process->errorString();
        delete process;
        return false;
    }
    qDebug() << "New executable launched with PID:" << pid;
    delete process;

    // Schedule cleanup using Windows API
    std::wstring cleanupCmd = L"cmd /c ping 127.0.0.1 -n 6 >nul & del /F /Q \"" + backupExe.toStdWString() + L"\"";
    qDebug() << "Cleanup command:" << QString::fromStdWString(cleanupCmd);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(
            NULL,
            &cleanupCmd[0],
            NULL, NULL, FALSE,
            CREATE_NO_WINDOW,
            NULL, NULL,
            &si, &pi)) {
        qDebug() << "Failed to start cleanup process for backup deletion. Error:" << GetLastError();
    } else {
        qDebug() << "Cleanup process started for:" << backupExe;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // Log backup file state
    if (QFile::exists(backupExe)) {
        qDebug() << "Backup file still exists before exit:" << backupExe;
    } else {
        qDebug() << "Backup file already deleted:" << backupExe;
    }

    // Quit the current application
    QCoreApplication::quit();
    return true;
}
#endif
#ifdef Q_OS_WIN
void XUpdate::fileDownloaded()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "Download failed:" << reply->errorString();
        reply->deleteLater();
        return;
    }

    qDebug() << "Download completed successfully!";
    QByteArray fileData = reply->readAll();

    QString downloadLocation = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/die_portable.zip";
    QFile file(downloadLocation);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open file for writing:" << downloadLocation;
        reply->deleteLater();
        return;
    }

    file.write(fileData);
    file.close();
    qDebug() << "File saved to:" << downloadLocation;

    QString exeName = "die.exe";
    QString tempExePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/die_temp.exe";

    QFile zip(downloadLocation);
    if (!zip.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open zip file.";
        QFile::remove(downloadLocation);
        reply->deleteLater();
        return;
    }

#pragma pack(push, 1)
    struct ZipLocalFileHeader {
        uint32_t signature;
        uint16_t version_needed;
        uint16_t flags;
        uint16_t compression_method;
        uint16_t mod_time;
        uint16_t mod_date;
        uint32_t crc32;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        uint16_t file_name_length;
        uint16_t extra_field_length;
    };
#pragma pack(pop)

    bool foundExe = false;
    while (!zip.atEnd()) {
        ZipLocalFileHeader header;
        if (zip.read(reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header))
            break;

        if (header.signature != 0x04034b50) {
            qDebug() << "Invalid ZIP signature.";
            break;
        }

        QByteArray fileNameData = zip.read(header.file_name_length);
        QString fileName = QString::fromUtf8(fileNameData);
        zip.seek(zip.pos() + header.extra_field_length);

        if (fileName == exeName && header.compression_method == 8) {
            QByteArray compressedData = zip.read(header.compressed_size);

            std::vector<char> outBuffer(header.uncompressed_size);
            z_stream strm = {};
            strm.next_in = reinterpret_cast<Bytef*>(compressedData.data());
            strm.avail_in = compressedData.size();
            strm.next_out = reinterpret_cast<Bytef*>(outBuffer.data());
            strm.avail_out = outBuffer.size();

            if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
                qDebug() << "inflateInit2 failed";
                break;
            }

            int ret = inflate(&strm, Z_FINISH);
            inflateEnd(&strm);

            if (ret != Z_STREAM_END) {
                qDebug() << "inflate failed:" << ret;
                break;
            }

            QFile outFile(tempExePath);
            if (!outFile.open(QIODevice::WriteOnly)) {
                qDebug() << "Failed to write temp EXE:" << tempExePath;
                break;
            }

            outFile.write(outBuffer.data(), strm.total_out);
            outFile.close();
            foundExe = true;
            qDebug() << "Extracted" << exeName << "to" << tempExePath;

            // Perform self-replacement
            if (!replace_self(outBuffer.data(), strm.total_out)) {
                qDebug() << "Self-update failed";
                QFile::remove(tempExePath);
                break;
            }

            QFile::remove(tempExePath);
            break;
        } else {
            zip.seek(zip.pos() + header.compressed_size);
        }
    }

    zip.close();
    QFile::remove(downloadLocation);

    if (!foundExe)
        qDebug() << "Executable not found in ZIP archive.";

    reply->deleteLater();
}
#endif

void XUpdate::handleReleaseInfo()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
        QJsonArray releases = jsonDoc.array();

        QString osType = QSysInfo::productType();
        QString arch = QSysInfo::currentCpuArchitecture();
        qDebug() << "Detected OS:" << osType;
        qDebug() << "Detected architecture:" << arch;

        QString ubuntuVersion;
        if (osType == "linux") {
            QProcess lsbRelease;
            lsbRelease.start("lsb_release", QStringList() << "-r" << "-s");
            lsbRelease.waitForFinished();
            ubuntuVersion = lsbRelease.readAll().trimmed();
            qDebug() << "Detected Ubuntu version:" << ubuntuVersion;
        }

        QString downloadLink;

        for (const QJsonValue &release : releases) {
            QJsonObject releaseObj = release.toObject();
            QString tagName = releaseObj["tag_name"].toString();

            if (!m_targetVersion.isEmpty() && tagName != m_targetVersion)
                continue;

            QJsonArray assets = releaseObj["assets"].toArray();
            for (const QJsonValue &asset : assets) {
                QString assetName = asset.toObject()["name"].toString();
                qDebug() << "Checking asset:" << assetName;

                if (osType == "windows" && assetName.contains("win") && assetName.contains("64")) {
                    downloadLink = asset.toObject()["browser_download_url"].toString();
                } else if (osType == "linux" && assetName.contains("lin") && assetName.contains(ubuntuVersion)) {
                    if ((arch == "x86_64" && assetName.contains("x86_64")) ||
                        (arch == "arm64" && assetName.contains("arm64"))) {
                        downloadLink = asset.toObject()["browser_download_url"].toString();
                    }
                } else if (osType == "osx" && assetName.contains("mac")) {
                    if ((arch == "x86_64" && assetName.contains("x86_64")) ||
                        (arch == "arm64" && assetName.contains("arm64"))) {
                        downloadLink = asset.toObject()["browser_download_url"].toString();
                    }
                }

                if (!downloadLink.isEmpty())
                    break;
            }

            if (!downloadLink.isEmpty())
                break;
        }

        if (!downloadLink.isEmpty()) {
            QUrl url(downloadLink);
            QNetworkRequest request(url);
            QNetworkReply* downloadReply = networkManager->get(request);

            qDebug() << "Starting download from:" << url.toString();
            connect(downloadReply, &QNetworkReply::downloadProgress, this, &XUpdate::updateDownloadProgress);
            connect(downloadReply, &QNetworkReply::finished, this, &XUpdate::fileDownloaded);
        } else {
            qWarning() << "No suitable release found for the current OS and architecture";
        }
    } else {
        qWarning() << "Failed to fetch release info:" << qPrintable(reply->errorString());
    }

    reply->deleteLater();
}

void XUpdate::showVersionSelectionDialog(const QString &stableVersion, const QString &betaVersion)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Choose Update Version"));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QRadioButton *stableRadio = nullptr;
    QRadioButton *betaRadio = nullptr;

    if (!stableVersion.isEmpty()) {
        stableRadio = new QRadioButton(tr("Stable version: %1").arg(stableVersion));
        layout->addWidget(stableRadio);
    }

    if (!betaVersion.isEmpty()) {
        betaRadio = new QRadioButton(tr("Beta version: %1").arg(betaVersion));
        layout->addWidget(betaRadio);
    }

    if (stableRadio)
        stableRadio->setChecked(true);
    else if (betaRadio)
        betaRadio->setChecked(true);

    QPushButton *updateButton = new QPushButton(tr("Update"));
    QPushButton *cancelButton = new QPushButton(tr("Cancel"));

    QDialogButtonBox *buttonBox = new QDialogButtonBox;
    buttonBox->addButton(updateButton, QDialogButtonBox::AcceptRole);
    buttonBox->addButton(cancelButton, QDialogButtonBox::RejectRole);

    layout->addWidget(buttonBox);

    // Connect buttons to dialog accept/reject
    connect(updateButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QString selectedVersion;
        if (stableRadio && stableRadio->isChecked())
            selectedVersion = stableVersion;
        else if (betaRadio && betaRadio->isChecked())
            selectedVersion = betaVersion;

        if (!selectedVersion.isEmpty()) {
            setTargetVersion(selectedVersion);
            startUpdate();
            show();
        }
    }
}
