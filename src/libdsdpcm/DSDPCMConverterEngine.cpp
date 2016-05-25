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

#include <math.h>
#include <stdio.h>
#include "DSDPCMConverterEngine.h"

#define LOG_ERROR   ("Error: ")
#define LOG_WARNING ("Warning: ")
#define LOG_INFO    ("Info: ")
#define LOG(p1, p2) console_fprintf(nullptr, "%s%s", p1, p2)

extern void console_fprintf(FILE* file, const char* fmt, ...);
extern void console_vfprintf(FILE* file, const char* fmt, va_list vl);

template<typename real_t>
static DWORD WINAPI ConverterThread(LPVOID threadarg) {
	DSDPCMConverterSlot<real_t>* slot = reinterpret_cast<DSDPCMConverterSlot<real_t>*>(threadarg);
	while (slot->run_slot) {
		WaitForSingleObject(slot->hEventPut, INFINITE);
		if (slot->run_slot) {
			slot->pcm_samples = slot->converter->convert(slot->dsd_data, slot->pcm_data, slot->dsd_samples);
		}
		else {
			slot->pcm_samples = 0;
		}
		SetEvent(slot->hEventGet);
	}
	return 0;
}

DSDPCMConverterEngine::DSDPCMConverterEngine() {
	channels = 0;
	framerate = 0;
	dsd_samplerate = 0;
	pcm_samplerate = 0;
	dB_gain = 0.0f;
	conv_delay = 0.0f;
	conv_type = DSDPCM_CONV_UNKNOWN;
	convSlots_fp32 = nullptr;
	convSlots_fp64 = nullptr;
	conv_called = false;
	for (int i = 0; i < 256; i++) {
		swap_bits[i] = 0;
		for (int j = 0; j < 8; j++) {
			swap_bits[i] |= ((i >> j) & 1) << (7 - j);
		}
	}
}

DSDPCMConverterEngine::~DSDPCMConverterEngine() {
	free();
	conv_delay = 0.0f;
}

float DSDPCMConverterEngine::get_delay() {
	return conv_delay;
}

void DSDPCMConverterEngine::set_gain(float dB_gain) {
	this->dB_gain = dB_gain;
}


bool DSDPCMConverterEngine::is_convert_called() {
	return conv_called;
}

int DSDPCMConverterEngine::init(int channels, int framerate, int dsd_samplerate, int pcm_samplerate, conv_type_e conv_type, bool conv_fp64, double* fir_coefs, int fir_length, bool skip_init) {
	if (skip_init && this->channels == channels && this->framerate == framerate && this->dsd_samplerate == dsd_samplerate && this->pcm_samplerate == pcm_samplerate) {
		return 1;
	}
	if (conv_type == DSDPCM_CONV_USER) {
		if (!(fir_coefs && fir_length > 0)) {
			return -2;
		}
	}
	free();
	this->channels = channels;
	this->framerate = framerate;
	this->dsd_samplerate = dsd_samplerate;
	this->pcm_samplerate = pcm_samplerate;
	this->conv_type = conv_type;
	this->conv_fp64 = conv_fp64;
	if (conv_fp64) {
		fltSetup_fp64.set_gain(dB_gain);
		fltSetup_fp64.set_fir1_64_coefs(fir_coefs, fir_length);
		convSlots_fp64 = init_slots<double>(fltSetup_fp64);
		conv_delay = convSlots_fp64[0].converter->get_delay();
	}
	else {
		fltSetup_fp32.set_gain(dB_gain);
		fltSetup_fp32.set_fir1_64_coefs(fir_coefs, fir_length);
		convSlots_fp32 = init_slots<float>(fltSetup_fp32);
		conv_delay = convSlots_fp32[0].converter->get_delay();
	}
	conv_called = false;
	return 0;
}

int DSDPCMConverterEngine::free() {
	if (convSlots_fp64) {
		free_slots<double>(convSlots_fp64);
		convSlots_fp64 = nullptr;
	}
	if (convSlots_fp32) {
		free_slots<float>(convSlots_fp32);
		convSlots_fp32 = nullptr;
	}
	return 0;
}

