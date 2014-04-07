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
class NotebookSyncDatabase;

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
                               QNetworkAccessManager *networkAccessManager,
                               Settings *settings,
                               const QString &calendarServerPath,
                               QObject *parent = 0);
    ~NotebookSyncAgent();

    void startSlowSync(const QString &notebookName,
                       const QString &notebookAccountId,
                       const QString &pluginName,
                       const QString &syncProfile,
                       const QString &color);
    void startQuickSync(mKCal::Notebook::Ptr notebook,
                        const QDateTime &changesSinceDate,
                        const KCalCore::Incidence::List &allCalendarIncidences);

    void abort();
    bool applyRemoteChanges();
    void finalize();

    bool isFinished() const;

signals:
    void finished(int minorErrorCode, const QString &message);

private slots:
    void reportRequestFinished();
    void nonReportRequestFinished();

private:
    void clearRequests();
    void emitFinished(int minorErrorCode, const QString &message);

    void fetchRemoteChanges();
    bool updateIncidences(const QList<Reader::CalendarResource> &resources);
    bool deleteIncidences(const QStringList &incidenceUids);

    void sendLocalChanges();
    bool loadLocalChanges(const QDateTime &fromDate,
                          KCalCore::Incidence::List *inserted,
                          KCalCore::Incidence::List *modified,
                          KCalCore::Incidence::List *deleted);
    bool discardLastSyncRemoteAdditions(KCalCore::Incidence::List *sourceList);
    bool discardLastSyncRemoteModifications(KCalCore::Incidence::List *sourceList);
    bool discardLastSyncRemoteDeletions(KCalCore::Incidence::List *sourceList);
    int removeCommonIncidences(KCalCore::Incidence::List *inserted,
                               KCalCore::Incidence::List *deleted);

    KCalCore::Incidence::List mCalendarIncidencesBeforeSync;
    QList<Reader::CalendarResource> mReceivedCalendarResources;
    QStringList mNewRemoteIncidenceIds;
    QHash<QString,QString> mModifiedIncidenceICalData;
    QStringList mIncidenceUidsToDelete;
    QSet<Request *> mRequests;
    QNetworkAccessManager* mNAManager;
    NotebookSyncDatabase* mSyncDatabase;
    Settings *mSettings;
    mKCal::ExtendedCalendar::Ptr mCalendar;
    mKCal::ExtendedStorage::Ptr mStorage;
    mKCal::Notebook::Ptr mNotebook;
    QDateTime mChangesSinceDate;
    QString mServerPath;
    SyncMode mSyncMode;
    bool mFinished;
};

#endif // NOTEBOOKSYNCAGENT_P_H
