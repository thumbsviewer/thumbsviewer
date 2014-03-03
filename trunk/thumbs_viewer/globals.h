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

#ifndef _GLOBALS_H
#define _GLOBALS_H

// Pretty window.
#pragma comment( linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"" )

// Include GDI+ support. We need it to draw .jpg and .png images.
#pragma comment( lib, "gdiplus.lib" )

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <wchar.h>
#include <gdiplus.h>
#include <process.h>

#include "resource.h"

#define PROGRAM_CAPTION L"Thumbs Viewer"

#define MIN_WIDTH		480
#define MIN_HEIGHT		320

#define MENU_OPEN		1001
#define MENU_SAVE_ALL	1002
#define MENU_SAVE_SEL	1003
#define MENU_EXIT		1004
#define MENU_ABOUT		1005
#define MENU_SELECT_ALL	1006
#define MENU_REMOVE_SEL	1007

#define SNAP_WIDTH		10		// The minimum distance at which our windows will attach together.

#define WM_DESTROY_ALT		WM_APP		// Allows non-window threads to call DestroyWindow.
#define WM_CHANGE_CURSOR	WM_APP + 1	// Updates the window cursor.

struct database_header
{
	char magic_identifier[ 8 ]; // {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1} for current version, was {0x0e, 0x11, 0xfc, 0x0d, 0xd0, 0xcf, 0x11, 0xe0} on old, beta 2 files (late '92) 
	char class_id[ 16 ];
	unsigned short minor_version;
	unsigned short dll_version;
	unsigned short byte_order;	// Always 0xFFFE
	unsigned short sector_shift;
	unsigned short short_sect_shift;
	unsigned short reserved_1;
	unsigned long reserved_2;
	unsigned long num_dir_sects;	// Not supported in Version 3 databases.
	unsigned long num_sat_sects;
	long first_dir_sect;
	unsigned long transactioning_sig;
	unsigned long short_sect_cutoff;
	long first_ssat_sect;
	unsigned long num_ssat_sects;
	long first_dis_sect;
	unsigned long num_dis_sects;
};

struct directory_header
{
	wchar_t sid[ 32 ];			// NULL terminated
	unsigned short sid_length;
	char entry_type;			// 0 = Invalid, 1 = Storage, 2 = Stream, 3 = Lock bytes, 4 = Property, 5 = Root
	char node_color;			// 0 = Red, 1 = Black
	long left_child;
	long right_child;
	long dir_id;
	char clsid[ 16 ];
	unsigned long user_flags;
	
	char create_time[ 8 ];
	char modify_time[ 8 ];

	long first_stream_sect;
	unsigned long stream_length;		// Low order bits. Should be less than or equal to 0x80000000 for Version 3 databases.
	unsigned long stream_length_high;	// High order bits.
};

// Holds shared variables among database entries.
struct shared_info
{
	wchar_t dbpath[ MAX_PATH ];
	unsigned char system;		// 0 = Unknown, 1 = Me/2000, 2 = XP/2003, 3 = Vista/2008/7
	unsigned short version;
	long *sat;
	long *ssat;
	char *short_stream_container;
	
	//These are found in the database header.
	unsigned short sect_size;
	unsigned long num_sat_sects;
	long first_dir_sect;
	long first_ssat_sect;
	unsigned long num_ssat_sects;
	long first_dis_sect;
	unsigned long num_dis_sects;
	unsigned long short_sect_cutoff;

	unsigned long count;		// Number of directory entries.
};

// This structure holds information obtained as we read the database. It's passed as an lParam to our listview items.
struct fileinfo
{
	unsigned long offset;				// Offset in SAT or short stream container (depends on size of entry)
	unsigned long size;					// Size of file.
	char extension;						// 0 = none, 1/3 = jpg, 2 = png, 4 = unknown
	wchar_t *filename;					// Name of the database entry.
	long long date_modified;			// Data checksum
	char entry_type;
	shared_info *si;

	fileinfo *next;						// Allows us to process catalog entries in order.
};

// Multi-file open structure.
struct pathinfo
{
	wchar_t *filepath;			// Path to the file/folder
	unsigned short offset;		// Offset to the first file.
	wchar_t *output_path;		// If the user wants to save files.
};

// Save To structure.
struct save_param
{
	wchar_t *filepath;		// Save directory.
	LPITEMIDLIST lpiidl;	// BrowseForFolder variable when saving files.
	bool save_all;			// Save All = true, Save Selected = false.
};

// Function prototypes
LRESULT CALLBACK MainWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

LRESULT CALLBACK ImageWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );
VOID CALLBACK TimerProc( HWND hWnd, UINT msg, UINT idTimer, DWORD dwTime );

unsigned __stdcall cleanup( void *pArguments );
unsigned __stdcall read_database( void *pArguments );
unsigned __stdcall remove_items( void *pArguments );
unsigned __stdcall save_items( void *pArguments );
char *extract( fileinfo *fi, unsigned long &size, unsigned long &header_offset );
Gdiplus::Image *create_image( char *buffer, unsigned long size, unsigned char format, unsigned int raw_width = 0, unsigned int raw_height = 0, unsigned int raw_size = 0, int raw_stride = 0 );
bool is_close( int a, int b );
void update_menus( bool disable_all );

// These are all variables that are shared among the separate .cpp files.

// Object handles.
extern HWND g_hWnd_main;			// Handle to our main window.
extern HWND g_hWnd_image;			// Handle to our image window.
extern HWND g_hWnd_list;			// Handle to the listview control.

extern CRITICAL_SECTION pe_cs;		// Allow only one read_database thread to be active.

extern HFONT hFont;					// Handle to the system's message font.

extern HICON hIcon_jpg;				// Handle to the system's .jpg icon.
extern HICON hIcon_png;				// Handle to the system's .png icon.

extern HMENU g_hMenu;				// Handle to our menu bar.
extern HMENU g_hMenuSub_context;	// Handle to our context menu.

// Window variables
extern RECT last_dim;				// Keeps track of the image window's dimension before it gets minimized.

extern bool is_attached;			// Toggled when our windows are attached
extern bool skip_main;				// Prevents the main window from moving the image window if it is about to attach.

extern char cmd_line;				// Show the main window and message prompts.

// Image variables
extern Gdiplus::Image *gdi_image;	// GDI+ image object. We need it to handle .png and .jpg images specifically.

extern POINT drag_rect;				// The current position of gdi_image in the image window.
extern POINT old_pos;				// The old position of gdi_image. Used to calculate the rate of change.

extern float scale;					// Scale of the image.

// Thread variables
extern bool kill_thread;			// Allow for a clean shutdown.

extern bool in_thread;				// Flag to indicate that we're in a worker thread.
extern bool skip_draw;				// Prevents WM_DRAWITEM from accessing listview items while we're removing them.

#endif
