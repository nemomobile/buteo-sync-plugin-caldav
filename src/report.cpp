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

Report::Report(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "REPORT", parent)
{
    FUNCTION_CALL_TRACE;
}

void Report::getAllEvents()
{
    FUNCTION_CALL_TRACE;

    QNetworkRequest request;
    QUrl url(mSettings->url());
    if (!mSettings->authToken().isEmpty()) {
        request.setRawHeader(QString("Authorization").toLatin1(), QString("Bearer " + mSettings->authToken()).toLatin1());
    } else {
        url.setUserName(mSettings->username());
        url.setPassword(mSettings->password());
    }
    request.setUrl(url);

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
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(slotError(QNetworkReply::NetworkError)));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Report::getAllETags()
{
    FUNCTION_CALL_TRACE;

    QNetworkRequest request;
    QUrl url(mSettings->url());
    if (!mSettings->authToken().isEmpty()) {
        request.setRawHeader(QString("Authorization").toLatin1(), QString("Bearer " + mSettings->authToken()).toLatin1());
    } else {
        url.setUserName(mSettings->username());
        url.setPassword(mSettings->password());
    }
    request.setUrl(url);
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
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(slotError(QNetworkReply::NetworkError)));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Report::multiGetEvents(const QStringList &eventIdList, bool includeCalendarData)
{
    FUNCTION_CALL_TRACE;

    if (eventIdList.isEmpty()) return;

    QNetworkRequest request;
    QUrl url(mSettings->url());
    if (!mSettings->authToken().isEmpty()) {
        request.setRawHeader(QString("Authorization").toLatin1(), QString("Bearer " + mSettings->authToken()).toLatin1());
    } else {
        url.setUserName(mSettings->username());
        url.setPassword(mSettings->password());
    }
    request.setUrl(url);
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
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(slotError(QNetworkReply::NetworkError)));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Report::processEvents()
{
    FUNCTION_CALL_TRACE;

    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        emit syncError(Sync::SYNC_ERROR);
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        emit finished();
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
        mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
        mKCal::ExtendedStorage::Ptr storage = calendar->defaultStorage(calendar);
        storage->open();
        QString aId = QString::number(mSettings->accountId());
        QString nbUid;
        mKCal::Notebook::List notebookList = storage->notebooks();
        LOG_DEBUG("Total Number of Notebooks in device = " << notebookList.count());
        Q_FOREACH (mKCal::Notebook::Ptr nbPtr, notebookList) {
            LOG_DEBUG(nbPtr->uid() << "     Notebook's' Account ID " << nbPtr->account() << "    Looking for Account ID = " << aId);
            if(nbPtr->account() == aId) {
                nbUid = nbPtr->uid();
            }
        }
        if (nbUid.isNull() || nbUid.isEmpty()) {
            LOG_WARNING("Not able to find NoteBook's UID...... Won't Save Events ");
            emit syncError(Sync::SYNC_ERROR);
            return;
        }
        storage->loadNotebookIncidences(nbUid);
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
                    origEvent = calendar->event(event->uid());
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
                        calendar->addEvent(event, nbUid);
                    }
                    break;
                case KCalCore::IncidenceBase::TypeTodo:
                    todo = item->incidencePtr().staticCast<KCalCore::Todo>();
                    origTodo = calendar->todo(todo->uid());
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
                        calendar->addTodo(todo, nbUid);
                    }
                    break;
                case KCalCore::IncidenceBase::TypeJournal:
                    journal = item->incidencePtr().staticCast<KCalCore::Journal>();
                    journal->setCustomProperty("buteo", "uri", item->href());
                    journal->setCustomProperty("buteo", "etag", item->etag());
                    calendar->addJournal(journal, nbUid);
                    break;
                case KCalCore::IncidenceBase::TypeFreeBusy:
                case KCalCore::IncidenceBase::TypeUnknown:
                    break;
            }
        }
        calendar->save();
        storage->save();
        storage->close();
    }
    emit finished();
}