int DSDPCMConverterEngine::convert(uint8_t* dsd_data, int dsd_samples, float* pcm_data) {
	int pcm_samples = 0;
	if (!dsd_data) {
		if (convSlots_fp64) {
			pcm_samples = convertR<double>(convSlots_fp64, pcm_data);
		}
		if (convSlots_fp32) {
			pcm_samples = convertR<float>(convSlots_fp32, pcm_data);
		}
		return pcm_samples;
	}
	if (!conv_called) {
		if (convSlots_fp64) {
			convertL<double>(convSlots_fp64, dsd_data, dsd_samples);
		}
		if (convSlots_fp32) {
			convertL<float>(convSlots_fp32, dsd_data, dsd_samples);
		}
		conv_called = true;
	}
	if (convSlots_fp64) {
		pcm_samples = convert<double>(convSlots_fp64, dsd_data, dsd_samples, pcm_data);
	}
	if (convSlots_fp32) {
		pcm_samples = convert<float>(convSlots_fp32, dsd_data, dsd_samples, pcm_data);
	}
	return pcm_samples;
}

template<typename real_t>
DSDPCMConverterSlot<real_t>* DSDPCMConverterEngine::init_slots(DSDPCMFilterSetup<real_t>& fltSetup) {
	DSDPCMConverterSlot<real_t>* convSlots = new DSDPCMConverterSlot<real_t>[channels];
	int dsd_samples = dsd_samplerate / 8 / framerate;
	int pcm_samples = pcm_samplerate / framerate;
	int decimation = dsd_samplerate / pcm_samplerate;
	for (int ch = 0; ch < channels; ch++) {
		DSDPCMConverterSlot<real_t>* slot = &convSlots[ch];
		slot->dsd_data = (uint8_t*)DSDPCMUtil::mem_alloc(dsd_samples * sizeof(uint8_t));
		slot->dsd_samples = dsd_samples;
		slot->pcm_data = (real_t*)DSDPCMUtil::mem_alloc(pcm_samples * sizeof(real_t));
		slot->pcm_samples = 0;
		switch (conv_type) {
		case DSDPCM_CONV_MULTISTAGE:
		{
			DSDPCMConverterMultistage<real_t>* pConv = nullptr;
			switch (decimation) {
			case 512:
				pConv = new DSDPCMConverterMultistage_x512<real_t>();
				break;
			case 256:
				pConv = new DSDPCMConverterMultistage_x256<real_t>();
				break;
			case 128:
				pConv = new DSDPCMConverterMultistage_x128<real_t>();
				break;
			case 64:
				pConv = new DSDPCMConverterMultistage_x64<real_t>();
				break;
			case 32:
				pConv = new DSDPCMConverterMultistage_x32<real_t>();
				break;
			case 16:
				pConv = new DSDPCMConverterMultistage_x16<real_t>();
				break;
			case 8:
				pConv = new DSDPCMConverterMultistage_x8<real_t>();
				break;
			}
			pConv->init(fltSetup, dsd_samples);
			slot->converter = pConv;
			break;
		}
		case DSDPCM_CONV_DIRECT:
		case DSDPCM_CONV_USER:
		{
			DSDPCMConverterDirect<real_t>* pConv = nullptr;
			switch (decimation) {
			case 512:
				pConv = new DSDPCMConverterDirect_x512<real_t>();
				break;
			case 256:
				pConv = new DSDPCMConverterDirect_x256<real_t>();
				break;
			case 128:
				pConv = new DSDPCMConverterDirect_x128<real_t>();
				break;
			case 64:
				pConv = new DSDPCMConverterDirect_x64<real_t>();
				break;
			case 32:
				pConv = new DSDPCMConverterDirect_x32<real_t>();
				break;
			case 16:
				pConv = new DSDPCMConverterDirect_x16<real_t>();
				break;
			case 8:
				pConv = new DSDPCMConverterDirect_x8<real_t>();
				break;
			}
			pConv->init(fltSetup, dsd_samples);
			slot->converter = pConv;
			break;
		}
		default:
			break;
		}
		slot->run_slot = true;
		slot->hEventGet = CreateEvent(NULL, FALSE, FALSE, NULL);
		slot->hEventPut = CreateEvent(NULL, FALSE, FALSE, NULL);
		slot->hThread = CreateThread(NULL, 0, ConverterThread<real_t>, (LPVOID)slot, 0, NULL);
	}
	return convSlots;
}

