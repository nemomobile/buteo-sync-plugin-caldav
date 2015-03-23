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

#include <LogMacros.h>

#define PROP_DTSTART_DATE_ONLY "dtstart-date_only"
#define PROP_DTEND_DATE_ONLY "dtend-date_only"
#define PROP_DTEND_ADDED_USING_DTSTART "dtend-added-as-dtstart"

#define COPY_IF_NOT_EQUAL(dest, src, get, set) \
{ \
    if (dest->get != src->get) { \
        dest->set(src->get); \
    } \
}

#define RETURN_FALSE_IF_NOT_EQUAL(a, b, func, desc) {\
    if (a->func != b->func) {\
        LOG_DEBUG("Incidence" << desc << "" << "properties are not equal:" << a->func << b->func); \
        return false;\
    }\
}

#define RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(failureCheck, desc, debug) {\
    if (failureCheck) {\
        LOG_DEBUG("Incidence" << desc << "properties are not equal:" << desc << debug); \
        return false;\
    }\
}

IncidenceHandler::IncidenceHandler()
{
}

IncidenceHandler::~IncidenceHandler()
{
}

void IncidenceHandler::normalizePersonEmail(KCalCore::Person *p)
{
    QString email = p->email().replace(QStringLiteral("mailto:"), QString(), Qt::CaseInsensitive);
    if (email != p->email()) {
        p->setEmail(email);
    }
}

template <typename T>
bool IncidenceHandler::pointerDataEqual(const QVector<QSharedPointer<T> > &vectorA, const QVector<QSharedPointer<T> > &vectorB)
{
    if (vectorA.count() != vectorB.count()) {
        return false;
    }
    for (int i=0; i<vectorA.count(); i++) {
        if (vectorA[i].data() != vectorB[i].data()) {
            return false;
        }
    }
    return true;
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
    RETURN_FALSE_IF_NOT_EQUAL(a, b, type(), "type");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, duration(), "duration");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, hasDuration(), "hasDuration");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, isReadOnly(), "isReadOnly");
    RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(a->dtStart() != b->dtStart(), "dtStart", (a->dtStart().toString() + " " + b->dtStart().toString()));
    RETURN_FALSE_IF_NOT_EQUAL(a, b, comments(), "comments");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, contacts(), "contacts");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, altDescription(), "altDescription");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, categories(), "categories");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, customStatus(), "customStatus");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, description(), "description");
    RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(!qFuzzyCompare(a->geoLatitude(), b->geoLatitude()), "geoLatitude", (QString("%1 %2").arg(a->geoLatitude()).arg(b->geoLatitude())));
    RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(!qFuzzyCompare(a->geoLongitude(), b->geoLongitude()), "geoLongitude", (QString("%1 %2").arg(a->geoLongitude()).arg(b->geoLongitude())));
    RETURN_FALSE_IF_NOT_EQUAL(a, b, hasGeo(), "hasGeo");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, location(), "location");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, secrecy(), "secrecy");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, status(), "status");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, summary(), "summary");

    // Some servers insert a mailto: in the organizer email address, so ignore this when comparing organizers
    KCalCore::Person personA(*a->organizer().data());
    KCalCore::Person personB(*b->organizer().data());
    normalizePersonEmail(&personA);
    normalizePersonEmail(&personB);
    RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(personA != personB, "organizer", (personA.fullName() + " " + personB.fullName()));

    // check recurrence information
    RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(*(a->recurrence()) != *(b->recurrence()), "recurrence", "...");

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
    RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(a->dateEnd() != b->dateEnd(), "dateEnd", (a->dateEnd().toString() + " " + b->dateEnd().toString()));
    RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(a->dtEnd() != b->dtEnd(), "dtEnd", (a->dtEnd().toString() + " " + b->dtEnd().toString()));
    RETURN_FALSE_IF_NOT_EQUAL(a, b, isMultiDay(), "isMultiDay");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, transparency(), "transparency");

    // Don't compare hasEndDate() as Event(Event*) does not initialize it based on the validity of
    // dtEnd(), so it could be false when dtEnd() is valid. The dtEnd comparison above is sufficient.

    return true;
}

