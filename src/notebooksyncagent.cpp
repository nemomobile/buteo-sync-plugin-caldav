/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2014 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Bea Lam <bea.lam@jollamobile.com>
 *               Stephan Rave <mail@stephanrave.de>
 *               Chris Adams <chris.adams@jollamobile.com>
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

#include <memorycalendar.h>
#include <incidence.h>
#include <icalformat.h>
#include <event.h>
#include <todo.h>
#include <journal.h>
#include <attendee.h>

#include <QDebug>


#define NOTEBOOK_FUNCTION_CALL_TRACE FUNCTION_CALL_TRACE(QString("%1 %2").arg(Q_FUNC_INFO).arg(mNotebook ? mNotebook->account() : ""))

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
    , mRetriedReport(false)
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
void NotebookSyncAgent::startSlowSync(const QString &calendarPath,
                                      const QString &notebookName,
                                      const QString &notebookAccountId,
                                      const QString &pluginName,
                                      const QString &syncProfile,
                                      const QString &color,
                                      const QDateTime &fromDateTime,
                                      const QDateTime &toDateTime,
                                      const QString &notebookUidToDelete)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    LOG_DEBUG("Start slow sync for notebook:" << notebookName << "for account" << notebookAccountId
              << "between" << fromDateTime << "to" << toDateTime);

    mSyncMode = SlowSync;
    mCalendarPath = calendarPath;
    mNotebookName = notebookName;
    mNotebookAccountId = notebookAccountId;
    mPluginName = pluginName;
    mSyncProfile = syncProfile;
    mColor = color;
    mFromDateTime = fromDateTime;
    mToDateTime = toDateTime;
    mNotebookUidToDelete = notebookUidToDelete;

    sendReportRequest();
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

    LOG_DEBUG("Start quick sync for notebook:" << notebook->uid()
              << "between" << fromDateTime << "to" << toDateTime
              << ", sync changes since" << changesSinceDate);

    mSyncMode = QuickSync;
    mNotebook = notebook;
    mCalendarIncidencesBeforeSync = allCalendarIncidences;
    mChangesSinceDate = changesSinceDate;
    mFromDateTime = fromDateTime;
    mToDateTime = toDateTime;

    bool ok = false;
    mLocalETags = mDatabase->eTags(mNotebook->uid(), &ok);
    if (!ok) {
        emitFinished(Buteo::SyncResults::DATABASE_FAILURE, QString("Unable to load etags for notebook: %1").arg(mNotebook->uid()));
        return;
    }

    // Incidences must be loaded with ExtendedStorage::allIncidences() rather than
    // ExtendedCalendar::incidences(), because the latter will load incidences from all
    // notebooks, rather than just the one for this report.
    // Note that storage incidence references cannot be used with ExtendedCalendar::deleteEvent()
    // etc. Those methods only work for references created by ExtendedCalendar.
    if (!mStorage->allIncidences(&mStorageIncidenceList, mNotebook->uid())) {
        emitFinished(Buteo::SyncResults::DATABASE_FAILURE, QString("Unable to load storage incidences for notebook: %1").arg(mNotebook->uid()));
        return;
    }

    mStorageIds.clear();
    Q_FOREACH(const KCalCore::Incidence::Ptr &incidence, mStorageIncidenceList) {
        mStorageIds.insert(KCalId(incidence));
    }

    LOG_DEBUG("Loaded" << mStorageIds.size() << "incidences from storage for notebook:" << mNotebook->uid());
    sendReportRequest();
}

void NotebookSyncAgent::sendReportRequest()
{
    if (mSyncMode == SlowSync) {
        Report *report = new Report(mNAManager, mSettings);
        mRequests.insert(report);
        connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
        report->getAllEvents(mServerPath, mFromDateTime, mToDateTime);
    } else {
        fetchRemoteChanges(mFromDateTime, mToDateTime);
    }
}

void NotebookSyncAgent::finalize()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    LOG_DEBUG("Writing" << mNewRemoteIncidenceIds.count() << "insertions,"
              << mModifiedIncidenceICalData.count() << "modifications,"
              << mIncidenceIdsToDelete.count() << "deletions,"
              << mUpdatedETags.count() << "updated etags");

    // remove additions, modifications and deletions from the last sync session
    // (don't call removeEntries() as that clears etags and calendars also)
    mDatabase->removeIncidenceChangeEntriesOnly(mNotebook->uid());

    mDatabase->setNeedsCleanSync(mNotebook->uid(), false); // TODO: on error, set to true?
    if (mNewRemoteIncidenceIds.count()) {
        mDatabase->insertAdditions(mNotebook->uid(), mNewRemoteIncidenceIds);
        mNewRemoteIncidenceIds.clear();
    }
    if (mModifiedIncidenceICalData.count()) {
        mDatabase->insertModifications(mNotebook->uid(), mModifiedIncidenceICalData);
        mModifiedIncidenceICalData.clear();
    }
    if (mIncidenceIdsToDelete.count()) {
        mDatabase->insertDeletions(mNotebook->uid(), mIncidenceIdsToDelete);
        mIncidenceIdsToDelete.clear();
    }
    if (mUpdatedETags.count()) {
        mDatabase->insertETags(mNotebook->uid(), mUpdatedETags);
        mUpdatedETags.clear();
    }
}

bool NotebookSyncAgent::isFinished() const
{
    return mFinished;
}

void NotebookSyncAgent::fetchRemoteChanges(const QDateTime &fromDateTime, const QDateTime &toDateTime)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = new Report(mNAManager, mSettings);
    mRequests.insert(report);
    connect(report, SIGNAL(finished()), this, SLOT(processETags()));
    report->getAllETags(mServerPath, fromDateTime, toDateTime);
}

