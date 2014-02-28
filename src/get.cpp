#include "get.h"
#include "settings.h"
#include "reader.h"

#include <QNetworkAccessManager>
#include <QBuffer>
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
    mNReply = mNAManager->get(request);
    debugRequest(request, QStringLiteral(""));
    connect(mNReply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(mNReply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(slotError(QNetworkReply::NetworkError)));
    connect(mNReply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Get::requestFinished()
{
    FUNCTION_CALL_TRACE;
    QByteArray data = mNReply->readAll();
    debugReply(*mNReply, data);

    // TODO this needs to save the received vcal data into the calendar database.
    LOG_CRITICAL("Get::requestFinished() is not implemented!");

    emit finished();
}
