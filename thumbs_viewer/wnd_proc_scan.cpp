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
#include "map_entries.h"

#define BTN_SCAN	1001
#define BTN_CANCEL	1002
#define BTN_LOAD	1003
#define BTN_DETAILS	1004
#define EDIT_PATH	1005

HWND g_hWnd_path = NULL;
HWND g_hWnd_extensions = NULL;
HWND g_hWnd_chk_folders = NULL;
HWND g_hWnd_hashing = NULL;
HWND g_hWnd_btn_scan = NULL;
HWND g_hWnd_btn_cancel = NULL;
HWND g_hWnd_load = NULL;
HWND g_hWnd_static_hash = NULL;
HWND g_hWnd_static_count = NULL;
HWND g_hWnd_btn_details = NULL;
HWND g_hWnd_static3 = NULL;
HWND g_hWnd_static4 = NULL;
HWND g_hWnd_static5 = NULL;

LRESULT CALLBACK ScanWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    switch ( msg )
    {
		case WM_CREATE:
		{
			RECT rc;
			GetClientRect( hWnd, &rc );

			wchar_t current_directory[ MAX_PATH ] = { 0 };
			GetCurrentDirectory( MAX_PATH, current_directory );

			HWND g_hWnd_static1 = CreateWindowA( WC_STATICA, "Initial scan directory:", WS_CHILD | WS_VISIBLE, 20, 20, rc.right - 40, 15, hWnd, NULL, NULL, NULL );
			g_hWnd_path = CreateWindowEx( WS_EX_CLIENTEDGE, WC_EDIT, current_directory, ES_AUTOHSCROLL | ES_READONLY | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, ( HMENU )EDIT_PATH, NULL, NULL );
			SendMessage( g_hWnd_path, EM_LIMITTEXT, MAX_PATH - 1, 0 );

			g_hWnd_load = CreateWindowA( WC_BUTTONA, "...", WS_CHILD | WS_TABSTOP | WS_VISIBLE, 0, 0, 0, 0, hWnd, ( HMENU )BTN_LOAD, NULL, NULL );

			HWND g_hWnd_static2 = CreateWindowA( WC_STATICA, "Limit scan to the following file types:", WS_CHILD | WS_VISIBLE, 20, 65, rc.right - 40, 15, hWnd, NULL, NULL, NULL );
			g_hWnd_extensions = CreateWindowEx( WS_EX_CLIENTEDGE, WC_EDIT, L".jpg|.jpeg|.png|.bmp|.gif", ES_AUTOHSCROLL | WS_CHILD | WS_TABSTOP | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL );
			SendMessage( g_hWnd_extensions, EM_LIMITTEXT, MAX_PATH - 1, 0 );

			g_hWnd_chk_folders = CreateWindowA( WC_BUTTONA, "Include Folders", BS_AUTOCHECKBOX | WS_CHILD | WS_TABSTOP | WS_VISIBLE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL );

			g_hWnd_static3 = CreateWindowA( WC_STATICA, "Current file/folder:", WS_CHILD, 20, 110, rc.right - 40, 15, hWnd, NULL, NULL, NULL );
			g_hWnd_hashing = CreateWindowEx( WS_EX_CLIENTEDGE, WC_EDIT, NULL, ES_AUTOHSCROLL | ES_READONLY | WS_CHILD, 0, 0, 0, 0, hWnd, NULL, NULL, NULL );

			g_hWnd_static4 = CreateWindowA( WC_STATICA, "Current file/folder hash:", WS_CHILD, 20, 155, rc.right - 310, 15, hWnd, NULL, NULL, NULL );
			g_hWnd_static_hash = CreateWindow( WC_STATIC, NULL, WS_CHILD, rc.right - 290, 155, rc.right - 195, 15, hWnd, NULL, NULL, NULL );

			g_hWnd_static5 = CreateWindowA( WC_STATICA, "Total files and/or folders:", WS_CHILD, 20, 175, rc.right - 310, 15, hWnd, NULL, NULL, NULL );
			g_hWnd_static_count = CreateWindow( WC_STATIC, NULL, WS_CHILD, rc.right - 290, 175, rc.right - 195, 15, hWnd, NULL, NULL, NULL );

			g_hWnd_btn_details = CreateWindowA( WC_BUTTONA, "Show Details \xBB", WS_CHILD | WS_TABSTOP | WS_VISIBLE, 0, 0, 0, 0, hWnd, ( HMENU )BTN_DETAILS, NULL, NULL );

			g_hWnd_btn_scan = CreateWindowA( WC_BUTTONA, "Scan", BS_DEFPUSHBUTTON | WS_CHILD | WS_TABSTOP | WS_VISIBLE, 0, 0, 0, 0, hWnd, ( HMENU )BTN_SCAN, NULL, NULL );
			g_hWnd_btn_cancel = CreateWindowA( WC_BUTTONA, "Cancel", WS_CHILD | WS_TABSTOP | WS_VISIBLE, 0, 0, 0, 0, hWnd, ( HMENU )BTN_CANCEL, NULL, NULL );

			// Make pretty font.
			SendMessage( g_hWnd_static1, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_static2, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_static3, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_static4, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_static5, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_path, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_load, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_extensions, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_chk_folders, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_hashing, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_static_hash, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_static_count, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_btn_details, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_btn_scan, WM_SETFONT, ( WPARAM )hFont, 0 );
			SendMessage( g_hWnd_btn_cancel, WM_SETFONT, ( WPARAM )hFont, 0 );

			return 0;
		}
		break;

		case WM_CTLCOLORSTATIC:
		{
			return ( LRESULT )( GetSysColorBrush( COLOR_WINDOW ) );
		}
		break;

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint( hWnd, &ps );

			RECT client_rc, frame_rc;
			GetClientRect( hWnd, &client_rc );

			// Create a memory buffer to draw to.
			HDC hdcMem = CreateCompatibleDC( hDC );

			HBITMAP hbm = CreateCompatibleBitmap( hDC, client_rc.right - client_rc.left, client_rc.bottom - client_rc.top );
			HBITMAP ohbm = ( HBITMAP )SelectObject( hdcMem, hbm );
			DeleteObject( ohbm );
			DeleteObject( hbm );

			// Fill the background.
			HBRUSH color = CreateSolidBrush( ( COLORREF )GetSysColor( COLOR_MENU ) );
			FillRect( hdcMem, &client_rc, color );
			DeleteObject( color );

			frame_rc = client_rc;
			frame_rc.left += 10;
			frame_rc.right -= 10;
			frame_rc.top += 10;
			frame_rc.bottom -= 40;

			// Fill the frame.
			color = CreateSolidBrush( ( COLORREF )GetSysColor( COLOR_WINDOW ) );
			FillRect( hdcMem, &frame_rc, color );
			DeleteObject( color );

			// Draw the frame's border.
			DrawEdge( hdcMem, &frame_rc, EDGE_ETCHED, BF_RECT );

			// Draw our memory buffer to the main device context.
			BitBlt( hDC, client_rc.left, client_rc.top, client_rc.right, client_rc.bottom, hdcMem, 0, 0, SRCCOPY );

			DeleteDC( hdcMem );
			EndPaint( hWnd, &ps );

			return 0;
		}
		break;

		case WM_CHANGE_CURSOR:
		{
			// SetCursor must be called from the window thread.
			if ( wParam == TRUE )
			{
				wait_cursor = LoadCursor( NULL, IDC_APPSTARTING );	// Arrow + hourglass.
				SetCursor( wait_cursor );
			}
			else
			{
				SetCursor( LoadCursor( NULL, IDC_ARROW ) );	// Default arrow.
				wait_cursor = NULL;
			}
		}
		break;

		case WM_SETCURSOR:
		{
			if ( wait_cursor != NULL )
			{
				SetCursor( wait_cursor );	// Keep setting our cursor if it reverts back to the default.
				return TRUE;
			}

			DefWindowProc( hWnd, msg, wParam, lParam );
			return FALSE;
		}
		break;

		case WM_COMMAND:
		{
			switch( LOWORD( wParam ) )
			{
				case IDOK:
				case BTN_SCAN:
				{
					if ( g_kill_scan == false )
					{
						EnableWindow( g_hWnd_btn_scan, FALSE );
						g_kill_scan = true;
						break;
					}

					int length = SendMessage( g_hWnd_path, WM_GETTEXT, MAX_PATH, ( LPARAM )g_filepath );
					if ( length >= 3 )
					{
						// We need to have at least the drive path. Example: "C:\"
						if ( g_filepath[ 1 ] == L':' && g_filepath[ 2 ] == L'\\' )
						{
							// Remove any trailing "\" from the path.
							if ( g_filepath[ length - 1 ] == L'\\' )
							{
								g_filepath[ length - 1 ] = '\0';
							}

							// Now get our extension filters.
							length = SendMessage( g_hWnd_extensions, WM_GETTEXT, MAX_PATH, ( LPARAM )( g_extension_filter + 1 ) );
							if ( length > 0 )
							{
								g_extension_filter[ 0 ] = L'|';				// Append the delimiter to the beginning of the string.
								g_extension_filter[ length + 1 ] = L'|';		// Append the delimiter to the end of the string.
								g_extension_filter[ length + 2 ] = L'\0';
								_wcslwr_s( g_extension_filter, length + 3 );	// Set them to lowercase for later comparison.
							}
							else
							{
								g_extension_filter[ 0 ] = L'\0';
							}

							g_include_folders = SendMessage( g_hWnd_chk_folders, BM_GETCHECK, 0, 0 ) ? true : false;

							// Run the scan thread.
							CloseHandle( ( HANDLE )_beginthreadex( NULL, 0, &map_entries, NULL, 0, NULL ) );
						}
						else
						{
							MessageBoxA( hWnd, "You must specify a valid path.", PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONWARNING );
						}
					}
				}
				break;

				case BTN_CANCEL:
				{
					SendMessage( hWnd, WM_CLOSE, 0, 0 );
				}
				break;

				case BTN_DETAILS:
				{
					g_show_details = !g_show_details;

					// Hiding the details will allow for a faster scan since it doesn't have to update our controls.
					if ( g_show_details == true )
					{
						SendMessageA( g_hWnd_btn_details, WM_SETTEXT, 0, ( LPARAM )"Hide Details \xAB" );
						ShowWindow( g_hWnd_static3, SW_SHOW );
						ShowWindow( g_hWnd_static4, SW_SHOW );
						ShowWindow( g_hWnd_static5, SW_SHOW );
						ShowWindow( g_hWnd_hashing, SW_SHOW );
						ShowWindow( g_hWnd_static_hash, SW_SHOW );
						ShowWindow( g_hWnd_static_count, SW_SHOW );
					}
					else
					{
						SendMessageA( g_hWnd_btn_details, WM_SETTEXT, 0, ( LPARAM )"Show Details \xBB" );
						ShowWindow( g_hWnd_static3, SW_HIDE );
						ShowWindow( g_hWnd_static4, SW_HIDE );
						ShowWindow( g_hWnd_static5, SW_HIDE );
						ShowWindow( g_hWnd_hashing, SW_HIDE );
						ShowWindow( g_hWnd_static_hash, SW_HIDE );
						ShowWindow( g_hWnd_static_count, SW_HIDE );
					}

					// Adjust the window height.
					RECT rc;
					GetWindowRect( hWnd, &rc );
					SetWindowPos( hWnd, NULL, 0, 0, rc.right - rc.left, MIN_HEIGHT - ( g_show_details == true ? 45 : 135 ), SWP_NOMOVE );
				}
				break;

				case BTN_LOAD:
				{
					// Open a browse for folder dialog box.
					BROWSEINFO bi = { 0 };
					bi.hwndOwner = hWnd;
					bi.lpszTitle = L"Select a location to scan.";
					bi.ulFlags = BIF_EDITBOX | BIF_VALIDATE;

					LPITEMIDLIST lpiidl = SHBrowseForFolder( &bi );
					if ( lpiidl )
					{
						wchar_t scan_directory[ MAX_PATH ] = { 0 };
						// Get the directory path from the id list.
						SHGetPathFromIDList( lpiidl, scan_directory );
						CoTaskMemFree( lpiidl );

						SendMessage( g_hWnd_path, WM_SETTEXT, 0, ( LPARAM )scan_directory );
					}
				}
				break;

				case EDIT_PATH:
				{
					if ( HIWORD( wParam ) == EN_CHANGE )
					{
						// Ensure that there's at least 3 characters (for a drive) to scan.
						EnableWindow( g_hWnd_btn_scan, ( SendMessage( g_hWnd_path, WM_GETTEXTLENGTH, 0, 0 ) >= 3 ) ? TRUE : FALSE );
					}
				}
				break;
			}

			return 0;
		}
		break;

		case WM_SIZE:
		{
			RECT rc;
			GetClientRect( hWnd, &rc );

			// Allow our controls to move in relation to the parent window.
			HDWP hdwp = BeginDeferWindowPos( 8 );
			DeferWindowPos( hdwp, g_hWnd_path, HWND_TOP, 20, 35, rc.right - 75, 20, SWP_NOZORDER );
			DeferWindowPos( hdwp, g_hWnd_load, HWND_TOP, rc.right - 50, 35, 30, 20, SWP_NOZORDER );
			DeferWindowPos( hdwp, g_hWnd_extensions, HWND_TOP, 20, 80, rc.right - 145, 20, SWP_NOZORDER );
			DeferWindowPos( hdwp, g_hWnd_chk_folders, HWND_TOP, rc.right - 120, 80, 100, 20, SWP_NOZORDER );
			DeferWindowPos( hdwp, g_hWnd_hashing, HWND_TOP, 20, 125, rc.right - 40, 20, SWP_NOZORDER );
			DeferWindowPos( hdwp, g_hWnd_btn_details, HWND_TOP, 10, rc.bottom - 32, 100, 23, SWP_NOZORDER );
			DeferWindowPos( hdwp, g_hWnd_btn_scan, HWND_TOP, rc.right - 175, rc.bottom - 32, 80, 23, SWP_NOZORDER );
			DeferWindowPos( hdwp, g_hWnd_btn_cancel, HWND_TOP, rc.right - 90, rc.bottom - 32, 80, 23, SWP_NOZORDER );
			EndDeferWindowPos( hdwp );

			return 0;
		}
		break;

		case WM_GETMINMAXINFO:
		{
			// Set the minimum dimensions that the window can be sized to.
			( ( MINMAXINFO * )lParam )->ptMinTrackSize.x = MIN_WIDTH;
			( ( MINMAXINFO * )lParam )->ptMinTrackSize.y = MIN_HEIGHT - ( g_show_details == true ? 45 : 135 );
			( ( MINMAXINFO * )lParam )->ptMaxTrackSize.y = MIN_HEIGHT - ( g_show_details == true ? 45 : 135 );

			return 0;
		}
		break;

		case WM_ACTIVATE:
		{
			// 0 = inactive, > 0 = active
			g_hWnd_active = ( wParam == 0 ? NULL : hWnd );

            return FALSE;
		}
		break;

		case WM_CLOSE:
		{
			g_kill_scan = true;

			// Reenable the main window.
			EnableWindow( g_hWnd_main, TRUE );
			SetForegroundWindow( g_hWnd_main );

			ShowWindow( hWnd, SW_HIDE );
		}
		break;

		case WM_ALERT:
		{
			MessageBoxA( hWnd, ( LPCSTR )lParam, PROGRAM_CAPTION_A, MB_APPLMODAL | MB_ICONINFORMATION | MB_SETFOREGROUND );
		}
		break;

		case WM_PROPAGATE:
		{
			if ( wParam == 1 )
			{
				g_kill_scan = false;

				EnableWindow( g_hWnd_path, FALSE );
				EnableWindow( g_hWnd_load, FALSE );
				EnableWindow( g_hWnd_extensions, FALSE );
				EnableWindow( g_hWnd_chk_folders, FALSE );

				// We're scanning. Set the button's text to "Stop".
				SendMessageA( g_hWnd_btn_scan, WM_SETTEXT, 0, ( LPARAM )"Stop" );
			}
			else if ( wParam == 2 )
			{
				// Clear the current file info if we finished the scan without stopping.
				if ( g_kill_scan == false )
				{
					SendMessage( g_hWnd_hashing, WM_SETTEXT, 0, 0 );
					SendMessageA( g_hWnd_static_hash, WM_SETTEXT, 0, 0 );
				}

				g_kill_scan = true;

				EnableWindow( g_hWnd_path, TRUE );
				EnableWindow( g_hWnd_load, TRUE );
				EnableWindow( g_hWnd_extensions, TRUE );
				EnableWindow( g_hWnd_chk_folders, TRUE );

				// We've stopped/finished scanning. Set the button's text to "Scan".
				SendMessageA( g_hWnd_btn_scan, WM_SETTEXT, 0, ( LPARAM )"Scan" );
				EnableWindow( g_hWnd_btn_scan, ( SendMessage( g_hWnd_path, WM_GETTEXTLENGTH, 0, 0 ) >= 3 ) ? TRUE : FALSE );
			}
			else if ( wParam == 3 )
			{
				SendMessage( g_hWnd_hashing, WM_SETTEXT, 0, lParam );
			}
			else if ( wParam == 4 )
			{
				SendMessageA( g_hWnd_static_hash, WM_SETTEXT, 0, lParam );
			}
			else if ( wParam == 5 )
			{
				SendMessageA( g_hWnd_static_count, WM_SETTEXT, 0, lParam );
			}
			else
			{
				g_kill_scan = true;

				// Reset text information.
				SendMessage( g_hWnd_hashing, WM_SETTEXT, 0, 0 );
				SendMessageA( g_hWnd_static_hash, WM_SETTEXT, 0, 0 );
				SendMessageA( g_hWnd_static_count, WM_SETTEXT, 0, 0 );
				SendMessageA( g_hWnd_btn_scan, WM_SETTEXT, 0, ( LPARAM )"Scan" );
				EnableWindow( g_hWnd_btn_scan, ( SendMessage( g_hWnd_path, WM_GETTEXTLENGTH, 0, 0 ) >= 3 ) ? TRUE : FALSE );

				// Disable the main window.
				EnableWindow( g_hWnd_main, FALSE );

				// Set the window above all other windows.
				SetForegroundWindow( hWnd );
				SetWindowPos( hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
				ShowWindow( hWnd, SW_SHOW );
			}
		}
		break;

		default:
		{
			return DefWindowProc( hWnd, msg, wParam, lParam );
		}
		break;
	}
	return TRUE;
}
