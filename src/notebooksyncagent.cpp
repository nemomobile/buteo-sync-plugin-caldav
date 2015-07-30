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

#include <LogMacros.h>
#include <SyncResults.h>

#include <memorycalendar.h>
#include <incidence.h>
#include <icalformat.h>
#include <event.h>
#include <todo.h>
#include <journal.h>
#include <attendee.h>

#include <QUuid>
#include <QDebug>


#define NOTEBOOK_FUNCTION_CALL_TRACE FUNCTION_CALL_TRACE(QString("%1 %2").arg(Q_FUNC_INFO).arg(mNotebook ? mNotebook->account() : ""))

namespace {
    // mKCal deletes custom properties of deleted incidences.
    // This is problematic for sync, as we need some fields
    // (resource URI and ETAG) in order to sync properly.
    // Hence, we abuse the COMMENTS field of the incidence.
    QString incidenceHrefUri(KCalCore::Incidence::Ptr incidence, const QString &remoteCalendarPath = QString(), bool *uriNeedsFilling = 0)
    {
        const QStringList &comments(incidence->comments());
        Q_FOREACH (const QString &comment, comments) {
            if (comment.startsWith("buteo:caldav:uri:")) {
                QString uri = comment.mid(17);
                if (uri.contains('%')) {
                    // if it contained a % or a space character, we percent-encoded
                    // the uri before storing it, because otherwise kcal doesn't
                    // split the comments properly.
                    uri = QUrl::fromPercentEncoding(uri.toUtf8());
                    LOG_DEBUG("URI comment was percent encoded:" << comment << ", returning uri:" << uri);
                }
                return uri;
            }
        }
        if (uriNeedsFilling) {
            // must be a newly locally-added event, with uri comment not yet set.
            // return the value which we should upload the event to.
            *uriNeedsFilling = true;
            return remoteCalendarPath + incidence->uid() + ".ics";
        }
        LOG_WARNING("Returning empty uri for:" << incidence->uid() << incidence->recurrenceId().toString());
        return QString();
    }
    void setIncidenceHrefUri(KCalCore::Incidence::Ptr incidence, const QString &hrefUri)
    {
        const QStringList &comments(incidence->comments());
        Q_FOREACH (const QString &comment, comments) {
            if (comment.startsWith("buteo:caldav:uri:")) {
                incidence->removeComment(comment);
                break;
            }
        }
        if (hrefUri.contains('%') || hrefUri.contains(' ')) {
            // need to percent-encode the uri before storing it,
            // otherwise mkcal doesn't split the comments correctly.
            incidence->addComment(QStringLiteral("buteo:caldav:uri:%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(hrefUri))));
        } else {
            incidence->addComment(QStringLiteral("buteo:caldav:uri:%1").arg(hrefUri));
        }
    }
    int findIncidenceMatchingHrefUri(KCalCore::Incidence::List incidences, const QString &hrefUri)
    {
        for (int i = 0; i < incidences.size(); ++i) {
            if (incidenceHrefUri(incidences[i]) == hrefUri) {
                return i;
            }
        }
        return -1;
    }
    QString incidenceETag(KCalCore::Incidence::Ptr incidence)
    {
        const QStringList &comments(incidence->comments());
        Q_FOREACH (const QString &comment, comments) {
            if (comment.startsWith("buteo:caldav:etag:")) {
                return comment.mid(18);
            }
        }
        return QString();
    }
    void setIncidenceETag(KCalCore::Incidence::Ptr incidence, const QString &etag)
    {
        const QStringList &comments(incidence->comments());
        Q_FOREACH (const QString &comment, comments) {
            if (comment.startsWith("buteo:caldav:etag:")) {
                incidence->removeComment(comment);
                break;
            }
        }
        incidence->addComment(QStringLiteral("buteo:caldav:etag:%1").arg(etag));
    }

    void uniteIncidenceLists(const KCalCore::Incidence::List &first, KCalCore::Incidence::List *second)
    {
        int originalSecondSize = second->size();
        bool foundMatch = false;
        Q_FOREACH (KCalCore::Incidence::Ptr inc, first) {
            foundMatch = false;
            for (int i = 0; i < originalSecondSize; ++i) {
                if (inc->uid() == second->at(i)->uid() && inc->recurrenceId() == second->at(i)->recurrenceId()) {
                    // found a match
                    foundMatch = true;
                    break;
                }
            }
            if (!foundMatch) {
                second->append(inc);
            }
        }
    }
}


NotebookSyncAgent::NotebookSyncAgent(mKCal::ExtendedCalendar::Ptr calendar,
                                     mKCal::ExtendedStorage::Ptr storage,
                                     QNetworkAccessManager *networkAccessManager,
                                     Settings *settings,
                                     const QString &remoteCalendarPath,
                                     QObject *parent)
    : QObject(parent)
    , mNetworkManager(networkAccessManager)
    , mSettings(settings)
    , mCalendar(calendar)
    , mStorage(storage)
    , mNotebook(0)
    , mRemoteCalendarPath(remoteCalendarPath)
    , mSyncMode(NoSyncMode)
    , mRetriedReport(false)
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
        QObject::disconnect(requests[i], 0, this, 0);
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
                                      const QDateTime &toDateTime)
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

    sendReportRequest();
}

void NotebookSyncAgent::sendReportRequest()
{
    // must be m_syncMode = SlowSync.
    Report *report = new Report(mNetworkManager, mSettings);
    mRequests.insert(report);
    connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
    report->getAllEvents(mRemoteCalendarPath, mFromDateTime, mToDateTime);
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
                                       const QDateTime &fromDateTime,
                                       const QDateTime &toDateTime)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    LOG_DEBUG("Start quick sync for notebook:" << notebook->uid()
              << "between" << fromDateTime << "to" << toDateTime
              << ", sync changes since" << changesSinceDate);

    mSyncMode = QuickSync;
    mNotebook = notebook;
    mChangesSinceDate = changesSinceDate;
    mFromDateTime = fromDateTime;
    mToDateTime = toDateTime;

    fetchRemoteChanges(mFromDateTime, mToDateTime);
}

