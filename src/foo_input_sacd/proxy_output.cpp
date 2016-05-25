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

#include "../ATLHelpers/ATLHelpers.h"
#include "sacd_setup.h"
#include "proxy_output.h"

static const GUID  proxy_output_guid = { 0xf4d74794, 0x2936, 0x4595,{ 0x8d, 0x6a, 0xc0, 0xce, 0x7c, 0x5b, 0x35, 0x25 } };
static const char* proxy_output_name = "DSD";

const uint8_t proxy_output_t::DoP_MARKER[2] = { 0x05, 0xFA };

bool proxy_output_t::g_advanced_settings_query() {
	return false;
}

bool proxy_output_t::g_needs_bitdepth_config() {
	return true;
}

bool proxy_output_t::g_needs_dither_config() {
	return true;
}

bool proxy_output_t::g_needs_device_list_prefixes() {
	return false;
}

void proxy_output_t::g_enum_devices(output_device_enum_callback& p_callback) {
	class dsd_output_device_enum_callback : public output_device_enum_callback {
		output_device_enum_callback* m_callback;
		const char* m_name;
	public:
		dsd_output_device_enum_callback(output_device_enum_callback* p_callback, const char* p_name) {
			m_callback = p_callback;
			m_name = p_name;
		}
		void on_device(const GUID& p_guid, const char* p_name, unsigned p_name_length) {
			pfc::string8 name = m_name;
			name.get_length() > 0 ? name << " : " << p_name : name << p_name;
			m_callback->on_device(p_guid, name, name.get_length());
		}
	};
	service_enum_t<output_entry> output_enum;
	service_ptr_t<output_entry> output_ptr;
	while (output_enum.next(output_ptr)) {
		dsd_output_device_enum_callback dsd_callback(&p_callback, output_ptr->get_name());
		if (output_ptr->get_guid() != g_get_guid()) {
			output_ptr->enum_devices(dsd_callback);
		}
	}
}

GUID proxy_output_t::g_get_guid() {
	return proxy_output_guid;
}

const char* proxy_output_t::g_get_name() {
	return proxy_output_name;
}

void proxy_output_t::g_advanced_settings_popup(HWND p_parent, POINT p_menupoint) {
}

service_ptr_t<output_entry> proxy_output_t::g_get_output() {
	outputCoreConfig_t output_config;
	static_api_ptr_t<output_manager> output_manager_ptr;
	output_manager_ptr->getCoreConfig(output_config);
	service_enum_t<output_entry> output_enum;
	service_ptr_t<output_entry> output_ptr;
	while (output_enum.next(output_ptr)) {
		const char* name = output_ptr->get_name();
		pfc::string8 dev;
		if (output_ptr->get_guid() != g_get_guid()) {
			if (output_ptr->get_device_name(output_config.m_device, dev)) {
				break;
			}
		}
	}
	return output_ptr;
}

proxy_output_t::proxy_output_t(const GUID& p_device, double p_buffer_length, bool p_dither, t_uint32 p_bitdepth) {
	m_stream_type = stream_type_e::BAD;
	m_volume = 0;
	m_duration = 0;
	m_trace = CSACDPreferences::g_get_trace();
	m_is_output_v2 = false;
	service_enum_t<output_entry> output_enum;
	service_ptr_t<output_entry> output_ptr;
	while (output_enum.next(output_ptr)) {
		pfc::string8 dev;
		if (output_ptr->get_guid() != g_get_guid()) {
			if (output_ptr->get_device_name(p_device, dev)) {
				output_ptr->instantiate(m_output, p_device, p_buffer_length, p_dither, p_bitdepth);
				m_is_output_v2 = m_output->service_query_t(m_output_v2);
				if (m_trace) {
					console::printf(
						"proxy_output::proxy_output() => output%s(plugin = \"%s\", buffer_length = %s, dither = %s, bitdepth = %d)",
						m_is_output_v2 ? "_v2" : "",
						output_ptr->get_name(),
						pfc::format_float(p_buffer_length, 0, 3).toString(),
						p_dither ? "true" : "false",
						p_bitdepth
					);
				}
				break;
			}
		}
	}
}

proxy_output_t::~proxy_output_t() {
	m_audio_stream->flush();
	if (m_trace) {
		console::printf("proxy_output::~proxy_output()");
	}
}

double proxy_output_t::get_latency() {
	double latency = m_output->get_latency();
	/*
	if (m_trace) {
		console::printf("proxy_output::get_latency() => %s", pfc::format_float(latency, 0, 3).toString());
	}
	*/
	return latency;
}

void proxy_output_t::process_samples(const audio_chunk& p_chunk) {
	t_samplespec pcm_spec;
	pcm_spec.fromchunk(p_chunk);
	if (!pcm_spec.is_valid()) {
		pfc::throw_exception_with_message<exception_io_data>("Invalid audio stream specifications");
	}
	audio_chunk_t* dsd_chunk = m_audio_stream->get_first_chunk_ptr();
	m_stream_type = dsd_chunk ? dsd_chunk->get_type() : stream_type_e::PCM;
	t_samplespec spec = dsd_chunk ? dsd_chunk->get_spec() : pcm_spec;
	if (spec != m_spec) {
		m_dop_marker_n.set_size(0);
		volume_adjust();
		m_spec = spec;
		m_duration = 0;
		if (m_trace) {
			print_stream_type();
		}
	}
	if (m_trace) {
		print_stream_size();
	}
	switch (m_stream_type) {
	case stream_type_e::PCM:
		m_output->process_samples(p_chunk);
		break;
	case stream_type_e::DSD:
		pack_dsd_to_dop(pcm_spec);
		m_output->process_samples(m_dop_chunk);
		break;
	default:
		break;
	}
	/*
	if (m_trace) {
		console::printf("proxy_output::process_samples(channels = %d, sample_rate = %d)", m_spec.m_channels, m_spec.m_sample_rate);
	}
	*/
}

