//-----------------------------------------------------------------------------
//
// Zandronum Source
// Copyright (C) 2014 Benjamin Berkels
// Copyright (C) 2014 Zandronum Development Team
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the Zandronum Development Team nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
// 4. Redistributions in any form must be accompanied by information on how to
//    obtain complete source code for the software and any accompanying
//    software that uses the software. The source code must either be included
//    in the distribution or be available for no more than the cost of
//    distribution plus a nominal fee, and must be freely redistributable
//    under reasonable conditions. For an executable file, complete source
//    code means the source code for all modules it contains. It does not
//    include source code for modules or files that typically accompany the
//    major components of the operating system on which the executable file
//    runs.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//
//
// Filename: za_database.cpp
//
//-----------------------------------------------------------------------------

#include "za_database.h"
#include "i_system.h"
#include "g_game.h"
#include "p_acs.h"
#include "features/database/computation/database_compute.h"
#include <sqlite3.h>
#include <cstring>
#include <string>

//*****************************************************************************
//	DEFINES

#define TABLENAME "Zandronum"

#define TIMEQUERY "SELECT (julianday('now') - 2440587.5)*86400.0"

//*****************************************************************************
//	VARIABLES

// [BB] Handle to our database.
sqlite3 *g_db = NULL;

// [Doruk-Aksoy] The ACS calls' prepared statements and the log checkpointer, both tied to g_db.
static zx::DatabaseStatements g_dbStatements;
static zx::DatabaseCheckpointer g_dbCheckpointer;
static bool g_dbIsFile = false;

static void database_ApplyPragmas ( void );

// [Doruk-Aksoy] A database file uses WAL when on, so a commit is one log write, and the rollback journal when off; db_enable_wal and db_disable_wal set it.
CUSTOM_CVAR( Bool, fua_db_wal, true, CVAR_ARCHIVE|CVAR_NOSETBYACS )
{
	database_ApplyPragmas ( );
}

// [Doruk-Aksoy] SQLite's synchronous level for a database file: 2 (FULL) waits for every commit to reach the disk, 1 (NORMAL) can lose the last commits on a power cut.
CUSTOM_CVAR( Int, fua_db_synchronous, 2, CVAR_ARCHIVE|CVAR_NOSETBYACS )
{
	database_ApplyPragmas ( );
}

// [BB] Filename for the database.
CUSTOM_CVAR( String, databasefile, ":memory:", CVAR_ARCHIVE|CVAR_NOSETBYACS )
{
	DATABASE_Init ( );
}

// [BB] The max page count allows to limit the size of the database.
CUSTOM_CVAR( Int, database_maxpagecount, 32768, CVAR_ARCHIVE|CVAR_NOSETBYACS )
{
	if ( DATABASE_IsAvailable() )
		DATABASE_SetMaxPageCount ( self );
}

//*****************************************************************************
//	PROTOTYPES

/**
 * \brief Handles the preparation, binding and execution of an SQLite command.
 *
 * \author Benjamin Berkels
 */
class DataBaseCommand
{
	sqlite3_stmt *_stmt;
public:
	DataBaseCommand ( const char *Command ) : _stmt ( NULL )
	{
		int error = sqlite3_prepare ( g_db, Command, -1, &_stmt, NULL );
		if ( error != SQLITE_OK )
			Printf ( "Could not prepare statement. Error: %s\n", sqlite3_errmsg ( g_db ) );
	}

	~DataBaseCommand ( )
	{
		finalize();
	}

	void bindString ( const int Index, const char *String )
	{
		int error = sqlite3_bind_text ( _stmt, Index, String, -1, SQLITE_STATIC );
		if ( error != SQLITE_OK )
			Printf ( "Could not bind text. Error: %s\n", sqlite3_errmsg ( g_db ) );
	}

	void bindInt ( const int Index, const int IntValue )
	{
		int error = sqlite3_bind_int ( _stmt, Index, IntValue );
		if ( error != SQLITE_OK )
			Printf ( "Could not bind integer. Error: %s\n", sqlite3_errmsg ( g_db ) );
	}