// A given incidence has been added or modified locally.
// To upsync the change, we need to construct the .ics data to upload to server.
// Since the incidence may be an occurrence or recurring series incidence,
// we cannot simply convert the incidence to iCal data, but instead we have to
// upsync an .ics containing the whole recurring series.
QString NotebookSyncAgent::constructLocalChangeIcs(KCalCore::Incidence::Ptr updatedIncidence)
{
    // create an in-memory calendar
    // add to it the required incidences (ie, check if has recurrenceId -> load parent and all instances; etc)
    // for each of those, we need to do the IncidenceToExport() modifications first
    // then, export from that calendar to .ics file.
    KCalCore::MemoryCalendar::Ptr memoryCalendar(new KCalCore::MemoryCalendar(KDateTime::UTC));
    if (updatedIncidence->hasRecurrenceId() || updatedIncidence->recurs()) {
        KCalCore::Incidence::Ptr recurringIncidence = updatedIncidence->hasRecurrenceId()
                                                ? mCalendar->incidence(updatedIncidence->uid(), KDateTime())
                                                : updatedIncidence;
        KCalCore::Incidence::List instances = mCalendar->instances(recurringIncidence);
        KCalCore::Incidence::Ptr exportableIncidence = IncidenceHandler::incidenceToExport(recurringIncidence);

        // remove EXDATE values from the recurring incidence which correspond to the persistent occurrences (instances)
        Q_FOREACH (KCalCore::Incidence::Ptr instance, instances) {
            QList<KDateTime> exDateTimes = exportableIncidence->recurrence()->exDateTimes();
            exDateTimes.removeAll(instance->recurrenceId());
            exportableIncidence->recurrence()->setExDateTimes(exDateTimes);
        }

        // store the base recurring event into the in-memory calendar
        memoryCalendar->addIncidence(exportableIncidence);

        // now create the persistent occurrences in the in-memory calendar
        Q_FOREACH (KCalCore::Incidence::Ptr instance, instances) {
            // We cannot call dissociateSingleOccurrence() on the MemoryCalendar
            // as that's an mKCal specific function.
            // We cannot call dissociateOccurrence() because that function
            // takes only a QDate instead of a KDateTime recurrenceId.
            // Thus, we need to manually create an exception occurrence.
            KCalCore::Incidence::Ptr exportableOccurrence(exportableIncidence->clone());
            exportableOccurrence->setCreated(instance->created());
            exportableOccurrence->setRevision(instance->revision());
            exportableOccurrence->clearRecurrence();
            exportableOccurrence->setRecurrenceId(instance->recurrenceId());
            exportableOccurrence->setDtStart(instance->recurrenceId());

            // add it, and then update it in-memory.
            memoryCalendar->addIncidence(exportableOccurrence);
            exportableOccurrence = memoryCalendar->incidence(instance->uid(), instance->recurrenceId());
            exportableOccurrence->startUpdates();
            IncidenceHandler::copyIncidenceProperties(exportableOccurrence, IncidenceHandler::incidenceToExport(instance));
            exportableOccurrence->endUpdates();
        }
    } else {
        KCalCore::Incidence::Ptr exportableIncidence = IncidenceHandler::incidenceToExport(updatedIncidence);
        memoryCalendar->addIncidence(exportableIncidence);
    }

    KCalCore::ICalFormat icalFormat;
    return icalFormat.toString(memoryCalendar, QString(), false);
}

