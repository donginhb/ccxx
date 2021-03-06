#include "cxdatabase_sqlite.h"

#include "cxstring.h"
#include "cxcontainer.h"
#include "cxapplication.h"
#include "cxfile.h"
#include "cxinterinfo.h"

using namespace std;

//********************* ********************* ********************* *********************
//********************* sqlite begin **********************************************************
//********************* ********************* ********************* *********************

#include "sqlite3/sqlite3.h"

bool g_bHasRegistDatabaseConstructorSqlite = CxDatabaseSqliteFactory::registDatabaseConstructor();

struct SqliteColumnDesc
{
    std::string columnName;
    int ftype;
    int fsize;
};

class CxDatabaseSqlite : public CxDatabase
{
private:
    sqlite3* _db;

public:
    class CursorSqlite : public CursorBase
    {
        CursorSqlite(int iPrefetchArraySize=100):
            CursorBase(CursorTypeSqlite, iPrefetchArraySize),
            _stmt(NULL),
            _rc(0),
            _ncols(0){}
        virtual ~CursorSqlite(){
            if (_stmt)
            {
                sqlite3_finalize(_stmt);
                _stmt = NULL;
            }
        }

    protected:
        sqlite3_stmt * _stmt;
        int _rc;
        int _ncols;
        vector<SqliteColumnDesc> _columnDescs;

        friend class CxDatabaseSqlite;
    };

public:
    CxDatabaseSqlite()
    {
        _db = NULL;
    }

    ~CxDatabaseSqlite()
    {
        if (_db)
        {
            closeDatabaseImpl();
        }
    }

protected:
    bool
    openDatabaseImpl(const std::string& sDatabase,
                     const std::string& sDatabaseType,
                     bool bCreate,
                     const std::map<std::string, std::string>* oParams)
    {
        if (!_db)
        {
            cxDebug() << "OpenDatabase.sqlite: open-begin: " << sDatabase;
            if (sDatabase.empty())
                return false;
            string sDatabase2 = sDatabase;
            if (!(CxFileSystem::hasRootPath(sDatabase) && CxFileSystem::isExist(sDatabase)))
            {
                sDatabase2 = CxFileSystem::mergeFilePath(CxDatabaseManager::getDefaultDatabasePath(), sDatabase);
            }
            int flags = bCreate ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READWRITE;
            int rc = sqlite3_open_v2(sDatabase2.c_str(), &_db, flags, NULL);
            if (rc)
            {
                fprintf(stderr, "can not open database :%s", sqlite3_errmsg(_db));
                sqlite3_close(_db);
                _db = NULL;
                return false;
            }
            cxDebug() << "OpenDatabase.sqlite: open-end: " << (_db != NULL);
        }
        return true;
    }

    void
    closeDatabaseImpl()
    {
        if (_db)
        {
            sqlite3_close(_db);
            _db = NULL;
        }
    }

    bool
    isOpenImpl() const
    {
        return _db;
    }

    bool
    createTableImpl(const std::string& sTableName, const std::map<std::string, std::string>& columns)
    {
        string sSql = CxString::format("CREATE TABLE [%s] (", sTableName.c_str());
        int iCount = 0;
        for (std::map<string, string>::const_iterator it = columns.begin(); it != columns.end(); ++it)
        {
            string sColumn = "[" + it->first + "] " + it->second + " ,";
            sSql += sColumn;
            ++iCount;
        }
        if (iCount > 0)
        {
            char* pch = (char*) sSql.data();
            pch[sSql.size() - 1] = ')';
            return execSql(sSql);
        }
        return true;
    }

    bool
    alterPrimaryKeyAddImpl(const std::string& sTableName, const std::vector<std::string>& columns)
    {
        string sSql = CxString::format("ALTER TABLE ADD CONSTRAINT %s PRIMARY KEY (", sTableName.c_str());
        int iCount = 0;
        for (size_t i = 0; i < columns.size(); ++i)
        {
            sSql += columns.at(i) + " ,";
            ++iCount;
        }
        if (iCount > 0)
        {
            char* pch = (char*) sSql.data();
            pch[sSql.size() - 1] = ')';
            return execSql(sSql);
        }
        return false;
    }

