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

#include "../ATLHelpers/ATLHelpers.h"
#include <stdint.h>
#include <foobar2000.h>
#include <component.h>
#include "audio_stream.h"
#include "sacd_reader.h"
#include "sacd_disc.h"
#include "sacd_dsdiff.h"
#include "sacd_dsf.h"
#include "sacd_metabase.h"
#include "sacd_setup.h"
#include "dst_decoder_foo.h"
#include "DSDPCMConverterEngine.h"

#ifdef _USE_MTAGS
#include "mtags_service.h"
#endif

using namespace pfc;

static const uint32_t UPDATE_STATS_MS = 500;
static const int BITRATE_AVGS = 16;
static const audio_sample PCM_OVERLOAD_THRESHOLD = 1.0f;

void console_fprintf(FILE* file, const char* fmt, ...) {
	va_list vl;
	va_start(vl, fmt);
	console::printfv(fmt, vl);
	va_end(vl);
}

void console_vfprintf(FILE* file, const char* fmt, va_list vl) {
	console::printfv(fmt, vl);
}

int get_sacd_channel_map_from_loudspeaker_config(int loudspeaker_config) {
	int sacd_channel_map;
	switch (loudspeaker_config) {
	case 0:
		sacd_channel_map = audio_chunk::channel_front_left | audio_chunk::channel_front_right;
		break;
	case 1:
		sacd_channel_map = audio_chunk::channel_front_left | audio_chunk::channel_front_right | audio_chunk::channel_back_left | audio_chunk::channel_back_right;
		break;
	case 2:
		sacd_channel_map = audio_chunk::channel_front_left | audio_chunk::channel_front_right | audio_chunk::channel_front_center | audio_chunk::channel_lfe;
		break;
	case 3:
		sacd_channel_map = audio_chunk::channel_front_left | audio_chunk::channel_front_right | audio_chunk::channel_front_center | audio_chunk::channel_back_left | audio_chunk::channel_back_right;
		break;
	case 4:
		sacd_channel_map = audio_chunk::channel_front_left | audio_chunk::channel_front_right | audio_chunk::channel_front_center | audio_chunk::channel_lfe | audio_chunk::channel_back_left | audio_chunk::channel_back_right;
		break;
	case 5:
		sacd_channel_map = audio_chunk::channel_front_center;
		break;
	case 6:
		sacd_channel_map = audio_chunk::channel_front_left | audio_chunk::channel_front_right | audio_chunk::channel_front_center;
		break;
	default:
		sacd_channel_map = 0;
		break;
	}
	return sacd_channel_map;
}

int get_sacd_channel_map_from_channels(int channels) {
	int sacd_channel_map;
	switch (channels) {
	case 1:
		sacd_channel_map = audio_chunk::channel_front_center;
		break;
	case 2:
		sacd_channel_map = audio_chunk::channel_front_left | audio_chunk::channel_front_right;
		break;
	case 3:
		sacd_channel_map = audio_chunk::channel_front_left | audio_chunk::channel_front_right | audio_chunk::channel_front_center;
		break;
	case 4:
		sacd_channel_map = audio_chunk::channel_front_left | audio_chunk::channel_front_right | audio_chunk::channel_back_left | audio_chunk::channel_back_right;
		break;
	case 5:
		sacd_channel_map = audio_chunk::channel_front_left | audio_chunk::channel_front_right | audio_chunk::channel_front_center | audio_chunk::channel_back_left | audio_chunk::channel_back_right;
		break;
	case 6:
		sacd_channel_map = audio_chunk::channel_front_left | audio_chunk::channel_front_right | audio_chunk::channel_front_center | audio_chunk::channel_lfe | audio_chunk::channel_back_left | audio_chunk::channel_back_right;
		break;
	default:
		sacd_channel_map = 0;
		break;
	}
	return sacd_channel_map;
}

conv_type_e get_converter_type() {
	conv_type_e conv_type = DSDPCM_CONV_MULTISTAGE;
	switch (CSACDPreferences::get_converter_mode()) {
	case 0:
	case 1:
		conv_type = DSDPCM_CONV_MULTISTAGE;
		break;
	case 2:
	case 3:
		conv_type = DSDPCM_CONV_DIRECT;
		break;
	case 4:
	case 5:
		conv_type = DSDPCM_CONV_USER;
		break;
	}
	return conv_type;
}

