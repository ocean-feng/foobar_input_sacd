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

#include "sacd_dsdiff.h"

#define MARK_TIME(m) ((double)m.hours * 60 * 60 + (double)m.minutes * 60 + (double)m.seconds + ((double)m.samples + (double)m.offset) / (double)m_samplerate)

sacd_dsdiff_t::sacd_dsdiff_t() {
	m_current_subsong = 0;
	m_dst_encoded = 0;
	m_id3tags_indexed = false;
}

sacd_dsdiff_t::~sacd_dsdiff_t() {
	close();
}

sacd_media_t* sacd_dsdiff_t::get_media() {
	return m_file;
}

uint32_t sacd_dsdiff_t::get_track_count(area_id_e area_id) {
	if (area_id == AREA_TWOCH && m_channel_count <= 2 || area_id == AREA_MULCH && m_channel_count > 2 || area_id == AREA_BOTH) {
		return m_subsong.size();
	}
	return 0;
}

int sacd_dsdiff_t::get_channels() {
	return m_channel_count;
}

int sacd_dsdiff_t::get_loudspeaker_config() {
	return m_loudspeaker_config;
}

int sacd_dsdiff_t::get_samplerate() {
	return m_samplerate;
}

int sacd_dsdiff_t::get_framerate() {
	return m_framerate;
}

uint64_t sacd_dsdiff_t::get_size() {
	return m_current_size;
}

uint64_t sacd_dsdiff_t::get_offset() {
	return m_file->get_position() - m_current_offset;
}

double sacd_dsdiff_t::get_duration() {
	if (m_current_subsong < m_subsong.size()) {
		return m_subsong[m_current_subsong].stop_time - m_subsong[m_current_subsong].start_time;
	}
	return 0.0;
}

double sacd_dsdiff_t::get_duration(uint32_t subsong) {
	if (subsong < m_subsong.size()) {
		return m_subsong[subsong].stop_time - m_subsong[subsong].start_time;
	}
	return 0.0;
}

bool sacd_dsdiff_t::is_dst() {
	return m_dst_encoded != 0;
}