void NotebookSyncAgent::sendLocalChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    QSet<KCalId> deleted;
    mLocallyInsertedIncidences.clear();
    mLocallyModifiedIncidences.clear();
    if (!loadLocalChanges(mChangesSinceDate, &mLocallyInsertedIncidences, &mLocallyModifiedIncidences, &deleted)) {
        emitFinished(Buteo::SyncResults::INTERNAL_ERROR, "Unable to load changes for calendar: " + mServerPath);
        return;
    }
    if (mLocallyInsertedIncidences.isEmpty() && mLocallyModifiedIncidences.isEmpty() && deleted.isEmpty()) {
        LOG_DEBUG("No changes to send!");
        emitFinished(Buteo::SyncResults::NO_ERROR, "Done, no local changes for " + mServerPath);
        return;
    }
    LOG_DEBUG("Total local changes for" << mServerPath << ":"
              << "inserted = " << mLocallyInsertedIncidences.count()
              << ", modified = " << mLocallyModifiedIncidences.count()
              << ", deleted = " << deleted.count());

    QSet<QString> addModUids;
    for (int i=0; i<mLocallyInsertedIncidences.count(); i++) {
        if (addModUids.contains(mLocallyInsertedIncidences[i]->uid())) {
            continue; // already handled this one, as a result of a previous update of another occurrence in the series.
        } else {
            addModUids.insert(mLocallyInsertedIncidences[i]->uid());
        }
        Put *put = new Put(mNAManager, mSettings);
        mRequests.insert(put);
        connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
        put->createEvent(mServerPath,
                         constructLocalChangeIcs(mLocallyInsertedIncidences[i]),
                         KCalId(mLocallyInsertedIncidences[i]));
    }
    for (int i=0; i<mLocallyModifiedIncidences.count(); i++) {
        if (addModUids.contains(mLocallyModifiedIncidences[i]->uid())) {
            continue; // already handled this one, as a result of a previous update of another occurrence in the series.
        }
        // first, handle updates of exceptions by uploading the entire modified series.
        if (mLocallyModifiedIncidences[i]->hasRecurrenceId()) {
            Put *put = new Put(mNAManager, mSettings);
            mRequests.insert(put);
            connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
            addModUids.insert(mLocallyModifiedIncidences[i]->uid());
            put->updateEvent(mServerPath,
                             constructLocalChangeIcs(mLocallyModifiedIncidences[i]),
                             mLocalETags.value(mLocallyModifiedIncidences[i]->customProperty("buteo", "uri")),
                             mLocallyModifiedIncidences[i]->customProperty("buteo", "uri"),
                             KCalId(mLocallyModifiedIncidences[i]));
        }
    }
    for (int i=0; i<mLocallyModifiedIncidences.count(); i++) {
        if (addModUids.contains(mLocallyModifiedIncidences[i]->uid())) {
            continue; // already handled this one, as a result of a previous update of another occurrence in the series.
        }
        // now handle updates of base incidences (which haven't otherwise already been upsynced), via direct update.
        KCalCore::ICalFormat icalFormat;
        Put *put = new Put(mNAManager, mSettings);
        mRequests.insert(put);
        connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
        put->updateEvent(mServerPath,
                         icalFormat.toICalString(IncidenceHandler::incidenceToExport(mLocallyModifiedIncidences[i])),
                         mLocalETags.value(mLocallyModifiedIncidences[i]->customProperty("buteo", "uri")),
                         mLocallyModifiedIncidences[i]->customProperty("buteo", "uri"),
                         KCalId(mLocallyModifiedIncidences[i]));
    }

    // For deletions, if a persistent exception is deleted we may need to do a PUT
    // containing all of the still-existing events in the series.
    // (Alternative is to push a STATUS:CANCELLED event?)
    // Hence, we first need to find out if any deletion is a lone-persistent-exception deletion.
    QMultiHash<QString, KDateTime> uidToRecurrenceIdDeletions;
    Q_FOREACH (const KCalId &kcalid, deleted) {
        uidToRecurrenceIdDeletions.insert(kcalid.uid, kcalid.recurrenceId);
    }

    // now send DELETEs as required, and PUTs as required.
    Q_FOREACH (const QString &uid, uidToRecurrenceIdDeletions.keys()) {
        QList<KDateTime> recurrenceIds = uidToRecurrenceIdDeletions.values(uid);
        if (!recurrenceIds.contains(KDateTime())) {
            // one or more persistent exceptions are being deleted; must PUT.
            if (addModUids.contains(uid)) {
                LOG_DEBUG("Already handled this exception deletion in another exception update");
                continue;
            }
            Put *put = new Put(mNAManager, mSettings);
            mRequests.insert(put);
            connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
            KCalCore::Incidence::Ptr recurringSeries = mCalendar->incidence(uid, KDateTime());
            if (!recurringSeries.isNull()) {
                put->updateEvent(mServerPath,
                                 constructLocalChangeIcs(recurringSeries),
                                 mLocalETags[recurringSeries->customProperty("buteo", "uri")],
                                 recurringSeries->customProperty("buteo", "uri"),
                                 KCalId(recurringSeries));
                continue; // finished with this deletion.
            } else {
                LOG_WARNING("Unable to load recurring incidence for deleted exception; deleting entire series instead");
                // fall through to the DELETE code below.
            }
        }

        // the whole series is being deleted; can DELETE.
        Delete *del = new Delete(mNAManager, mSettings);
        mRequests.insert(del);
        connect(del, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));

        // We have to determine the correct href of the deleted incidence. Unfortunately,
        // the storage backend deletes all custom properties of deleted incidences, so
        // we cannot use customProperty("buteo", "uri").
        // Fortunately, the deleted incidence can be found in mReceivedCalendarResources.
        QString href;
        Q_FOREACH(const Reader::CalendarResource &resource, mReceivedCalendarResources) {
            if (!resource.incidences.isEmpty() && (uid == resource.incidences.first()->uid())) {
                href = resource.href;
            }
        }
        if (href.isNull()) {
            emitFinished(Buteo::SyncResults::INTERNAL_ERROR,
                         "Unable to determine href for locally deleted incidence:" + uid);
        }
        del->deleteEvent(href);
    }
}

