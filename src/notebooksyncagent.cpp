/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2014 Jolla Ltd. and/or its subsidiary(-ies).
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
#include "notebooksyncagent.h"
#include "incidencehandler.h"
#include "settings.h"
#include "report.h"
#include "put.h"
#include "delete.h"
#include "reader.h"

#include <caldavcalendardatabase.h>

#include <LogMacros.h>
#include <SyncResults.h>

#include <incidence.h>
#include <icalformat.h>
#include <event.h>
#include <todo.h>
#include <journal.h>
#include <attendee.h>

#include <QDebug>


#define NOTEBOOK_FUNCTION_CALL_TRACE LOG_CRITICAL(Q_FUNC_INFO << (mNotebook ? mNotebook->account() : ""))

static KCalCore::Incidence::Ptr fetchIncidence(const mKCal::ExtendedCalendar::Ptr &calendar, const QString &uid)
{
    KCalCore::Event::Ptr event = calendar->event(uid);
    if (event) {
        return event;
    }
    KCalCore::Journal::Ptr journal = calendar->journal(uid);
    if (journal) {
        return journal;
    }
    KCalCore::Todo::Ptr todo = calendar->todo(uid);
    if (todo) {
        return todo;
    }
    return KCalCore::Incidence::Ptr();
}

NotebookSyncAgent::NotebookSyncAgent(mKCal::ExtendedCalendar::Ptr calendar,
                                     mKCal::ExtendedStorage::Ptr storage,
                                     CalDavCalendarDatabase *database,
                                     QNetworkAccessManager *networkAccessManager,
                                     Settings *settings,
                                     const QString &calendarServerPath,
                                     QObject *parent)
    : QObject(parent)
    , mNAManager(networkAccessManager)
    , mDatabase(database)
    , mSettings(settings)
    , mCalendar(calendar)
    , mStorage(storage)
    , mNotebook(0)
    , mServerPath(calendarServerPath)
    , mSyncMode(NoSyncMode)
    , mFinished(false)
{
}

NotebookSyncAgent::~NotebookSyncAgent()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    clearRequests();
}

void NotebookSyncAgent::abort()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    clearRequests();
}

void NotebookSyncAgent::clearRequests()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    QList<Request *> requests = mRequests.toList();
    for (int i=0; i<requests.count(); i++) {
        requests[i]->deleteLater();
    }
    mRequests.clear();
}

/*
    Slow sync mode:

    1) Get all calendars on the server using Report::getAllEvents()
    2) Save all received calendar data to disk.

    Step 2) is triggered by CalDavClient once *all* notebook syncs have finished.
 */
void NotebookSyncAgent::startSlowSync(const QString &notebookName,
                                      const QString &notebookAccountId,
                                      const QString &pluginName,
                                      const QString &syncProfile,
                                      const QString &color,
                                      const QDateTime &fromDateTime,
                                      const QDateTime &toDateTime)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    mSyncMode = SlowSync;
    mNotebook = mKCal::Notebook::Ptr(new mKCal::Notebook(notebookName, QString()));
    mNotebook->setAccount(notebookAccountId);
    mNotebook->setPluginName(pluginName);
    mNotebook->setSyncProfile(syncProfile);
    mNotebook->setColor(color);

    if (!mStorage->addNotebook(mNotebook)) {
        emitFinished(Buteo::SyncResults::INTERNAL_ERROR, "unable to add notebook "
                     + mNotebook->uid() + " for account/calendar " + mNotebook->account());
        return;
    }
    LOG_DEBUG("NOTEBOOK created" << mNotebook->uid() << "account:" << mNotebook->account());

    Report *report = new Report(mNAManager, mSettings);
    mRequests.insert(report);
    connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
    report->getAllEvents(mServerPath, fromDateTime, toDateTime);
}

/*
    Quick sync mode:

    1) Get all remote calendar etags and updated calendar data from the server using Report::getAllETags()
    2) Get all local changes since the last sync
    3) Filter out local changes that were actually remote changes written by step 5) of this
       sequence from a previous sync
    4) Send the local changes to the server using Put and Delete requests
    5) Write the remote calendar changes to disk.

    Step 5) is triggered by CalDavClient once *all* notebook syncs have finished.
 */
void NotebookSyncAgent::startQuickSync(mKCal::Notebook::Ptr notebook,
                                       const QDateTime &changesSinceDate,
                                       const KCalCore::Incidence::List &allCalendarIncidences,
                                       const QDateTime &fromDateTime,
                                       const QDateTime &toDateTime)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;
    mSyncMode = QuickSync;
    mNotebook = notebook;
    mCalendarIncidencesBeforeSync = allCalendarIncidences;
    mChangesSinceDate = changesSinceDate;

    fetchRemoteChanges(fromDateTime, toDateTime);
}