bool get_converter_fp64() {
	bool conv_fp64 = false;
	switch (CSACDPreferences::get_converter_mode()) {
	case 1:
	case 3:
	case 5:
		conv_fp64 = true;
		break;
	}
	return conv_fp64;
}

void fix_pcm_stream(bool is_end, audio_sample* pcm_data, int pcm_samples, int pcm_channels) {
	if (!is_end) {
		if (pcm_samples > 1) {
			for (int ch = 0; ch < pcm_channels; ch++) {
				pcm_data[0 * pcm_channels + ch] = pcm_data[1 * pcm_channels + ch];
			}
		}
	}
	else {
		if (pcm_samples > 1) {
			for (int ch = 0; ch < pcm_channels; ch++) {
				pcm_data[(pcm_samples - 1) * pcm_channels + ch] = pcm_data[(pcm_samples - 2) * pcm_channels + ch];
			}
		}
	}
}

DSDPCMConverterEngine* g_dsdpcm_playback = nullptr;
bool                   g_track_completed;
bool                   g_cue_playback = false;

class input_sacd_t {
	media_type_e           media_type;
	sacd_media_t*          sacd_media;
	sacd_reader_t*         sacd_reader;
	sacd_metabase_t*       sacd_metabase;
#ifdef _USE_MTAGS
	mtags_service_t*       tags_metabase;
#endif
	area_id_e              area_id;
	unsigned               flags;
	int                    sacd_bitrate[BITRATE_AVGS];
	int                    sacd_bitrate_idx;
	int                    sacd_bitrate_sum;
	array_t<uint8_t>       dsd_buf;
	int                    dsd_buf_size;
	array_t<uint8_t>       dst_buf;
	int                    dst_buf_size;
	array_t<audio_sample>  pcm_buf;
	dst_decoder_t*         dst_decoder;
	DSDPCMConverterEngine* dsdpcm_decoder;
	DSDPCMConverterEngine* dsdpcm_convert;
	bool                   log_overloads;
	string8                log_track_name;
	uint32_t               info_update_time_ms;
	int                    pcm_out_channels;
	unsigned int           pcm_out_channel_map;
	int                    pcm_out_samplerate;
	int                    pcm_out_bits_per_sample;
	int                    pcm_out_samples;
	float                  pcm_out_delay;
	int                    pcm_out_delta;
	uint64_t               pcm_out_offset;
	int                    pcm_min_samplerate;
	bool                   use_dsd_path;
	int                    dsd_samplerate;
	
	static_api_ptr_t<audio_stream_t> dsd_stream;
	
	int                    framerate;
	bool                   track_completed;
	bool                   cue_playback;
	int                    excpt_cnt;
public:
	input_sacd_t() {
		sacd_media = nullptr;
		sacd_reader = nullptr;
		dst_decoder = nullptr;
		dsdpcm_decoder = nullptr;
		dsdpcm_convert = nullptr;
		info_update_time_ms = 0;
		sacd_reader = nullptr;
		sacd_metabase = nullptr;
#ifdef _USE_MTAGS
		tags_metabase = nullptr;
#endif
		cue_playback = false;
	}

	virtual ~input_sacd_t() {
		if (sacd_reader) {
			delete sacd_reader;
		}
		if (sacd_media) {
			delete sacd_media;
		}
		if (sacd_metabase) {
			delete sacd_metabase;
		}
#ifdef _USE_MTAGS
		if (tags_metabase) {
			delete tags_metabase;
		}
#endif
		if (dst_decoder) {
			delete dst_decoder;
		}
		if (dsdpcm_convert) {
			delete dsdpcm_convert;
		}
	}