bool NotebookSyncAgent::loadLocalChanges(const QDateTime &fromDate,
                                         KCalCore::Incidence::List *inserted,
                                         KCalCore::Incidence::List *modified,
                                         QSet<KCalId> *deleted)
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

    KCalCore::Incidence::List deletedIncidences;
    if (!mStorage->deletedIncidences(&deletedIncidences, kFromDate, notebookUid)) {
        LOG_CRITICAL("mKCal::ExtendedStorage::deletedIncidences() failed");
        return false;
    }
    Q_FOREACH(const KCalCore::Incidence::Ptr &incidence, deletedIncidences) {
        deleted->insert(KCalId(incidence));
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

// discard local changes which are obsoleted by remote changes, or caused by previous remote changes.
bool NotebookSyncAgent::discardRemoteChanges(KCalCore::Incidence::List *localInserted,
                                             KCalCore::Incidence::List *localModified,
                                             QSet<KCalId> *localDeleted)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

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
    QSet<KCalId> remoteDeletedIncidences(mIncidenceIdsToDelete);

    QSet<KCalId> additions = mDatabase->additions(mNotebook->uid(), &ok);
    if (!ok) {
        LOG_CRITICAL("Unable to look up last sync additions for notebook:" << mNotebook->uid());
        return false;
    }
    Q_FOREACH (const KCalId &kcalid, additions) {
        LOG_TRACE("Tracking previous addition:" << kcalid.uid << kcalid.recurrenceId.toString());
    }

    QHash<KCalId,QString> modifications = mDatabase->modifications(mNotebook->uid(), &ok);
    if (!ok) {
        LOG_CRITICAL("Unable to look up last sync modifications for notebook:" << mNotebook->uid());
        return false;
    }
    Q_FOREACH (const KCalId &kcalid, modifications.keys()) {
        LOG_TRACE("Tracking previous modification:" << kcalid.uid << kcalid.recurrenceId.toString() << "==>" << modifications.value(kcalid));
    }

    for (KCalCore::Incidence::List::iterator it = localInserted->begin(); it != localInserted->end();) {
        const KCalCore::Incidence::Ptr &incidence = *it;
        const KCalId &kcalid = KCalId(incidence);
        if (remoteDeletedIncidences.contains(kcalid)) {
            LOG_DEBUG("Discarding addition deleted on server:" << kcalid.toString());
            it = localInserted->erase(it);
        } else if (additions.contains(kcalid)) {
            if (incidence->lastModified().isValid() && incidence->lastModified() > incidence->created()) {
                // This incidence has been modified since it was added from the server in the last sync,
                // so it's a modification rather than an addition.
                LOG_DEBUG("Moving to modified:" << kcalid.toString());
                KCalCore::Incidence::Ptr savedIncidence = mCalendar->incidence(kcalid.uid, kcalid.recurrenceId);
                if (savedIncidence) {
                    localModified->append(savedIncidence);
                    it = localInserted->erase(it);
                } else {
                    ++it;
                }
            } else {
                LOG_DEBUG("Discarding addition from previous sync:" << kcalid.toString());
                it = localInserted->erase(it);
            }
        } else if (modifications.contains(kcalid)) {
            // Sometimes recurring event exception modifications are treated as additions of
            // both the exception _and_ the recurring event, by loadIncidences().  I don't know why.
            // This check ensures that we also check previous modifications to see if the event
            // is tracked there.
            LOG_DEBUG("Discarding tracked recurring event modification (reported as addition) from previous sync:" << kcalid.toString());
            it = localInserted->erase(it);
        } else {
            LOG_DEBUG("Entirely new addition:" << incidence->uid() << incidence->recurrenceId().toString());
            ++it;
        }
    }

    for (KCalCore::Incidence::List::iterator it = localModified->begin(); it != localModified->end();) {
        KCalCore::Incidence::Ptr sourceIncidence = *it;
        const KCalId &kcalid = KCalId(sourceIncidence);
        if (remoteDeletedIncidences.contains(kcalid) || mRemoteModifiedIds.contains(kcalid)) {
            LOG_DEBUG("Discarding modification,"
                      << (remoteDeletedIncidences.contains(kcalid) ? "was already deleted on server" : "")
                      << (mRemoteModifiedIds.contains(kcalid) ? "was already modified on server": ""));
            it = localModified->erase(it);
            continue;
        } else if (modifications.contains(kcalid)) {
            KCalCore::ICalFormat iCalFormat;
            KCalCore::Incidence::Ptr receivedIncidence = iCalFormat.fromString(modifications[kcalid]);
            if (receivedIncidence.isNull()) {
                LOG_WARNING("Not sending modification, cannot parse the received incidence:" << modifications[kcalid]);
                it = localModified->erase(it);
                continue;
            }
            // If incidences are the same, then we assume the local incidence was not changed after
            // the remote incidence was received, and thus there are no modifications to report.
            IncidenceHandler::prepareImportedIncidence(receivedIncidence);  // ensure fields are updated as per imported incidences
            if (IncidenceHandler::copiedPropertiesAreEqual(sourceIncidence, receivedIncidence)) {
                LOG_DEBUG("Discarding modification" << kcalid.toString());
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


    // Incidences which have been received during the last sync and then were deleted locally
    // will not show up in localDeleted as their creation time is after the start time of
    // the last sync. Therefore, go through the list of additions and add all of them which
    // are missing in the storage to the localDeleted list.
    Q_FOREACH(const KCalId& kcalid, additions) {
        if (!mStorageIds.contains(kcalid) && !localDeleted->contains(kcalid)) {
            LOG_DEBUG("Adding previous addition " << kcalid.toString()
                      << " to local deletions as it has vanished from local storage");
            localDeleted->insert(kcalid);
        }
    }

    QSet<KCalId> deletions = mDatabase->deletions(mNotebook->uid(), &ok);
    if (!ok) {
        LOG_CRITICAL("Unable to look up last sync deletions for notebook:" << mNotebook->uid());
        return false;
    }
    for (QSet<KCalId>::iterator it = localDeleted->begin(); it != localDeleted->end();) {
        const KCalId &kcalid = *it;
        if (deletions.contains(kcalid)) {
            // Ignore locally deleted incidences which have been deleted during the last sync.
            LOG_DEBUG("Discarding deletion from last sync:" << kcalid.toString());
            it = localDeleted->erase(it);
        } else if (!mRemoteUids.contains(kcalid.uid)) {
            // All locally deleted incidence which are still on the server, have been received
            // by fetchRemoteChanges. Conversely, all locally deleted incidences which are not
            // under the seen uids should be ignored as they have already been
            // deleted on the server.
            LOG_DEBUG("Discarding deletion, was already deleted on server:" << kcalid.toString());
            it = localDeleted->erase(it);
        } else {
            // Add the uid to mLocalDeletedUids so that we do not insert the incidence as
            // new incidence later on.
            mLocalDeletedIds.insert(kcalid);
            ++it;
        }
    }

    return true;
}

int NotebookSyncAgent::removeCommonIncidences(KCalCore::Incidence::List *firstList, QSet<KCalId> *secondList)
{
    QSet<KCalId> firstListUids;
    for (int i=0; i<firstList->count(); i++) {
        firstListUids.insert(KCalId(firstList->at(i)));
    }
    QSet<KCalId> commonUids;
    for (QSet<KCalId>::iterator it = secondList->begin(); it != secondList->end();) {
        if (firstListUids.contains(*it)) {
            commonUids.insert(*it);
            it = secondList->erase(it);
        } else {
            ++it;
        }
    }
    int removed = commonUids.count();
    if (removed > 0) {
        for (KCalCore::Incidence::List::iterator it = firstList->begin(); it != firstList->end();) {
            const KCalCore::Incidence::Ptr &incidence = *it;
            if (commonUids.contains(KCalId(incidence))) {
                commonUids.remove(KCalId(incidence));
                it = firstList->erase(it);
            } else {
                ++it;
            }
        }
    }
    return removed;
}

void NotebookSyncAgent::processETags()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = qobject_cast<Report*>(sender());
    mRequests.remove(report);
    report->deleteLater();

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        LOG_DEBUG("Process tags for server path" << mServerPath);
        QMultiHash<QString, Reader::CalendarResource> map = report->receivedCalendarResources();
        QStringList eventIdList;
        if (mStorageIncidenceList.isEmpty()) {
            LOG_DEBUG("No local incidences stored, all received resources must be server-side additions");
        } else {
            QSet<QString> seenUris;
            Q_FOREACH (KCalCore::Incidence::Ptr incidence, mStorageIncidenceList) {
                QString uri = incidence->customProperty("buteo", "uri");
                if (seenUris.contains(uri)) {
                    LOG_TRACE("Skipping incidence:" << incidence->uid() << incidence->recurrenceId().toString() << "- already checked it's ETag");
                    continue;
                }
                if (uri.isEmpty()) {
                    //Newly added to Local DB -- Skip this incidence
                    LOG_TRACE("Skipping newly-added incidence with no uri:" << incidence->uid() << incidence->recurrenceId().toString());
                    continue;
                }
                if (!map.contains(uri)) {
                    // we have an incidence that's not on the remote server, so delete it
                    LOG_DEBUG("Need to delete local-but-not-remote:" << uri);
                    switch (incidence->type()) {
                    case KCalCore::IncidenceBase::TypeEvent:
                    case KCalCore::IncidenceBase::TypeTodo:
                    case KCalCore::IncidenceBase::TypeJournal:
                        mIncidenceIdsToDelete.insert(KCalId(incidence));
                        break;
                    case KCalCore::IncidenceBase::TypeFreeBusy:
                    case KCalCore::IncidenceBase::TypeUnknown:
                        break;
                    }
                    continue;
                } else {
                    QList<Reader::CalendarResource> resources = map.values(uri);
                    bool foundNonMatch = false;
                    QString seenEtag;
                    for (int i = 0; i < resources.size(); ++i) {
                        if (mLocalETags.value(resources[i].href) != resources[i].etag) {
                            // need to update this resource as it has changed server-side.
                            LOG_DEBUG("Found non-matching ETag:" << mLocalETags.value(resources[i].href) << "for:" << uri << "with ETag:" << resources[i].etag);
                            // ensure we only fetch once (eg, if we have recurring + occurrence with same href to fetch).
                            if (!eventIdList.contains(resources[i].href)) {
                                eventIdList.append(resources[i].href);
                            }
                            foundNonMatch = true;
                            break;
                        } else {
                            seenEtag = resources[i].etag;
                        }
                    }
                    if (!foundNonMatch) {
                        // we can remove this one, this is a known etag.
                        LOG_DEBUG("All ETags match for uri:" << uri << ":" << seenEtag);
                        map.remove(uri);
                        seenUris.insert(uri);

                        // prepopulate the mRemoteUids list with this series' uid
                        // as it also exists on the server.
                        mRemoteUids.insert(incidence->uid());
                    }
                }
            }
        }

        // any items remaining in the map are new events which need to be retrieved.
        // ensure that we only fetch each given URI once.
        LOG_DEBUG("Fetching new incidences:" << map.keys());
        Q_FOREACH (const QString &eventId, map.keys()) {
            if (!eventIdList.contains(eventId)) {
                eventIdList.append(eventId);
            }
        }

        // fetch updated and new items full data.
        if (!eventIdList.isEmpty()) {
            // some incidences have changed on the server, so fetch the new details
            Report *report = new Report(mNAManager, mSettings);
            mRequests.insert(report);
            connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
            report->multiGetEvents(mServerPath, eventIdList);
            return;
        } else {
            sendLocalChanges();
            return;
        }
    } else if (report->networkError() == QNetworkReply::AuthenticationRequiredError && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        LOG_WARNING("Retrying REPORT after request failed with QNetworkReply::AuthenticationRequiredError");
        mRetriedReport = true;
        sendReportRequest();
        return;
    }

    // no remote changes to downsync, and no local changes to upsync - we're finished.
    emitFinished(report->errorCode(), report->errorString());
}


void NotebookSyncAgent::reportRequestFinished()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = qobject_cast<Report*>(sender());
    mRequests.remove(report);
    report->deleteLater();

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        mReceivedCalendarResources = report->receivedCalendarResources().values();
        Q_FOREACH (const Reader::CalendarResource & resource, mReceivedCalendarResources) {
            Q_FOREACH (KCalCore::Incidence::Ptr incidence, resource.incidences) {
                mRemoteUids.insert(incidence->uid());
                mRemoteModifiedIds.insert(KCalId(incidence));
                LOG_TRACE("Have received modified remote incidence id:" << KCalId(incidence).toString());
            }
        }

        LOG_DEBUG("Report request finished: received:"
                  << report->receivedCalendarResources().size() << "iCal blobs containing a total of"
                  << report->receivedCalendarResources().values().count() << "incidences");

        if (mSyncMode == QuickSync) {
            sendLocalChanges();
            return;
        }
    } else if (mSyncMode == SlowSync
               && report->networkError() == QNetworkReply::AuthenticationRequiredError
               && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        LOG_WARNING("Retrying REPORT after request failed with QNetworkReply::AuthenticationRequiredError");
        mRetriedReport = true;
        sendReportRequest();
        return;
    }
    emitFinished(report->errorCode(), report->errorString());
}