void NotebookSyncAgent::finalize()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    LOG_DEBUG("Writing" << mNewRemoteIncidenceIds.count() << "insertions,"
              << mModifiedIncidenceICalData.count() << "modifications"
              << mIncidenceUidsToDelete.count() << "deletions");

    mDatabase->removeEntries(mNotebook->uid());

    if (mNewRemoteIncidenceIds.count()) {
        mDatabase->insertAdditions(mNotebook->uid(), mNewRemoteIncidenceIds);
        mNewRemoteIncidenceIds.clear();
    }
    if (mModifiedIncidenceICalData.count()) {
        mDatabase->insertModifications(mNotebook->uid(), mModifiedIncidenceICalData);
        mModifiedIncidenceICalData.clear();
    }
    if (mIncidenceUidsToDelete.count()) {
        mDatabase->insertDeletions(mNotebook->uid(), mIncidenceUidsToDelete);
        mIncidenceUidsToDelete.clear();
    }
}

bool NotebookSyncAgent::isFinished() const
{
    return mFinished;
}

void NotebookSyncAgent::fetchRemoteChanges(const QDateTime &fromDateTime, const QDateTime &toDateTime)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // Incidences must be loaded with ExtendedStorage::allIncidences() rather than
    // ExtendedCalendar::incidences(), because the latter will load incidences from all
    // notebooks, rather than just the one for this report.
    // Note that storage incidence references cannot be used with ExtendedCalendar::deleteEvent()
    // etc. Those methods only work for references created by ExtendedCalendar.
    KCalCore::Incidence::List storageIncidenceList;
    if (!mStorage->allIncidences(&storageIncidenceList, mNotebook->uid())) {
        emitFinished(Buteo::SyncResults::DATABASE_FAILURE, QString("Unable to load storage incidences for notebook: %1").arg(mNotebook->uid()));
        return;
    }

    Report *report = new Report(mNAManager, mSettings);
    mRequests.insert(report);
    connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
    report->getAllETags(mServerPath, storageIncidenceList, fromDateTime, toDateTime);
}

void NotebookSyncAgent::sendLocalChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    KCalCore::Incidence::List inserted;
    KCalCore::Incidence::List modified;
    KCalCore::Incidence::List deleted;
    if (!loadLocalChanges(mChangesSinceDate, &inserted, &modified, &deleted)) {
        emitFinished(Buteo::SyncResults::INTERNAL_ERROR, "Unable to load changes for calendar: " + mServerPath);
        return;
    }
    if (inserted.isEmpty() && modified.isEmpty() && deleted.isEmpty()) {
        LOG_DEBUG("No changes to send!");
        emitFinished(Buteo::SyncResults::NO_ERROR, "Done, no local changes for " + mServerPath);
        return;
    }
    LOG_DEBUG("Total changes for" << mServerPath << ":"
              << "inserted = " << inserted.count()
              << "modified = " << modified.count()
              << "deleted = " << deleted.count());

    for (int i=0; i<inserted.count(); i++) {
        Put *put = new Put(mNAManager, mSettings);
        mRequests.insert(put);
        connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
        put->createEvent(mServerPath, inserted[i]);
    }
    for (int i=0; i<modified.count(); i++) {
        Put *put = new Put(mNAManager, mSettings);
        mRequests.insert(put);
        connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
        put->updateEvent(mServerPath, modified[i]);
    }
    for (int i=0; i<deleted.count(); i++) {
        Delete *del = new Delete(mNAManager, mSettings);
        mRequests.insert(del);
        connect(del, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
        del->deleteEvent(mServerPath, deleted[i]);
    }
}

