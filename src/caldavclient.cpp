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
    , mManager(0)
    , mAuth(0)
    , mCalendar(0)
    , mStorage(0)
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

    mNAManager = new QNetworkAccessManager(this);

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

    // This function is called after the account has been deleted to allow the plugin to remove
    // all the notebooks associated with the account.

    QString accountIdString = iProfile.key(Buteo::KEY_ACCOUNT_ID);
    if (accountIdString.isEmpty()) {
        LOG_CRITICAL("profile does not specify" << Buteo::KEY_ACCOUNT_ID);
        return false;
    }
    QString notebookAccountPrefix = accountIdString + "-";

    mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
    mKCal::ExtendedStorage::Ptr storage = calendar->defaultStorage(calendar);
    if (!storage->open()) {
        calendar->close();
        LOG_CRITICAL("unable to open calendar storage");
        return false;
    }

    mKCal::Notebook::List notebookList = storage->notebooks();
    LOG_DEBUG("Total Number of Notebooks in device = " << notebookList.count());
    int deletedCount = 0;
    Q_FOREACH (mKCal::Notebook::Ptr notebook, notebookList) {
        if (notebook->account().startsWith(notebookAccountPrefix)) {
            if (storage->loadNotebookIncidences(notebook->uid())) {
                calendar->deleteAllIncidences();   
            } else {
                LOG_WARNING("Unable to load incidences for notebook:" << notebook->uid() << "for account:" << accountIdString);
            }
            if (storage->deleteNotebook(notebook)) {
                deletedCount++;
            } else {
                LOG_WARNING("Unable to delete notebook:" << notebook->uid() << "for account:" << accountIdString);
            }
        }
    }
    LOG_DEBUG("Deleted" << deletedCount << "notebooks");
    if (deletedCount > 0 && !storage->save()) {
        LOG_CRITICAL("Unable to save calendar storage");
    }
    storage->close();
    calendar->close();
    return true;
}

void CalDavClient::connectivityStateChanged(Sync::ConnectivityType aType, bool aState)
{
    FUNCTION_CALL_TRACE;
    LOG_DEBUG("Received connectivity change event:" << aType << " changed to " << aState);
}

QList<Settings::CalendarInfo> CalDavClient::loadCalendars(Accounts::Account *account, Accounts::Service srv) const
{
    if (!account || !srv.isValid()) {
        return QList<Settings::CalendarInfo>();
    }
    account->selectService(srv);
    QStringList calendarPaths = account->value("calendars").toStringList();
    QStringList enabledCalendars = account->value("enabled_calendars").toStringList();
    QStringList displayNames = account->value("calendar_display_names").toStringList();
    QStringList colors = account->value("calendar_colors").toStringList();
    account->selectService(Accounts::Service());

    if (enabledCalendars.count() > calendarPaths.count()
            || calendarPaths.count() != displayNames.count()
            || calendarPaths.count() != colors.count()) {
        LOG_CRITICAL("Bad calendar data for account" << account->id() << "and service" << srv.name());
        return QList<Settings::CalendarInfo>();
    }
    QList<Settings::CalendarInfo> allCalendarInfo;
    for (int i=0; i<calendarPaths.count(); i++) {
        if (!enabledCalendars.contains(calendarPaths[i])) {
            continue;
        }
        Settings::CalendarInfo info = { calendarPaths[i], displayNames[i], colors[i] };
        allCalendarInfo << info;
    }
    return allCalendarInfo;
}