	void open(service_ptr_t<file> p_filehint, const char* p_path, t_input_open_reason p_reason, abort_callback& p_abort) {
		cue_playback = cue_playback || g_cue_playback;
		if (p_reason == input_open_decode) {
			g_cue_playback = false;
		}
		string_filename_ext filename_ext(p_path);
		string_extension ext(p_path);
		bool raw_media  = false;
		media_type = UNK_TYPE;
		if (stricmp_utf8(ext, "ISO") == 0) {
			media_type = ISO_TYPE;
		} else if (stricmp_utf8(ext, "DAT") == 0) {
			media_type = ISO_TYPE;
		} else if (stricmp_utf8(ext, "DFF") == 0) {
			media_type = DSDIFF_TYPE;
		} else if (stricmp_utf8(ext, "DSF") == 0) {
			media_type = DSF_TYPE;
		} else if ((stricmp_utf8(filename_ext, "") == 0 || stricmp_utf8(filename_ext, "MASTER1.TOC") == 0) && strlen_utf8(p_path) > 7 && sacd_disc_t::g_is_sacd(p_path[7])) {
			media_type = ISO_TYPE;
			raw_media = true;
		}
		if (media_type == UNK_TYPE) {
			throw exception_io_unsupported_format();
		}
		if (raw_media) {
			sacd_media = new sacd_media_disc_t(p_abort);
			if (!sacd_media) {
				throw exception_overflow();
			}
		}
		else {
			sacd_media = new sacd_media_file_t(p_abort);
			if (!sacd_media) {
				throw exception_overflow();
			}
		}
		switch (media_type) {
		case ISO_TYPE:
			sacd_reader = new sacd_disc_t;
			if (!sacd_reader) {
				throw exception_overflow();
			}
			break;
		case DSDIFF_TYPE:
			sacd_reader = new sacd_dsdiff_t;
			if (!sacd_reader) {
				throw exception_overflow();
			}
			break;
		case DSF_TYPE:
			sacd_reader = new sacd_dsf_t;
			if (!sacd_reader) {
				throw exception_overflow();
			}
			break;
		default:
			throw exception_io_data();
			break;
		}
		if (!sacd_media->open(p_filehint, p_path, (media_type == ISO_TYPE && p_reason == input_open_info_write) ? input_open_info_read : p_reason)) {
			throw exception_io_data();
		}
		if (!sacd_reader->open(sacd_media, (cue_playback ? MODE_SINGLE_TRACK : 0) | (CSACDPreferences::get_emaster() ? MODE_FULL_PLAYBACK : 0))) {
			throw exception_io_data();
		}
		switch (media_type) {
		case ISO_TYPE:
			if (CSACDPreferences::get_editable_tags()) {
				const char* metafile_path = nullptr;
				string_replace_extension metafile_name(p_path, "xml");
				if (!raw_media && CSACDPreferences::get_store_tags_with_iso()) {
					metafile_path = metafile_name;
				}
				if (sacd_metabase) {
					delete sacd_metabase;
				}
				sacd_metabase = new sacd_metabase_t(static_cast<sacd_disc_t*>(sacd_reader), metafile_path, p_abort);
#ifdef _USE_MTAGS
				if (tags_metabase) {
					delete tags_metabase;
				}
				tags_metabase = new mtags_service_t(p_path, p_abort);
#endif
			}
			break;
		}
		log_overloads = CSACDPreferences::get_log_overloads();
		pcm_out_samplerate = CSACDPreferences::get_samplerate();
		pcm_out_bits_per_sample = 24;
	}

	t_uint32 get_subsong_count() {
		area_id = (area_id_e)CSACDPreferences::get_area();
		t_uint32 track_count = 0;
		switch (area_id) {
		case AREA_TWOCH:
			track_count = sacd_reader->get_track_count(AREA_TWOCH);
			if (track_count == 0) {
				area_id = AREA_BOTH;
				track_count = sacd_reader->get_track_count(AREA_MULCH);
			}
			break;
		case AREA_MULCH:
			track_count = sacd_reader->get_track_count(AREA_MULCH);
			if (track_count == 0) {
				area_id = AREA_BOTH;
				track_count = sacd_reader->get_track_count(AREA_TWOCH);
			}
			break;
		default:
			track_count = sacd_reader->get_track_count(AREA_TWOCH) + sacd_reader->get_track_count(AREA_MULCH);
			break;
		}
		return track_count;
	}

	t_uint32 get_subsong(t_uint32 p_index) {
		switch (area_id) {
		case AREA_MULCH:
			p_index += sacd_reader->get_track_count(AREA_TWOCH);
			break;
		}
		return p_index;
	}

