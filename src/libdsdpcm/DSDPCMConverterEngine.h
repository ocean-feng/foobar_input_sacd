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

#pragma once

#include <windows.h>
#include "DSDPCMConverterMultistage.h"
#include "DSDPCMConverterDirect.h"

template<typename real_t>
class DSDPCMConverterSlot {
public:
	uint8_t* dsd_data;
	int      dsd_samples;
	real_t*  pcm_data;
	int      pcm_samples;
	bool     run_slot;
	DSDPCMConverter<real_t>* converter;
	HANDLE hThread;
	HANDLE hEventGet;
	HANDLE hEventPut;
	DSDPCMConverterSlot() {
		dsd_data = nullptr;
		dsd_samples = 0;
		pcm_data = nullptr;
		pcm_samples = 0;
		run_slot = false;
		converter = nullptr;
		hThread = NULL;
		hEventGet = NULL;
		hEventPut = NULL;
	}
};

class DSDPCMConverterEngine {
	int   channels;
	int   framerate;
	int   dsd_samplerate;
	int   pcm_samplerate;
	float dB_gain;
	float conv_delay;
	conv_type_e conv_type;
	bool        conv_fp64;
	bool        conv_called;
	DSDPCMFilterSetup<float>     fltSetup_fp32;
	DSDPCMFilterSetup<double>    fltSetup_fp64;
	DSDPCMConverterSlot<float>*  convSlots_fp32;
	DSDPCMConverterSlot<double>* convSlots_fp64;
	uint8_t swap_bits[256];
public:
	DSDPCMConverterEngine();
	~DSDPCMConverterEngine();
	float get_delay();
	void set_gain(float dB_gain);
	bool is_convert_called();
	int init(int channels, int framerate, int dsd_samplerate, int pcm_samplerate, conv_type_e conv_type, bool conv_fp64, double* fir_coefs, int fir_length, bool skip_init);
	int free();
	int convert(uint8_t* dsd_data, int dsd_samples, float* pcm_data);
private:
	template<typename real_t> DSDPCMConverterSlot<real_t>* init_slots(DSDPCMFilterSetup<real_t>& fltSetup);
	template<typename real_t> void free_slots(DSDPCMConverterSlot<real_t>* convSlots);
	template<typename real_t> int convert(DSDPCMConverterSlot<real_t>* convSlots, uint8_t* dsd_data, int dsd_samples, float* pcm_data);
	template<typename real_t> int convertL(DSDPCMConverterSlot<real_t>* convSlots, uint8_t* dsd_data, int dsd_samples);
	template<typename real_t> int convertR(DSDPCMConverterSlot<real_t>* convSlots, float* pcm_data);
};