void NotebookSyncAgent::fetchRemoteChanges(const QDateTime &fromDateTime, const QDateTime &toDateTime)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // must be m_syncMode = QuickSync.
    Report *report = new Report(mNetworkManager, mSettings);
    mRequests.insert(report);
    connect(report, SIGNAL(finished()), this, SLOT(processETags()));
    report->getAllETags(mRemoteCalendarPath, fromDateTime, toDateTime);
}

void NotebookSyncAgent::reportRequestFinished()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = qobject_cast<Report*>(sender());
    mRequests.remove(report);
    report->deleteLater();

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        if (mPossibleLocalModificationIncidenceIds.isEmpty()) {
            // every received resource was from m_remoteAdditions or m_remoteModifications.
            mReceivedCalendarResources = report->receivedCalendarResources().values();
        } else {
            // From the received resources, some will have been fetched so that we can perform a more detailed
            // per-event delta calculation (ie, the "possible" local modifications).
            // We can discard any incidence in the m_localModifications list which is not significantly
            // different to the incidence fetched from the remote server.
            // In either case, we will remove that resource from the receivedResources list because
            // we don't want to store the remote version of it (instead we probably want to upsync the local mod).
            int originalLocalModificationsCount = mLocalModifications.size(), discardedLocalModifications = 0;
            QMultiHash<QString, Reader::CalendarResource> receivedResources = report->receivedCalendarResources();
            Q_FOREACH (const QString &hrefUri, receivedResources.keys()) {
                QList<Reader::CalendarResource> resources = receivedResources.values(hrefUri);
                if (mPossibleLocalModificationIncidenceIds.contains(hrefUri)) {
                    // this resource was fetched so that we could perform a field-by-field delta detection
                    // just in case the only change was the ETAG/URI value (due to previous sync).
                    removePossibleLocalModificationIfIdentical(hrefUri,
                                                               mPossibleLocalModificationIncidenceIds.values(hrefUri),
                                                               resources,
                                                               mAddedPersistentExceptionOccurrences,
                                                               &mLocalModifications);
                } else {
                    // these were resources fetched from m_remoteAdditions or m_remoteModifications
                    mReceivedCalendarResources.append(resources);
                }
            }
            discardedLocalModifications = originalLocalModificationsCount - mLocalModifications.size();
            LOG_DEBUG("" << discardedLocalModifications << "out of" << originalLocalModificationsCount <<
                      "local modifications were discarded as spurious (etag/uri update only)");
        }

        LOG_DEBUG("Report request finished: received:"
                  << report->receivedCalendarResources().size() << "iCal blobs containing a total of"
                  << report->receivedCalendarResources().values().count() << "incidences"
                  << "of which" << mReceivedCalendarResources.size() << "incidences were remote additions/modifications");

        if (mSyncMode == QuickSync) {
            sendLocalChanges();
            return;
        }

        // NOTE: we don't store the remote artifacts yet
        // Instead, we just emit finished (for this notebook)
        // Once ALL notebooks are finished, then we apply the remote changes.
        // This prevents the worst partial-sync issues.

    } else if (mSyncMode == SlowSync
               && report->networkError() == QNetworkReply::AuthenticationRequiredError
               && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        LOG_WARNING("Retrying REPORT after request failed with QNetworkReply::AuthenticationRequiredError");
        mRetriedReport = true;
        sendReportRequest();
        return;
    }

    LOG_DEBUG("emitting report request finished with result:" << report->errorCode() << report->errorString());
    emitFinished(report->errorCode(), report->errorString());
}

void NotebookSyncAgent::processETags()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = qobject_cast<Report*>(sender());
    mRequests.remove(report);
    report->deleteLater();

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        LOG_DEBUG("Process tags for server path" << mRemoteCalendarPath);
        // we have a hash from resource href-uri to resource info (including etags).
        QMultiHash<QString, Reader::CalendarResource> map = report->receivedCalendarResources();
        QHash<QString, QString> remoteHrefUriToEtags;
        Q_FOREACH (const QString &href, map.keys()) {
            if (!href.contains(mRemoteCalendarPath)) {
                LOG_WARNING("href does not contain server path:" << href << ":" << mRemoteCalendarPath);
                emitFinished(Buteo::SyncResults::INTERNAL_ERROR, "unable to calculate remote resource uids");
                return;
            }
            QList<Reader::CalendarResource> resources = map.values(href);
            if (resources.size()) {
                remoteHrefUriToEtags.insert(href, resources[0].etag);
            }
        }

        // calculate the local and remote delta.
        if (!calculateDelta(KDateTime(mChangesSinceDate),
                            remoteHrefUriToEtags,
                            &mLocalAdditions,
                            &mLocalModifications,
                            &mLocalDeletions,
                            &mRemoteAdditions,
                            &mRemoteModifications,
                            &mRemoteDeletions)) {
            emitFinished(Buteo::SyncResults::INTERNAL_ERROR, "unable to calculate sync delta");
            return;
        }

        // Note that due to the fact that we update the ETAG and URI data in locally
        // upsynced events during sync, those incidences will be reported as modified
        // during the next sync cycle (even though the only changes may have been
        // that ETAG+URI change).  Hence, we need to fetch all of those again, and
        // then manually check equivalence (ignoring etag+uri value) with remote copy.
        QStringList fetchPossiblyLocallyModifiedIncidenceHrefUris;
        Q_FOREACH (KCalCore::Incidence::Ptr possiblyModified, mLocalModifications) {
            QString pmiHrefUri = incidenceHrefUri(possiblyModified);
            if (pmiHrefUri.isEmpty()) {
                // this was a modification reported to a previously partially-synced event.
                // we always treat this as a "definite" local modification.
            } else if (!fetchPossiblyLocallyModifiedIncidenceHrefUris.contains(pmiHrefUri)) {
                // this was a modification reported to a successfully synced event.
                // we always treat this as a "possible" local modification.
                // hence, we reload from remote and perform field-by-field comparison.
                fetchPossiblyLocallyModifiedIncidenceHrefUris.append(pmiHrefUri);
            }
        }

        // fetch updated and new items full data if required.
        QStringList fetchRemoteHrefUris = mRemoteAdditions + mRemoteModifications + fetchPossiblyLocallyModifiedIncidenceHrefUris;
        if (!fetchRemoteHrefUris.isEmpty()) {
            // some incidences have changed on the server, so fetch the new details
            Report *report = new Report(mNetworkManager, mSettings);
            mRequests.insert(report);
            connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
            report->multiGetEvents(mRemoteCalendarPath, fetchRemoteHrefUris);
            return;
        }

        // no remote modifications/additions we need to fetch; just upsync local changes.
        sendLocalChanges();
        return;
    } else if (report->networkError() == QNetworkReply::AuthenticationRequiredError && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        LOG_WARNING("Retrying ETAG REPORT after request failed with QNetworkReply::AuthenticationRequiredError");
        mRetriedReport = true;
        fetchRemoteChanges(mFromDateTime, mToDateTime);
        return;
    }

    // no remote changes to downsync, and no local changes to upsync - we're finished.
    LOG_DEBUG("no remote changes to downsync and no local changes to upsync - finished!");
    emitFinished(report->errorCode(), report->errorString());
}