bool sacd_dsdiff_t::open(sacd_media_t* p_file, uint32_t mode) {
	m_file = p_file;
	m_dsti_size = 0;
	Chunk ck;
	ID id;
	bool skip_emaster_chunks = (mode & MODE_SINGLE_TRACK) == MODE_SINGLE_TRACK;
	bool full_playback = (mode & MODE_FULL_PLAYBACK) == MODE_FULL_PLAYBACK;
	uint32_t id3_tag_index = 0;
	uint32_t start_mark_count = 0;
	id3tags_t t_old;
	m_subsong.resize(0);
	m_id3tags.resize(0);
	m_id3tags_indexed = false;
	if (!m_file->seek(0)) {
		return false;
	}
	if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck) && ck.has_id("FRM8"))) {
		return false;
	}
	if (!(m_file->read(&id, sizeof(id)) == sizeof(id) && id.has_id("DSD "))) {
		return false;
	}
	m_frm8_size = ck.get_size();
	m_id3_offset = sizeof(ck) + ck.get_size();
	while ((uint64_t)m_file->get_position() < m_frm8_size + sizeof(ck)) {
		if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck))) {
			return false;
		}
		if (ck.has_id("FVER") && ck.get_size() == 4) {
			uint32_t version;
			if (!(m_file->read(&version, sizeof(version)) == sizeof(version))) {
				return false;
			}
			m_version = hton32(version);
		}
		else if (ck.has_id("PROP")) {
			if (!(m_file->read(&id, sizeof(id)) == sizeof(id) && id.has_id("SND "))) {
				return false;
			}
			uint64_t id_prop_size = ck.get_size() - sizeof(id);
			uint64_t id_prop_read = 0;
			while (id_prop_read < id_prop_size) {
				if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck))) {
					return false;
				}
				if (ck.has_id("FS  ") && ck.get_size() == 4) {
					uint32_t samplerate;
					if (!(m_file->read(&samplerate, sizeof(samplerate)) == sizeof(samplerate))) {
						return false;
					}
					m_samplerate = hton32(samplerate);
				}
				else if (ck.has_id("CHNL")) {
					uint16_t channel_count;
					if (!(m_file->read(&channel_count, sizeof(channel_count)) == sizeof(channel_count))) {
						return false;
					}
					m_channel_count = hton16(channel_count);
					switch (m_channel_count) {
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
						m_loudspeaker_config = 3; 
						break;
					case 6:
						m_loudspeaker_config = 4; 
						break;
					default:
						m_loudspeaker_config = 65535; 
						break;
					}
					m_file->skip(ck.get_size() - sizeof(channel_count));
				}
				else if (ck.has_id("CMPR")) {
					if (!(m_file->read(&id, sizeof(id)) == sizeof(id))) {
						return false;
					}
					if (id.has_id("DSD ")) {
						m_dst_encoded = 0;
					}
					if (id.has_id("DST ")) {
						m_dst_encoded = 1;
					}
					m_file->skip(ck.get_size() - sizeof(id));
				}
				else if (ck.has_id("LSCO")) {
					uint16_t loudspeaker_config;
					if (!(m_file->read(&loudspeaker_config, sizeof(loudspeaker_config)) == sizeof(loudspeaker_config))) {
						return false;
					}
					m_loudspeaker_config = hton16(loudspeaker_config);
					m_file->skip(ck.get_size() - sizeof(loudspeaker_config));
				}
				else if (ck.has_id("ID3 ")) {
					t_old.index  = 0;
					t_old.offset = m_file->get_position();
					t_old.size   = ck.get_size();
					t_old.data.resize((uint32_t)ck.get_size());
					m_file->read(t_old.data.data(), t_old.data.size());
				}
				else {
					m_file->skip(ck.get_size());
				}
				id_prop_read += sizeof(ck) + ck.get_size() + (ck.get_size() & 1);
				m_file->skip(m_file->get_position() & 1);
			}
		}
		else if (ck.has_id("DSD ")) {
			m_data_offset = m_file->get_position();
			m_data_size = ck.get_size();
			m_framerate = 75;
			m_frame_size = m_samplerate / 8 * m_channel_count / m_framerate;
			m_frame_count = (uint32_t)(m_data_size / m_frame_size);
			m_file->skip(ck.get_size());
			subsong_t s;
			s.start_time = 0.0;
			s.stop_time  = m_frame_count / m_framerate;
			m_subsong.push_back(s);
		}
		else if (ck.has_id("DST ")) {
			m_data_offset = m_file->get_position();
			m_data_size = ck.get_size();
			if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck) && ck.has_id("FRTE") && ck.get_size() == 6)) {
				return false;
			}
			m_data_offset += sizeof(ck) + ck.get_size();
			m_data_size -= sizeof(ck) + ck.get_size();
			m_current_offset = m_data_offset;
			m_current_size = m_data_size;
			uint32_t frame_count;
			if (!(m_file->read(&frame_count, sizeof(frame_count)) == sizeof(frame_count))) {
				return false;
			}
			m_frame_count = hton32(frame_count);
			uint16_t framerate;
			if (!(m_file->read(&framerate, sizeof(framerate)) == sizeof(framerate))) {
				return false;
			}
			m_framerate = hton16(framerate);
			m_frame_size = m_samplerate / 8 * m_channel_count / m_framerate;
			m_file->seek(m_data_offset + m_data_size);
			subsong_t s;
			s.start_time = 0.0;
			s.stop_time = (double)m_frame_count / m_framerate;
			m_subsong.push_back(s);
		}
		else if (ck.has_id("DSTI")) {
			m_dsti_offset = m_file->get_position();
			m_dsti_size = ck.get_size();
			m_file->skip(ck.get_size());
		}
		else if (ck.has_id("DIIN") && !skip_emaster_chunks) {
			uint64_t id_diin_size = ck.get_size();
			uint64_t id_diin_read = 0;
			while (id_diin_read < id_diin_size) {
				if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck))) {
					return false;
				}
				if (ck.has_id("MARK") && ck.get_size() >= sizeof(Marker)) {
					Marker m;
					if (m_file->read(&m, sizeof(Marker)) == sizeof(Marker)) {
						m.hours       = hton16(m.hours);
						m.samples     = hton32(m.samples);
						m.offset      = hton32(m.offset);
						m.markType    = hton16(m.markType);
						m.markChannel = hton16(m.markChannel);
						m.TrackFlags  = hton16(m.TrackFlags);
						m.count       = hton32(m.count);
						switch (m.markType) {
						case TrackStart:
							if (start_mark_count > 0) {
								subsong_t s;
								m_subsong.push_back(s);
							}
							start_mark_count++;
							if (m_subsong.size() > 0) {
								m_subsong[m_subsong.size() - 1].start_time = MARK_TIME(m);
								m_subsong[m_subsong.size() - 1].stop_time  = (double)m_frame_count / m_framerate;
								if (m_subsong.size() - 1 > 0) {
									if (m_subsong[m_subsong.size() - 2].stop_time > m_subsong[m_subsong.size() - 1].start_time) {
										m_subsong[m_subsong.size() - 2].stop_time =  m_subsong[m_subsong.size() - 1].start_time;
									}
								}
							}
							break;
						case TrackStop:
							if (!full_playback) {
								if (m_subsong.size() > 0) {
									m_subsong[m_subsong.size() - 1].stop_time = MARK_TIME(m);
								}
							}
							break;
						}
					}
					m_file->skip(ck.get_size() - sizeof(Marker));
				}
				else {
					m_file->skip(ck.get_size());
				}
				id_diin_read += sizeof(ck) + ck.get_size();
				m_file->skip(m_file->get_position() & 1);
			}
		}
		else if (ck.has_id("ID3 ") && !skip_emaster_chunks) {
			m_id3_offset = min(m_id3_offset,  (uint64_t)m_file->get_position() - sizeof(ck));
			id3tags_t t;
			t.index  = m_id3tags.size();
			t.offset = m_file->get_position();
			t.size   = ck.get_size();
			t.data.resize((uint32_t)ck.get_size());
			m_file->read(t.data.data(), t.data.size());
			m_id3tags.push_back(t);
		}
		else {
			m_file->skip(ck.get_size());
		}
		m_file->skip(m_file->get_position() & 1);
	}
	if (m_id3tags.size() == 0) {
		if (t_old.size > 0) {
			m_id3tags.push_back(t_old);
		}
	}
	m_file->seek(m_data_offset);
	return m_subsong.size() > 0;
}

