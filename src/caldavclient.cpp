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
#include "notebooksyncagent.h"

#include <caldavcalendardatabase.h>

#include <extendedcalendar.h>
#include <extendedstorage.h>
#include <notebook.h>

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
    , mDatabase(0)
    , mCalendar(0)
    , mStorage(0)
    , mFirstSync(true)
{
    FUNCTION_CALL_TRACE;
}

CalDavClient::~CalDavClient()
{
    FUNCTION_CALL_TRACE;

    delete mDatabase;
}

bool CalDavClient::init()
{
    FUNCTION_CALL_TRACE;

    if (lastSyncTime().isNull()) {
        mFirstSync = true;
    } else {
        mFirstSync = false;
    }

    mNAManager = new QNetworkAccessManager(this);
    mDatabase = new CalDavCalendarDatabase;
    connect(mDatabase, SIGNAL(writeStatusChanged()), this, SLOT(databaseWriteStatusChanged()));

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

    if (!mDatabase) {
        mDatabase = new CalDavCalendarDatabase;
    }

    QString accountIdString = iProfile.key(Buteo::KEY_ACCOUNT_ID);
    int accountId = accountIdString.toInt();
    if (accountId == 0) {
        LOG_CRITICAL("profile does not specify" << Buteo::KEY_ACCOUNT_ID);
        return false;
    }

    mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
    mKCal::ExtendedStorage::Ptr storage = mKCal::ExtendedCalendar::defaultStorage(calendar);
    if (!storage->open()) {
        calendar->close();
        LOG_CRITICAL("unable to open calendar storage");
        return false;
    }

    deleteNotebooksForAccount(accountId, calendar, storage);
    storage->close();
    calendar->close();
    return true;
}