void NotebookSyncAgent::sendLocalChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (!mLocalAdditions.count() && !mLocalModifications.count() && !mLocalDeletions.count()) {
        // no local changes to upsync.
        // we're finished syncing.
        LOG_DEBUG("no local changes to upsync - finished with notebook" << mNotebookName << mRemoteCalendarPath);
        emitFinished(Buteo::SyncResults::NO_ERROR, QString());
    } else {
        LOG_DEBUG("upsyncing local changes: A/M/R:" << mLocalAdditions.count() << "/" << mLocalModifications.count() << "/" << mLocalDeletions.count());
    }

    QSet<QString> addModUids;
    for (int i = 0; i < mLocalAdditions.count(); i++) {
        if (addModUids.contains(mLocalAdditions[i]->uid())) {
            continue; // already handled this one, as a result of a previous update of another occurrence in the series.
        } else {
            addModUids.insert(mLocalAdditions[i]->uid());
        }
        Put *put = new Put(mNetworkManager, mSettings);
        mRequests.insert(put);
        connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
        put->createEvent(mRemoteCalendarPath,
                         constructLocalChangeIcs(mLocalAdditions[i]),
                         mLocalAdditions[i]->uid());
    }
    for (int i = 0; i < mLocalModifications.count(); i++) {
        if (addModUids.contains(mLocalModifications[i]->uid())) {
            continue; // already handled this one, as a result of a previous update of another occurrence in the series.
        } else if (incidenceHrefUri(mLocalModifications[i]).isEmpty()) {
            LOG_WARNING("error: local modification without valid url:" << mLocalModifications[i]->uid() << "->" << incidenceHrefUri(mLocalModifications[i]));
            emitFinished(Buteo::SyncResults::INTERNAL_ERROR,
                         "Unable to determine remote uri for modified incidence:" + mLocalModifications[i]->uid());
            return;
        }
        // first, handle updates of exceptions by uploading the entire modified series.
        if (mLocalModifications[i]->hasRecurrenceId()) {
            addModUids.insert(mLocalModifications[i]->uid());
            Put *put = new Put(mNetworkManager, mSettings);
            mRequests.insert(put);
            connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
            put->updateEvent(mRemoteCalendarPath,
                             constructLocalChangeIcs(mLocalModifications[i]),
                             incidenceETag(mLocalModifications[i]),
                             incidenceHrefUri(mLocalModifications[i]),
                             mLocalModifications[i]->uid());
        }
    }
    for (int i = 0; i < mLocalModifications.count(); i++) {
        if (addModUids.contains(mLocalModifications[i]->uid())) {
            continue; // already handled this one, as a result of a previous update of another occurrence in the series.
        }
        // now handle updates of base incidences (which haven't otherwise already been upsynced), via direct update.
        // TODO: is this correct?  Or should we always generate the entire series as a resource we upload?
        // E.g. in the case where there is a pre-existing persistent exception, and the base-event gets modified,
        // does this PUT of the modified base-event cause (unwanted) changes/deletion of the persistent exception?
        KCalCore::ICalFormat icalFormat;
        Put *put = new Put(mNetworkManager, mSettings);
        mRequests.insert(put);
        connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
        put->updateEvent(mRemoteCalendarPath,
                         icalFormat.toICalString(IncidenceHandler::incidenceToExport(mLocalModifications[i])),
                         incidenceETag(mLocalModifications[i]),
                         incidenceHrefUri(mLocalModifications[i]),
                         mLocalModifications[i]->uid());
    }

    // For deletions, if a persistent exception is deleted we may need to do a PUT
    // containing all of the still-existing events in the series.
    // (Alternative is to push a STATUS:CANCELLED event?)
    // Hence, we first need to find out if any deletion is a lone-persistent-exception deletion.
    QMultiHash<QString, KDateTime> uidToRecurrenceIdDeletions;
    QHash<QString, QPair<QString, QString> > uidToEtagAndUri;  // we cannot look up custom properties of deleted incidences, so cache them here.
    Q_FOREACH (const LocalDeletion &localDeletion, mLocalDeletions) {
        uidToRecurrenceIdDeletions.insert(localDeletion.deletedIncidence->uid(), localDeletion.deletedIncidence->recurrenceId());
        uidToEtagAndUri.insert(localDeletion.deletedIncidence->uid(), qMakePair(localDeletion.remoteEtag, localDeletion.hrefUri));
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
            Put *put = new Put(mNetworkManager, mSettings);
            mRequests.insert(put);
            connect(put, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
            KCalCore::Incidence::Ptr recurringSeries = mCalendar->incidence(uid, KDateTime());
            if (!recurringSeries.isNull()) {
                put->updateEvent(mRemoteCalendarPath,
                                 constructLocalChangeIcs(recurringSeries),
                                 uidToEtagAndUri.value(uid).first,
                                 uidToEtagAndUri.value(uid).second,
                                 uid);
                continue; // finished with this deletion.
            } else {
                LOG_WARNING("Unable to load recurring incidence for deleted exception; deleting entire series instead");
                // fall through to the DELETE code below.
            }
        }

        // the whole series is being deleted; can DELETE.
        QString remoteUri = uidToEtagAndUri.value(uid).second;
        LOG_DEBUG("deleting whole series:" << remoteUri << "with uid:" << uid);
        Delete *del = new Delete(mNetworkManager, mSettings);
        mRequests.insert(del);
        connect(del, SIGNAL(finished()), this, SLOT(nonReportRequestFinished()));
        del->deleteEvent(remoteUri);
    }
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

    // After upsyncing changes (additions + modifications) we need to update local incidences.
    // For modifications, we need to get the new (server-side) etag values, and store them into those incidences.
    // For additions, we need to do that, and ALSO update the local URI value (to remote resource path).
    // We then set the modification date back to what is was before, so that the ETAG/URI update
    // doesn't get reported as an event modification during our next sync cycle.

    QStringList hrefsToReload;
    for (int i = 0; i < mLocalAdditions.count(); i++) {
        KCalCore::Incidence::Ptr &incidence = mLocalAdditions[i];
        QString href = mRemoteCalendarPath + incidence->uid() + ".ics"; // that's where we PUT the addition.
        if (!mUpdatedETags.contains(href)) {
            LOG_DEBUG("Did not receive ETag for incidence " << incidence->uid() << "- will reload from server");
            if (!hrefsToReload.contains(href)) {
                hrefsToReload.append(href);
            }
        } else {
            // Set the URI and the ETAG property to the required values.
            LOG_DEBUG("Adding URI and ETAG to locally added incidence:" << incidence->uid() << incidence->recurrenceId().toString() << ":" << href << mUpdatedETags[href]);
            KDateTime modDate = incidence->lastModified();
            incidence->startUpdates();
            setIncidenceHrefUri(incidence, href);
            setIncidenceETag(incidence, mUpdatedETags[href]);
            incidence->setLastModified(modDate);
            incidence->endUpdates();
            Reader::CalendarResource resource;
            resource.etag = mUpdatedETags[href];
            resource.href = href;
            resource.incidences = KCalCore::Incidence::List() << incidence;
            KCalCore::ICalFormat icalFormat;
            resource.iCalData = icalFormat.toICalString(IncidenceHandler::incidenceToExport(incidence));
            mReceivedCalendarResources.append(resource);
        }
    }

    for (int i = 0; i < mLocalModifications.count(); i++) {
        KCalCore::Incidence::Ptr &incidence = mLocalModifications[i];
        QString href = incidenceHrefUri(incidence);
        if (!mUpdatedETags.contains(href)) {
            LOG_DEBUG("Did not receive ETag for incidence " << incidence->uid() << "- will reload from server");
            if (!hrefsToReload.contains(href)) {
                hrefsToReload.append(href);
            }
        } else {
            LOG_DEBUG("Updating ETAG in locally modified incidence:" << incidence->uid() << incidence->recurrenceId().toString() << ":" << mUpdatedETags[href]);
            KDateTime modDate = incidence->lastModified();
            incidence->startUpdates();
            setIncidenceETag(incidence, mUpdatedETags[href]);
            incidence->setLastModified(modDate);
            incidence->endUpdates();
            Reader::CalendarResource resource;
            resource.etag = mUpdatedETags[href];
            resource.href = href;
            resource.incidences = KCalCore::Incidence::List() << incidence;
            KCalCore::ICalFormat icalFormat;
            resource.iCalData = icalFormat.toICalString(IncidenceHandler::incidenceToExport(incidence));
            mReceivedCalendarResources.append(resource);
        }
    }

    if (!hrefsToReload.isEmpty()) {
        Report *report = new Report(mNetworkManager, mSettings);
        mRequests.insert(report);
        connect(report, SIGNAL(finished()), this, SLOT(additionalReportRequestFinished()));
        report->multiGetEvents(mRemoteCalendarPath, hrefsToReload);
        return;
    } else {
        emitFinished(Buteo::SyncResults::NO_ERROR, QStringLiteral("Finished requests for %1").arg(mNotebook->account()));
    }
}