void Report::processETags()
{
    FUNCTION_CALL_TRACE;

    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        emit syncError(Sync::SYNC_ERROR);
        return;
    }
    QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCode.isValid()) {
        int status = statusCode.toInt();
        if (status > 299) {
            qWarning() << "Got error status response for REPORT:" << status;
            reply->deleteLater();
            emit finished();
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
        mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
        mKCal::ExtendedStorage::Ptr storage = calendar->defaultStorage(calendar);
        storage->open();
        QString aId = QString::number(mSettings->accountId());
        QString nbUid;
        mKCal::Notebook::List notebookList = storage->notebooks();
        LOG_DEBUG("Total Number of Notebooks in device = " << notebookList.count());
        Q_FOREACH (mKCal::Notebook::Ptr nbPtr, notebookList) {
            if(nbPtr->account() == aId) {
                nbUid = nbPtr->uid();
            }
        }
        if (nbUid.isNull() || nbUid.isEmpty()) {
            LOG_WARNING("Not able to find NoteBook's UID...... Won't Save Events ");
            emit syncError(Sync::SYNC_ERROR);
            return;
        }
        storage->loadNotebookIncidences(nbUid);

        KCalCore::Incidence::List incidenceList = calendar->incidences(QDateTime::currentDateTime().addMonths(-12).date(),
                                                                       QDateTime::currentDateTime().addMonths(12).date());
        LOG_DEBUG("Total incidences in list = " << incidenceList.count());
        KCalCore::Event::Ptr event ;
        KCalCore::Todo::Ptr todo ;
        KCalCore::Journal::Ptr journal ;
        Q_FOREACH (KCalCore::Incidence::Ptr incidence , incidenceList) {
            QString uri = incidence->customProperty("buteo", "uri");
            if (uri == NULL || uri.isEmpty()) {
                //Newly added to Local DB -- Skip this incidence
                continue;
            }

            CDItem *item = map.take(uri);
            if (item == 0) {
                switch(incidence->type()) {
                    case KCalCore::IncidenceBase::TypeEvent:
                        event = incidence.staticCast<KCalCore::Event>();
                        LOG_DEBUG("DELETING Event = " << uri << calendar->deleteEvent(event));
                        break;
                    case KCalCore::IncidenceBase::TypeTodo:
                        todo = incidence.staticCast<KCalCore::Todo>();
                        LOG_DEBUG("DELETING Todo = " << uri << calendar->deleteTodo(todo));
                        break;
                    case KCalCore::IncidenceBase::TypeJournal:
                        journal = incidence.staticCast<KCalCore::Journal>();
                        LOG_DEBUG("DELETING Journal = " << uri << calendar->deleteJournal(journal));
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
            multiGetEvents(eventIdList, true);
        } else {
            emit finished();
        }

        calendar->save();
        storage->save();
        storage->close();
    }
}

void Report::updateETags()
{
    FUNCTION_CALL_TRACE;

    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        emit syncError(Sync::SYNC_ERROR);
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        emit finished();
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
        mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
        mKCal::ExtendedStorage::Ptr storage = calendar->defaultStorage(calendar);
        storage->open();
        storage->load();
        KCalCore::Event::Ptr event;
        KCalCore::Todo::Ptr todo;
        KCalCore::Journal::Ptr journal;
        QHash<QString, CDItem*>::const_iterator iter = map.constBegin();
        while(iter != map.constEnd()) {
            CDItem *item = iter.value();
            QString uid = item->incidencePtr()->uid();
            LOG_DEBUG("UID to be updated = " << uid << "     ETAG = " << item->etag());
            ++iter;
            event = calendar->event(uid);
            if (!event.isNull()) {
                event->startUpdates();
                event->setCustomProperty("buteo", "etag", item->etag());
                event->endUpdates();
                LOG_DEBUG("ETAG was updated = " << item->etag());
                continue;
            }
            todo = calendar->todo(uid);
            if (!todo.isNull()) {
                todo->startUpdates();
                todo->setCustomProperty("buteo", "etag", item->etag());
                todo->endUpdates();
                continue;
            }
            journal = calendar->journal(uid);
            if (!journal.isNull()) {
                journal->startUpdates();
                journal->setCustomProperty("buteo", "etag", item->etag());
                journal->endUpdates();
                continue;
            }
            LOG_DEBUG("Could not find the correct TYPE of INCIDENCE ");
        }

        calendar->save();
        storage->save();
        storage->close();
    }

    emit finished();
}
