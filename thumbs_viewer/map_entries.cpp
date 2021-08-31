/*
	thumbs_viewer will extract thumbnail images from thumbs database files.
	Copyright (C) 2011-2021 Eric Kutcher

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "map_entries.h"
#include "globals.h"
#include "utilities.h"

#include <stdio.h>

wchar_t g_filepath[ MAX_PATH ] = { 0 };				// Path to the files and folders to scan.
wchar_t g_extension_filter[ MAX_PATH + 2 ] = { 0 };	// A list of extensions to filter from a file scan.

bool g_include_folders = false;						// Include folders in a file scan.
bool g_show_details = false;						// Show details in the scan window.

bool g_kill_scan = true;							// Stop a file scan.

bool is_win_8_or_higher = true;						// Windows 8 and newer use a different initial hash value.

unsigned int file_count = 0;						// Number of files scanned.
unsigned int match_count = 0;						// Number of files that match an entry hash.

//#define _WIN32_WINNT_WIN7		0x0601
#define _WIN32_WINNT_WIN8		0x0602
//#define _WIN32_WINNT_WINBLUE	0x0603

BOOL IsWindowsVersionOrGreater( WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor )
{
	OSVERSIONINFOEXW osvi = { sizeof( osvi ), 0, 0, 0, 0, { 0 }, 0, 0 };
	DWORDLONG const dwlConditionMask = VerSetConditionMask(
		VerSetConditionMask(
		VerSetConditionMask(
		0, VER_MAJORVERSION, VER_GREATER_EQUAL ),
		   VER_MINORVERSION, VER_GREATER_EQUAL ),
		   VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL );

	osvi.dwMajorVersion = wMajorVersion;
	osvi.dwMinorVersion = wMinorVersion;
	osvi.wServicePackMajor = wServicePackMajor;

	return VerifyVersionInfoW( &osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask ) != FALSE;
}

BOOL IsWindows8OrGreater()
{
	return IsWindowsVersionOrGreater( HIBYTE( _WIN32_WINNT_WIN8 ), LOBYTE( _WIN32_WINNT_WIN8 ), 0 );
}

void update_scan_info( unsigned long long hash, wchar_t *filepath )
{
	// Now that we have a hash value to compare, search our fileinfo tree for the same value.
	linked_list *ll = ( linked_list * )dllrbt_find( fileinfo_tree, ( void * )hash, true );
	while ( ll != NULL )
	{
		if ( ll->fi != NULL )
		{
			++match_count;

			// Replace the hash filename with the local filename.
			free( ll->fi->filename );
			ll->fi->filename = _wcsdup( filepath );
		}

		ll = ll->next;
	}

	++file_count; 

	// Update our scan window with new scan information.
	if ( g_show_details )
	{
		SendMessage( g_hWnd_scan, WM_PROPAGATE, 3, ( LPARAM )filepath );
		char buf[ 17 ] = { 0 };
		sprintf_s( buf, 17, "%016llx", hash );
		SendMessageA( g_hWnd_scan, WM_PROPAGATE, 4, ( LPARAM )buf );
		sprintf_s( buf, 17, "%lu", file_count );
		SendMessageA( g_hWnd_scan, WM_PROPAGATE, 5, ( LPARAM )buf );
	}
}

unsigned long long hash_data( char *data, unsigned long long hash, short length )
{
	while ( length-- > 0 )
	{
		hash ^= ( ( ( hash * 0x820 ) + ( *data++ & 0x00000000000000FF ) ) + ( hash >> 2 ) );
	}

	return hash;
}

void hash_file( wchar_t *filepath, wchar_t *filename )
{
	// Initial hash value. This value was found in thumbcache.dll.
	unsigned long long hash;

	if ( !is_win_8_or_higher )
	{
		hash = 0x295BA83CF71232D9;
	}
	else
	{
		hash = 0x68DFB54498C54783;
	}

	// Hash the filename.
	hash = hash_data( ( char * )filename, hash, ( short )( wcslen( filename ) * sizeof( wchar_t ) ) );

	update_scan_info( hash, filepath );
}

/*
// The header value for entries created on Windows Vista and newer will contain 8 bytes that are used to identify the original file.
// On Windows Vista and 7 these 8 bytes are the original file's Thumbcache ID. The algorithm for generating that can be found in my Thumbcache Viewer project.
// On Windows 8 and newer these 8 bytes are generated using the algorithm below. 
unsigned long long hash_id_and_date( wchar_t *filepath )
{
	// Initial hash value. This value was found in thumbcache.dll.
	unsigned long long hash = 0xBBA03D3F2F654661;

	HANDLE hFile = CreateFile( filepath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL );
	if ( hFile != INVALID_HANDLE_VALUE )
	{
		// Hash File ID - found in the Master File Table.
		BY_HANDLE_FILE_INFORMATION bhfi;
		GetFileInformationByHandle( hFile, &bhfi );
		CloseHandle( hFile );
		unsigned long long file_id = bhfi.nFileIndexHigh;
		file_id = ( file_id << 32 ) | bhfi.nFileIndexLow;

		hash = hash_data( ( char * )&file_id, hash, sizeof( unsigned long long ) );

		// Hash Last Modified FILETIME.
		return hash_data( ( char * )&bhfi.ftLastWriteTime, hash, sizeof( unsigned long long ) );
	}
	else
	{
		return 0;
	}
}*/