bool IncidenceHandler::todosEqual(const KCalCore::Todo::Ptr &a, const KCalCore::Todo::Ptr &b)
{
    RETURN_FALSE_IF_NOT_EQUAL(a, b, hasCompletedDate(), "hasCompletedDate");
    RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(a->dtRecurrence() != b->dtRecurrence(), "dtRecurrence", (a->dtRecurrence().toString() + " " + b->dtRecurrence().toString()));
    RETURN_FALSE_IF_NOT_EQUAL(a, b, hasDueDate(), "hasDueDate");
    RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(a->dtDue() != b->dtDue(), "dtDue", (a->dtDue().toString() + " " + b->dtDue().toString()));
    RETURN_FALSE_IF_NOT_EQUAL(a, b, hasStartDate(), "hasStartDate");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, isCompleted(), "isCompleted");
    RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(a->completed() != b->completed(), "completed", (a->completed().toString() + " " + b->completed().toString()));
    RETURN_FALSE_IF_NOT_EQUAL(a, b, isOpenEnded(), "isOpenEnded");
    RETURN_FALSE_IF_NOT_EQUAL(a, b, percentComplete(), "percentComplete");
    return true;
}

bool IncidenceHandler::journalsEqual(const KCalCore::Journal::Ptr &, const KCalCore::Journal::Ptr &)
{
    // no journal-specific properties; it only uses the base incidence properties
    return true;
}

