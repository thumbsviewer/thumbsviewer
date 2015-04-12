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

#include "read_thumbs.h"
#include "globals.h"
#include "utilities.h"

unsigned long msat_size = 0;
unsigned long sat_size = 0;
unsigned long ssat_size = 0;

long *g_msat = NULL;

// Extract the file from the SAT or short stream container.
char *extract( fileinfo *fi, unsigned long &size, unsigned long &header_offset )
{
	char *buf = NULL;

	if ( fi == NULL || ( fi != NULL && fi->si == NULL ) )
	{
		return NULL;
	}

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
					if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Invalid SAT termination index.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
					break;
				}
				
				// Each index should be no greater than the size of the SAT array.
				if ( sat_index > ( long )( fi->si->num_sat_sects * ( fi->si->sect_size / sizeof( long ) ) ) )
				{
					if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "SAT index out of bounds.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
					if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Premature end of file encountered while extracting the file.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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

					fi->flag |= FIF_TYPE_CMYK_JPG;
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
					if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Invalid Short SAT termination index.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
					break;
				}
				
				// Each index should be no greater than the size of the Short SAT array.
				if ( ssat_index > ( long )( fi->si->num_ssat_sects * ( fi->si->sect_size / sizeof( long ) ) ) )
				{
					if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Short SAT index out of bounds.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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

					fi->flag |= FIF_TYPE_CMYK_JPG;
				}
			}
		}
	}

	// Set the extension if none has been set.
	if ( !( fi->flag & 0x0F ) && buf != NULL )	// Mask the first 4 bits to see if an extension has been set.
	{
		// Detect the file extension and copy it into the filename string.
		if ( size > 4 && memcmp( buf + header_offset, FILE_TYPE_JPEG, 4 ) == 0 )		// First 4 bytes
		{
			fi->flag |= FIF_TYPE_JPG;
		}
		else if ( size > 8 && memcmp( buf + header_offset, FILE_TYPE_PNG, 8 ) == 0 )	// First 8 bytes
		{
			fi->flag |= FIF_TYPE_PNG;
		}
		else
		{
			fi->flag |= FIF_TYPE_UNKNOWN;
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
	if ( fi == NULL || ( fi != NULL && fi->si == NULL ) )
	{
		return SC_FAIL;	// Fail silently. Don't do shared_info cleanup.
	}

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
			if ( g_kill_thread == true )
			{
				free( buf );
				return SC_QUIT;	// Quit silently. Don't do shared_info cleanup.
			}

			// The SAT should terminate with -2, but we shouldn't get here before the for loop completes.
			if ( sat_index < 0 )
			{
				if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Invalid SAT termination index.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
				break;
			}
			
			// Each index should be no greater than the size of the SAT array.
			if ( sat_index > ( long )( fi->si->num_sat_sects * ( fi->si->sect_size / sizeof( long ) ) ) )
			{
				if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "SAT index out of bounds.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
				if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Premature end of file encountered while updating the directory.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
			if ( g_kill_thread == true )
			{
				free( buf );
				return SC_QUIT;	// Quit silently. Don't do shared_info cleanup.
			}

			// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
			if ( ssat_index < 0 )
			{
				if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Invalid Short SAT termination index.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
				break;
			}
			
			// Each index should be no greater than the size of the Short SAT array.
			if ( ssat_index > ( long )( fi->si->num_ssat_sects * ( fi->si->sect_size / sizeof( long ) ) ) )
			{
				if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Short SAT index out of bounds.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
			if ( g_kill_thread == true )
			{
				free( buf );
				return SC_QUIT;	// Quit silently. Don't do shared_info cleanup.
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
				if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Invalid directory entry.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
	if ( g_si == NULL || ( g_si != NULL && g_si->sat == NULL ) )
	{
		return SC_FAIL;	// Fail silently. Don't do shared_info cleanup.
	}

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
		if ( g_kill_thread == true )
		{
			return SC_QUIT;	// Quit silently. Don't do shared_info cleanup.
		}

		// The SAT should terminate with -2, but we shouldn't get here before the for loop completes.
		if ( sat_index < 0 )
		{
			if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Invalid SAT termination index.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * ( g_si->sect_size / sizeof( long ) ) ) )
		{
			if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "SAT index out of bounds.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
			if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Premature end of file encountered while building the short stream container.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
	if ( g_si == NULL )
	{
		return SC_QUIT;
	}
	else if ( g_si != NULL && g_si->sat == NULL )
	{
		cleanup_shared_info( &g_si );

		return SC_QUIT;
	}

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
		if ( g_kill_thread == true )
		{
			if ( g_fi == NULL )
			{
				cleanup_shared_info( &g_si );
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
					if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "No entries were found.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
				}
			}
			else
			{
				if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Invalid SAT termination index.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
			}

			break;
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * ( g_si->sect_size / sizeof( long ) ) ) )
		{
			if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "SAT index out of bounds.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
			if ( g_kill_thread == true )
			{
				if ( g_fi == NULL )
				{
					cleanup_shared_info( &g_si );
				}

				return SC_QUIT;
			}

			directory_header dh;
			ReadFile( hFile, &dh, sizeof( directory_header ), &read, NULL );

			if ( read < sizeof( directory_header ) )
			{
				if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Premature end of file encountered while building the directory.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
			fi->flag = 0;			// None set.
			fi->si = g_si;
			fi->si->version = 0;	// Unknown until/if we process a catalog entry.
			fi->si->system = 0;		// Unknown until/if we process a catalog entry.
			++( fi->si->count );	// Increment the number of entries.
			fi->next = NULL;
			fi->entry_hash = 0;

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
			if ( cache_short_stream_container( hFile, root_dh, g_si ) == SC_QUIT )
			{
				return SC_QUIT;	// Allow the main thread to do shared_info cleanup.
			}
		}

		if ( catalog_found == true )
		{
			if ( update_catalog_entries( hFile, g_fi, catalog_dh ) == SC_QUIT )
			{
				return SC_QUIT;	// Allow the main thread to do shared_info cleanup.
			}
		}
	}
	else	// Free our shared info structure if no item was added to the list.
	{
		cleanup_shared_info( &g_si );
	}

	return SC_OK;
}

// Builds the Short SAT.
// This table is found by traversing the SAT.
char build_ssat( HANDLE hFile, shared_info *g_si )
{
	if ( g_si == NULL )
	{
		return SC_QUIT;
	}
	else if ( g_si != NULL && g_si->sat == NULL )
	{
		cleanup_shared_info( &g_si );

		return SC_QUIT;
	}

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
		if ( g_kill_thread == true )
		{
			cleanup_shared_info( &g_si );

			return SC_QUIT;
		}

		// The Short SAT should terminate with -2, but we shouldn't get here before the for loop completes.
		if ( sat_index < 0 )
		{
			if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Invalid SAT termination index.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}
		
		// Each index should be no greater than the size of the SAT array.
		if ( sat_index > ( long )( g_si->num_sat_sects * ( g_si->sect_size / sizeof( long ) ) ) )
		{
			if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "SAT index out of bounds.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
			if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Premature end of file encountered while building the Short SAT.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
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
	if ( g_si == NULL )
	{
		return SC_QUIT;
	}

	DWORD read = 0, total = 0;
	unsigned long sector_offset = 0;

	g_si->sat = ( long * )malloc( sat_size );
	memset( g_si->sat, -1, sat_size );

	// Save each sector in the Master SAT.
	for ( unsigned long msat_index = 0; msat_index < g_si->num_sat_sects; msat_index++ )
	{
		// Stop processing and exit the thread.
		if ( g_kill_thread == true )
		{
			cleanup_shared_info( &g_si );

			return SC_QUIT;
		}

		// We shouldn't get here before the for loop completes.
		if ( g_msat[ msat_index ] < 0 )
		{
			if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Invalid Master SAT termination index.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}

		// Adjust the file pointer to the SAT
		sector_offset = g_si->sect_size + ( g_msat[ msat_index ] * g_si->sect_size );
		SetFilePointer( hFile, sector_offset, 0, FILE_BEGIN );

		ReadFile( hFile, g_si->sat + ( total / sizeof( long ) ), g_si->sect_size, &read, NULL );
		total += read;

		if ( read < g_si->sect_size )
		{
			if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Premature end of file encountered while building the SAT.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}
	}

	return SC_OK;
}

// Builds the MSAT.
// This is only used to build the SAT and nothing more.
char build_msat( HANDLE hFile, shared_info *g_si )
{
	if ( g_si == NULL )
	{
		return SC_QUIT;
	}

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
		if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Premature end of file encountered while building the Master SAT.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
		return SC_FAIL;
	}

	// If there are DISATs, then we'll add them to the MSAT list.
	for ( unsigned long i = 0; i < g_si->num_dis_sects; i++ )
	{
		// Stop processing and exit the thread.
		if ( g_kill_thread == true )
		{
			cleanup_shared_info( &g_si );

			return SC_QUIT;
		}

		SetFilePointer( hFile, last_sector, 0, FILE_BEGIN );

		// Read the first 127 or 1023 SAT sectors (508 or 4092 bytes) in the DISAT.
		ReadFile( hFile, g_msat + ( total / sizeof( long ) ), g_si->sect_size - sizeof( long ), &read, NULL );
		total += read;

		if ( read < g_si->sect_size - sizeof( long ) )
		{
			if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Premature end of file encountered while building the Master SAT.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}

		// Get the pointer to the next DISAT.
		ReadFile( hFile, &last_sector, 4, &read, NULL );

		if ( read < 4 )
		{
			if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Premature end of file encountered while building the Master SAT.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
			return SC_FAIL;
		}

		// The last index in the DISAT contains a pointer to the next DISAT. That's assuming there's any more DISATs left.
		last_sector = g_si->sect_size + ( last_sector * g_si->sect_size );
	}

	return SC_OK;
}

unsigned __stdcall read_thumbs( void *pArguments )
{
	// This will block every other thread from entering until the first thread is complete.
	// Protects our global variables.
	EnterCriticalSection( &pe_cs );

	in_thread = true;

	Processing_Window( true );

	pathinfo *pi = ( pathinfo * )pArguments;
	if ( pi != NULL && pi->filepath != NULL )
	{
		int fname_length = 0;
		wchar_t *fname = pi->filepath + pi->offset;

		int filepath_length = wcslen( pi->filepath ) + 1;	// Include NULL character.
		
		bool construct_filepath = ( filepath_length > pi->offset && cmd_line == 0 ? false : true );

		wchar_t *filepath = NULL;

		// We're going to open each file in the path info.
		do
		{
			// Stop processing and exit the thread.
			if ( g_kill_thread == true )
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

					if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "Premature end of file encountered while reading the header.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }

					continue;
				}

				// Make sure it's a thumbs database and the stucture was filled correctly.
				if ( memcmp( dh.magic_identifier, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8 ) != 0 )
				{
					CloseHandle( hFile );
					free( filepath );

					if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "The file is not a thumbs database.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }

					continue;
				}

				// These values are the minimum at which we can multiply the sector size (512) and not go out of range.
				if ( dh.num_sat_sects > 0x7FFFFF || dh.num_ssat_sects > 0x7FFFFF || dh.num_dis_sects > 0x810203 )
				{
					CloseHandle( hFile );
					free( filepath );

					if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "The total sector allocation table size is too large.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }

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

					if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "The total sector allocation table size exceeds the size of the database.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }

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
				if ( cmd_line != 2 ){ MessageBoxA( g_hWnd_main, "The database file failed to open.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING ); }
			}

			// Free the old filepath.
			free( filepath );
		}
		while ( construct_filepath == true && *fname != L'\0' );

		// Save the files or a CSV if the user specified an output directory through the command-line.
		if ( pi->output_path != NULL )
		{
			if ( pi->type == 0 )	// Save thumbnail images.
			{
				save_param *save_type = ( save_param * )malloc( sizeof( save_param ) );
				save_type->type = 1;	// Build directory. It may not exist.
				save_type->save_all = true;
				save_type->filepath = pi->output_path;

				// save_type is freed in the save_items thread.
				CloseHandle( ( HANDLE )_beginthreadex( NULL, 0, &save_items, ( void * )save_type, 0, NULL ) );
			}
			else	// Save CSV.
			{
				// output_path is freed in save_csv.
				CloseHandle( ( HANDLE )_beginthreadex( NULL, 0, &save_csv, ( void * )pi->output_path, 0, NULL ) );
			}
		}

		free( pi->filepath );
	}
	else if ( pi != NULL )	// filepath == NULL
	{
		free( pi->output_path );	// Assume output_path is set.
	}

	free( pi );

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