    int
    loadTableImpl(const std::string& sTableName,
                  std::vector<std::vector<std::string> >& rows,
                  std::vector<std::string>* oColumnNames = NULL,
                  int iMaxRowCount = -1)
    {
        string sSql = "SELECT ";
        if (oColumnNames && oColumnNames->size() > 0)
        {
            int iCount = 0;
            for (size_t i = 0; i < oColumnNames->size(); ++i)
            {
                sSql += oColumnNames->at(i) + " ,";
                ++iCount;
            }
            sSql.resize(sSql.size() - 1);
        }
        else
        {
            sSql += "* ";
        }
        sSql += "FROM " + sTableName;

        return loadSqlImpl(sSql, rows, oColumnNames);
    }

    int
    saveTableImpl(const std::string& sTableName,
                  const std::vector<std::string>& columnNames,
                  const std::vector<std::vector<std::string> >& rows,
                  const std::vector<int>& columnTypes = std::vector<int>(),
                  bool bTransaction = true)
    {
        if (!_db)
        {
            string sErrorString = "error : db is not open sql!";
            setLastError(-1, sErrorString);
            return -1;
        }
        string sSql = CxString::format("INSERT INTO [%s] ", sTableName.c_str());
        string sColumnes = "(";
        string sValues = "(";
        int iCount = 0;
        for (size_t i = 0; i < columnNames.size(); ++i)
        {
            sColumnes += columnNames.at(i) + " ,";
            sValues += "? ,";
            ++iCount;
        }
        if (iCount > 0)
        {
            char* pch = (char*) sColumnes.data();
            pch[sColumnes.size() - 1] = ')';
            pch = (char*) sValues.data();
            pch[sValues.size() - 1] = ')';
        }
        sSql = sSql + sColumnes + " VALUES " + sValues;

        if (bTransaction)
            if (!beginTransaction())
                return -1;

        sqlite3_stmt* stmt;
        const char* tail;
        int rc = sqlite3_prepare(_db, sSql.c_str(), sSql.size(), &stmt, &tail);
        if (rc != SQLITE_OK)
        {
            // set LastError
            string sErrorString = CxString::format("sql exec error: %s", sqlite3_errmsg(_db));
            setLastError(rc, sErrorString);
            // rollback
            if (bTransaction)
                rollbackTransaction();
            return -1;
        }

        for (size_t i = 0; i < rows.size(); ++i)
        {
            rc = sqlite3_reset(stmt);
            if (rc != SQLITE_OK)
            {
                continue;
            }
            const vector<string>& row = rows.at(i);
            for (size_t j = 0; j < row.size(); ++j)
            {
                const string& sValue = row.at(j);
                sqlite3_bind_text(stmt, j + 1, sValue.c_str(), sValue.size(), 0);
            }
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_OK && rc != SQLITE_DONE)
            {
                // set LastError
                string sErrorString = CxString::format("sql exec error: %s", sqlite3_errmsg(_db));
                setLastError(rc, sErrorString);
                // roll back
                if (bTransaction)
                    rollbackTransaction();
                return i;
            }
        }
        sqlite3_finalize(stmt);
        // commit
        if (bTransaction)
            commitTransaction();
        return rows.size();
    }

    int
    saveTableImpl(const std::string& sSql,
                  const std::vector<std::vector<std::string> >& rows,
                  const std::vector<int>* oColumnTypes,
                  bool bTransaction = false)
    {
        if (!_db)
        {
            string sErrorString = "error : db is not open sql!";
            setLastError(-1, sErrorString);
            return -1;
        }

        if (bTransaction)
            if (!beginTransaction())
                return -1;

        sqlite3_stmt* stmt;
        const char* tail;
        int rc = sqlite3_prepare(_db, sSql.c_str(), sSql.size(), &stmt, &tail);
        if (rc != SQLITE_OK)
        {
            string sErrorString = CxString::format("sql exec error: %s", sqlite3_errmsg(_db));
            setLastError(rc, sErrorString);
            // roll back
            if (bTransaction)
                rollbackTransaction();
            return -1;
        }
        for (size_t i = 0; i < rows.size(); ++i)
        {
            rc = sqlite3_reset(stmt);
            if (rc != SQLITE_OK)
            {
                continue;
            }
            const vector<string>& row = rows.at(i);
            for (size_t j = 0; j < row.size(); ++j)
            {
                const string& sValue = row.at(j);
                sqlite3_bind_text(stmt, j + 1, sValue.c_str(), sValue.size(), 0);
            }
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_OK && rc != SQLITE_DONE)
            {
                string sErrorString = CxString::format("sql exec error: %s", sqlite3_errmsg(_db));
                setLastError(rc, sErrorString);
                // roll back
                if (bTransaction)
                    rollbackTransaction();
                return i;
            }
        }
        sqlite3_finalize(stmt);
        // commit
        if (bTransaction)
            commitTransaction();
        return rows.size();
    }

    int
    updateTableImpl(const std::string& sTableName,
                    const std::vector<std::string>& columnNames,
                    const std::vector<std::vector<std::string> >& rows,
                    const std::vector<int>& columnTypes = std::vector<int>(),
                    bool bTransaction = true)
    {
        if (!_db)
        {
            string sErrorString = "error : db is not open sql!";
            setLastError(-1, sErrorString);
            return -1;
        }
        if (columnNames.size() < 3)
        {
            string sErrorString = "error : columnNames size less 3!";
            setLastError(-2, sErrorString);
            return -2;
        }
        string sSql = CxString::format("UPDATE [%s] SET", sTableName.c_str());
        string sSetColumneValues = "(";
        for (size_t i = 0; i < columnNames.size() - 1; ++i)
        {
            sSetColumneValues += columnNames.at(i) + " = ? ,";
        }
        {
            char* pch = (char*) sSetColumneValues.data();
            pch[sSetColumneValues.size() - 1] = ' ';
        }
        sSql = sSql + sSetColumneValues
            + CxString::format(" WHERE [%s] = ? ", columnNames.at(columnNames.size() - 1).c_str());

        // start transaction
        if (bTransaction)
            if (!beginTransaction())
                return -1;

        sqlite3_stmt* stmt;
        const char* tail;
        int rc = sqlite3_prepare(_db, sSql.c_str(), sSql.size(), &stmt, &tail);
        if (rc != SQLITE_OK)
        {
            // set LastError
            string sErrorString = CxString::format("sql exec error: %s", sqlite3_errmsg(_db));
            setLastError(rc, sErrorString);
            // rollback
            if (bTransaction)
                rollbackTransaction();
            return -1;
        }

        for (size_t i = 0; i < rows.size(); ++i)
        {
            rc = sqlite3_reset(stmt);
            if (rc != SQLITE_OK)
            {
                continue;
            }
            const vector<string>& row = rows.at(i);
            for (size_t j = 0; j < row.size(); ++j)
            {
                const string& sValue = row.at(j);
                sqlite3_bind_text(stmt, j + 1, sValue.c_str(), sValue.size(), 0);
            }
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_OK && rc != SQLITE_DONE)
            {
                // set LastError
                string sErrorString = CxString::format("sql exec error: %s", sqlite3_errmsg(_db));
                setLastError(rc, sErrorString);
                // rollback
                if (bTransaction)
                    rollbackTransaction();
                return i;
            }
        }
        sqlite3_finalize(stmt);
        // commit
        if (bTransaction)
            commitTransaction();
        return rows.size();
    }

    int
    execSqlImpl(const string& sSql)
    {
        if (!_db)
        {
            string sErrorString = "error : db is not open sql!";
            setLastError(-1, sErrorString);
            return -1;
        }
        int result = sqlite3_total_changes(_db);
        char* zErr;
        int rc = sqlite3_exec(_db, sSql.c_str(), NULL, NULL, &zErr);
        if (rc != SQLITE_OK)
        {
            string sErrorString;
            if (zErr != NULL)
            {
                sErrorString = CxString::format("sql exec error: %s", zErr);
                sqlite3_free(zErr);
            }
            setLastError(rc, sErrorString);
            return -2;
        }
        return sqlite3_total_changes(_db) - result;
    }

    int
    execSqlListImpl(const std::vector<std::string>& sqlList)
    {
        if (!_db)
        {
            string sErrorString = "error : db is not open sql!";
            setLastError(-1, sErrorString);
            return -1;
        }
        int result = sqlite3_total_changes(_db);
        if (!beginTransaction())
            return -2;
        for (size_t i = 0; i < sqlList.size(); ++i)
        {
            const string& sSql = sqlList.at(i);
            char* zErr;
            int rc = sqlite3_exec(_db, sSql.c_str(), NULL, NULL, &zErr);
            if (rc != SQLITE_OK)
            {
                rollbackTransaction();
                string sErrorString;
                if (zErr != NULL)
                {
                    sErrorString = CxString::format("sql exec error: %s", zErr);
                    sqlite3_free(zErr);
                }
                setLastError(rc, sErrorString);
                return -3;
            }
        }
        if (commitTransaction())
        {
            return sqlite3_total_changes(_db) - result;
        }
        else
        {
            return -4;
        }
    }

    int
    loadSqlImpl(const std::string& sSql,
                std::vector<std::vector<std::string> >& rows,
                std::vector<std::string>* oColumnNames = NULL,
                int iMaxRowCount = -1)
    {
        if (!_db)
        {
            string sErrorString = "error : db is not open sql!";
            setLastError(-1, sErrorString);
            return -1;
        }
        int rc, ncols;
        sqlite3_stmt* stmt;
        const char* tail;
        rc = sqlite3_prepare(_db, sSql.c_str(), sSql.size(), &stmt, &tail);
        if (rc != SQLITE_OK)
        {
            string sErrorString = CxString::format("sql exec error: %s", sqlite3_errmsg(_db));
            setLastError(rc, sErrorString);
            return -1;
        }
        rc = sqlite3_step(stmt);
        ncols = sqlite3_column_count(stmt);
        if (oColumnNames && oColumnNames->empty())
        {
            for (int i = 0; i < ncols; ++i)
            {
                string sColumnName = sqlite3_column_name(stmt, i);
                oColumnNames->push_back(sColumnName);
            }
        }
        while (rc == SQLITE_ROW)
        {
            vector<string> row;
            for (int i = 0; i < ncols; ++i)
            {
                string sValue;
                const unsigned char* pch = sqlite3_column_text(stmt, i);
                if (pch)
                    sValue = string((const char*) pch);
                row.push_back(sValue);
            }
            rows.push_back(row);
            rc = sqlite3_step(stmt);
            if (iMaxRowCount > 0 && rows.size() >= iMaxRowCount)
            {
                break;
            }
        }
        sqlite3_finalize(stmt);
        return rows.size();
    }

    int
    loadSql2Impl(const std::string& sSql,
                 std::vector<std::vector<std::string> >& rows,
                 std::vector<std::string>* oColumnNames = NULL,
                 std::vector<int>* oColumnTypes = NULL,
                 int iMaxRowCount = -1)
    {
        if (!_db)
        {
            string sErrorString = "error : db is not open sql!";
            setLastError(-1, sErrorString);
            return -1;
        }
        int rc, ncols;
        sqlite3_stmt* stmt;
        const char* tail;
        rc = sqlite3_prepare(_db, sSql.c_str(), sSql.size(), &stmt, &tail);
        if (rc != SQLITE_OK)
        {
            string sErrorString = CxString::format("sql exec error: %s", sqlite3_errmsg(_db));
            setLastError(rc, sErrorString);
            return -1;
        }
        rc = sqlite3_step(stmt);
        ncols = sqlite3_column_count(stmt);
        if (oColumnNames)
            oColumnNames->clear();
        if (oColumnTypes)
            oColumnTypes->clear();
        for (int i = 0; i < ncols; ++i)
        {
            if (oColumnTypes)
            {
                int iColumnType = ColumnTypeNone;
                switch (sqlite3_column_type(stmt, i))
                {
                    case SQLITE_INTEGER:iColumnType = ColumnTypeLongint;
                        break;
                    case SQLITE_FLOAT:iColumnType = ColumnTypeDouble;
                        break;
                    case SQLITE_BLOB:iColumnType = ColumnTypeBlob;
                        break;
                    case SQLITE3_TEXT:iColumnType = ColumnTypeString;
                        break;
                    default:iColumnType = ColumnTypeString;
                        break;
                }
                oColumnTypes->push_back(iColumnType);
            }
            if (oColumnNames)
            {
                string sColumnName = sqlite3_column_name(stmt, i);
                oColumnNames->push_back(sColumnName);
            }
        }
        while (rc == SQLITE_ROW)
        {
            vector<string> row;
            for (int i = 0; i < ncols; ++i)
            {
                string sValue;
                const unsigned char* pch = sqlite3_column_text(stmt, i);
                if (pch)
                    sValue = string((const char*) pch);
                row.push_back(sValue);
            }
            rows.push_back(row);
            rc = sqlite3_step(stmt);
            if (iMaxRowCount > 0 && rows.size() >= iMaxRowCount)
            {
                break;
            }
        }
        sqlite3_finalize(stmt);
        return rows.size();
    }

    void*
    getDbImpl() { return _db; }

    CursorBase*
    cursorLoadImpl(const std::string& sSql, int iPrefetchArraySize)
    {
        if (!_db)
        {
            string sErrorString = "error : db is not open sql!";
            setLastError(-1, sErrorString);
            return NULL;
        }
        int rc, ncols;
        sqlite3_stmt* stmt;
        const char* tail;
        rc = sqlite3_prepare(_db, sSql.c_str(), sSql.size(), &stmt, &tail);
        if (rc != SQLITE_OK)
        {
            string sErrorString = CxString::format("sql exec error: %s", sqlite3_errmsg(_db));
            setLastError(rc, sErrorString);
            return NULL;
        }
        rc = sqlite3_step(stmt);
        ncols = sqlite3_column_count(stmt);
        CursorSqlite * r = new CursorSqlite(iPrefetchArraySize);
        r->_stmt = stmt;
        for (int i = 0; i < ncols; ++i)
        {
            SqliteColumnDesc sqliteColumnDesc;
            sqliteColumnDesc.columnName = sqlite3_column_name(stmt, i);;
            sqliteColumnDesc.ftype = sqlite3_column_type(stmt, i);
            sqliteColumnDesc.fsize = sqlite3_column_bytes(stmt, i);
            r->_columnDescs.push_back(sqliteColumnDesc);
            r->_columnNames.push_back(sqliteColumnDesc.columnName);
            int iColumnType = ColumnTypeNone;
            switch (sqlite3_column_type(stmt, i))
            {
                case SQLITE_INTEGER:
                {
                    iColumnType = ColumnTypeLongint;
                }
                    break;
                case SQLITE_FLOAT:
                {
                    iColumnType = ColumnTypeDouble;
                }
                    break;
                case SQLITE_TEXT:
                {
                    iColumnType = ColumnTypeString;
                }
                    break;
                case SQLITE_BLOB:
                {
                    iColumnType = ColumnTypeBlob;
                }
                    break;
                default:
                {
                    iColumnType = ColumnTypeNone;
                }
            }
            r->_columnTypes.push_back(iColumnType);
            r->_columnSizes.push_back(sqliteColumnDesc.fsize);
        }
        r->_rc = rc;
        r->_ncols = ncols;

        return r;
    }

    bool
    cursorIsEndImpl(CursorBase * oCursor)
    {
        CursorSqlite * oCursorSqlite = (CursorSqlite *)oCursor;
        int rc = oCursorSqlite->_rc;
        return rc != SQLITE_ROW;
    }

    int
    cursorPutImpl(CursorBase* oCursor, std::vector<std::vector<std::string> >& rows, int iMaxRowCount)
    {
        int r = 0;
        if (!_db)
        {
            string sErrorString = "error : db is not open sql!";
            setLastError(-1, sErrorString);
            return r;
        }

        if (oCursor == NULL) return r;
        if (oCursor->_cursorType != CursorTypeSqlite)
        {
            return r;
        }
        CursorSqlite * oCursorSqlite = (CursorSqlite *)oCursor;
        sqlite3_stmt * stmt = oCursorSqlite->_stmt;
        int rc = oCursorSqlite->_rc;
        int ncols = oCursorSqlite->_ncols;
        int iPutRowCount = iMaxRowCount > 0 ? iMaxRowCount : oCursorSqlite->_prefetchArraySize;
        while (rc == SQLITE_ROW)
        {
            vector<string> row;
            for (int i = 0; i < ncols; ++i)
            {
                string sValue;
                const unsigned char* pch = sqlite3_column_text(stmt, i);
                if (pch)
                    sValue = string((const char*) pch);
                row.push_back(sValue);
            }
            rows.push_back(row);
            rc = sqlite3_step(stmt);
            ++r;
            if (r >= iPutRowCount)
            {
                break;
            }
        }
        return r;
    }

    int
    cursorCloseImpl(CursorBase* oCursor)
    {
        return TRUE;
    }

private:
    bool
    beginTransaction()
    {
        return execSqlImpl("BEGIN");
    }

    bool
    commitTransaction()
    {
        return execSqlImpl("COMMIT");
    }

    bool
    rollbackTransaction()
    {
        return execSqlImpl("ROLLBACK");
    }

};

bool
CxDatabaseSqliteFactory::registDatabaseConstructor()
{
    CxDatabaseManager::registDatabaseConstructor(CxDatabaseSqliteFactory::isMyDatabase,
                                                 CxDatabaseSqliteFactory::createDatabase);
    return true;
}

bool
CxDatabaseSqliteFactory::isMyDatabase(const std::string& sConnectSource)
{
    return CxString::equalCase(CxFileSystem::extractFileSuffixName(sConnectSource), ".db");
}

CxDatabase*
CxDatabaseSqliteFactory::createDatabase(void)
{
    return new CxDatabaseSqlite();
}

