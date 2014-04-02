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

#define BUTEO_ENABLE_DEBUG

#include "caldavclient.h"
#include "report.h"
#include "put.h"
#include "delete.h"
#include "reader.h"

#include <extendedcalendar.h>
#include <extendedstorage.h>
#include <notebook.h>
#include <icalformat.h>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDateTime>

#include <Accounts/Manager>
#include <Accounts/Account>

#include <PluginCbInterface.h>
#include <LogMacros.h>
#include <ProfileEngineDefs.h>
#include <ProfileManager.h>

#define KEY_ACCOUNT_SERVICE_NAME "account_service_name"

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
    abort(aStatus);
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

void CalDavClient::abort(Sync::SyncStatus status)
{
    FUNCTION_CALL_TRACE;

    syncFinished(status, QStringLiteral("Sync aborted"));
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
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR, QStringLiteral("Cannot find notebook UID, cannot save any events"));
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

    QString accountIdString = iProfile.key(Buteo::KEY_ACCOUNT_ID);
    QString serviceName = iProfile.key(KEY_ACCOUNT_SERVICE_NAME);
    QString remoteDatabasePath = iProfile.key(Buteo::KEY_REMOTE_DATABASE);

    bool accountIdOk = false;
    int accountId = accountIdString.toInt(&accountIdOk);
    if (!accountIdOk) {
        LOG_WARNING("account id not found in profile");
        return false;
    }
    if (serviceName.isEmpty()) {
        LOG_WARNING("service name not found in profile");
        return false;
    }
    if (remoteDatabasePath.isEmpty()) {
        LOG_WARNING("remote database path not found in profile");
        return false;
    }

    // caldav plugin relies on the path ending with a separator
    if (!remoteDatabasePath.endsWith('/')) {
        remoteDatabasePath += '/';
    }

    if (!mManager) {
        mManager = new Accounts::Manager(this);
    }

    mAuth = new AuthHandler(mManager, accountId, serviceName, remoteDatabasePath);
    if (!mAuth->init()) {
        return false;
    }
    connect(mAuth, SIGNAL(success()), this, SLOT(start()));
    connect(mAuth, SIGNAL(failed()), this, SLOT(authenticationError()));

    mSettings.setIgnoreSSLErrors(true);
    mSettings.setUrl(remoteDatabasePath);
    mSettings.setAccountId(accountId);

    mSyncDirection = iProfile.syncDirection();
    mConflictResPolicy = iProfile.conflictResolutionPolicy();

    mAccountId = accountId;
    return true;
}

void CalDavClient::syncFinished(int minorErrorCode, const QString &message)
{
    FUNCTION_CALL_TRACE;

    clearRequests();

    if (minorErrorCode == Buteo::SyncResults::NO_ERROR) {
        LOG_DEBUG("CalDAV sync succeeded!" << message);
        mResults = Buteo::SyncResults(QDateTime::currentDateTime().toUTC(),
                                      Buteo::SyncResults::SYNC_RESULT_SUCCESS,
                                      Buteo::SyncResults::NO_ERROR);
        emit success(getProfileName(), message);
    } else {
        LOG_CRITICAL("CalDAV sync failed:" << minorErrorCode << message);
        mResults = Buteo::SyncResults(QDateTime::currentDateTime().toUTC(),
                                      Buteo::SyncResults::SYNC_RESULT_FAILED,
                                      minorErrorCode);
        emit error(getProfileName(), message, minorErrorCode);
    }
}

void CalDavClient::authenticationError()
{
    syncFinished(Buteo::SyncResults::AUTHENTICATION_FAILURE, QStringLiteral("Authentication failed"));
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
        mRequests.insert(report);
        connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
        report->getAllEvents();
    }
}

bool CalDavClient::loadStorageChanges(mKCal::ExtendedStorage::Ptr storage,
                                      const KDateTime &fromDate,
                                      KCalCore::Incidence::List *inserted,
                                      KCalCore::Incidence::List *modified,
                                      KCalCore::Incidence::List *deleted,
                                      QString *error)
{
    FUNCTION_CALL_TRACE;

    QString accountId = QString::number(mSettings.accountId());
    QString notebookUid;
    mKCal::Notebook::List notebookList = storage->notebooks();
    Q_FOREACH (mKCal::Notebook::Ptr notebook, notebookList) {
        if (notebook->account() == accountId) {
            notebookUid = notebook->uid();
            break;
        }
    }

    if (notebookUid.isEmpty()) {
        *error = "Cannot find mkCal::Notebook for account: " + accountId;
        return false;
    }

    if (!storage->insertedIncidences(inserted, fromDate, notebookUid)) {
        *error = "mKCal::ExtendedStorage::insertedIncidences() failed";
        return false;
    }
    if (!storage->modifiedIncidences(modified, fromDate, notebookUid)) {
        *error = "mKCal::ExtendedStorage::modifiedIncidences() failed";
        return false;
    }
    if (!storage->deletedIncidences(deleted, fromDate, notebookUid)) {
        *error = "mKCal::ExtendedStorage::deletedIncidences() failed";
        return false;
    }

    // If an event has changed to/from the caldav notebook and back since the last sync,
    // it will be present in both the inserted and deleted lists. In this case, nothing
    // has actually changed, so remove it from both lists.
    int removed = removeCommonIncidences(inserted, deleted);
    if (removed > 0) {
        LOG_DEBUG("Removed" << removed << "UIDs found in both inserted and removed lists");
    }

    return true;
}