void NotebookSyncAgent::additionalReportRequestFinished()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // The server did not originally respond with the update ETAG values after
    // our initial PUT/UPDATE so we had to do an addition report request.
    // This response will contain the new ETAG values for any resource we
    // upsynced (ie, a local modification/addition).

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

bool NotebookSyncAgent::applyRemoteChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (mSyncMode == SlowSync) {
        // delete the existing notebook associated with this calendar path, if it exists
        // TODO: if required.  Currently we don't support per-notebook clean sync.

        // and create a new one
        mNotebook = mKCal::Notebook::Ptr(new mKCal::Notebook(mNotebookName, QString()));
        mNotebook->setAccount(mNotebookAccountId);
        mNotebook->setPluginName(mPluginName);
        mNotebook->setSyncProfile(mSyncProfile + ":" + mCalendarPath); // ugly hack because mkcal API is deficient.  I wanted to use uid field but it won't save.
        mNotebook->setColor(mColor);
        if (!mStorage->addNotebook(mNotebook)) {
            LOG_DEBUG("Unable to (re)create notebook" << mNotebookName << "during slow sync for account" << mNotebookAccountId << ":" << mCalendarPath);
            return false;
        }
    }

    if (!updateIncidences(mReceivedCalendarResources)) {
        return false;
    }
    if (!deleteIncidences(mRemoteDeletions)) {
        return false;
    }

    mNotebook->setSyncDate(mNotebookSyncedDateTime);
    mStorage->updateNotebook(mNotebook);

    return true;
}

void NotebookSyncAgent::emitFinished(int minorErrorCode, const QString &message)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (mFinished) {
        return;
    }
    mNotebookSyncedDateTime = KDateTime::currentUtcDateTime();
    mFinished = true;
    clearRequests();

    emit finished(minorErrorCode, message);
}

void NotebookSyncAgent::finalize()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;
}

bool NotebookSyncAgent::isFinished() const
{
    return mFinished;
}

// ------------------------------ Utility / implementation functions.

