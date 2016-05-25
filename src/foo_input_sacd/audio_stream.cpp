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

#include "audio_stream.h"

const GUID audio_stream_t::class_guid = { 0x18091b99, 0x3944, 0x4648,{ 0x96, 0xf5, 0x3, 0xa3, 0xa1, 0x3, 0x44, 0xc2 } };

audio_stream_impl::audio_stream_impl() {
	InitializeCriticalSection(&m_chunk_cs);
	m_chunk_read_samples = 0;
	m_samples = 0;
	m_duration = m_max_duration = 0.0;
}

audio_stream_impl::~audio_stream_impl() {
	flush();
	DeleteCriticalSection(&m_chunk_cs);
}

t_size audio_stream_impl::get_samples() {
	return m_samples;
}

double audio_stream_impl::get_duration() {
	return m_duration;
}

void audio_stream_impl::set_max_duration(double p_duration) {
	m_max_duration = p_duration;
}

double audio_stream_impl::get_watermark() {
	return (m_max_duration > 0.0) ? m_duration / m_max_duration : 0.0;
}

t_samplespec audio_stream_impl::get_spec() {
	t_samplespec spec;
	EnterCriticalSection(&m_chunk_cs);
	if (m_chunks.get_count() > 0) {
		pfc::iterator<audio_chunk_t*> chunk_iter = m_chunks.first();
		spec = (*chunk_iter)->get_spec();
	}
	LeaveCriticalSection(&m_chunk_cs);
	return spec;
}

bool audio_stream_impl::is_spec_change() {
	return m_last_read_spec != get_spec();
}

void audio_stream_impl::reset_spec_change() {
	m_last_read_spec = get_spec();
}

stream_type_e audio_stream_impl::get_type() {
	stream_type_e type;
	t_samplespec spec = get_spec();
	if (spec.m_sample_rate > 0) {
		type = (spec.m_sample_rate < DSDxFs64) ? stream_type_e::PCM : stream_type_e::DSD;
	}
	else {
		type = stream_type_e::BAD;
	}
	return type;
}

audio_chunk_t* audio_stream_impl::get_first_chunk_ptr() {
	audio_chunk_t* chunk_ptr = nullptr;
	EnterCriticalSection(&m_chunk_cs);
	if (m_chunks.get_count() > 0) {
		chunk_ptr = *(m_chunks.first());
	}
	LeaveCriticalSection(&m_chunk_cs);
	return chunk_ptr;
}

void audio_stream_impl::remove_first_chunk() {
	EnterCriticalSection(&m_chunk_cs);
	if (m_chunks.get_count() > 0) {
		pfc::iterator<audio_chunk_t*> chunk_iter = m_chunks.first();
		(*chunk_iter)->get_duration();
		m_samples -= (*chunk_iter)->get_samples();
		m_duration -= (*chunk_iter)->get_duration();
		delete *chunk_iter;
		m_chunks.remove(chunk_iter);
	}
	LeaveCriticalSection(&m_chunk_cs);
}

t_size audio_stream_impl::read(void* p_buffer, t_size p_samples) {
	t_size read_samples = 0;
	EnterCriticalSection(&m_chunk_cs);
	while (m_chunks.get_count() > 0) {
		pfc::iterator<audio_chunk_t*> chunk_iter = m_chunks.first();
		t_samplespec chunk_spec = (*chunk_iter)->get_spec();
		if (chunk_spec != m_last_read_spec) {
			break;
		}
		void* chunk_data = (*chunk_iter)->get_data();
		t_size chunk_samples = (*chunk_iter)->get_samples();
		t_size chunk_samplesize = (*chunk_iter)->get_samplesize();
		unsigned chunk_channels = (*chunk_iter)->get_channels();
		double sample_duration = (*chunk_iter)->get_duration() / (*chunk_iter)->get_samples();
		t_size samples_to_read = p_samples - read_samples;
		if (chunk_samples - m_chunk_read_samples > samples_to_read) {
			memcpy(reinterpret_cast<uint8_t*>(p_buffer) + read_samples * chunk_channels * chunk_samplesize, reinterpret_cast<uint8_t*>(chunk_data) + m_chunk_read_samples * chunk_channels * chunk_samplesize, samples_to_read * chunk_channels * chunk_samplesize);
			m_chunk_read_samples += samples_to_read;
			read_samples += samples_to_read;
			m_samples -= samples_to_read;
			m_duration -= sample_duration * samples_to_read;
		}
		else {
			memcpy(reinterpret_cast<uint8_t*>(p_buffer) + read_samples * chunk_channels * chunk_samplesize, reinterpret_cast<uint8_t*>(chunk_data) + m_chunk_read_samples * chunk_channels * chunk_samplesize, (chunk_samples - m_chunk_read_samples) * chunk_channels * chunk_samplesize);
			delete *chunk_iter;
			m_chunks.remove(chunk_iter);
			read_samples += chunk_samples - m_chunk_read_samples;
			m_samples -= chunk_samples - m_chunk_read_samples;
			m_duration -= sample_duration * (chunk_samples - m_chunk_read_samples);
			m_chunk_read_samples = 0;
		}
		if (p_samples <= read_samples) {
			break;
		}
	}
	LeaveCriticalSection(&m_chunk_cs);
	return read_samples;
}

void audio_stream_impl::write(const void* p_buffer, t_size p_samples, unsigned p_channels, unsigned p_samplerate, unsigned p_channel_config) {
	audio_chunk_t* chunk = new audio_chunk_t(p_buffer, p_samples, p_channels, p_samplerate, p_channel_config);
	EnterCriticalSection(&m_chunk_cs);
	m_chunks.add_item(chunk);
	m_samples += chunk->get_samples();
	m_duration += chunk->get_duration();
	LeaveCriticalSection(&m_chunk_cs);
}

void audio_stream_impl::flush() {
	EnterCriticalSection(&m_chunk_cs);
	while (m_chunks.get_count() > 0) {
		pfc::iterator<audio_chunk_t*> chunk_iter = m_chunks.first();
		delete *chunk_iter;
		m_chunks.remove(chunk_iter);
	}
	m_chunk_read_samples = 0;
	m_samples = 0;
	m_duration = 0.0;
	LeaveCriticalSection(&m_chunk_cs);
}

service_factory_single_t<audio_stream_impl> g_audio_stream_service_factory;