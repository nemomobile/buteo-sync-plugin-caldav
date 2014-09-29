/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2014 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Bea Lam <bea.lam@jollamobile.com>
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
#ifndef NOTEBOOKSYNCAGENT_P_H
#define NOTEBOOKSYNCAGENT_P_H

#include "reader.h"

#include <extendedcalendar.h>
#include <extendedstorage.h>

#include <QDateTime>

class QNetworkAccessManager;
class Request;
class Settings;
class CalDavCalendarDatabase;

class NotebookSyncAgent : public QObject
{
    Q_OBJECT
public:
    enum SyncMode {
        NoSyncMode,
        SlowSync,   // download everything
        QuickSync   // updates only
    };

    explicit NotebookSyncAgent(mKCal::ExtendedCalendar::Ptr calendar,
                               mKCal::ExtendedStorage::Ptr storage,
                               CalDavCalendarDatabase *database,
                               QNetworkAccessManager *networkAccessManager,
                               Settings *settings,
                               const QString &calendarServerPath,
                               QObject *parent = 0);
    ~NotebookSyncAgent();

    void startSlowSync(const QString &notebookName,
                       const QString &notebookAccountId,
                       const QString &pluginName,
                       const QString &syncProfile,
                       const QString &color,
                       const QDateTime &fromDateTime,
                       const QDateTime &toDateTime);

    void startQuickSync(mKCal::Notebook::Ptr notebook,
                        const QDateTime &changesSinceDate,
                        const KCalCore::Incidence::List &allCalendarIncidences,
                        const QDateTime &fromDateTime,
                        const QDateTime &toDateTime);

    void abort();
    bool applyRemoteChanges();
    void finalize();

    bool isFinished() const;

signals:
    void finished(int minorErrorCode, const QString &message);

private slots:
    void reportRequestFinished();
    void additionalReportRequestFinished();
    void nonReportRequestFinished();
    void processETags();
private:
    void sendReportRequest();
    void clearRequests();
    void emitFinished(int minorErrorCode, const QString &message);

    void fetchRemoteChanges(const QDateTime &fromDateTime, const QDateTime &toDateTime);
    bool updateIncidences(const QList<Reader::CalendarResource> &resources);
    bool deleteIncidences(const QStringList &incidenceUids);

    void sendLocalChanges();
    void finalizeSendingLocalChanges();
    bool loadLocalChanges(const QDateTime &fromDate,
                          KCalCore::Incidence::List *inserted,
                          KCalCore::Incidence::List *modified,
                          QStringList *deleted);
    bool discardRemoteChanges(KCalCore::Incidence::List *localInserted,
                              KCalCore::Incidence::List *localModified,
                              QStringList *localDeleted);
    int removeCommonIncidences(KCalCore::Incidence::List *inserted,
                               QStringList *deleted);

    KCalCore::Incidence::List mCalendarIncidencesBeforeSync;
    KCalCore::Incidence::List mStorageIncidenceList;
    KCalCore::Incidence::List mLocallyInsertedIncidences;
    KCalCore::Incidence::List mLocallyModifiedIncidences;
    QSet<QString> mStorageUids;
    QSet<QString> mLocalDeletedUids;
    QList<Reader::CalendarResource> mReceivedCalendarResources;
    QSet<QString> mReceivedUids;
    QStringList mNewRemoteIncidenceIds;
    QHash<QString,QString> mModifiedIncidenceICalData;
    QHash<QString,QString> mLocalETags;
    QHash<QString,QString> mUpdatedETags;
    QStringList mIncidenceUidsToDelete;
    QSet<Request *> mRequests;
    QNetworkAccessManager* mNAManager;
    CalDavCalendarDatabase* mDatabase;
    Settings *mSettings;
    mKCal::ExtendedCalendar::Ptr mCalendar;
    mKCal::ExtendedStorage::Ptr mStorage;
    mKCal::Notebook::Ptr mNotebook;
    QDateTime mFromDateTime;
    QDateTime mToDateTime;
    QDateTime mChangesSinceDate;
    QString mServerPath;
    SyncMode mSyncMode;
    bool mFinished;
    bool mRetriedReport;
};

#endif // NOTEBOOKSYNCAGENT_P_H
