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

#include <QXmlStreamReader>
#include <QHash>

#include "cditem.h"

class Reader : public QObject
{
    Q_OBJECT

public:
    explicit Reader(QObject *parent = 0);
    ~Reader();

    void read(const QByteArray &data);
    QHash<QString, CDItem *> getIncidenceMap();

private:
    void readMultiStatus();
    void readResponse();
    void readHref(CDItem *item);
    void readPropStat(CDItem *item);
    void readProp(CDItem *item);
    void readStatus(CDItem *item);
    void readGetETag(CDItem *item);
    void readCalendarData(CDItem *item);

private:
    QXmlStreamReader *mReader;
    QHash<QString, CDItem*> mIncidenceMap;
};

#endif // READER_H
