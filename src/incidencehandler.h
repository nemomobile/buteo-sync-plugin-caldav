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
#ifndef INCIDENCEHANDLER_P_H
#define INCIDENCEHANDLER_P_H

#include <incidence.h>
#include <event.h>
#include <todo.h>
#include <journal.h>
#include <attendee.h>

class IncidenceHandler
{
public:
    static bool copiedPropertiesAreEqual(const KCalCore::Incidence::Ptr &a, const KCalCore::Incidence::Ptr &b);
    static void copyIncidenceProperties(KCalCore::Incidence::Ptr dest, const KCalCore::Incidence::Ptr &src);

private:
    IncidenceHandler();
    ~IncidenceHandler();

    static void copyEventProperties(KCalCore::Event::Ptr dest, const KCalCore::Event::Ptr &src);
    static void copyTodoProperties(KCalCore::Todo::Ptr dest, const KCalCore::Todo::Ptr &src);
    static void copyJournalProperties(KCalCore::Journal::Ptr dest, const KCalCore::Journal::Ptr &src);

    static bool eventsEqual(const KCalCore::Event::Ptr &a, const KCalCore::Event::Ptr &b);
    static bool todosEqual(const KCalCore::Todo::Ptr &a, const KCalCore::Todo::Ptr &b);
    static bool journalsEqual(const KCalCore::Journal::Ptr &a, const KCalCore::Journal::Ptr &b);
};

#endif // INCIDENCEHANDLER_P_H
