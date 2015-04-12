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

#ifndef READ_THUMBS_H
#define READ_THUMBS_H

#include "globals.h"

#define FILE_TYPE_JPEG	"\xFF\xD8\xFF\xE0"
#define FILE_TYPE_PNG	"\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"

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

// Return status codes for various functions.
#define SC_FAIL	0
#define SC_OK	1
#define SC_QUIT	2

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

unsigned __stdcall read_thumbs( void *pArguments );

char *extract( fileinfo *fi, unsigned long &size, unsigned long &header_offset );

#endif
