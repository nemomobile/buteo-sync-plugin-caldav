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
                                         Buteo::PluginCbInterface *aCbInterface)
{
    return new CalDavClient(aPluginName, aProfile, aCbInterface);
}

extern "C" void destroyPlugin(CalDavClient *aClient)
{
    delete aClient;
}

static const QString VCalExtension = QStringLiteral(".ics");

CalDavClient::CalDavClient(const QString& aPluginName,
                            const Buteo::SyncProfile& aProfile,
                            Buteo::PluginCbInterface *aCbInterface)
    : ClientPlugin(aPluginName, aProfile, aCbInterface)
    , mManager(NULL)
    , mAuth(NULL)
    , mSlowSync(true)
{
    FUNCTION_CALL_TRACE;
}

CalDavClient::~CalDavClient()
{
    FUNCTION_CALL_TRACE;
}

bool CalDavClient::init()
{
    FUNCTION_CALL_TRACE;

    if (lastSyncTime().isNull()) {
        mSlowSync = true;
    } else {
        mSlowSync = false;
    }

    mNAManager = new QNetworkAccessManager;

    if (initConfig()) {
        return true;
    } else {
        // Uninitialize everything that was initialized before failure.
        uninit();
        return false;
    }
}

bool CalDavClient::uninit()
{
    FUNCTION_CALL_TRACE;
    return true;
}

bool CalDavClient::startSync()
{
    FUNCTION_CALL_TRACE;

    if (!mAuth)
        return false;

    mAuth->authenticate();

    LOG_DEBUG ("Init done. Continuing with sync");

    return true;
}

void CalDavClient::abortSync(Sync::SyncStatus aStatus)
{
    FUNCTION_CALL_TRACE;
    Sync::SyncStatus state = Sync::SYNC_ABORTED;

    if (aStatus == Sync::SYNC_ERROR) {
        state = Sync::SYNC_CONNECTION_ERROR;
    }

    if (!this->abort(state)) {
        LOG_DEBUG("Agent not active, aborting immediately");
        syncFinished(Sync::SYNC_ABORTED);

    } else {
        LOG_DEBUG("Agent active, abort event posted");
    }
}

bool CalDavClient::start()
{
    FUNCTION_CALL_TRACE;

    if (!mAuth->username().isEmpty() && !mAuth->password().isEmpty()) {
        mSettings.setUsername(mAuth->username());
        mSettings.setPassword(mAuth->password());
    }
    mSettings.setAuthToken(mAuth->token());

    switch (mSyncDirection) {
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
        // flow through
    default:
        // throw configuration error
        break;
    };

    return true;
}

bool CalDavClient::abort(Sync::SyncStatus status)
{
    Q_UNUSED(status)
    syncFinished(Sync::SYNC_ABORTED);
    return true;
}

bool CalDavClient::cleanUp()
{
    FUNCTION_CALL_TRACE;
    QStringList accountList = iProfile.keyValues(Buteo::KEY_ACCOUNT_ID);
    int accountId = 0;
    if (!accountList.isEmpty()) {
        QString aId = accountList.first();
        if (aId != NULL) {
            accountId = aId.toInt();
        }
    }

    mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
    mKCal::ExtendedStorage::Ptr storage = calendar->defaultStorage(calendar);
    storage->open();
    QString aId = QString::number(accountId);
    QString nbUid;
    mKCal::Notebook::List notebookList = storage->notebooks();
    LOG_DEBUG("Total Number of Notebooks in device = " << notebookList.count());
    Q_FOREACH (mKCal::Notebook::Ptr nbPtr, notebookList) {
        if(nbPtr->account() == aId) {
            nbUid = nbPtr->uid();
            storage->loadNotebookIncidences(nbUid);
            calendar->deleteAllIncidences();
            storage->deleteNotebook(nbPtr);
            break;
        }
    }
    storage->save();
    storage->close();
    calendar->close();

    if (nbUid.isNull() || nbUid.isEmpty()) {
        LOG_WARNING("Not able to find NoteBook's UID...... Won't Save Events ");
        syncFinished(Sync::SYNC_ERROR);
        return false;
    }

    return true;
}

void CalDavClient::connectivityStateChanged(Sync::ConnectivityType aType, bool aState)
{
    FUNCTION_CALL_TRACE;
    LOG_DEBUG("Received connectivity change event:" << aType << " changed to " << aState);
}

