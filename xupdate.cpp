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

XUpdate::XUpdate(QObject *pParent) : XThreadObject(pParent)
{
}

void XUpdate::addRecord(const QString &sLocalPath, const QString &sURL)
{
    RECORD record;
    record.sLocalPath = sLocalPath;
    record.sInfoURL = sURL;

    QString sFolderName = QFileInfo(sLocalPath).fileName();
    record.sZipURL = sURL;
    record.sZipURL.replace(QStringLiteral("info.ini"), sFolderName + QStringLiteral(".zip"));

    m_listRecords.append(record);
}

QDate XUpdate::_parseDate(const QString &sContent)
{
    for (const QString &sLine : sContent.split('\n')) {
        QString sL = sLine.trimmed();
        if (sL.startsWith(QStringLiteral("date="))) {
            return QDate::fromString(sL.mid(5).trimmed(), QStringLiteral("yyyy-MM-dd"));
        }
    }
    return QDate();
}

bool XUpdate::_downloadAndUnpack(const RECORD &record)
{
    QString sTempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString sFolderName = QFileInfo(record.sLocalPath).fileName();
    QString sTempZip = sTempDir + "/" + sFolderName + "_update.zip";

    emit infoMessage(tr("Downloading %1...").arg(sFolderName));

    if (!XGitHub::downloadFile(record.sZipURL, sTempZip)) {
        emit errorMessage(tr("Failed to download: %1").arg(record.sZipURL));
        return false;
    }

    emit infoMessage(tr("Extracting %1...").arg(sFolderName));

    // Clear destination and recreate before extracting
    QDir(record.sLocalPath).removeRecursively();
    QDir().mkpath(record.sLocalPath);

    bool bSuccess = false;

    QFile file(sTempZip);
    if (file.open(QIODevice::ReadOnly)) {
        XZip xzip(&file);
        if (xzip.isValid()) {
            XBinary::PDSTRUCT pdStruct = XBinary::createPdStruct();
            QList<XArchive::RECORD> listRecords = xzip.getRecords(-1, &pdStruct);
            // Strip the top-level "foldername/" prefix so files land directly in sLocalPath
            bSuccess = xzip.decompressToPath(&listRecords, sFolderName + "/", record.sLocalPath, &pdStruct);
        } else {
            emit errorMessage(tr("Invalid ZIP archive: %1").arg(sTempZip));
        }
        file.close();
    } else {
        emit errorMessage(tr("Cannot open downloaded ZIP: %1").arg(sTempZip));
    }

    QFile::remove(sTempZip);

    if (bSuccess) {
        emit infoMessage(tr("%1 updated successfully.").arg(sFolderName));
    } else {
        emit errorMessage(tr("Failed to extract %1.").arg(sFolderName));
    }

    return bSuccess;
}

void XUpdate::process()
{
    for (int i = 0; i < m_listRecords.count(); i++) {
        const RECORD &record = m_listRecords.at(i);
        QString sFolderName = QFileInfo(record.sLocalPath).fileName();
        QString sLocalInfoPath = record.sLocalPath + "/info.ini";

        if (!QFile::exists(sLocalInfoPath)) {
            emit warningMessage(tr("No local info.ini for %1, skipping.").arg(sFolderName));
            continue;
        }

        // Read local date
        QFile localFile(sLocalInfoPath);
        if (!localFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            emit warningMessage(tr("Cannot read local info.ini for %1, skipping.").arg(sFolderName));
            continue;
        }
        QDate localDate = _parseDate(QString::fromUtf8(localFile.readAll()));
        localFile.close();

        // Fetch remote info.ini
        emit infoMessage(tr("Checking %1...").arg(sFolderName));
        XGitHub::WEBFILE webFile = XGitHub::getWebFile(record.sInfoURL);

        if (!webFile.bValid) {
            emit warningMessage(tr("Cannot fetch remote info.ini for %1, skipping.").arg(sFolderName));
            continue;
        }

        QDate remoteDate = _parseDate(webFile.sContent);

        if (!remoteDate.isValid()) {
            emit warningMessage(tr("Invalid remote date for %1, skipping.").arg(sFolderName));
            continue;
        }

        if (!localDate.isValid() || remoteDate > localDate) {
            emit infoMessage(tr("Updating %1: local=%2, remote=%3")
                                 .arg(sFolderName)
                                 .arg(localDate.toString(QStringLiteral("yyyy-MM-dd")))
                                 .arg(remoteDate.toString(QStringLiteral("yyyy-MM-dd"))));
            _downloadAndUnpack(record);
        } else {
            emit infoMessage(tr("%1 is up to date (%2).")
                                 .arg(sFolderName)
                                 .arg(localDate.toString(QStringLiteral("yyyy-MM-dd"))));
        }
    }
}
