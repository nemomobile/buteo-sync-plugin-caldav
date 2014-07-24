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

Put::Put(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "PUT", parent)
{
}

void Put::updateEvent(const QString &serverPath, KCalCore::Incidence::Ptr incidence)
{
    FUNCTION_CALL_TRACE;

    Q_UNUSED(serverPath);

    KCalCore::ICalFormat icalFormat;
    QString data = icalFormat.toICalString(IncidenceHandler::incidenceToExport(incidence));
    if (data == NULL || data.isEmpty()) {
        LOG_WARNING("Error while converting iCal Object to string");
        return;
    }
    QString etag = incidence->customProperty("buteo", "etag");
    QString uri  = incidence->customProperty("buteo", "uri");
    mUidList.append(incidence->uid());
    QNetworkRequest request;

    // 'uri' contains the calendar path + filename
    prepareRequest(&request, uri);
    request.setRawHeader("If-Match", etag.toLatin1());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/calendar; charset=utf-8");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData(data.toLatin1());
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, data);
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Put::createEvent(const QString &serverPath, KCalCore::Incidence::Ptr incidence)
{
    FUNCTION_CALL_TRACE;

    KCalCore::ICalFormat icalFormat;
    QString ical = icalFormat.toICalString(IncidenceHandler::incidenceToExport(incidence));
    if (ical == NULL) {
        LOG_WARNING("Error while converting iCal Object to string");
        return;
    }

    QString uid  = incidence->uid();
    mUidList.append(incidence->uid());
    QNetworkRequest request;
    prepareRequest(&request, serverPath + uid + ".ics");
    request.setRawHeader("If-None-Match", "*");
    request.setHeader(QNetworkRequest::ContentLengthHeader, ical.length());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/calendar; charset=utf-8");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData(ical.toLatin1());
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, ical);
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

    finishedWithReplyResult(reply->error());
    reply->deleteLater();
}
