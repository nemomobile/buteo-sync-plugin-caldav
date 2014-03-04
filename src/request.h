/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2013 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Mani Chandrasekar <maninc@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef REQUEST_H
#define REQUEST_H

#include "settings.h"

#include <LogMacros.h>
#include <SyncCommonDefs.h>
#include <SyncResults.h>

#include <QObject>
#include <QPointer>
#include <QNetworkReply>
#include <QSslError>
#include <QNetworkRequest>

class Request : public QObject
{
    Q_OBJECT
public:
    explicit Request(QNetworkAccessManager *manager,
                     Settings *settings,
                     const QString &requestType,
                     QObject *parent = 0);

    QString command() const;
    int errorCode() const;
    QString errorString() const;

Q_SIGNALS:
    void finished();

protected Q_SLOTS:
    virtual void slotSslErrors(QList<QSslError>);

protected:
    bool wasDeleted() const;

    void finishedWithSuccess();
    void finishedWithError(int minorCode, const QString &errorString);
    void finishedWithInternalError();
    void finishedWithReplyResult(QNetworkReply::NetworkError error);

    void debugRequest(const QNetworkRequest &request, const QByteArray &data);
    void debugRequest(const QNetworkRequest &request, const QString &data);
    void debugReply(const QNetworkReply &reply, const QByteArray &data);
    void debugReplyAndReadAll(QNetworkReply *reply);

    QNetworkAccessManager *mNAManager;
    const QString REQUEST_TYPE;
    Settings* mSettings;
    QPointer<Request> mSelfPointer;
    int mMinorCode;
    QString mErrorString;
};

#endif // REQUEST_H