bool sacd_dsdiff_t::close() {
	m_current_subsong = 0;
	m_subsong.resize(0);
	m_id3tags.resize(0);
	m_id3tags_indexed = false;
	m_dsti_size = 0;
	return true;
}

void sacd_dsdiff_t::set_area(area_id_e area_id) {
}

void sacd_dsdiff_t::set_emaster(bool emaster) {
}

bool sacd_dsdiff_t::set_track(uint32_t track_number, area_id_e area_id, uint32_t offset) {
	if (track_number < m_subsong.size()) {
		m_current_subsong = track_number;
		double t0 = m_subsong[m_current_subsong].start_time;
		double t1 = m_subsong[m_current_subsong].stop_time;
		uint64_t offset = (uint64_t)(t0 * m_framerate / m_frame_count * m_data_size);
		uint64_t size = (uint64_t)(t1 * m_framerate / m_frame_count * m_data_size) - offset;
		if (m_dst_encoded) {
			if (m_dsti_size > 0) {
				if ((uint32_t)(t0 * m_framerate) < (uint32_t)(m_dsti_size / sizeof(DSTFrameIndex) - 1)) {
					m_current_offset = get_dsti_for_frame((uint32_t)(t0 * m_framerate));
				}
				else {
					m_current_offset = m_data_offset + offset;
				}
				if ((uint32_t)(t1 * m_framerate) < (uint32_t)(m_dsti_size / sizeof(DSTFrameIndex) - 1)) {
					m_current_size = get_dsti_for_frame((uint32_t)(t1 * m_framerate)) - m_current_offset;
				}
				else {
					m_current_size = size;
				}
			}
			else {
				m_current_offset = m_data_offset + offset;
				m_current_size = size;
			}
		}
		else {
			m_current_offset = m_data_offset + (offset / m_frame_size) * m_frame_size;
			m_current_size = (size / m_frame_size) * m_frame_size;
		}
	}
	m_file->seek(m_current_offset);
	return true;
}

