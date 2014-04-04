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

#include "report.h"
#include "reader.h"
#include "put.h"
#include "settings.h"

#include <QNetworkAccessManager>
#include <QBuffer>
#include <QDebug>
#include <QStringList>
#include <QTimer>

#include <icalformat.h>
#include <incidence.h>
#include <event.h>
#include <todo.h>
#include <journal.h>
#include <attendee.h>

#include <extendedcalendar.h>
#include <extendedstorage.h>
#include <notebook.h>

Report::Report(QNetworkAccessManager *manager, Settings *settings, mKCal::ExtendedCalendar::Ptr calendar, mKCal::ExtendedStorage::Ptr storage, QObject *parent)
    : Request(manager, settings, "REPORT", parent)
    , mCalendar(calendar)
    , mStorage(storage)
{
    FUNCTION_CALL_TRACE;
}

bool Report::initRequest(const QString &serverPath)
{
    if (!mStorage || !mCalendar) {
        finishedWithInternalError("calendar or storage not specified");
        return false;
    }
    mKCal::Notebook::Ptr notebook;
    QString notebookId = mSettings->notebookId(serverPath);
    mKCal::Notebook::List notebookList = mStorage->notebooks();
    LOG_DEBUG("Total Number of Notebooks in device = " << notebookList.count());
    Q_FOREACH (mKCal::Notebook::Ptr nbPtr, notebookList) {
        LOG_DEBUG(nbPtr->uid() << "     Notebook's' Account ID " << nbPtr->account() << " Looking for Account ID = " << notebookId);
        if (nbPtr->account() == notebookId) {
            notebook = nbPtr;
            break;
        }
    }
    if (!notebook) {
        finishedWithError(Buteo::SyncResults::DATABASE_FAILURE, QStringLiteral("Cannot find notebook UID, cannot save any events"));
        return false;
    }
    mNotebook = notebook;
    return true;
}

