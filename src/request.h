/*
 * This file is part of buteo-gcontact-plugin package
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

#include <QObject>
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

Q_SIGNALS:
    void finished();
    void syncError(Sync::SyncStatus);

protected Q_SLOTS:
    virtual void slotError(QNetworkReply::NetworkError);
    virtual void slotSslErrors(QList<QSslError>);

protected:
    void debugRequest(const QNetworkRequest &request, const QByteArray &data);
    void debugRequest(const QNetworkRequest &request, const QString &data);
    void debugReply(const QNetworkReply &reply, const QByteArray &data);
    void debugReplyAndReadAll(QNetworkReply *reply);

    QNetworkAccessManager *mNAManager;
    const QString REQUEST_TYPE;
    Settings* mSettings;
};

#endif // REQUEST_H