// called in the QuickSync codepath after fetching etags for remote resources.
// from the etags, we can determine the local and remote sync delta.
bool NotebookSyncAgent::calculateDelta(
        // in parameters:
        const KDateTime &fromDate,                     //  fromDate:    date to load local changes since
        const QHash<QString, QString> &remoteUriEtags, //  remoteEtags: map of uri to etag which exist on the remote server.
        // out parameters:
        KCalCore::Incidence::List *localAdditions,
        KCalCore::Incidence::List *localModifications,
        QList<LocalDeletion> *localDeletions,
        QList<QString> *remoteAdditions,
        QList<QString> *remoteModifications,
        KCalCore::Incidence::List *remoteDeletions)
{
    // Note that the mKCal API doesn't provide a way to get all deleted/modified incidences
    // for a notebook, as it implements the SQL query using an inequality on both modifiedAfter
    // and createdBefore; so instead we have to build a datetime which "should" satisfy
    // the inequality for all possible local modifications detectable since the last sync.
    KDateTime syncDateTime = mNotebook->syncDate().addSecs(1); // deleted after, created before...

    // load all local incidences
    KCalCore::Incidence::List localIncidences;
    if (!mStorage->allIncidences(&localIncidences, mNotebook->uid())) {
        emitFinished(Buteo::SyncResults::DATABASE_FAILURE, QString("Unable to load storage incidences for notebook: %1").arg(mNotebook->uid()));
        return false;
    }

    // separate them into buckets.
    // note that each remote URI can be associated with multiple local incidences (due recurrenceId incidences)
    // Here we can determine local additions and remote deletions.
    KCalCore::Incidence::List additions, addedPersistentExceptionOccurrences;
    if (!mStorage->insertedIncidences(&additions, fromDate < syncDateTime ? fromDate : syncDateTime, mNotebook->uid())) {
        LOG_CRITICAL("mKCal::ExtendedStorage::insertedIncidences() failed");
        return false;
    }
    // We only use the above "additions" list to find new exception occurrences.
    // We have to handle those specially since they WILL have a hrefUri set in them,
    // as they inherit all properties from the base recurring event.
    QSet<QString> seenRemoteUris;
    QHash<QString, QString> previouslySyncedEtags; // remote uri to the etag we saw last time.
    Q_FOREACH (KCalCore::Incidence::Ptr incidence, localIncidences) {
        bool uriWasEmpty = false;
        QString remoteUri = incidenceHrefUri(incidence, mRemoteCalendarPath, &uriWasEmpty);
        if (uriWasEmpty) {
            // must be either a new local addition or a previously-upsynced local addition
            // if we failed to update its uri after the successful upsync.
            if (remoteUriEtags.contains(remoteUri)) { // we saw this on remote side...
                // previously partially upsynced, needs uri update.
                LOG_DEBUG("have previously partially upsynced local addition, needs uri update:" << remoteUri);
                seenRemoteUris.insert(remoteUri);
            } else { // it doesn't exist on remote side...
                // new local addition.
                LOG_DEBUG("have new local addition:" << incidence->uid() << incidence->recurrenceId().toString());
                localAdditions->append(incidence);
                // Note: if it was partially upsynced and then connection failed
                // and then removed remotely, then on next sync (ie, this one)
                // it will appear like a "new" local addition.  TODO: FIXME? How?
            }
        } else {
            // this is a previously-synced incidence with a remote uri,
            // OR a newly-added persistent occurrence to a previously-synced recurring series.
            if (!remoteUriEtags.contains(remoteUri)) {
                LOG_DEBUG("have remote deletion of previously synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                remoteDeletions->append(incidence);
            } else {
                bool isNewLocalPersistentException = false;
                Q_FOREACH (KCalCore::Incidence::Ptr added, additions) {
                    bool addedUriEmpty = false;
                    QString addedUri = incidenceHrefUri(added, mRemoteCalendarPath, &addedUriEmpty);
                    if (addedUriEmpty) {
                        // ignore this one, it cannot be a persistent-exception occurrence
                        // otherwise it would inherit the uri value from the parent recurring series.
                        continue;
                    } else if (addedUri == remoteUri && added->recurrenceId().isValid() && added->recurrenceId() == incidence->recurrenceId()) {
                        LOG_DEBUG("Found new locally-added persistent exception:" << added->uid() << added->recurrenceId().toString() << ":" << addedUri);
                        addedPersistentExceptionOccurrences.append(incidence);
                        isNewLocalPersistentException = true;
                        mAddedPersistentExceptionOccurrences.insert(addedUri, added->recurrenceId());
                        break;
                    }
                }

                if (!isNewLocalPersistentException) {
                    // this is a possibly modified or possibly unchanged, previously synced incidence.
                    // we later check to see if it was modified server-side by checking the etag value.
                    LOG_DEBUG("have possibly modified or possibly unchanged previously synced local incidence:" << remoteUri);
                    seenRemoteUris.insert(remoteUri);
                    previouslySyncedEtags.insert(remoteUri, incidenceETag(incidence));
                }
            }
        }
    }

    // now determine local deletions.  Note that we combine deletions reported since
    // the from date and since the last sync date, due to mKCal API semantics.
    KCalCore::Incidence::List deleted, deletedSyncDate;
    uniteIncidenceLists(deletedSyncDate, &deleted);
    if (!mStorage->deletedIncidences(&deleted, fromDate, mNotebook->uid()) ||
        !mStorage->deletedIncidences(&deletedSyncDate, syncDateTime, mNotebook->uid())) {
        LOG_CRITICAL("mKCal::ExtendedStorage::deletedIncidences() failed");
        return false;
    }
    QSet<QString> deletedSeriesUids;
    Q_FOREACH (KCalCore::Incidence::Ptr incidence, deleted) {
        bool uriWasEmpty = false;
        QString remoteUri = incidenceHrefUri(incidence, mRemoteCalendarPath, &uriWasEmpty);
        if (remoteUriEtags.contains(remoteUri)) {
            if (uriWasEmpty) {
                // we originally upsynced this pure-local addition, but then connectivity was
                // lost before we updated the uid of it locally to include the remote uri.
                // subsequently, the user deleted the incidence.
                // Hence, it exists remotely, and has been deleted locally.
                LOG_DEBUG("have local deletion for partially synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
            } else {
                // the incidence was previously synced successfully.  it has now been deleted locally.
                LOG_DEBUG("have local deletion for previously synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
            }
            LocalDeletion localDeletion(incidence, remoteUriEtags.value(remoteUri), remoteUri);
            localDeletions->append(localDeletion);
            seenRemoteUris.insert(remoteUri);
            if (incidence->recurrenceId().isNull()) {
                // this is a deletion of an entire series.  We use this information later to remove
                // unnecessary deletions of exception occurrences if the base series is also deleted.
                deletedSeriesUids.insert(incidence->uid());
            }
        } else {
            // it was either already deleted remotely, or was never upsynced from the local prior to deletion.
            LOG_DEBUG("ignoring local deletion of non-existent remote incidence:" << incidence->uid() << incidence->recurrenceId().toString() << "at" << remoteUri);
        }
    }

    // now determine local modifications.  Note that we combine modifications reported since
    // the from date and since the last sync date, due to mKCal API semantics.
    // We also unite into the reported modifications any addedPersistentExceptionOccurrences
    // (calculated earlier) - we treat them as modifications of the series rather than additions.
    KCalCore::Incidence::List modified, modifiedSyncDate;
    if (!mStorage->modifiedIncidences(&modified, fromDate, mNotebook->uid()) ||
        !mStorage->modifiedIncidences(&modifiedSyncDate, syncDateTime, mNotebook->uid())) {
        LOG_CRITICAL("mKCal::ExtendedStorage::modifiedIncidences() failed");
        return false;
    }
    uniteIncidenceLists(modifiedSyncDate, &modified);
    uniteIncidenceLists(addedPersistentExceptionOccurrences, &modified);
    Q_FOREACH (KCalCore::Incidence::Ptr incidence, modified) {
        // if it also appears in localDeletions, ignore it - it was deleted locally.
        // if it also appears in localAdditions, ignore it - we are already uploading it.
        // if it doesn't appear in remoteEtags, ignore it - it was deleted remotely.
        // if its etag has changed remotely, ignore it - it was modified remotely.
        bool uriWasEmpty = false;
        QString remoteUri = incidenceHrefUri(incidence, mRemoteCalendarPath, &uriWasEmpty);
        if (uriWasEmpty) {
            // incidence either hasn't been synced before, or was partially synced.
            if (remoteUriEtags.contains(remoteUri)) { // yep, we previously upsynced it but then connectivity died.
                // partially synced previously, connectivity died before we could update the uri field with remote url.
                LOG_DEBUG("have local modification to partially synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                // note: we cannot check the etag to determine if it changed, since we may not have received the updated etag after the partial sync.
                // we treat this as a "definite" local modification due to the partially-synced status.
                localModifications->append(incidence);
                seenRemoteUris.insert(remoteUri);
            } else if (localAdditions->contains(incidence)) {
                LOG_DEBUG("ignoring local modification to locally added incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                continue;
            } else {
                LOG_DEBUG("ignoring local modification to remotely removed partially-synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                continue;
            }
        } else {
            // we have a modification to a previously-synced incidence.
            QString localEtag = incidenceETag(incidence);
            if (!remoteUriEtags.contains(remoteUri)) {
                LOG_DEBUG("ignoring local modification to remotely deleted incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                bool foundRemoteDeletion = false;
                for (int i = 0; i < remoteDeletions->size(); ++i) {
                    if (remoteDeletions->at(i)->uid() == incidence->uid() && remoteDeletions->at(i)->recurrenceId() == incidence->recurrenceId()) {
                        foundRemoteDeletion = true;
                        break;
                    }
                }
                if (!foundRemoteDeletion) {
                    // this should never happen, and is always an error.
                    LOG_WARNING("But unable to find corresponding remote deletion!  Aborting sync due to unrecoverable error!");
                    return false;
                }
            } else {
                // determine if the remote etag is still the same.
                // if it is not, then the incidence was modified server-side.
                if (localEtag != remoteUriEtags.value(remoteUri)) {
                    // if the etags are different, then the event was also modified remotely.
                    // we only support PreferRemote conflict resolution, so we discard the local modification.
                    LOG_DEBUG("ignoring local modification to remotely modified incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                    remoteModifications->append(remoteUri);
                } else {
                    // this is either a real local modification, or a modification being reported due to URI/ETAG update after previous sync.
                    // because it may be being reported due solely to URI/ETAG update, we treat it as a "possible" local modification.
                    LOG_DEBUG("have possible local modification:" << incidence->uid() << incidence->recurrenceId().toString());
                    localModifications->append(incidence); // NOTE: we later reload this event from server to detect "true" delta.
                    mPossibleLocalModificationIncidenceIds.insert(remoteUri, incidence->recurrenceId());
                }
                seenRemoteUris.insert(remoteUri);
            }
        }
    }

    // now determine remote additions and modifications.
    Q_FOREACH (const QString &remoteUri, remoteUriEtags.keys()) {
        if (!seenRemoteUris.contains(remoteUri)) {
            // this is probably a pure server-side addition, but there is one other possibility:
            // if it was newly added to the server before the previous sync cycle, then it will
            // have been added locally (due to remote addition) during the last sync cycle.
            // If the event was subsequently deleted locally prior to this sync cycle, then
            // mKCal will NOT report it as a deletion (or an addition) because it assumes that
            // it was a pure local addition + deletion.
            // The solution?  We need to manually search every deleted incidence for uri value.
            // Unfortunately, the mKCal API doesn't allow us to get all deleted incidences,
            // but we can get all incidences deleted since the last sync date.
            // That should suffice, and we've already injected those deletions into the deletions
            // list, so if we hit this branch, then it must be a new remote addition.
            LOG_DEBUG("have new remote addition:" << remoteUri);
            remoteAdditions->append(remoteUri);
        } else if (!previouslySyncedEtags.contains(remoteUri)) {
            // this is a server-side modification which is obsoleted by a local deletion
            LOG_DEBUG("ignoring remote modification to locally deleted incidence at:" << remoteUri);
        } else if (previouslySyncedEtags.value(remoteUri) != remoteUriEtags.value(remoteUri)) {
            // etag changed; this is a server-side modification.
            LOG_DEBUG("have remote modification to previously synced incidence at:" << remoteUri);
            LOG_DEBUG("previously seen ETag was:" << previouslySyncedEtags.value(remoteUri) << "-> new ETag is:" << remoteUriEtags.value(remoteUri));
            remoteModifications->append(remoteUri);
        } else {
            // this incidence is unchanged since last sync.
            LOG_DEBUG("unchanged server-side since last sync:" << remoteUri);
        }
    }

    // Now remove from the list of deletions, any deletion of a persistent exception occurrence
    // if the base recurring series was also deleted, since the deletion of the base series
    // will already ensure deletion of all exception occurrences when pushed to the remote.
    // Also remove from the list of deletions, any deletion of a persistent exception occurrence
    // if the base recurring series was modified remotely.  This one is a suboptimal case,
    // as it will effectively "roll-back" a local deletion of an occurrence, if the remote series
    // has changed.  But it's better than the alternative: being stuck in a sync failure loop.
    QList<LocalDeletion>::iterator it = localDeletions->begin();
    bool deletedIncidenceUriWasEmpty = false;
    QString deletedIncidenceRemoteUri;
    while (it != localDeletions->end()) {
        if (!(*it).deletedIncidence->recurrenceId().isNull()) {
            if (deletedSeriesUids.contains((*it).deletedIncidence->uid())) {
                LOG_DEBUG("ignoring deletion of persistent exception already handled by series deletion:"
                          << (*it).deletedIncidence->uid() << (*it).deletedIncidence->recurrenceId().toString());
                it = localDeletions->erase(it);
            } else {
                deletedIncidenceUriWasEmpty = false;
                deletedIncidenceRemoteUri = incidenceHrefUri((*it).deletedIncidence, mRemoteCalendarPath, &deletedIncidenceUriWasEmpty);
                if (!deletedIncidenceUriWasEmpty && remoteModifications->contains(deletedIncidenceRemoteUri)) {
                    // Sub-optimal case.  TODO: improve handling of this case.
                    LOG_DEBUG("ignoring deletion of persistent exception due to remote series modification:"
                              << (*it).deletedIncidence->uid() << (*it).deletedIncidence->recurrenceId().toString());
                    it = localDeletions->erase(it);
                }
            }
        } else {
            ++it;
        }
    }

    LOG_DEBUG("Calculated local  A/M/R:" << localAdditions->size() << "/" << localModifications->size() << "/" << localDeletions->size());
    LOG_DEBUG("Calculated remote A/M/R:" << remoteAdditions->size() << "/" << remoteModifications->size() << "/" << remoteDeletions->size());

    return true;
}

// Called in the QuickSync codepath after some local modifications were reported by mKCal.
// We fetched the remote version of the resource so that we can detect whether the local change
// is "real" or whether it was just reporting the local modification of the ETAG or URI field.
void NotebookSyncAgent::removePossibleLocalModificationIfIdentical(
        const QString &remoteUri,
        const QList<KDateTime> &recurrenceIds,
        const QList<Reader::CalendarResource> &remoteResources,
        const QMultiHash<QString, KDateTime> &addedPersistentExceptionOccurrences,
        KCalCore::Incidence::List *localModifications)
{
    // the remoteResources list contains all of the ical resources fetched from the remote URI.
    bool foundMatch = false;
    Q_FOREACH (const KDateTime &rid, recurrenceIds) {
        // find the possible local modification associated with this recurrenceId.
        int removeIdx = -1;
        for (int i = 0; i < localModifications->size(); ++i) {
            // Only compare incidences which relate to the remote resource.
            QString hrefUri = incidenceHrefUri(localModifications->at(i));
            if (hrefUri != remoteUri) {
                LOG_DEBUG("skipping unrelated local modification:" << localModifications->at(i)->uid()
                          << "(" << hrefUri << ") for remote uri:" << remoteUri);
                continue;
            }
            // Note: we compare the remote resources with the "export" version of the local modifications
            // otherwise spurious differences might be detected.
            const KCalCore::Incidence::Ptr &pLMod = IncidenceHandler::incidenceToExport(localModifications->at(i));
            if (pLMod->recurrenceId() == rid) {
                // check to see if the modification is actually an added persistent exception occurrence.
                if (addedPersistentExceptionOccurrences.values(hrefUri).contains(rid)) {
                    // The "modification" is actually an addition of a persistent exception occurrence
                    // which we treat as a modification of the series, then no remote incidence will exist yet.
                    foundMatch = true;
                    removeIdx = -1; // this is a real local modification. no-op, but for completeness.
                    break;
                }

                // not a persistent exception occurrence addition, must be a "normal" local modification.
                // found the local incidence.  now find the copy received from the server and detect changes.
                Q_FOREACH (const Reader::CalendarResource &resource, remoteResources) {
                    if (resource.href != remoteUri) {
                        LOG_WARNING("error while removing spurious possible local modifications: resource uri mismatch:" << resource.href << "->" << remoteUri);
                    } else {
                        Q_FOREACH (const KCalCore::Incidence::Ptr &remoteIncidence, resource.incidences) {
                            const KCalCore::Incidence::Ptr &exportRInc = IncidenceHandler::incidenceToExport(remoteIncidence);
                            if (exportRInc->recurrenceId() == rid) {
                                // found the remote incidence.  compare it to the local.
                                LOG_DEBUG("comparing:" << pLMod->uid() << "(" << remoteUri << ") to:" << exportRInc->uid() << "(" << resource.href << ")");
                                foundMatch = true;
                                if (IncidenceHandler::copiedPropertiesAreEqual(pLMod, exportRInc)) {
                                    removeIdx = i;  // this is a spurious local modification which needs to be removed.
                                } else {
                                    removeIdx = -1; // this is a real local modification. no-op, but for completeness.
                                }
                                break;
                            }
                        }
                    }
                    if (foundMatch) {
                        break;
                    }
                }
            }
            if (foundMatch) {
                break;
            }
        }

        // remove the possible local modification if it proved to be spurious
        if (foundMatch) {
            if (removeIdx >= 0) {
                LOG_DEBUG("discarding spurious local modification to:" << remoteUri << rid.toString());
                localModifications->remove(removeIdx);
            } else {
                LOG_DEBUG("local modification to:" << remoteUri << rid.toString() << "is real.");
            }
        } else {
            // this is always an internal logic error.  We explicitly requested it.
            LOG_WARNING("error: couldn't find remote incidence for possible local modification! FIXME!");
        }
    }
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

bool NotebookSyncAgent::updateIncidence(KCalCore::Incidence::Ptr incidence, KCalCore::Incidence::List notebookIncidences, const Reader::CalendarResource &resource, bool *criticalError)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (incidence.isNull()) {
        return false;
    }

    // find any existing local incidence which corresponds to the received incidence
    int matchingIncidenceIdx = findIncidenceMatchingHrefUri(notebookIncidences, resource.href);
    if (matchingIncidenceIdx >= 0) {
        LOG_DEBUG("found matching local incidence uid:" << notebookIncidences[matchingIncidenceIdx]->uid() <<
                  "for remote incidence:" << incidence->uid() << "from resource:" << resource.href << resource.etag);
        incidence->setUid(notebookIncidences[matchingIncidenceIdx]->uid()); // should not be different...
    }

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
            LOG_DEBUG("Queuing existing event for deletion:" << storedIncidence->uid() << storedIncidence->recurrenceId().toString()
                                                             << resource.href << resource.etag);
            mLocalDeletions.append(LocalDeletion(incidence, resource.etag, resource.href));
        } else {
            LOG_DEBUG("Updating existing event:" << storedIncidence->uid() << storedIncidence->recurrenceId().toString()
                                                 << resource.href << resource.etag);
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

            // ensure we set the url and etag as required.
            setIncidenceHrefUri(storedIncidence, resource.href);
            setIncidenceETag(storedIncidence, resource.etag);
            storedIncidence->endUpdates();
        }
    } else {
        // the new incidence will be either a new persistent occurrence, or a new base-series (or new non-recurring).
        LOG_DEBUG("Have new incidence:" << incidence->uid() << incidence->recurrenceId().toString()
                                        << resource.href << resource.etag);

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
            setIncidenceHrefUri(occurrence, resource.href);
            setIncidenceETag(occurrence, resource.etag);
            if (!mCalendar->addEvent(occurrence.staticCast<KCalCore::Event>(), mNotebook->uid())) {
                LOG_WARNING("error: could not add dissociated occurrence to calendar");
                return false;
            }
            LOG_DEBUG("Added new occurrence incidence:" << occurrence->uid() << occurrence->recurrenceId().toString());
            return true;
        }

        // just a new event without needing detach.
        IncidenceHandler::prepareImportedIncidence(incidence);
        setIncidenceHrefUri(incidence, resource.href);
        setIncidenceETag(incidence, resource.etag);

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
            LOG_DEBUG("Added new incidence:" << incidence->uid() << incidence->recurrenceId().toString());
        } else {
            LOG_CRITICAL("Unable to add incidence" << incidence->uid() << incidence->recurrenceId().toString() << "to notebook" << mNotebook->uid());
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

        // load the list of current incidences in the notebook.
        // later, we will match remote incidences with local ones, based on the URI value.
        KCalCore::Incidence::List notebookIncidences;
        mStorage->loadNotebookIncidences(mNotebook->uid());
        mStorage->allIncidences(&notebookIncidences, mNotebook->uid());

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
            LOG_DEBUG("No parent or base incidence in resource's incidence list, performing direct updates to persistent occurrences");
            for (int i = 0; i < resource.incidences.size(); ++i) {
                KCalCore::Incidence::Ptr remoteInstance = resource.incidences[i];
                updateIncidence(remoteInstance, notebookIncidences, resource, &criticalError);
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
        LOG_DEBUG("Saving the added/updated base incidence before saving persistent exceptions:" << resource.incidences[parentIndex]->uid());
        KCalCore::Incidence::Ptr updatedBaseIncidence = resource.incidences[parentIndex];
        updateIncidence(updatedBaseIncidence, notebookIncidences, resource, &criticalError); // update the base incidence first.
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

            LOG_DEBUG("Now saving a persistent exception:" << resource.incidences[i]->recurrenceId().toString());
            KCalCore::Incidence::Ptr remoteInstance = resource.incidences[i];
            remoteRecurrenceIds.append(remoteInstance->recurrenceId());
            updateIncidence(remoteInstance, notebookIncidences, resource, &criticalError);
            if (criticalError) {
                LOG_WARNING("Error saving updated persistent occurrence of resource" << resource.href << ":" << remoteInstance->recurrenceId().toString());
                return false;
            }
        }

        // remove persistent exceptions which are not in the remote list.
        for (int i = 0; i < localInstances.size(); ++i) {
            KCalCore::Incidence::Ptr localInstance = localInstances[i];
            if (!remoteRecurrenceIds.contains(localInstance->recurrenceId())) {
                LOG_DEBUG("Now removing remotely-removed persistent occurrence:" << localInstance->recurrenceId().toString());
                if (!mCalendar->deleteIncidence(localInstance)) {
                    LOG_WARNING("Error removing remotely deleted persistent occurrence of resource" << resource.href << ":" << localInstance->recurrenceId().toString());
                    return false;
                }
            }
        }
    }

    return true;
}

bool NotebookSyncAgent::deleteIncidences(KCalCore::Incidence::List deletedIncidences)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;
    Q_FOREACH (KCalCore::Incidence::Ptr doomed, deletedIncidences) {
        mStorage->load(doomed->uid());
        if (!mCalendar->deleteIncidence(mCalendar->incidence(doomed->uid(), doomed->recurrenceId()))) {
            LOG_CRITICAL("Unable to delete incidence: " << doomed->uid() << doomed->recurrenceId().toString());
            return false;
        } else {
            LOG_DEBUG("Deleted incidence: " << doomed->uid() << doomed->recurrenceId().toString());
        }
    }
    return true;
}