	void finalize ( )
	{
		if ( _stmt != NULL )
		{
			sqlite3_finalize ( _stmt );
			_stmt = NULL;
		}
	}

	bool step ( )
	{
		const int result = sqlite3_step ( _stmt );
		if ( ( result != SQLITE_ROW ) && ( result != SQLITE_DONE ) )
		{
			Printf ( "Could not step statement. Error: %s\n", sqlite3_errmsg ( g_db ) );
			finalize ( );
		}

		return ( result == SQLITE_ROW );
	}

	void exec ( )
	{
		const int result = sqlite3_step ( _stmt );
		if ( result == SQLITE_ROW )
			Printf ( "Executing statement did not finish, sqlite3_step() has another row ready.\n" );
		else if ( result != SQLITE_DONE )
			Printf ( "Could not execute statement. Error: %s\n", sqlite3_errmsg ( g_db ) );

		finalize();
	}

	const unsigned char *getText ( const int ColumnIndex )
	{
		return sqlite3_column_text ( _stmt, ColumnIndex );
	}

	int getInteger ( const int ColumnIndex )
	{
		return sqlite3_column_int ( _stmt, ColumnIndex );
	}

	void iterateAndGetReturnedEntries ( TArray<std::pair<FString, FString> > &Entries )
	{
		while ( step( ) )
		{
			std::pair<FString, FString> value;
			value.first.Format ( "%s", getText(1) );
			value.second.Format ( "%s", getText(2) );
			Entries.Push ( value );
		}
		finalize();
	}
};

//*****************************************************************************
//	FUNCTIONS

void database_ClearHandle ( void )
{
	// [Doruk-Aksoy] The checkpointer's connection and the cached statements go first, or sqlite3_close refuses.
	zx::ComputeDatabaseCheckpointerStop ( g_dbCheckpointer );
	zx::ComputeDatabaseBind ( g_dbStatements, NULL );
	g_dbIsFile = false;
	if ( g_db != NULL )
	{
		sqlite3_close ( g_db );
		g_db = NULL;
	}
}

//*****************************************************************************
//
void database_ExecuteCommand ( const char *Command, int (*Callback)(void*,int,char**,char**) = NULL, void *Data = NULL )
{
	int error = sqlite3_exec ( g_db, Command, Callback, Data, 0);
	if ( error != SQLITE_OK )
		Printf ( "Error: %s\n", sqlite3_errmsg ( g_db ) );
}

//*****************************************************************************
//
static void database_ApplyPragmas ( void )
{
	if ( g_db == NULL )
		return;

	for ( const std::string &pragma : zx::ComputeDatabasePragmas ( !g_dbIsFile, fua_db_wal, fua_db_synchronous ) )
		database_ExecuteCommand ( pragma.c_str() );
}

//*****************************************************************************
//
static void database_PrintError ( const char *CallingFunction )
{
	Printf ( "%s error: %s\n", CallingFunction, sqlite3_errmsg ( g_db ) );
}

//*****************************************************************************
//

void DATABASE_Construct( void )
{
	// [BB] At least we should close the database in case it is open.
	atterm( DATABASE_Destruct );
}

//*****************************************************************************
//

void DATABASE_Destruct( void )
{
	database_ClearHandle ( );
}

