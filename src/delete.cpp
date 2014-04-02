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

#include "delete.h"
#include "settings.h"

#include <QNetworkAccessManager>
#include <QDebug>

static const QString VCalExtension = QStringLiteral(".ics");

Delete::Delete(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "DELETE", parent)
{
    FUNCTION_CALL_TRACE;
}

void Delete::deleteEvent(const QString &serverPath, KCalCore::Incidence::Ptr incidence)
{
    FUNCTION_CALL_TRACE;

    QString uri = resourceUriForIncidence(incidence);
    if (uri.isEmpty()) {
        finishedWithError(Buteo::SyncResults::INTERNAL_ERROR,
                          QString("DELETE not sent, cannot get uri for incidence %1").arg(incidence->uid()));
        return;
    }
    QNetworkRequest request;
    prepareRequest(&request, serverPath + uri);
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1());
    debugRequest(request, QStringLiteral(""));
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Delete::requestFinished()
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

QString Delete::resourceUriForIncidence(KCalCore::Incidence::Ptr incidence)
{
    QString uri = incidence->customProperty("buteo", "uri");
    if (!uri.isEmpty()) {
        return uri.split("/", QString::SkipEmptyParts).last();
    }
    QString path = incidence->uid();
    if (!path.isEmpty()) {
        path += VCalExtension;
    }
    return path;
}
