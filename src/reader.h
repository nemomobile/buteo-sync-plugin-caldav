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

#ifndef READER_H
#define READER_H

#include <QObject>
#include <QMultiHash>

#include <incidence.h>

class QXmlStreamReader;

class Reader : public QObject
{
    Q_OBJECT
public:
    struct CalendarResource {
        QString href;
        QString etag;
        QString status;
        QString iCalData;
        KCalCore::Incidence::List incidences;
    };

    explicit Reader(QObject *parent = 0);
    ~Reader();

    void read(const QByteArray &data);
    const QMultiHash<QString, CalendarResource>& results() const;

private:
    void readMultiStatus();
    void readResponse();
    void readPropStat(CalendarResource *resource);
    void readProp(CalendarResource *resource);

private:
    QXmlStreamReader *mReader;
    QMultiHash<QString, CalendarResource> mResults;
};

#endif // READER_H