void IncidenceHandler::copyIncidenceProperties(KCalCore::Incidence::Ptr dest, const KCalCore::Incidence::Ptr &src)
{
    if (!dest || !src) {
        qWarning() << "Invalid parameters!";
        return;
    }
    if (dest->type() != src->type()) {
        qWarning() << "incidences do not have same type!";
        return;
    }

    KDateTime origCreated = dest->created();
    KDateTime origLastModified = dest->lastModified();

    // Copy recurrence information if required.
    if (*(dest->recurrence()) != *(src->recurrence())) {
        dest->recurrence()->clear();

        KCalCore::Recurrence *dr = dest->recurrence();
        KCalCore::Recurrence *sr = src->recurrence();

        // recurrence rules and dates
        KCalCore::RecurrenceRule::List srRRules = sr->rRules();
        for (QList<KCalCore::RecurrenceRule*>::const_iterator it = srRRules.constBegin(), end = srRRules.constEnd(); it != end; ++it) {
            KCalCore::RecurrenceRule *r = new KCalCore::RecurrenceRule(*(*it));
            dr->addRRule(r);
        }
        dr->setRDates(sr->rDates());
        dr->setRDateTimes(sr->rDateTimes());

        // exception rules and dates
        KCalCore::RecurrenceRule::List srExRules = sr->exRules();
        for (QList<KCalCore::RecurrenceRule*>::const_iterator it = srExRules.constBegin(), end = srExRules.constEnd(); it != end; ++it) {
            KCalCore::RecurrenceRule *r = new KCalCore::RecurrenceRule(*(*it));
            dr->addExRule(r);
        }
        dr->setExDates(sr->exDates());
        dr->setExDateTimes(sr->exDateTimes());
    }

    // copy the duration before the dtEnd as calling setDuration() changes the dtEnd
    COPY_IF_NOT_EQUAL(dest, src, duration(), setDuration);

    if (dest->type() == KCalCore::IncidenceBase::TypeEvent && src->type() == KCalCore::IncidenceBase::TypeEvent) {
        KCalCore::Event::Ptr destEvent = dest.staticCast<KCalCore::Event>();
        KCalCore::Event::Ptr srcEvent = src.staticCast<KCalCore::Event>();
        COPY_IF_NOT_EQUAL(destEvent, srcEvent, dtEnd(), setDtEnd);
        COPY_IF_NOT_EQUAL(destEvent, srcEvent, transparency(), setTransparency);
    }

    if (dest->type() == KCalCore::IncidenceBase::TypeTodo && src->type() == KCalCore::IncidenceBase::TypeTodo) {
        KCalCore::Todo::Ptr destTodo = dest.staticCast<KCalCore::Todo>();
        KCalCore::Todo::Ptr srcTodo = src.staticCast<KCalCore::Todo>();
        COPY_IF_NOT_EQUAL(destTodo, srcTodo, completed(), setCompleted);
        COPY_IF_NOT_EQUAL(destTodo, srcTodo, dtRecurrence(), setDtRecurrence);
        COPY_IF_NOT_EQUAL(destTodo, srcTodo, percentComplete(), setPercentComplete);
    }

    // dtStart and dtEnd changes allDay value, so must set those before copying allDay value
    COPY_IF_NOT_EQUAL(dest, src, dtStart(), setDtStart);
    COPY_IF_NOT_EQUAL(dest, src, allDay(), setAllDay);

    COPY_IF_NOT_EQUAL(dest, src, hasDuration(), setHasDuration);
    COPY_IF_NOT_EQUAL(dest, src, organizer(), setOrganizer);
    COPY_IF_NOT_EQUAL(dest, src, isReadOnly(), setReadOnly);

    if (!pointerDataEqual(src->attendees(), dest->attendees())) {
        dest->clearAttendees();
        Q_FOREACH (const KCalCore::Attendee::Ptr &attendee, src->attendees()) {
            dest->addAttendee(attendee);
        }
    }

    if (src->comments() != dest->comments()) {
        dest->clearComments();
        Q_FOREACH (const QString &comment, src->comments()) {
            dest->addComment(comment);
        }
    }
    if (src->contacts() != dest->contacts()) {
        dest->clearContacts();
        Q_FOREACH (const QString &contact, src->contacts()) {
            dest->addContact(contact);
        }
    }

    COPY_IF_NOT_EQUAL(dest, src, altDescription(), setAltDescription);
    COPY_IF_NOT_EQUAL(dest, src, categories(), setCategories);
    COPY_IF_NOT_EQUAL(dest, src, customStatus(), setCustomStatus);
    COPY_IF_NOT_EQUAL(dest, src, description(), setDescription);
    COPY_IF_NOT_EQUAL(dest, src, geoLatitude(), setGeoLatitude);
    COPY_IF_NOT_EQUAL(dest, src, geoLongitude(), setGeoLongitude);
    COPY_IF_NOT_EQUAL(dest, src, hasGeo(), setHasGeo);
    COPY_IF_NOT_EQUAL(dest, src, location(), setLocation);
    COPY_IF_NOT_EQUAL(dest, src, resources(), setResources);
    COPY_IF_NOT_EQUAL(dest, src, secrecy(), setSecrecy);
    COPY_IF_NOT_EQUAL(dest, src, status(), setStatus);
    COPY_IF_NOT_EQUAL(dest, src, summary(), setSummary);
    COPY_IF_NOT_EQUAL(dest, src, revision(), setRevision);

    if (!pointerDataEqual(src->alarms(), dest->alarms())) {
        dest->clearAlarms();
        Q_FOREACH (const KCalCore::Alarm::Ptr &alarm, src->alarms()) {
            dest->addAlarm(alarm);
        }
    }

    if (!pointerDataEqual(src->attachments(), dest->attachments())) {
        dest->clearAttachments();
        Q_FOREACH (const KCalCore::Attachment::Ptr &attachment, src->attachments()) {
            dest->addAttachment(attachment);
        }
    }

    // Don't change created and lastModified properties as that affects mkcal
    // calculations for when the incidence was added and modified in the db.
    if (origCreated != dest->created()) {
        dest->setCreated(origCreated);
    }
    if (origLastModified != dest->lastModified()) {
        dest->setLastModified(origLastModified);
    }
}

