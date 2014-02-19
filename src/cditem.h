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

#ifndef CDITEM_H
#define CDITEM_H

#include <QString>

#include <incidence.h>

class CDItem
{
public:
    CDItem() {}

    void setHref(const QString &href);
    QString href();

    void setStatus(const QString &status);
    QString status();

    void setIncidence(KCalCore::Incidence::Ptr iPtr);
    KCalCore::Incidence::Ptr incidencePtr();

    void setETag(const QString &etag);
    QString etag();

private:
    QString mHref;
    QString mStatus;
    QString mETag;
    KCalCore::Incidence::Ptr mIncidencePtr;
};

#endif // CDITEM_H