void traverse_directory( wchar_t *path )
{
	// We don't want to continue scanning if the user cancels the scan.
	if ( g_kill_scan )
	{
		return;
	}

	// Set the file path to search for all files/folders in the current directory.
	wchar_t filepath[ ( MAX_PATH * 2 ) + 2 ];
	swprintf_s( filepath, ( MAX_PATH * 2 ) + 2, L"%.259s\\*", path );

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = FindFirstFileEx( ( LPCWSTR )filepath, FindExInfoStandard, &FindFileData, FindExSearchNameMatch, NULL, 0 );
	if ( hFind != INVALID_HANDLE_VALUE ) 
	{
		do
		{
			if ( g_kill_scan )
			{
				break;	// We need to close the find file handle.
			}

			// See if the file is a directory.
			if ( ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 )
			{
				// Go through all directories except "." and ".." (current and parent)
				if ( ( wcscmp( FindFileData.cFileName, L"." ) != 0 ) && ( wcscmp( FindFileData.cFileName, L".." ) != 0 ) )
				{
					// Move to the next directory. Limit the path length to MAX_PATH.
					if ( swprintf_s( filepath, ( MAX_PATH * 2 ) + 2, L"%.259s\\%.259s", path, FindFileData.cFileName ) < MAX_PATH )
					{
						traverse_directory( filepath );

						// Only hash folders if enabled.
						if ( g_include_folders )
						{
							hash_file( filepath, FindFileData.cFileName );
						}
					}
				}
			}
			else
			{
				// See if the file's extension is in our filter. Go to the next file if it's not.
				wchar_t *ext = get_extension_from_filename( FindFileData.cFileName, ( unsigned long )wcslen( FindFileData.cFileName ) );
				if ( g_extension_filter[ 0 ] != 0 )
				{
					// Do a case-insensitive substring search for the extension.
					int ext_length = ( int )wcslen( ext );
					wchar_t *temp_ext = ( wchar_t * )malloc( sizeof( wchar_t ) * ( ext_length + 3 ) );
					for ( int i = 0; i < ext_length; ++i )
					{
						temp_ext[ i + 1 ] = towlower( ext[ i ] );
					}
					temp_ext[ 0 ] = L'|';				// Append the delimiter to the beginning of the string.
					temp_ext[ ext_length + 1 ] = L'|';	// Append the delimiter to the end of the string.
					temp_ext[ ext_length + 2 ] = L'\0';

					if ( wcsstr( g_extension_filter, temp_ext ) == NULL )
					{
						free( temp_ext );
						continue;
					}

					free( temp_ext );
				}

				swprintf_s( filepath, ( MAX_PATH * 2 ) + 2, L"%.259s\\%.259s", path, FindFileData.cFileName );

				hash_file( filepath, FindFileData.cFileName );
			}
		}
		while ( FindNextFile( hFind, &FindFileData ) != 0 );	// Go to the next file.

		FindClose( hFind );	// Close the find file handle.
	}
}

unsigned __stdcall map_entries( void * /*pArguments*/ )
{
	// This will block every other thread from entering until the first thread is complete.
	EnterCriticalSection( &pe_cs );

	SetWindowTextA( g_hWnd_scan, "Map File Paths to Entry Hashes - Please wait..." );	// Update the window title.
	SendMessage( g_hWnd_scan, WM_CHANGE_CURSOR, TRUE, 0 );	// SetCursor only works from the main thread. Set it to an arrow with hourglass.

	// Disable scan button, enable cancel button.
	SendMessage( g_hWnd_scan, WM_PROPAGATE, 1, 0 );

	create_fileinfo_tree();

	file_count = 0;		// Reset the file count.
	match_count = 0;	// Reset the match count.

	is_win_8_or_higher = ( IsWindows8OrGreater() != FALSE ? true : false );

	traverse_directory( g_filepath );

	cleanup_fileinfo_tree();

	InvalidateRect( g_hWnd_list, NULL, TRUE );

	// Update the details.
	if ( !g_show_details )
	{
		char msg[ 11 ] = { 0 };
		sprintf_s( msg, 11, "%lu", file_count );
		SendMessageA( g_hWnd_scan, WM_PROPAGATE, 5, ( LPARAM )msg );
	}

	// Reset button and text.
	SendMessage( g_hWnd_scan, WM_PROPAGATE, 2, 0 );

	if ( match_count > 0 )
	{
		char msg[ 30 ] = { 0 };
		sprintf_s( msg, 30, "%d file%s mapped.", match_count, ( match_count > 1 ? "s were" : " was" ) );
		SendNotifyMessageA( g_hWnd_scan, WM_ALERT, 0, ( LPARAM )msg );
	}
	else
	{
		SendNotifyMessageA( g_hWnd_scan, WM_ALERT, 0, ( LPARAM )"No files were mapped." );
	}

	SendMessage( g_hWnd_scan, WM_CHANGE_CURSOR, FALSE, 0 );	// Reset the cursor.
	SetWindowTextA( g_hWnd_scan, "Map File Paths to Entry Hashes" );	// Reset the window title.

	// We're done. Let other threads continue.
	LeaveCriticalSection( &pe_cs );

	_endthreadex( 0 );
	return 0;
}
