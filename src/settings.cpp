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

#include "settings.h"

Settings::Settings()
    : mAccountId(0)
    , mIgnoreSSLErrors(false)
{
}

QString Settings::authToken() const
{
    return mOAuthToken;
}

void Settings::setAuthToken(const QString & token)
{
    mOAuthToken = token;
}

bool Settings::ignoreSSLErrors() const
{
    return mIgnoreSSLErrors;
}

void Settings::setIgnoreSSLErrors(bool ignore)
{
    mIgnoreSSLErrors = ignore;
}

QString Settings::password() const
{
    return mPassword;
}

void Settings::setPassword(const QString & password)
{
    mPassword = password;
}

QString Settings::username() const
{
    return mUsername;
}

void Settings::setUsername(const QString & username)
{
    mUsername = username;
}

void Settings::setAccountId(quint32 accountId)
{
    mAccountId = accountId;
}

quint32 Settings::accountId() const
{
    return mAccountId;
}

void Settings::setServerAddress(const QString &serverAddress)
{
    mServerAddress = serverAddress;
}

QString Settings::serverAddress() const
{
    return mServerAddress;
}

void Settings::setCalendars(const QList<CalendarInfo> &calendars)
{
    mCalendars = calendars;
}

QList<Settings::CalendarInfo> Settings::calendars() const
{
    return mCalendars;
}

QString Settings::notebookId(const QString &calendarServerPath) const
{
    return QString::number(mAccountId) + "-" + calendarServerPath;
}