	void get_info(t_uint32 p_subsong, file_info& p_info, abort_callback& p_abort) {
		t_uint32 subsong = p_subsong;
		if (media_type == ISO_TYPE) {
			uint8_t twoch_count = sacd_reader->get_track_count(AREA_TWOCH);
			if (subsong < twoch_count) {
				sacd_reader->set_area(AREA_TWOCH);
			}
			else {
				subsong -= twoch_count;
				if (subsong < sacd_reader->get_track_count(AREA_MULCH)) {
					sacd_reader->set_area(AREA_MULCH);
				}
			}
		}
		p_info.set_length(sacd_reader->get_duration(subsong));
		p_info.info_set_int("samplerate", CSACDPreferences::in_dsd_mode() ? sacd_reader->get_samplerate() : pcm_out_samplerate);
		p_info.info_set_int("channels", sacd_reader->get_channels());
		p_info.info_set_int("bitspersample", pcm_out_bits_per_sample);
		string_formatter(codec);
		codec << (sacd_reader->is_dst() ? "DST" : "DSD");
		codec << sacd_reader->get_samplerate() / DSDxFs1;
		p_info.info_set("codec", codec);
		p_info.info_set("encoding", "lossless");
		p_info.info_set_bitrate(((t_int64)(sacd_reader->get_samplerate() * sacd_reader->get_channels()) + 500) / 1000);
		sacd_reader->get_info(subsong, p_info);
		if (sacd_metabase) {
			sacd_metabase->get_meta_info(p_subsong, p_info);
			sacd_metabase->get_replaygain(p_subsong, p_info, (float)CSACDPreferences::get_volume());
		}
#ifdef _USE_MTAGS
		if (tags_metabase) {
			//tags_metabase->set_replaygain((float)CSACDPreferences::get_volume());
			tags_metabase->get_info(p_subsong, p_info, p_abort);
		}
#endif
		if (log_overloads) {
			const char* track_title = p_info.meta_get("title", 0);
			log_track_name = track_title ? track_title : "Untitled";
		}
	}

	t_filestats get_file_stats(abort_callback& p_abort) {
		return sacd_media->get_stats();
	}

	void decode_initialize(t_uint32 p_subsong, unsigned p_flags, abort_callback& p_abort) {
		t_uint32 subsong = p_subsong;
		flags = p_flags;
		sacd_reader->set_emaster(CSACDPreferences::get_emaster());
		uint8_t twoch_count = sacd_reader->get_track_count(AREA_TWOCH);
		if (subsong < twoch_count) {
			if (!sacd_reader->set_track(subsong, AREA_TWOCH, 0)) {
				throw exception_io();
			}
		}
		else {
			subsong -= twoch_count;
			if (subsong < sacd_reader->get_track_count(AREA_MULCH)) {
				if (!sacd_reader->set_track(subsong, AREA_MULCH, 0)) {
					throw exception_io();
				}
			}
		}
		dsd_samplerate = sacd_reader->get_samplerate();
		framerate = sacd_reader->get_framerate();
		pcm_out_channels = sacd_reader->get_channels();
		dst_buf_size = dsd_buf_size = dsd_samplerate / 8 / framerate * pcm_out_channels;
		dsd_buf.set_size(DST_DECODER_THREADS * dsd_buf_size);
		dst_buf.set_size(DST_DECODER_THREADS * dst_buf_size);
		pcm_out_channel_map = get_sacd_channel_map_from_loudspeaker_config(sacd_reader->get_loudspeaker_config());
		if (pcm_out_channel_map == 0) {
			pcm_out_channel_map = get_sacd_channel_map_from_channels(pcm_out_channels);
		}
		pcm_min_samplerate = DSDxFs1;
		while ((pcm_min_samplerate / framerate) * framerate != pcm_min_samplerate) {
			pcm_min_samplerate *= 2;
		}
		pcm_out_samplerate = max(pcm_min_samplerate, pcm_out_samplerate);
		pcm_out_samples = pcm_out_samplerate / framerate;
		pcm_buf.set_size(pcm_out_channels * pcm_out_samples);
		memset(sacd_bitrate, 0, sizeof(sacd_bitrate));
		sacd_bitrate_idx = 0;
		sacd_bitrate_sum = 0;
		use_dsd_path = false;
		double* fir_data = nullptr;
		int fir_size = 0;
		if (get_converter_type() == DSDPCM_CONV_USER) {
			fir_data = CSACDPreferences::get_user_fir().get_ptr();
			fir_size = CSACDPreferences::get_user_fir().get_size();
		}
		bool skip_init = false;
		if (flags & input_flag_playback) {
			dsdpcm_decoder = g_dsdpcm_playback;
			skip_init = g_track_completed;
			g_track_completed = false;
			use_dsd_path = CSACDPreferences::in_dsd_mode();
		}
		else {
			dsdpcm_convert = new DSDPCMConverterEngine();
			dsdpcm_decoder = dsdpcm_convert;
			skip_init = false;
		}
		dsdpcm_decoder->set_gain((float)CSACDPreferences::get_volume());
		int rv = dsdpcm_decoder->init(pcm_out_channels, framerate, dsd_samplerate, pcm_out_samplerate, get_converter_type(), get_converter_fp64(), fir_data, fir_size, skip_init);
		if (rv < 0) {
			if (rv == -2) {
				popup_message::g_show("No installed FIR, continue with the default", "DSD2PCM", popup_message::icon_error);
			}
			int rv = dsdpcm_decoder->init(pcm_out_channels, framerate, dsd_samplerate, pcm_out_samplerate, DSDPCM_CONV_DIRECT, get_converter_fp64(), nullptr, 0, skip_init);
			if (rv < 0) {
				throw exception_io();
			}
		}
		if (flags & input_flag_playback) {
			pcm_out_delay = 0.0f;
		}
		else {
			pcm_out_delay = dsdpcm_decoder->get_delay();
		}
		pcm_out_delta = (int)(pcm_out_delay + 0.5f);
		if (pcm_out_delta > pcm_out_samples - 1) {
			pcm_out_delta = pcm_out_samples - 1;
		}
		track_completed = false;
		excpt_cnt = 0;
	}

