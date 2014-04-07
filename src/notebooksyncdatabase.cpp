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
#include "notebooksyncdatabase.h"

#include <LogMacros.h>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDebug>

static const char *createLastSyncAdditionsTable =
        "\n CREATE TABLE LastSyncAdditions ("
        "\n incidenceUid TEXT PRIMARY KEY,"
        "\n notebookUid TEXT NOT NULL);";

static const char *createLastSyncModificationsTable =
        "\n CREATE TABLE LastSyncModifications ("
        "\n incidenceUid TEXT PRIMARY KEY,"
        "\n notebookUid TEXT NOT NULL,"
        "\n iCalData TEXT);";

static const char *createLastSyncDeletionsTable =
        "\n CREATE TABLE LastSyncDeletions ("
        "\n incidenceUid TEXT PRIMARY KEY,"
        "\n notebookUid TEXT NOT NULL);";


static bool createDatabase(QSqlDatabase *database)
{
    static const char *createStatements[] = { createLastSyncAdditionsTable, createLastSyncModificationsTable, createLastSyncDeletionsTable };
    static const int createStatementsCount = 3;
    for (int i=0; i<createStatementsCount; ++i) {
        QSqlQuery query(*database);
        if (!query.exec(QLatin1String(createStatements[i]))) {
            LOG_CRITICAL(QString("Database creation failed: %1\n%2")
                    .arg(query.lastError().text())
                    .arg(createStatements[i]));
            return false;
        }
    }
    return true;
}

static QSqlDatabase openDatabase(const QString &databaseName)
{
    QString homeDir = QStandardPaths::standardLocations(QStandardPaths::HomeLocation).value(0);
    if (homeDir.isEmpty()) {
        LOG_CRITICAL("Cannot load database, QStandardPaths::HomeLocation not found");
        return QSqlDatabase();
    }
    QString privilegedDataDir = homeDir + "/.local/share/system/privileged/";
    QDir databaseDir(privilegedDataDir);
    if (!databaseDir.exists()) {
        LOG_CRITICAL("Cannot load database," << privilegedDataDir << " not found");
        return QSqlDatabase();
    }
    databaseDir = privilegedDataDir + "Sync/";
    if (!databaseDir.exists() && !databaseDir.mkpath(QStringLiteral("."))) {
        LOG_CRITICAL("Cannot load database, cannot create database directory:" << databaseDir.path());
        return QSqlDatabase();
    }

    QString databaseFile = databaseDir.absoluteFilePath(databaseName);
    bool databaseExists = QFile::exists(databaseFile);

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), databaseName);
    database.setDatabaseName(databaseFile);
    if (!database.open()) {
        LOG_CRITICAL("Cannot open database" << databaseFile << "error:" << database.lastError().text());
        return QSqlDatabase();
    }
    if (!databaseExists && !createDatabase(&database)) {
        LOG_CRITICAL("Cannot load database, cannot create database at:" << databaseDir.path());
        database.close();
        QFile::remove(databaseFile);
        return QSqlDatabase();
    }
    LOG_DEBUG("Opened database:" << databaseFile);
    return database;
}

static QSqlDatabase notebookSyncDatabase()
{
    static QSqlDatabase database = openDatabase("caldav-sync.db");
    return database;
}


NotebookSyncDatabase::NotebookSyncDatabase(const QString &notebookUid, const QSqlDatabase &db)
    : mNotebookUid(notebookUid)
    , mDatabase(db)
{
}

NotebookSyncDatabase::~NotebookSyncDatabase()
{
}

NotebookSyncDatabase *NotebookSyncDatabase::open(const QString &notebookUid)
{
    return new NotebookSyncDatabase(notebookUid, notebookSyncDatabase());
}

bool NotebookSyncDatabase::isOpen() const
{
    return mDatabase.isOpen();
}

QStringList NotebookSyncDatabase::lastSyncAdditions(bool *ok)
{
    qWarning() << Q_FUNC_INFO << mNotebookUid;

    if (!mDatabase.isOpen()) {
        LOG_CRITICAL("Database is not open!");
        *ok = false;
        return QStringList();
    }
    QStringList ret;
    *ok = false;
    static const QString sql(QStringLiteral("SELECT incidenceUid FROM LastSyncAdditions WHERE notebookUid = '%1'"));
    QSqlQuery query(mDatabase);
    if (!query.exec(sql.arg(mNotebookUid))) {
        LOG_CRITICAL("SQL query failed:" << query.executedQuery() << "Error:" << query.lastError().text());
        return ret;
    }
    while (query.next()) {
        ret << query.value(0).toString();
    }
    *ok = true;
    return ret;
}

QHash<QString, QString> NotebookSyncDatabase::lastSyncModifications(bool *ok)
{
    qWarning() << Q_FUNC_INFO << mNotebookUid;
    if (!mDatabase.isOpen()) {
        LOG_CRITICAL("Database is not open!");
        *ok = false;
        return QHash<QString, QString>();
    }
    QHash<QString, QString> ret;
    *ok = false;
    static const QString sql(QStringLiteral("SELECT incidenceUid,iCalData FROM LastSyncModifications WHERE notebookUid = '%1'"));
    QSqlQuery query(mDatabase);
    if (!query.exec(sql.arg(mNotebookUid))) {
        LOG_CRITICAL("SQL query failed:" << query.executedQuery() << "Error:" << query.lastError().text());
        return ret;
    }
    while (query.next()) {
        ret.insert(query.value(0).toString(), query.value(1).toString());
    }
    *ok = true;
    return ret;
}

