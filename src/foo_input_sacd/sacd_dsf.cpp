/*
* SACD Decoder plugin
* Copyright (c) 2011-2014 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
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

#include "sacd_dsf.h"

#define MARK_TIME(m) ((double)m.hours * 60 * 60 + (double)m.minutes * 60 + (double)m.seconds + ((double)m.samples + (double)m.offset) / (double)m_samplerate)

sacd_dsf_t::sacd_dsf_t() {
	for (int i = 0; i < 256; i++) {
		swap_bits[i] = 0;
		for (int j = 0; j < 8; j++) {
			swap_bits[i] |= ((i >> j) & 1) << (7 - j);
		}
	}
}

sacd_dsf_t::~sacd_dsf_t() {
	close();
}

sacd_media_t* sacd_dsf_t::get_media() {
	return m_file;
}

uint32_t sacd_dsf_t::get_track_count(area_id_e area_id) {
	if (area_id == AREA_TWOCH && m_channel_count <= 2 || area_id == AREA_MULCH && m_channel_count > 2 || area_id == AREA_BOTH) {
		return 1;
	}
	return 0;
}

int sacd_dsf_t::get_channels() {
	return m_channel_count;
}

int sacd_dsf_t::get_loudspeaker_config() {
	return m_loudspeaker_config;
}

int sacd_dsf_t::get_samplerate() {
	return m_samplerate;
}

int sacd_dsf_t::get_framerate() {
	return m_framerate;
}

uint64_t sacd_dsf_t::get_size() {
	return (m_sample_count / 8) * m_channel_count;
}

uint64_t sacd_dsf_t::get_offset() {
	return m_file->get_position() - m_read_offset;
}

double sacd_dsf_t::get_duration() {
	return m_samplerate > 0 ? (double)m_sample_count / m_samplerate : 0.0;
}

double sacd_dsf_t::get_duration(uint32_t subsong) {
	if (subsong < 1) {
		return m_samplerate > 0 ? (double)m_sample_count / m_samplerate : 0.0;
	}
	return 0.0;
}

bool sacd_dsf_t::is_dst() {
	return false;
}

bool sacd_dsf_t::open(sacd_media_t* p_file, uint32_t mode) {
	m_file = p_file;
	Chunk ck;
	FmtDSFChunk fmt;
	uint64_t pos;
	if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck) && ck.has_id("DSD "))) {
		return false;
	}
	if (ck.get_size() != hton64((uint64_t)28)) {
		return false;
	}
	if (m_file->read(&m_file_size, sizeof(m_file_size)) != sizeof(m_file_size)) {
		return false;
	}
	if (m_file->read(&m_id3_offset, sizeof(m_id3_offset)) != sizeof(m_id3_offset)) {
		return false;
	}
	pos = m_file->get_position();
	if (!(m_file->read(&fmt, sizeof(fmt)) == sizeof(fmt) && fmt.has_id("fmt "))) {
		return false;
	}
	if (fmt.format_id != 0) {
		return false;
	}
	m_version = fmt.format_version;
	switch (fmt.channel_type) {
	case 1:
		m_loudspeaker_config = 5;
		break;
	case 2:
		m_loudspeaker_config = 0;
		break;
	case 3:
		m_loudspeaker_config = 6;
		break;
	case 4:
		m_loudspeaker_config = 1;
		break;
	case 5:
		m_loudspeaker_config = 2;
		break;
	case 6:
		m_loudspeaker_config = 3;
		break;
	case 7:
		m_loudspeaker_config = 4;
		break;
	default:
		m_loudspeaker_config = 65535;
		break;
	}
	if (fmt.channel_count < 1 || fmt.channel_count > 6) {
		return false;
	}
	m_channel_count = fmt.channel_count;
	m_samplerate = fmt.samplerate;
	m_framerate = 75;
	switch (fmt.bits_per_sample) {
	case 1:
		m_is_lsb = true;
		break;
	case 8:
		m_is_lsb = false;
		break;
	default:
		return false;
		break;
	}
	m_sample_count = fmt.sample_count;
	m_block_size = fmt.block_size;
	m_block_offset = m_block_size;
	m_block_data_end = 0;
	m_file->seek(pos + hton64(fmt.get_size()));
	if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck) && ck.has_id("data"))) {
		return false;
	}
	m_block_data.resize(m_channel_count * m_block_size);
	m_data_offset = m_file->get_position();
	m_data_end_offset = m_data_offset + get_size();
	m_data_size = hton64(ck.get_size()) - sizeof(ck);
	m_read_offset = m_data_offset;
	return true;
}

bool sacd_dsf_t::close() {
	return true;
}

void sacd_dsf_t::set_area(area_id_e area_id) {
}

void sacd_dsf_t::set_emaster(bool emaster) {
}

bool sacd_dsf_t::set_track(uint32_t track_number, area_id_e area_id, uint32_t offset) {
	if (track_number) {
		return false;
	}
	m_file->seek(m_data_offset);
	return true;
}

bool sacd_dsf_t::read_frame(uint8_t* frame_data, size_t* frame_size, frame_type_e* frame_type) {
	int samples_read = 0;
	for (int i = 0; i < (int)*frame_size / m_channel_count; i++) {
		if (m_block_offset * m_channel_count >= m_block_data_end) {
			m_block_data_end = (int)min(m_data_end_offset - m_file->get_position(), m_block_data.size());
			if (m_block_data_end > 0) {
				m_block_data_end = m_file->read(m_block_data.data(), m_block_data_end);
			}
			if (m_block_data_end > 0) {
				m_block_offset = 0;
			}
			else {
				break;
			}
		}
		for (int ch = 0; ch < m_channel_count; ch++) {
			uint8_t b = m_block_data.data()[ch * m_block_size + m_block_offset];
			frame_data[i * m_channel_count + ch] = m_is_lsb ? swap_bits[b] : b;
		}
		m_block_offset++;
		samples_read++;
	}
	*frame_size = samples_read * m_channel_count;
	*frame_type = samples_read > 0 ? FRAME_DSD : FRAME_INVALID;
	return samples_read > 0;
}

bool sacd_dsf_t::seek(double seconds) {
	uint64_t offset = min((uint64_t)(get_size() * seconds / get_duration()), get_size());
	offset = (offset / (m_block_size * m_channel_count)) * (m_block_size * m_channel_count);
	m_file->seek(m_data_offset + offset);
	m_block_offset = m_block_size;
	m_block_data_end = 0;
	return true;
}

void sacd_dsf_t::get_info(uint32_t subsong, file_info& info) {
	if (m_id3_offset > 0) {
		uint64_t pos;
		pos = m_file->get_position();
		m_file->seek(m_id3_offset);
		m_id3_data.resize((t_size)(m_file_size - m_id3_offset));
		m_file->read(m_id3_data.data(), m_id3_data.size());
		m_file->seek(pos);
		try {
			service_ptr_t<file> f;
			abort_callback_impl abort;
			filesystem::g_open_tempmem(f, abort);
			f->write(m_id3_data.data(), m_id3_data.size(), abort);
			tag_processor::read_id3v2(f, info, abort);
		}
		catch (exception_io_data) {
		}
	}
}

void sacd_dsf_t::set_info(uint32_t subsong, const file_info& info) {
	if (m_id3_data.size() > 0) {
		try {
			service_ptr_t<file> f;
			abort_callback_impl abort;
			filesystem::g_open_tempmem(f, abort);
			tag_processor::write_id3v2(f, info, abort);
			m_id3_data.resize((uint32_t)f->get_size(abort));
			if (f->get_size(abort) > 0) {
				f->seek(0, abort);
				f->read(m_id3_data.data(), m_id3_data.size(), abort);
			}
		}
		catch (exception_io_data) {
		}
	}
}

bool sacd_dsf_t::commit() {
	uint64_t pos = m_file->get_position();
	if (m_id3_offset == 0) {
		m_id3_offset = m_file_size;
	}
	m_file->truncate(m_id3_offset);
	m_file->seek(m_id3_offset);
	if (m_id3_data.data() > 0) {
		m_file->write(m_id3_data.data(), m_id3_data.size());
	}
	else {
		m_id3_offset = 0;
		m_file->seek(20);
		m_file->write(&m_id3_offset, sizeof(m_id3_offset));
	}
	m_file_size = m_file->get_size();
	m_file->seek(12);
	m_file->write(&m_file_size, sizeof(m_file_size));
	m_file->seek(pos);
	return true;
}

bool sacd_dsf_t::set_samplerate(sacd_media_t* p_file, uint32_t p_samplerate) {
	bool updated = false;
	m_file = p_file;
	Chunk ck;
	FmtDSFChunk fmt;
	uint64_t pos;
	if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck) && ck.has_id("DSD "))) {
		return false;
	}
	if (ck.get_size() != hton64((uint64_t)28)) {
		return false;
	}
	if (m_file->read(&m_file_size, sizeof(m_file_size)) != sizeof(m_file_size)) {
		return false;
	}
	if (m_file->read(&m_id3_offset, sizeof(m_id3_offset)) != sizeof(m_id3_offset)) {
		return false;
	}
	pos = m_file->get_position();
	if (!(m_file->read(&fmt, sizeof(fmt)) == sizeof(fmt) && fmt.has_id("fmt "))) {
		return false;
	}
	if (fmt.format_id != 0) {
		return false;
	}
	m_version = fmt.format_version;
	m_samplerate = fmt.samplerate;
	if (m_samplerate != p_samplerate) {
		fmt.samplerate = p_samplerate;
		m_file->seek(m_file->get_position() - sizeof(fmt));
		if (!(m_file->write(&fmt, sizeof(fmt)) == sizeof(fmt))) {
			return false;
		}
		updated = true;
	}
	return updated;
}