void IncidenceHandler::prepareImportedIncidence(KCalCore::Incidence::Ptr incidence)
{
    if (!incidence->type() == KCalCore::IncidenceBase::TypeEvent) {
        return;
    }
    KCalCore::Event::Ptr event = incidence.staticCast<KCalCore::Event>();

    if (event->allDay()) {
        KDateTime dtStart = event->dtStart();
        KDateTime dtEnd = event->dtEnd();

        // calendar requires all-day events to have times in order to appear correctly
        if (dtStart.isDateOnly()) {
            incidence->setCustomProperty("buteo", PROP_DTSTART_DATE_ONLY, PROP_DTSTART_DATE_ONLY);
            dtStart.setTime(QTime(0, 0, 0, 0));
            event->setDtStart(dtStart);
            LOG_DEBUG("Added time to DTSTART, now" << dtStart.toString() << "for" << incidence->uid());
        } else {
            incidence->removeCustomProperty("buteo", PROP_DTSTART_DATE_ONLY);
        }
        if (dtEnd.isValid() && dtEnd.isDateOnly()) {
            incidence->setCustomProperty("buteo", PROP_DTEND_DATE_ONLY, PROP_DTEND_DATE_ONLY);
            dtEnd.setTime(QTime(0, 0, 0, 0));
            event->setDtEnd(dtEnd);
            LOG_DEBUG("Added time to DTEND, now" << dtEnd.toString() << "for" << incidence->uid());
        } else {
            incidence->removeCustomProperty("buteo", PROP_DTEND_DATE_ONLY);
        }

        // calendar processing requires all-day events to have a dtEnd
        if (!dtEnd.isValid()) {
            LOG_DEBUG("Adding DTEND to" << incidence->uid() << "as" << dtStart.toString());
            event->setCustomProperty("buteo", PROP_DTEND_ADDED_USING_DTSTART, PROP_DTEND_ADDED_USING_DTSTART);
            event->setDtEnd(dtStart);
        } else {
            event->removeCustomProperty("buteo", PROP_DTSTART_DATE_ONLY);
        }

        // setting dtStart/End changes the allDay value, so ensure it is still set to true
        event->setAllDay(true);
    }
}

KCalCore::Incidence::Ptr IncidenceHandler::incidenceToExport(KCalCore::Incidence::Ptr sourceIncidence)
{
    if (sourceIncidence->type() != KCalCore::IncidenceBase::TypeEvent) {
        return sourceIncidence;
    }
    KCalCore::Incidence::Ptr incidence = QSharedPointer<KCalCore::Incidence>(sourceIncidence->clone());
    KCalCore::Event::Ptr event = incidence.staticCast<KCalCore::Event>();

    if (event->allDay()) {
        bool sendWithoutDtEnd = !event->customProperty("buteo", PROP_DTEND_ADDED_USING_DTSTART).isEmpty()
                && (event->dtStart() == event->dtEnd());
        event->removeCustomProperty("buteo", PROP_DTEND_ADDED_USING_DTSTART);

        if (sendWithoutDtEnd) {
            // A single-day all-day event was received without a DTEND, and it is still a single-day
            // all-day event, so remove the DTEND before upsyncing.
            LOG_DEBUG("Remove DTEND from" << incidence->uid());
            event->setDtEnd(KDateTime());
        } else if (event->hasEndDate()) {
            KDateTime dt = event->dtEnd();
            // Event::dtEnd() is inclusive, but DTEND in iCalendar format is exclusive.
            LOG_DEBUG("Adding +1 day to" << dt.toString() << "to make exclusive DTEND" << "for" << incidence->uid());
            dt = dt.addDays(1);
            event->setDtEnd(dt);
        } else {
            KDateTime dt = event->dtStart().addDays(1);
            LOG_DEBUG("Adding DTEND of DTSTART+1" << dt.toString() << "for" << incidence->uid());
            event->setDtEnd(dt);
        }
    }

    // if the time was added by us, remove it before upsyncing to the server
    if (!event->customProperty("buteo", PROP_DTSTART_DATE_ONLY).isEmpty()) {
        KDateTime dt = event->dtStart();
        LOG_DEBUG("Strip time from start date" << dt.toString());
        dt.setDateOnly(true);
        event->setDtStart(dt);
        event->removeCustomProperty("buteo", PROP_DTSTART_DATE_ONLY);
    }
    if (!event->customProperty("buteo", PROP_DTEND_DATE_ONLY).isEmpty()) {
        KDateTime dt = event->dtEnd();
        LOG_DEBUG("Strip time from end date" << dt.toString());
        dt.setDateOnly(true);
        event->setDtEnd(dt);
        event->removeCustomProperty("buteo", PROP_DTEND_DATE_ONLY);
    }

    event->removeCustomProperty("buteo", "uri");

    // We used to add this custom property and upsync with it still intact. Make sure it's removed
    event->removeCustomProperty("buteo", "etag");

    return incidence;
}