//*****************************************************************************
//
void DATABASE_Init ( void )
{
	// [BB] Make sure no database is open.
	database_ClearHandle ( );

	const char *dbFileName = databasefile.GetGenericRep( CVAR_String ).String;

	// [BB] If the user doesn't specify a name for the database, don't open one.
	if ( strlen ( dbFileName ) == 0 )
		return;

	const int error = sqlite3_open ( dbFileName, &g_db );
	if ( error )
	{
		Printf ( "Can't open database \"%s\": %s\n", dbFileName, sqlite3_errmsg ( g_db ) );
		database_ClearHandle ( );
		return;
	}
	else if ( strcmp ( dbFileName, ":memory:" ) == 0 )
		Printf ( PRINT_BOLD, "Using in-memory database. The database will not be saved on exit.\n" );
	else
		Printf ( "Opening database \"%s\" succeeded.\n", dbFileName );

	// [BB] Make sure we have a table.
	DATABASE_CreateTable ( );

	// [BB] Now that the database is ready, we can set the max page count.
	DATABASE_SetMaxPageCount ( database_maxpagecount );

	g_dbIsFile = ( strcmp ( dbFileName, ":memory:" ) != 0 );
	database_ApplyPragmas ( );
	zx::ComputeDatabaseBind ( g_dbStatements, g_db );

	// [Doruk-Aksoy] Checkpoints run on their own thread; SQLite's automatic ones stay on only when that thread cannot start.
	if ( g_dbIsFile )
		database_ExecuteCommand ( zx::ComputeDatabaseCheckpointerStart ( g_dbCheckpointer, dbFileName, 1000 ) ? "PRAGMA wal_autocheckpoint=0" : "PRAGMA wal_autocheckpoint=1000" );
}

//*****************************************************************************
//
bool DATABASE_IsAvailable ( const char *CallingFunction )
{
	const bool available = ( g_db != NULL );
	if ( !available && CallingFunction )
		Printf ( "%s error: No database.\n", CallingFunction );

	return available;
}

//*****************************************************************************
//
void DATABASE_SetMaxPageCount ( const unsigned int MaxPageCount )
{
	FString commandString;
	// [BB] Binding MaxPageCount to the query doesn't seem to work, so
	// we'll have to use this workaround.
	commandString.Format ( "PRAGMA max_page_count=%d", MaxPageCount );
	database_ExecuteCommand ( commandString.GetChars() );
}

//*****************************************************************************
//
void DATABASE_BeginTransaction ( void )
{
	if ( DATABASE_IsAvailable ( "DATABASE_BeginTransaction" ) == false )
		return;

	database_ExecuteCommand ( "BEGIN TRANSACTION" );
}

//*****************************************************************************
//
void DATABASE_EndTransaction ( void )
{
	if ( DATABASE_IsAvailable ( "DATABASE_EndTransaction" ) == false )
		return;

	database_ExecuteCommand ( "END TRANSACTION" );
}

//*****************************************************************************
//
void DATABASE_CreateTable ( )
{
	if ( DATABASE_IsAvailable ( "DATABASE_CreateTable" ) == false )
		return;

	database_ExecuteCommand ( "CREATE TABLE if not exists " TABLENAME "(Namespace text, KeyName text, Value text, Timestamp text, PRIMARY KEY (Namespace, KeyName))" );
	database_ExecuteCommand ( zx::ComputeDatabaseIndexSql() );
}

//*****************************************************************************
//
void DATABASE_ClearTable ( )
{
	if ( DATABASE_IsAvailable ( "DATABASE_ClearTable" ) == false )
		return;

	database_ExecuteCommand ( "DELETE FROM " TABLENAME );
}

//*****************************************************************************
//
void DATABASE_DeleteTable ( )
{
	if ( DATABASE_IsAvailable ( "DATABASE_DeleteTable" ) == false )
		return;

	database_ExecuteCommand ( "DROP TABLE " TABLENAME );
}

//*****************************************************************************
//

static int database_DumpTableCallback ( void * /*Data*/, int Argc, char **Argv, char **ColName )
{
   for ( int i = 0; i < Argc; ++i )
      Printf ( "%s = %s\n", ColName[i], Argv[i] ? Argv[i] : "NULL" );
   Printf("\n");
   return 0;
}

//*****************************************************************************
//
void DATABASE_DumpTable ( )
{
	if ( DATABASE_IsAvailable ( "DATABASE_DumpTable" ) == false )
		return;

	Printf ( "Dumping table \"%s\"\n", TABLENAME );
	database_ExecuteCommand ( "SELECT * from " TABLENAME, database_DumpTableCallback );
}

//*****************************************************************************
//
// [Doruk-Aksoy] The journal follows fua_db_wal, so these set it and the last setting applied wins, before or after the file opens.
void DATABASE_EnableWAL ( )
{
	fua_db_wal = true;
}

