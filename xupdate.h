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
#ifndef XUPDATE_H
#define XUPDATE_H

#include "xthreadobject.h"
#include "xgithub.h"
#include "xzip.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QStandardPaths>

class XUpdate : public XThreadObject {
    Q_OBJECT

public:
    struct RECORD {
        QString sLocalPath;
        QString sInfoURL;
        QString sZipURL;
    };

    explicit XUpdate(QObject *pParent = nullptr);

    // sURL: direct URL to remote info.ini
    // zip URL is derived by replacing "info.ini" with "{foldername}.zip"
    void addRecord(const QString &sLocalPath, const QString &sURL);

    virtual void process() override;

private:
    static QDate _parseDate(const QString &sContent);
    bool _downloadAndUnpack(const RECORD &record);

    QList<RECORD> m_listRecords;
};

#endif  // XUPDATE_H
