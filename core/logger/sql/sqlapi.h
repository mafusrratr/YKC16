#ifndef _MAIN_H_
#define _MAIN_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "sqlite3.h"
int callback(void *pArg, int nArg, char **azArg, char **azCol);
int CreateSqliteTable(sqlite3 *db,const char *sql);
int OpenSqlite(const char *DbFilePath,sqlite3 **db);
int InsertSqliteata(sqlite3 *db,const char *sql);
int LookUpSqlite(sqlite3 *db,const char *sql);
int DeleteSqlite(sqlite3 *db,const char *sql);
int UpdateSqlite(sqlite3 *db,const char *sql);
int close_sql(void);
int sqlite_preupdate(unsigned char add,unsigned char status,unsigned short number,unsigned char *vin);
int sqlit_exec(unsigned char *vin,unsigned char *result);

#ifdef __cplusplus
}  /* end of the 'extern "C"' block */
#endif


#endif
