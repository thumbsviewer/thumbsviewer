/*
    thumbs_viewer will extract thumbnail images from thumbs database files.
    Copyright (C) 2011-2014 Eric Kutcher

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

#include "globals.h"

#include <stdio.h>
#include <stdlib.h>

#define FILE_TYPE_JPEG	"\xFF\xD8\xFF\xE0"
#define FILE_TYPE_PNG	"\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"

// Return status codes for various functions.
#define SC_FAIL	0
#define SC_OK	1
#define SC_QUIT	2

HANDLE shutdown_mutex = NULL;	// Blocks shutdown while a worker thread is active.
bool kill_thread = false;		// Allow for a clean shutdown.

CRITICAL_SECTION pe_cs;			// Queues additional worker threads.
bool in_thread = false;			// Flag to indicate that we're in a worker thread.
bool skip_draw = false;			// Prevents WM_DRAWITEM from accessing listview items while we're removing them.

unsigned long msat_size = 0;
unsigned long sat_size = 0;
unsigned long ssat_size = 0;

long *g_msat = NULL;

// 20 bytes
#define jfif_header		"\xFF\xD8\xFF\xE0\x00\x10\x4A\x46\x49\x46\x00\x01\x01\x01\x00\x60" \
						"\x00\x60\x00\x00"

// 138 bytes (Luminance and Chrominance)
#define quantization	"\xFF\xDB\x00\x43\x00\x08\x06\x06\x07\x06\x05\x08\x07\x07\x07\x09" \
						"\x09\x08\x0A\x0C\x14\x0D\x0C\x0B\x0B\x0C\x19\x12\x13\x0F\x14\x1D" \
						"\x1A\x1F\x1E\x1D\x1A\x1C\x1C\x20\x24\x2E\x27\x20\x22\x2C\x23\x1C" \
						"\x1C\x28\x37\x29\x2C\x30\x31\x34\x34\x34\x1F\x27\x39\x3D\x38\x32" \
						"\x3C\x2E\x33\x34\x32"											   \
						"\xFF\xDB\x00\x43\x01\x09\x09\x09\x0C\x0B\x0C\x18\x0D\x0D\x18\x32" \
						"\x21\x1C\x21\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32" \
						"\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32" \
						"\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32\x32" \
						"\x32\x32\x32\x32\x32"

// 216 bytes
#define huffman_table	"\xFF\xC4\x00\x1F\x00\x00\x01\x05\x01\x01\x01\x01\x01\x01\x00\x00" \
						"\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A" \
						"\x0B\xFF\xC4\x00\xB5\x10\x00\x02\x01\x03\x03\x02\x04\x03\x05\x05" \
						"\x04\x04\x00\x00\x01\x7D\x01\x02\x03\x00\x04\x11\x05\x12\x21\x31" \
						"\x41\x06\x13\x51\x61\x07\x22\x71\x14\x32\x81\x91\xA1\x08\x23\x42" \
						"\xB1\xC1\x15\x52\xD1\xF0\x24\x33\x62\x72\x82\x09\x0A\x16\x17\x18" \
						"\x19\x1A\x25\x26\x27\x28\x29\x2A\x34\x35\x36\x37\x38\x39\x3A\x43" \
						"\x44\x45\x46\x47\x48\x49\x4A\x53\x54\x55\x56\x57\x58\x59\x5A\x63" \
						"\x64\x65\x66\x67\x68\x69\x6A\x73\x74\x75\x76\x77\x78\x79\x7A\x83" \
						"\x84\x85\x86\x87\x88\x89\x8A\x92\x93\x94\x95\x96\x97\x98\x99\x9A" \
						"\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xAA\xB2\xB3\xB4\xB5\xB6\xB7\xB8" \
						"\xB9\xBA\xC2\xC3\xC4\xC5\xC6\xC7\xC8\xC9\xCA\xD2\xD3\xD4\xD5\xD6" \
						"\xD7\xD8\xD9\xDA\xE1\xE2\xE3\xE4\xE5\xE6\xE7\xE8\xE9\xEA\xF1\xF2" \
						"\xF3\xF4\xF5\xF6\xF7\xF8\xF9\xFA"

rbt_tree *fileinfo_tree = NULL;	// Red-black tree of fileinfo structures.

unsigned int file_count = 0;	// Number of files scanned.
unsigned int match_count = 0;	// Number of files that match an entry hash.

int rbt_compare( void *a, void *b )
{
	if ( a > b )
	{
		return 1;
	}
	
	if ( a < b )
	{
		return -1;
	}

	return 0;
}

wchar_t *get_extension_from_filename( wchar_t *filename, unsigned long length )
{
	while ( length != 0 && filename[ --length ] != L'.' );

	return filename + length;
}

wchar_t *get_filename_from_path( wchar_t *path, unsigned long length )
{
	while ( length != 0 && path[ --length ] != L'\\' );

	if ( path[ length ] == L'\\' )
	{
		++length;
	}
	return path + length;
}

void reverse_string( wchar_t *string )
{
	if ( string == NULL )
	{
		return;
	}

	int end = wcslen( string ) - 1;
	int start = 0;

	while ( start < end )
	{
		string[ start ] ^= string[ end ];
		string[ end ] ^= string[ start ];
		string[ start ] ^= string[ end ];

		++start;
		--end;
	}
}

unsigned long long hash_data( char *data, unsigned long long hash, short length )
{
	while ( length-- > 0 )
	{
		hash = ( ( ( hash * 0x820 ) + ( *data++ & 0x00000000000000FF ) ) + ( hash >> 2 ) ) ^ hash;
	}

	return hash;
}

void hash_file( wchar_t *filepath, wchar_t *filename )
{
	// Initial hash value. This value was found in thumbcache.dll.
	unsigned long long hash = 0x295BA83CF71232D9;

	// Hash the filename.
	hash = hash_data( ( char * )filename, hash, wcslen( filename ) * sizeof( wchar_t ) );

	// Now that we have a hash value to compare, search our fileinfo tree for the same value.
	fileinfo *fi = ( fileinfo * )rbt_find( fileinfo_tree, ( void * )hash, true );
	if ( fi != NULL )
	{
		++match_count;

		// Replace the hash filename with the local filename.
		free( fi->filename );
		fi->filename = _wcsdup( filepath );
	}

	++file_count; 

	// Update our scan window with new scan information.
	if ( show_details == true )
	{
		SendMessage( g_hWnd_scan, WM_PROPAGATE, 3, ( LPARAM )filepath );
		wchar_t buf[ 19 ] = { 0 };
		swprintf_s( buf, 19, L"0x%016llx", hash );
		SendMessage( g_hWnd_scan, WM_PROPAGATE, 4, ( LPARAM )buf );
		swprintf_s( buf, 19, L"%lu", file_count );
		SendMessage( g_hWnd_scan, WM_PROPAGATE, 5, ( LPARAM )buf );
	}
}

void traverse_directory( wchar_t *path )
{
	// We don't want to continue scanning if the user cancels the scan.
	if ( kill_scan == true )
	{
		return;
	}

	// Set the file path to search for all files/folders in the current directory.
	wchar_t filepath[ MAX_PATH ];
	swprintf_s( filepath, MAX_PATH, L"%s\\*", path );

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = FindFirstFileEx( ( LPCWSTR )filepath, FindExInfoStandard, &FindFileData, FindExSearchNameMatch, NULL, 0 );
	if ( hFind != INVALID_HANDLE_VALUE ) 
	{
		do
		{
			if ( kill_scan == true )
			{
				break;	// We need to close the find file handle.
			}

			wchar_t next_path[ MAX_PATH ];

			// See if the file is a directory.
			if ( ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 )
			{
				// Go through all directories except "." and ".." (current and parent)
				if ( ( wcscmp( FindFileData.cFileName, L"." ) != 0 ) && ( wcscmp( FindFileData.cFileName, L".." ) != 0 ) )
				{
					// Move to the next directory.
					swprintf_s( next_path, MAX_PATH, L"%s\\%s", path, FindFileData.cFileName );

					traverse_directory( next_path );

					// Only hash folders if enabled.
					if ( include_folders == true )
					{
						hash_file( next_path, FindFileData.cFileName );
					}
				}
			}
			else
			{
				// See if the file's extension is in our filter. Go to the next file if it's not.
				wchar_t *ext = get_extension_from_filename( FindFileData.cFileName, wcslen( FindFileData.cFileName ) );
				if ( extension_filter[ 0 ] != 0 )
				{
					// Do a case-insensitive substring search for the extension.
					int ext_length = wcslen( ext );
					wchar_t *temp_ext = ( wchar_t * )malloc( sizeof( wchar_t ) * ( ext_length + 3 ) );
					for ( int i = 0; i < ext_length; ++i )
					{
						temp_ext[ i + 1 ] = towlower( ext[ i ] );
					}
					temp_ext[ 0 ] = L'|';				// Append the delimiter to the beginning of the string.
					temp_ext[ ext_length + 1 ] = L'|';	// Append the delimiter to the end of the string.
					temp_ext[ ext_length + 2 ] = L'\0';

					if ( wcsstr( extension_filter, temp_ext ) == NULL )
					{
						free( temp_ext );
						continue;
					}

					free( temp_ext );
				}

				swprintf_s( next_path, MAX_PATH, L"%s\\%s", path, FindFileData.cFileName );

				hash_file( next_path, FindFileData.cFileName );
			}
		}
		while ( FindNextFile( hFind, &FindFileData ) != 0 );	// Go to the next file.

		FindClose( hFind );	// Close the find file handle.
	}
}

unsigned __stdcall scan_files( void *pArguments )
{
	// This will block every other thread from entering until the first thread is complete.
	EnterCriticalSection( &pe_cs );

	SetWindowText( g_hWnd_scan, L"Map File Paths to Entry Hashes - Please wait..." );	// Update the window title.
	SendMessage( g_hWnd_scan, WM_CHANGE_CURSOR, TRUE, 0 );	// SetCursor only works from the main thread. Set it to an arrow with hourglass.

	// Disable scan button, enable cancel button.
	SendMessage( g_hWnd_scan, WM_PROPAGATE, 1, 0 );

	LVITEM lvi = { NULL };
	lvi.mask = LVIF_PARAM;

	int item_count = SendMessage( g_hWnd_list, LVM_GETITEMCOUNT, 0, 0 );

	// Create the fileinfo tree if it doesn't exist.
	if ( fileinfo_tree == NULL )
	{
		fileinfo_tree = rbt_create( rbt_compare );
	}

	// Go through each item and add them to our tree.
	for ( int i = 0; i < item_count; ++i )
	{
		// We don't want to continue scanning if the user cancels the scan.
		if ( kill_scan == true )
		{
			break;
		}

		lvi.iItem = i;
		SendMessage( g_hWnd_list, LVM_GETITEM, 0, ( LPARAM )&lvi );

		if ( ( ( fileinfo * )lvi.lParam )->mapped == false )
		{
			// Make sure it's a hashed filename. It should be formatted like: 256_0123456789ABCDEF
			wchar_t *filename = wcschr( ( ( fileinfo * )lvi.lParam )->filename, L'_' );

			if ( filename != NULL )
			{
				( ( fileinfo * )lvi.lParam )->entry_hash = _wcstoui64( filename + 1, NULL, 16 );

				// Don't attempt to insert the fileinfo if it's already in the tree, or if it's a duplicate.
				if ( ( ( fileinfo * )lvi.lParam )->entry_hash != 0 && rbt_insert( fileinfo_tree, ( void * )( ( fileinfo * )lvi.lParam )->entry_hash, ( fileinfo * )lvi.lParam ) == RBT_STATUS_OK )
				{
					( ( fileinfo * )lvi.lParam )->mapped = true;
				}
			}
		}
	}

	file_count = 0;		// Reset the file count.
	match_count = 0;	// Reset the match count.
	traverse_directory( g_filepath );

	InvalidateRect( g_hWnd_list, NULL, TRUE );

	// Update the details.
	if ( show_details == false )
	{
		wchar_t msg[ 11 ] = { 0 };
		swprintf_s( msg, 11, L"%lu", file_count );
		SendMessage( g_hWnd_scan, WM_PROPAGATE, 5, ( LPARAM )msg );
	}

	// Reset button and text.
	SendMessage( g_hWnd_scan, WM_PROPAGATE, 2, 0 );

	if ( match_count > 0 )
	{
		wchar_t msg[ 30 ] = { 0 };
		swprintf_s( msg, 30, L"%d file%s mapped.", match_count, ( match_count > 1 ? L"s were" : L" was" ) );
		MessageBox( g_hWnd_scan, msg, PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONINFORMATION );
	}
	else
	{
		MessageBox( g_hWnd_scan, L"No files were mapped.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONINFORMATION );
	}

	SendMessage( g_hWnd_scan, WM_CHANGE_CURSOR, FALSE, 0 );	// Reset the cursor.
	SetWindowText( g_hWnd_scan, L"Map File Paths to Entry Hashes" );	// Reset the window title.

	// We're done. Let other threads continue.
	LeaveCriticalSection( &pe_cs );

	_endthreadex( 0 );
	return 0;
}

int GetEncoderClsid( const WCHAR *format, CLSID *pClsid )
{
	UINT num = 0;          // number of image encoders
	UINT size = 0;         // size of the image encoder array in bytes

	Gdiplus::ImageCodecInfo *pImageCodecInfo = NULL;

	Gdiplus::GetImageEncodersSize( &num, &size );
	if ( size == 0 )
	{
		return -1;  // Failure
	}

	pImageCodecInfo = ( Gdiplus::ImageCodecInfo * )( malloc( size ) );
	if ( pImageCodecInfo == NULL )
	{
		return -1;  // Failure
	}

	Gdiplus::GetImageEncoders( num, size, pImageCodecInfo );

	for ( UINT j = 0; j < num; ++j )
	{
		if ( wcscmp( pImageCodecInfo[ j ].MimeType, format ) == 0 )
		{
			*pClsid = pImageCodecInfo[ j ].Clsid;
			free( pImageCodecInfo );
			return j;  // Success
		}    
	}

	free( pImageCodecInfo );
	return -1;  // Failure
}

// Create a stream to store our buffer and then store the stream into a GDI+ image object.
Gdiplus::Image *create_image( char *buffer, unsigned long size, unsigned char format, unsigned int raw_width, unsigned int raw_height, unsigned int raw_size, int raw_stride )
{
	ULONG written = 0;
	IStream *is = NULL;
	CreateStreamOnHGlobal( NULL, TRUE, &is );
	is->Write( buffer, size, &written );
	Gdiplus::Image *image = new Gdiplus::Image( is );

	// If we have a CYMK based JPEG, then we're going to have to convert it to RGB.
	if ( format == 1 )
	{
		UINT height = image->GetHeight();
		UINT width = image->GetWidth();

		Gdiplus::Rect rc( 0, 0, width, height );
		Gdiplus::BitmapData bmd;

		// Bitmap with CMYK values.
		Gdiplus::Bitmap bm( is );
		// There's no mention of PixelFormat32bppCMYK on MSDN, but I think the minimum support is Windows XP with its latest service pack (SP3 for 32bit, and SP2 for 64bit).
		if ( bm.LockBits( &rc, Gdiplus::ImageLockModeRead, PixelFormat32bppCMYK, &bmd ) == Gdiplus::Ok )
		{
			Gdiplus::BitmapData bmd2;
			// New bitmap to convert CMYK to RGB
			Gdiplus::Bitmap *new_image = new Gdiplus::Bitmap( width, height, PixelFormat32bppRGB );
			if ( new_image->LockBits( &rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppRGB, &bmd2 ) == Gdiplus::Ok )
			{
				UINT *raw_bm = ( UINT * )bmd.Scan0;
				UINT *raw_bm2 = ( UINT * )bmd2.Scan0;

				// Go through each pixel in the array.
				for ( UINT row = 0; row < height; ++row )
				{
					for ( UINT col = 0; col < width; ++col )
					{
						// LockBits with PixelFormat32bppCMYK appears to remove the black channel and leaves us with CMY values in the range of 0 to 255.
						// We take the compliment of cyan, magenta, and yellow to get our RGB values. (255 - C), (255 - M), (255 - Y)
						// Notice that we're writing the pixels in reverse order (to flip the image on the horizontal axis).
						raw_bm2[ ( ( ( ( height - 1 ) - row ) * bmd2.Stride ) / 4 ) + col ] = 0xFF000000
							| ( ( 255 - ( ( 0x00FF0000 & raw_bm[ row * bmd.Stride / 4 + col ] ) >> 16 ) ) << 16 )
							| ( ( 255 - ( ( 0x0000FF00 & raw_bm[ row * bmd.Stride / 4 + col ] ) >> 8 ) ) << 8 )
							|   ( 255 - ( ( 0x000000FF & raw_bm[ row * bmd.Stride / 4 + col ] ) ) );
					}
				}

				bm.UnlockBits( &bmd );
				new_image->UnlockBits( &bmd2 );

				// Delete the old image created from the image stream and set it to the new bitmap.
				delete image;
				image = NULL;
				image = new_image;
			}
			else
			{
				delete new_image;
			}
		}
	}
	else if ( ( format == 2 || format == 3 ) && raw_width > 0 && raw_height > 0 && raw_size > 0 )	// File might be a raw bitmap.
	{
		Gdiplus::Rect rc( 0, 0, raw_width, raw_height );
		Gdiplus::BitmapData bmd;

		unsigned int channels = raw_size / ( raw_width * raw_height );

		if ( channels == 3 )
		{
			Gdiplus::Bitmap *new_image = new Gdiplus::Bitmap( raw_width, raw_height, PixelFormat24bppRGB );
			if ( new_image->LockBits( &rc, Gdiplus::ImageLockModeWrite, PixelFormat24bppRGB, &bmd ) == Gdiplus::Ok )
			{
				if ( format == 2 )	// This image is flipped along the horizontal axis.
				{
					unsigned char *raw_bm = ( unsigned char * )bmd.Scan0;

					raw_stride = abs( raw_stride );
					unsigned int padding = 0;
					if ( raw_size % ( raw_width * raw_height ) != 0 )
					{
						padding = raw_stride - ( raw_width * channels );
					}	

					// Copy each row (excluding any padding) in reverse order. 
					for ( unsigned int offset = 0; offset < raw_size; offset += raw_stride )
					{
						memcpy_s( raw_bm + ( raw_size - ( raw_width * channels ) ) - offset - padding, raw_size - offset, buffer + offset, raw_stride - padding );
					} 
				}
				else if ( format == 3 )	// This image is not flipped along the horizontal axis.
				{
					memcpy_s( bmd.Scan0, raw_size, buffer, raw_size );
				}

				new_image->UnlockBits( &bmd );

				// Delete the old image created from the image stream and set it to the new bitmap.
				delete image;
				image = NULL;
				image = new_image;
			}
			else
			{
				delete new_image;
			}
		}
		else if ( channels == 4 )
		{
			// This bitmap contains an alpha channel.
			Gdiplus::Bitmap *new_image = new Gdiplus::Bitmap( raw_width, raw_height, PixelFormat32bppARGB );
			if ( new_image->LockBits( &rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmd ) == Gdiplus::Ok )
			{
				memcpy_s( bmd.Scan0, raw_size, buffer, raw_size );

				new_image->UnlockBits( &bmd );

				// Delete the old image created from the image stream and set it to the new bitmap.
				delete image;
				image = NULL;
				image = new_image;
			}
			else
			{
				delete new_image;
			}
		}
	}

	is->Release();

	return image;
}

// This will allow our main thread to continue while secondary threads finish their processing.
unsigned __stdcall cleanup( void *pArguments )
{
	// This mutex will be released when the thread gets killed.
	shutdown_mutex = CreateSemaphore( NULL, 0, 1, NULL );

	kill_thread = true;	// Causes secondary threads to cease processing and release the mutex.

	// Wait for any active threads to complete. 5 second timeout in case we miss the release.
	WaitForSingleObject( shutdown_mutex, 5000 );
	CloseHandle( shutdown_mutex );
	shutdown_mutex = NULL;

	// DestroyWindow won't work on a window from a different thread. So we'll send a message to trigger it.
	SendMessage( g_hWnd_main, WM_DESTROY_ALT, 0, 0 );

	_endthreadex( 0 );
	return 0;
}

unsigned __stdcall remove_items( void *pArguments )
{
	// This will block every other thread from entering until the first thread is complete.
	EnterCriticalSection( &pe_cs );

	in_thread = true;
	
	skip_draw = true;	// Prevent the listview from drawing while freeing lParam values.

	SetWindowText( g_hWnd_main, L"Thumbs Viewer - Please wait..." );	// Update the window title.
	EnableWindow( g_hWnd_list, FALSE );									// Prevent any interaction with the listview while we're processing.
	SendMessage( g_hWnd_main, WM_CHANGE_CURSOR, TRUE, 0 );				// SetCursor only works from the main thread. Set it to an arrow with hourglass.
	update_menus( true );												// Disable all processing menu items.

	LVITEM lvi = { NULL };
	lvi.mask = LVIF_PARAM;

	int item_count = SendMessage( g_hWnd_list, LVM_GETITEMCOUNT, 0, 0 );
	int sel_count = SendMessage( g_hWnd_list, LVM_GETSELECTEDCOUNT, 0, 0 );

	// See if we've selected all the items. We can clear the list much faster this way.
	if ( item_count == sel_count )
	{
		// Go through each item, and free their lParam values. current_fileinfo will get deleted here.
		for ( int i = 0; i < item_count; ++i )
		{
			// Stop processing and exit the thread.
			if ( kill_thread == true )
			{
				break;
			}

			// We first need to get the lParam value otherwise the memory won't be freed.
			lvi.iItem = i;
			SendMessage( g_hWnd_list, LVM_GETITEM, 0, ( LPARAM )&lvi );

			( ( fileinfo * )lvi.lParam )->si->count--;

			// Remove our shared information from the linked list if there's no more items for this database.
			if ( ( ( fileinfo * )lvi.lParam )->si->count == 0 )
			{
				free( ( ( fileinfo * )lvi.lParam )->si->sat );
				free( ( ( fileinfo * )lvi.lParam )->si->ssat );
				free( ( ( fileinfo * )lvi.lParam )->si->short_stream_container );
				free( ( ( fileinfo * )lvi.lParam )->si );
			}

			// First free the filename pointer. We don't need to bother with the linked list pointer since it's only used during the initial read.
			free( ( ( fileinfo * )lvi.lParam )->filename );
			// Then free the fileinfo structure.
			free( ( fileinfo * )lvi.lParam );
		}

		// Clean up our fileinfo tree.
		rbt_delete( fileinfo_tree );
		fileinfo_tree = NULL;

		SendMessage( g_hWnd_list, LVM_DELETEALLITEMS, 0, 0 );
	}
	else	// Otherwise, we're going to have to go through each selection one at a time. (SLOOOOOW) Start from the end and work our way to the beginning.
	{
		// Scroll to the first item.
		// This will reduce the time it takes to remove a large selection of items.
		// When we delete the item from the end of the listview, the control won't force a paint refresh (since the item's not likely to be visible)
		SendMessage( g_hWnd_list, LVM_ENSUREVISIBLE, 0, FALSE );

		int *index_array = ( int * )malloc( sizeof( int ) * sel_count );

		lvi.iItem = -1;	// Set this to -1 so that the LVM_GETNEXTITEM call can go through the list correctly.

		// Create an index list of selected items (in reverse order).
		for ( int i = 0; i < sel_count; i++ )
		{
			lvi.iItem = index_array[ sel_count - 1 - i ] = SendMessage( g_hWnd_list, LVM_GETNEXTITEM, lvi.iItem, LVNI_SELECTED );
		}

		for ( int i = 0; i < sel_count; i++ )
		{
			// Stop processing and exit the thread.
			if ( kill_thread == true )
			{
				break;
			}

			// We first need to get the lParam value otherwise the memory won't be freed.
			lvi.iItem = index_array[ i ];
			SendMessage( g_hWnd_list, LVM_GETITEM, 0, ( LPARAM )&lvi );

			// Remove the fileinfo from the fileinfo tree if it exists in it.
			if ( ( ( fileinfo * )lvi.lParam )->mapped == true )
			{
				// First find the fileinfo to remove from the fileinfo tree.
				rbt_iterator *itr = rbt_find( fileinfo_tree, ( void * )( ( fileinfo * )lvi.lParam )->entry_hash, false );
				// Now remove it from the fileinfo tree. The tree will rebalance itself.
				rbt_remove( fileinfo_tree, itr );
			}

			( ( fileinfo * )lvi.lParam )->si->count--;

			// Remove our shared information from the linked list if there's no more items for this database.
			if ( ( ( fileinfo * )lvi.lParam )->si->count == 0 )
			{
				free( ( ( fileinfo * )lvi.lParam )->si->sat );
				free( ( ( fileinfo * )lvi.lParam )->si->ssat );
				free( ( ( fileinfo * )lvi.lParam )->si->short_stream_container );
				free( ( ( fileinfo * )lvi.lParam )->si );
			}
	
			// Free our filename, then fileinfo structure. We don't need to bother with the linked list pointer since it's only used during the initial read.
			free( ( ( fileinfo * )lvi.lParam )->filename );
			// Then free the fileinfo structure.
			free( ( fileinfo * )lvi.lParam );

			// Remove the list item.
			SendMessage( g_hWnd_list, LVM_DELETEITEM, index_array[ i ], 0 );
		}

		free( index_array );
	}

	skip_draw = false;	// Allow drawing again.

	update_menus( false );									// Enable the appropriate menu items.
	SendMessage( g_hWnd_main, WM_CHANGE_CURSOR, FALSE, 0 );	// Reset the cursor.
	EnableWindow( g_hWnd_list, TRUE );						// Allow the listview to be interactive. Also forces a refresh to update the item count column.
	SetFocus( g_hWnd_list );								// Give focus back to the listview to allow shortcut keys.
	SetWindowText( g_hWnd_main, PROGRAM_CAPTION );			// Reset the window title.

	// Release the mutex if we're killing the thread.
	if ( shutdown_mutex != NULL )
	{
		ReleaseSemaphore( shutdown_mutex, 1, NULL );
	}

	in_thread = false;

	// We're done. Let other threads continue.
	LeaveCriticalSection( &pe_cs );

	_endthreadex( 0 );
	return 0;
}

// Allocates a new string if characters need escaping. Otherwise, it returns NULL.
char *escape_csv( const char *string )
{
	char *escaped_string = NULL;
	char *q = NULL;
	const char *p = NULL;
	int c = 0;

	if ( string == NULL )
	{
		return NULL;
	}

	// Get the character count and offset it for any quotes.
	for ( c = 0, p = string; *p != NULL; ++p ) 
	{
		if ( *p != '\"' )
		{
			++c;
		}
		else
		{
			c += 2;
		}
	}

	// If the string has no special characters to escape, then return NULL.
	if ( c <= ( p - string ) )
	{
		return NULL;
	}

	q = escaped_string = ( char * )malloc( sizeof( char ) * ( c + 1 ) );

	for ( p = string; *p != NULL; ++p ) 
	{
		if ( *p != '\"' )
		{
			*q = *p;
			++q;
		}
		else
		{
			*q++ = '\"';
			*q++ = '\"';
		}
	}

	*q = 0;	// Sanity.

	return escaped_string;
}

unsigned __stdcall save_csv( void *pArguments )
{
	// This will block every other thread from entering until the first thread is complete.
	EnterCriticalSection( &pe_cs );

	in_thread = true;

	SetWindowText( g_hWnd_main, L"Thumbs Viewer - Please wait..." );	// Update the window title.
	EnableWindow( g_hWnd_list, FALSE );									// Prevent any interaction with the listview while we're processing.
	SendMessage( g_hWnd_main, WM_CHANGE_CURSOR, TRUE, 0 );				// SetCursor only works from the main thread. Set it to an arrow with hourglass.
	update_menus( true );												// Disable all processing menu items.

	save_param *save_type = ( save_param * )pArguments;

	// Open our config file if it exists.
	HANDLE hFile = CreateFile( save_type->filepath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile != INVALID_HANDLE_VALUE )
	{
		int size = ( 32768 + 1 );
		DWORD write = 0;
		int write_buf_offset = 0;
		char *system_string = NULL;
		SYSTEMTIME st;
		FILETIME ft;

		char *write_buf = ( char * )malloc( sizeof( char ) * size );

		// Write the UTF-8 BOM and CSV column titles.
		WriteFile( hFile, "\xEF\xBB\xBF" "Filename,Entry Size (bytes),Sector Index,Date Modified (UTC),FILETIME,System,Location", 88, &write, NULL );

		// Get the number of items we'll be saving.
		int save_items = SendMessage( g_hWnd_list, LVM_GETITEMCOUNT, 0, 0 );

		// Retrieve the lParam value from the selected listview item.
		LVITEM lvi = { NULL };
		lvi.mask = LVIF_PARAM;

		// Go through all the items we'll be saving.
		for ( int i = 0; i < save_items; ++i )
		{
			// Stop processing and exit the thread.
			if ( kill_thread == true )
			{
				break;
			}

			lvi.iItem = i;
			SendMessage( g_hWnd_list, LVM_GETITEM, 0, ( LPARAM )&lvi );

			int filename_length = WideCharToMultiByte( CP_UTF8, 0, ( ( fileinfo * )lvi.lParam )->filename, -1, NULL, 0, NULL, NULL );
			char *utf8_filename = ( char * )malloc( sizeof( char ) * filename_length ); // Size includes the null character.
			filename_length = WideCharToMultiByte( CP_UTF8, 0, ( ( fileinfo * )lvi.lParam )->filename, -1, utf8_filename, filename_length, NULL, NULL ) - 1;

			// The filename comes from the database entry and it could have unsupported characters.
			char *escaped_filename = escape_csv( utf8_filename );
			if ( escaped_filename != NULL )
			{
				free( utf8_filename );
				utf8_filename = escaped_filename;
			}

			int dbpath_length = WideCharToMultiByte( CP_UTF8, 0, ( ( fileinfo * )lvi.lParam )->si->dbpath, -1, NULL, 0, NULL, NULL );
			char *utf8_dbpath = ( char * )malloc( sizeof( char ) * dbpath_length ); // Size includes the null character.
			dbpath_length = WideCharToMultiByte( CP_UTF8, 0, ( ( fileinfo * )lvi.lParam )->si->dbpath, -1, utf8_dbpath, dbpath_length, NULL, NULL ) - 1;

			bool has_version = true;

			switch ( ( ( fileinfo * )lvi.lParam )->si->system )
			{
				case 1:
				{
					system_string = "Windows Me/2000";
				}
				break;

				case 2:
				{
					system_string = "Windows XP/2003";
				}
				break;

				case 3:
				{
					has_version = false;
					system_string = "Windows Vista/2008/7/8/8.1";
				}
				break;

				default:
				{
					has_version = false;
					system_string = "Unknown";
				}
				break;
			}

			// See if the next entry can fit in the buffer. If it can't, then we dump the buffer.
			if ( write_buf_offset + filename_length + dbpath_length + ( 10 * 10 ) + ( 20 * 1 ) + 26 + 30 > size )
			{
				// Dump the buffer.
				WriteFile( hFile, write_buf, write_buf_offset, &write, NULL );
				write_buf_offset = 0;
			}

			write_buf_offset += sprintf_s( write_buf + write_buf_offset, size - write_buf_offset, "\r\n\"%s\",%lu,%d in %sSAT,",
										   utf8_filename,
										   ( ( fileinfo * )lvi.lParam )->size,
										   ( ( fileinfo * )lvi.lParam )->offset,
										   ( ( ( fileinfo * )lvi.lParam )->size < ( ( fileinfo * )lvi.lParam )->si->short_sect_cutoff ? "S" : "" ) );

			if ( ( ( fileinfo * )lvi.lParam )->date_modified != 0 )
			{
				ft.dwLowDateTime = ( DWORD )( ( fileinfo * )lvi.lParam )->date_modified;
				ft.dwHighDateTime = ( DWORD )( ( ( fileinfo * )lvi.lParam )->date_modified >> 32 );
				FileTimeToSystemTime( &ft, &st );

				write_buf_offset += sprintf_s( write_buf + write_buf_offset, size - write_buf_offset, "%d/%d/%d (%02d:%02d:%02d.%d),%llu",
										   st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
										   ( ( fileinfo * )lvi.lParam )->date_modified );
			}
			else
			{
				write_buf[ write_buf_offset++ ] = ',';
			}

			if ( has_version == true )
			{
				write_buf_offset += sprintf_s( write_buf + write_buf_offset, size - write_buf_offset, ",%d: ",
										   ( ( fileinfo * )lvi.lParam )->si->version );
			}
			else
			{
				write_buf[ write_buf_offset++ ] = ',';
			}
			
			write_buf_offset += sprintf_s( write_buf + write_buf_offset, size - write_buf_offset, "%s,\"%s\"",
										   system_string,
										   utf8_dbpath );

			free( utf8_filename );
			free( utf8_dbpath );
		}

		// If there's anything remaining in the buffer, then write it to the file.
		if ( write_buf_offset > 0 )
		{
			WriteFile( hFile, write_buf, write_buf_offset, &write, NULL );
		}

		free( write_buf );

		CloseHandle( hFile );
	}

	free( save_type->filepath );
	free( save_type );

	update_menus( false );									// Enable the appropriate menu items.
	SendMessage( g_hWnd_main, WM_CHANGE_CURSOR, FALSE, 0 );	// Reset the cursor.
	EnableWindow( g_hWnd_list, TRUE );						// Allow the listview to be interactive.
	SetFocus( g_hWnd_list );								// Give focus back to the listview to allow shortcut keys. 
	SetWindowText( g_hWnd_main, PROGRAM_CAPTION );			// Reset the window title.

	// Release the mutex if we're killing the thread.
	if ( shutdown_mutex != NULL )
	{
		ReleaseSemaphore( shutdown_mutex, 1, NULL );
	}
	else if ( cmd_line == 2 )	// Exit the program if we're done saving.
	{
		// DestroyWindow won't work on a window from a different thread. So we'll send a message to trigger it.
		SendMessage( g_hWnd_main, WM_DESTROY_ALT, 0, 0 );
	}

	in_thread = false;

	// We're done. Let other threads continue.
	LeaveCriticalSection( &pe_cs );

	_endthreadex( 0 );
	return 0;
}

unsigned __stdcall save_items( void *pArguments )
{
	// This will block every other thread from entering until the first thread is complete.
	EnterCriticalSection( &pe_cs );

	in_thread = true;

	SetWindowText( g_hWnd_main, L"Thumbs Viewer - Please wait..." );	// Update the window title.
	EnableWindow( g_hWnd_list, FALSE );									// Prevent any interaction with the listview while we're processing.
	SendMessage( g_hWnd_main, WM_CHANGE_CURSOR, TRUE, 0 );				// SetCursor only works from the main thread. Set it to an arrow with hourglass.
	update_menus( true );												// Disable all processing menu items.

	save_param *save_type = ( save_param * )pArguments;

	wchar_t save_directory[ MAX_PATH ] = { 0 };
	if ( save_type->lpiidl != NULL )
	{
		// Get the directory path from the id list.
		SHGetPathFromIDList( save_type->lpiidl, save_directory );
		CoTaskMemFree( save_type->lpiidl );
	}
	else if ( save_type->filepath != NULL )
	{
		wcsncpy_s( save_directory, MAX_PATH, save_type->filepath, MAX_PATH - 1 );
	}
	
	// Depending on what was selected, get the number of items we'll be saving.
	int save_items = ( save_type->save_all == true ? SendMessage( g_hWnd_list, LVM_GETITEMCOUNT, 0, 0 ) : SendMessage( g_hWnd_list, LVM_GETSELECTEDCOUNT, 0, 0 ) );

	// Retrieve the lParam value from the selected listview item.
	LVITEM lvi = { NULL };
	lvi.mask = LVIF_PARAM;
	lvi.iItem = -1;	// Set this to -1 so that the LVM_GETNEXTITEM call can go through the list correctly.

	// Go through all the items we'll be saving.
	for ( int i = 0; i < save_items; ++i )
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			break;
		}

		lvi.iItem = ( save_type->save_all == true ? i : SendMessage( g_hWnd_list, LVM_GETNEXTITEM, lvi.iItem, LVNI_SELECTED ) );
		SendMessage( g_hWnd_list, LVM_GETITEM, 0, ( LPARAM )&lvi );

		unsigned long size = 0, header_offset = 0;	// Size excludes the header offset.
		// Create a buffer to read in our new bitmap.
		char *save_image = extract( ( ( fileinfo * )lvi.lParam ), size, header_offset );

		// Directory + backslash + filename + extension + NULL character = ( 2 * MAX_PATH ) + 6
		wchar_t fullpath[ ( 2 * MAX_PATH ) + 6 ] = { 0 };

		wchar_t *filename = NULL;
		if ( ( ( fileinfo * )lvi.lParam )->si->system == 1 || ( ( fileinfo * )lvi.lParam )->extension == 4 )
		{
			// Windows Me and 2000 will have a full path for the filename and we can assume it has a "\" in it since we look for it when detecting the system.
			filename = get_filename_from_path( ( ( fileinfo * )lvi.lParam )->filename, wcslen( ( ( fileinfo * )lvi.lParam )->filename ) );
		}
		else
		{
			filename = ( ( fileinfo * )lvi.lParam )->filename;
		}

		if ( ( ( fileinfo * )lvi.lParam )->extension == 1 || ( ( fileinfo * )lvi.lParam )->extension == 3 )
		{
			wchar_t *ext = get_extension_from_filename( filename, wcslen( filename ) );
			// The extension in the filename might not be the actual type. So we'll append .jpg to the end of it.
			if ( _wcsicmp( ext, L".jpg" ) == 0 || _wcsicmp( ext, L".jpeg" ) == 0 )
			{
				swprintf_s( fullpath, ( 2 * MAX_PATH ) + 6, L"%s\\%.259s", save_directory, filename );
			}
			else
			{
				swprintf_s( fullpath, ( 2 * MAX_PATH ) + 6, L"%s\\%.259s.jpg", save_directory, filename );
			}
		}
		else if ( ( ( fileinfo * )lvi.lParam )->extension == 2 || ( ( ( fileinfo * )lvi.lParam )->extension == 4 && header_offset > 0 ) )
		{
			wchar_t *ext = get_extension_from_filename( filename, wcslen( filename ) );
			// The extension in the filename might not be the actual type. So we'll append .png to the end of it.
			if ( _wcsicmp( ext, L".png" ) == 0 )
			{
				swprintf_s( fullpath, ( 2 * MAX_PATH ) + 6, L"%s\\%.259s", save_directory, filename );
			}
			else
			{
				swprintf_s( fullpath, ( 2 * MAX_PATH ) + 6, L"%s\\%.259s.png", save_directory, filename );
			}
		}
		else
		{
			swprintf_s( fullpath, ( 2 * MAX_PATH ) + 6, L"%s\\%.259s", save_directory, filename );
		}

		// If we have a CYMK based JPEG, then we're going to have to convert it to RGB.
		if ( ( ( fileinfo * )lvi.lParam )->extension == 3 )
		{
			Gdiplus::Image *save_bm_image = create_image( save_image + header_offset, size, 1 );

			// Get the class identifier for the JPEG encoder.
			CLSID jpgClsid;
			GetEncoderClsid( L"image/jpeg", &jpgClsid );

			Gdiplus::EncoderParameters encoderParameters;
			encoderParameters.Count = 1;
			encoderParameters.Parameter[ 0 ].Guid = Gdiplus::EncoderQuality;
			encoderParameters.Parameter[ 0 ].Type = Gdiplus::EncoderParameterValueTypeLong;
			encoderParameters.Parameter[ 0 ].NumberOfValues = 1;
			ULONG quality = 100;
			encoderParameters.Parameter[ 0 ].Value = &quality;

			// The size will differ from what's listed in the database since we had to reconstruct the image.
			// Switch the encoder to PNG or BMP to save a lossless image.
			if ( save_bm_image->Save( fullpath, &jpgClsid, &encoderParameters ) != Gdiplus::Ok )
			{
				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"An error occurred while converting the image to save.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			}

			delete save_bm_image;
		}
		else
		{
			if ( ( ( fileinfo * )lvi.lParam )->extension == 4 && header_offset > 0 )
			{
				unsigned char format = 0;
				unsigned int raw_width = 0;
				unsigned int raw_height = 0;
				unsigned int raw_size = 0;
				int raw_stride = 0;

				if ( header_offset == 0x18 )
				{
					memcpy_s( &raw_stride, sizeof( int ), save_image + ( header_offset - ( sizeof( unsigned int ) * 4 ) ), sizeof( int ) );
					memcpy_s( &raw_width, sizeof( unsigned int ), save_image + ( header_offset - ( sizeof( unsigned int ) * 3 ) ), sizeof( unsigned int ) );
					memcpy_s( &raw_height, sizeof( unsigned int ), save_image + ( header_offset - ( sizeof( unsigned int ) * 2 ) ), sizeof( unsigned int ) );
					format = 2;
				}
				else if ( header_offset == 0x34 )
				{
					memcpy_s( &raw_width, sizeof( unsigned int ), save_image + sizeof( unsigned int ), sizeof( unsigned int ) );
					memcpy_s( &raw_height, sizeof( unsigned int ), save_image + ( sizeof( unsigned int ) * 2 ), sizeof( unsigned int ) );
					memcpy_s( &raw_stride, sizeof( int ), save_image + ( sizeof( unsigned int ) * 3 ), sizeof( int ) );
					format = 3;
				}
				memcpy_s( &raw_size, sizeof( unsigned int ), save_image + ( header_offset - sizeof( unsigned int ) ), sizeof( unsigned int ) );

				Gdiplus::Image *save_bm_image = create_image( save_image + header_offset, size, format, raw_width, raw_height, raw_size, raw_stride );

				// Get the class identifier for the PNG encoder. We're going to save this as a PNG in order to preserve any alpha channels.
				CLSID pngClsid;
				GetEncoderClsid( L"image/png", &pngClsid );

				Gdiplus::EncoderParameters encoderParameters;
				encoderParameters.Count = 1;
				encoderParameters.Parameter[ 0 ].Guid = Gdiplus::EncoderQuality;
				encoderParameters.Parameter[ 0 ].Type = Gdiplus::EncoderParameterValueTypeLong;
				encoderParameters.Parameter[ 0 ].NumberOfValues = 1;
				ULONG quality = 100;
				encoderParameters.Parameter[ 0 ].Value = &quality;

				// The size will differ from what's listed in the database since we had to reconstruct the image.
				if ( save_bm_image->Save( fullpath, &pngClsid, &encoderParameters ) != Gdiplus::Ok )
				{
					if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"An error occurred while converting the image to save.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
				}

				delete save_bm_image;
			}
			else
			{
				// Attempt to open a file for saving.
				HANDLE hFile_save = CreateFile( fullpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
				if ( hFile_save != INVALID_HANDLE_VALUE )
				{
					// Write the buffer to our file.
					DWORD dwBytesWritten = 0;
					WriteFile( hFile_save, save_image + header_offset, size, &dwBytesWritten, NULL );

					CloseHandle( hFile_save );
				}

				// See if the path was too long.
				if ( GetLastError() == ERROR_PATH_NOT_FOUND )
				{
					if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"One or more files could not be saved. Please check the filename and path.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
				}
			}
		}
		// Free our buffer.
		free( save_image );
	}

	free( save_type->filepath );
	free( save_type );

	update_menus( false );									// Enable the appropriate menu items.
	SendMessage( g_hWnd_main, WM_CHANGE_CURSOR, FALSE, 0 );	// Reset the cursor.
	EnableWindow( g_hWnd_list, TRUE );						// Allow the listview to be interactive.
	SetFocus( g_hWnd_list );								// Give focus back to the listview to allow shortcut keys. 
	SetWindowText( g_hWnd_main, PROGRAM_CAPTION );			// Reset the window title.

	// Release the mutex if we're killing the thread.
	if ( shutdown_mutex != NULL )
	{
		ReleaseSemaphore( shutdown_mutex, 1, NULL );
	}
	else if ( cmd_line == 2 )	// Exit the program if we're done saving.
	{
		// DestroyWindow won't work on a window from a different thread. So we'll send a message to trigger it.
		SendMessage( g_hWnd_main, WM_DESTROY_ALT, 0, 0 );
	}

	in_thread = false;

	// We're done. Let other threads continue.
	LeaveCriticalSection( &pe_cs );

	_endthreadex( 0 );
	return 0;
}

// Extract the file from the SAT or short stream container.
char *extract( fileinfo *fi, unsigned long &size, unsigned long &header_offset )
{
	char *buf = NULL;

	if ( fi->entry_type == 2 )
	{
		// See if the stream is in the SAT.
		if ( fi->size >= fi->si->short_sect_cutoff && fi->si->sat != NULL )
		{
			DWORD read = 0, total = 0;
			long sat_index = fi->offset; 
			unsigned long sector_offset = fi->si->sect_size + ( sat_index * fi->si->sect_size );
			unsigned long bytes_to_read = fi->si->sect_size;

			bool exit_extract = false;

			// Attempt to open a file for reading.
			HANDLE hFile = CreateFile( fi->si->dbpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
			if ( hFile == INVALID_HANDLE_VALUE )
			{
				return NULL;
			}

			buf = ( char * )malloc( sizeof( char ) * fi->size );
			memset( buf, 0, sizeof( char ) * fi->size );

			while ( total < fi->size )
			{
				// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
				if ( sat_index < 0 )
				{
					if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
					break;
				}
				
				// Each index should be no greater than the size of the SAT array.
				if ( sat_index > ( long )( fi->si->num_sat_sects * ( fi->si->sect_size / sizeof( long ) ) ) )
				{
					if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
					exit_extract = true;
				}

				// Adjust the file pointer to the Short SAT
				SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );

				if ( exit_extract == false )
				{
					sector_offset = fi->si->sect_size + ( fi->si->sat[ sat_index ] * fi->si->sect_size );
				}

				if ( total + fi->si->sect_size > fi->size )
				{
					bytes_to_read = fi->size - total;
				}

				ReadFile( hFile, buf + total, bytes_to_read, &read, NULL );
				total += read;

				if ( read < bytes_to_read )
				{
					if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Premature end of file encountered while extracting the file.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
					break;
				}

				if ( exit_extract == true )
				{
					break;
				}

				sat_index = fi->si->sat[ sat_index ];
			}

			CloseHandle( hFile );

			header_offset = 0;
			if ( total > sizeof( unsigned long ) )
			{
				memcpy_s( &header_offset, sizeof( unsigned long ), buf, sizeof( unsigned long ) );

				if ( header_offset > total )
				{
					header_offset = 0;
				}
			}

			size = total - header_offset;

			// See if there's a second header.
			// The first header will look like this:
			// Header length (4 bytes) - I wonder if this value also dictates the content type?
			// Some value (4 bytes)
			// Content length (4 bytes)
			if ( size > 2 && memcmp( buf + header_offset, "\xFF\xD8", 2 ) != 0 )
			{
				// Second header exists. Reconstruct the image.
				// The second header will look like this:
				// Some value (4 bytes)
				// Content length (4 bytes)
				// Image width (4 bytes)
				// Image height (4 bytes)
				unsigned long second_header = 0;
				memcpy_s( &second_header, sizeof( unsigned long ), buf + header_offset, sizeof( unsigned long ) );
				if ( second_header == 1 )
				{
					char *buf2 = ( char * )malloc( sizeof( char ) * total + 374 - 30 );

					memcpy_s( buf2, total + 374 - 30, jfif_header, 20 );
					memcpy_s( buf2 + 20, total + 374 - 30 - 20, quantization, 138 );
					memcpy_s( buf2 + 158, total + 374 - 30 - 158, buf + 30, 22 );
					memcpy_s( buf2 + 180, total + 374 - 30 - 180, huffman_table, 216 );
					memcpy_s( buf2 + 396, total + 374 - 30 - 396, buf + 52, total - 52 );

					free( buf );

					buf = buf2;

					header_offset = 0;

					size = total + 374 - 30;

					fi->extension = 3;	// CMYK JPEG
				}
			}
		}
		else if ( fi->si->short_stream_container != NULL && fi->si->ssat != NULL )	// Stream is in the short stream.
		{
			DWORD read = 0;
			long ssat_index = fi->offset;
			unsigned long sector_offset = 0;

			buf = ( char * )malloc( sizeof( char ) * fi->size );
			memset( buf, 0, sizeof( char ) * fi->size );

			unsigned long buf_offset = 0;
			unsigned long bytes_to_read = 64;
			while ( buf_offset < fi->size )
			{
				// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
				if ( ssat_index < 0 )
				{
					if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Invalid Short SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
					break;
				}
				
				// Each index should be no greater than the size of the Short SAT array.
				if ( ssat_index > ( long )( fi->si->num_ssat_sects * ( fi->si->sect_size / sizeof( long ) ) ) )
				{
					if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Short SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
					break;
				}

				sector_offset = ssat_index * 64;

				if ( buf_offset + 64 > fi->size )
				{
					bytes_to_read = fi->size - buf_offset;
				}

				memcpy_s( buf + buf_offset, fi->size - buf_offset, fi->si->short_stream_container + sector_offset, bytes_to_read );
				buf_offset += bytes_to_read;

				ssat_index = fi->si->ssat[ ssat_index ];
			}

			header_offset = 0;
			if ( buf_offset > sizeof( unsigned long ) )
			{
				memcpy_s( &header_offset, sizeof( unsigned long ), buf, sizeof( unsigned long ) );

				if ( header_offset > fi->size )
				{
					header_offset = 0;
				}
			}

			size = fi->size - header_offset;

			// The first header will look like this:
			// Header length (4 bytes) - I wonder if this value also dictates the content type?
			// Some value (4 bytes)
			// Content length (4 bytes)
			// See if there's a second header.
			if ( size > 2 && memcmp( buf + header_offset, "\xFF\xD8", 2 ) != 0 )
			{
				// The second header will look like this:
				// Some value (4 bytes)
				// Content length (4 bytes)
				// Image width (4 bytes)
				// Image height (4 bytes)
				// Second header exists. Reconstruct the image.
				unsigned long second_header = 0;
				memcpy_s( &second_header, sizeof( unsigned long ), buf + header_offset, sizeof( unsigned long ) );
				if ( second_header == 1 )
				{
					char *buf2 = ( char * )malloc( sizeof( char ) * fi->size + 374 - 30 );

					memcpy_s( buf2, fi->size + 374 - 30, jfif_header, 20 );
					memcpy_s( buf2 + 20, fi->size + 374 - 30 - 20, quantization, 138 );
					memcpy_s( buf2 + 158, fi->size + 374 - 30 - 158, buf + 30, 22 );
					memcpy_s( buf2 + 180, fi->size + 374 - 30 - 180, huffman_table, 216 );
					memcpy_s( buf2 + 396, fi->size + 374 - 30 - 396, buf + 52, fi->size - 52 );

					free( buf );

					buf = buf2;

					header_offset = 0;

					size = fi->size + 374 - 30;

					fi->extension = 3;	// CMYK JPEG
				}
			}
		}
	}

	// Set the extension if none has been set.
	if ( fi->extension == 0 && buf != NULL )
	{
		// Detect the file extension and copy it into the filename string.
		if ( size > 4 && memcmp( buf + header_offset, FILE_TYPE_JPEG, 4 ) == 0 )		// First 4 bytes
		{
			fi->extension = 1;
		}
		else if ( size > 8 && memcmp( buf + header_offset, FILE_TYPE_PNG, 8 ) == 0 )	// First 8 bytes
		{
			fi->extension = 2;
		}
		else
		{
			fi->extension = 4;	// Unknown
		}
	}

	return buf;
}

// Entries that exist in the catalog will be updated.
// Me, and 2000 will have full paths.
// XP and 2003 will just have the file name.
// Windows Vista, 2008, and 7 don't appear to have catalogs.
char update_catalog_entries( HANDLE hFile, fileinfo *fi, directory_header dh )
{
	char *buf = NULL;
	if ( dh.stream_length >= fi->si->short_sect_cutoff && fi->si->sat != NULL )
	{
		DWORD read = 0, total = 0;
		long sat_index = dh.first_stream_sect; 
		unsigned long sector_offset = fi->si->sect_size + ( sat_index * fi->si->sect_size );
		unsigned long bytes_to_read = fi->si->sect_size;

		bool exit_update = false;

		buf = ( char * )malloc( sizeof( char ) * dh.stream_length );
		memset( buf, 0, sizeof( char ) * dh.stream_length );

		while ( total < dh.stream_length )
		{
			// Stop processing and exit the thread.
			if ( kill_thread == true )
			{
				free( buf );
				return SC_QUIT;
			}

			// The SAT should terminate with -2, but we shouldn't get here before the for loop completes.
			if ( sat_index < 0 )
			{
				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
				break;
			}
			
			// Each index should be no greater than the size of the SAT array.
			if ( sat_index > ( long )( fi->si->num_sat_sects * ( fi->si->sect_size / sizeof( long ) ) ) )
			{
				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
				exit_update = true;
			}

			// Adjust the file pointer to the SAT
			SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );

			if ( exit_update == false )
			{
				sector_offset = fi->si->sect_size + ( fi->si->sat[ sat_index ] * fi->si->sect_size );
			}

			if ( total + fi->si->sect_size > dh.stream_length )
			{
				bytes_to_read = dh.stream_length - total;
			}

			ReadFile( hFile, buf + total, bytes_to_read, &read, NULL );
			total += read;

			if ( read < bytes_to_read )
			{
				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Premature end of file encountered while updating the directory.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
				break;
			}

			if ( exit_update == true )
			{
				break;
			}

			sat_index = fi->si->sat[ sat_index ];
		}
	}
	else if ( fi->si->short_stream_container != NULL && fi->si->ssat != NULL )
	{
		long ssat_index = dh.first_stream_sect;
		unsigned long sector_offset = 0;

		buf = ( char * )malloc( sizeof( char ) * dh.stream_length );
		memset( buf, 0, sizeof( char ) * dh.stream_length );

		unsigned long buf_offset = 0;
		unsigned long bytes_to_read = 64;
		while ( buf_offset < dh.stream_length )
		{
			// Stop processing and exit the thread.
			if ( kill_thread == true )
			{
				free( buf );
				return SC_QUIT;
			}

			// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
			if ( ssat_index < 0 )
			{
				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Invalid Short SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
				break;
			}
			
			// Each index should be no greater than the size of the Short SAT array.
			if ( ssat_index > ( long )( fi->si->num_ssat_sects * ( fi->si->sect_size / sizeof( long ) ) ) )
			{
				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Short SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
				break;
			}

			sector_offset = ssat_index * 64;

			if ( buf_offset + 64 > dh.stream_length )
			{
				bytes_to_read = dh.stream_length - buf_offset;
			}

			memcpy_s( buf + buf_offset, dh.stream_length - buf_offset, fi->si->short_stream_container + sector_offset, bytes_to_read );
			buf_offset += bytes_to_read;

			ssat_index = fi->si->ssat[ ssat_index ];
		}
	}

	if ( buf != NULL && dh.stream_length > ( 2 * sizeof( unsigned short ) ) )
	{
		// 2 byte offset, 2 byte version, 4 bytes number of entries.
		unsigned long offset = 0;
		memcpy_s( &offset, sizeof( unsigned long ), buf, sizeof( unsigned short ) );
		memcpy_s( &fi->si->version, sizeof( unsigned short ), buf + sizeof( unsigned short ), sizeof( unsigned short ) );

		fileinfo *root_fi = fi;
		fileinfo *last_fi = NULL;
		wchar_t sid[ 32 ];

		while ( offset < dh.stream_length )
		{
			// Stop processing and exit the thread.
			if ( kill_thread == true )
			{
				free( buf );
				return SC_QUIT;
			}

			// We may have to scan the linked list for the next item. Reset it to the last item before the scan.
			if ( fi == NULL )
			{
				if ( last_fi != NULL )
				{
					fi = last_fi;
				}
				else
				{
					break;	// If no last info was set, then we can't continue.
				}
			}

			unsigned long entry_length = 0;
			memcpy_s( &entry_length, sizeof( unsigned long ), buf + offset, sizeof( unsigned long ) );
			offset += sizeof( long );
			unsigned long entry_num = 0;
			memcpy_s( &entry_num, sizeof( unsigned long ), buf + offset, sizeof( unsigned long ) );
			offset += sizeof( long );
			__int64 date_modified = 0;
			memcpy_s( &date_modified, sizeof( __int64 ), buf + offset, 8 );
			offset += sizeof( __int64 );

			// It seems that version 4 databases have an additional value before the filename.
			if ( fi->si->sect_size == 4096 )
			{
				unsigned long unknown = 0;	// Padding?
				memcpy_s( &unknown, sizeof( unsigned long ), buf + offset, sizeof( unsigned long ) );
				offset += sizeof( long );

				entry_length -= sizeof( unsigned long );
			}

			unsigned long name_length = entry_length - 0x14;

			if ( name_length > dh.stream_length )
			{
				free( buf );
				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Invalid directory entry.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
				return SC_FAIL;
			}

			wchar_t *original_name = ( wchar_t * )malloc( name_length + sizeof( wchar_t ) );
			wcsncpy_s( original_name, ( name_length + sizeof( wchar_t ) ) / sizeof( wchar_t ), ( wchar_t * )( buf + offset ), name_length / sizeof( wchar_t ) );

			if ( fi != NULL )
			{
				// We need to verify that the entry number and sid match.
				// The catalog entries generally appear to be in order, but the actual content in our linked list might not be. I've seen this in ehthumbs.db files.
				swprintf_s( sid, 32, L"%u", ( fi->si->version == 1 ? entry_num * 10 : entry_num ) );	// The entry number needs to be multiplied by 10 if the version is 1.
				reverse_string( sid );
				if ( wcscmp( sid, fi->filename ) != 0 )
				{
					last_fi = fi;

					// If we used a tree instead of a linked list, this would be much faster, but it's overkill since this isn't common.
					fileinfo *temp_fi = root_fi;
					while ( temp_fi != NULL )
					{
						if ( wcscmp( sid, temp_fi->filename ) == 0 )
						{
							fi = temp_fi;
							break;
						}

						temp_fi = temp_fi->next;
					}
				}

				fi->date_modified = date_modified;
				free( fi->filename );
				fi->filename = original_name;

				// There's no documentation on this and it's difficult to find test cases. Anyone want to install Windows Me? I didn't think so.
				// I can't refine this until I get test cases, but this should suffice for now.
				switch ( fi->si->version )
				{
					case 4:	// 2000?
					{
						fi->si->system = 1;	// Me, 2000
					}
					break;

					case 1: // Windows Media Center edition (XP, Vista, 7) has ehthumbs.db, ehthumbs_vista.db, Image.db, Video.db, etc. Are there version 1 databases not found on WMC systems?
					case 5:	// XP - no SP?
					case 6:	// XP - SP1?
					case 7:	// XP - SP2+?
					{
						fi->si->system = 2;	// XP, 2003
					}
					break;

					default:	// Fall back to our old method of detection.
					{
						// See if the filename contains a path. ":\" should be enough to signify a path.
						if ( name_length > 2 && fi->filename[ 1 ] == L':' && fi->filename[ 2 ] == L'\\' )
						{
							fi->si->system = 1;	// Me, 2000
						}
						else
						{
							fi->si->system = 2;	// XP, 2003
						}
					}
					break;
				}

				fi = fi->next;
			}

			offset += ( name_length + 4 );
		}
		
		free( buf );
	}

	InvalidateRect( g_hWnd_list, NULL, TRUE );

	return SC_OK;
}

// Save the short stream container for later lookup.
// This is always located in the SAT.
char cache_short_stream_container( HANDLE hFile, directory_header dh, shared_info *g_si )
{
	// Make sure we have a short stream container.
	if ( dh.stream_length <= 0 || dh.first_stream_sect < 0 )
	{
		return SC_OK;
	}

	DWORD read = 0, total = 0;
	long sat_index = dh.first_stream_sect; 
	unsigned long sector_offset = g_si->sect_size + ( sat_index * g_si->sect_size );
	unsigned long bytes_to_read = g_si->sect_size;

	bool exit_cache = false;

	g_si->short_stream_container = ( char * )malloc( sizeof( char ) * dh.stream_length );
	memset( g_si->short_stream_container, 0, sizeof( char ) * dh.stream_length );

	while ( total < dh.stream_length )
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			return SC_QUIT;
		}

		// The SAT should terminate with -2, but we shouldn't get here before the for loop completes.
		if ( sat_index < 0 )
		{
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * ( g_si->sect_size / sizeof( long ) ) ) )
		{
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			exit_cache = true;
		}

		// Adjust the file pointer to the Short SAT
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );

		// Adjust the offset if we have a valid sat index.
		if ( exit_cache == false )
		{
			sector_offset = g_si->sect_size + ( g_si->sat[ sat_index ] * g_si->sect_size );
		}

		if ( total + g_si->sect_size > dh.stream_length )
		{
			bytes_to_read = dh.stream_length - total;
		}

		ReadFile( hFile, g_si->short_stream_container + total, bytes_to_read, &read, NULL );
		total += read;

		if ( read < bytes_to_read )
		{
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Premature end of file encountered while building the short stream container.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}

		if ( exit_cache == true )
		{
			return SC_FAIL;
		}

		sat_index = g_si->sat[ sat_index ];
	}

	return SC_OK;
}

// Builds a list of directory entries.
// This list is found by traversing the SAT.
// The directory is stored as a red-black tree in the database, but we can simply iterate through it with a linked list.
char build_directory( HANDLE hFile, shared_info *g_si )
{
	DWORD read = 0;
	long sat_index = g_si->first_dir_sect;
	unsigned long sector_offset = g_si->sect_size + ( sat_index * g_si->sect_size );
	unsigned long sector_count = 0;

	bool root_found = false;
	directory_header root_dh = { 0 };

	bool catalog_found = false;
	directory_header catalog_dh = { 0 };

	fileinfo *g_fi = NULL;
	fileinfo *last_fi = NULL;

	bool exit_build = false;

	int item_count = SendMessage( g_hWnd_list, LVM_GETITEMCOUNT, 0, 0 ); // We don't need to call this for each item.

	// Save each directory sector from the SAT. The number of directory list sectors is not known for Version 3 databases.
	while ( sector_count < ( g_si->num_sat_sects * ( g_si->sect_size / sizeof( long ) ) ) )
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			if ( g_fi == NULL )
			{
				free( g_si->sat );
				free( g_si->ssat );
				free( g_si );
			}

			return SC_QUIT;
		}

		// The directory list should terminate with -2.
		if ( sat_index < 0 )
		{
			if ( sat_index == -2 )
			{
				if ( g_fi != NULL )
				{
					g_fi->si->system = 3;	// Assume the system is Vista/2008/7
				}
				else
				{
					if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"No entries were found.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
				}
			}
			else
			{
				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			}

			break;
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * ( g_si->sect_size / sizeof( long ) ) ) )
		{
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			exit_build = true;
		}

		// Adjust the file pointer to the Short SAT
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );

		if ( exit_build == false )
		{
			sector_offset = g_si->sect_size + ( g_si->sat[ sat_index ] * g_si->sect_size );
		}

		// There are 4 directory items per 512 byte sector.
		for ( int i = 0; i < ( g_si->sect_size / 128 ); i++ )
		{
			// Stop processing and exit the thread.
			if ( kill_thread == true )
			{
				if ( g_fi == NULL )
				{
					free( g_si->sat );
					free( g_si->ssat );
					free( g_si );
				}

				return SC_QUIT;
			}

			directory_header dh;
			ReadFile( hFile, &dh, sizeof( directory_header ), &read, NULL );

			if ( read < sizeof( directory_header ) )
			{
				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Premature end of file encountered while building the directory.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
				exit_build = true;
				break;
			}

			// Skip invalid entries.
			if ( dh.entry_type == 0 )
			{
				continue;
			}

			if ( dh.entry_type == 5 )
			{
				root_dh = dh;			// Save the root entry
				root_found = true;
				continue;
			}

			if ( catalog_found == false && wcsncmp( dh.sid, L"Catalog", 32 ) == 0 )
			{
				catalog_dh = dh;		// Save the catalog entry
				catalog_found = true;	// Short circuit the condition above.
				continue;
			}

			// dh.create_time never seems to be set.
			fileinfo *fi = ( fileinfo * )malloc( sizeof( fileinfo ) );
			fi->filename = ( wchar_t * )malloc( sizeof( wchar_t ) * 32 );
			wcsncpy_s( fi->filename, 32, dh.sid, 31 );
			memcpy_s( &fi->date_modified, sizeof( __int64 ), dh.modify_time, 8 );
			fi->offset = dh.first_stream_sect;
			fi->size = dh.stream_length;
			fi->entry_type = dh.entry_type;
			fi->extension = 0;		// None set.
			fi->si = g_si;
			fi->si->version = 0;	// Unknown until/if we process a catalog entry.
			fi->si->system = 0;		// Unknown until/if we process a catalog entry.
			fi->si->count++;		// Increment the number of entries.
			fi->next = NULL;
			fi->entry_hash = 0;
			fi->mapped = false;

			// Store the fileinfo in the list (first in, first out)
			if ( last_fi != NULL )
			{
				last_fi->next = fi;
			}
			else
			{
				g_fi = fi;
			}
			last_fi = fi;

			// Insert a row into our listview.
			LVITEM lvi = { NULL };
			lvi.mask = LVIF_PARAM; // Our listview items will display the text contained the lParam value.
			lvi.iItem = item_count++;
			lvi.iSubItem = 0;
			lvi.lParam = ( LPARAM )fi;
			SendMessage( g_hWnd_list, LVM_INSERTITEM, 0, ( LPARAM )&lvi );
		}

		if ( exit_build == true )
		{
			break;
		}

		// Each index points to the next index.
		sat_index = g_si->sat[ sat_index ];
		++sector_count;	// Update the number of sectors we've traversed.
	}

	if ( g_fi != NULL )
	{
		if ( root_found == true )
		{
			cache_short_stream_container( hFile, root_dh, g_si );
		}

		if ( catalog_found == true )
		{
			update_catalog_entries( hFile, g_fi, catalog_dh );
		}
	}
	else	// Free our shared info structure if no item was added to the list.
	{
		free( g_si->sat );
		free( g_si->ssat );
		free( g_si );
	}

	return SC_OK;
}

// Builds the Short SAT.
// This table is found by traversing the SAT.
char build_ssat( HANDLE hFile, shared_info *g_si )
{
	DWORD read = 0, total = 0;
	long sat_index = g_si->first_ssat_sect;
	unsigned long sector_offset = g_si->sect_size + ( sat_index * g_si->sect_size );

	bool exit_build = false;

	g_si->ssat = ( long * )malloc( ssat_size );
	memset( g_si->ssat, -1, ssat_size );

	// Save each sector from the SAT.
	for ( unsigned long i = 0; i < g_si->num_ssat_sects; i++ )
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			free( g_si->sat );
			free( g_si->ssat );
			free( g_si );

			return SC_QUIT;
		}

		// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
		if ( sat_index < 0 )
		{
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * ( g_si->sect_size / sizeof( long ) ) ) )
		{
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			exit_build = true;
		}

		// Adjust the file pointer to the Short SAT
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );

		if ( exit_build == false )
		{
			sector_offset = g_si->sect_size + ( g_si->sat[ sat_index ] * g_si->sect_size );
		}

		ReadFile( hFile, g_si->ssat + ( total / sizeof( long ) ), g_si->sect_size, &read, NULL );
		total += read;

		if ( read < g_si->sect_size )
		{
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Premature end of file encountered while building the Short SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}

		if ( exit_build == true )
		{
			return SC_FAIL;
		}

		// Each index points to the next index.
		sat_index = g_si->sat[ sat_index ];
	}

	return SC_OK;
}

// Builds the SAT.
// We concatenate each sector listed in the MSAT to build the SAT.
char build_sat( HANDLE hFile, shared_info *g_si )
{
	DWORD read = 0, total = 0;
	unsigned long sector_offset = 0;

	g_si->sat = ( long * )malloc( sat_size );
	memset( g_si->sat, -1, sat_size );

	// Save each sector in the Master SAT.
	for ( unsigned long msat_index = 0; msat_index < g_si->num_sat_sects; msat_index++ )
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			free( g_si->sat );
			free( g_si );

			return SC_QUIT;
		}

		// We shouldn't get here before the for loop completes.
		if ( g_msat[ msat_index ] < 0 )
		{
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Invalid Master SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}

		// Adjust the file pointer to the SAT
		sector_offset = g_si->sect_size + ( g_msat[ msat_index ] * g_si->sect_size );
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );

		ReadFile( hFile, g_si->sat + ( total / sizeof( long ) ), g_si->sect_size, &read, NULL );
		total += read;

		if ( read < g_si->sect_size )
		{
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Premature end of file encountered while building the SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}
	}

	return SC_OK;
}

// Builds the MSAT.
// This is only used to build the SAT and nothing more.
char build_msat( HANDLE hFile, shared_info *g_si )
{
	DWORD read = 0, total = 436;	// If the sector size is 4096 bytes, then the remaining 3585 bytes are filled with 0.
	unsigned long last_sector = g_si->sect_size + ( g_si->first_dis_sect * g_si->sect_size );	// Offset to the next DISAT (double indirect sector allocation table)

	g_msat = ( long * )malloc( msat_size );	// g_msat is freed in our read database function.
	memset( g_msat, -1, msat_size );

	// Set the file pionter to the beginning of the master sector allocation table.
	SetFilePointer( hFile, 76, 0, FILE_BEGIN );

	// The first MSAT (contained within the 512 byte header) is 436 bytes. Every other MSAT will be 512 or 4096 bytes.
	ReadFile( hFile, g_msat, 436, &read, NULL );

	if ( read < 436 )
	{
		if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Premature end of file encountered while building the Master SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
		return SC_FAIL;
	}

	// If there are DISATs, then we'll add them to the MSAT list.
	for ( unsigned long i = 0; i < g_si->num_dis_sects; i++ )
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			free( g_si );

			return SC_QUIT;
		}

		SetFilePointer( hFile, last_sector, 0, FILE_BEGIN );

		// Read the first 127 or 1023 SAT sectors (508 or 4092 bytes) in the DISAT.
		ReadFile( hFile, g_msat + ( total / sizeof( long ) ), g_si->sect_size - sizeof( long ), &read, NULL );
		total += read;

		if ( read < g_si->sect_size - sizeof( long ) )
		{
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Premature end of file encountered while building the Master SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}

		// Get the pointer to the next DISAT.
		ReadFile( hFile, &last_sector, 4, &read, NULL );

		if ( read < 4 )
		{
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Premature end of file encountered while building the Master SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}

		// The last index in the DISAT contains a pointer to the next DISAT. That's assuming there's any more DISATs left.
		last_sector = g_si->sect_size + ( last_sector * g_si->sect_size );
	}

	return SC_OK;
}

unsigned __stdcall read_database( void *pArguments )
{
	// This will block every other thread from entering until the first thread is complete.
	// Protects our global variables.
	EnterCriticalSection( &pe_cs );

	in_thread = true;

	SetWindowText( g_hWnd_main, L"Thumbs Viewer - Please wait..." );	// Update the window title.
	EnableWindow( g_hWnd_list, FALSE );									// Prevent any interaction with the listview while we're processing.
	SendMessage( g_hWnd_main, WM_CHANGE_CURSOR, TRUE, 0 );				// SetCursor only works from the main thread. Set it to an arrow with hourglass.
	update_menus( true );												// Disable all processing menu items.

	pathinfo *pi = ( pathinfo * )pArguments;

	int fname_length = 0;
	wchar_t *fname = pi->filepath + pi->offset;

	int filepath_length = wcslen( pi->filepath ) + 1;	// Include NULL character.
	
	bool construct_filepath = ( filepath_length > pi->offset && cmd_line == 0 ? false : true );

	wchar_t *filepath = NULL;

	// We're going to open each file in the path info.
	do
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			break;
		}

		// Construct the filepath for each file.
		if ( construct_filepath == true )
		{
			fname_length = wcslen( fname ) + 1;	// Include '\' character or NULL character

			if ( cmd_line != 0 )
			{
				filepath = ( wchar_t * )malloc( sizeof( wchar_t ) * fname_length );
				wcscpy_s( filepath, fname_length, fname );
			}
			else
			{
				filepath = ( wchar_t * )malloc( sizeof( wchar_t ) * ( filepath_length + fname_length ) );
				swprintf_s( filepath, filepath_length + fname_length, L"%s\\%s", pi->filepath, fname );
			}

			// Move to the next file name.
			fname += fname_length;
		}
		else	// Copy the filepath.
		{
			filepath = ( wchar_t * )malloc( sizeof( wchar_t ) * filepath_length );
			wcscpy_s( filepath, filepath_length, pi->filepath );
		}

		// Attempt to open our database file.
		HANDLE hFile = CreateFile( filepath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
		if ( hFile != INVALID_HANDLE_VALUE )
		{
			DWORD read = 0;
			LARGE_INTEGER f_size = { 0 };

			database_header dh = { 0 };

			// Get the header information for this database.
			ReadFile( hFile, &dh, sizeof( database_header ), &read, NULL );

			if ( read < sizeof( database_header ) )
			{
				CloseHandle( hFile );
				free( filepath );

				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"Premature end of file encountered while reading the header.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }

				continue;
			}

			// Make sure it's a thumbs database and the stucture was filled correctly.
			if ( memcmp( dh.magic_identifier, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8 ) != 0 )
			{
				CloseHandle( hFile );
				free( filepath );

				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"The file is not a thumbs database.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }

				continue;
			}

			// These values are the minimum at which we can multiply the sector size (512) and not go out of range.
			if ( dh.num_sat_sects > 0x7FFFFF || dh.num_ssat_sects > 0x7FFFFF || dh.num_dis_sects > 0x810203 )
			{
				CloseHandle( hFile );
				free( filepath );

				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"The total sector allocation table size is too large.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }

				continue;
			}

			// The sector size is equivalent to the 2 to the power of sector_shift. Version 3 = 2^9 = 512. Version 4 = 2^12 = 4096. We'll default to 512 if it's not version 4.
			unsigned short sect_size = ( dh.dll_version == 0x0004 && dh.sector_shift == 0x000C ? 4096 : 512 );

			msat_size = sizeof( long ) * ( 109 + ( ( dh.num_dis_sects > 0 ? dh.num_dis_sects : 0 ) * ( ( sect_size / sizeof( long ) ) - 1 ) ) );
			sat_size = ( dh.num_sat_sects > 0 ? dh.num_sat_sects : 0 ) * sect_size;
			ssat_size = ( dh.num_ssat_sects > 0 ? dh.num_ssat_sects : 0 ) * sect_size;

			GetFileSizeEx( hFile, &f_size );

			// This is a simple check to make sure we don't allocate too much memory.
			if ( ( msat_size + sat_size + ssat_size ) > f_size.QuadPart )
			{
				CloseHandle( hFile );
				free( filepath );

				if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"The total sector allocation table size exceeds the size of the database.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }

				continue;
			}

			// This information is shared between entries within the database.
			shared_info *si = ( shared_info * )malloc( sizeof( shared_info ) );
			si->sat = NULL;
			si->ssat = NULL;
			si->short_stream_container = NULL;
			si->count = 0;
			si->sect_size = sect_size;
			si->first_dir_sect = dh.first_dir_sect;
			si->first_dis_sect = dh.first_dis_sect;
			si->first_ssat_sect = dh.first_ssat_sect;
			si->num_ssat_sects = dh.num_ssat_sects;
			si->num_dis_sects = dh.num_dis_sects;
			si->num_sat_sects = dh.num_sat_sects;
			si->short_sect_cutoff = dh.short_sect_cutoff;
			
			wcscpy_s( si->dbpath, MAX_PATH, filepath );

			// Short-circuit the remaining functions if the status code is quit. The functions must be called in this order.
			if ( build_msat( hFile, si ) != SC_QUIT && build_sat( hFile, si ) != SC_QUIT && build_ssat( hFile, si ) != SC_QUIT && build_directory( hFile, si ) != SC_QUIT ){}

			// We no longer need this table.
			free( g_msat );

			// Close the input file.
			CloseHandle( hFile );
		}
		else
		{
			// If this occurs, then there's something wrong with the user's system.
			if ( cmd_line != 2 ){ MessageBox( g_hWnd_main, L"The database file failed to open.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING ); }
		}

		// Free the old filepath.
		free( filepath );
	}
	while ( construct_filepath == true && *fname != L'\0' );

	// Save the files or a CSV if the user specified an output directory through the command-line.
	if ( pi->output_path != NULL )
	{
		save_param *save_type = ( save_param * )malloc( sizeof( save_param ) );	// Freed in the called threads.
		save_type->lpiidl = NULL;
		save_type->save_all = true;

		if ( pi->type == 0 )	// Save thumbnail images.
		{
			wchar_t output_path[ MAX_PATH ] = { 0 };
			// Create and set the directory that we'll be outputting files to.
			if ( GetFileAttributes( pi->output_path ) == INVALID_FILE_ATTRIBUTES )
			{
				CreateDirectory( pi->output_path, NULL );
			}

			SetCurrentDirectory( pi->output_path );			// Set the path (relative or full)
			GetCurrentDirectory( MAX_PATH, output_path );	// Get the full path

			save_type->filepath = _wcsdup( output_path );

			free( pi->output_path );

			CloseHandle( ( HANDLE )_beginthreadex( NULL, 0, &save_items, ( void * )save_type, 0, NULL ) );
		}
		else	// Save CSV.
		{
			save_type->filepath = pi->output_path;

			CloseHandle( ( HANDLE )_beginthreadex( NULL, 0, &save_csv, ( void * )save_type, 0, NULL ) );
		}
	}

	// Free the path info.
	free( pi->filepath );
	free( pi );

	update_menus( false );									// Enable the appropriate menu items.
	SendMessage( g_hWnd_main, WM_CHANGE_CURSOR, FALSE, 0 );	// Reset the cursor.
	EnableWindow( g_hWnd_list, TRUE );						// Allow the listview to be interactive.
	SetFocus( g_hWnd_list );								// Give focus back to the listview to allow shortcut keys. 
	SetWindowText( g_hWnd_main, PROGRAM_CAPTION );			// Reset the window title.

	// Release the mutex if we're killing the thread.
	if ( shutdown_mutex != NULL )
	{
		ReleaseSemaphore( shutdown_mutex, 1, NULL );
	}

	in_thread = false;

	// We're done. Let other threads continue.
	LeaveCriticalSection( &pe_cs );

	_endthreadex( 0 );
	return 0;
}
