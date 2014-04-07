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
#ifndef NOTEBOOKSYNCDATABASE_H
#define NOTEBOOKSYNCDATABASE_H

#include <QSqlDatabase>
#include <QStringList>

class NotebookSyncDatabase
{
public:
    ~NotebookSyncDatabase();

    bool isOpen() const;

    QStringList lastSyncAdditions(bool *ok);    // const?
    QHash<QString, QString> lastSyncModifications(bool *ok);
    QStringList lastSyncDeletions(bool *ok);

    bool writeLastSyncAdditions(const QStringList &incidenceUids);
    bool writeLastSyncModifications(const QHash<QString, QString> &incidenceDetails);
    bool writeLastSyncDeletions(const QStringList &incidenceUids);

    bool removeLastSyncAdditions();
    bool removeLastSyncModifications();
    bool removeLastSyncDeletions();

    static NotebookSyncDatabase *open(const QString &notebookUid);

    static bool clearEntriesForNotebook(const QString &notebookUid);

private:
    NotebookSyncDatabase(const QString &notebookUid, const QSqlDatabase &db);
    bool removeEntries(const QString &table);

    QString mNotebookUid;
    QSqlDatabase mDatabase;
};

#endif //NOTEBOOKSYNCDATABASE_H
