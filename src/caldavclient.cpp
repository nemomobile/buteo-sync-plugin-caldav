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

#include "caldavclient.h"
#include "report.h"
#include "put.h"
#include "delete.h"
#include "reader.h"

#include <extendedcalendar.h>
#include <extendedstorage.h>
#include <notebook.h>
#include <icalformat.h>

#include <QNetworkReply>
#include <QDateTime>

#include <Accounts/Manager>
#include <Accounts/Account>

#include <PluginCbInterface.h>
#include <LogMacros.h>
#include <ProfileEngineDefs.h>
#include <ProfileManager.h>

extern "C" CalDavClient* createPlugin(const QString& aPluginName,
                                         const Buteo::SyncProfile& aProfile,
                                         Buteo::PluginCbInterface *aCbInterface) {
    return new CalDavClient(aPluginName, aProfile, aCbInterface);
}

extern "C" void destroyPlugin(CalDavClient *aClient) {
    delete aClient;
}

CalDavClient::CalDavClient(const QString& aPluginName,
                            const Buteo::SyncProfile& aProfile,
                            Buteo::PluginCbInterface *aCbInterface) :
    ClientPlugin(aPluginName, aProfile, aCbInterface), mSlowSync (true), mAuth(NULL)
{
    FUNCTION_CALL_TRACE;
}

CalDavClient::~CalDavClient() {
    FUNCTION_CALL_TRACE;
}

bool CalDavClient::init() {
    FUNCTION_CALL_TRACE;

    if (lastSyncTime().isNull ())
        mSlowSync = true;
    else
        mSlowSync = false;

    mNAManager = new QNetworkAccessManager;

    if (initConfig ()) {
        return true;
    } else {
        // Uninitialize everything that was initialized before failure.
        uninit();
        return false;
    }
}

bool CalDavClient::uninit() {
    FUNCTION_CALL_TRACE;

    return true;
}

bool CalDavClient::startSync() {
    FUNCTION_CALL_TRACE;

    if (!mAuth)
        return false;

    connect(this, SIGNAL(stateChanged(Sync::SyncProgressDetail)),
            this, SLOT(receiveStateChanged(Sync::SyncProgressDetail)));
    connect(this, SIGNAL(syncFinished(Sync::SyncStatus)),
            this, SLOT(receiveSyncFinished(Sync::SyncStatus)));

    mAuth->authenticate();

    LOG_DEBUG ("Init done. Continuing with sync");

    return true;
}

void CalDavClient::abortSync(Sync::SyncStatus aStatus) {
    FUNCTION_CALL_TRACE;
    Sync::SyncStatus state = Sync::SYNC_ABORTED;

    if (aStatus == Sync::SYNC_ERROR) {
        state = Sync::SYNC_CONNECTION_ERROR;
    }

    if( !this->abort (state)) {
        LOG_DEBUG( "Agent not active, aborting immediately" );
        syncFinished(Sync::SYNC_ABORTED);

    }
    else
    {
        LOG_DEBUG( "Agent active, abort event posted" );
    }
}

bool CalDavClient::start() {
    FUNCTION_CALL_TRACE;

    if (!mAuth->username().isEmpty() && !mAuth->password().isEmpty()) {
        mSettings.setUsername(mAuth->username());
        mSettings.setPassword(mAuth->password());
    }
    mSettings.setAuthToken(mAuth->token());

    switch (mSyncDirection)
    {
    case Buteo::SyncProfile::SYNC_DIRECTION_TWO_WAY:
        if (mSlowSync) {
            startSlowSync();
        } else {
            startQuickSync();
        }

        break;
    case Buteo::SyncProfile::SYNC_DIRECTION_FROM_REMOTE:
        // Not required
        break;
    case Buteo::SyncProfile::SYNC_DIRECTION_TO_REMOTE:
        // Not required
        break;
    case Buteo::SyncProfile::SYNC_DIRECTION_UNDEFINED:
        // Not required
    default:
        // throw configuration error
        break;
    };

    return true;
}

bool CalDavClient::abort(Sync::SyncStatus status) {
    Q_UNUSED(status)
    emit syncFinished (Sync::SYNC_ABORTED);
    return true;
}

bool CalDavClient::cleanUp() {
    FUNCTION_CALL_TRACE;
    return true;
}

void CalDavClient::connectivityStateChanged(Sync::ConnectivityType aType, bool aState) {
    FUNCTION_CALL_TRACE;
    LOG_DEBUG("Received connectivity change event:" << aType << " changed to " << aState);
}