//*****************************************************************************
//
void DATABASE_DisableWAL ( )
{
	fua_db_wal = false;
}

//*****************************************************************************
//
void DATABASE_DumpNamespace ( const char *Namespace )
{
	if ( DATABASE_IsAvailable ( "DATABASE_DumpNamespace" ) == false )
		return;

	Printf ( "Dumping namespace \"%s\"\n", Namespace );
	DataBaseCommand cmd ( "SELECT * from " TABLENAME " WHERE Namespace=?1" );
	cmd.bindString ( 1, Namespace );
	while ( cmd.step( ) )
		Printf ( "%s %s %s\n", cmd.getText(1), cmd.getText(2), cmd.getText(3) );
	cmd.finalize();
}

//*****************************************************************************
//
void DATABASE_AddEntry ( const char *Namespace, const char *EntryName, const char *EntryValue )
{
	if ( DATABASE_IsAvailable ( "DATABASE_AddEntry" ) == false )
		return;

	DataBaseCommand cmd ( "INSERT INTO " TABLENAME " VALUES(?1,?2,?3,(" TIMEQUERY "))" );
	cmd.bindString ( 1, Namespace );
	cmd.bindString ( 2, EntryName );
	cmd.bindString ( 3, EntryValue );
	cmd.exec ( );
}

//*****************************************************************************
//
void DATABASE_SetEntry ( const char *Namespace, const char *EntryName, const char *EntryValue )
{
	if ( DATABASE_IsAvailable ( "DATABASE_SetEntry" ) == false )
		return;

	DataBaseCommand cmd ( "UPDATE " TABLENAME " SET Value=?3,Timestamp=(" TIMEQUERY ") WHERE Namespace=?1 AND KeyName=?2" );
	cmd.bindString ( 1, Namespace );
	cmd.bindString ( 2, EntryName );
	cmd.bindString ( 3, EntryValue );
	cmd.exec ( );
}

//*****************************************************************************
//
FString DATABASE_GetEntry ( const char *Namespace, const char *EntryName )
{
	if ( DATABASE_IsAvailable ( "DATABASE_GetEntry" ) == false )
		return "";

	DataBaseCommand cmd ( "SELECT * FROM " TABLENAME " WHERE Namespace=?1 AND KeyName=?2" );
	cmd.bindString ( 1, Namespace );
	cmd.bindString ( 2, EntryName );
	cmd.step( );
	FString value;
	value.AppendFormat ( "%s", cmd.getText(2) );
	// [BB] We assume that the query will return exactly one row. So we finish stepping now.
	cmd.exec( );
	return value;
}

//*****************************************************************************
//
bool DATABASE_EntryExists ( const char *Namespace, const char *EntryName )
{
	if ( DATABASE_IsAvailable ( "DATABASE_GetEntry" ) == false )
		return "";

	DataBaseCommand cmd ( "SELECT * FROM " TABLENAME " WHERE Namespace=?1 AND KeyName=?2" );
	cmd.bindString ( 1, Namespace );
	cmd.bindString ( 2, EntryName );
	// [BB] The destructor of DataBaseCommand calls finalize.
	return ( cmd.step( ) );
}

//*****************************************************************************
//
void DATABASE_DeleteEntry ( const char *Namespace, const char *EntryName )
{
	if ( DATABASE_IsAvailable ( "DATABASE_DeleteEntry" ) == false )
		return;

	DataBaseCommand cmd ( "DELETE FROM " TABLENAME " WHERE Namespace=?1 AND KeyName=?2" );
	cmd.bindString ( 1, Namespace );
	cmd.bindString ( 2, EntryName );
	cmd.exec ( );
}

