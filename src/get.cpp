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

Get::Get(QNetworkAccessManager *manager, Settings *settings, QObject *parent) :
     Request(manager, settings, "GET", parent)
{
}

void Get::getEvent(const QString &u) {
    QNetworkRequest request;

    QUrl url(u);
    if (!mSettings->authToken().isEmpty()) {
        request.setRawHeader(QString("Authorization").toLatin1(), QString("Bearer " + mSettings->authToken()).toLatin1());
    } else {
        url.setUserName(mSettings->username());
        url.setPassword(mSettings->password());
    }
    request.setUrl(url);
    mNReply = mNAManager->get(request);
    debugRequest(request, "");
    connect(mNReply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(mNReply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(slotError(QNetworkReply::NetworkError)));
    connect(mNReply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));

}

void Get::requestFinished() {
    QByteArray data = mNReply->readAll();
    LOG_DEBUG("GET Request Finished............" << data);

    Reader reader;
    reader.read(data);
    QHash<QString, CDItem *> map = reader.getIncidenceMap();
    if (!map.isEmpty()) {
        QHash<QString, CDItem *>::iterator iter = map.begin();
        CDItem *item = iter.value();
        qDebug() << "----------------------------" << item->etag() << "\n";
        emit finished();
    }

    emit syncError(Sync::SYNC_ERROR);
}

void Get::slotError(QNetworkReply::NetworkError error) {
    qDebug() << "Error # " << error;
    if (error <= 200) {
        emit syncError(Sync::SYNC_CONNECTION_ERROR);
    } else if (error > 200 && error < 400) {
        emit syncError(Sync::SYNC_SERVER_FAILURE);
    } else {
        emit syncError(Sync::SYNC_ERROR);
    }
}

void Get::slotSslErrors(QList<QSslError> errors) {
    qDebug() << "SSL Error";
    if (mSettings->ignoreSSLErrors()) {
        mNReply->ignoreSslErrors(errors);
    }
}