int CalDavClient::removeCommonIncidences(KCalCore::Incidence::List *firstList, KCalCore::Incidence::List *secondList)
{
    QSet<QString> firstListUids;
    for (int i=0; i<firstList->count(); i++) {
        firstListUids.insert(firstList->at(i)->uid());
    }
    QSet<QString> commonUids;
    for (KCalCore::Incidence::List::iterator it = secondList->begin(); it != secondList->end();) {
        KCalCore::Incidence::Ptr incidence = *it;
        if (firstListUids.contains(incidence->uid())) {
            commonUids.insert(incidence->uid());
            it = secondList->erase(it);
        } else {
            ++it;
        }
    }
    int removed = commonUids.count();
    if (removed > 0) {
        for (KCalCore::Incidence::List::iterator it = firstList->begin(); it != firstList->end();) {
            KCalCore::Incidence::Ptr incidence = *it;
            if (commonUids.contains(incidence->uid())) {
                commonUids.remove(incidence->uid());
                it = firstList->erase(it);
            } else {
                ++it;
            }
        }
    }
    return removed;
}

void CalDavClient::startQuickSync()
{
    FUNCTION_CALL_TRACE;

    mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
    mKCal::ExtendedStorage::Ptr storage = calendar->defaultStorage(calendar);

    storage->open();
    storage->load(QDateTime::currentDateTime().toUTC().addMonths(-6).date(),
                  QDateTime::currentDateTime().toUTC().addMonths(12).date());

    // we add 2 seconds to ensure that the timestamp doesn't
    // fall prior to when the calendar db commit fs sync finalises.
    KDateTime fromDate(lastSyncTime().addSecs(2));
    LOG_DEBUG("\n\nLAST SYNC TIME = " << fromDate.toString() << "\n\n");

    KCalCore::Incidence::List inserted;
    KCalCore::Incidence::List modified;
    KCalCore::Incidence::List deleted;
    QString errorString;
    if (!loadStorageChanges(storage, fromDate, &inserted, &modified, &deleted, &errorString)) {
        storage->close();
        calendar->close();
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR, errorString);
        return;
    }
    LOG_DEBUG("Changes: inserted = " << inserted.count()
              << "modified = " << modified.count()
              << "deleted = " << deleted.count());
    if (inserted.isEmpty() && modified.isEmpty() && deleted.isEmpty()) {
        // no local changes to send, just do a REPORT to pull updates from server
        retrieveETags();
    } else {
        for (int i=0; i<inserted.count(); i++) {
            Put *put = new Put(mNAManager, &mSettings);
            mRequests.insert(put);
            connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
            put->createEvent(inserted[i]);
        }
        for (int i=0; i<modified.count(); i++) {
            Put *put = new Put(mNAManager, &mSettings);
            mRequests.insert(put);
            connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
            put->updateEvent(modified[i]);
        }
        for (int i=0; i<deleted.count(); i++) {
            Delete *del = new Delete(mNAManager, &mSettings);
            mRequests.insert(del);
            connect(del, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
            del->deleteEvent(deleted[i]);
        }
    }
    storage->close();
    calendar->close();
}

void CalDavClient::nonReportRequestFinished()
{
    FUNCTION_CALL_TRACE;

    Request *request = qobject_cast<Request*>(sender());
    if (!request) {
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR, QStringLiteral("Invalid request object"));
        return;
    }

    if (request->errorCode() != Buteo::SyncResults::NO_ERROR) {
        qWarning() << "Aborting sync," << request->command() << "failed!" << request->errorString();
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR, request->errorString());
        return;
    }

    mRequests.remove(request);
    request->deleteLater();

    if (mRequests.isEmpty()) {
        // now we can send a REPORT
        retrieveETags();
    }
}

void CalDavClient::retrieveETags()
{
    FUNCTION_CALL_TRACE;

    Report *report = new Report(mNAManager, &mSettings);
    mRequests.insert(report);
    connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
    report->getAllETags();
}

void CalDavClient::clearRequests()
{
    FUNCTION_CALL_TRACE;

    QList<Request *> requests = mRequests.toList();
    for (int i=0; i<requests.count(); i++) {
        requests[i]->deleteLater();
    }
    mRequests.clear();
}

void CalDavClient::reportRequestFinished()
{
    Report *request = qobject_cast<Report*>(sender());
    if (!request) {
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR, QStringLiteral("Invalid request object"));
        return;
    }

    if (request->errorCode() != Buteo::SyncResults::NO_ERROR) {
        qWarning() << "REPORT request failed!" << request->errorString();
    }
    syncFinished(request->errorCode(), request->errorString());
}