void CalDavClient::deleteNotebooksForAccount(int accountId, mKCal::ExtendedCalendar::Ptr calendar, mKCal::ExtendedStorage::Ptr storage)
{
    FUNCTION_CALL_TRACE;

    QString notebookAccountPrefix = QString::number(accountId) + "-";
    mKCal::Notebook::List notebookList = storage->notebooks();
    LOG_DEBUG("Total Number of Notebooks in device = " << notebookList.count());
    int deletedCount = 0;
    Q_FOREACH (mKCal::Notebook::Ptr notebook, notebookList) {
        if (notebook->account().startsWith(notebookAccountPrefix)) {
            if (storage->loadNotebookIncidences(notebook->uid())) {
                calendar->deleteAllIncidences();
            } else {
                LOG_WARNING("Unable to load incidences for notebook:" << notebook->uid() << "for account:" << accountId);
            }
            if (storage->deleteNotebook(notebook)) {
                deletedCount++;
            } else {
                LOG_WARNING("Unable to delete notebook:" << notebook->uid() << "for account:" << accountId);
            }
            mDatabase->removeEntries(notebook->uid());
        }
    }
    LOG_DEBUG("Deleted" << deletedCount << "notebooks");
    if (deletedCount > 0 && !storage->save()) {
        LOG_CRITICAL("Unable to save calendar storage after deleting notebooks");
    } else {
        mDatabase->commit();
    }
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

    if (!mDatabase->isValid()) {
        LOG_CRITICAL("Invalid database!");
        return false;
    }

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

    clearAgents();

    if (mFirstSync) {
        if (minorErrorCode == Buteo::SyncResults::NO_ERROR) {
            // Set the lastSyncTime after the calendar data is
            // written locally to the mkcal db.
            mSyncStartTime = QDateTime::currentDateTime().toUTC().addSecs(2);
            LOG_DEBUG("\n\n++++++++++++++ First sync mSyncStartTime:" << mSyncStartTime << "LAST SYNC:" << lastSyncTime());
        } else {
            if (mDatabase->writeStatus() != CalDavCalendarDatabase::Error) {
                deleteNotebooksForAccount(mSettings.accountId(), mCalendar, mStorage);
            }
        }
    }
    if (mCalendar) {
        mCalendar->close();
    }
    if (mStorage) {
        mStorage->close();
    }

    if (minorErrorCode == Buteo::SyncResults::NO_ERROR) {
        LOG_DEBUG("CalDAV sync succeeded!" << message);
        mResults = Buteo::SyncResults(mSyncStartTime,
                                      Buteo::SyncResults::SYNC_RESULT_SUCCESS,
                                      Buteo::SyncResults::NO_ERROR);
        emit success(getProfileName(), message);
    } else {
        LOG_CRITICAL("CalDAV sync failed:" << minorErrorCode << message);
        mResults = Buteo::SyncResults(lastSyncTime(),       // don't change the last sync time
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

void CalDavClient::getSyncDateRange(const QDateTime &sourceDate, QDateTime *fromDateTime, QDateTime *toDateTime)
{
    if (!fromDateTime || !toDateTime) {
        LOG_CRITICAL("fromDate or toDate is invalid");
        return;
    }
    *fromDateTime = sourceDate.addMonths(-6);
    *toDateTime = sourceDate.addMonths(12);
}

void CalDavClient::start()
{
    FUNCTION_CALL_TRACE;

    if (!mAuth->username().isEmpty() && !mAuth->password().isEmpty()) {
        mSettings.setUsername(mAuth->username());
        mSettings.setPassword(mAuth->password());
    }
    mSettings.setAuthToken(mAuth->token());

    QList<Settings::CalendarInfo> allCalendarInfo = mSettings.calendars();
    if (allCalendarInfo.isEmpty()) {
        syncFinished(Buteo::SyncResults::NO_ERROR, "No calendars for this account");
        return;
    }
    mCalendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
    mStorage = mKCal::ExtendedCalendar::defaultStorage(mCalendar);
    if (!mStorage->open()) {
        syncFinished(Buteo::SyncResults::DATABASE_FAILURE, "unable to open calendar storage");
        return;
    }

    QDateTime fromDateTime;
    QDateTime toDateTime;
    KCalCore::Incidence::List calendarIncidences;
    mKCal::Notebook::List notebooks;
    if (mFirstSync) {
        mSyncStartTime = QDateTime();
        getSyncDateRange(QDateTime::currentDateTime().toUTC(), &fromDateTime, &toDateTime);
    } else {
        mSyncStartTime = QDateTime::currentDateTime().toUTC();
        getSyncDateRange(mSyncStartTime, &fromDateTime, &toDateTime);
        if (!mStorage->load(fromDateTime.date(), toDateTime.date())) {
            syncFinished(Buteo::SyncResults::DATABASE_FAILURE, "unable to load calendar storage");
            return;
        }
        calendarIncidences = mCalendar->incidences(fromDateTime.date(), toDateTime.date());
        notebooks = mStorage->notebooks();
    }
    LOG_DEBUG("\n\n++++++++++++++ mSyncStartTime:" << mSyncStartTime << "LAST SYNC:" << lastSyncTime());

    Q_FOREACH (const Settings::CalendarInfo &calendarInfo, allCalendarInfo) {
        mKCal::Notebook::Ptr existingNotebook;
        Q_FOREACH (mKCal::Notebook::Ptr notebook, notebooks) {
            if (notebook->account() == mSettings.notebookId(calendarInfo.serverPath)) {
                existingNotebook = notebook;
                break;
            }
        }
        if (existingNotebook) {
            NotebookSyncAgent *agent = new NotebookSyncAgent(mCalendar, mStorage, mDatabase, mNAManager, &mSettings, calendarInfo.serverPath, this);
            connect(agent, SIGNAL(finished(int,QString)),
                    this, SLOT(notebookSyncFinished(int,QString)));
            mNotebookSyncAgents.append(agent);
            agent->startQuickSync(existingNotebook, lastSyncTime(), calendarIncidences, fromDateTime, toDateTime);
        } else {
            NotebookSyncAgent *agent = new NotebookSyncAgent(mCalendar, mStorage, mDatabase, mNAManager, &mSettings, calendarInfo.serverPath, this);
            connect(agent, SIGNAL(finished(int,QString)),
                    this, SLOT(notebookSyncFinished(int,QString)));
            mNotebookSyncAgents.append(agent);
            agent->startSlowSync(calendarInfo.displayName,
                                 mSettings.notebookId(calendarInfo.serverPath),
                                 getPluginName(),
                                 getProfileName(),
                                 calendarInfo.color,
                                 fromDateTime,
                                 toDateTime);
        }
    }
    if (mNotebookSyncAgents.isEmpty()) {
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR, "Could not add or find existing notebooks for this account");
    }
}

void CalDavClient::clearAgents()
{
    FUNCTION_CALL_TRACE;

    for (int i=0; i<mNotebookSyncAgents.count(); i++) {
        mNotebookSyncAgents[i]->deleteLater();
    }
    mNotebookSyncAgents.clear();
}

void CalDavClient::databaseWriteStatusChanged()
{
    FUNCTION_CALL_TRACE;

    CalDavCalendarDatabase *db = qobject_cast<CalDavCalendarDatabase*>(sender());

    if (db->writeStatus() == CalDavCalendarDatabase::Error) {
        syncFinished(Buteo::SyncResults::DATABASE_FAILURE, QString("Unable to write to database"));
    } else if (db->writeStatus() == CalDavCalendarDatabase::Finished) {
        syncFinished(Buteo::SyncResults::NO_ERROR, QString());
    }
}

void CalDavClient::notebookSyncFinished(int errorCode, const QString &errorString)
{
    FUNCTION_CALL_TRACE;
    LOG_CRITICAL("Notebook sync finished. Total agents:" << mNotebookSyncAgents.count());

    NotebookSyncAgent *agent = qobject_cast<NotebookSyncAgent*>(sender());
    agent->disconnect(this);

    if (errorCode != Buteo::SyncResults::NO_ERROR) {
        syncFinished(errorCode, errorString);
        return;
    }
    bool finished = true;
    for (int i=0; i<mNotebookSyncAgents.count(); i++) {
        if (!mNotebookSyncAgents[i]->isFinished()) {
            finished = false;
            break;
        }
    }
    if (finished) {
        for (int i=0; i<mNotebookSyncAgents.count(); i++) {
            if (!mNotebookSyncAgents[i]->applyRemoteChanges()) {
                syncFinished(Buteo::SyncResults::INTERNAL_ERROR, QStringLiteral("unable to write notebook changes"));
                return;
            }
        }
        if (mStorage->save()) {
            for (int i=0; i<mNotebookSyncAgents.count(); i++) {
                mNotebookSyncAgents[i]->finalize();
            }
            LOG_DEBUG("Any database changes to write? (including clearing of entries)" << mDatabase->hasChanges());
            if (mDatabase->hasChanges()) {
                // commit and wait for database changes to be written
                mDatabase->commit();
                mDatabase->wait();
                if (mDatabase->writeStatus() == CalDavCalendarDatabase::Error) {
                    syncFinished(Buteo::SyncResults::DATABASE_FAILURE, QString("Unable to write to database"));
                } else if (mDatabase->writeStatus() == CalDavCalendarDatabase::Finished) {
                    syncFinished(Buteo::SyncResults::NO_ERROR, QString());
                }
            } else {
                syncFinished(errorCode, errorString);
            }
        } else {
            syncFinished(Buteo::SyncResults::DATABASE_FAILURE, QStringLiteral("unable to save calendar storage"));
        }
    }
}
