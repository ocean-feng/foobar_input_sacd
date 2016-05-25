/*
* SACD Decoder plugin
* Copyright (c) 2011-2015 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with FFmpeg; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef _DST_DECODER_H_INCLUDED
#define _DST_DECODER_H_INCLUDED

#include <windows.h>
#include "DSTDecoder.h"

#define DST_DECODER_THREADS 8

enum slot_state_t {SLOT_EMPTY, SLOT_LOADED, SLOT_RUNNING, SLOT_READY, SLOT_READY_WITH_ERROR, SLOT_TERMINATING};

class frame_slot_t {
public:
	bool        run_slot;
	int         state;
	uint8_t*    dsd_data;
	int         dsd_size;
	uint8_t*    dst_data;
	int         dst_size;
	int         channel_count;
	int         samplerate;
	int         framerate;
	HANDLE      hThread;
	HANDLE      hEventGet;
	HANDLE      hEventPut;
	int         frame_nr;
	CDSTDecoder D;
	frame_slot_t() {
		run_slot = false;
		state = SLOT_EMPTY;
		dsd_data = nullptr;
		dsd_size = 0;
		dst_data = nullptr;
		dst_size = 0;
		channel_count = 0;
		samplerate = 0;
		framerate = 0;
		hThread = NULL;
		hEventGet = NULL;
		hEventPut = NULL;
		frame_nr = 0;
	}
};

class dst_decoder_t {
	frame_slot_t* frame_slots;
	int           slot_nr;       
	int           thread_count;
	int           channel_count;
	int           samplerate;
	int           framerate;
	uint32_t      frame_nr;
public:
	dst_decoder_t(int threads = DST_DECODER_THREADS);
	~dst_decoder_t();
	int get_slot_nr();
	int init(int channel_count, int samplerate, int framerate);
	int decode(uint8_t* dst_data, size_t dst_size, uint8_t** dsd_data, size_t* dsd_size);
};

#endif
