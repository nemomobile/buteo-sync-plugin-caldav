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

#ifndef GCALENDARCLIENT_H
#define GCALENDARCLIENT_H

#include "buteo-caldav-plugin.h"
#include "oauthhandler.h"
#include "settings.h"

#include <QList>
#include <QPair>
#include <QtNetwork/QNetworkAccessManager>

#include <ClientPlugin.h>
#include <SyncResults.h>
#include <SyncCommonDefs.h>

class BUTEOCALDAVPLUGINSHARED_EXPORT CalDavClient : Buteo::ClientPlugin
{
    public:
        Q_OBJECT
    public:

        /*! \brief Constructor
         *
         * @param aPluginName Name of this client plugin
         * @param aProfile Sync profile
         * @param aCbInterface Pointer to the callback interface
         */
        CalDavClient( const QString& aPluginName,
                      const Buteo::SyncProfile& aProfile,
                      Buteo::PluginCbInterface *aCbInterface );

        /*! \brief Destructor
         *
         * Call uninit before destroying the object.
         */
        virtual ~CalDavClient();

        //! @see SyncPluginBase::init
        virtual bool init();

        //! @see SyncPluginBase::uninit
        virtual bool uninit();

        //! @see ClientPlugin::startSync
        virtual bool startSync();

        //! @see SyncPluginBase::abortSync
        virtual void abortSync(Sync::SyncStatus aStatus = Sync::SYNC_ABORTED);

        //! @see SyncPluginBase::getSyncResults
        virtual Buteo::SyncResults getSyncResults() const;

        //! @see SyncPluginBase::cleanUp
        virtual bool cleanUp();

    signals:
        void stateChanged (Sync::SyncProgressDetail progress);

        void syncFinished (Sync::SyncStatus);

    public slots:

        //! @see SyncPluginBase::connectivityStateChanged
        virtual void connectivityStateChanged( Sync::ConnectivityType aType, bool aState );

        bool start ();

        void authenticationError();

        void receiveStateChanged(Sync::SyncProgressDetail aState);

        void receiveSyncFinished(Sync::SyncStatus);

        void requestFinished();

    private:

        void startSlowSync();

        void startQuickSync();

        const QDateTime lastSyncTime();

        const QString authToken ();

        bool abort (Sync::SyncStatus status);

        bool initConfig ();

        void closeConfig ();

        Buteo::SyncProfile::SyncDirection syncDirection();

        Buteo::SyncProfile::ConflictResolutionPolicy conflictResolutionPolicy();

        OAuthHandler*               mOAuth;

        bool                        mSlowSync;

        Buteo::SyncResults          mResults;

        quint32                     mAccountId;

        Sync::SyncStatus            mSyncStatus;

        Buteo::SyncProfile::SyncDirection mSyncDirection;

        Buteo::SyncProfile::ConflictResolutionPolicy mConflictResPolicy;

        QNetworkAccessManager       *mNAManager;

        Settings                    mSettings;
};

/*! \brief Creates CalDav client plugin
 *
 * @param aPluginName Name of this client plugin
 * @param aProfile Profile to use
 * @param aCbInterface Pointer to the callback interface
 * @return Client plugin on success, otherwise NULL
 */
extern "C" CalDavClient* createPlugin( const QString& aPluginName,
                                       const Buteo::SyncProfile& aProfile,
                                       Buteo::PluginCbInterface *aCbInterface );

/*! \brief Destroys CalDav client plugin
 *
 * @param aClient CalDav client plugin instance to destroy
 */
extern "C" void destroyPlugin( CalDavClient *aClient );

#endif // GCALENDARCLIENT_H
