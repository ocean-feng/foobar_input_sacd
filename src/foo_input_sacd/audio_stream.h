/*
* SACD Decoder plugin
* Copyright (c) 2011-2016 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
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

#include <foobar2000.h>

constexpr unsigned DSDxFs = 44100;
constexpr unsigned DSDxFs1 = DSDxFs * 1;
constexpr unsigned DSDxFs2 = DSDxFs * 2;
constexpr unsigned DSDxFs4 = DSDxFs * 4;
constexpr unsigned DSDxFs8 = DSDxFs * 8;
constexpr unsigned DSDxFs64 = DSDxFs * 64;
constexpr unsigned DSDxFs128 = DSDxFs * 128;
constexpr unsigned DSDxFs256 = DSDxFs * 256;
constexpr unsigned DSDxFs512 = DSDxFs * 512;

constexpr unsigned D48xFs = 48000;
constexpr unsigned D48xFs1 = D48xFs * 1;
constexpr unsigned D48xFs2 = D48xFs * 2;
constexpr unsigned D48xFs4 = D48xFs * 4;
constexpr unsigned D48xFs8 = D48xFs * 8;
constexpr unsigned D48xFs64 = D48xFs * 64;
constexpr unsigned D48xFs128 = D48xFs * 128;
constexpr unsigned D48xFs256 = D48xFs * 256;
constexpr unsigned D48xFs512 = D48xFs * 512;

constexpr unsigned DSD_SAMPLERATE = DSDxFs64;
constexpr unsigned PCM_SAMPLERATE = DSDxFs1;

constexpr unsigned DSD_SILENCE_BYTE = 0x69;

enum class stream_type_e { BAD = -1, PCM = 0, DSD = 1, DoP = 2 };

class audio_chunk_t {
	t_samplespec          m_spec;
	pfc::array_t<uint8_t> m_buffer;
	t_size                m_samples;
public:
	audio_chunk_t() {
		m_samples = 0;
	}
	
	audio_chunk_t(const audio_chunk_t& p_source) {
		m_spec = p_source.m_spec;
		m_buffer = p_source.m_buffer;
		m_samples = p_source.m_samples;
	}

	audio_chunk_t(const void* p_buffer, t_size p_samples, unsigned p_channels, unsigned p_samplerate, unsigned p_channel_config) {
		set_data(p_buffer, p_samples, p_channels, p_samplerate, p_channel_config);
	}

	stream_type_e get_type() {
		stream_type_e type;
		if (m_spec.m_sample_rate > 0) {
			if (m_spec.m_sample_rate < DSDxFs64) {
				type = stream_type_e::PCM;
			}
			else {
				type = stream_type_e::DSD;
			}
		}
		else {
			type = stream_type_e::BAD;
		}
		return type;
	}

	t_samplespec get_spec() {
		return m_spec;
	}

	t_size get_channels() {
		return m_spec.m_channels;
	}

	t_size get_samples() {
		return m_samples;
	}

	t_size get_samplesize() {
		return (m_spec.m_sample_rate < DSDxFs64) ? sizeof(audio_sample) : sizeof(uint8_t);;
	}

	double get_duration() {
		double duration;
		if (m_spec.m_sample_rate > 0) {
			if (m_spec.m_sample_rate < DSDxFs64) {
				duration = (double)m_samples / (double)m_spec.m_sample_rate;
			}
			else {
				duration = (double)m_samples / (double)(m_spec.m_sample_rate / 8);
			}
		}
		else {
			duration = 0.0;
		}
		return duration;
	}

	void* get_data() {
		return m_buffer.get_ptr();
	}

	void set_data(const void* p_buffer, t_size p_samples, unsigned p_channels, unsigned p_samplerate, unsigned p_channel_config) {
		m_samples = p_samples;
		m_spec.m_channels = p_channels;
		m_spec.m_sample_rate = p_samplerate;
		m_spec.m_channel_config = p_channel_config;
		t_size buffer_size = m_samples * m_spec.m_channels * get_samplesize();
		m_buffer.append_fromptr(reinterpret_cast<const uint8_t*>(p_buffer), buffer_size);
	}
};

class NOVTABLE audio_stream_t : public service_base {
	FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(audio_stream_t);
public:
	virtual t_size get_samples() = 0;
	virtual double get_duration() = 0;
	virtual void set_max_duration(double p_duration) = 0;
	virtual double get_watermark() = 0;
	virtual t_samplespec get_spec() = 0;
	virtual bool is_spec_change() = 0;
	virtual void reset_spec_change() = 0;
	virtual stream_type_e get_type() = 0;
	virtual audio_chunk_t* get_first_chunk_ptr() = 0;
	virtual void remove_first_chunk() = 0;
	virtual t_size read(void* p_buffer, t_size p_samples) = 0;
	virtual void write(const void* p_buffer, t_size p_samples, unsigned p_channels, unsigned p_samplerate, unsigned p_channel_config) = 0;
	virtual void flush() = 0;
};

class NOVTABLE audio_stream_impl : public audio_stream_t {
	pfc::chain_list_v2_t<audio_chunk_t*> m_chunks;
	CRITICAL_SECTION                     m_chunk_cs;
	t_size                               m_chunk_read_samples;
	t_size m_samples;
	double m_duration;
	double m_max_duration;
	t_samplespec m_last_read_spec;
public:
	audio_stream_impl();
	virtual ~audio_stream_impl();
	virtual t_size get_samples();
	virtual double get_duration();
	virtual void set_max_duration(double p_duration);
	virtual double get_watermark();
	virtual t_samplespec get_spec();
	virtual bool is_spec_change();
	virtual void reset_spec_change();
	virtual stream_type_e get_type();
	virtual audio_chunk_t* get_first_chunk_ptr();
	virtual void remove_first_chunk();
	virtual t_size read(void* p_buffer, t_size p_samples);
	virtual void write(const void* p_buffer, t_size p_samples, unsigned p_channels, unsigned p_samplerate, unsigned p_channel_config);
	virtual void flush();
};

class audio_buffer_t {
	void* m_data;
	t_size m_channels;
	t_size m_samples;
	stream_type_e m_type;
	t_size m_sampleSize;
public:
	audio_buffer_t() {
		m_data = nullptr;
		m_channels = m_samples = 0;
		m_type = stream_type_e::BAD;
		m_sampleSize = 0;
	}
	audio_buffer_t(t_size p_channels, t_size p_samples, stream_type_e p_type) {
		m_data = nullptr;
		m_channels = p_channels;
		m_samples = p_samples;
		switch (p_type) {
		case stream_type_e::PCM:
			m_type = stream_type_e::PCM;
			m_sampleSize = sizeof(audio_sample);
			break;
		case stream_type_e::DSD:
			m_type = stream_type_e::DSD;
			m_sampleSize = sizeof(uint8_t);
			break;
		case stream_type_e::DoP:
			m_type = stream_type_e::DoP;
			m_sampleSize = sizeof(uint16_t);
			break;
		default:
			m_type = stream_type_e::BAD;
			m_sampleSize = 0;
			break;
		}
		if (m_channels > 0 && m_samples > 0 && m_sampleSize > 0) {
			m_data = new uint8_t[m_channels * m_samples * m_sampleSize];
		}
	}
	~audio_buffer_t() {
		free();
	}
	void free() {
		if (m_data) {
			delete [] m_data;
			m_data = nullptr;
		}
		m_channels = m_samples = 0;
		m_type = stream_type_e::BAD;
		m_sampleSize = 0;
	}
	void* get_data() {
		return m_data;
	}
	t_size get_size() {
		return m_channels * m_samples * m_sampleSize;
	}
	stream_type_e get_type() {
		return m_type;
	}
};