bool CalDavClient::initConfig()
{
    FUNCTION_CALL_TRACE;
    LOG_DEBUG("Initiating config...");

    if (!mManager) {
        mManager = new Accounts::Manager(this);
    }

    QString accountIdString = iProfile.key(Buteo::KEY_ACCOUNT_ID);
    QString serviceName = iProfile.key(KEY_ACCOUNT_SERVICE_NAME);
    bool accountIdOk = false;
    int accountId = accountIdString.toInt(&accountIdOk);
    if (!accountIdOk) {
        LOG_CRITICAL("no account id specified," << Buteo::KEY_ACCOUNT_ID << "not found in profile");
        return false;
    }
    if (serviceName.isEmpty()) {
        LOG_CRITICAL("no service name specified," << KEY_ACCOUNT_SERVICE_NAME << "not found in profile");
        return false;
    }
    Accounts::Account *account = mManager->account(accountId);
    if (!account) {
        LOG_CRITICAL("cannot find account" << accountId);
        return false;
    }
    Accounts::Service srv = mManager->service(serviceName);
    if (!srv.isValid()) {
        LOG_CRITICAL("cannot load service" << serviceName);
        return false;
    }

    mSettings.setCalendars(loadCalendars(account, srv));
    if (mSettings.calendars().isEmpty()) {
        LOG_CRITICAL("no calendars found");
        return false;
    }

    account->selectService(srv);
    mSettings.setServerAddress(account->value("server_address").toString());
    account->selectService(Accounts::Service());
    if (mSettings.serverAddress().isEmpty()) {
        LOG_CRITICAL("remote_address not found in service settings");
        return false;
    }

    mAuth = new AuthHandler(mManager, accountId, serviceName);
    if (!mAuth->init()) {
        return false;
    }
    connect(mAuth, SIGNAL(success()), this, SLOT(start()));
    connect(mAuth, SIGNAL(failed()), this, SLOT(authenticationError()));

    mSettings.setIgnoreSSLErrors(true);
    mSettings.setAccountId(accountId);

    mSyncDirection = iProfile.syncDirection();
    mConflictResPolicy = iProfile.conflictResolutionPolicy();

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

    QList<Settings::CalendarInfo> allCalendarInfo = mSettings.calendars();
    if (allCalendarInfo.isEmpty()) {
        syncFinished(Buteo::SyncResults::NO_ERROR, "No calendars for this account");
        return;
    }
    mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
    mKCal::ExtendedStorage::Ptr storage = calendar->defaultStorage(calendar);
    if (!storage->open()) {
        calendar->close();
        syncFinished(Buteo::SyncResults::DATABASE_FAILURE, "unable to open calendar storage");
        return;
    }
    Q_FOREACH (const Settings::CalendarInfo &calendarInfo, allCalendarInfo) {
        mKCal::Notebook::Ptr notebook = mKCal::Notebook::Ptr(new mKCal::Notebook(calendarInfo.displayName, ""));
        notebook->setAccount(mSettings.notebookId(calendarInfo.serverPath));
        notebook->setPluginName(getPluginName());
        notebook->setSyncProfile(getProfileName());
        notebook->setColor(calendarInfo.color);
        if (!storage->addNotebook(notebook)) {
            storage->close();
            calendar->close();
            syncFinished(Buteo::SyncResults::INTERNAL_ERROR, "unable to add notebook " + notebook->uid() + " for account/calendar " + notebook->account());
            return;
        }
        LOG_DEBUG("NOTEBOOK created" << notebook->uid() << "account:" << notebook->account());
    }

    mCalendar = calendar;
    mStorage = storage;

    Q_FOREACH (const Settings::CalendarInfo &calendarInfo, allCalendarInfo) {
        Report *report = new Report(mNAManager, &mSettings, mCalendar, mStorage, this);
        mRequests.insert(report);
        connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
        report->getAllEvents(calendarInfo.serverPath);
    }
}

bool CalDavClient::loadStorageChanges(mKCal::ExtendedStorage::Ptr storage,
                                      const QString &notebookUid,
                                      const KDateTime &fromDate,
                                      KCalCore::Incidence::List *inserted,
                                      KCalCore::Incidence::List *modified,
                                      KCalCore::Incidence::List *deleted,
                                      QString *error)
{
    FUNCTION_CALL_TRACE;

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

    QList<Settings::CalendarInfo> allCalendarInfo = mSettings.calendars();
    if (allCalendarInfo.isEmpty()) {
        syncFinished(Buteo::SyncResults::NO_ERROR, "No calendars for this account");
        return;
    }
    mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
    mKCal::ExtendedStorage::Ptr storage = calendar->defaultStorage(calendar);
    if (!storage->open()) {
        syncFinished(Buteo::SyncResults::DATABASE_FAILURE, "unable to open calendar storage");
        return;
    }
    if (!storage->load(QDateTime::currentDateTime().toUTC().addMonths(-6).date(),
                      QDateTime::currentDateTime().toUTC().addMonths(12).date())) {
        storage->close();
        syncFinished(Buteo::SyncResults::DATABASE_FAILURE, "unable to load calendar storage");
        return;
    }
    mCalendar = calendar;
    mStorage = storage;
    mKCal::Notebook::List notebookList = storage->notebooks();
    Q_FOREACH (mKCal::Notebook::Ptr notebook, notebookList) {
        Q_FOREACH (const Settings::CalendarInfo &calendarInfo, allCalendarInfo) {
            if (notebook->account() == mSettings.notebookId(calendarInfo.serverPath)) {
                syncNotebookChanges(storage, notebook, calendarInfo.serverPath);
                break;
            }
        }
    }
}

void CalDavClient::syncNotebookChanges(mKCal::ExtendedStorage::Ptr storage, mKCal::Notebook::Ptr notebook, const QString &serverPath)
{
    FUNCTION_CALL_TRACE;

    // we add 2 seconds to ensure that the timestamp doesn't
    // fall prior to when the calendar db commit fs sync finalises.
    KDateTime fromDate(lastSyncTime().addSecs(2));
    LOG_DEBUG("\n\nLAST SYNC TIME = " << fromDate.toString() << "\n\n");

    KCalCore::Incidence::List inserted;
    KCalCore::Incidence::List modified;
    KCalCore::Incidence::List deleted;
    QString errorString;
    if (!loadStorageChanges(storage, notebook->uid(), fromDate, &inserted, &modified, &deleted, &errorString)) {
        LOG_WARNING("Unable to load changes for calendar:" << serverPath);
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR, errorString);
        return;
    }
    LOG_DEBUG("Changes: inserted = " << inserted.count()
              << "modified = " << modified.count()
              << "deleted = " << deleted.count());
    if (inserted.isEmpty() && modified.isEmpty() && deleted.isEmpty()) {
        // no local changes to send, just do a REPORT to pull updates from server
        retrieveETags(serverPath);
    } else {
        for (int i=0; i<inserted.count(); i++) {
            Put *put = new Put(mNAManager, &mSettings, this);
            mRequests.insert(put);
            connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
            put->createEvent(serverPath, inserted[i]);
        }
        for (int i=0; i<modified.count(); i++) {
            Put *put = new Put(mNAManager, &mSettings, this);
            mRequests.insert(put);
            connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
            put->updateEvent(serverPath, modified[i]);
        }
        for (int i=0; i<deleted.count(); i++) {
            Delete *del = new Delete(mNAManager, &mSettings, this);
            mRequests.insert(del);
            connect(del, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
            del->deleteEvent(serverPath, deleted[i]);
        }
    }
}