void proxy_output_t::update(bool& p_ready) {
	m_output->update(p_ready);
	/*
	if (m_trace) {
		console::printf("proxy_output::update(%s)", p_ready ? "true" : "false");
	}
	*/
}

void proxy_output_t::pause(bool p_state) {
	m_output->pause(p_state);
	if (m_trace) {
		console::printf("proxy_output::pause(%s)", p_state ? "true" : "false");
	}
}

void proxy_output_t::flush() {
	m_audio_stream->flush();
	m_output->flush();
	if (m_trace) {
		console::printf("proxy_output::flush()");
	}
}

void proxy_output_t::flush_changing_track() {
	m_audio_stream->flush();
	if (m_is_output_v2) {
		m_output_v2->flush_changing_track();
	}
	else {
		m_output->flush();
	}
	if (m_trace) {
		console::printf("proxy_output::flush_changing_track()");
	}
}

void proxy_output_t::force_play() {
	m_output->force_play();
	if (m_trace) {
		console::printf("proxy_output::force_play()");
	}
}

void proxy_output_t::volume_set(double p_val) {
	m_volume = p_val;
	volume_adjust();
	if (m_trace) {
		console::printf("proxy_output::volume_set(%s)", pfc::format_float(p_val, 0, 3).toString());
	}
}

bool proxy_output_t::want_track_marks() {
	bool track_marks = m_is_output_v2 ? m_output_v2->want_track_marks() : true;
	if (m_trace) {
		console::printf("proxy_output::want_track_marks() => %s", track_marks ? "true" : "false");
	}
	return track_marks;
}

void proxy_output_t::on_track_mark() {
	if (m_is_output_v2) {
		m_output_v2->on_track_mark();
	}
	if (m_trace) {
		console::printf("proxy_output::on_track_mark()");
	}
}

void proxy_output_t::enable_fading(bool p_state) {
	if (m_is_output_v2) {
		m_output_v2->enable_fading(p_state);
	}
	if (m_trace) {
		console::printf("proxy_output::enable_fading(%s)", p_state ? "true" : "false");
	}
}

void proxy_output_t::pack_dsd_to_dop(t_samplespec& p_spec) {
	audio_chunk_t* dsd_chunk = m_audio_stream->get_first_chunk_ptr();
	if (!dsd_chunk) {
		return;
	}
	t_samplespec spec = dsd_chunk->get_spec();
	t_size dsd_channels = spec.m_channels;
	t_size dsd_samples = dsd_chunk->get_samples();
	if (spec.m_channels > p_spec.m_channels) {
		spec.m_channels = p_spec.m_channels;
		spec.m_channel_config = p_spec.m_channel_config;
	}
	uint8_t* dsd_data = static_cast<uint8_t*>(dsd_chunk->get_data());
	unsigned dop_samplerate = spec.m_sample_rate / 16;
	t_size dop_channels = spec.m_channels;
	t_size dop_samples = dsd_samples / 2;
	t_size dop_bytes = 3 * dop_channels * dop_samples;
	m_dop_data.set_size(dop_bytes);
	uint8_t* dop_data = m_dop_data.get_ptr();
	if (m_dop_marker_n.get_size() != dop_channels) {
		m_dop_marker_n.set_size(dop_channels);
		for (t_size ch = 0; ch < m_dop_marker_n.get_size(); ch++) {
			m_dop_marker_n[ch] = 0;
		}
	}
	unsigned o = 0;
	for (t_size sample = 0; sample < dop_samples; sample++) {
		for (t_size ch = 0; ch < dop_channels; ch++) {
			uint8_t b_msb, b_lsb;
			b_msb = dsd_data[(2 * sample + 0) * dsd_channels + ch];
			b_lsb = dsd_data[(2 * sample + 1) * dsd_channels + ch];
			dop_data[o + 0] = b_lsb;
			dop_data[o + 1] = b_msb;
			dop_data[o + 2] = DoP_MARKER[m_dop_marker_n[ch]];
			o += 3;
			m_dop_marker_n[ch] = ++m_dop_marker_n[ch] & 1;
		}
	}
	m_dop_chunk.set_data_fixedpoint(dop_data, dop_bytes, dop_samplerate, dop_channels, 24, spec.m_channel_config);
	m_audio_stream->remove_first_chunk();
}

void proxy_output_t::volume_adjust() {
	switch (m_stream_type) {
	case stream_type_e::DSD:
	case stream_type_e::DoP:
		m_output->volume_set(0);
		break;
	default:
		m_output->volume_set(m_volume);
		break;
	}
}

void proxy_output_t::print_stream_type() {
	char* stream_type;
	switch (m_stream_type) {
	case stream_type_e::PCM:
		stream_type = "PCM";
		break;
	case stream_type_e::DSD:
		stream_type = "DSD";
		break;
	case stream_type_e::DoP:
		stream_type = "DoP";
		break;
	default:
		stream_type = "BAD";
		break;
	}
	console::printf("Stream type: %s", stream_type);
}

void proxy_output_t::print_stream_size() {
	double duration = m_audio_stream->get_duration();
	if (duration != m_duration) {
		m_duration = duration;
		console::printf("DSD stream duration: %s (%d samples)", pfc::format_float(m_duration, 0, 3).toString(), m_audio_stream->get_samples());
	}
}

static output_factory_t<proxy_output_t> g_proxy_output_factory;
