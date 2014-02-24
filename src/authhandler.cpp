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

#include "authhandler.h"

#include <QVariantMap>
#include <QTextStream>
#include <QFile>
#include <QStringList>
#include <QDebug>
#include <QUrl>

#include <Accounts/Manager>

#include <oauth2data.h>

#include <ProfileEngineDefs.h>
#include <LogMacros.h>

#include <sailfishkeyprovider.h>

using namespace Accounts;
using namespace SignOn;

const QString RESPONSE_TYPE         ("ResponseType");
const QString SCOPE                 ("Scope");
const QString AUTH_PATH             ("AuthPath");
const QString TOKEN_PATH            ("TokenPath");
const QString REDIRECT_URI          ("RedirectUri");
const QString HOST                  ("Host");
const QString SLASH                 ("/");
const QString AUTH                  ("auth");
const QString AUTH_METHOD           ("method");
const QString MECHANISM             ("mechanism");

AuthHandler::AuthHandler(Accounts::Manager *manager, const quint32 accountId, const QString &remoteDatabasePath, QObject *parent)
    : QObject(parent)
    , mAccount (manager->account(accountId))
    , mRemoteDatabasePath(remoteDatabasePath)
{
}

bool AuthHandler::init()
{
    if (mAccount == NULL) {
        LOG_DEBUG("Account is not created... Cannot authenticate");
        return false;
    }

    QVariant val = QVariant::String;
    mAccount->value(AUTH + SLASH + AUTH_METHOD, val);
    mMethod = val.toString();
    mAccount->value(AUTH + SLASH + MECHANISM, val);
    mMechanism = val.toString();

    qint32 cId = mAccount->credentialsId();
    if (cId == 0) {
        QMap<MethodName,MechanismsList> methods;
        methods.insert(mMethod, QStringList()  << mMechanism);
        IdentityInfo *info = new IdentityInfo(mAccount->displayName(), "", methods);
        QUrl url(mRemoteDatabasePath);
        info->setRealms(QStringList() << url.host());
        info->setType(IdentityInfo::Application);

        mIdentity = Identity::newIdentity(*info);
        if (!mIdentity) {
            LOG_DEBUG("Identity could not be created for account");
            return false;
        }

        connect(mIdentity, SIGNAL(credentialsStored(const quint32)),
                this, SLOT(credentialsStored(const quint32)));
        connect(mIdentity, SIGNAL(error(const SignOn::Error &)),
                this, SLOT(error(const SignOn::Error &)));

        mIdentity->storeCredentials();
    } else {
        mIdentity = Identity::existingIdentity(cId);
    }

    mSession = mIdentity->createSession(mMethod.toLatin1());
    if (!mSession) {
        LOG_DEBUG("Signon session could not be created with method" << mMethod);
        return false;
    }

    connect(mSession, SIGNAL(response(const SignOn::SessionData &)),
            this, SLOT(sessionResponse(const SignOn::SessionData &)));

    connect(mSession, SIGNAL(error(const SignOn::Error &)),
            this, SLOT(error(const SignOn::Error &)));

    return true;
}

void AuthHandler::sessionResponse(const SessionData &sessionData)
{
    if (mMethod.compare("password", Qt::CaseInsensitive) == 0) {
        QStringList propertyList = sessionData.propertyNames();
        Q_FOREACH (const QString &propertyName, propertyList) {
            LOG_DEBUG(propertyName << sessionData.getProperty(propertyName).toString());
            if (propertyName.compare("username", Qt::CaseInsensitive) == 0) {
                mUsername = sessionData.getProperty( propertyName ).toString();
            } else if (propertyName.compare("secret", Qt::CaseInsensitive) == 0) {
                mPassword = sessionData.getProperty( propertyName ).toString();
            }
        }
    } else if (mMethod.compare("oauth2", Qt::CaseInsensitive) == 0) {
        OAuth2PluginNS::OAuth2PluginTokenData response = sessionData.data<OAuth2PluginNS::OAuth2PluginTokenData>();
        mToken = response.AccessToken();
    } else {
        LOG_FATAL("Unsupported Mechanism requested....................");
        emit failed();
        return;
    }

    LOG_DEBUG("Authenticated !!!");

    emit success();
}

const QString AuthHandler::token()
{
    return mToken;
}

const QString AuthHandler::username()
{
    return mUsername;
}

const QString AuthHandler::password()
{
    return mPassword;
}

void AuthHandler::authenticate()
{
    QVariant val = QVariant::String;
    mAccount->value(AUTH + SLASH + AUTH_METHOD, val);
    QString method = val.toString();

    if (mMethod.compare("password", Qt::CaseInsensitive) == 0) {
        SignOn::SessionData data;
        mSession->process(data, mMechanism);
    } else if (mMethod.compare("oauth2", Qt::CaseInsensitive) == 0) {
        mAccount->value(AUTH + SLASH + method + SLASH + mMechanism + SLASH + HOST, val);
        QString host = val.toString();
        mAccount->value(AUTH + SLASH + method + SLASH + mMechanism + SLASH + AUTH_PATH, val);
        QString auth_url = val.toString();
        mAccount->value(AUTH + SLASH + method + SLASH + mMechanism + SLASH + TOKEN_PATH, val);
        QString token_url = val.toString();
        mAccount->value(AUTH + SLASH + method + SLASH + mMechanism + SLASH + REDIRECT_URI, val);
        QString redirect_uri = val.toString();
        mAccount->value(AUTH + SLASH + method + SLASH + mMechanism + SLASH + RESPONSE_TYPE, val);
        QString response_type = val.toString();

        QStringList scope;
        QVariant val1 = QVariant::StringList;
        mAccount->value(AUTH + SLASH + method + SLASH + mMechanism + SLASH + SCOPE, val1);
        scope.append(val1.toStringList());
        qDebug() << scope << "\n";

        QByteArray providerName = mAccount->providerName().toLatin1();
        QString clientId = storedKeyValue(providerName.constData(), "caldav", "client_id");
        QString clientSecret = storedKeyValue(providerName.constData(), "caldav", "client_secret");
        OAuth2PluginNS::OAuth2PluginData data;
        data.setClientId(clientId);
        data.setClientSecret(clientSecret);
        data.setHost(host);
        data.setAuthPath(auth_url);
        data.setTokenPath(token_url);
        data.setRedirectUri(redirect_uri);
        data.setResponseType(QStringList() << response_type);
        data.setScope(scope);

        mSession->process(data, mMechanism);
    } else {
        LOG_FATAL("Unsupported Method requested....................");
        emit failed();
    }
}

void AuthHandler::credentialsStored(const quint32 id)
{
    mAccount->setCredentialsId(id);
    mAccount->sync();
}

void AuthHandler::error(const SignOn::Error & error)
{
    printf(error.message().toStdString().c_str());
    qDebug() << error.message();

    emit failed();
}

QString AuthHandler::storedKeyValue(const char *provider, const char *service, const char *keyName)
{
    char *storedKey = NULL;
    QString retn;

    int success = SailfishKeyProvider_storedKey(provider, service, keyName, &storedKey);
    if (success == 0 && storedKey != NULL && strlen(storedKey) != 0) {
        retn = QLatin1String(storedKey);
    }

    if (storedKey) {
        free(storedKey);
    }

    return retn;
}
