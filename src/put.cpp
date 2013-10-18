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

Put::Put(QNetworkAccessManager *manager, Settings *settings, QObject *parent) :
    Request(manager, settings, "PUT", parent)
{
}

void Put::updateEvent(KCalCore::Incidence::Ptr incidence) {

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
    QUrl url(mSettings->makeUrl().host() + uri);

    if (!mSettings->authToken().isEmpty()) {
        request.setRawHeader(QString("Authorization").toLatin1(), QString("Bearer " + mSettings->authToken()).toLatin1());
    } else {
        url.setUserName(mSettings->username());
        url.setPassword(mSettings->password());
    }
    request.setUrl(url);
    request.setRawHeader("If-Match", etag.toLatin1());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/calendar; charset=utf-8");

    QBuffer *buffer = new QBuffer;
    buffer->setData(data.toLatin1());
    mNReply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, data.toLatin1());
    connect(mNReply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(mNReply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(slotError(QNetworkReply::NetworkError)));
    connect(mNReply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));

}

void Put::createEvent(KCalCore::Incidence::Ptr incidence) {

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

    QBuffer *buffer = new QBuffer;
    buffer->setData(ical.toLatin1());
    mNReply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, ical.toLatin1());
    connect(mNReply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(mNReply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(slotError(QNetworkReply::NetworkError)));
    connect(mNReply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));

}

void Put::requestFinished() {
    LOG_DEBUG("PUT Request Finished............" << mNReply->readAll());
    qDebug() << "------------------------PUT Finished-----------------------\n" << mNReply->readAll();
    QVariant statusCode = mNReply->attribute( QNetworkRequest::HttpStatusCodeAttribute );
    if (statusCode.isValid() ) {
        int status = statusCode.toInt();
        qDebug() << "Status Code : " << status << "\n";
        if (status == 201 || status == 204) {
            //Update ETag for corresponding UID
            Get *get = new Get(mNAManager, mSettings);
            get->getEvent(QString(mNReply->url().toString()));
            connect(get, SIGNAL(finished()), this, SIGNAL(finished()));
            connect(get, SIGNAL(syncError(Sync::SyncStatus)), this, SIGNAL(syncError(Sync::SyncStatus)));
            connect(get, SIGNAL(finished()), this, SLOT(deleteLater()));
        }
    }
    const QList<QByteArray>& rawHeaderList(mNReply->rawHeaderList());
    foreach (QByteArray rawHeader, rawHeaderList) {
        qDebug() << rawHeader << " : " << mNReply->rawHeader(rawHeader);
    }
    qDebug() << "---------------------------------------------------------------------\n";
}

void Put::slotError(QNetworkReply::NetworkError error) {
    qDebug() << "Error # " << error;
    if (error <= 200) {
        emit syncError(Sync::SYNC_CONNECTION_ERROR);
    } else if (error > 200 && error < 400) {
        emit syncError(Sync::SYNC_SERVER_FAILURE);
    } else {
        emit syncError(Sync::SYNC_ERROR);
    }
}

void Put::slotSslErrors(QList<QSslError> errors) {
    qDebug() << "SSL Error";
    if (mSettings->ignoreSSLErrors()) {
        mNReply->ignoreSslErrors(errors);
    }
}