void NotebookSyncAgent::additionalReportRequestFinished()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = qobject_cast<Report*>(sender());
    mRequests.remove(report);
    report->deleteLater();

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        mReceivedCalendarResources.append(report->receivedCalendarResources().values());
        LOG_DEBUG("Additional report request finished: received:"
                  << report->receivedCalendarResources().size() << "iCal blobs containing a total of"
                  << report->receivedCalendarResources().values().count() << "incidences");
        LOG_DEBUG("Have received" << mReceivedCalendarResources.count() << "incidences in total!");
        emitFinished(Buteo::SyncResults::NO_ERROR, QStringLiteral("Finished requests for %1").arg(mNotebook->account()));
        return;
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

    if (request->errorCode() != Buteo::SyncResults::NO_ERROR) {
        LOG_CRITICAL("Aborting sync," << request->command() << "failed" << request->errorString() << "for notebook" << mCalendarPath << "of account:" << mNotebookAccountId);
        emitFinished(request->errorCode(), request->errorString());
    } else {
        Put *putRequest = qobject_cast<Put*>(request);
        if (putRequest) {
            QHash<QString, QString> updatedETags = putRequest->updatedETags();
            Q_FOREACH (const QString &uri, updatedETags.keys()) {
                mUpdatedETags[uri] = updatedETags[uri];
            }
        }
        if (mRequests.isEmpty()) {
            finalizeSendingLocalChanges();
        }
    }
    request->deleteLater();
}

