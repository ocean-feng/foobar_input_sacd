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

#include "DSDPCMConverter.h"

template<typename real_t>
class DSDPCMConverterDirect : public DSDPCMConverter<real_t> {
};

template<typename real_t>
class DSDPCMConverterDirect_x512 : public DSDPCMConverterDirect<real_t> {
	DSDPCMFir<real_t> dsd_fir1;
	PCMPCMFir<real_t> pcm_fir2a;
	PCMPCMFir<real_t> pcm_fir2b;
	PCMPCMFir<real_t> pcm_fir3;
public:
	void init(DSDPCMFilterSetup<real_t>& flt_setup, int dsd_samples) {
		alloc_pcm_temp1(dsd_samples / 8);
		alloc_pcm_temp2(dsd_samples / 16);
		dsd_fir1.init(flt_setup.get_fir1_64_ctables(), flt_setup.get_fir1_64_length(), 64);
		pcm_fir2a.init(flt_setup.get_fir2_2_coefs(), flt_setup.get_fir2_2_length(), 2);
		pcm_fir2b.init(flt_setup.get_fir2_2_coefs(), flt_setup.get_fir2_2_length(), 2);
		pcm_fir3.init(flt_setup.get_fir3_2_coefs(), flt_setup.get_fir3_2_length(), 2);
		delay = ((dsd_fir1.get_delay() / pcm_fir2a.get_decimation() + pcm_fir2a.get_delay()) / pcm_fir2b.get_decimation() + pcm_fir2b.get_delay()) / pcm_fir3.get_decimation() + pcm_fir3.get_delay();
	}
	int convert(uint8_t* dsd_data, real_t* pcm_data, int dsd_samples) {
		int pcm_samples;
		pcm_samples = dsd_fir1.run(dsd_data, pcm_temp1, dsd_samples);
		pcm_samples = pcm_fir2a.run(pcm_temp1, pcm_temp2, pcm_samples);
		pcm_samples = pcm_fir2b.run(pcm_temp2, pcm_temp1, pcm_samples);
		pcm_samples = pcm_fir3.run(pcm_temp1, pcm_data, pcm_samples);
		return pcm_samples;
	}
};

template<typename real_t>
class DSDPCMConverterDirect_x256 : public DSDPCMConverterDirect<real_t> {
	DSDPCMFir<real_t> dsd_fir1;
	PCMPCMFir<real_t> pcm_fir2a;
	PCMPCMFir<real_t> pcm_fir3;
public:
	void init(DSDPCMFilterSetup<real_t>& flt_setup, int dsd_samples) {
		alloc_pcm_temp1(dsd_samples / 8);
		alloc_pcm_temp2(dsd_samples / 16);
		dsd_fir1.init(flt_setup.get_fir1_64_ctables(), flt_setup.get_fir1_64_length(), 64);
		pcm_fir2a.init(flt_setup.get_fir2_2_coefs(), flt_setup.get_fir2_2_length(), 2);
		pcm_fir3.init(flt_setup.get_fir3_2_coefs(), flt_setup.get_fir3_2_length(), 2);
		delay = (dsd_fir1.get_delay() / pcm_fir2a.get_decimation() + pcm_fir2a.get_delay()) / pcm_fir3.get_decimation() + pcm_fir3.get_delay();
	}
	int convert(uint8_t* dsd_data, real_t* pcm_data, int dsd_samples) {
		int pcm_samples;
		pcm_samples = dsd_fir1.run(dsd_data, pcm_temp1, dsd_samples);
		pcm_samples = pcm_fir2a.run(pcm_temp1, pcm_temp2, pcm_samples);
		pcm_samples = pcm_fir3.run(pcm_temp2, pcm_data, pcm_samples);
		return pcm_samples;
	}
};