void Report::getAllEvents(const QString &serverPath)
{
    FUNCTION_CALL_TRACE;
    if (!initRequest(serverPath)) {
        return;
    }
    mServerPath = serverPath;
    QNetworkRequest request;
    prepareRequest(&request, serverPath);
    request.setRawHeader("Depth", "1");
    request.setRawHeader("Prefer", "return-minimal");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData("<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">" \
                    "<d:prop> <d:getetag /> <c:calendar-data /> </d:prop>"       \
                    "<c:filter> <c:comp-filter name=\"VCALENDAR\" /> </c:filter>" \
                    "</c:calendar-query>");
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, buffer->buffer());
    connect(reply, SIGNAL(finished()), this, SLOT(processEvents()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Report::getAllETags(const QString &serverPath)
{
    FUNCTION_CALL_TRACE;
    if (!initRequest(serverPath)) {
        return;
    }
    mServerPath = serverPath;
    QNetworkRequest request;
    prepareRequest(&request, serverPath);
    request.setRawHeader("Depth", "1");
    request.setRawHeader("Prefer", "return-minimal");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData("<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">" \
                    "<d:prop> <d:getetag /> </d:prop> " \
                    "<c:filter> <c:comp-filter name=\"VCALENDAR\" > " \
                    "</c:comp-filter> </c:filter>" \
                    "</c:calendar-query> ");
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, buffer->buffer());

    connect(reply, SIGNAL(finished()), this, SLOT(processETags()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Report::multiGetEvents(const QString &serverPath, const QStringList &eventIdList, bool includeCalendarData)
{
    FUNCTION_CALL_TRACE;

    if (eventIdList.isEmpty()) {
        return;
    }
    QNetworkRequest request;
    prepareRequest(&request, serverPath);
    request.setRawHeader("Depth", "1");
    request.setRawHeader("Prefer", "return-minimal");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");

    QString multiGetRequest = "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">" \
                              "<d:prop><d:getetag />";
    if (includeCalendarData) {
        multiGetRequest.append("<c:calendar-data />");
    }
    multiGetRequest.append("</d:prop>");

    Q_FOREACH (const QString &eventId , eventIdList) {
        multiGetRequest.append("<d:href>");
        multiGetRequest.append(eventId);
        multiGetRequest.append("</d:href>");
    }
    multiGetRequest.append("</c:calendar-multiget>");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData(multiGetRequest.toLatin1());
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);

    if (includeCalendarData) {
        connect(reply, SIGNAL(finished()), this, SLOT(processEvents()));
    } else {
        connect(reply, SIGNAL(finished()), this, SLOT(updateETags()));
    }
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Report::processEvents()
{
    FUNCTION_CALL_TRACE;

    if (wasDeleted()) {
        LOG_DEBUG(command() << "request was aborted");
        return;
    }
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        finishedWithInternalError();
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        finishedWithReplyResult(reply->error());
        return;
    }
    QByteArray data = reply->readAll();
    reply->deleteLater();

    if (!data.isNull() && !data.isEmpty()) {
        Reader reader;
        reader.read(data);
        LOG_DEBUG("Total content length of the data = " << data.length());
        LOG_DEBUG(data);
        QHash<QString, CDItem*> map = reader.getIncidenceMap();
        QString nbUid = mNotebook->uid();
        KCalCore::Event::Ptr event ;
        KCalCore::Todo::Ptr todo ;
        KCalCore::Journal::Ptr journal ;
        KCalCore::Event::Ptr origEvent ;
        KCalCore::Todo::Ptr origTodo ;
        QHash<QString, CDItem*>::const_iterator iter = map.constBegin();
        while(iter != map.constEnd()) {
            CDItem *item = iter.value();
            ++iter;
            if (item->incidencePtr().isNull()) continue;

            switch(item->incidencePtr()->type()) {
                case KCalCore::IncidenceBase::TypeEvent:
                    event = item->incidencePtr().staticCast<KCalCore::Event>();
                    origEvent = mCalendar->event(event->uid());
                    LOG_DEBUG("UID of the event = " << event->uid());
                    //If Event is already added to Calendar, then update its property
                    if (origEvent != NULL) {
                        origEvent->startUpdates();
                        origEvent->setLocation(event->location());
                        origEvent->setSummary(event->summary());
                        origEvent->setDescription(event->description());
                        origEvent->setCustomProperty("buteo", "etag", item->etag());
                        origEvent->setCustomProperty("buteo", "uri", item->href());
                        origEvent->setHasDuration(event->hasDuration());
                        origEvent->setDuration(event->duration());
                        origEvent->setLastModified(event->lastModified());
                        origEvent->setOrganizer(event->organizer());
                        origEvent->setReadOnly(event->isReadOnly());
                        origEvent->setDtStart(event->dtStart());
                        origEvent->setHasEndDate(event->hasEndDate());
                        origEvent->setDtEnd(event->dtEnd());
                        origEvent->setAllDay(event->allDay());
                        origEvent->setSecrecy(event->secrecy());
                        KCalCore::Attendee::List attendeeList = event->attendees();
                        origEvent->clearAttendees();
                        Q_FOREACH (KCalCore::Attendee::Ptr attendee , attendeeList) {
                            origEvent->addAttendee(attendee);
                        }
                        origEvent->endUpdates();
                    } else {
                        event->setCustomProperty("buteo", "uri", item->href());
                        event->setCustomProperty("buteo", "etag", item->etag());
                        if (!mCalendar->addEvent(event, nbUid)) {
                            LOG_WARNING("Unable to add event" << event->uid() << "to notebook" << nbUid);
                        }
                    }
                    break;
                case KCalCore::IncidenceBase::TypeTodo:
                    todo = item->incidencePtr().staticCast<KCalCore::Todo>();
                    origTodo = mCalendar->todo(todo->uid());
                    //If Event is already added to Calendar, then update its property
                    if (origTodo != NULL) {
                        origTodo->startUpdates();
                        origTodo->setLocation(todo->location());
                        origTodo->setSummary(todo->summary());
                        origTodo->setDescription(todo->description());
                        origTodo->setCustomProperty("buteo", "etag", item->etag());
                        origTodo->setCustomProperty("buteo", "uri", item->href());
                        origTodo->setLastModified(todo->lastModified());
                        origTodo->setOrganizer(todo->organizer());
                        origTodo->setReadOnly(todo->isReadOnly());
                        origTodo->setDtStart(todo->dtStart());
                        origTodo->setSecrecy(todo->secrecy());
                        origTodo->endUpdates();
                    } else {
                        todo->setCustomProperty("buteo", "uri", item->href());
                        todo->setCustomProperty("buteo", "etag", item->etag());
                        if (!mCalendar->addTodo(todo, nbUid)) {
                            LOG_WARNING("Unable to add todo" << todo->uid() << "to notebook" << nbUid);
                        }
                    }
                    break;
                case KCalCore::IncidenceBase::TypeJournal:
                    journal = item->incidencePtr().staticCast<KCalCore::Journal>();
                    journal->setCustomProperty("buteo", "uri", item->href());
                    journal->setCustomProperty("buteo", "etag", item->etag());
                    if (!mCalendar->addJournal(journal, nbUid)) {
                        LOG_WARNING("Unable to add event" << journal->uid() << "to notebook" << nbUid);
                    }
                    break;
                case KCalCore::IncidenceBase::TypeFreeBusy:
                case KCalCore::IncidenceBase::TypeUnknown:
                    break;
            }
        }
    }
    finishedWithSuccess();
}

void Report::processETags()
{
    FUNCTION_CALL_TRACE;

    LOG_DEBUG("Process tags for server path" << mServerPath);

    if (wasDeleted()) {
        LOG_DEBUG(command() << "request was aborted");
        return;
    }

    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        finishedWithInternalError();
        return;
    }
    QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCode.isValid()) {
        int status = statusCode.toInt();
        if (status > 299) {
            finishedWithError(Buteo::SyncResults::INTERNAL_ERROR,
                              QString("Got error status response for REPORT: %1").arg(status));
            reply->deleteLater();
            return;
        }
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    if (!data.isNull() && !data.isEmpty()) {
        LOG_DEBUG(data);
        Reader reader;
        reader.read(data);
        LOG_DEBUG("Total content length of the data = " << data.length());
        QHash<QString, CDItem*> map = reader.getIncidenceMap();
        QStringList eventIdList;

        // Incidences must be loaded with ExtendedStorage::allIncidences() rather than
        // ExtendedCalendar::incidences(), because the latter will load incidences from all
        // notebooks, rather than just the one for this report.
        KCalCore::Incidence::List storageIncidenceList;
        if (!mStorage->allIncidences(&storageIncidenceList, mNotebook->uid())) {
            finishedWithError(Buteo::SyncResults::DATABASE_FAILURE, QString("Unable to load storage incidences for notebook: %1").arg(mNotebook->uid()));
            return;
        }
        // Since these incidence refs come from ExtendedStorage rather than ExtendedCalendar,
        // calling ExtendedCalendar::deleteEvent() etc. with these refs will fail, so save the refs to
        // a list and CalDavClient can match them to refs from ExtendedCalendar later to delete them.
        Q_FOREACH (KCalCore::Incidence::Ptr incidence, storageIncidenceList) {
            QString uri = incidence->customProperty("buteo", "uri");
            if (uri == NULL || uri.isEmpty()) {
                //Newly added to Local DB -- Skip this incidence
                continue;
            }
            CDItem *item = map.take(uri);
            if (item == 0) {
                switch (incidence->type()) {
                case KCalCore::IncidenceBase::TypeEvent:
                case KCalCore::IncidenceBase::TypeTodo:
                case KCalCore::IncidenceBase::TypeJournal:
                    mIncidencesToDelete.append(incidence);
                    break;
                case KCalCore::IncidenceBase::TypeFreeBusy:
                case KCalCore::IncidenceBase::TypeUnknown:
                    break;
                }
                continue;
            } else {
                if (incidence->customProperty("buteo", "etag") != item->etag()) {
                    eventIdList.append(item->href());
                }
            }
        }
        eventIdList.append(map.keys());
        if (!eventIdList.isEmpty()) {
            multiGetEvents(mServerPath, eventIdList, true);
        } else {
            finishedWithSuccess();
        }
    }
}

void Report::updateETags()
{
    FUNCTION_CALL_TRACE;

    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        finishedWithInternalError();
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        finishedWithReplyResult(reply->error());
        return;
    }
    QByteArray data = reply->readAll();
    debugReply(*reply, data);
    reply->deleteLater();

    if (!data.isNull() && !data.isEmpty()) {
        Reader reader;
        reader.read(data);
        LOG_DEBUG("Total content length of the data = " << data.length());
        QHash<QString, CDItem*> map = reader.getIncidenceMap();
        KCalCore::Event::Ptr event;
        KCalCore::Todo::Ptr todo;
        KCalCore::Journal::Ptr journal;
        QHash<QString, CDItem*>::const_iterator iter = map.constBegin();
        while(iter != map.constEnd()) {
            CDItem *item = iter.value();
            QString uid = item->incidencePtr()->uid();
            LOG_DEBUG("UID to be updated = " << uid << "     ETAG = " << item->etag());
            ++iter;
            event = mCalendar->event(uid);
            if (!event.isNull()) {
                event->startUpdates();
                event->setCustomProperty("buteo", "etag", item->etag());
                event->endUpdates();
                LOG_DEBUG("ETAG was updated = " << item->etag());
                continue;
            }
            todo = mCalendar->todo(uid);
            if (!todo.isNull()) {
                todo->startUpdates();
                todo->setCustomProperty("buteo", "etag", item->etag());
                todo->endUpdates();
                continue;
            }
            journal = mCalendar->journal(uid);
            if (!journal.isNull()) {
                journal->startUpdates();
                journal->setCustomProperty("buteo", "etag", item->etag());
                journal->endUpdates();
                continue;
            }
            LOG_DEBUG("Could not find the correct TYPE of INCIDENCE ");
        }
    }
    finishedWithSuccess();
}

KCalCore::Incidence::List Report::incidencesToDelete() const
{
    return mIncidencesToDelete;
}
