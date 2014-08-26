/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2013 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Mani Chandrasekar <maninc@gmail.com>
 *               Stephan Rave <mail@stephanrave.de>
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

#include <LogMacros.h>

static const QString DateTimeFormat = QStringLiteral("yyyyMMddTHHmmss");
static const QString DateTimeFormatUTC = DateTimeFormat + QStringLiteral("Z");

static QString dateTimeToString(const QDateTime &dt)
{
    if (dt.timeSpec() == Qt::UTC) {
        return dt.toString(DateTimeFormatUTC);
    } else {
        return dt.toString(DateTimeFormat);
    }
}

static QByteArray timeRangeFilterXml(const QDateTime &fromDateTime, const QDateTime &toDateTime)
{
    QByteArray xml;
    if (fromDateTime.isValid() || toDateTime.isValid()) {
        xml = "<c:comp-filter name=\"VEVENT\"> <c:time-range ";
        if (fromDateTime.isValid()) {
            xml += "start=\"" + dateTimeToString(fromDateTime) + "\" ";
        }
        if (toDateTime.isValid()) {
            xml += "end=\"" + dateTimeToString(toDateTime) + "\" ";
        }
        xml += " /></c:comp-filter>";

    }
    return xml;
}

Report::Report(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "REPORT", parent)
{
    FUNCTION_CALL_TRACE;
}

void Report::getAllEvents(const QString &serverPath, const QDateTime &fromDateTime, const QDateTime &toDateTime)
{
    FUNCTION_CALL_TRACE;

    QByteArray requestData = \
            "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">" \
                "<d:prop>" \
                    "<d:getetag />"\
                    "<c:calendar-data />" \
                "</d:prop>"
                "<c:filter>" \
                    "<c:comp-filter name=\"VCALENDAR\">";
    if (fromDateTime.isValid() || toDateTime.isValid()) {
        requestData.append(timeRangeFilterXml(fromDateTime, toDateTime));
    }
    requestData += \
                    "</c:comp-filter>" \
                "</c:filter>" \
            "</c:calendar-query>";
    sendRequest(serverPath, requestData);
}

void Report::getAllETags(const QString &serverPath,
                         const QDateTime &fromDateTime,
                         const QDateTime &toDateTime)
{
    FUNCTION_CALL_TRACE;

    QByteArray requestData = \
            "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">" \
                "<d:prop>" \
                    "<d:getetag />"\
                "</d:prop>"
                "<c:filter>" \
                    "<c:comp-filter name=\"VCALENDAR\">";
    if (fromDateTime.isValid() || toDateTime.isValid()) {
        requestData.append(timeRangeFilterXml(fromDateTime, toDateTime));
    }
    requestData += \
                    "</c:comp-filter>" \
                "</c:filter>" \
            "</c:calendar-query>";
    sendRequest(serverPath, requestData);
}

void Report::multiGetEvents(const QString &serverPath, const QStringList &eventIdList)
{
    FUNCTION_CALL_TRACE;

    if (eventIdList.isEmpty()) {
        return;
    }

    QByteArray requestData = "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">" \
                             "<d:prop><d:getetag /><c:calendar-data /></d:prop>";
    Q_FOREACH (const QString &eventId , eventIdList) {
        requestData.append("<d:href>");
        requestData.append(eventId);
        requestData.append("</d:href>");
    }
    requestData.append("</c:calendar-multiget>");

    sendRequest(serverPath, requestData);
 }

void Report::sendRequest(const QString& serverPath, const QByteArray &requestData)
{
    FUNCTION_CALL_TRACE;
    mServerPath = serverPath;

    QNetworkRequest request;
    prepareRequest(&request, serverPath);
    request.setRawHeader("Depth", "1");
    request.setRawHeader("Prefer", "return-minimal");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");
    QBuffer *buffer = new QBuffer(this);
    buffer->setData(requestData);
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, buffer->buffer());
    connect(reply, SIGNAL(finished()), this, SLOT(processResponse()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Report::processResponse()
{
    FUNCTION_CALL_TRACE;

    LOG_DEBUG("Process " << command() << " response for server path" << mServerPath);

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

    if (!data.isNull() && !data.isEmpty()) {
        Reader reader;
        reader.read(data);
        mReceivedResources = reader.results();
        finishedWithSuccess();
    } else {
        finishedWithError(Buteo::SyncResults::INTERNAL_ERROR, QString("Empty response body for ") + command());
    }
}

QHash<QString, Reader::CalendarResource> Report::receivedCalendarResources() const
{
    return mReceivedResources;
}