void CalDavClient::nonReportRequestFinished()
{
    FUNCTION_CALL_TRACE;

    Request *request = qobject_cast<Request*>(sender());
    if (!request) {
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR, QStringLiteral("Invalid request object"));
        return;
    }
    mRequests.remove(request);
    request->deleteLater();

    if (request->errorCode() != Buteo::SyncResults::NO_ERROR) {
        qWarning() << "Aborting sync," << request->command() << "failed!" << request->errorString();
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR, request->errorString());
        return;
    }
    if (mRequests.isEmpty()) {
        // now send a REPORT to fetch the latest data for each calendar
        Q_FOREACH(const Settings::CalendarInfo &calendarInfo, mSettings.calendars()) {
            retrieveETags(calendarInfo.serverPath);
        }
    }
}

void CalDavClient::retrieveETags(const QString &serverPath)
{
    FUNCTION_CALL_TRACE;

    Report *report = new Report(mNAManager, &mSettings, mCalendar, mStorage, this);
    mRequests.insert(report);
    connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
    report->getAllETags(serverPath);
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

void CalDavClient::deleteIncidences(const KCalCore::Incidence::List &sourceList)
{
    FUNCTION_CALL_TRACE;

    if (sourceList.isEmpty()) {
        return;
    }
    // ExtendedCalendar::delete* methods only work for references provided by ExtendedCalendar
    // (and not those from ExtendedStorage) so load these references here.
    QDateTime dt = QDateTime::currentDateTime();
    KCalCore::Incidence::List realIncidenceRefs = mCalendar->incidences(dt.addMonths(-12).date(), dt.addMonths(12).date());
    QHash<QString, KCalCore::Incidence::Ptr> realIncidenceRefsMap;
    for (int i=0; i<realIncidenceRefs.count(); i++) {
        realIncidenceRefsMap.insert(realIncidenceRefs[i]->uid(), realIncidenceRefs[i]);
    }
    Q_FOREACH(const KCalCore::Incidence::Ptr &sourceIncidence, sourceList) {
        if (realIncidenceRefsMap.contains(sourceIncidence->uid())) {
            KCalCore::Incidence::Ptr realIncidenceRef = realIncidenceRefsMap.take(sourceIncidence->uid());
            switch (realIncidenceRef->type()) {
            case KCalCore::IncidenceBase::TypeEvent:
                if (!mCalendar->deleteEvent(realIncidenceRef.staticCast<KCalCore::Event>())) {
                    LOG_DEBUG("Unable to delete Event = " << realIncidenceRef->customProperty("buteo", "uri"));
                }
                break;
            case KCalCore::IncidenceBase::TypeTodo:
                if (!mCalendar->deleteTodo(realIncidenceRef.staticCast<KCalCore::Todo>())) {
                    LOG_DEBUG("Unable to delete Todo = " << realIncidenceRef->customProperty("buteo", "uri"));
                }
                break;
            case KCalCore::IncidenceBase::TypeJournal:
                if (!mCalendar->deleteJournal(realIncidenceRef.staticCast<KCalCore::Journal>())) {
                    LOG_DEBUG("Unable to delete Journal = " << realIncidenceRef->customProperty("buteo", "uri"));
                }
                break;
            default:
                break;
            }
        }
    }
}

void CalDavClient::reportRequestFinished()
{
    FUNCTION_CALL_TRACE;

    Report *request = qobject_cast<Report*>(sender());
    if (!request) {
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR, QStringLiteral("Invalid request object"));
        return;
    }
    mRequests.remove(request);
    request->deleteLater();
    LOG_DEBUG("Report request finished. Requests remaining:" << mRequests.count());

    mIncidencesToDelete += request->incidencesToDelete();

    if (request->errorCode() != Buteo::SyncResults::NO_ERROR) {
        syncFinished(request->errorCode(), request->errorString());
        return;
    }
    if (mRequests.isEmpty()) {
        deleteIncidences(mIncidencesToDelete);
        mIncidencesToDelete.clear();

        bool saved = mStorage->save();
        mStorage->close();
        mCalendar->close();
        if (saved) {
            syncFinished(request->errorCode(), request->errorString());
        } else {
            syncFinished(Buteo::SyncResults::DATABASE_FAILURE, QStringLiteral("unable to save calendar storage"));
        }
    }
}