template<typename real_t>
class DSDPCMConverterDirect_x128 : public DSDPCMConverterDirect<real_t> {
	DSDPCMFir<real_t> dsd_fir1;
	PCMPCMFir<real_t> pcm_fir3;
public:
	void init(DSDPCMFilterSetup<real_t>& flt_setup, int dsd_samples) {
		alloc_pcm_temp1(dsd_samples / 8);
		dsd_fir1.init(flt_setup.get_fir1_64_ctables(), flt_setup.get_fir1_64_length(), 64);
		pcm_fir3.init(flt_setup.get_fir3_2_coefs(), flt_setup.get_fir3_2_length(), 2);
		delay = dsd_fir1.get_delay() / pcm_fir3.get_decimation() + pcm_fir3.get_delay();
	}
	int convert(uint8_t* dsd_data, real_t* pcm_data, int dsd_samples) {
		int pcm_samples;
		pcm_samples = dsd_fir1.run(dsd_data, pcm_temp1, dsd_samples);
		pcm_samples = pcm_fir3.run(pcm_temp1, pcm_data, pcm_samples);
		return pcm_samples;
	}
};

template<typename real_t>
class DSDPCMConverterDirect_x64 : public DSDPCMConverterDirect<real_t> {
	DSDPCMFir<real_t> dsd_fir1;
	PCMPCMFir<real_t> pcm_fir3;
public:
	void init(DSDPCMFilterSetup<real_t>& flt_setup, int dsd_samples) {
		alloc_pcm_temp1(dsd_samples / 4);
		dsd_fir1.init(flt_setup.get_fir1_64_ctables(), flt_setup.get_fir1_64_length(), 32);
		pcm_fir3.init(flt_setup.get_fir3_2_coefs(), flt_setup.get_fir3_2_length(), 2);
		delay = dsd_fir1.get_delay() / pcm_fir3.get_decimation() + pcm_fir3.get_delay();
	}
	int convert(uint8_t* dsd_data, real_t* pcm_data, int dsd_samples) {
		int pcm_samples;
		pcm_samples = dsd_fir1.run(dsd_data, pcm_temp1, dsd_samples);
		pcm_samples = pcm_fir3.run(pcm_temp1, pcm_data, pcm_samples);
		return pcm_samples;
	}
};

template<typename real_t>
class DSDPCMConverterDirect_x32: public DSDPCMConverterDirect<real_t> {
	DSDPCMFir<real_t> dsd_fir1;
public:
	void init(DSDPCMFilterSetup<real_t>& flt_setup, int dsd_samples) {
		alloc_pcm_temp1(dsd_samples / 4);
		dsd_fir1.init(flt_setup.get_fir1_64_ctables(), flt_setup.get_fir1_64_length(), 32);
		delay = dsd_fir1.get_delay();
	}
	int convert(uint8_t* dsd_data, real_t* pcm_data, int dsd_samples) {
		int pcm_samples;
		pcm_samples = dsd_fir1.run(dsd_data, pcm_data, dsd_samples);
		return pcm_samples;
	}
};

template<typename real_t>
class DSDPCMConverterDirect_x16 : public DSDPCMConverterDirect<real_t> {
	DSDPCMFir<real_t> dsd_fir1;
public:
	void init(DSDPCMFilterSetup<real_t>& flt_setup, int dsd_samples) {
		alloc_pcm_temp1(dsd_samples / 2);
		dsd_fir1.init(flt_setup.get_fir1_64_ctables(), flt_setup.get_fir1_64_length(), 16);
		delay = dsd_fir1.get_delay();
	}
	int convert(uint8_t* dsd_data, real_t* pcm_data, int dsd_samples) {
		int pcm_samples;
		pcm_samples = dsd_fir1.run(dsd_data, pcm_data, dsd_samples);
		return pcm_samples;
	}
};

template<typename real_t>
class DSDPCMConverterDirect_x8 : public DSDPCMConverterDirect<real_t> {
	DSDPCMFir<real_t> dsd_fir1;
public:
	void init(DSDPCMFilterSetup<real_t>& flt_setup, int dsd_samples) {
		alloc_pcm_temp1(dsd_samples);
		dsd_fir1.init(flt_setup.get_fir1_64_ctables(), flt_setup.get_fir1_64_length(), 8);
		delay = dsd_fir1.get_delay();
	}
	int convert(uint8_t* dsd_data, real_t* pcm_data, int dsd_samples) {
		int pcm_samples;
		pcm_samples = dsd_fir1.run(dsd_data, pcm_data, dsd_samples);
		return pcm_samples;
	}
};
