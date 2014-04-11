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

#include "report.h"
#include "reader.h"
#include "settings.h"

#include <QNetworkAccessManager>
#include <QBuffer>
#include <QDebug>
#include <QStringList>

#include <incidence.h>

Report::Report(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "REPORT", parent)
{
    FUNCTION_CALL_TRACE;
}

void Report::getAllEvents(const QString &serverPath)
{
    FUNCTION_CALL_TRACE;
    mServerPath = serverPath;

    QNetworkRequest request;
    prepareRequest(&request, serverPath);
    request.setRawHeader("Depth", "1");
    request.setRawHeader("Prefer", "return-minimal");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData("<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">" \
                    "<d:prop> <d:getetag /> <c:calendar-data /> </d:prop>"       \
                    "<c:filter> <c:comp-filter name=\"VCALENDAR\" /> </c:filter>" \
                    "</c:calendar-query>");
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, buffer->buffer());
    connect(reply, SIGNAL(finished()), this, SLOT(processEvents()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Report::getAllETags(const QString &serverPath, const KCalCore::Incidence::List &currentLocalIncidences)
{
    FUNCTION_CALL_TRACE;
    mServerPath = serverPath;
    mLocalIncidences = currentLocalIncidences;

    QNetworkRequest request;
    prepareRequest(&request, serverPath);
    request.setRawHeader("Depth", "1");
    request.setRawHeader("Prefer", "return-minimal");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData("<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">" \
                    "<d:prop> <d:getetag /> </d:prop> " \
                    "<c:filter> <c:comp-filter name=\"VCALENDAR\" > " \
                    "</c:comp-filter> </c:filter>" \
                    "</c:calendar-query> ");
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, buffer->buffer());

    connect(reply, SIGNAL(finished()), this, SLOT(processETags()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Report::multiGetEvents(const QString &serverPath, const QStringList &eventIdList)
{
    FUNCTION_CALL_TRACE;

    if (eventIdList.isEmpty()) {
        return;
    }
    QNetworkRequest request;
    prepareRequest(&request, serverPath);
    request.setRawHeader("Depth", "1");
    request.setRawHeader("Prefer", "return-minimal");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");

    QString multiGetRequest = "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">" \
                              "<d:prop><d:getetag /><c:calendar-data /></d:prop>";
    Q_FOREACH (const QString &eventId , eventIdList) {
        multiGetRequest.append("<d:href>");
        multiGetRequest.append(eventId);
        multiGetRequest.append("</d:href>");
    }
    multiGetRequest.append("</c:calendar-multiget>");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData(multiGetRequest.toLatin1());
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    connect(reply, SIGNAL(finished()), this, SLOT(processEvents()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Report::processEvents()
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
    if (reply->error() != QNetworkReply::NoError) {
        finishedWithReplyResult(reply->error());
        return;
    }
    QByteArray data = reply->readAll();
    debugReply(*reply, data);
    reply->deleteLater();

    if (!data.isNull() && !data.isEmpty()) {
        Reader reader;
        reader.read(data);
        mReceivedResources = reader.results().values();
    }
    finishedWithSuccess();
}

void Report::processETags()
{
    FUNCTION_CALL_TRACE;

    LOG_DEBUG("Process tags for server path" << mServerPath);

    if (wasDeleted()) {
        LOG_DEBUG(command() << "request was aborted");
        return;
    }

    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        finishedWithInternalError();
        return;
    }
    QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCode.isValid()) {
        int status = statusCode.toInt();
        if (status > 299) {
            finishedWithError(Buteo::SyncResults::INTERNAL_ERROR,
                              QString("Got error status response for REPORT: %1").arg(status));
            reply->deleteLater();
            return;
        }
    }

    QByteArray data = reply->readAll();
    debugReply(*reply, data);
    reply->deleteLater();

    if (!data.isEmpty()) {
        Reader reader;
        reader.read(data);
        QHash<QString, Reader::CalendarResource> map = reader.results();
        QStringList eventIdList;

        Q_FOREACH (KCalCore::Incidence::Ptr incidence, mLocalIncidences) {
            QString uri = incidence->customProperty("buteo", "uri");
            if (uri.isEmpty()) {
                //Newly added to Local DB -- Skip this incidence
                continue;
            }
            if (!map.contains(uri)) {
                // we have an incidence that's not on the remote server, so delete it
                switch (incidence->type()) {
                case KCalCore::IncidenceBase::TypeEvent:
                case KCalCore::IncidenceBase::TypeTodo:
                case KCalCore::IncidenceBase::TypeJournal:
                    mLocalIncidenceUidsNotOnServer.append(incidence->uid());
                    break;
                case KCalCore::IncidenceBase::TypeFreeBusy:
                case KCalCore::IncidenceBase::TypeUnknown:
                    break;
                }
                continue;
            } else {
                Reader::CalendarResource resource = map.take(uri);
                if (incidence->customProperty("buteo", "etag") != resource.etag) {
                    eventIdList.append(resource.href);
                }
            }
        }
        eventIdList.append(map.keys());
        if (!eventIdList.isEmpty()) {
            // some incidences have changed on the server, so fetch the new details
            multiGetEvents(mServerPath, eventIdList);
        } else {
            finishedWithSuccess();
        }
    } else {
        finishedWithError(Buteo::SyncResults::INTERNAL_ERROR, QString("Empty response body for REPORT"));
    }
}

QList<Reader::CalendarResource> Report::receivedCalendarResources() const
{
    return mReceivedResources;
}

QStringList Report::localIncidenceUidsNotOnServer() const
{
    return mLocalIncidenceUidsNotOnServer;
}