bool NotebookSyncAgent::loadLocalChanges(const QDateTime &fromDate,
                                         KCalCore::Incidence::List *inserted,
                                         KCalCore::Incidence::List *modified,
                                         KCalCore::Incidence::List *deleted)
{
    FUNCTION_CALL_TRACE;

    if (!mStorage) {
        LOG_CRITICAL("mStorage not set");
        return false;
    }
    QString notebookUid = mNotebook->uid();
    KDateTime kFromDate(fromDate);
    if (!mStorage->insertedIncidences(inserted, kFromDate, notebookUid)) {
        LOG_CRITICAL("mKCal::ExtendedStorage::insertedIncidences() failed");
        return false;
    }
    if (!mStorage->modifiedIncidences(modified, kFromDate, notebookUid)) {
        LOG_CRITICAL("mKCal::ExtendedStorage::modifiedIncidences() failed");
        return false;
    }
    if (!mStorage->deletedIncidences(deleted, kFromDate, notebookUid)) {
        LOG_CRITICAL("mKCal::ExtendedStorage::deletedIncidences() failed");
        return false;
    }
    LOG_DEBUG("Initially found changes for" << mServerPath << "since" << fromDate << ":"
              << "inserted = " << inserted->count()
              << "modified = " << modified->count()
              << "deleted = " << deleted->count());

    // Any server changes synced to the local db during the last sync will be picked up as
    // "local changes", so we must discard these changes so that they are not sent back to
    // the server on this next sync.
    if (!discardRemoteChanges(inserted, modified, deleted)) {
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

bool NotebookSyncAgent::discardRemoteChanges(KCalCore::Incidence::List *localInserted,
                                             KCalCore::Incidence::List *localModified,
                                             KCalCore::Incidence::List *localDeleted)
{
    // Go through the local inserted, modified and deletions list and:
    // - Discard from them respectively the additions, modifications and deletions that were
    //   created as a result of the last remote sync.
    // - Discard any incidences that have already been deleted on the server. (These will be
    //   deleted locally when the current sync finishes.)
    // - Discard any local modifications that were modified on the server, as the server
    //   modifications take precedence.

    if (!mNotebook) {
        LOG_CRITICAL("no notebook");
        return false;
    }
    bool ok = false;
    QSet<QString> remoteDeletedIncidences = QSet<QString>::fromList(mIncidenceUidsToDelete);

    QStringList additions = mDatabase->additions(mNotebook->uid(), &ok);
    if (!ok) {
        LOG_CRITICAL("Unable to look up last sync additions for notebook:" << mNotebook->uid());
        return false;
    }
    QHash<QString,QString> modifications = mDatabase->modifications(mNotebook->uid(), &ok);
    if (!ok) {
        LOG_CRITICAL("Unable to look up last sync modifications for notebook:" << mNotebook->uid());
        return false;
    }

    for (KCalCore::Incidence::List::iterator it = localInserted->begin(); it != localInserted->end();) {
        const KCalCore::Incidence::Ptr &incidence = *it;
        const QString &uid = incidence->uid();
        if (remoteDeletedIncidences.contains(uid)) {
            LOG_DEBUG("Discarding addition deleted on server:" << uid);
            it = localInserted->erase(it);
        } else if (additions.indexOf(uid) >= 0) {
            if (incidence->lastModified().isValid() && incidence->lastModified() > incidence->created()) {
                // This incidence has been modified since it was added from the server in the last sync,
                // so it's a modification rather than an addition.
                LOG_DEBUG("Moving to modified:" << uid);
                KCalCore::Incidence::Ptr savedIncidence = fetchIncidence(mCalendar, uid);
                if (savedIncidence) {
                    localModified->append(savedIncidence);
                    it = localInserted->erase(it);
                } else {
                    ++it;
                }
            } else {
                LOG_DEBUG("Discarding addition from previous sync:" << uid);
                it = localInserted->erase(it);
            }
        } else {
            ++it;
        }
    }

    QSet<QString> serverModifiedUids;
    for (int i=0; i<mReceivedCalendarResources.count(); i++) {
        serverModifiedUids.insert(Reader::hrefToUid(mReceivedCalendarResources[i].href));
    }
    for (KCalCore::Incidence::List::iterator it = localModified->begin(); it != localModified->end();) {
        KCalCore::Incidence::Ptr sourceIncidence = *it;
        const QString &uid = sourceIncidence->uid();
        if (remoteDeletedIncidences.contains(uid) || serverModifiedUids.contains(uid)) {
            it = localModified->erase(it);
            continue;
        } else if (modifications.contains(sourceIncidence->uid())) {
            KCalCore::ICalFormat iCalFormat;
            KCalCore::Incidence::Ptr receivedIncidence = iCalFormat.fromString(modifications[sourceIncidence->uid()]);
            if (receivedIncidence.isNull()) {
                LOG_WARNING("Not sending modification, cannot parse the received incidence:" << modifications[sourceIncidence->uid()]);
                it = localModified->erase(it);
                continue;
            }
            // If incidences are the same, then we assume the local incidence was not changed after
            // the remote incidence was received, and thus there are no modifications to report.
            if (IncidenceHandler::copiedPropertiesAreEqual(sourceIncidence, receivedIncidence)) {
                LOG_DEBUG("Discarding modification" << (*it)->uid());
                it = localModified->erase(it);
                continue;
            }
        }

        // The default storage implementation applies the organizer as an attendee by default. Don't do this
        // as it turns the incidence into a scheduled event requiring acceptance/rejection/etc.
        const KCalCore::Person::Ptr organizer = sourceIncidence->organizer();
        if (organizer) {
            Q_FOREACH (const KCalCore::Attendee::Ptr &attendee, sourceIncidence->attendees()) {
                if (attendee->email() == organizer->email() && attendee->fullName() == organizer->fullName()) {
                    LOG_DEBUG("Discarding organizer as attendee" << attendee->fullName());
                    sourceIncidence->deleteAttendee(attendee);
                    break;
                }
            }
        }
        ++it;
    }

    QStringList deletions = mDatabase->deletions(mNotebook->uid(), &ok);
    if (!ok) {
        LOG_CRITICAL("Unable to look up last sync deletions for notebook:" << mNotebook->uid());
        return false;
    }
    for (KCalCore::Incidence::List::iterator it = localDeleted->begin(); it != localDeleted->end();) {
        const QString &uid = (*it)->uid();
        if (remoteDeletedIncidences.contains(uid) || deletions.indexOf(uid) >= 0) {
            LOG_DEBUG("Discarding deletion" << uid);
            it = localDeleted->erase(it);
        } else {
            ++it;
        }
    }

    return true;
}

int NotebookSyncAgent::removeCommonIncidences(KCalCore::Incidence::List *firstList, KCalCore::Incidence::List *secondList)
{
    QSet<QString> firstListUids;
    for (int i=0; i<firstList->count(); i++) {
        firstListUids.insert(firstList->at(i)->uid());
    }
    QSet<QString> commonUids;
    for (KCalCore::Incidence::List::iterator it = secondList->begin(); it != secondList->end();) {
        const KCalCore::Incidence::Ptr &incidence = *it;
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
            const KCalCore::Incidence::Ptr &incidence = *it;
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

void NotebookSyncAgent::reportRequestFinished()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = qobject_cast<Report*>(sender());
    mRequests.remove(report);
    report->deleteLater();

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        mReceivedCalendarResources = report->receivedCalendarResources();
        LOG_DEBUG("Received" << mReceivedCalendarResources.count() << "calendar resources");
        if (mSyncMode == QuickSync) {
            mIncidenceUidsToDelete = report->localIncidenceUidsNotOnServer();
            sendLocalChanges();
            return;
        }
    }
    emitFinished(report->errorCode(), report->errorString());
}

void NotebookSyncAgent::nonReportRequestFinished()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Request *request = qobject_cast<Request*>(sender());
    if (!request) {
        emitFinished(Buteo::SyncResults::INTERNAL_ERROR, QStringLiteral("Invalid request object"));
        return;
    }
    mRequests.remove(request);
    request->deleteLater();

    if (request->errorCode() != Buteo::SyncResults::NO_ERROR) {
        LOG_CRITICAL("Aborting sync," << request->command() << "failed" << request->errorString() << "for notebook:" << mNotebook->account());
        emitFinished(Buteo::SyncResults::INTERNAL_ERROR, request->errorString());
    } else if (mRequests.isEmpty()) {
        emitFinished(Buteo::SyncResults::NO_ERROR, QStringLiteral("Finished requests for %1").arg(mNotebook->account()));
    }
}

void NotebookSyncAgent::emitFinished(int minorErrorCode, const QString &message)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (mFinished) {
        return;
    }
    mFinished = true;
    clearRequests();

    emit finished(minorErrorCode, message);
}