void NotebookSyncAgent::finalizeSendingLocalChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    QStringList hrefsToReload;

    for (int i=0; i<mLocallyInsertedIncidences.count(); i++) {
        KCalCore::Incidence::Ptr &incidence = mLocallyInsertedIncidences[i];
        QString href = mServerPath + incidence->uid() + ".ics";

        if (!mUpdatedETags.contains(href)) {
            LOG_DEBUG("Did not receive ETag for incidence " << KCalId(incidence).toString() << "- will reload from server");
            if (!hrefsToReload.contains(href)) {
                hrefsToReload.append(href);
            }
        } else {
            // We still need to save the href of this incidence. Otherwise, processETags
            // will not be able to identify the incidence on the server during the next sync.
            // If we set custom properties of an incidence, this will change its modification
            // time. (We also cannot reset the modification time as it will be automatically
            // set by the storage backend when the incidence is saved to the database.
            // (See SqliteStorage::Private::saveIncidences of mkcal.)
            // As a workaround, add the incidence to mReceivedCalendarResources, which
            // will write the changes and add the incidence to the sync modifications
            // database, so the custom property change is not picked up as a local
            // modification during next sync.
            LOG_DEBUG("Adding URI to existing incidence:" << KCalId(incidence).toString());
            incidence->setCustomProperty("buteo", "uri", href);
            Reader::CalendarResource resource;
            resource.href = href;
            resource.incidences = KCalCore::Incidence::List() << incidence;
            KCalCore::ICalFormat icalFormat;
            resource.iCalData = icalFormat.toICalString(IncidenceHandler::incidenceToExport(incidence));
            mReceivedCalendarResources.append(resource);
        }
    }

    for (int i=0; i<mLocallyModifiedIncidences.count(); i++) {
        KCalCore::Incidence::Ptr &incidence = mLocallyModifiedIncidences[i];
        QString href = incidence->customProperty("buteo", "uri");
        if (!mUpdatedETags.contains(href)) {
            LOG_DEBUG("Did not receive ETag for incidence " << KCalId(incidence).toString() << "- will reload from server");
            if (!hrefsToReload.contains(href)) {
                hrefsToReload.append(href);
            }
        }
    }

    if (!hrefsToReload.isEmpty()) {
        Report *report = new Report(mNAManager, mSettings);
        mRequests.insert(report);
        connect(report, SIGNAL(finished()), this, SLOT(additionalReportRequestFinished()));
        report->multiGetEvents(mServerPath, hrefsToReload);
        return;
    } else {
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

    if (mSyncMode == SlowSync) {
        // delete the existing notebook associated with this calendar path, if it exists
        if (!mNotebookUidToDelete.isEmpty()) {
            mStorage->loadNotebookIncidences(mNotebookUidToDelete);
            mKCal::Notebook::Ptr doomedNotebook = mStorage->notebook(mNotebookUidToDelete);
            if (doomedNotebook) {
                LOG_DEBUG("Deleting notebook which was queued for deletion:" << mNotebookUidToDelete);
                if (!mStorage->deleteNotebook(doomedNotebook)) {
                    LOG_DEBUG("Failed to delete notebook" << mNotebookUidToDelete << "which was marked for clean sync");
                    return false;
                }
            } else {
                LOG_DEBUG("Failed to find notebook which was queued for deletion:" << mNotebookUidToDelete);
                return false;
            }
        }

        // and create a new one
        mNotebook = mKCal::Notebook::Ptr(new mKCal::Notebook(mNotebookName, QString()));
        mNotebook->setAccount(mNotebookAccountId);
        mNotebook->setPluginName(mPluginName);
        mNotebook->setSyncProfile(mSyncProfile);
        mNotebook->setColor(mColor);
        if (!mStorage->addNotebook(mNotebook)) {
            LOG_DEBUG("Unable to (re)create notebook" << mNotebookName << "during slow sync for account" << mNotebookAccountId << ":" << mCalendarPath);
            return false;
        }

        mDatabase->addRemoteCalendar(mNotebook->uid(), mCalendarPath);
        LOG_DEBUG("Remote calendar" << mCalendarPath << "mapped to newly created notebook" << mNotebook->uid() << "in OOB db for account" << mNotebook->account());
    }

    if (!updateIncidences(mReceivedCalendarResources)) {
        return false;
    }
    if (!deleteIncidences(mIncidenceIdsToDelete)) {
        return false;
    }
    return true;
}