bool CalDavClient::initConfig () {

    FUNCTION_CALL_TRACE;

    LOG_DEBUG("Initiating config...");

    mAccountId = 0;
    QString scope = "";
    QStringList accountList = iProfile.keyValues(Buteo::KEY_ACCOUNT_ID);
    QStringList scopeList   = iProfile.keyValues(Buteo::KEY_REMOTE_DATABASE);
    QStringList unameList   = iProfile.keyValues(Buteo::KEY_USERNAME);
    QStringList paswdList   = iProfile.keyValues(Buteo::KEY_PASSWORD);
    if (!accountList.isEmpty()) {
        QString aId = accountList.first();
        if (aId != NULL) {
            mAccountId = aId.toInt();
        }
    } else {
        return false;
    }

    if (!scopeList.isEmpty()) {
        scope = scopeList.first();
    }
    mAuth = new AuthHandler(mAccountId, scope);
    if (!mAuth->init()) {
        return false;
    }
    connect(mAuth, SIGNAL(success()), this, SLOT(start()));
    connect(mAuth, SIGNAL(failed()), this, SLOT(authenticationError()));

    mSettings.setIgnoreSSLErrors(true);
    mSettings.setUrl(scope);
    if (!unameList.isEmpty())
        mSettings.setUsername(unameList.first());
    if (!paswdList.isEmpty())
        mSettings.setPassword(paswdList.first());
    mSettings.setAccountId(mAccountId);

    mSyncDirection = iProfile.syncDirection();
    mConflictResPolicy = iProfile.conflictResolutionPolicy();

    return true;
}

void CalDavClient::receiveStateChanged(Sync::SyncProgressDetail aState)
{
    FUNCTION_CALL_TRACE;

    switch(aState) {
    case Sync::SYNC_PROGRESS_SENDING_ITEMS: {
        emit syncProgressDetail (getProfileName(), Sync::SYNC_PROGRESS_SENDING_ITEMS);
        break;
    }
    case Sync::SYNC_PROGRESS_RECEIVING_ITEMS: {
        emit syncProgressDetail (getProfileName(), Sync::SYNC_PROGRESS_RECEIVING_ITEMS);
        break;
    }
    case Sync::SYNC_PROGRESS_FINALISING: {
        emit syncProgressDetail (getProfileName(),Sync::SYNC_PROGRESS_FINALISING);
        break;
    }
    default:
        //do nothing
        break;
    };
}

void CalDavClient::receiveSyncFinished(Sync::SyncStatus aState) {
    FUNCTION_CALL_TRACE;

    switch(aState)
    {
        case Sync::SYNC_ERROR:
        case Sync::SYNC_AUTHENTICATION_FAILURE:
        case Sync::SYNC_DATABASE_FAILURE:
        case Sync::SYNC_CONNECTION_ERROR:
        case Sync::SYNC_NOTPOSSIBLE:
        {
            emit error( getProfileName(), "", aState);
            break;
        }
        case Sync::SYNC_ABORTED:
        case Sync::SYNC_DONE:
        {
            emit success( getProfileName(), QString::number(aState));
            break;
        }
        case Sync::SYNC_QUEUED:
        case Sync::SYNC_STARTED:
        case Sync::SYNC_PROGRESS:
        default:
        {
            emit error( getProfileName(), "", aState);
            break;
        }
    }
}

void CalDavClient::authenticationError() {
    emit syncFinished (Sync::SYNC_AUTHENTICATION_FAILURE);
}

const QDateTime CalDavClient::lastSyncTime() {
    FUNCTION_CALL_TRACE;

    Buteo::ProfileManager pm;
    Buteo::SyncProfile* sp = pm.syncProfile (iProfile.name());
    if (!sp->lastSuccessfulSyncTime().isNull ())
        return sp->lastSuccessfulSyncTime().addSecs(30);
    else
        return sp->lastSuccessfulSyncTime();
}

Buteo::SyncProfile::SyncDirection CalDavClient::syncDirection ()
{
    FUNCTION_CALL_TRACE;
    return mSyncDirection;
}

Buteo::SyncProfile::ConflictResolutionPolicy CalDavClient::conflictResolutionPolicy ()
{
    FUNCTION_CALL_TRACE;
    return mConflictResPolicy;
}

Buteo::SyncResults
CalDavClient::getSyncResults() const
{
    FUNCTION_CALL_TRACE;

    return mResults;
}

