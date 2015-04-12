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

#ifndef UTILITIES_H
#define UTILITIES_H

#include "globals.h"
#include "dllrbt.h"

#define SNAP_WIDTH		10		// The minimum distance at which our windows will attach together.

unsigned __stdcall cleanup( void *pArguments );
unsigned __stdcall remove_items( void *pArguments );
unsigned __stdcall save_csv( void *pArguments );
unsigned __stdcall save_items( void *pArguments );
unsigned __stdcall copy_items( void *pArguments );

wchar_t *get_extension_from_filename( wchar_t *filename, unsigned long length );
wchar_t *get_filename_from_path( wchar_t *path, unsigned long length );
void reverse_string( wchar_t *string );
char *escape_csv( const char *string );

void cleanup_shared_info( shared_info **si );
void cleanup_fileinfo_tree();
void create_fileinfo_tree();

bool is_close( int a, int b );

void Processing_Window( bool enable );

Gdiplus::Image *create_image( char *buffer, unsigned long size, unsigned char format, unsigned int raw_width = 0, unsigned int raw_height = 0, unsigned int raw_size = 0, int raw_stride = 0 );

extern HANDLE shutdown_semaphore;	// Blocks shutdown while a worker thread is active.
extern dllrbt_tree *fileinfo_tree;	// Red-black tree of fileinfo structures.

#endif