bool NotebookSyncAgent::applyRemoteChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (!updateIncidences(mReceivedCalendarResources)) {
        return false;
    }
    if (!deleteIncidences(mIncidenceUidsToDelete)) {
        return false;
    }
    return true;
}

bool NotebookSyncAgent::updateIncidences(const QList<Reader::CalendarResource> &resources)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    for (int i=0; i<resources.count(); i++) {
        const Reader::CalendarResource &resource = resources.at(i);
        KCalCore::ICalFormat iCalFormat;
        KCalCore::Incidence::Ptr newIncidence = iCalFormat.fromString(resource.iCalData);
        if (newIncidence.isNull()) {
            continue;
        }
        KCalCore::Incidence::Ptr storedIncidence;
        switch (newIncidence->type()) {
        case KCalCore::IncidenceBase::TypeEvent:
            storedIncidence = mCalendar->event(newIncidence->uid());
            break;
        case KCalCore::IncidenceBase::TypeTodo:
            storedIncidence = mCalendar->todo(newIncidence->uid());
            break;
        case KCalCore::IncidenceBase::TypeJournal:
            storedIncidence = mCalendar->journal(newIncidence->uid());
            break;
        case KCalCore::IncidenceBase::TypeFreeBusy:
        case KCalCore::IncidenceBase::TypeUnknown:
            qWarning() << "Unsupported incidence type:" << newIncidence->type();
            continue;
        }
        if (storedIncidence) {
            LOG_DEBUG("Updating existing event:" << newIncidence->uid());
            storedIncidence->startUpdates();
            IncidenceHandler::copyIncidenceProperties(storedIncidence, newIncidence);
            storedIncidence->setCustomProperty("buteo", "uri", resource.href);
            storedIncidence->setCustomProperty("buteo", "etag", resource.etag);
            storedIncidence->endUpdates();

            // Save the modified incidence so it can be used to check whether there were
            // local changes on the next sync.
            mModifiedIncidenceICalData.insert(newIncidence->uid(), resource.iCalData);
        } else {
            LOG_DEBUG("Saving new event:" << newIncidence->uid() << resource.href << resource.etag);
            newIncidence->setCustomProperty("buteo", "uri", resource.href);
            newIncidence->setCustomProperty("buteo", "etag", resource.etag);
            IncidenceHandler::prepareIncidenceProperties(newIncidence);

            bool added = false;
            switch (newIncidence->type()) {
            case KCalCore::IncidenceBase::TypeEvent:
                added = mCalendar->addEvent(newIncidence.staticCast<KCalCore::Event>(), mNotebook->uid());
                break;
            case KCalCore::IncidenceBase::TypeTodo:
                added = mCalendar->addTodo(newIncidence.staticCast<KCalCore::Todo>(), mNotebook->uid());
                break;
            case KCalCore::IncidenceBase::TypeJournal:
                added = mCalendar->addJournal(newIncidence.staticCast<KCalCore::Journal>(), mNotebook->uid());
                break;
            case KCalCore::IncidenceBase::TypeFreeBusy:
            case KCalCore::IncidenceBase::TypeUnknown:
                LOG_WARNING("Unsupported incidence type:" << newIncidence->type());
                continue;
            }
            if (added) {
                // Save the new incidence so it can be discarded from the list of local changes
                // on the next sync when local changes are sent to the server.
                mNewRemoteIncidenceIds << newIncidence->uid();
                LOG_DEBUG("Added new incidence:" << newIncidence->uid());
            } else {
                LOG_CRITICAL("Unable to add incidence" << newIncidence->uid() << "to notebook" << mNotebook->uid());
                return false;
            }
        }
    }
    return true;
}

