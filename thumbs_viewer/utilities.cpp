/*
    thumbs_viewer will extract thumbnail images from thumbs database files.
    Copyright (C) 2011-2015 Eric Kutcher

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
#include "utilities.h"
#include "read_thumbs.h"
#include "menus.h"

#include <stdio.h>

HANDLE shutdown_semaphore = NULL;	// Blocks shutdown while a worker thread is active.
bool g_kill_thread = false;			// Allow for a clean shutdown.

CRITICAL_SECTION pe_cs;				// Queues additional worker threads.
bool in_thread = false;				// Flag to indicate that we're in a worker thread.
bool skip_draw = false;				// Prevents WM_DRAWITEM from accessing listview items while we're removing them.

dllrbt_tree *fileinfo_tree = NULL;	// Red-black tree of fileinfo structures.

bool is_close( int a, int b )
{
	// See if the distance between two points is less than the snap width.
	return abs( a - b ) < SNAP_WIDTH;
}

void Processing_Window( bool enable )
{
	if ( enable == true )
	{
		SetWindowTextA( g_hWnd_main, "Thumbs Viewer - Please wait..." );	// Update the window title.
		EnableWindow( g_hWnd_list, FALSE );									// Prevent any interaction with the listview while we're processing.
		SendMessage( g_hWnd_main, WM_CHANGE_CURSOR, TRUE, 0 );				// SetCursor only works from the main thread. Set it to an arrow with hourglass.
		UpdateMenus( UM_DISABLE );											// Disable all processing menu items.
	}
	else
	{
		UpdateMenus( UM_ENABLE );								// Enable all processing menu items.
		SendMessage( g_hWnd_main, WM_CHANGE_CURSOR, FALSE, 0 );	// Reset the cursor.
		EnableWindow( g_hWnd_list, TRUE );						// Allow the listview to be interactive. Also forces a refresh to update the item count column.
		SetFocus( g_hWnd_list );								// Give focus back to the listview to allow shortcut keys.
		SetWindowTextA( g_hWnd_main, PROGRAM_CAPTION_A );		// Reset the window title.
	}
}

int dllrbt_compare( void *a, void *b )
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

void cleanup_shared_info( shared_info **si )
{
	free( ( *si )->short_stream_container );
	free( ( *si )->ssat );
	free( ( *si )->sat );
	free( *si );
	*si = NULL;
}

void cleanup_fileinfo_tree()
{
	// Free the values of the fileinfo tree.
	node_type *node = dllrbt_get_head( fileinfo_tree );
	while ( node != NULL )
	{
		// Free the linked list if there is one.
		linked_list *fi_node = ( linked_list * )node->val;
		while ( fi_node != NULL )
		{
			linked_list *del_fi_node = fi_node;

			fi_node = fi_node->next;

			free( del_fi_node );
		}

		node = node->next;
	}

	// Clean up our fileinfo tree.
	dllrbt_delete_recursively( fileinfo_tree );
	fileinfo_tree = NULL;
}

void create_fileinfo_tree()
{
	LVITEM lvi = { NULL };
	lvi.mask = LVIF_PARAM;

	fileinfo *fi = NULL;

	int item_count = SendMessage( g_hWnd_list, LVM_GETITEMCOUNT, 0, 0 );

	// Create the fileinfo tree if it doesn't exist.
	if ( fileinfo_tree == NULL )
	{
		fileinfo_tree = dllrbt_create( dllrbt_compare );
	}

	// Go through each item and add them to our tree.
	for ( int i = 0; i < item_count; ++i )
	{
		// We don't want to continue scanning if the user cancels the scan.
		if ( g_kill_scan == true )
		{
			break;
		}

		lvi.iItem = i;
		SendMessage( g_hWnd_list, LVM_GETITEM, 0, ( LPARAM )&lvi );

		fi = ( fileinfo * )lvi.lParam;

		// Don't attempt to insert the fileinfo if it's already in the tree.
		if ( fi != NULL )
		{
			// Make sure it's a hashed filename. It should be formatted like: 256_0123456789ABCDEF
			wchar_t *filename = wcschr( fi->filename, L'_' );

			if ( filename != NULL )
			{
				int filename_length = wcslen( filename + 1 );
				if ( filename_length <= 16 && filename_length >= 0 )
				{
					fi->entry_hash = _wcstoui64( filename + 1, NULL, 16 );

					// Create the node to insert into a linked list.
					linked_list *fi_node = ( linked_list * )malloc( sizeof( linked_list ) );
					fi_node->fi = fi;
					fi_node->next = NULL;

					// See if our tree has the hash to add the node to.
					linked_list *ll = ( linked_list * )dllrbt_find( fileinfo_tree, ( void * )fi->entry_hash, true );
					if ( ll == NULL )
					{
						if ( dllrbt_insert( fileinfo_tree, ( void * )fi->entry_hash, fi_node ) != DLLRBT_STATUS_OK )
						{
							free( fi_node );
						}
					}
					else	// If a hash exits, insert the node into the linked list.
					{
						linked_list *next = ll->next;	// We'll insert the node after the head.
						fi_node->next = next;
						ll->next = fi_node;
					}
				}
			}
		}
	}
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

	// If we have a CMYK based JPEG, then we're going to have to convert it to RGB.
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
	// This semaphore will be released when the thread gets killed.
	shutdown_semaphore = CreateSemaphore( NULL, 0, 1, NULL );

	g_kill_thread = true;	// Causes secondary threads to cease processing and release the semaphore.

	// Wait for any active threads to complete. 5 second timeout in case we miss the release.
	WaitForSingleObject( shutdown_semaphore, 5000 );
	CloseHandle( shutdown_semaphore );
	shutdown_semaphore = NULL;

	// DestroyWindow won't work on a window from a different thread. So we'll send a message to trigger it.
	SendMessage( g_hWnd_main, WM_DESTROY_ALT, 0, 0 );

	_endthreadex( 0 );
	return 0;
}

unsigned __stdcall copy_items( void *pArguments )
{
	// This will block every other thread from entering until the first thread is complete.
	EnterCriticalSection( &pe_cs );

	in_thread = true;

	Processing_Window( true );

	LVITEM lvi = { 0 };
	lvi.mask = LVIF_PARAM;
	lvi.iItem = -1;	// Set this to -1 so that the LVM_GETNEXTITEM call can go through the list correctly.

	int item_count = SendMessage( g_hWnd_list, LVM_GETITEMCOUNT, 0, 0 );
	int sel_count = SendMessage( g_hWnd_list, LVM_GETSELECTEDCOUNT, 0, 0 );
	
	bool copy_all = false;
	if ( item_count == sel_count )
	{
		copy_all = true;
	}
	else
	{
		item_count = sel_count;
	}

	unsigned int buffer_size = 8192;
	unsigned int buffer_offset = 0;
	wchar_t *copy_buffer = ( wchar_t * )malloc( sizeof( wchar_t ) * buffer_size );	// Allocate 8 kilobytes.

	int value_length = 0;

	wchar_t tbuf[ MAX_PATH ];
	wchar_t *buf = tbuf;

	bool add_newline = false;
	bool add_tab = false;

	// Go through each item, and copy their lParam values.
	for ( int i = 0; i < item_count; ++i )
	{
		// Stop processing and exit the thread.
		if ( g_kill_thread == true )
		{
			break;
		}

		if ( copy_all == true )
		{
			lvi.iItem = i;
		}
		else
		{
			lvi.iItem = SendMessage( g_hWnd_list, LVM_GETNEXTITEM, lvi.iItem, LVNI_SELECTED );
		}

		SendMessage( g_hWnd_list, LVM_GETITEM, 0, ( LPARAM )&lvi );

		fileinfo *fi = ( fileinfo * )lvi.lParam;

		if ( fi == NULL || ( fi != NULL && fi->si == NULL ) )
		{
			continue;
		}

		add_newline = add_tab = false;

		for ( int j = 1; j < NUM_COLUMNS; ++j )
		{
			switch ( j )
			{
				case 1:
				{
					buf = fi->filename;
					value_length = ( buf != NULL ? wcslen( buf ) : 0 );
				}
				break;

				case 2:
				{
					buf = tbuf;	// Reset the buffer pointer.

					// Depending on our toggle, output the size in either kilobytes or bytes.
					value_length = swprintf_s( buf, MAX_PATH, ( is_kbytes_size == true ? L"%d KB" : L"%d B" ), ( is_kbytes_size == true ? fi->size / 1024 : fi->size ) );
				}
				break;

				case 3:
				{
					// Distinguish between Short SAT and SAT entries.
					value_length = swprintf_s( buf, MAX_PATH, ( fi->size < fi->si->short_sect_cutoff ? L"%d in SSAT" : L"%d in SAT" ), fi->offset );
				}
				break;

				case 4:
				{
					// Format the date if there is one.
					if ( fi->date_modified > 0 )
					{
						SYSTEMTIME st;
						FILETIME ft;
						ft.dwLowDateTime = ( DWORD )fi->date_modified;
						ft.dwHighDateTime = ( DWORD )( fi->date_modified >> 32 );
						FileTimeToSystemTime( &ft, &st );
						value_length = swprintf_s( buf, MAX_PATH, L"%d/%d/%d (%02d:%02d:%02d.%d)", st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds );
					}
					else	// No date.
					{
						buf = NULL;
						value_length = 0;
					}
				}
				break;

				case 5:
				{
					buf = tbuf;	// Reset the buffer pointer.

					if ( fi->si->system == 1 )
					{
						value_length = swprintf_s( buf, MAX_PATH, L"%d: Windows Me/2000", fi->si->version );
					}
					else if ( fi->si->system == 2 )
					{
						value_length = swprintf_s( buf, MAX_PATH, L"%d: Windows XP/2003", fi->si->version );
					}
					else if ( fi->si->system == 3 )
					{
						buf = L"Windows Vista/2008/7/8/8.1";
						value_length = 26;
					}
					else
					{
						buf = L"Unknown";
						value_length = 7;
					}
				}
				break;

				case 6:
				{
					buf = fi->si->dbpath;
					value_length = wcslen( buf );
				}
				break;
			}

			if ( buf == NULL || ( buf != NULL && buf[ 0 ] == NULL ) )
			{
				if ( j == 1 )
				{
					add_tab = false;
				}

				continue;
			}

			if ( j > 1 && add_tab == true )
			{
				*( copy_buffer + buffer_offset ) = L'\t';
				++buffer_offset;
			}

			add_tab = true;

			while ( buffer_offset + value_length + 3 >= buffer_size )	// Add +3 for \t and \r\n
			{
				buffer_size += 8192;
				wchar_t *realloc_buffer = ( wchar_t * )realloc( copy_buffer, sizeof( wchar_t ) * buffer_size );
				if ( realloc_buffer == NULL )
				{
					goto CLEANUP;
				}

				copy_buffer = realloc_buffer;
			}
			wmemcpy_s( copy_buffer + buffer_offset, buffer_size - buffer_offset, buf, value_length );
			buffer_offset += value_length;

			add_newline = true;
		}

		if ( i < item_count - 1 && add_newline == true )	// Add newlines for every item except the last.
		{
			*( copy_buffer + buffer_offset ) = L'\r';
			++buffer_offset;
			*( copy_buffer + buffer_offset ) = L'\n';
			++buffer_offset;
		}
		else if ( ( i == item_count - 1 && add_newline == false ) && buffer_offset >= 2 )	// If add_newline is false for the last item, then a newline character is in the buffer.
		{
			buffer_offset -= 2;	// Ignore the last newline in the buffer.
		}
	}

	if ( OpenClipboard( g_hWnd_list ) )
	{
		EmptyClipboard();

		DWORD len = buffer_offset;

		// Allocate a global memory object for the text.
		HGLOBAL hglbCopy = GlobalAlloc( GMEM_MOVEABLE, sizeof( wchar_t ) * ( len + 1 ) );
		if ( hglbCopy != NULL )
		{
			// Lock the handle and copy the text to the buffer. lptstrCopy doesn't get freed.
			wchar_t *lptstrCopy = ( wchar_t * )GlobalLock( hglbCopy );
			if ( lptstrCopy != NULL )
			{
				wmemcpy_s( lptstrCopy, len + 1, copy_buffer, len );
				lptstrCopy[ len ] = 0; // Sanity
			}

			GlobalUnlock( hglbCopy );

			if ( SetClipboardData( CF_UNICODETEXT, hglbCopy ) == NULL )
			{
				GlobalFree( hglbCopy );	// Only free this Global memory if SetClipboardData fails.
			}

			CloseClipboard();
		}
	}

CLEANUP:

	free( copy_buffer );

	Processing_Window( false );

	// Release the semaphore if we're killing the thread.
	if ( shutdown_semaphore != NULL )
	{
		ReleaseSemaphore( shutdown_semaphore, 1, NULL );
	}

	in_thread = false;

	// We're done. Let other threads continue.
	LeaveCriticalSection( &pe_cs );

	_endthreadex( 0 );
	return 0;
}

unsigned __stdcall remove_items( void *pArguments )
{
	// This will block every other thread from entering until the first thread is complete.
	EnterCriticalSection( &pe_cs );

	in_thread = true;
	
	skip_draw = true;	// Prevent the listview from drawing while freeing lParam values.

	Processing_Window( true );

	LVITEM lvi = { NULL };
	lvi.mask = LVIF_PARAM;

	fileinfo *fi = NULL;

	int item_count = SendMessage( g_hWnd_list, LVM_GETITEMCOUNT, 0, 0 );
	int sel_count = SendMessage( g_hWnd_list, LVM_GETSELECTEDCOUNT, 0, 0 );

	// See if we've selected all the items. We can clear the list much faster this way.
	if ( item_count == sel_count )
	{
		// Go through each item, and free their lParam values. current_fileinfo will get deleted here.
		for ( int i = 0; i < item_count; ++i )
		{
			// Stop processing and exit the thread.
			if ( g_kill_thread == true )
			{
				break;
			}

			// We first need to get the lParam value otherwise the memory won't be freed.
			lvi.iItem = i;
			SendMessage( g_hWnd_list, LVM_GETITEM, 0, ( LPARAM )&lvi );

			fi = ( fileinfo * )lvi.lParam;

			if ( fi != NULL )
			{
				if ( fi->si != NULL )
				{
					--( fi->si->count );

					// Remove our shared information from the linked list if there's no more items for this database.
					if ( fi->si->count == 0 )
					{
						cleanup_shared_info( &( fi->si ) );
					}
				}

				// First free the filename pointer. We don't need to bother with the linked list pointer since it's only used during the initial read.
				free( fi->filename );
				// Then free the fileinfo structure.
				free( fi );
			}
		}

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
			if ( g_kill_thread == true )
			{
				break;
			}

			// We first need to get the lParam value otherwise the memory won't be freed.
			lvi.iItem = index_array[ i ];
			SendMessage( g_hWnd_list, LVM_GETITEM, 0, ( LPARAM )&lvi );

			fi = ( fileinfo * )lvi.lParam;

			if ( fi != NULL )
			{
				if ( fi->si != NULL )
				{
					--( fi->si->count );

					// Remove our shared information from the linked list if there's no more items for this database.
					if ( fi->si->count == 0 )
					{
						cleanup_shared_info( &( fi->si ) );
					}
				}
		
				// Free our filename, then fileinfo structure. We don't need to bother with the linked list pointer since it's only used during the initial read.
				free( fi->filename );
				// Then free the fileinfo structure.
				free( fi );
			}

			// Remove the list item.
			SendMessage( g_hWnd_list, LVM_DELETEITEM, index_array[ i ], 0 );
		}

		free( index_array );
	}

	skip_draw = false;	// Allow drawing again.

	Processing_Window( false );

	// Release the semaphore if we're killing the thread.
	if ( shutdown_semaphore != NULL )
	{
		ReleaseSemaphore( shutdown_semaphore, 1, NULL );
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

	Processing_Window( true );

	wchar_t *filepath = ( wchar_t * )pArguments;
	if ( filepath != NULL )
	{
		// Open our config file if it exists.
		HANDLE hFile = CreateFile( filepath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
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

			fileinfo *fi = NULL;

			// Go through all the items we'll be saving.
			for ( int i = 0; i < save_items; ++i )
			{
				// Stop processing and exit the thread.
				if ( g_kill_thread == true )
				{
					break;
				}

				lvi.iItem = i;
				SendMessage( g_hWnd_list, LVM_GETITEM, 0, ( LPARAM )&lvi );

				fi = ( fileinfo * )lvi.lParam;
				if ( fi == NULL || ( fi != NULL && fi->si == NULL ) )
				{
					continue;
				}

				int filename_length = WideCharToMultiByte( CP_UTF8, 0, ( fi->filename != NULL ? fi->filename : L"" ), -1, NULL, 0, NULL, NULL );
				char *utf8_filename = ( char * )malloc( sizeof( char ) * filename_length ); // Size includes the null character.
				filename_length = WideCharToMultiByte( CP_UTF8, 0, ( fi->filename != NULL ? fi->filename : L"" ), -1, utf8_filename, filename_length, NULL, NULL ) - 1;

				// The filename comes from the database entry and it could have unsupported characters.
				char *escaped_filename = escape_csv( utf8_filename );
				if ( escaped_filename != NULL )
				{
					free( utf8_filename );
					utf8_filename = escaped_filename;
				}

				int dbpath_length = WideCharToMultiByte( CP_UTF8, 0, fi->si->dbpath, -1, NULL, 0, NULL, NULL );
				char *utf8_dbpath = ( char * )malloc( sizeof( char ) * dbpath_length ); // Size includes the null character.
				dbpath_length = WideCharToMultiByte( CP_UTF8, 0, fi->si->dbpath, -1, utf8_dbpath, dbpath_length, NULL, NULL ) - 1;

				bool has_version = true;

				switch ( fi->si->system )
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
				if ( write_buf_offset + filename_length + dbpath_length + ( 10 * 10 ) + ( 20 * 1 ) + 26 + 30 + 1 > size )
				{
					// Dump the buffer.
					WriteFile( hFile, write_buf, write_buf_offset, &write, NULL );
					write_buf_offset = 0;
				}

				write_buf_offset += sprintf_s( write_buf + write_buf_offset, size - write_buf_offset, "\r\n\"%s\",%lu,%d in %sSAT,",
											   utf8_filename,
											   fi->size,
											   fi->offset,
											   ( fi->size < fi->si->short_sect_cutoff ? "S" : "" ) );

				if ( fi->date_modified != 0 )
				{
					ft.dwLowDateTime = ( DWORD )fi->date_modified;
					ft.dwHighDateTime = ( DWORD )( fi->date_modified >> 32 );
					FileTimeToSystemTime( &ft, &st );

					write_buf_offset += sprintf_s( write_buf + write_buf_offset, size - write_buf_offset, "%d/%d/%d (%02d:%02d:%02d.%d),%llu",
											   st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
											   fi->date_modified );
				}
				else
				{
					write_buf[ write_buf_offset++ ] = ',';
				}

				if ( has_version == true )
				{
					write_buf_offset += sprintf_s( write_buf + write_buf_offset, size - write_buf_offset, ",%d: ",
											   fi->si->version );
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

		free( filepath );
	}

	Processing_Window( false );

	// Release the semaphore if we're killing the thread.
	if ( shutdown_semaphore != NULL )
	{
		ReleaseSemaphore( shutdown_semaphore, 1, NULL );
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

	Processing_Window( true );

	save_param *save_type = ( save_param * )pArguments;
	if ( save_type != NULL )
	{
		wchar_t save_directory[ MAX_PATH ] = { 0 };
		if ( save_type->filepath == NULL )
		{
			GetCurrentDirectory( MAX_PATH, save_directory );
		}
		else if ( save_type->type == 1 )
		{
			// Create and set the directory that we'll be outputting files to.
			if ( GetFileAttributes( save_type->filepath ) == INVALID_FILE_ATTRIBUTES )
			{
				CreateDirectory( save_type->filepath, NULL );
			}

			// Get the full path if the input was relative.
			GetFullPathName( save_type->filepath, MAX_PATH, save_directory, NULL );
		}
		else
		{
			wcsncpy_s( save_directory, MAX_PATH, save_type->filepath, MAX_PATH - 1 );
		}

		// Depending on what was selected, get the number of items we'll be saving.
		int save_items = ( save_type->save_all == true ? SendMessage( g_hWnd_list, LVM_GETITEMCOUNT, 0, 0 ) : SendMessage( g_hWnd_list, LVM_GETSELECTEDCOUNT, 0, 0 ) );

		// Retrieve the lParam value from the selected listview item.
		LVITEM lvi = { NULL };
		lvi.mask = LVIF_PARAM;
		lvi.iItem = -1;	// Set this to -1 so that the LVM_GETNEXTITEM call can go through the list correctly.

		fileinfo *fi = NULL;

		// Go through all the items we'll be saving.
		for ( int i = 0; i < save_items; ++i )
		{
			// Stop processing and exit the thread.
			if ( g_kill_thread == true )
			{
				break;
			}

			lvi.iItem = ( save_type->save_all == true ? i : SendMessage( g_hWnd_list, LVM_GETNEXTITEM, lvi.iItem, LVNI_SELECTED ) );
			SendMessage( g_hWnd_list, LVM_GETITEM, 0, ( LPARAM )&lvi );

			fi = ( fileinfo * )lvi.lParam;
			if ( fi == NULL || ( fi != NULL && fi->filename == NULL ) )
			{
				continue;
			}

			unsigned long size = 0, header_offset = 0;	// Size excludes the header offset.
			// Create a buffer to read in our new bitmap.
			char *save_image = extract( fi, size, header_offset );
			if ( save_image == NULL )
			{
				continue;
			}

			// Directory + backslash + filename + extension + NULL character = ( MAX_PATH * 2 ) + 6
			wchar_t fullpath[ ( MAX_PATH * 2 ) + 6 ] = { 0 };

			wchar_t *filename = get_filename_from_path( fi->filename, wcslen( fi->filename ) );

			if ( ( fi->flag & FIF_TYPE_JPG ) || ( fi->flag & FIF_TYPE_CMYK_JPG ) )
			{
				wchar_t *ext = get_extension_from_filename( filename, wcslen( filename ) );
				// The extension in the filename might not be the actual type. So we'll append .jpg to the end of it.
				if ( _wcsicmp( ext, L".jpg" ) == 0 || _wcsicmp( ext, L".jpeg" ) == 0 )
				{
					swprintf_s( fullpath, ( MAX_PATH * 2 ) + 6, L"%.259s\\%.259s", save_directory, filename );
				}
				else
				{
					swprintf_s( fullpath, ( MAX_PATH * 2 ) + 6, L"%.259s\\%.259s.jpg", save_directory, filename );
				}
			}
			else if ( ( fi->flag & FIF_TYPE_PNG ) || ( ( fi->flag & FIF_TYPE_UNKNOWN ) && header_offset > 0 ) )
			{
				wchar_t *ext = get_extension_from_filename( filename, wcslen( filename ) );
				// The extension in the filename might not be the actual type. So we'll append .png to the end of it.
				if ( _wcsicmp( ext, L".png" ) == 0 )
				{
					swprintf_s( fullpath, ( MAX_PATH * 2 ) + 6, L"%.259s\\%.259s", save_directory, filename );
				}
				else
				{
					swprintf_s( fullpath, ( MAX_PATH * 2 ) + 6, L"%.259s\\%.259s.png", save_directory, filename );
				}
			}
			else
			{
				swprintf_s( fullpath, ( MAX_PATH * 2 ) + 6, L"%.259s\\%.259s", save_directory, filename );
			}

			// If we have a CMYK based JPEG, then we're going to have to convert it to RGB.
			if ( fi->flag & FIF_TYPE_CMYK_JPG )
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
					if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "An error occurred while converting the image to save.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
				}

				delete save_bm_image;
			}
			else
			{
				if ( ( fi->flag & FIF_TYPE_UNKNOWN ) && header_offset > 0 )
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
						if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "An error occurred while converting the image to save.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
						if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "One or more files could not be saved. Please check the filename and path.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
					}
				}
			}
			// Free our buffer.
			free( save_image );
		}

		free( save_type->filepath );
		free( save_type );
	}

	Processing_Window( false );

	// Release the semaphore if we're killing the thread.
	if ( shutdown_semaphore != NULL )
	{
		ReleaseSemaphore( shutdown_semaphore, 1, NULL );
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
