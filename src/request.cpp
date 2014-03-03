/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2013 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Bea Lam <bea.lam@jollamobile.com>
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

#include "request.h"

Request::Request(QNetworkAccessManager *manager,
                 Settings *settings,
                 const QString &requestType,
                 QObject *parent)
    : QObject(parent)
    , mNAManager(manager)
    , REQUEST_TYPE(requestType)
    , mSettings(settings)
{
    FUNCTION_CALL_TRACE;
}

void Request::slotError(QNetworkReply::NetworkError error)
{
    FUNCTION_CALL_TRACE;
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    debugReplyAndReadAll(reply);

    if (error <= 200) {
        emit syncError(Sync::SYNC_CONNECTION_ERROR);
    } else if (error > 200 && error < 400) {
        emit syncError(Sync::SYNC_SERVER_FAILURE);
    } else {
        emit syncError(Sync::SYNC_ERROR);
    }
}

void Request::slotSslErrors(QList<QSslError> errors)
{
    FUNCTION_CALL_TRACE;
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    debugReplyAndReadAll(reply);

    if (mSettings->ignoreSSLErrors()) {
        reply->ignoreSslErrors(errors);
    }
}

void Request::debugRequest(const QNetworkRequest &request, const QByteArray &data)
{
#ifdef BUTEO_ENABLE_DEBUG
    qDebug() << "-----------------------------------------------\n";
    const QList<QByteArray>& rawHeaderList(request.rawHeaderList());
    Q_FOREACH (const QByteArray &rawHeader, rawHeaderList) {
        qDebug() << rawHeader << " : " << request.rawHeader(rawHeader);
    }
    qDebug() << "URL = " << request.url();
    qDebug() << "Request : \n" << data;
    qDebug() << "---------------------------------------------------------------------\n";
#endif
}

void Request::debugRequest(const QNetworkRequest &request, const QString &data)
{
#ifdef BUTEO_ENABLE_DEBUG
    debugRequest(request, data.toLatin1());
#endif
}

void Request::debugReply(const QNetworkReply &reply, const QByteArray &data)
{
#ifdef BUTEO_ENABLE_DEBUG
    qDebug() << "---------------------------------------------------------------------\n";
    qDebug() << REQUEST_TYPE << "response status code:" << reply.attribute(QNetworkRequest::HttpStatusCodeAttribute);
    QList<QNetworkReply::RawHeaderPair> headers = reply.rawHeaderPairs();
    qDebug() << REQUEST_TYPE << "response headers:";
    for (int i=0; i<headers.count(); i++) {
        qDebug() << "\t" << headers[i].first << ":" << headers[i].second;
    }
    if (!data.isEmpty()) {
        qDebug() << REQUEST_TYPE << "response data:" << data;
    }
    qDebug() << "---------------------------------------------------------------------\n";
#endif
}

void Request::debugReplyAndReadAll(QNetworkReply *reply)
{
#ifdef BUTEO_ENABLE_DEBUG
    debugReply(*reply, reply->readAll());
#endif
}