bool NotebookSyncAgent::updateIncidence(KCalCore::Incidence::Ptr incidence, const Reader::CalendarResource &resource, bool *criticalError)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (incidence.isNull()) {
        return false;
    }
    if (mLocalDeletedIds.contains(KCalId(incidence))) {
        LOG_DEBUG("Ignore incidence already deleted locally:" << resource.href);
        return false;
    }

    // find any existing incidence with this uid
    mStorage->load(incidence->uid());
    KCalCore::Incidence::Ptr storedIncidence;
    switch (incidence->type()) {
    case KCalCore::IncidenceBase::TypeEvent:
        storedIncidence = mCalendar->event(incidence->uid(), incidence->hasRecurrenceId() ? incidence->recurrenceId() : KDateTime());
        break;
    case KCalCore::IncidenceBase::TypeTodo:
        storedIncidence = mCalendar->todo(incidence->uid());
        break;
    case KCalCore::IncidenceBase::TypeJournal:
        storedIncidence = mCalendar->journal(incidence->uid());
        break;
    case KCalCore::IncidenceBase::TypeFreeBusy:
    case KCalCore::IncidenceBase::TypeUnknown:
        qWarning() << "Unsupported incidence type:" << incidence->type();
        return false;
    }
    if (storedIncidence) {
        if (incidence->status() == KCalCore::Incidence::StatusCanceled
                || incidence->customStatus().compare(QStringLiteral("CANCELLED"), Qt::CaseInsensitive) == 0) {
            LOG_DEBUG("Queuing existing event for deletion:" << KCalId(incidence).toString()
                                                             << resource.href
                                                             << resource.etag);
            mIncidenceIdsToDelete.insert(KCalId(incidence));
        } else {
            LOG_DEBUG("Updating existing event:" << KCalId(incidence).toString()
                                                 << resource.href
                                                 << resource.etag);
            storedIncidence->startUpdates();
            IncidenceHandler::prepareImportedIncidence(incidence);
            IncidenceHandler::copyIncidenceProperties(storedIncidence, incidence);

            // if this incidence is a recurring incidence, we should get all persistent occurrences
            // and add them back as EXDATEs.  This is because mkcal expects that dissociated
            // single instances will correspond to an EXDATE, but most sync servers do not (and
            // so will not include the RECURRENCE-ID values as EXDATEs of the parent).
            if (storedIncidence->recurs()) {
                KCalCore::Incidence::List instances = mCalendar->instances(incidence);
                Q_FOREACH (KCalCore::Incidence::Ptr instance, instances) {
                    if (instance->hasRecurrenceId()) {
                        storedIncidence->recurrence()->addExDateTime(instance->recurrenceId());
                    }
                }
            }

            storedIncidence->setCustomProperty("buteo", "uri", resource.href);
            storedIncidence->endUpdates();

            // Save the modified incidence so it can be used to check whether there were
            // local changes on the next sync.
            mModifiedIncidenceICalData.insert(KCalId(incidence), resource.iCalData);

            // Save the incidence etag.
            mUpdatedETags[resource.href] = resource.etag;
        }
    } else {
        LOG_DEBUG("Saving new event:" << KCalId(incidence).toString()
                                      << resource.href
                                      << resource.etag);
        KCalCore::Incidence::Ptr occurrence;
        if (incidence->hasRecurrenceId()) {
            // no dissociated occurrence exists already (ie, it's not an update), so create a new one.
            // need to detach, and then copy the properties into the detached occurrence.
            KCalCore::Incidence::Ptr recurringIncidence = mCalendar->event(incidence->uid(), KDateTime());
            if (recurringIncidence.isNull()) {
                LOG_WARNING("error: parent recurring incidence could not be retrieved:" << incidence->uid());
                return false;
            }
            occurrence = mCalendar->dissociateSingleOccurrence(recurringIncidence, incidence->recurrenceId(), incidence->recurrenceId().timeSpec());
            if (occurrence.isNull()) {
                LOG_WARNING("error: could not dissociate occurrence from recurring event:" << incidence->uid() << incidence->recurrenceId().toString());
                return false;
            }

            IncidenceHandler::prepareImportedIncidence(incidence);
            IncidenceHandler::copyIncidenceProperties(occurrence, incidence);
            occurrence->setCustomProperty("buteo", "uri", resource.href);
            if (!mCalendar->addEvent(occurrence.staticCast<KCalCore::Event>(), mNotebook->uid())) {
                LOG_WARNING("error: could not add dissociated occurrence to calendar");
                return false;
            }

            // Save the new incidence so it can be discarded from the list of local changes
            // on the next sync when local changes are sent to the server.
            mNewRemoteIncidenceIds << KCalId(incidence);
            mModifiedIncidenceICalData.insert(KCalId(incidence), resource.iCalData);
            mUpdatedETags[resource.href] = resource.etag;
            LOG_DEBUG("Added new occurrence incidence:" << KCalId(incidence).toString());
            return true;
        }

        // just a new event without needing detach.
        IncidenceHandler::prepareImportedIncidence(incidence);
        incidence->setCustomProperty("buteo", "uri", resource.href);
        mUpdatedETags[resource.href] = resource.etag;

        bool added = false;
        switch (incidence->type()) {
        case KCalCore::IncidenceBase::TypeEvent:
            added = mCalendar->addEvent(incidence.staticCast<KCalCore::Event>(), mNotebook->uid());
            break;
        case KCalCore::IncidenceBase::TypeTodo:
            added = mCalendar->addTodo(incidence.staticCast<KCalCore::Todo>(), mNotebook->uid());
            break;
        case KCalCore::IncidenceBase::TypeJournal:
            added = mCalendar->addJournal(incidence.staticCast<KCalCore::Journal>(), mNotebook->uid());
            break;
        case KCalCore::IncidenceBase::TypeFreeBusy:
        case KCalCore::IncidenceBase::TypeUnknown:
            LOG_WARNING("Unsupported incidence type:" << incidence->type());
            return false;
        }
        if (added) {
            // Save the new incidence so it can be discarded from the list of local changes
            // on the next sync when local changes are sent to the server.
            mNewRemoteIncidenceIds << KCalId(incidence);
            LOG_DEBUG("Added new incidence:" << KCalId(incidence).toString());
        } else {
            LOG_CRITICAL("Unable to add incidence" << KCalId(incidence).toString() << "to notebook" << mNotebook->uid());
            *criticalError = true;
            return false;
        }
    }
    return true;
}