bool sacd_dsdiff_t::read_frame(uint8_t* frame_data, size_t* frame_size, frame_type_e* frame_type) {
//static uint64_t s_next_frame = 0;
//if (m_file->get_position() != s_next_frame) {
//	console::printf("offset: %d - %d", (uint32_t)m_file->get_position(), (uint32_t)s_next_frame);
//}
	if (m_dst_encoded) {
		Chunk ck;
		while ((uint64_t)m_file->get_position() < m_current_offset + m_current_size && m_file->read(&ck, sizeof(ck)) == sizeof(ck)) {
			if (ck.has_id("DSTF") && ck.get_size() <= (uint64_t)*frame_size) {
				if (m_file->read(frame_data, (size_t)ck.get_size()) == ck.get_size()) {
					m_file->skip(ck.get_size() & 1);
					*frame_size = (size_t)ck.get_size();
					*frame_type = FRAME_DST;
//s_next_frame = m_file->get_position();
					return true;
				}
				break;
			}
			else if (ck.has_id("DSTC") && ck.get_size() == 4) {
				uint32_t crc;
				if (ck.get_size() == sizeof(crc)) {
					if (m_file->read(&crc, sizeof(crc)) != sizeof(crc)) {
						break;
					}
				}
				else {
					m_file->skip(ck.get_size());
					m_file->skip(ck.get_size() & 1);
				}
			}
			else {
				m_file->seek(1 - (int)sizeof(ck), file::seek_from_current);
			}
		}
	}
	else {
		int64_t position = m_file->get_position();
		*frame_size = (size_t)min((int64_t)*frame_size, max((int64_t)0, (int64_t)(m_current_offset + m_current_size) - position));
		if (*frame_size > 0) {
			*frame_size = m_file->read(frame_data, *frame_size);
			*frame_size -= *frame_size % m_channel_count;
			if (*frame_size > 0) {
				*frame_type = FRAME_DSD;
				//s_next_frame = m_file->get_position();
				return true;
			}
		}
	}
	*frame_type = FRAME_INVALID;
	return false;
}

bool sacd_dsdiff_t::seek(double seconds) {
	uint64_t offset = min((uint64_t)(get_size() * seconds / get_duration()), get_size());
	if (m_dst_encoded) {
		if (m_dsti_size > 0) {
			uint32_t frame = min((uint32_t)((m_subsong[m_current_subsong].start_time + seconds) * m_framerate), m_frame_count - 1);
			if (frame < (uint32_t)(m_dsti_size / sizeof(DSTFrameIndex) - 1)) {
				offset = get_dsti_for_frame(frame) - m_current_offset;
			}
		}
	}
	else {
		offset = (offset / m_frame_size) * m_frame_size;
	}
	m_file->seek(m_current_offset + offset);
	return true;
}

void sacd_dsdiff_t::get_info(uint32_t subsong, file_info& info) {
	if (!m_id3tags_indexed) {
		index_id3tags(info);
		m_id3tags_indexed = true;
	}
	for (uint32_t i = 0; i < m_id3tags.size(); i++) {
		if (subsong == m_id3tags[i].index) {
			get_id3tags(i, info); 
			break;
		}
	}
}

void sacd_dsdiff_t::set_info(uint32_t subsong, const file_info& info) {
	if (m_id3tags_indexed) {
		set_id3tags(subsong, info);
	}
}

