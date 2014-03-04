#include "get.h"
#include "settings.h"
#include "reader.h"

#include <QNetworkAccessManager>
#include <QDebug>
#include <QHash>
#include <QStringList>

#include <incidence.h>
#include <icalformat.h>

Get::Get(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "GET", parent)
{
}

void Get::getEvent(const QString &u)
{
    QNetworkRequest request;

    QUrl url(u);
    if (!mSettings->authToken().isEmpty()) {
        request.setRawHeader(QString("Authorization").toLatin1(),
                             QString("Bearer " + mSettings->authToken()).toLatin1());
    } else {
        url.setUserName(mSettings->username());
        url.setPassword(mSettings->password());
    }
    request.setUrl(url);
    QNetworkReply *reply = mNAManager->get(request);
    debugRequest(request, QStringLiteral(""));
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Get::requestFinished()
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

    // TODO this needs to save the received vcal data into the calendar database.
    LOG_CRITICAL("Get::requestFinished() is not implemented!");

    finishedWithReplyResult(reply->error());
    reply->deleteLater();
}
