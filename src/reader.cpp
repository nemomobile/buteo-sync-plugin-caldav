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

#include "reader.h"

#include <QDebug>
#include <QUrl>
#include <QXmlStreamReader>

#include <LogMacros.h>

Reader::Reader(QObject *parent)
    : QObject(parent)
    , mReader(0)
{
}

Reader::~Reader()
{
    delete mReader;
}

void Reader::read(const QByteArray &data)
{
    delete mReader;
    mReader = new QXmlStreamReader(data);
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "multistatus") {
            readMultiStatus();
        }
    }
}

const QHash<QString, Reader::CalendarResource>& Reader::results() const
{
    return mResults;
}

QString Reader::hrefToUid(const QString &href)
{
    QString result = href;
    int slash = result.lastIndexOf('/');
    if (slash >= 0 && slash+1 < result.length()) {
        result = result.mid(slash + 1);
    }
    int ext = result.lastIndexOf(".ics");
    if (ext >= 0) {
        result = result.mid(0, ext);
    }
    return result;
}

void Reader::readMultiStatus()
{
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "response") {
            readResponse();
        }
    }
}

void Reader::readResponse()
{
    CalendarResource resource;
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "href") {
            resource.href = mReader->readElementText();
        } else if (mReader->name() == "propstat") {
            readPropStat(&resource);
        }
    }
    if (resource.href.isEmpty()) {
        LOG_WARNING("Ignoring received calendar object data, is missing href value");
        return;
    }
    LOG_DEBUG(QUrl::fromPercentEncoding(resource.href.toLatin1()));
    mResults.insert(QUrl::fromPercentEncoding(resource.href.toLatin1()), resource);
}

void Reader::readPropStat(CalendarResource *resource)
{
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "prop") {
            readProp(resource);
        } else if (mReader->name() == "status") {
            resource->status = mReader->readElementText();
        }
    }
}

void Reader::readProp(CalendarResource *resource)
{
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "getetag") {
            resource->etag = mReader->readElementText();
        } else if (mReader->name() == "calendar-data") {
            resource->iCalData = mReader->readElementText();
        }
    }
}