bool sacd_dsdiff_t::commit() {
	m_file->truncate(m_id3_offset);
	m_file->seek(m_id3_offset);
	bool tag_found = false;
	for (uint32_t i = 0; i < m_id3tags.size(); i++) {
		write_id3tag(m_id3tags[i].data.data(), m_id3tags[i].data.size());
	}
	Chunk ck;
	ck.set_id("FRM8");
	ck.set_size(m_file->get_position() - sizeof(Chunk));
	m_file->seek(0);
	m_file->write(&ck, sizeof(ck));
	return true;
}

uint64_t sacd_dsdiff_t::get_dsti_for_frame(uint32_t frame_nr) {
	uint64_t      cur_offset;
	DSTFrameIndex frame_index;
	cur_offset = m_file->get_position();
	frame_nr = min(frame_nr, (uint32_t)(m_dsti_size / sizeof(DSTFrameIndex) - 1)); 
	m_file->seek(m_dsti_offset + frame_nr * sizeof(DSTFrameIndex));
	cur_offset = m_file->get_position();
	m_file->read(&frame_index, sizeof(DSTFrameIndex));
	m_file->seek(cur_offset);
	return hton64(frame_index.offset) - sizeof(Chunk);
}

void sacd_dsdiff_t::get_id3tags(uint32_t subsong, file_info& info) {
	if (m_id3tags[subsong].data.size() > 0) {
		try {
			service_ptr_t<file> f;
			abort_callback_impl abort;
			filesystem::g_open_tempmem(f, abort);
			f->write(m_id3tags[subsong].data.data(), m_id3tags[subsong].data.size(), abort);
			tag_processor::read_id3v2(f, info, abort);
		}
		catch (exception_io_data) {
		}
	}
}

void sacd_dsdiff_t::set_id3tags(uint32_t subsong, const file_info& info) {
	uint32_t index = m_id3tags.size();
	for (uint32_t i = 0; i < m_id3tags.size(); i++) {
		if (subsong == m_id3tags[i].index) {
			index = i;
			break;
		}
	}
	if (index == m_id3tags.size()) {
		id3tags_t t;
		t.offset  = 0;
		t.size    = 0;
		t.index = subsong;
		m_id3tags.push_back(t);
	}
	if (info.meta_get_count() == 0) {
		for (uint32_t i = index + 1; i < m_id3tags.size(); i++) {
			m_id3tags[i - 1] = m_id3tags[i];
		}
		m_id3tags.resize(m_id3tags.size() - 1);
	}
	else {
		try {
			service_ptr_t<file> f;
			abort_callback_impl abort;
			filesystem::g_open_tempmem(f, abort);
			tag_processor::write_id3v2(f, info, abort);
			m_id3tags[index].data.resize((uint32_t)f->get_size(abort));
			f->seek(0, abort);
			f->read(m_id3tags[index].data.data(), m_id3tags[index].data.size(), abort);
		}
		catch (exception_io_data) {
		}
	}
}

void sacd_dsdiff_t::index_id3tags(file_info& info) {
	for (uint32_t i = 0; i < m_id3tags.size(); i++) {
		get_id3tags(i, info);
		const char* meta = info.meta_get("tracknumber", 0);
		long tracknumber = meta ? atol(meta) : 0;
		m_id3tags[i].index = tracknumber > 0 ? (uint32_t)(tracknumber - 1) : i;
	}
}

void sacd_dsdiff_t::write_id3tag(const void* data, uint32_t size) {
	Chunk ck;
	ck.set_id("ID3 ");
	ck.set_size(size);
	m_file->write(&ck, sizeof(ck));
	m_file->write(data, size);
	if (m_file->get_position() & 1) {
		const uint8_t c = 0;
		m_file->write(&c, 1);
	}
}

