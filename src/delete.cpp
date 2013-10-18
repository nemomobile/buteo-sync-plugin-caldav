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

#include "delete.h"
#include "settings.h"

#include <QNetworkAccessManager>
#include <QBuffer>
#include <QDebug>

Delete::Delete(QNetworkAccessManager *manager, Settings *settings, QObject *parent) :
        Request(manager, settings, "DELETE", parent)
{
    FUNCTION_CALL_TRACE;
}


void Delete::deleteEvent(const QString &uri) {
    QNetworkRequest request;
    QUrl url(mSettings->url() + uri );
    if (!mSettings->authToken().isEmpty()) {
        request.setRawHeader(QString("Authorization").toLatin1(),
                             QString("Bearer " + mSettings->authToken()).toLatin1());
    } else {
        url.setUserName(mSettings->username());
        url.setPassword(mSettings->password());
    }

    request.setUrl(url);
    mNReply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1());
    debugRequest(request, "");
    connect(mNReply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(mNReply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(slotError(QNetworkReply::NetworkError)));
    connect(mNReply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));

}

void Delete::requestFinished() {
    LOG_DEBUG("DELETE Request Finished............" << mNReply->readAll());
    QVariant statusCode = mNReply->attribute( QNetworkRequest::HttpStatusCodeAttribute );
    if (statusCode.isValid() ) {
        int status = statusCode.toInt();
        qDebug() << "Status Code : " << status << "\n";
        emit finished();
    }
    const QList<QByteArray>& rawHeaderList(mNReply->rawHeaderList());
    foreach (QByteArray rawHeader, rawHeaderList) {
        qDebug() << rawHeader << " : " << mNReply->rawHeader(rawHeader);
    }
    qDebug() << "---------------------------------------------------------------------\n";
}

void Delete::slotError(QNetworkReply::NetworkError error) {
    if (error <= 200) {
        emit syncError(Sync::SYNC_CONNECTION_ERROR);
    } else if (error > 200 && error < 400) {
        emit syncError(Sync::SYNC_SERVER_FAILURE);
    } else {
        emit syncError(Sync::SYNC_ERROR);
    }
}

void Delete::slotSslErrors(QList<QSslError> errors) {
    qDebug() << "SSL Error";
    if (mSettings->ignoreSSLErrors()) {
        mNReply->ignoreSslErrors(errors);
    }
}
