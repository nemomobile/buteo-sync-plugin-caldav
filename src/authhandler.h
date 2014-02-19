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

#ifndef AUTHHANDLER_H
#define AUTHHANDLER_H

#include <QObject>

#include <SyncCommonDefs.h>
#include <SyncProfile.h>
#include <SignOn/AuthService>
#include <SignOn/Identity>

#include <Accounts/Account>

class AuthHandler : public QObject
{
    Q_OBJECT
public:
    explicit AuthHandler(const quint32 accountId, const QString &scope, QObject *parent = 0);

    void authenticate();
    const QString token();
    bool init();
    const QString username();
    const QString password();

Q_SIGNALS:
    void success();
    void failed();

private:
    void getToken();
    void processTokenResponse(const QByteArray &tokenJSON);
    void deviceAuth();
    void processDeviceCode(const QByteArray &deviceCodeJSON);

    QString	iDeviceCode;
    QString iUserCode;
    QString iVerificationURL;
    QString iToken;

    QString storedKeyValue(const char *provider, const char *service, const char *keyName);

private Q_SLOTS:
    void credentialsStored(const quint32);
    void error(const SignOn::Error &);
    void sessionResponse(const SignOn::SessionData &);

private:
    SignOn::Identity    *mIdentity;
    SignOn::AuthSession *mSession;
    Accounts::Account   *mAccount;
    QString mToken;
    QString mUsername;
    QString mPassword;
    QString mScope;
    QString mMethod, mMechanism;
};

#endif // AUTHHANDLER_H