bool NotebookSyncAgent::deleteIncidences(const QStringList &incidenceUids)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (incidenceUids.isEmpty() || mCalendarIncidencesBeforeSync.isEmpty()) {
        return true;
    }
    QHash<QString, KCalCore::Incidence::Ptr> calendarIncidencesMap;
    for (int i=0; i<mCalendarIncidencesBeforeSync.count(); i++) {
        calendarIncidencesMap.insert(mCalendarIncidencesBeforeSync[i]->uid(), mCalendarIncidencesBeforeSync[i]);
    }
    Q_FOREACH (const QString &incidenceUid, incidenceUids) {
        if (calendarIncidencesMap.contains(incidenceUid)) {
            KCalCore::Incidence::Ptr calendarIncidence = calendarIncidencesMap.take(incidenceUid);
            switch (calendarIncidence->type()) {
            case KCalCore::IncidenceBase::TypeEvent:
                if (!mCalendar->deleteEvent(calendarIncidence.staticCast<KCalCore::Event>())) {
                    LOG_CRITICAL("Unable to delete Event = " << calendarIncidence->customProperty("buteo", "uri"));
                    return false;
                }
                LOG_DEBUG("Deleted Event = " << calendarIncidence->customProperty("buteo", "uri"));
                break;
            case KCalCore::IncidenceBase::TypeTodo:
                if (!mCalendar->deleteTodo(calendarIncidence.staticCast<KCalCore::Todo>())) {
                    LOG_CRITICAL("Unable to delete Todo = " << calendarIncidence->customProperty("buteo", "uri"));
                    return false;
                }
                LOG_DEBUG("Deleted Todo = " << calendarIncidence->customProperty("buteo", "uri"));
                break;
            case KCalCore::IncidenceBase::TypeJournal:
                if (!mCalendar->deleteJournal(calendarIncidence.staticCast<KCalCore::Journal>())) {
                    LOG_CRITICAL("Unable to delete Journal = " << calendarIncidence->customProperty("buteo", "uri"));
                    return false;
                }
                LOG_DEBUG("Deleted Journal = " << calendarIncidence->customProperty("buteo", "uri"));
                break;
            default:
                break;
            }
        }
    }
    return true;
}