	bool decode_run_internal(audio_chunk& p_chunk, abort_callback& p_abort) {
		uint8_t* dsd_data;
		uint8_t* dst_data;
		if (track_completed) {
			return false;
		}
		size_t dsd_size = 0;
		size_t dst_size = 0;
		for (;;) {
			int slot_nr = dst_decoder ? dst_decoder->get_slot_nr() : 0;
			dsd_data = dsd_buf.get_ptr() + dsd_buf_size * slot_nr;
			dst_data = dst_buf.get_ptr() + dst_buf_size * slot_nr;
			dst_size = dst_buf_size;
			frame_type_e frame_type;
			if (sacd_reader->read_frame(dst_data, &dst_size, &frame_type)) {
				if (dst_size > 0) {
					if (frame_type == FRAME_INVALID) {
						dst_size = dst_buf_size; 
						memset(dst_data, DSD_SILENCE_BYTE, dst_size);
					}
					if (frame_type == FRAME_DST) {
						if (!dst_decoder) {
							dst_decoder = new dst_decoder_t(DST_DECODER_THREADS);
							if (!dst_decoder || dst_decoder->init(sacd_reader->get_channels(), sacd_reader->get_samplerate(), sacd_reader->get_framerate()) != 0) {
								return false;
							}
						}
						dst_decoder->decode(dst_data, dst_size, &dsd_data, &dsd_size);
					}
					else {
						dsd_data = dst_data;
						dsd_size = dst_size;
					}
					sacd_bitrate_idx = (++sacd_bitrate_idx) % BITRATE_AVGS;
					sacd_bitrate_sum -= sacd_bitrate[sacd_bitrate_idx];
					sacd_bitrate[sacd_bitrate_idx] = dst_size * 8 * framerate;
					sacd_bitrate_sum += sacd_bitrate[sacd_bitrate_idx];
					if (dsd_size > 0) {
						if (use_dsd_path) {
							dsd_stream->write(dsd_data, dsd_size / pcm_out_channels, pcm_out_channels, dsd_samplerate, pcm_out_channel_map);
							p_chunk.set_sample_rate(pcm_min_samplerate);
							p_chunk.set_channels(pcm_out_channels, pcm_out_channel_map);
							p_chunk.set_silence(pcm_min_samplerate / framerate);
						}
						else {
							int remove_samples = dsdpcm_decoder->is_convert_called() ? 0 : pcm_out_delta;
							dsdpcm_decoder->convert(dsd_data, dsd_size, pcm_buf.get_ptr());
							if (remove_samples > 0) {
								fix_pcm_stream(false, pcm_buf.get_ptr() + pcm_out_channels * remove_samples, pcm_out_samples - remove_samples, pcm_out_channels);
							}
							p_chunk.set_data(pcm_buf.get_ptr() + pcm_out_channels * remove_samples, pcm_out_samples - remove_samples, pcm_out_channels, pcm_out_samplerate, pcm_out_channel_map);
							if (log_overloads) {
								decode_check_overloads(pcm_buf.get_ptr() + pcm_out_channels * remove_samples, pcm_out_samples - remove_samples);
							}
							pcm_out_offset += pcm_out_samples - remove_samples;
						}
						return true;
					}
				}
			}
			else {
				break;
			}
		}
		dsd_data = nullptr;
		dst_data = nullptr;
		dst_size = 0;
		if (dst_decoder) {
			dst_decoder->decode(dst_data, dst_size, &dsd_data, &dsd_size);
		}
		if (use_dsd_path) {
			if (dsd_size > 0) {
				dsd_stream->write(dsd_data, dsd_size / pcm_out_channels, pcm_out_channels, dsd_samplerate, pcm_out_channel_map);
				p_chunk.set_sample_rate(pcm_min_samplerate);
				p_chunk.set_channels(pcm_out_channels, pcm_out_channel_map);
				p_chunk.set_silence(pcm_min_samplerate / framerate);
				return true;
			}
			if (flags & input_flag_playback) {
				g_track_completed = true;
			}
		}
		else {
			if (dsd_size > 0) {
				dsdpcm_decoder->convert(dsd_data, dsd_size, pcm_buf.get_ptr());
				p_chunk.set_data(pcm_buf.get_ptr(), pcm_out_samples, pcm_out_channels, pcm_out_samplerate, pcm_out_channel_map);
				if (log_overloads) {
					decode_check_overloads(pcm_buf.get_ptr(), pcm_out_samples);
				}
				pcm_out_offset += pcm_out_samples;
				return true;
			}
			if (flags & input_flag_playback) {
				g_track_completed = true;
			}
			else {
				int append_samples = pcm_out_delta;
				if (append_samples > 0) {
					dsdpcm_decoder->convert(nullptr, 0, pcm_buf.get_ptr());
					fix_pcm_stream(true, pcm_buf.get_ptr(), append_samples, pcm_out_channels);
					p_chunk.set_data(pcm_buf.get_ptr(), append_samples, pcm_out_channels, pcm_out_samplerate, pcm_out_channel_map);
					if (log_overloads) {
						decode_check_overloads(pcm_buf.get_ptr(), append_samples);
					}
				}
				pcm_out_offset += append_samples;
				track_completed = true;
				return true;
			}
			track_completed = true;
		}
		return false;
	}

