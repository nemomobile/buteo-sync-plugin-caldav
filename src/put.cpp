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

#include "put.h"
#include "report.h"
#include "settings.h"
#include "incidencehandler.h"

#include <QNetworkAccessManager>
#include <QBuffer>
#include <QDebug>
#include <QStringList>
#include <QUrl>

#include <incidence.h>
#include <icalformat.h>

#include <LogMacros.h>

#define PROP_INCIDENCE_URI "uri"

Put::Put(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "PUT", parent)
{
}

void Put::updateEvent(const QString &serverPath, const QString &icalData, const QString &eTag, const QString &uri, const KCalId &kcalId)
{
    FUNCTION_CALL_TRACE;
    Q_UNUSED(serverPath);

    if (mIdList.contains(kcalId)) {
        LOG_WARNING("Already uploaded modification to event with id:" << kcalId.toString());
        return;
    }

    mIdList.insert(kcalId);
    QByteArray data = icalData.toUtf8();
    if (data.isEmpty()) {
        LOG_WARNING("Error while converting iCal Object to QByteArray");
        return;
    }

    // 'uri' contains the calendar path + filename
    QNetworkRequest request;
    prepareRequest(&request, uri);
    request.setRawHeader("If-Match", eTag.toLatin1());
    request.setHeader(QNetworkRequest::ContentLengthHeader, data.length());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/calendar; charset=utf-8");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData(data);
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    reply->setProperty(PROP_INCIDENCE_URI, uri);
    debugRequest(request, data);
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Put::createEvent(const QString &serverPath, const QString &icalData, const KCalId &kcalId)
{
    FUNCTION_CALL_TRACE;

    if (mIdList.contains(kcalId)) {
        LOG_WARNING("Already uploaded modification to event with id:" << kcalId.toString());
        return;
    }

    mIdList.insert(kcalId);
    QByteArray data = icalData.toUtf8();
    if (data.isEmpty()) {
        LOG_WARNING("Error while converting iCal Object to QByteArray");
        return;
    }

    QNetworkRequest request;
    QString uri = serverPath + kcalId.uid + ".ics";
    prepareRequest(&request, uri);
    request.setRawHeader("If-None-Match", "*");
    request.setHeader(QNetworkRequest::ContentLengthHeader, data.length());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/calendar; charset=utf-8");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData(data);
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    reply->setProperty(PROP_INCIDENCE_URI, uri);
    debugRequest(request, data);
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Put::requestFinished()
{
    FUNCTION_CALL_TRACE;

    if (wasDeleted()) {
        LOG_DEBUG(command() << "request was aborted");
        return;
    }

    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        finishedWithInternalError();
        return;
    }
    debugReplyAndReadAll(reply);

    // Server may update the etag as soon as the modification is received and send back a new etag
    Q_FOREACH (const QNetworkReply::RawHeaderPair &header, reply->rawHeaderPairs()) {
        if (header.first.toLower() == QStringLiteral("etag")) {
            mUpdatedETags.insert(reply->property(PROP_INCIDENCE_URI).toString(), header.second);
        }
    }

    finishedWithReplyResult(reply->error());
    reply->deleteLater();
}

QHash<QString,QString> Put::updatedETags() const
{
    return mUpdatedETags;
}
