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

#include "put.h"
#include "report.h"
#include "settings.h"
#include "get.h"

#include <QNetworkAccessManager>
#include <QBuffer>
#include <QDebug>
#include <QStringList>
#include <QUrl>

#include <incidence.h>
#include <icalformat.h>

Put::Put(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "PUT", parent)
{
}

void Put::updateEvent(KCalCore::Incidence::Ptr incidence)
{
    FUNCTION_CALL_TRACE;

    KCalCore::ICalFormat *icalFormat = new KCalCore::ICalFormat;
    QString data = icalFormat->toICalString(incidence);
    if (data == NULL || data.isEmpty()) {
        LOG_WARNING("Error while converting iCal Object to string");
        return;
    }
    QString etag = incidence->customProperty("buteo", "etag");
    QString uri  = incidence->customProperty("buteo", "uri");
    mUidList.append(incidence->uid());
    QNetworkRequest request;

    // Get a URL of the server + path + filename.
    // Settings::makeUrl() has server+path and 'uri' has path+filename, so strip
    // the path from url and use the one from 'uri'.
    QUrl url = mSettings->makeUrl();
    QString urlString = url.toString();
    int urlPathIndex = urlString.indexOf(url.path());
    if (urlPathIndex >= 0) {
        urlString = urlString.mid(0, urlPathIndex);
    }
    url.setUrl(urlString + uri);

    if (!mSettings->authToken().isEmpty()) {
        request.setRawHeader(QString("Authorization").toLatin1(),
                             QString("Bearer " + mSettings->authToken()).toLatin1());
    } else {
        url.setUserName(mSettings->username());
        url.setPassword(mSettings->password());
    }
    request.setUrl(url);
    request.setRawHeader("If-Match", etag.toLatin1());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/calendar; charset=utf-8");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData(data.toLatin1());
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, data);
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(slotError(QNetworkReply::NetworkError)));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Put::createEvent(KCalCore::Incidence::Ptr incidence)
{
    FUNCTION_CALL_TRACE;

    KCalCore::ICalFormat *icalFormat = new KCalCore::ICalFormat;
    QString ical = icalFormat->toICalString(incidence);
    if (ical == NULL) {
        LOG_WARNING("Error while converting iCal Object to string");
        return;
    }

    QString uid  = incidence->uid();
    mUidList.append(incidence->uid());
    QNetworkRequest request;
    QUrl url(mSettings->url() + uid + ".ics");

    request.setRawHeader("If-None-Match", "*");
    request.setHeader(QNetworkRequest::ContentLengthHeader, ical.length());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/calendar; charset=utf-8");
    if (!mSettings->authToken().isEmpty()) {
        request.setRawHeader(QString("Authorization").toLatin1(), QString("Bearer " + mSettings->authToken()).toLatin1());
    } else {
        url.setUserName(mSettings->username());
        url.setPassword(mSettings->password());
    }
    request.setUrl(url);

    QBuffer *buffer = new QBuffer(this);
    buffer->setData(ical.toLatin1());
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, ical);
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(slotError(QNetworkReply::NetworkError)));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Put::requestFinished()
{
    FUNCTION_CALL_TRACE;
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        emit syncError(Sync::SYNC_ERROR);
        return;
    }
    debugReplyAndReadAll(reply);
    reply->deleteLater();

    emit finished();
}