	const char* get_time_stamp(double t) {
		static char ts[13];
		int fraction = (int)((t - floor(t)) * 1000);
		int second = (int)floor(t);
		int minute = second / 60;
		int hour = minute / 60;
		ts[ 0] = '0' + (hour / 10) % 6;
		ts[ 1] = '0' + hour % 10;
		ts[ 2] = ':';
		ts[ 3] = '0' + (minute / 10) % 6;
		ts[ 4] = '0' + minute % 10;
		ts[ 5] = '.';
		ts[ 6] = '0' + (second / 10) % 6;
		ts[ 7] = '0' + second % 10;
		ts[ 8] = '.';
		ts[ 9] = '0' + (fraction / 100) % 10;
		ts[10] = '0' + (fraction / 10) % 10;
		ts[11] = '0' + fraction % 10;
		ts[12] = '\0';
		return ts;
	}

	void decode_check_overloads(const audio_sample* pcm_data, t_size pcm_samples) {
		for (t_size sample = 0; sample < pcm_samples; sample++) {
			for (int ch = 0; ch < pcm_out_channels; ch++) {
				audio_sample v = pcm_data[sample * pcm_out_channels + ch];
				if ((v > +PCM_OVERLOAD_THRESHOLD) || (v < -PCM_OVERLOAD_THRESHOLD)) {
					double overload_time = (double)(pcm_out_offset + sample) / (double)pcm_out_samplerate;
					const char* track_name = log_track_name;
					const char* time_stamp = get_time_stamp(overload_time);
					console_fprintf(nullptr, "Overload at '%s' ch:%d [%s]", track_name, ch, time_stamp);
					break;
				}
			}
		}
	}

