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

#include "incidencehandler.h"

#include <QDebug>
#include <QBuffer>
#include <QDataStream>

IncidenceHandler::IncidenceHandler()
{
}

IncidenceHandler::~IncidenceHandler()
{
}

// Checks whether a specific set of properties are equal.
bool IncidenceHandler::copiedPropertiesAreEqual(const KCalCore::Incidence::Ptr &a, const KCalCore::Incidence::Ptr &b)
{
    if (!a || !b) {
        qWarning() << "Invalid paramters! a:" << a << "b:" << b;
        return false;
    }

    // Do not compare created() or lastModified() because we don't update these fields when
    // an incidence is updated by copyIncidenceProperties(), so they are guaranteed to be unequal.
    // TODO compare deref alarms and attachment lists to compare them also.
    // Don't compare resources() for now because KCalCore may insert QStringList("") as the resources
    // when in fact it should be QStringList(), which causes the comparison to fail.
    if (a->type() != b->type()
            || a->allDay() != b->allDay()
            || a->duration() != b->duration()
            || a->hasDuration() != b->hasDuration()
            || *a->organizer().data() != *b->organizer().data()
            || a->isReadOnly() != b->isReadOnly()
            || a->dtStart() != b->dtStart()
            || a->comments() != b->comments()
            || a->contacts() != b->contacts()
            || a->altDescription() != b->altDescription()
            || a->categories() != b->categories()
            || a->customStatus() != b->customStatus()
            || a->description() != b->description()
            || !qFuzzyCompare(a->geoLatitude(), b->geoLatitude())
            || !qFuzzyCompare(a->geoLongitude(), b->geoLongitude())
            || a->hasGeo() != b->hasGeo()
            || a->location() != b->location()
            || a->secrecy() != b->secrecy()
            || a->status() != b->status()
            || a->summary() != b->summary()) {
        return false;
    }

    switch (a->type()) {
    case KCalCore::IncidenceBase::TypeEvent:
        if (!eventsEqual(a.staticCast<KCalCore::Event>(), b.staticCast<KCalCore::Event>())) {
            return false;
        }
        break;
    case KCalCore::IncidenceBase::TypeTodo:
        if (!todosEqual(a.staticCast<KCalCore::Todo>(), b.staticCast<KCalCore::Todo>())) {
            return false;
        }
        break;
    case KCalCore::IncidenceBase::TypeJournal:
        if (!journalsEqual(a.staticCast<KCalCore::Journal>(), b.staticCast<KCalCore::Journal>())) {
            return false;
        }
        break;
    case KCalCore::IncidenceBase::TypeFreeBusy:
    case KCalCore::IncidenceBase::TypeUnknown:
        break;
    }
    return true;
}

bool IncidenceHandler::eventsEqual(const KCalCore::Event::Ptr &a, const KCalCore::Event::Ptr &b)
{
    return a->hasEndDate() == b->hasEndDate()
            && a->dateEnd() == b->dateEnd()
            && a->dtEnd() == a->dtEnd()
            && a->isMultiDay() == b->isMultiDay()
            && a->transparency() == b->transparency();
}

bool IncidenceHandler::todosEqual(const KCalCore::Todo::Ptr &a, const KCalCore::Todo::Ptr &b)
{
    return a->hasCompletedDate() == b->hasCompletedDate()
            && a->dtRecurrence() == a->dtRecurrence()
            && a->dtStart() == b->dtStart()
            && a->hasDueDate() == b->hasDueDate()
            && a->dtDue() == b->dtDue()
            && a->hasStartDate() == b->hasStartDate()
            && a->isCompleted() == b->isCompleted()
            && a->completed() == b->completed()
            && a->isOpenEnded() == b->isOpenEnded()
            && a->isOverdue() == b->isOverdue()
            && a->mimeType() == b->mimeType()
            && a->percentComplete() == b->percentComplete();
}

bool IncidenceHandler::journalsEqual(const KCalCore::Journal::Ptr &a, const KCalCore::Journal::Ptr &b)
{
    return a->mimeType() == b->mimeType();
}

void IncidenceHandler::copyIncidenceProperties(KCalCore::Incidence::Ptr dest, const KCalCore::Incidence::Ptr &src)
{
    if (dest->type() != src->type()) {
        qWarning() << "incidences do not have same type!";
        return;
    }

    KDateTime origCreated = dest->created();
    KDateTime origLastModified = dest->lastModified();

    // Recurrences need to be copied through serialization, else they are not recreated.
    dest->clearRecurrence();
    QBuffer buffer;
    buffer.open(QBuffer::ReadWrite);
    QDataStream out(&buffer);
    out << *src.data();
    QDataStream in(&buffer);
    buffer.seek(0);
    in >> *dest.data();

    dest->setAllDay(src->allDay());
    dest->setDuration(src->duration());
    dest->setHasDuration(src->hasDuration());
    dest->setOrganizer(src->organizer());
    dest->setReadOnly(src->isReadOnly());
    dest->setDtStart(src->dtStart());

    dest->clearAttendees();
    Q_FOREACH (const KCalCore::Attendee::Ptr &attendee, src->attendees()) {
        dest->addAttendee(attendee);
    }
    dest->clearComments();
    Q_FOREACH (const QString &comment, src->comments()) {
        dest->addComment(comment);
    }
    dest->clearContacts();
    Q_FOREACH (const QString &contact, src->contacts()) {
        dest->addContact(contact);
    }

    dest->setAltDescription(src->altDescription());
    dest->setCategories(src->categories());
    dest->setCustomStatus(src->customStatus());
    dest->setDescription(src->description());
    dest->setGeoLatitude(src->geoLatitude());
    dest->setGeoLongitude(src->geoLongitude());
    dest->setHasGeo(src->hasGeo());
    dest->setLocation(src->location());
    dest->setResources(src->resources());
    dest->setSecrecy(src->secrecy());
    dest->setStatus(src->status());
    dest->setSummary(src->summary());
    dest->setRevision(src->revision());

    dest->clearAlarms();
    Q_FOREACH (const KCalCore::Alarm::Ptr &alarm, src->alarms()) {
        dest->addAlarm(alarm);
    }

    dest->clearAttachments();
    Q_FOREACH (const KCalCore::Attachment::Ptr &attachment, src->attachments()) {
        dest->addAttachment(attachment);
    }

    // Don't change created and lastModified properties as that affects mkcal
    // calculations for when the incidence was added and modified in the db.
    dest->setCreated(origCreated);
    dest->setLastModified(origLastModified);

    prepareIncidenceProperties(dest);
}

void IncidenceHandler::prepareIncidenceProperties(KCalCore::Incidence::Ptr incidence)
{
    if (incidence->allDay()) {
        if (incidence->type() == KCalCore::IncidenceBase::TypeEvent) {
            // All-day events must go from midnight of one day to midnight of the next, else
            // it is not recognized as an all-day event even if the flag is set.
            KCalCore::Event::Ptr event = incidence.staticCast<KCalCore::Event>();
            KDateTime start = incidence->dtStart();
            start.setTime(QTime(0, 0, 0, 0));
            KDateTime end = start.addDays(1);
            event->setDtStart(start);
            event->setDtEnd(end);
        }
    }
}
