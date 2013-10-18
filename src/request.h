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
    explicit Request(QNetworkAccessManager *manager, Settings *settings,
                     QString requestType, QObject *parent = 0) :
        QObject(parent), mNAManager(manager), REQUEST_TYPE(requestType), mSettings(settings)
    {    FUNCTION_CALL_TRACE;  }

signals:
    void finished();
    void syncError(Sync::SyncStatus);

protected slots:
    virtual void slotError(QNetworkReply::NetworkError) = 0;
    virtual void slotSslErrors(QList<QSslError>) = 0;

protected:
    void debugRequest(const QNetworkRequest &request, const QByteArray &data) {
        qDebug() << "-----------------------------------------------\n";
        const QList<QByteArray>& rawHeaderList(request.rawHeaderList());
        foreach (QByteArray rawHeader, rawHeaderList) {
            qDebug() << rawHeader << " : " << request.rawHeader(rawHeader);
        }
        qDebug() << "URL = " << request.url();
        qDebug() << "Request : \n" << data;
        qDebug() << "---------------------------------------------------------------------\n";
    }

    QNetworkAccessManager *mNAManager;

    QNetworkReply *mNReply;

    const QString REQUEST_TYPE;

    Settings* mSettings;

};

#endif // REQUEST_H