	bool decode_run(audio_chunk& p_chunk, abort_callback& p_abort) {
		bool rv;
		__try {
			rv = decode_run_internal(p_chunk, p_abort);
			excpt_cnt = 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			excpt_cnt++;
			console::printf("Exception caught in decode_run");
			rv = excpt_cnt < 1000;
		}
		return rv;
	}

	void decode_seek(double p_seconds, abort_callback& p_abort) {
		if (!sacd_reader->seek(p_seconds)) {
			throw exception_io();
		}
	}

	bool decode_can_seek() {
		return sacd_media->can_seek();
	}

	bool decode_get_dynamic_info(file_info& p_info, double& p_timestamp_delta) {
		DWORD curr_time_ms = GetTickCount();
		if (info_update_time_ms + (DWORD)UPDATE_STATS_MS >= curr_time_ms) {
			return false;
		}
		info_update_time_ms = curr_time_ms;
		p_info.info_set_bitrate_vbr(((t_int64)(sacd_bitrate_sum / BITRATE_AVGS) + 500) / 1000);
		return true;
	}
	
	bool decode_get_dynamic_info_track(file_info& p_info, double& p_timestamp_delta) {
		return false;
	}
	
	void decode_on_idle(abort_callback & p_abort) {
		sacd_media->on_idle();
	}

	void retag_set_info(t_uint32 p_subsong, const file_info& p_info, abort_callback& p_abort) {
		if (CSACDPreferences::get_editable_tags() && !cue_playback) {
			if (sacd_metabase) {
				sacd_metabase->set_meta_info(p_subsong, p_info, false);
				sacd_metabase->set_replaygain(p_subsong, p_info, (float)CSACDPreferences::get_volume());
				if (CSACDPreferences::get_linked_tags()) {
					t_uint32 twoch_count = sacd_reader->get_track_count(AREA_TWOCH);
					t_uint32 mulch_count = sacd_reader->get_track_count(AREA_MULCH);
					if (twoch_count == mulch_count) {
						sacd_metabase->set_meta_info((p_subsong < twoch_count) ? p_subsong + twoch_count : p_subsong - twoch_count, p_info, true);
					}
				}
			}
#ifdef _USE_MTAGS
			if (tags_metabase) {
				tags_metabase->set_info(p_subsong, p_info, p_abort);
			}
#endif
			sacd_reader->set_info(p_subsong, p_info);
		}
	}
	
	void retag_commit(abort_callback& p_abort) {
		if (CSACDPreferences::get_editable_tags() && !cue_playback) {
			if (sacd_metabase) {
				sacd_metabase->commit();
			}
#ifdef _USE_MTAGS
			if (tags_metabase) {
				tags_metabase->commit(p_abort);
			}
#endif
			sacd_reader->commit();
		}
	}

	static bool g_is_our_content_type(const char* p_content_type) {
		return false;
	}
	
	static bool g_is_our_path(const char* p_path, const char* p_ext) {
		g_cue_playback = g_cue_playback || stricmp_utf8(p_ext, "CUE") == 0; 
		string_filename_ext filename_ext(p_path);
		return
			(stricmp_utf8(p_ext, "ISO") == 0 || stricmp_utf8(p_ext, "DAT") == 0) && sacd_disc_t::g_is_sacd(p_path) ||
			stricmp_utf8(p_ext, "DFF") == 0 ||
			stricmp_utf8(p_ext, "DSF") == 0 ||
			(stricmp_utf8(filename_ext, "") == 0 || stricmp_utf8(filename_ext, "MASTER1.TOC") == 0) && strlen_utf8(p_path) > 7 && sacd_disc_t::g_is_sacd(p_path[7]);
	}
};

static input_factory_ex_t<input_sacd_t> g_input_sacd_factory;

class initquit_sacd_t : public initquit {
public:
	virtual void on_init() {
#ifdef _DEBUG
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
		g_dsdpcm_playback = new DSDPCMConverterEngine();
	}
	
	virtual void on_quit() {
		delete g_dsdpcm_playback;
	}
};

static initquit_factory_t<initquit_sacd_t> g_initquit_sacd_factory;

DECLARE_COMPONENT_VERSION("Super Audio CD Decoder", "0.9.7", "Super Audio CD Decoder Input PlugIn.\n\nCopyright (c) 2011-2015 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>");
DECLARE_FILE_TYPE("SACD files", "*.DAT;*.DFF;*.DSF;*.ISO;MASTER1.TOC");