bool NotebookSyncAgent::updateIncidences(const QList<Reader::CalendarResource> &resources)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // We need to coalesce any resources which have the same UID.
    // This can be the case if there is addition of both a recurring event,
    // and a modified occurrence of that event, in the same sync cycle.
    // To ensure that we deal with the original recurring event first,
    // we find the resource which includes that change and promote it
    // in the list (so that we deal with it before the other).
    QList<Reader::CalendarResource> orderedResources;
    for (int i = 0; i < resources.count(); ++i) {
        bool prependedResource = false;
        for (int j = 0; j < resources[i].incidences.count(); ++j) {
            if (!resources[i].incidences[j]->hasRecurrenceId()) {
                // we have a non-occurrence event which needs promotion.
                orderedResources.prepend(resources[i]);
                prependedResource = true;
                break;
            }
        }
        if (!prependedResource) {
            // this resource needs to be appended.
            orderedResources.append(resources[i]);
        }
    }

    for (int i = 0; i < orderedResources.count(); ++i) {
        const Reader::CalendarResource &resource = orderedResources.at(i);
        if (!resource.incidences.size()) {
            continue;
        }

        // Each resource is either a single event series (or non-recurring event) OR
        // a list of updated/added persistent exceptions to an existing series.
        // If the resource contains an event series which includes the base incidence,
        // then we need to compare the local series with the remote series, to ensure
        // we remove any incidences which occur locally but not remotely.
        // However, if the resource's incidence list does not contain the base incidence,
        // but instead contains just persistent exceptions (ie, have recurrenceId) then
        // we can assume that no persistent exceptions were removed - only added/updated.
        QString uid = resource.incidences.first()->uid();
        Q_FOREACH (KCalCore::Incidence::Ptr incidence, resource.incidences) {
            if (incidence->uid() != uid) {
                LOG_WARNING("Updated incidence list contains incidences with non-matching uids!");
                return false; // this is always an error.  each resource corresponds to a single event series.
            }
        }

        // find the recurring incidence (parent) in the update list, and save it.
        // alternatively, it may be a non-recurring base incidence.
        bool criticalError = false;
        int parentIndex = -1;
        for (int i = 0; i < resource.incidences.size(); ++i) {
            if (!resource.incidences[i]->hasRecurrenceId()) {
                parentIndex = i;
                break;
            }
        }

        if (parentIndex == -1) {
            LOG_DEBUG("No parent or base incidence in resource's incidence list, performing direct updates");
            for (int i = 0; i < resource.incidences.size(); ++i) {
                KCalCore::Incidence::Ptr remoteInstance = resource.incidences[i];
                updateIncidence(remoteInstance, resource, &criticalError);
                if (criticalError) {
                    LOG_WARNING("Error saving updated persistent occurrence of resource" << resource.href << ":" << remoteInstance->recurrenceId().toString());
                    return false;
                }
            }
            return true; // finished
        }

        // if there was a parent / base incidence, then we need to compare local/remote lists.
        // load the local (persistent) occurrences of the series.  Later we will update or remove them as required.
        KCalCore::Incidence::Ptr localBaseIncidence = mCalendar->incidence(uid, KDateTime());
        KCalCore::Incidence::List localInstances;
        if (!localBaseIncidence.isNull() && localBaseIncidence->recurs()) {
            localInstances = mCalendar->instances(localBaseIncidence); // TODO: should we use the updatedBaseIncidence here instead?
        }

        // first save the added/updated base incidence
        KCalCore::Incidence::Ptr updatedBaseIncidence = resource.incidences[parentIndex];
        updateIncidence(updatedBaseIncidence, resource, &criticalError); // update the base incidence first.
        if (criticalError) {
            LOG_WARNING("Error saving base incidence of resource" << resource.href);
            return false;
        }

        // update persistent exceptions which are in the remote list.
        QList<KDateTime> remoteRecurrenceIds;
        for (int i = 0; i < resource.incidences.size(); ++i) {
            if (i == parentIndex) {
                continue; // already handled this one.
            }

            KCalCore::Incidence::Ptr remoteInstance = resource.incidences[i];
            remoteRecurrenceIds.append(remoteInstance->recurrenceId());
            updateIncidence(remoteInstance, resource, &criticalError);
            if (criticalError) {
                LOG_WARNING("Error saving updated persistent occurrence of resource" << resource.href << ":" << remoteInstance->recurrenceId().toString());
                return false;
            }
        }

        // remove persistent exceptions which are not in the remote list.
        for (int i = 0; i < localInstances.size(); ++i) {
            KCalCore::Incidence::Ptr localInstance = localInstances[i];
            if (!remoteRecurrenceIds.contains(localInstance->recurrenceId())) {
                if (!mCalendar->deleteIncidence(localInstance)) {
                    LOG_WARNING("Error removing remotely deleted persistent occurrence of resource" << resource.href << ":" << localInstance->recurrenceId().toString());
                    return false;
                }
            }
        }
    }

    return true;
}

bool NotebookSyncAgent::deleteIncidences(const QSet<KCalId> &incidenceUids)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (incidenceUids.isEmpty() || mCalendarIncidencesBeforeSync.isEmpty()) {
        return true;
    }
    QHash<KCalId, KCalCore::Incidence::Ptr> calendarIncidencesMap;
    for (int i=0; i<mCalendarIncidencesBeforeSync.count(); i++) {
        calendarIncidencesMap.insert(KCalId(mCalendarIncidencesBeforeSync[i]), mCalendarIncidencesBeforeSync[i]);
    }
    Q_FOREACH (const KCalId &incidenceUid, incidenceUids) {
        if (calendarIncidencesMap.contains(incidenceUid)) {
            KCalCore::Incidence::Ptr calendarIncidence = calendarIncidencesMap.take(incidenceUid);
            switch (calendarIncidence->type()) {
            case KCalCore::IncidenceBase::TypeEvent:
                if (!mCalendar->deleteEvent(calendarIncidence.staticCast<KCalCore::Event>())) {
                    LOG_CRITICAL("Unable to delete Event = " << incidenceUid.toString() << calendarIncidence->customProperty("buteo", "uri"));
                    return false;
                }
                LOG_DEBUG("Deleted Event = " << incidenceUid.toString() << calendarIncidence->customProperty("buteo", "uri"));
                break;
            case KCalCore::IncidenceBase::TypeTodo:
                if (!mCalendar->deleteTodo(calendarIncidence.staticCast<KCalCore::Todo>())) {
                    LOG_CRITICAL("Unable to delete Todo = " << incidenceUid.toString() << calendarIncidence->customProperty("buteo", "uri"));
                    return false;
                }
                LOG_DEBUG("Deleted Todo = " << incidenceUid.toString() << calendarIncidence->customProperty("buteo", "uri"));
                break;
            case KCalCore::IncidenceBase::TypeJournal:
                if (!mCalendar->deleteJournal(calendarIncidence.staticCast<KCalCore::Journal>())) {
                    LOG_CRITICAL("Unable to delete Journal = " << incidenceUid.toString() << calendarIncidence->customProperty("buteo", "uri"));
                    return false;
                }
                LOG_DEBUG("Deleted Journal = " << incidenceUid.toString() << calendarIncidence->customProperty("buteo", "uri"));
                break;
            default:
                break;
            }
        }
    }
    return true;
}