QStringList NotebookSyncDatabase::lastSyncDeletions(bool *ok)
{
    if (!mDatabase.isOpen()) {
        LOG_CRITICAL("Database is not open!");
        *ok = false;
        return QStringList();
    }
    QStringList ret;
    *ok = false;
    static const QString sql(QStringLiteral("SELECT incidenceUid FROM LastSyncDeletions WHERE notebookUid = '%1'"));
    QSqlQuery query(mDatabase);
    if (!query.exec(sql.arg(mNotebookUid))) {
        LOG_CRITICAL("SQL query failed:" << query.executedQuery() << "Error:" << query.lastError().text());
        return ret;
    }
    while (query.next()) {
        ret << query.value(0).toString();
    }
    *ok = true;
    return ret;
}

bool NotebookSyncDatabase::writeLastSyncAdditions(const QStringList &incidenceUids)
{
    qWarning() << Q_FUNC_INFO << mNotebookUid;
    if (!mDatabase.isOpen()) {
        LOG_CRITICAL("Database is not open!");
        return false;
    }
    static const QString sql(QStringLiteral("INSERT INTO LastSyncAdditions (incidenceUid, notebookUid) VALUES (:incidenceUid, :notebookUid)"));
    QSqlQuery query(mDatabase);
    query.prepare(sql);

    QVariantList incidenceUidsVariants;
    QVariantList notebookUidsVariants;
    for (int i=0; i<incidenceUids.count(); i++) {
        incidenceUidsVariants << incidenceUids[i];
        notebookUidsVariants << mNotebookUid;
    }
    query.addBindValue(incidenceUidsVariants);
    query.addBindValue(notebookUidsVariants);

    if (!query.execBatch()) {
        LOG_CRITICAL("SQL query failed:" << query.executedQuery() << "Error:" << query.lastError().text());
        return false;
    }
    return true;
}

bool NotebookSyncDatabase::writeLastSyncModifications(const QHash<QString, QString> &incidenceDetails)
{
    if (!mDatabase.isOpen()) {
        LOG_CRITICAL("Database is not open!");
        return false;
    }
    static const QString sql(QStringLiteral("INSERT INTO LastSyncModifications (incidenceUid, notebookUid, iCalData) VALUES (:incidenceUid, :notebookUid, :iCalData)"));
    QSqlQuery query(mDatabase);
    query.prepare(sql);

    QVariantList incidenceUidsVariants;
    QVariantList notebookUidsVariants;
    QVariantList incidenceICalVariants;
    Q_FOREACH (const QString &incidenceUid, incidenceDetails.keys()) {
        incidenceUidsVariants << incidenceUid;
        notebookUidsVariants << mNotebookUid;
        incidenceICalVariants << incidenceDetails[incidenceUid];
    }
    query.addBindValue(incidenceUidsVariants);
    query.addBindValue(notebookUidsVariants);
    query.addBindValue(incidenceICalVariants);

    if (!query.execBatch()) {
        LOG_CRITICAL("SQL query failed:" << query.executedQuery() << "Error:" << query.lastError().text());
        return false;
    }
    return true;
}

bool NotebookSyncDatabase::writeLastSyncDeletions(const QStringList &incidenceUids)
{
    if (!mDatabase.isOpen()) {
        LOG_CRITICAL("Database is not open!");
        return false;
    }
    static const QString sql(QStringLiteral("INSERT INTO LastSyncDeletions (incidenceUid, notebookUid) VALUES (:incidenceUid, :notebookUid)"));
    QSqlQuery query(mDatabase);
    query.prepare(sql);

    QVariantList incidenceUidsVariants;
    QVariantList notebookUidsVariants;
    for (int i=0; i<incidenceUids.count(); i++) {
        incidenceUidsVariants << incidenceUids[i];
        notebookUidsVariants << mNotebookUid;
    }
    query.addBindValue(incidenceUidsVariants);
    query.addBindValue(notebookUidsVariants);

    if (!query.execBatch()) {
        LOG_CRITICAL("SQL query failed:" << query.executedQuery() << "Error:" << query.lastError().text());
        return false;
    }
    return true;
}

bool NotebookSyncDatabase::removeLastSyncAdditions()
{
    return removeEntries(QStringLiteral("LastSyncAdditions"));
}

bool NotebookSyncDatabase::removeLastSyncModifications()
{
    return removeEntries(QStringLiteral("LastSyncModifications"));
}

bool NotebookSyncDatabase::removeLastSyncDeletions()
{
    return removeEntries(QStringLiteral("LastSyncDeletions"));
}

bool NotebookSyncDatabase::clearEntriesForNotebook(const QString &notebookUid)
{
    LOG_DEBUG("clearEntriesForNotebook:" << notebookUid);

    NotebookSyncDatabase *db = new NotebookSyncDatabase(notebookUid, notebookSyncDatabase());
    db->removeLastSyncAdditions();
    db->removeLastSyncModifications();
    db->removeLastSyncDeletions();
    delete db;
}

bool NotebookSyncDatabase::removeEntries(const QString &table)
{
    static const QString sql(QStringLiteral("DELETE FROM %1 WHERE notebookUid = '%2'"));
    QSqlQuery query(mDatabase);
    if (!query.exec(sql.arg(table).arg(mNotebookUid))) {
        LOG_CRITICAL("SQL query failed:" << query.executedQuery() << "Error:" << query.lastError().text());
        return false;
    }
    return true;
}
