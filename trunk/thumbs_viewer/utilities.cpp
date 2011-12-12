/*
    thumbs_viewer will extract thumbnail images from thumbs database files.
    Copyright (C) 2011 Eric Kutcher

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

#define SNAP_WIDTH		10;		// The minimum distance at which our windows will attach together.

CRITICAL_SECTION open_cs;

unsigned long msat_size = 0;
unsigned long sat_size = 0;
unsigned long ssat_size = 0;

long *g_msat = NULL;

shared_info_linked_list *g_si = NULL;

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

bool is_close( int a, int b )
{
	// See if the distance between two points is less than the snap width.
	return abs( a - b ) < SNAP_WIDTH;
}

// Extract the file from the SAT or short stream container.
char *extract( fileinfo *fi, unsigned long &size, unsigned long &header_offset )
{
	wchar_t *filepath = fi->si->dbpath;
	char *buf = NULL;

	if ( fi->entry_type == 2 )
	{
		// See if the stream is in the SAT.
		if ( fi->size > fi->si->short_sect_cutoff && fi->si->sat != NULL )
		{
			DWORD read = 0, total = 0;
			long sat_index = fi->offset; 
			unsigned long sector_offset = 512 + ( sat_index * 512 );
			unsigned long bytes_to_read = 512;

			// Attempt to open a file for reading.
			HANDLE hFile = CreateFile( filepath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
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
void update_catalog_entries( HANDLE hFile, fileinfo *fi, directory_header dh )
{
	char *buf = NULL;
	if ( dh.short_stream_length > fi->si->short_sect_cutoff && fi->si->sat != NULL )
	{
		DWORD read = 0, total = 0;
		long sat_index = dh.first_short_stream_sect; 
		unsigned long sector_offset = 512 + ( sat_index * 512 );
		unsigned long bytes_to_read = 512;

		buf = ( char * )malloc( sizeof( char ) * dh.short_stream_length );

		while ( total < dh.short_stream_length )
		{
			// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
			if ( sat_index < 0 )
			{
				free( buf );
				MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return;
			}
			
			// Each index should be no greater than the size of the SAT array.
			if ( sat_index > ( long )( fi->si->num_sat_sects * 128 ) )
			{
				free( buf );
				MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return;
			}

			// Adjust the file pointer to the Short SAT
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
				return;
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
			// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
			if ( ssat_index < 0 )
			{
				free( buf );
				MessageBox( g_hWnd_main, L"Invalid Short SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return;
			}
			
			// Each index should be no greater than the size of the Short SAT array.
			if ( ssat_index > ( long )( fi->si->num_ssat_sects * 128 ) )
			{
				free( buf );
				MessageBox( g_hWnd_main, L"Short SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return;
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

	if ( buf != NULL )
	{
		unsigned long offset = ( unsigned short )buf[ 0 ];
		while ( offset < dh.short_stream_length && fi != NULL )
		{
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
				MessageBox( g_hWnd_main, L"Invalid directory entry.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				break;
			}

			wchar_t *original_name = ( wchar_t * )malloc( name_length + sizeof( wchar_t ) );
			wcsncpy_s( original_name, ( name_length + sizeof( wchar_t ) ) / sizeof( wchar_t ), ( wchar_t * )( buf + offset ), name_length / sizeof( wchar_t ) );

			if ( fi != NULL )
			{
				fi->date_modified = date_modified;
				free( fi->filename );
				fi->filename = original_name;

				// See if the filename contains a path. ":\" should be enough to signify a path.
				if ( name_length > 2 && fi->filename[ 1 ] == L':' && fi->filename[ 2 ] == L'\\' )
				{
					fi->si->system = 1;	// Me, 2000
				}
				else
				{
					fi->si->system = 2;	// XP, 2003
				}
				
				fi = fi->next;
			}

			offset += ( name_length + 4 );
		}
		
		free( buf );
	}

	InvalidateRect( g_hWnd_list, NULL, TRUE );
}

// Save the short stream container for later lookup.
// This is always located in the SAT.
void cache_short_stream_container( HANDLE hFile, directory_header dh )
{
	// Make sure we have a short stream container.
	if ( dh.short_stream_length <= 0 || dh.first_short_stream_sect < 0 )
	{
		return;
	}

	DWORD read = 0, total = 0;
	long sat_index = dh.first_short_stream_sect; 
	unsigned long sector_offset = 512 + ( sat_index * 512 );
	unsigned long bytes_to_read = 512;

	g_si->short_stream_container = ( char * )malloc( sizeof( char ) * dh.short_stream_length );

	while ( total < dh.short_stream_length )
	{
		// The SAT should terminate with -2, but we shouldn't get here before the for loop completes.
		if ( sat_index < 0 )
		{
			MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			break;
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * 128 ) )
		{
			MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			break;
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
			break;
		}

		sat_index = g_si->sat[ sat_index ];
	}
}

// Builds a list of directory entries.
// This list is found by traversing the SAT.
// The directory is stored as a red-black tree in the database, but we can simply iterate through it with a linked list.
void build_directory( HANDLE hFile )
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

	unsigned int item_count = SendMessage( g_hWnd_list, LVM_GETITEMCOUNT, 0, 0 ); // We don't need to call this for each item.

	// Save each directory sector from the SAT. The number of sectors is unknown.
	while ( true )
	{
		// The directory list should terminate with -2.
		if ( sat_index < 0 )
		{
			if ( sat_index == -2 )
			{
				if ( root_found == true )
				{
					cache_short_stream_container( hFile, root_dh );
				}

				g_fi->si->system = 3;	// Assume the system is Vista/2008/7

				if ( catalog_found == true )
				{
					update_catalog_entries( hFile, g_fi, catalog_dh );
				}

				return;
			}
			else
			{
				MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return;
			}
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * 128 ) )
		{
			MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return;
		}

		// Adjust the file pointer to the Short SAT
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );
		sector_offset = 512 + ( g_si->sat[ sat_index ] * 512 );

		// There are 4 directory items per 512 byte sector.
		for ( int i = 0; i < 4; i++ )
		{
			directory_header dh;
			ReadFile( hFile, &dh, sizeof( directory_header ), &read, NULL );

			if ( read < sizeof( directory_header ) )
			{
				MessageBox( g_hWnd_main, L"Premature end of file encountered while building the directory.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
				return;
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
			fi->extension = 0;	// None set.
			fi->si = g_si;
			fi->si->system = 0;	// Unknown until/if we process a catalog entry. There's really no definitive way to detect the system, but we can make assumptions.
			fi->si->count++;	// Increment the number of entries.
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

			// Enable the Save All and Select All menu items.
			EnableMenuItem( g_hMenu, MENU_SAVE_ALL, MF_ENABLED );
			EnableMenuItem( g_hMenu, MENU_SELECT_ALL, MF_ENABLED );
			EnableMenuItem( g_hMenuSub_context, MENU_SELECT_ALL, MF_ENABLED );
		}

		// Each index points to the next index.
		sat_index = g_si->sat[ sat_index ];
	}

	// We should never get here.
	return;
}

// Builds the Short SAT.
// This table is found by traversing the SAT.
void build_ssat( HANDLE hFile )
{
	DWORD read = 0, total = 0;
	long sat_index = g_si->first_ssat_sect;
	unsigned long sector_offset = 512 + ( sat_index * 512 );

	g_si->ssat = ( long * )malloc( ssat_size );

	// Save each sector from the SAT.
	for ( unsigned long i = 0; i < g_si->num_ssat_sects; i++ )
	{
		// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
		if ( sat_index < 0 )
		{
			MessageBox( g_hWnd_main, L"Invalid SAT termination index.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return;
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * 128 ) )
		{
			MessageBox( g_hWnd_main, L"SAT index out of bounds.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return;
		}

		// Adjust the file pointer to the Short SAT
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );
		sector_offset = 512 + ( g_si->sat[ sat_index ] * 512 );

		ReadFile( hFile, g_si->ssat + ( total / sizeof( long ) ), 512, &read, NULL );
		total += read;

		if ( read < 512 )
		{
			MessageBox( g_hWnd_main, L"Premature end of file encountered while building the Short SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return;
		}

		// Each index points to the next index.
		sat_index = g_si->sat[ sat_index ];
	}

	return;
}

// Builds the SAT.
// We concatenate each sector listed in the MSAT to build the SAT.
void build_sat( HANDLE hFile )
{
	DWORD read = 0, total = 0;
	unsigned long sector_offset = 0;

	g_si->sat = ( long * )malloc( sat_size );

	// Save each sector in the Master SAT.
	for ( unsigned long msat_index = 0; msat_index < g_si->num_sat_sects; msat_index++ )
	{
		// Adjust the file pointer to the SAT
		sector_offset = 512 + ( g_msat[ msat_index ] * 512 );
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );

		ReadFile( hFile, g_si->sat + ( total / sizeof( long ) ), 512, &read, NULL );
		total += read;

		if ( read < 512 )
		{
			MessageBox( g_hWnd_main, L"Premature end of file encountered while building the SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return;
		}
	}

	return;
}

// Builds the MSAT.
// This is only used to build the SAT and nothing more.
void build_msat( HANDLE hFile )
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
		return;
	}

	// If there are DISATs, then we'll add them to the MSAT list.
	for ( unsigned long i = 0; i < g_si->num_dis_sects; i++ )
	{
		SetFilePointer( hFile, last_sector, 0, FILE_BEGIN );

		// Read the first 127 SAT sectors (508 bytes) in the DISAT.
		ReadFile( hFile, g_msat + ( total / sizeof( long ) ), 508, &read, NULL );
		total += read;

		if ( read < 508 )
		{
			MessageBox( g_hWnd_main, L"Premature end of file encountered while building the Master SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return;
		}

		// Get the pointer to the next DISAT.
		ReadFile( hFile, &last_sector, 4, &read, NULL );

		if ( read < 4 )
		{
			MessageBox( g_hWnd_main, L"Premature end of file encountered while building the Master SAT.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			return;
		}

		// The last index in the DISAT contains a pointer to the next DISAT. That's assuming there's any more DISATs left.
		last_sector = 512 + ( last_sector * 512 );
	}

	return;
}

// Clean up our allocated memory.
void cleanup()
{
	shared_info_linked_list *si = g_si;
	shared_info_linked_list *del_si = NULL;
	while ( si != NULL )
	{
		del_si = si;
		si = si->next;

		free( del_si->sat );
		free( del_si->ssat );
		free( del_si->short_stream_container );
		free( del_si );
	}

	g_si = NULL;
}

unsigned __stdcall read_database( void *pArguments )
{
	// This will block every other thread from entering until the first thread is complete.
	// Protects our global variables.
	EnterCriticalSection( &open_cs );

	wchar_t *filepath = ( wchar_t * )pArguments;

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
			MessageBox( g_hWnd_main, L"Premature end of file encountered while reading the header.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );

			// We're done. Let other threads continue.
			LeaveCriticalSection( &open_cs );

			_endthreadex( 0 );
			return 0;
		}

		// Make sure it's a thumbs database and the stucture was filled correctly.
		if ( memcmp( dh.magic_identifier, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8 ) != 0 )
		{
			CloseHandle( hFile );
			free( filepath );

			MessageBox( g_hWnd_main, L"The file is not a thumbs database.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );

			// We're done. Let other threads continue.
			LeaveCriticalSection( &open_cs );

			_endthreadex( 0 );
			return 0;
		}

		// These values are the minimum at which we can multiply the sector size (512) and not go out of range.
		if ( dh.num_sat_sects > 0x7FFFFF || dh.num_ssat_sects > 0x7FFFFF || dh.num_dis_sects > 0x810203 )
		{
			MessageBox( g_hWnd_main, L"The total sector allocation table size is too large.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			CloseHandle( hFile );

			// We're done. Let other threads continue.
			LeaveCriticalSection( &open_cs );

			_endthreadex( 0 );
			return 0;
		}

		msat_size = sizeof( long ) * ( 109 + ( ( dh.num_dis_sects > 0 ? dh.num_dis_sects : 0 ) * 127 ) );
		sat_size = ( dh.num_sat_sects > 0 ? dh.num_sat_sects : 0 ) * 512;
		ssat_size = ( dh.num_ssat_sects > 0 ? dh.num_ssat_sects : 0 ) * 512;

		GetFileSizeEx( hFile, &f_size );

		// This is a simple check to make sure we don't allocate too much memory.
		if ( ( msat_size + sat_size + ssat_size ) > f_size.QuadPart )
		{
			MessageBox( g_hWnd_main, L"The total sector allocation table size exceeds the size of the database.", PROGRAM_CAPTION, MB_APPLMODAL | MB_ICONWARNING );
			CloseHandle( hFile );

			// We're done. Let other threads continue.
			LeaveCriticalSection( &open_cs );

			_endthreadex( 0 );
			return 0;
		}

		// This information is shared between entries within the database.
		shared_info_linked_list *si = ( shared_info_linked_list * )malloc( sizeof( shared_info_linked_list ) );

		si->next = g_si;
		g_si = si;

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

		build_msat( hFile );
		build_sat( hFile );
		build_ssat( hFile );
		build_directory( hFile );
		
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

	free( filepath );

	// We're done. Let other threads continue.
	LeaveCriticalSection( &open_cs );

	_endthreadex( 0 );
	return 0;
}
