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

#ifndef CALDAVCLIENT_H
#define CALDAVCLIENT_H

#include "buteo-caldav-plugin.h"
#include "authhandler.h"
#include "settings.h"

#include <QList>
#include <QPair>
#include <QtNetwork/QNetworkAccessManager>

#include <ClientPlugin.h>
#include <SyncResults.h>
#include <SyncCommonDefs.h>

#include <Accounts/Manager>

class BUTEOCALDAVPLUGINSHARED_EXPORT CalDavClient : public Buteo::ClientPlugin
{
    Q_OBJECT

public:
    CalDavClient(const QString &aPluginName,
                 const Buteo::SyncProfile &aProfile,
                 Buteo::PluginCbInterface *aCbInterface);
    virtual ~CalDavClient();

    virtual bool init();
    virtual bool uninit();
    virtual bool startSync();
    virtual void abortSync(Sync::SyncStatus aStatus = Sync::SYNC_ABORTED);
    virtual Buteo::SyncResults getSyncResults() const;
    virtual bool cleanUp();

Q_SIGNALS:
    void stateChanged(Sync::SyncProgressDetail progress);
    void syncFinished(Sync::SyncStatus);

public Q_SLOTS:
    virtual void connectivityStateChanged(Sync::ConnectivityType aType, bool aState);
    bool start();
    void authenticationError();
    void receiveStateChanged(Sync::SyncProgressDetail aState);
    void receiveSyncFinished(Sync::SyncStatus);
    void requestFinished();

private:
    void startSlowSync();
    void startQuickSync();
    const QDateTime lastSyncTime();
    const QString authToken();
    bool abort(Sync::SyncStatus status);
    bool initConfig();
    void closeConfig();
    Buteo::SyncProfile::SyncDirection syncDirection();
    Buteo::SyncProfile::ConflictResolutionPolicy conflictResolutionPolicy();

    QNetworkAccessManager*      mNAManager;
    Accounts::Manager*          mManager;
    AuthHandler*                mAuth;
    Buteo::SyncResults          mResults;
    quint32                     mAccountId;
    Sync::SyncStatus            mSyncStatus;
    Buteo::SyncProfile::SyncDirection mSyncDirection;
    Buteo::SyncProfile::ConflictResolutionPolicy mConflictResPolicy;
    Settings                    mSettings;
    bool                        mSlowSync;
};

/*! \brief Creates CalDav client plugin
 *
 * @param aPluginName Name of this client plugin
 * @param aProfile Profile to use
 * @param aCbInterface Pointer to the callback interface
 * @return Client plugin on success, otherwise NULL
 */
extern "C" CalDavClient* createPlugin(const QString &aPluginName,
                                      const Buteo::SyncProfile &aProfile,
                                      Buteo::PluginCbInterface *aCbInterface);

/*! \brief Destroys CalDav client plugin
 *
 * @param aClient CalDav client plugin instance to destroy
 */
extern "C" void destroyPlugin(CalDavClient *aClient);

#endif // CALDAVCLIENT_H
