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

#ifndef REPORT_H
#define REPORT_H

#include "request.h"
#include "reader.h"

#include <incidence.h>

#include <QObject>

class QNetworkAccessManager;
class Settings;

class Report : public Request
{
    Q_OBJECT

public:
    explicit Report(QNetworkAccessManager *manager, Settings *settings, QObject *parent = 0);

    void getAllEvents(const QString &serverPath, const QDateTime &fromDateTime, const QDateTime &toDateTime);
    void getAllETags(const QString &serverPath,
                     const KCalCore::Incidence::List &currentLocalIncidences,
                     const KCalCore::Incidence::List &localDeletedIncidences,
                     const QDateTime &fromDateTime,
                     const QDateTime &toDateTime);

    QList<Reader::CalendarResource> receivedCalendarResources() const;
    QStringList localIncidenceUidsNotOnServer() const;

private Q_SLOTS:
    void processEvents();
    void processETags();

private:
    void multiGetEvents(const QString &serverPath, const QStringList &eventIdList);
    bool initRequest(const QString &serverPath);

    QList<Reader::CalendarResource> mReceivedResources;
    QStringList mLocalIncidenceUidsNotOnServer;
    KCalCore::Incidence::List mLocalIncidences;
    KCalCore::Incidence::List mLocalDeletedIncidences;
    QString mServerPath;
};

#endif // REPORT_H
