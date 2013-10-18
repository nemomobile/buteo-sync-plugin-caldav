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

#include "reader.h"
#include "cditem.h"

#include <incidence.h>
#include <icalformat.h>

#include <QDebug>
#include <QStringList>
#include <QUrl>

#include <LogMacros.h>

Reader::Reader(QObject *parent) :
    QObject(parent)
{
}

Reader::~Reader() {
    delete mReader;
}

void Reader::read(const QByteArray data) {
    mReader = new QXmlStreamReader(data);
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "multistatus") {
            readMultiStatus();
        }
    }
}

void Reader::readMultiStatus() {
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "response") {
            readResponse();
        }
    }
}

void Reader::readResponse() {
    CDItem *item = new CDItem();
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "href") {
            readHref(item);
        } else if (mReader->name() == "propstat") {
            readPropStat(item);
        } else {
            delete item;
            continue;
        }
    }
    QString uid = item->href();
    LOG_DEBUG(QUrl::fromPercentEncoding(uid.toLatin1()));
    mIncidenceMap.insert(QUrl::fromPercentEncoding(uid.toLatin1()), item);
}

void Reader::readHref(CDItem* item) {
    item->setHref(mReader->readElementText());
    qDebug() << item->href();
}

void Reader::readPropStat(CDItem* item) {
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "prop") {
            readProp(item);
        } else if (mReader->name() == "status") {
            readStatus(item);
        }
    }
}

void Reader::readStatus(CDItem* item) {
    item->setStatus(mReader->readElementText());
    qDebug() << item->status();
}

void Reader::readProp(CDItem* item) {
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "getetag") {
            readGetETag(item);
        } else if (mReader->name() == "calendar-data") {
            readCalendarData(item);
        }
    }
}

void Reader::readGetETag(CDItem* item) {
    item->setETag(mReader->readElementText());
    qDebug() << item->etag();
}

void Reader::readCalendarData(CDItem* item) {
    QString event = mReader->readElementText();
    KCalCore::ICalFormat *icalFormat = new KCalCore::ICalFormat();
    KCalCore::Incidence::Ptr incidence = icalFormat->fromString(event);
    if (incidence != 0) {
        item->setIncidence(incidence);

        KCalCore::Incidence *inc = (KCalCore::Incidence*)incidence.data();
    }
}

QHash<QString, CDItem *> Reader::getIncidenceMap() {
    return mIncidenceMap;
}