bool CalDavClient::initConfig()
{
    FUNCTION_CALL_TRACE;
    LOG_DEBUG("Initiating config...");

    mAccountId = 0;
    QString remoteDatabasePath = "";
    QStringList accountList = iProfile.keyValues(Buteo::KEY_ACCOUNT_ID);
    QStringList remotePaths = iProfile.keyValues(Buteo::KEY_REMOTE_DATABASE);
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

    if (!remotePaths.isEmpty()) {
        remoteDatabasePath = remotePaths.first();
    }
    // caldav plugin relies on the path ending with a separator
    if (!remoteDatabasePath.endsWith('/')) {
        remoteDatabasePath += '/';
    }
    if (!mManager) {
        mManager = new Accounts::Manager(this);
    }
    mAuth = new AuthHandler(mManager, mAccountId, remoteDatabasePath);
    if (!mAuth->init()) {
        return false;
    }
    connect(mAuth, SIGNAL(success()), this, SLOT(start()));
    connect(mAuth, SIGNAL(failed()), this, SLOT(authenticationError()));

    mSettings.setIgnoreSSLErrors(true);
    mSettings.setUrl(remoteDatabasePath);
    if (!unameList.isEmpty())
        mSettings.setUsername(unameList.first());
    if (!paswdList.isEmpty())
        mSettings.setPassword(paswdList.first());
    mSettings.setAccountId(mAccountId);

    mSyncDirection = iProfile.syncDirection();
    mConflictResPolicy = iProfile.conflictResolutionPolicy();

    return true;
}

void CalDavClient::syncFinished(Sync::SyncStatus syncStatus)
{
    FUNCTION_CALL_TRACE;

    int minorErrorCode = -1;

    switch (syncStatus)
    {
    case Sync::SYNC_QUEUED:
    case Sync::SYNC_STARTED:
    case Sync::SYNC_PROGRESS:
        // no error, sync has not finished
        break;
    case Sync::SYNC_ERROR:
        minorErrorCode = Buteo::SyncResults::INTERNAL_ERROR;
        break;
    case Sync::SYNC_DONE:
        minorErrorCode = Buteo::SyncResults::NO_ERROR;
        break;
    case Sync::SYNC_ABORTED:
        minorErrorCode = Buteo::SyncResults::ABORTED;
        break;
    case Sync::SYNC_CANCELLED:
        minorErrorCode = Buteo::SyncResults::ABORTED;
        break;
    case Sync::SYNC_STOPPING:
        minorErrorCode = Buteo::SyncResults::ABORTED;
        break;
    case Sync::SYNC_NOTPOSSIBLE:
        minorErrorCode = Buteo::SyncResults::INTERNAL_ERROR;
        break;
    case Sync::SYNC_AUTHENTICATION_FAILURE:
        minorErrorCode = Buteo::SyncResults::AUTHENTICATION_FAILURE;
        break;
    case Sync::SYNC_DATABASE_FAILURE:
        minorErrorCode = Buteo::SyncResults::DATABASE_FAILURE;
        break;
    case Sync::SYNC_CONNECTION_ERROR:
        minorErrorCode = Buteo::SyncResults::CONNECTION_ERROR;
        break;
    case Sync::SYNC_SERVER_FAILURE:
        minorErrorCode = Buteo::SyncResults::INTERNAL_ERROR;
        break;
    case Sync::SYNC_BAD_REQUEST:
        minorErrorCode = Buteo::SyncResults::INTERNAL_ERROR;
        break;
    }

    if (minorErrorCode == Buteo::SyncResults::NO_ERROR) {
        mResults = Buteo::SyncResults(QDateTime::currentDateTime().toUTC(),
                                      Buteo::SyncResults::SYNC_RESULT_SUCCESS,
                                      Buteo::SyncResults::NO_ERROR);
        emit success(getProfileName(), QString::number(syncStatus));
    } else if (minorErrorCode > 0) {
        mResults = Buteo::SyncResults(QDateTime::currentDateTime().toUTC(),
                                      Buteo::SyncResults::SYNC_RESULT_FAILED,
                                      minorErrorCode);
        emit error(getProfileName(), "", syncStatus);
    }
}

void CalDavClient::authenticationError()
{
    syncFinished(Sync::SYNC_AUTHENTICATION_FAILURE);
}

QDateTime CalDavClient::lastSyncTime()
{
    return iProfile.lastSuccessfulSyncTime();
}

Buteo::SyncProfile::SyncDirection CalDavClient::syncDirection()
{
    FUNCTION_CALL_TRACE;
    return mSyncDirection;
}

Buteo::SyncProfile::ConflictResolutionPolicy CalDavClient::conflictResolutionPolicy()
{
    FUNCTION_CALL_TRACE;
    return mConflictResPolicy;
}

