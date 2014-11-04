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

#include <LogMacros.h>

Request::Request(QNetworkAccessManager *manager,
                 Settings *settings,
                 const QString &requestType,
                 QObject *parent)
    : QObject(parent)
    , mNAManager(manager)
    , REQUEST_TYPE(requestType)
    , mSettings(settings)
    , mNetworkError(QNetworkReply::NoError)
    , mMinorCode(Buteo::SyncResults::NO_ERROR)
{
    FUNCTION_CALL_TRACE;

    mSelfPointer = this;
}

int Request::errorCode() const
{
    return mMinorCode;
}

QString Request::errorString() const
{
    return mErrorString;
}

QNetworkReply::NetworkError Request::networkError() const
{
    return mNetworkError;
}

QString Request::command() const
{
    return REQUEST_TYPE;
}

void Request::finishedWithReplyResult(QNetworkReply::NetworkError error)
{
    mNetworkError = error;
    if (error == QNetworkReply::NoError) {
        finishedWithSuccess();
    } else {
        int errorCode = Buteo::SyncResults::INTERNAL_ERROR;
        if (error == QNetworkReply::SslHandshakeFailedError || error == QNetworkReply::ContentAccessDenied ||
                error == QNetworkReply::AuthenticationRequiredError) {
            errorCode = Buteo::SyncResults::AUTHENTICATION_FAILURE;
        } else if (error < 200) {
            errorCode = Buteo::SyncResults::CONNECTION_ERROR;
        }
        finishedWithError(errorCode, QString("Network request failed with QNetworkReply::NetworkError: %1").arg(error));
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
    } else {
        qWarning() << command() << "request failed with SSL error";
    }
}

void Request::finishedWithError(int minorCode, const QString &errorString)
{
    if (minorCode != Buteo::SyncResults::NO_ERROR) {
        LOG_CRITICAL(REQUEST_TYPE << "request failed." << minorCode << errorString);
    }
    mMinorCode = minorCode;
    mErrorString = errorString;
    emit finished();
}

void Request::finishedWithInternalError(const QString &errorString)
{
    finishedWithError(Buteo::SyncResults::INTERNAL_ERROR, errorString.isEmpty() ? QStringLiteral("Internal error") : errorString);
}

void Request::finishedWithSuccess()
{
    mMinorCode = Buteo::SyncResults::NO_ERROR;
    emit finished();
}

void Request::prepareRequest(QNetworkRequest *request, const QString &requestPath)
{
    QUrl url(mSettings->serverAddress());
    if (!mSettings->authToken().isEmpty()) {
        request->setRawHeader(QString("Authorization").toLatin1(),
                              QString("Bearer " + mSettings->authToken()).toLatin1());
    } else {
        url.setUserName(mSettings->username());
        url.setPassword(mSettings->password());
    }
    url.setPath(QUrl::fromPercentEncoding(requestPath.toLatin1()));
    request->setUrl(url);
}

bool Request::wasDeleted() const
{
    return mSelfPointer == 0;
}

void Request::debugRequest(const QNetworkRequest &request, const QByteArray &data)
{
    LOG_PROTOCOL(debuggingString(request, data));
}

void Request::debugRequest(const QNetworkRequest &request, const QString &data)
{
    LOG_PROTOCOL(debuggingString(request, data.toUtf8()));
}

void Request::debugReply(const QNetworkReply &reply, const QByteArray &data)
{
    LOG_PROTOCOL(debuggingString(reply, data));
}

void Request::debugReplyAndReadAll(QNetworkReply *reply)
{
    LOG_PROTOCOL(debuggingString(*reply, reply->readAll()));
}

QString Request::debuggingString(const QNetworkRequest &request, const QByteArray &data)
{
    QStringList text;
    text += "---------------------------------------------------------------------";
    const QList<QByteArray> &rawHeaderList = request.rawHeaderList();
    Q_FOREACH (const QByteArray &rawHeader, rawHeaderList) {
        text += rawHeader + " : " + request.rawHeader(rawHeader);
    }
    QUrl censoredUrl = request.url();
    censoredUrl.setUserName(QStringLiteral("user"));
    censoredUrl.setPassword(QStringLiteral("pass"));
    text += "URL = " + censoredUrl.toString();
    text += "Request : " + REQUEST_TYPE +  "\n" + data;
    text += "---------------------------------------------------------------------\n";
    return text.join(QChar('\n'));
}

QString Request::debuggingString(const QNetworkReply &reply, const QByteArray &data)
{
    QStringList text;
    text += "---------------------------------------------------------------------";
    text += REQUEST_TYPE + " response status code: " + reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toString();
    QList<QNetworkReply::RawHeaderPair> headers = reply.rawHeaderPairs();
    text += REQUEST_TYPE + " response headers:";
    for (int i=0; i<headers.count(); i++) {
        text += "\t" + headers[i].first + " : " + headers[i].second;
    }
    if (!data.isEmpty()) {
        text += REQUEST_TYPE + " response data:" + data;
    }
    text += "---------------------------------------------------------------------\n";
    return text.join(QChar('\n'));
}