void CalDavClient::startSlowSync() {

    Accounts::Manager *manager = new Accounts::Manager();
    Accounts::Account *account  = manager->account(mAccountId);
    if (account != NULL) {
        mKCal::Notebook::Ptr notebook = mKCal::Notebook::Ptr(new mKCal::Notebook(account->displayName(), ""));
        notebook->setAccount(QString::number(mAccountId));
        notebook->setPluginName(getPluginName());
        notebook->setSyncProfile(getProfileName());

        mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr (new mKCal::ExtendedCalendar(KDateTime::Spec::LocalZone()));
        mKCal::ExtendedStorage::Ptr storage = calendar->defaultStorage(calendar);
        storage->open();
        bool status = storage->addNotebook(notebook);
        LOG_DEBUG("NOTEBOOK created " << status << "   UUID of NoteBook = " << notebook->uid());
        storage->close();
        calendar->close();

        Report *report = new Report(mNAManager, &mSettings);
        report->getAllEvents();
        connect(report, SIGNAL(finished()), this, SLOT(requestFinished()));
        connect(report, SIGNAL(finished()), report, SLOT(deleteLater()));
        connect(report, SIGNAL(syncError(Sync::SyncStatus)), this, SIGNAL(syncFinished(Sync::SyncStatus)));
    }
}

void CalDavClient::startQuickSync() {

    mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr ( new mKCal::ExtendedCalendar( KDateTime::Spec::LocalZone() ) );
    mKCal::ExtendedStorage::Ptr storage = calendar->defaultStorage(calendar);

    storage->open();
    storage->load(QDateTime::currentDateTime().addMonths(-6).date(), QDateTime::currentDateTime().addMonths(-6).date());
    KCalCore::Incidence::List *list = new KCalCore::Incidence::List();
    LOG_DEBUG("\n\n------------------>>>>>>>>>>>>>>>>> LAST SYNC TIME = " << lastSyncTime() << "\n\n\n\n");
    KDateTime date(lastSyncTime());

    qDebug() << storage->insertedIncidences(list, date);
    qDebug() << "Total Inserted incidences = " << list->count();
    int count = list->count();
    for(int index = 0; index < count; index++) {
        KCalCore::Incidence::Ptr incidence = list->at(index);
        Put *put = new Put(mNAManager, &mSettings);
        put->createEvent(incidence);
        connect(put, SIGNAL(finished()), put, SLOT(deleteLater()));
        connect(put, SIGNAL(syncError(Sync::SyncStatus)), this, SIGNAL(syncFinished(Sync::SyncStatus)));
    }

    list->clear();
    qDebug() << storage->modifiedIncidences(list, date);
    qDebug() << "Total Modified incidences = " << list->count();
    count = list->count();
    for(int index = 0; index < count; index++) {
        KCalCore::Incidence::Ptr incidence = list->at(index);
        Put *put = new Put(mNAManager, &mSettings);
        put->updateEvent(incidence);
        connect(put, SIGNAL(finished()), put, SLOT(deleteLater()));
        connect(put, SIGNAL(syncError(Sync::SyncStatus)), this, SIGNAL(syncFinished(Sync::SyncStatus)));
    }

    list->clear();
    qDebug() << storage->deletedIncidences(list, date);
    qDebug() << "Total Deleted incidences = " << list->count();
    count = list->count();
    for(int index = 0; index < count; index++) {
        KCalCore::Incidence::Ptr incidence = list->at(index);
        Delete *del = new Delete(mNAManager, &mSettings);
        QString uri = incidence->customProperty("buteo", "uri");
        LOG_DEBUG("Incidence URI =      " << uri);
        QString path;
        if (uri.isEmpty()) {
            path = incidence->uid();
        } else {
            path = uri.split("/", QString::SkipEmptyParts).last();
        }

        del->deleteEvent(uri);
        connect(del, SIGNAL(finished()), del, SLOT(deleteLater()));
        connect(del, SIGNAL(syncError(Sync::SyncStatus)), this, SIGNAL(syncFinished(Sync::SyncStatus)));
    }
    storage->close();
    calendar->close();

    Report *report = new Report(mNAManager, &mSettings);
    report->getAllETags();
    connect(report, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(report, SIGNAL(finished()), report, SLOT(deleteLater()));
    connect(report, SIGNAL(syncError(Sync::SyncStatus)), this, SIGNAL(syncFinished(Sync::SyncStatus)));
}

void CalDavClient::requestFinished() {
    emit syncFinished(Sync::SYNC_DONE);
}