//*****************************************************************************
//
void DATABASE_SaveSetEntry ( const char *Namespace, const char *EntryName, const char *EntryValue )
{
	if ( DATABASE_IsAvailable ( "DATABASE_SaveSetEntry" ) == false )
		return;

	// [Doruk-Aksoy] One cached upsert, or a delete for an empty value, in place of the exists check and its follow-up.
	if ( zx::ComputeDatabaseSaveSet ( g_dbStatements, Namespace, EntryName, EntryValue ) == false )
		database_PrintError ( "DATABASE_SaveSetEntry" );
}

//*****************************************************************************
//
void DATABASE_SaveSetEntryInt ( const char *Namespace, const char *EntryName, int EntryValue )
{
	FString entryValue;
	entryValue.AppendFormat ( "%d", EntryValue );
	DATABASE_SaveSetEntry ( Namespace, EntryName, entryValue.GetChars() );
}

//*****************************************************************************
//
FString DATABASE_SaveGetEntry ( const char *Namespace, const char *EntryName )
{
	if ( DATABASE_IsAvailable ( "DATABASE_SaveGetEntry" ) == false )
		return "";

	std::string value;
	bool found = false;
	if ( zx::ComputeDatabaseGet ( g_dbStatements, Namespace, EntryName, value, found ) == false )
		database_PrintError ( "DATABASE_SaveGetEntry" );
	return value.c_str();
}

//*****************************************************************************
//
void DATABASE_SaveIncrementEntryInt ( const char *Namespace, const char *EntryName, int Increment )
{
	if ( DATABASE_IsAvailable ( "DATABASE_SaveIncrementEntryInt" ) == false )
		return;

	if ( zx::ComputeDatabaseIncrement ( g_dbStatements, Namespace, EntryName, Increment ) == false )
		database_PrintError ( "DATABASE_SaveIncrementEntryInt" );
}

//*****************************************************************************
//
int DATABASE_GetEntryRank ( const char *Namespace, const char *EntryName, const bool Descending )
{
	if ( DATABASE_IsAvailable ( "DATABASE_GetEntryRank" ) == false )
		return -1;

	int rank = -1;
	if ( zx::ComputeDatabaseRank ( g_dbStatements, Namespace, EntryName, Descending, rank ) == false )
		database_PrintError ( "DATABASE_GetEntryRank" );
	return rank;
}

//*****************************************************************************
//
static int database_CopyEntries ( const zx::DatabaseEntries &Source, TArray<std::pair<FString, FString> > &Entries )
{
	Entries.Clear();
	for ( const std::pair<std::string, std::string> &entry : Source )
		Entries.Push ( std::pair<FString, FString> ( entry.first.c_str(), entry.second.c_str() ) );
	return Entries.Size();
}

//*****************************************************************************
//
int DATABASE_GetSortedEntries ( const char *Namespace, const int N, const int Offset, const bool Descending, TArray<std::pair<FString, FString> > &Entries )
{
	if ( DATABASE_IsAvailable ( "DATABASE_GetSortedEntryAsPair" ) == false )
	{
		Entries.Clear();
		return 0;
	}

	zx::DatabaseEntries entries;
	if ( zx::ComputeDatabaseSorted ( g_dbStatements, Namespace, N, Offset, Descending, entries ) == false )
		database_PrintError ( "DATABASE_GetSortedEntries" );
	return database_CopyEntries ( entries, Entries );
}

//*****************************************************************************
//
int DATABASE_GetEntries ( const char *Namespace, TArray<std::pair<FString, FString> > &Entries )
{
	if ( DATABASE_IsAvailable ( "DATABASE_GetEntries" ) == false )
	{
		Entries.Clear();
		return 0;
	}

	zx::DatabaseEntries entries;
	if ( zx::ComputeDatabaseEntries ( g_dbStatements, Namespace, entries ) == false )
		database_PrintError ( "DATABASE_GetEntries" );
	return database_CopyEntries ( entries, Entries );
}

//*****************************************************************************
//	CONSOLE COMMANDS

CCMD ( dumpdb )
{
	DATABASE_DumpTable();
}

CCMD ( db_enable_wal )
{
	// [BB] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	DATABASE_EnableWAL();
}

CCMD ( db_disable_wal )
{
	// [BB] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	DATABASE_DisableWAL();
}