template<typename real_t>
void DSDPCMConverterEngine::free_slots(DSDPCMConverterSlot<real_t>* convSlots) {
	for (int ch = 0; ch < channels; ch++) {
		DSDPCMConverterSlot<real_t>* slot = &convSlots[ch];
		slot->run_slot = false;
		SetEvent(slot->hEventPut); // Release worker (decoding) thread for exit
		// Wait until worker (decoding) thread exit
		if (WaitForSingleObject(slot->hThread, 100) == WAIT_TIMEOUT)	{
			TerminateThread(slot->hThread, 0);
			LOG(LOG_ERROR, ("Could not stop DSD to PCM converter thread"));
		}
		CloseHandle(slot->hEventGet);
		CloseHandle(slot->hEventPut);
		CloseHandle(slot->hThread);
		delete slot->converter;
		slot->converter = nullptr;
		DSDPCMUtil::mem_free(slot->dsd_data);
		slot->dsd_data = nullptr;
		slot->dsd_samples = 0;
		DSDPCMUtil::mem_free(slot->pcm_data);
		slot->pcm_data = nullptr;
		slot->pcm_samples = 0;
	}
	delete[] convSlots;
	convSlots = nullptr;
}

template<typename real_t>
int DSDPCMConverterEngine::convert(DSDPCMConverterSlot<real_t>* convSlots, uint8_t* dsd_data, int dsd_samples, float* pcm_data) {
	int pcm_samples = 0;
	for (int ch = 0; ch < channels; ch++)	{
		DSDPCMConverterSlot<real_t>* slot = &convSlots[ch];
		slot->dsd_samples = dsd_samples / channels;
		for (int sample = 0; sample < slot->dsd_samples; sample++)	{
			slot->dsd_data[sample] = dsd_data[sample * channels + ch];
		}
		SetEvent(slot->hEventPut); // Release worker (decoding) thread on the loaded slot
	}
	for (int ch = 0; ch < channels; ch++)	{
		DSDPCMConverterSlot<real_t>* slot = &convSlots[ch];
		WaitForSingleObject(slot->hEventGet, INFINITE);	// Wait until worker (decoding) thread is complete
		for (int sample = 0; sample < slot->pcm_samples; sample++)	{
			pcm_data[sample * channels + ch] = (float)slot->pcm_data[sample];
		}
		pcm_samples += slot->pcm_samples;
	}
	return pcm_samples;
}

template<typename real_t>
int DSDPCMConverterEngine::convertL(DSDPCMConverterSlot<real_t>* convSlots, uint8_t* dsd_data, int dsd_samples) {
	for (int ch = 0; ch < channels; ch++)	{
		DSDPCMConverterSlot<real_t>* slot = &convSlots[ch];
		slot->dsd_samples = dsd_samples / channels;
		for (int sample = 0; sample < slot->dsd_samples; sample++)	{
			slot->dsd_data[sample] = swap_bits[dsd_data[(slot->dsd_samples - 1 - sample) * channels + ch]];
		}
		SetEvent(convSlots[ch].hEventPut); // Release worker (decoding) thread on the loaded slot
	}
	for (int ch = 0; ch < channels; ch++)	{
		DSDPCMConverterSlot<real_t>* slot = &convSlots[ch];
		WaitForSingleObject(slot->hEventGet, INFINITE);	// Wait until worker (decoding) thread is complete
	}
	return 0;
}

template<typename real_t>
int DSDPCMConverterEngine::convertR(DSDPCMConverterSlot<real_t>* convSlots, float* pcm_data) {
	int pcm_samples = 0;
	for (int ch = 0; ch < channels; ch++)	{
		DSDPCMConverterSlot<real_t>* slot = &convSlots[ch];
		for (int sample = 0; sample < slot->dsd_samples / 2; sample++)	{
			uint8_t temp = slot->dsd_data[slot->dsd_samples - 1 - sample];
			slot->dsd_data[slot->dsd_samples - 1 - sample] = swap_bits[slot->dsd_data[sample]];
			slot->dsd_data[sample] = swap_bits[temp];
		}
		SetEvent(convSlots[ch].hEventPut); // Release worker (decoding) thread on the loaded slot
	}
	for (int ch = 0; ch < channels; ch++)	{
		DSDPCMConverterSlot<real_t>* slot = &convSlots[ch];
		WaitForSingleObject(slot->hEventGet, INFINITE);	// Wait until worker (decoding) thread is complete
		for (int sample = 0; sample < slot->pcm_samples; sample++)	{
			pcm_data[sample * channels + ch] = (float)slot->pcm_data[sample];
		}
		pcm_samples += slot->pcm_samples;
	}
	return pcm_samples;
}