bool sacd_dsdiff_t::set_samplerate(sacd_media_t* p_file, uint32_t p_samplerate) {
	bool updated = false;
	m_file = p_file;
	Chunk ck;
	ID id;
	if (!m_file->seek(0)) {
		return false;
	}
	if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck) && ck.has_id("FRM8"))) {
		return false;
	}
	if (!(m_file->read(&id, sizeof(id)) == sizeof(id) && id.has_id("DSD "))) {
		return false;
	}
	m_frm8_size = ck.get_size();
	while ((uint64_t)m_file->get_position() < m_frm8_size + sizeof(ck)) {
		if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck))) {
			return false;
		}
		if (ck.has_id("FVER") && ck.get_size() == 4) {
			uint32_t version;
			if (!(m_file->read(&version, sizeof(version)) == sizeof(version))) {
				return false;
			}
			m_version = hton32(version);
		}
		else if (ck.has_id("PROP")) {
			if (!(m_file->read(&id, sizeof(id)) == sizeof(id) && id.has_id("SND "))) {
				return false;
			}
			uint64_t id_prop_size = ck.get_size() - sizeof(id);
			uint64_t id_prop_read = 0;
			while (id_prop_read < id_prop_size) {
				if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck))) {
					return false;
				}
				if (ck.has_id("FS  ") && ck.get_size() == 4) {
					uint32_t samplerate;
					if (!(m_file->read(&samplerate, sizeof(samplerate)) == sizeof(samplerate))) {
						return false;
					}
					m_samplerate = hton32(samplerate);
					if (m_samplerate != p_samplerate) {
						m_file->seek(m_file->get_position() - sizeof(samplerate));
						samplerate = hton32(p_samplerate);
						if (!(m_file->write(&samplerate, sizeof(samplerate)) == sizeof(samplerate))) {
							return false;
						}
						updated = true;
					}
				}
				else if (ck.has_id("CMPR")) {
					if (!(m_file->read(&id, sizeof(id)) == sizeof(id))) {
						return false;
					}
					if (id.has_id("DSD ")) {
						m_dst_encoded = 0;
					}
					if (id.has_id("DST ")) {
						m_dst_encoded = 1;
					}
					m_file->skip(ck.get_size() - sizeof(id));
				}
				else {
					m_file->skip(ck.get_size());
				}
				id_prop_read += sizeof(ck) + ck.get_size() + (ck.get_size() & 1);
				m_file->skip(m_file->get_position() & 1);
			}
		}
		else if (ck.has_id("DSD ")) {
			m_data_offset = m_file->get_position();
			m_data_size = ck.get_size();
			m_framerate = 75;
			m_frame_size = m_samplerate / 8 * m_channel_count / m_framerate;
			m_frame_count = (uint32_t)(m_data_size / m_frame_size);
			m_file->skip(ck.get_size());
		}
		else if (ck.has_id("DST ")) {
			m_data_offset = m_file->get_position();
			m_data_size = ck.get_size();
			if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck) && ck.has_id("FRTE") && ck.get_size() == 6)) {
				return false;
			}
			m_data_offset += sizeof(ck) + ck.get_size();
			m_data_size -= sizeof(ck) + ck.get_size();
			m_current_offset = m_data_offset;
			m_current_size = m_data_size;
			uint32_t frame_count;
			if (!(m_file->read(&frame_count, sizeof(frame_count)) == sizeof(frame_count))) {
				return false;
			}
			m_frame_count = hton32(frame_count);
			uint16_t framerate;
			if (!(m_file->read(&framerate, sizeof(framerate)) == sizeof(framerate))) {
				return false;
			}
			m_framerate = hton16(framerate);
			m_frame_size = m_samplerate / 8 * m_channel_count / m_framerate;
			if (m_samplerate != p_samplerate) {
				m_file->seek(m_file->get_position() - sizeof(framerate));
				m_framerate = (uint16_t)((uint64_t)p_samplerate * (uint64_t)m_framerate / (uint64_t)m_samplerate);
				framerate = hton16(m_framerate);
				if (!(m_file->write(&framerate, sizeof(framerate)) == sizeof(framerate))) {
					return false;
				}
				updated = true;
			}
			m_file->seek(m_data_offset + m_data_size);
		}
		else {
			m_file->skip(ck.get_size());
		}
		m_file->skip(m_file->get_position() & 1);
	}
	m_file->seek(m_data_offset);
	return updated;
}
