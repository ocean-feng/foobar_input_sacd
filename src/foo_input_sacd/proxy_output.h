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

#include <windows.h>
#include <foobar2000.h>
#include "audio_stream.h"

class proxy_output_t : public output_v2 {
	static const uint8_t DoP_MARKER[2];
	pfc::array_t<t_size> m_dop_marker_n;
	pfc::array_t<uint8_t> m_dop_data;
	audio_chunk_impl m_dop_chunk;
	static_api_ptr_t<audio_stream_t> m_audio_stream;
	service_ptr_t<output> m_output;
	t_samplespec m_spec;
	stream_type_e m_stream_type;
	double m_volume;
	double m_duration;
	bool m_trace;
	service_ptr_t<output_v2> m_output_v2;
	bool m_is_output_v2;
public:
	static bool g_advanced_settings_query();
	static bool g_needs_bitdepth_config();
	static bool g_needs_dither_config();
	static bool g_needs_device_list_prefixes();
	static void g_enum_devices(output_device_enum_callback& p_callback);
	static GUID g_get_guid();
	static const char* g_get_name();
	static void g_advanced_settings_popup(HWND p_parent, POINT p_menupoint);
	static service_ptr_t<output_entry> g_get_output();
	proxy_output_t(const GUID& p_device, double p_buffer_length, bool p_dither, t_uint32 p_bitdepth);
	virtual ~proxy_output_t();
	virtual double get_latency();
	virtual void process_samples(const audio_chunk& p_chunk);
	virtual void update(bool& p_ready);
	virtual void pause(bool p_state);
	virtual void flush();
	virtual void flush_changing_track();
	virtual void force_play();
	virtual void volume_set(double p_val);
	virtual bool want_track_marks();
	virtual void on_track_mark();
	virtual void enable_fading(bool p_state);
private:
	void pack_dsd_to_dop(t_samplespec& p_spec);
	void volume_adjust();
	void print_stream_type();
	void print_stream_size();
};