Buteo::SyncResults CalDavClient::getSyncResults() const
{
    FUNCTION_CALL_TRACE;

    return mResults;
}

void CalDavClient::startSlowSync()
{
    FUNCTION_CALL_TRACE;

    if (!mManager) {
        mManager = new Accounts::Manager(this);
    }
    Accounts::Account *account  = mManager->account(mAccountId);
    if (account != NULL) {
        mKCal::Notebook::Ptr notebook = mKCal::Notebook::Ptr(new mKCal::Notebook(account->displayName(), ""));
        notebook->setAccount(QString::number(mAccountId));
        notebook->setPluginName(getPluginName());
        notebook->setSyncProfile(getProfileName());

        mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
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
        connect(report, SIGNAL(syncError(Sync::SyncStatus)), this, SLOT(syncFinished(Sync::SyncStatus)));
    }
}

void CalDavClient::startQuickSync()
{
    FUNCTION_CALL_TRACE;

    mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
    mKCal::ExtendedStorage::Ptr storage = calendar->defaultStorage(calendar);

    storage->open();
    storage->load(QDateTime::currentDateTime().toUTC().addMonths(-6).date(),
                  QDateTime::currentDateTime().toUTC().addMonths(12).date());
    KCalCore::Incidence::List *list = new KCalCore::Incidence::List();

    // we add 2 seconds to ensure that the timestamp doesn't
    // fall prior to when the calendar db commit fs sync finalises.
    KDateTime date(lastSyncTime().addSecs(2));

    LOG_DEBUG("\n\n------------------>>>>>>>>>>>>>>>>> LAST SYNC TIME = " << date.toString() << "\n\n\n\n");

    LOG_DEBUG("Inserted incidences:" << storage->insertedIncidences(list, date));
    LOG_DEBUG("Total Inserted incidences = " << list->count());
    int count = list->count();
    for(int index = 0; index < count; index++) {
        KCalCore::Incidence::Ptr incidence = list->at(index);
        Put *put = new Put(mNAManager, &mSettings);
        put->createEvent(incidence);
        connect(put, SIGNAL(finished()), put, SLOT(deleteLater()));
        connect(put, SIGNAL(syncError(Sync::SyncStatus)), this, SLOT(syncFinished(Sync::SyncStatus)));
    }

    list->clear();
    LOG_DEBUG("Modified incidences:" << storage->modifiedIncidences(list, date));
    LOG_DEBUG("Total Modified incidences = " << list->count());
    count = list->count();
    for(int index = 0; index < count; index++) {
        KCalCore::Incidence::Ptr incidence = list->at(index);
        Put *put = new Put(mNAManager, &mSettings);
        put->updateEvent(incidence);
        connect(put, SIGNAL(finished()), put, SLOT(deleteLater()));
        connect(put, SIGNAL(syncError(Sync::SyncStatus)), this, SLOT(syncFinished(Sync::SyncStatus)));
    }

    list->clear();
    LOG_DEBUG("Deleted incidences:" << storage->deletedIncidences(list, date));
    LOG_DEBUG("Total Deleted incidences = " << list->count());
    count = list->count();
    for(int index = 0; index < count; index++) {
        KCalCore::Incidence::Ptr incidence = list->at(index);
        Delete *del = new Delete(mNAManager, &mSettings);
        QString uri = incidence->customProperty("buteo", "uri");
        LOG_DEBUG("Incidence URI =      " << uri);
        QString path;
        if (uri.isEmpty()) {
            path = incidence->uid();
            if (!path.isEmpty()) {
                path += VCalExtension;
            }
        } else {
            path = uri.split("/", QString::SkipEmptyParts).last();
        }
        del->deleteEvent(path);
        connect(del, SIGNAL(finished()), del, SLOT(deleteLater()));
        connect(del, SIGNAL(syncError(Sync::SyncStatus)), this, SLOT(syncFinished(Sync::SyncStatus)));
    }
    storage->close();
    calendar->close();

    Report *report = new Report(mNAManager, &mSettings);
    report->getAllETags();
    connect(report, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(report, SIGNAL(finished()), report, SLOT(deleteLater()));
    connect(report, SIGNAL(syncError(Sync::SyncStatus)), this, SLOT(syncFinished(Sync::SyncStatus)));
}

void CalDavClient::requestFinished()
{
    FUNCTION_CALL_TRACE;
    LOG_DEBUG("Request finished at" << lastSyncTime());
    syncFinished(Sync::SYNC_DONE);
}
