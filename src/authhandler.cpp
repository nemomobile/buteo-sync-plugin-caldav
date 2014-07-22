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

#include "authhandler.h"

#include <QVariantMap>
#include <QTextStream>
#include <QFile>
#include <QStringList>
#include <QDebug>
#include <QUrl>

#include <Accounts/Manager>
#include <Accounts/AuthData>
#include <Accounts/AccountService>
#include <SignOn/SessionData>

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

AuthHandler::AuthHandler(Accounts::Manager *manager, const quint32 accountId, const QString &accountService, QObject *parent)
    : QObject(parent)
    , mAccountManager(manager)
    , mAccount(manager->account(accountId))
    , m_accountService(accountService)
{
}

bool AuthHandler::init()
{
    FUNCTION_CALL_TRACE;

    if (mAccount == NULL) {
        LOG_DEBUG("Invalid account");
        return false;
    }

    QVariant val = QVariant::String;

    Accounts::Service srv = mAccountManager->service(m_accountService);
    if (!srv.isValid()) {
        LOG_WARNING("Cannot select service:" << m_accountService);
        return false;
    }
    mAccount->selectService(srv);
    mAccount->value(AUTH + SLASH + AUTH_METHOD, val);
    mMethod = val.toString();
    mAccount->value(AUTH + SLASH + MECHANISM, val);
    mMechanism = val.toString();
    qint32 cId = mAccount->credentialsId();
    mAccount->selectService(Accounts::Service());

    if (cId == 0) {
        LOG_WARNING("Cannot authenticate, no credentials stored for service:" << m_accountService);
        return false;
    }
    mIdentity = Identity::existingIdentity(cId);
    if (!mIdentity) {
        LOG_WARNING("Cannot get existing identity for credentials:" << cId);
        return false;
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
    FUNCTION_CALL_TRACE;

    if (mMethod.compare("password", Qt::CaseInsensitive) == 0) {
        QStringList propertyList = sessionData.propertyNames();
        Q_FOREACH (const QString &propertyName, propertyList) {
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
        LOG_FATAL("Unsupported Mechanism requested!");
        emit failed();
        return;
    }
    LOG_DEBUG("Authenticated!");
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
    FUNCTION_CALL_TRACE;

    QByteArray providerName = mAccount->providerName().toLatin1();

    Accounts::Service srv = mAccountManager->service(m_accountService);
    mAccount->selectService(srv);

    QVariant val = QVariant::String;
    mAccount->value(AUTH + SLASH + AUTH_METHOD, val);
    QString method = val.toString();
    mAccount->selectService(Accounts::Service());

    if (mMethod.compare("password", Qt::CaseInsensitive) == 0) {
        Accounts::AccountService as(mAccount, srv);
        Accounts::AuthData authData(as.authData());
        QVariantMap parameters = authData.parameters();
        parameters.insert("UiPolicy", SignOn::NoUserInteractionPolicy);

        SignOn::SessionData data(parameters);
        mSession->process(data, mMechanism);
    } else if (mMethod.compare("oauth2", Qt::CaseInsensitive) == 0) {
        mAccount->selectService(srv);

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

        mAccount->selectService(Accounts::Service());

        mSession->process(data, mMechanism);
    } else {
        LOG_FATAL("Unsupported Method requested!");
        emit failed();
    }
}

void AuthHandler::error(const SignOn::Error & error)
{
    FUNCTION_CALL_TRACE;
    LOG_DEBUG("Auth error:" << error.message());
    emit failed();
}

QString AuthHandler::storedKeyValue(const char *provider, const char *service, const char *keyName)
{
    FUNCTION_CALL_TRACE;

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
