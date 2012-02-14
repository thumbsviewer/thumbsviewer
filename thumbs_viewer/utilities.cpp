/*
    thumbs_viewer will extract thumbnail images from thumbs database files.
    Copyright (C) 2011-2012 Eric Kutcher

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

wchar_t *get_extension_from_filename( wchar_t *filename, unsigned long length )
{
	while ( length != 0 && filename[ --length ] != L'.' );

	return filename + length + 1;
}

wchar_t *get_filename_from_path( wchar_t *path, unsigned long length )
{
	while ( length != 0 && path[ --length ] != L'\\' );

	return path + length + 1;
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
Gdiplus::Image *create_image( char *buffer, unsigned long size, bool is_cmyk )
{
	ULONG written = 0;
	IStream *is = NULL;
	CreateStreamOnHGlobal( NULL, TRUE, &is );
	is->Write( buffer, size, &written );
	Gdiplus::Image *image = new Gdiplus::Image( is );

	// If we have a CYMK based JPEG, then we're going to have to convert it to RGB.
	if ( is_cmyk )
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
	// Get the directory path from the id list.
	SHGetPathFromIDList( save_type->lpiidl, save_directory );
	CoTaskMemFree( save_type->lpiidl );
	
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
		if ( ( ( fileinfo * )lvi.lParam )->si->system == 1 )
		{
			// Windows Me and 2000 will have a full path for the filename and we can assume it has a "\" in it since we look for it when detecting the system.
			filename = get_filename_from_path( ( ( fileinfo * )lvi.lParam )->filename, wcslen( ( ( fileinfo * )lvi.lParam )->filename ) );
		}
		else
		{
			filename = ( ( fileinfo * )lvi.lParam )->filename;
		}

		wchar_t *ext = get_extension_from_filename( filename, wcslen( filename ) );
		if ( ( ( fileinfo * )lvi.lParam )->extension == 1 || ( ( fileinfo * )lvi.lParam )->extension == 3 )
		{
			// The extension in the filename might not be the actual type. So we'll append .jpg to the end of it.
			if ( _wcsicmp( ext, L"jpg" ) == 0 || _wcsicmp( ext, L"jpeg" ) == 0 )
			{
				swprintf_s( fullpath, ( 2 * MAX_PATH ) + 6, L"%s\\%.259s", save_directory, filename );
			}
			else
			{
				swprintf_s( fullpath, ( 2 * MAX_PATH ) + 6, L"%s\\%.259s.jpg", save_directory, filename );
			}
		}
		else if ( ( ( fileinfo * )lvi.lParam )->extension == 2 )
		{
			swprintf_s( fullpath, ( 2 * MAX_PATH ) + 6, L"%s\\%.259s.png", save_directory, filename );
		}
		else
		{
			swprintf_s( fullpath, ( 2 * MAX_PATH ) + 6, L"%s\\%.259s", save_directory, filename );
		}

		// If we have a CYMK based JPEG, then we're going to have to convert it to RGB.
		if ( ( ( fileinfo * )lvi.lParam )->extension == 3 )
		{
			Gdiplus::Image *save_bm_image = create_image( save_image + header_offset, size, true );

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
				MessageBox( g_hWnd_main, L"An error occurred while converting the image to save.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
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
				MessageBox( g_hWnd_main, L"One or more files could not be saved. Please check the filename and path.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			}
		}
		// Free our buffer.
		free( save_image );
	}

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
			unsigned long sector_offset = 512 + ( sat_index * 512 );
			unsigned long bytes_to_read = 512;

			// Attempt to open a file for reading.
			HANDLE hFile = CreateFile( fi->si->dbpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
			if ( hFile == INVALID_HANDLE_VALUE )
			{
				return NULL;
			}

			buf = ( char * )malloc( sizeof( char ) * fi->size );

			while ( total < fi->size )
			{
				// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
				if ( sat_index < 0 )
				{
					MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
					break;
				}
				
				// Each index should be no greater than the size of the SAT array.
				if ( sat_index > ( long )( fi->si->num_sat_sects * 128 ) )
				{
					MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
					break;
				}

				// Adjust the file pointer to the Short SAT
				SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );
				sector_offset = 512 + ( fi->si->sat[ sat_index ] * 512 );

				if ( total + 512 > fi->size )
				{
					bytes_to_read = fi->size - total;
				}

				ReadFile( hFile, buf + total, bytes_to_read, &read, NULL );
				total += read;

				if ( read < bytes_to_read )
				{
					MessageBox( g_hWnd_main, L"Premature end of file encountered while extracting the file.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
					break;
				}

				sat_index = fi->si->sat[ sat_index ];
			}

			header_offset = ( total > sizeof( unsigned long ) ? ( unsigned long )buf[ 0 ] : 0 );

			if ( header_offset > total )
			{
				header_offset = 0;
			}

			size = total - header_offset;

			// See if there's a second header.
			// The first header will look like this:
			// Header length (4 bytes)
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
				if ( ( unsigned long )( buf + header_offset )[ 0 ] == 1 )
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

			CloseHandle( hFile );
		}
		else if ( fi->si->short_stream_container != NULL && fi->si->ssat != NULL )	// Stream is in the short stream.
		{
			DWORD read = 0, total = 0;
			long ssat_index = fi->offset;

			buf = ( char * )malloc( sizeof( char ) * fi->size );

			unsigned long buf_offset = 0;
			unsigned long bytes_to_read = 64;
			while ( buf_offset < fi->size )
			{
				// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
				if ( ssat_index < 0 )
				{
					MessageBox( g_hWnd_main, L"Invalid Short SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
					return buf;
				}
				
				// Each index should be no greater than the size of the Short SAT array.
				if ( ssat_index > ( long )( fi->si->num_ssat_sects * 128 ) )
				{
					MessageBox( g_hWnd_main, L"Short SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
					return buf;
				}


				unsigned long sector_offset = ssat_index * 64;

				if ( buf_offset + 64 > fi->size )
				{
					bytes_to_read = fi->size - buf_offset;
				}

				memcpy_s( buf + buf_offset, fi->size - buf_offset, fi->si->short_stream_container + sector_offset, bytes_to_read );
				buf_offset += bytes_to_read;

				ssat_index = fi->si->ssat[ ssat_index ];
			}

			header_offset = ( buf_offset > sizeof( unsigned long ) ? ( unsigned long )buf[ 0 ] : 0 );

			if ( header_offset > fi->size )
			{
				header_offset = 0;
			}

			size = fi->size - header_offset;

			// The first header will look like this:
			// Header length (4 bytes)
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
				if ( ( unsigned long )( buf + header_offset )[ 0 ] == 1 )
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
char update_catalog_entries( HANDLE hFile, fileinfo *fi, directory_header dh, shared_info *g_si )
{
	char *buf = NULL;
	if ( dh.short_stream_length >= fi->si->short_sect_cutoff && fi->si->sat != NULL )
	{
		DWORD read = 0, total = 0;
		long sat_index = dh.first_short_stream_sect; 
		unsigned long sector_offset = 512 + ( sat_index * 512 );
		unsigned long bytes_to_read = 512;

		buf = ( char * )malloc( sizeof( char ) * dh.short_stream_length );

		while ( total < dh.short_stream_length )
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
				free( buf );
				MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return SC_FAIL;
			}
			
			// Each index should be no greater than the size of the SAT array.
			if ( sat_index > ( long )( fi->si->num_sat_sects * 128 ) )
			{
				free( buf );
				MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return SC_FAIL;
			}

			// Adjust the file pointer to the SAT
			SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );
			sector_offset = 512 + ( fi->si->sat[ sat_index ] * 512 );

			if ( total + 512 > dh.short_stream_length )
			{
				bytes_to_read = dh.short_stream_length - total;
			}

			ReadFile( hFile, buf + total, bytes_to_read, &read, NULL );
			total += read;

			if ( read < bytes_to_read )
			{
				free( buf );
				MessageBox( g_hWnd_main, L"Premature end of file encountered while updating the directory.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return SC_FAIL;
			}

			sat_index = g_si->sat[ sat_index ];
		}
	}
	else if ( fi->si->short_stream_container != NULL && fi->si->ssat != NULL )
	{
		long ssat_index = dh.first_short_stream_sect;
		unsigned long sector_offset = ssat_index * 64;

		buf = ( char * )malloc( sizeof( char ) * dh.short_stream_length );

		unsigned long buf_offset = 0;
		unsigned long bytes_to_read = 64;
		while ( buf_offset < dh.short_stream_length )
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
				free( buf );
				MessageBox( g_hWnd_main, L"Invalid Short SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return SC_FAIL;
			}
			
			// Each index should be no greater than the size of the Short SAT array.
			if ( ssat_index > ( long )( fi->si->num_ssat_sects * 128 ) )
			{
				free( buf );
				MessageBox( g_hWnd_main, L"Short SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return SC_FAIL;
			}

			unsigned long s_offset = ssat_index * 64;

			if ( buf_offset + 64 > dh.short_stream_length )
			{
				bytes_to_read = dh.short_stream_length - buf_offset;
			}

			memcpy_s( buf + buf_offset, dh.short_stream_length - buf_offset, fi->si->short_stream_container + s_offset, bytes_to_read );
			buf_offset += bytes_to_read;

			ssat_index = fi->si->ssat[ ssat_index ];
		}
	}

	if ( buf != NULL && dh.short_stream_length > ( 2 * sizeof( unsigned short ) ) )
	{
		unsigned long offset = ( unsigned short )buf[ 0 ];
		memcpy_s( &fi->si->version, sizeof( unsigned short ), buf + 1, sizeof( unsigned short ) );
		fi->si->version = ( ( fi->si->version & 0x00FF ) << 8 | ( fi->si->version & 0xFF00 ) >> 8 );

		while ( offset < dh.short_stream_length && fi != NULL )
		{
			// Stop processing and exit the thread.
			if ( kill_thread == true )
			{
				free( buf );
				return SC_QUIT;
			}

			unsigned short entry_length = 0;
			memcpy_s( &entry_length, sizeof( unsigned short ), buf + offset, sizeof( unsigned short ) );
			offset += sizeof( long );
			unsigned long entry_num = 0;
			memcpy_s( &entry_num, sizeof( unsigned long ), buf + offset, sizeof( unsigned long ) );
			offset += sizeof( long );
			__int64 date_modified = 0;
			memcpy_s( &date_modified, sizeof( __int64 ), buf + offset, 8 );
			offset += sizeof( __int64 );

			unsigned long name_length = entry_length - 0x14;

			if ( name_length > dh.short_stream_length )
			{
				free( buf );
				MessageBox( g_hWnd_main, L"Invalid directory entry.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return SC_FAIL;
			}

			wchar_t *original_name = ( wchar_t * )malloc( name_length + sizeof( wchar_t ) );
			wcsncpy_s( original_name, ( name_length + sizeof( wchar_t ) ) / sizeof( wchar_t ), ( wchar_t * )( buf + offset ), name_length / sizeof( wchar_t ) );

			if ( fi != NULL )
			{
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
	if ( dh.short_stream_length <= 0 || dh.first_short_stream_sect < 0 )
	{
		return SC_OK;
	}

	DWORD read = 0, total = 0;
	long sat_index = dh.first_short_stream_sect; 
	unsigned long sector_offset = 512 + ( sat_index * 512 );
	unsigned long bytes_to_read = 512;

	g_si->short_stream_container = ( char * )malloc( sizeof( char ) * dh.short_stream_length );

	while ( total < dh.short_stream_length )
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			return SC_QUIT;
		}

		// The SAT should terminate with -2, but we shouldn't get here before the for loop completes.
		if ( sat_index < 0 )
		{
			MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return SC_FAIL;
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * 128 ) )
		{
			MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return SC_FAIL;
		}

		// Adjust the file pointer to the Short SAT
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );
		sector_offset = 512 + ( g_si->sat[ sat_index ] * 512 );

		if ( total + 512 > dh.short_stream_length )
		{
			bytes_to_read = dh.short_stream_length - total;
		}

		ReadFile( hFile, g_si->short_stream_container + total, bytes_to_read, &read, NULL );
		total += read;

		if ( read < bytes_to_read )
		{
			MessageBox( g_hWnd_main, L"Premature end of file encountered while building the short stream container.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
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
	unsigned long sector_offset = 512 + ( sat_index * 512 );

	bool root_found = false;
	directory_header root_dh = { 0 };

	bool catalog_found = false;
	directory_header catalog_dh = { 0 };

	fileinfo *g_fi = NULL;
	fileinfo *last_fi = NULL;

	int item_count = SendMessage( g_hWnd_list, LVM_GETITEMCOUNT, 0, 0 ); // We don't need to call this for each item.

	// Save each directory sector from the SAT. The number of sectors is unknown.
	while ( true )
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			return SC_QUIT;
		}

		// The directory list should terminate with -2.
		if ( sat_index < 0 )
		{
			if ( sat_index == -2 )
			{
				if ( g_fi != NULL )
				{
					if ( root_found == true )
					{
						cache_short_stream_container( hFile, root_dh, g_si );
					}

					g_fi->si->system = 3;	// Assume the system is Vista/2008/7

					if ( catalog_found == true )
					{
						update_catalog_entries( hFile, g_fi, catalog_dh, g_si );
					}

					return SC_OK;
				}
				else
				{
					MessageBox( g_hWnd_main, L"No entries were found.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				}
			}
			else
			{
				MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			}

			return SC_FAIL;
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * 128 ) )
		{
			MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return SC_FAIL;
		}

		// Adjust the file pointer to the Short SAT
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );
		sector_offset = 512 + ( g_si->sat[ sat_index ] * 512 );

		// There are 4 directory items per 512 byte sector.
		for ( int i = 0; i < 4; i++ )
		{
			// Stop processing and exit the thread.
			if ( kill_thread == true )
			{
				return SC_QUIT;
			}

			directory_header dh;
			ReadFile( hFile, &dh, sizeof( directory_header ), &read, NULL );

			if ( read < sizeof( directory_header ) )
			{
				MessageBox( g_hWnd_main, L"Premature end of file encountered while building the directory.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return SC_FAIL;
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
			wcscpy_s( fi->filename, 32, dh.sid );
			memcpy_s( &fi->date_modified, sizeof( __int64 ), dh.modify_time, 8 );
			fi->offset = dh.first_short_stream_sect;
			fi->size = dh.short_stream_length;
			fi->entry_type = dh.entry_type;
			fi->extension = 0;		// None set.
			fi->si = g_si;
			fi->si->version = 0;	// Unknown until/if we process a catalog entry.
			fi->si->system = 0;		// Unknown until/if we process a catalog entry.
			fi->si->count++;		// Increment the number of entries.
			fi->next = NULL;

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

		// Each index points to the next index.
		sat_index = g_si->sat[ sat_index ];
	}

	// We should never get here.
	return SC_FAIL;
}

// Builds the Short SAT.
// This table is found by traversing the SAT.
char build_ssat( HANDLE hFile, shared_info *g_si )
{
	DWORD read = 0, total = 0;
	long sat_index = g_si->first_ssat_sect;
	unsigned long sector_offset = 512 + ( sat_index * 512 );

	g_si->ssat = ( long * )malloc( ssat_size );

	// Save each sector from the SAT.
	for ( unsigned long i = 0; i < g_si->num_ssat_sects; i++ )
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			return SC_QUIT;
		}

		// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
		if ( sat_index < 0 )
		{
			MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return SC_FAIL;
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * 128 ) )
		{
			MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return SC_FAIL;
		}

		// Adjust the file pointer to the Short SAT
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );
		sector_offset = 512 + ( g_si->sat[ sat_index ] * 512 );

		ReadFile( hFile, g_si->ssat + ( total / sizeof( long ) ), 512, &read, NULL );
		total += read;

		if ( read < 512 )
		{
			MessageBox( g_hWnd_main, L"Premature end of file encountered while building the Short SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
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

	// Save each sector in the Master SAT.
	for ( unsigned long msat_index = 0; msat_index < g_si->num_sat_sects; msat_index++ )
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			return SC_QUIT;
		}

		// Adjust the file pointer to the SAT
		sector_offset = 512 + ( g_msat[ msat_index ] * 512 );
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );

		ReadFile( hFile, g_si->sat + ( total / sizeof( long ) ), 512, &read, NULL );
		total += read;

		if ( read < 512 )
		{
			MessageBox( g_hWnd_main, L"Premature end of file encountered while building the SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return SC_FAIL;
		}
	}

	return SC_OK;
}

// Builds the MSAT.
// This is only used to build the SAT and nothing more.
char build_msat( HANDLE hFile, shared_info *g_si )
{
	DWORD read = 0, total = 436;
	unsigned long last_sector = 512 + ( g_si->first_dis_sect * 512 );	// Offset to the next DISAT (double indirect sector allocation table)

	g_msat = ( long * )malloc( msat_size );

	// Set the file pionter to the beginning of the master sector allocation table.
	SetFilePointer( hFile, 76, 0, FILE_BEGIN );

	// The first MSAT (contained within the 512 byte header) is 436 bytes. Every other MSAT will be 512 bytes.
	ReadFile( hFile, g_msat, 436, &read, NULL );

	if ( read < 436 )
	{
		MessageBox( g_hWnd_main, L"Premature end of file encountered while building the Master SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
		return SC_FAIL;
	}

	// If there are DISATs, then we'll add them to the MSAT list.
	for ( unsigned long i = 0; i < g_si->num_dis_sects; i++ )
	{
		// Stop processing and exit the thread.
		if ( kill_thread == true )
		{
			return SC_QUIT;
		}

		SetFilePointer( hFile, last_sector, 0, FILE_BEGIN );

		// Read the first 127 SAT sectors (508 bytes) in the DISAT.
		ReadFile( hFile, g_msat + ( total / sizeof( long ) ), 508, &read, NULL );
		total += read;

		if ( read < 508 )
		{
			MessageBox( g_hWnd_main, L"Premature end of file encountered while building the Master SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return SC_FAIL;
		}

		// Get the pointer to the next DISAT.
		ReadFile( hFile, &last_sector, 4, &read, NULL );

		if ( read < 4 )
		{
			MessageBox( g_hWnd_main, L"Premature end of file encountered while building the Master SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return SC_FAIL;
		}

		// The last index in the DISAT contains a pointer to the next DISAT. That's assuming there's any more DISATs left.
		last_sector = 512 + ( last_sector * 512 );
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
	
	bool construct_filepath = ( filepath_length > pi->offset ? false : true );

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
			fname_length = wcslen( fname ) + 1;	// Include '\' character

			filepath = ( wchar_t * )malloc( sizeof( wchar_t ) * ( filepath_length + fname_length ) );
			swprintf_s( filepath, filepath_length + fname_length, L"%s\\%s", pi->filepath, fname );

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

				MessageBox( g_hWnd_main, L"Premature end of file encountered while reading the header.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );

				continue;
			}

			// Make sure it's a thumbs database and the stucture was filled correctly.
			if ( memcmp( dh.magic_identifier, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8 ) != 0 )
			{
				CloseHandle( hFile );
				free( filepath );

				MessageBox( g_hWnd_main, L"The file is not a thumbs database.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );

				continue;
			}

			// These values are the minimum at which we can multiply the sector size (512) and not go out of range.
			if ( dh.num_sat_sects > 0x7FFFFF || dh.num_ssat_sects > 0x7FFFFF || dh.num_dis_sects > 0x810203 )
			{
				CloseHandle( hFile );
				free( filepath );

				MessageBox( g_hWnd_main, L"The total sector allocation table size is too large.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );

				continue;
			}

			msat_size = sizeof( long ) * ( 109 + ( ( dh.num_dis_sects > 0 ? dh.num_dis_sects : 0 ) * 127 ) );
			sat_size = ( dh.num_sat_sects > 0 ? dh.num_sat_sects : 0 ) * 512;
			ssat_size = ( dh.num_ssat_sects > 0 ? dh.num_ssat_sects : 0 ) * 512;

			GetFileSizeEx( hFile, &f_size );

			// This is a simple check to make sure we don't allocate too much memory.
			if ( ( msat_size + sat_size + ssat_size ) > f_size.QuadPart )
			{
				CloseHandle( hFile );
				free( filepath );

				MessageBox( g_hWnd_main, L"The total sector allocation table size exceeds the size of the database.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );

				continue;
			}

			// This information is shared between entries within the database.
			shared_info *si = ( shared_info * )malloc( sizeof( shared_info ) );
			si->sat = NULL;
			si->ssat = NULL;
			si->short_stream_container = NULL;
			si->count = 0;
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
			MessageBox( g_hWnd_main, L"The database file failed to open.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
		}

		// Free the old filepath.
		free( filepath );
	}
	while ( construct_filepath == true && *fname != L'\0' );

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
