/*
* SACD Decoder plugin
* Copyright (c) 2011-2015 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
*
* This module partially uses code from SACD Ripper http://code.google.com/p/sacd-ripper/ project
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

#include <string>
#include <foobar2000.h>
#include "sacd_disc.h"

using namespace std;

static inline int has_two_channel(scarletbook_handle_t* handle) {
	return handle->twoch_area_idx != -1;
}

static inline int has_multi_channel(scarletbook_handle_t* handle) {
	return handle->mulch_area_idx != -1;
}

static inline int has_both_channels(scarletbook_handle_t* handle) {
	return handle->twoch_area_idx != -1 && handle->mulch_area_idx != -1;
}

static inline area_toc_t* get_two_channel(scarletbook_handle_t* handle) {
	return handle->twoch_area_idx == -1 ? 0 : handle->area[handle->twoch_area_idx].area_toc;
}

static inline area_toc_t* get_multi_channel(scarletbook_handle_t* handle) {
	return handle->mulch_area_idx == -1 ? 0 : handle->area[handle->mulch_area_idx].area_toc;
}

typedef struct {
	uint32_t    id;
	const char* name;
} codepage_id_t;

static codepage_id_t codepage_ids[] = {
	{CP_ACP, character_set[0]},
	{20127,  character_set[1]},
	{28591,  character_set[2]},
	{932,    character_set[3]},
	{949,    character_set[4]},
	{936,    character_set[5]},
	{950,    character_set[6]},
	{28591,  character_set[7]},
};

static inline string charset_convert(const char* instring, size_t insize, uint8_t codepage_index) {
	string utf8_str;
	uint32_t codepage_id = CP_ACP;
	codepage_index = 0;
	if (codepage_index < sizeof(codepage_ids) / sizeof(*codepage_ids)) {
		codepage_id = codepage_ids[codepage_index].id;
	}
	WCHAR wcs_data[2047 + 1];
	int wcs_size = MultiByteToWideChar(codepage_id, 0, instring, insize, wcs_data, 2047);
	if (wcs_size > 0) {
		wcs_data[wcs_size] = 0;
		pfc::stringcvt::string_utf8_from_wide utf8_data(wcs_data);
		utf8_str = utf8_data;
	}
	else {
		utf8_str = instring;
	}
	return utf8_str;
}

static inline int get_channel_count(audio_frame_info_t* frame_info) {
  if (frame_info->channel_bit_2 == 1 && frame_info->channel_bit_3 == 0) {
		return 6;
  }
  else if (frame_info->channel_bit_2 == 0 && frame_info->channel_bit_3 == 1) {
		return 5;
  }
  else {
		return 2;
  }
}

bool sacd_disc_t::g_is_sacd(const char* p_path) {
	try {
		abort_callback_impl abort;
		service_ptr_t<file> file;
		char sacdmtoc[8];
		filesystem::g_open_read(file, p_path, abort);
		file->seek(START_OF_MASTER_TOC * SACD_LSN_SIZE, abort);
		if (file->read(sacdmtoc, 8, abort) == 8) {
			if (memcmp(sacdmtoc, "SACDMTOC", 8) == 0) {
				return true;
			}
		}
		file->seek(START_OF_MASTER_TOC * SACD_PSN_SIZE + 12, abort);
		if (file->read(sacdmtoc, 8, abort) == 8) {
			if (memcmp(sacdmtoc, "SACDMTOC", 8) == 0) {
				return true;
			}
		}
	}
	catch (...) {
	}
	return false;
}

bool sacd_disc_t::g_is_sacd(const char p_drive) {
	TCHAR device[8];
	_tcscpy_s(device, 8, _T("?:\\"));
	device[0] = (TCHAR)p_drive;
	UINT uType = ::GetDriveType(device);
	if (uType != DRIVE_CDROM) {
		return false;
	}
	__try {
		_tcscpy_s(device, 8, _T("\\\\.\\?:"));
		device[4] = (TCHAR)p_drive;
		HANDLE hDevice;
		hDevice = ::CreateFile(device, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
		if (hDevice == INVALID_HANDLE_VALUE) {
			hDevice = ::CreateFile(device, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
		}
		if (hDevice != INVALID_HANDLE_VALUE) {
			if (::SetFilePointer(hDevice, START_OF_MASTER_TOC * SACD_LSN_SIZE, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
				char sacdmtoc[SACD_LSN_SIZE];
				DWORD cbRead;
				if (::ReadFile(hDevice, sacdmtoc, SACD_LSN_SIZE, &cbRead, NULL)) { 
					if (memcmp(sacdmtoc, "SACDMTOC", 8) == 0) {
						return true;
					}
				}
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
	return false;
}

sacd_disc_t::sacd_disc_t() {
	m_audio_sector.header.dst_encoded = 0;
	m_emaster = false;
	m_dst_encoded = false;
	m_sector_bad_reads = 0;
	m_file = nullptr;
	m_sb.master_data = nullptr;
	m_sb.area[0].area_data = nullptr;
	m_sb.area[1].area_data = nullptr;
	m_sb.area_count = 0;
	m_sb.twoch_area_idx = -1;
	m_sb.mulch_area_idx = -1;
	m_sector_size = 0;
}

sacd_disc_t::~sacd_disc_t() {
	close();
}

sacd_media_t* sacd_disc_t::get_media() {
	return m_file;
}

scarletbook_handle_t* sacd_disc_t::get_handle() {
	return &m_sb;
}

scarletbook_area_t* sacd_disc_t::get_area(area_id_e area_id) {
	switch (area_id) {
	case AREA_TWOCH:
		if (m_sb.twoch_area_idx != -1)
			return &m_sb.area[m_sb.twoch_area_idx];
		break;
	case AREA_MULCH:
		if (m_sb.mulch_area_idx != -1)
			return &m_sb.area[m_sb.mulch_area_idx];
		break;
	}
	return 0;
}

uint32_t sacd_disc_t::get_track_count(area_id_e area_id) {
	scarletbook_area_t* area = get_area(area_id);
	if (area) {
		return get_area(area_id)->area_toc->track_count;
	}
	return 0;
}

area_id_e sacd_disc_t::get_track_area_id() {
	return m_track_area;
}

uint8_t sacd_disc_t::get_track_number() {
	return m_track_number;
}

uint32_t sacd_disc_t::get_track_length_lsn() {
	return m_track_length_lsn;
}

int sacd_disc_t::get_channels() {
	return get_area(m_track_area) ? get_area(m_track_area)->area_toc->channel_count : 0;
}

int sacd_disc_t::get_loudspeaker_config() {
	return get_area(m_track_area) ? get_area(m_track_area)->area_toc->loudspeaker_config : 0;
}

int sacd_disc_t::get_samplerate() {
	return SACD_SAMPLING_FREQUENCY;
}

int sacd_disc_t::get_framerate() {
	return 75;
}

uint64_t sacd_disc_t::get_size() {
	return (uint64_t)m_track_length_lsn * (uint64_t)m_sector_size;
}

uint64_t sacd_disc_t::get_offset() {
	return (uint64_t)m_track_current_lsn * (uint64_t)m_sector_size;
}

double sacd_disc_t::get_duration() {
	if (get_area(m_track_area)) {
		area_tracklist_time_duration_t duration = get_area(m_track_area)->area_tracklist_time->duration[m_track_number];
		return duration.minutes * 60.0 + duration.seconds * 1.0 + duration.frames / 75.0;
	}
	return 0.0;
}

double sacd_disc_t::get_duration(uint32_t subsong) {
	if (get_area(m_track_area)) {
		area_tracklist_time_duration_t duration = get_area(m_track_area)->area_tracklist_time->duration[subsong];
		return duration.minutes * 60.0 + duration.seconds * 1.0 + duration.frames / 75.0;
	}
	return 0.0;
}

void sacd_disc_t::get_info(t_uint32 subsong, file_info& info) {
	scarletbook_area_t* area = 0;
	uint8_t twoch_count = get_track_count(AREA_TWOCH);
	if (subsong < twoch_count) {
		area = get_area(AREA_TWOCH);
	}
	else {
		subsong -= twoch_count;
		if (subsong < get_track_count(AREA_MULCH)) {
			area = get_area(AREA_MULCH);
		}
	}
	if (!area) {
		return;
	}
	pfc::string_formatter(track);
	track << (subsong + 1);
	info.meta_set("tracknumber", track);
	if (area->area_toc->track_count > 0) {
		pfc::string_formatter(t);
		t << area->area_toc->track_count;
		info.meta_set("totaltracks", t);
	}
	if (get_handle()->master_toc->album_set_size > 1) {
		if (get_handle()->master_toc->album_sequence_number > 0) {
			pfc::string_formatter(t);
			t << get_handle()->master_toc->album_sequence_number;
			info.meta_set("discnumber", t);
		}
		pfc::string_formatter(t);
		t << get_handle()->master_toc->album_set_size;
		info.meta_set("totaldiscs", t);
	}
	scarletbook_handle_t* sb = get_handle();
	if (sb->master_toc->disc_date_year > 0) {
		pfc::string_formatter(t);
		t << sb->master_toc->disc_date_year;
		/*
		if (sb->master_toc->disc_date_month > 0) {
			t << "-";
			if (sb->master_toc->disc_date_month < 10) {
				t << "0";
			}
			t << sb->master_toc->disc_date_month;
			if (sb->master_toc->disc_date_day > 0) {
				t << "-";
				if (sb->master_toc->disc_date_day < 10) {
					t << "0";
				}
				t << sb->master_toc->disc_date_day;
			}
		}
		*/
		info.meta_set("date", t);
	}
 
	if (!sb->master_text.album_title.empty())
		info.meta_set("album", sb->master_text.album_title.c_str());
	if (!sb->master_text.album_artist.empty())
		info.meta_set("artist", sb->master_text.album_artist.c_str());
	if (!area->area_track_text[subsong].track_type_title.empty())
		info.meta_set("title", area->area_track_text[subsong].track_type_title.c_str());
	if (!area->area_track_text[subsong].track_type_composer.empty())
		info.meta_set("composer", area->area_track_text[subsong].track_type_composer.c_str());
	if (!area->area_track_text[subsong].track_type_performer.empty())
		info.meta_set("performer", area->area_track_text[subsong].track_type_performer.c_str());
	if (!area->area_track_text[subsong].track_type_message.empty())
		info.meta_set("comment", area->area_track_text[subsong].track_type_message.c_str());
	if (area->area_isrc_genre) {
		if (area->area_isrc_genre->track_genre[subsong].category == 1) {
			uint8_t genre = area->area_isrc_genre->track_genre[subsong].genre;
			if (genre > 0) {
				info.meta_set("genre", album_genre[genre]);
			}
		}
	}
}

void sacd_disc_t::set_info(t_uint32 subsong, const file_info& info) {
}

bool sacd_disc_t::commit() {
	return true;
}

bool sacd_disc_t::is_dst() {
	return m_dst_encoded;
}

bool sacd_disc_t::open(sacd_media_t* p_file, uint32_t mode) {
	m_file = p_file;
	m_sb.master_data = nullptr;
	m_sb.area[0].area_data = nullptr;
	m_sb.area[1].area_data = nullptr;
	m_sb.area_count = 0;
	m_sb.twoch_area_idx = -1;
	m_sb.mulch_area_idx = -1;
	char sacdmtoc[8];
	m_sector_size = 0;
	m_sector_bad_reads = 0;
	m_file->seek((uint64_t)START_OF_MASTER_TOC * (uint64_t)SACD_LSN_SIZE);
	if (m_file->read(sacdmtoc, 8) == 8) {
		if (memcmp(sacdmtoc, "SACDMTOC", 8) == 0) {
			m_sector_size = SACD_LSN_SIZE;
			m_buffer = m_sector_buffer;
		}
	}
	if (!m_file->seek((uint64_t)START_OF_MASTER_TOC * (uint64_t)SACD_PSN_SIZE + 12)) {
		close();
		return false;
	}
	if (m_file->read(sacdmtoc, 8) == 8) {
		if (memcmp(sacdmtoc, "SACDMTOC", 8) == 0) {
			m_sector_size = SACD_PSN_SIZE;
			m_buffer = m_sector_buffer + 12;
		}
	}
	if (!m_file->seek(0)) {
		close();
		return false;
	}
	if (m_sector_size == 0) {
		close();
		return false;
	}
	if (!read_master_toc()) {
		close();
		return false;
	}
	if (m_sb.master_toc->area_1_toc_1_start) {
		m_sb.area[m_sb.area_count].area_data = (uint8_t*)malloc(m_sb.master_toc->area_1_toc_size * SACD_LSN_SIZE);
		if (!m_sb.area[m_sb.area_count].area_data) {
			close();
			return false;
		}
		if (!read_blocks_raw(m_sb.master_toc->area_1_toc_1_start, m_sb.master_toc->area_1_toc_size, m_sb.area[m_sb.area_count].area_data)) {
			m_sb.master_toc->area_1_toc_1_start = 0;
		}
		else {
			if (read_area_toc(m_sb.area_count)) {
				m_sb.area_count++;
			}
		}
	}
	if (m_sb.master_toc->area_2_toc_1_start) {
		m_sb.area[m_sb.area_count].area_data = (uint8_t*)malloc(m_sb.master_toc->area_2_toc_size * SACD_LSN_SIZE);
		if (!m_sb.area[m_sb.area_count].area_data) {
			close();
			return false;
		}
		if (!read_blocks_raw(m_sb.master_toc->area_2_toc_1_start, m_sb.master_toc->area_2_toc_size, m_sb.area[m_sb.area_count].area_data)) {
			m_sb.master_toc->area_2_toc_1_start = 0;
			return true;
		}
		if (read_area_toc(m_sb.area_count)) {
			m_sb.area_count++;
		}
	}
	return true;
}

bool sacd_disc_t::close() {
	if (has_two_channel(&m_sb)) {
		free_area(&m_sb.area[m_sb.twoch_area_idx]);
		free(m_sb.area[m_sb.twoch_area_idx].area_data);
		m_sb.area[m_sb.twoch_area_idx].area_data = nullptr;
		m_sb.twoch_area_idx = -1;
	}
	if (has_multi_channel(&m_sb))	{
		free_area(&m_sb.area[m_sb.mulch_area_idx]);
		free(m_sb.area[m_sb.mulch_area_idx].area_data);
		m_sb.area[m_sb.mulch_area_idx].area_data = nullptr;
		m_sb.mulch_area_idx = -1;
	}
	m_sb.area_count = 0;
	master_text_t* mt = &m_sb.master_text;
	mt->album_title.clear();
	mt->album_title_phonetic.clear();
	mt->album_artist.clear();
	mt->album_artist_phonetic.clear();
	mt->album_publisher.clear();
	mt->album_publisher_phonetic.clear();
	mt->album_copyright.clear();
	mt->album_copyright_phonetic.clear();
	mt->disc_title.clear();
	mt->disc_title_phonetic.clear();
	mt->disc_artist.clear();
	mt->disc_artist_phonetic.clear();
	mt->disc_publisher.clear();
	mt->disc_publisher_phonetic.clear();
	mt->disc_copyright.clear();
	mt->disc_copyright_phonetic.clear();
	if (m_sb.master_data) {
		free(m_sb.master_data);
		m_sb.master_data = nullptr;
	}
	return true;
}

void sacd_disc_t::set_emaster(bool emaster) {
	m_emaster = emaster;
}

void sacd_disc_t::set_area(area_id_e area_id) {
	m_track_area = area_id;
	m_dst_encoded = get_area(m_track_area) ? get_area(m_track_area)->area_toc->frame_format == FRAME_FORMAT_DST : false;
}

bool sacd_disc_t::set_track(uint32_t track_number, area_id_e area_id, uint32_t offset) {
	if (track_number < get_track_count(area_id)) {
		scarletbook_area_t* area = get_area(area_id);
		m_track_number = track_number;
		m_track_area = area_id;
		if (!m_emaster) {
			m_track_start_lsn = area->area_tracklist_offset->track_start_lsn[track_number];
			m_track_length_lsn = area->area_tracklist_offset->track_length_lsn[track_number];
		}
		else {
			if (track_number > 0) {
				m_track_start_lsn = area->area_tracklist_offset->track_start_lsn[track_number];
			}
			else {
				m_track_start_lsn = area->area_toc->track_start;
			}
			if (track_number < get_track_count(area_id) - 1) {
				m_track_length_lsn = area->area_tracklist_offset->track_start_lsn[track_number + 1] - m_track_start_lsn + 1;
			}
			else {
				m_track_length_lsn = area->area_toc->track_end - m_track_start_lsn;
			}
		}
		m_track_current_lsn = m_track_start_lsn + offset;
		m_channel_count = area->area_toc->channel_count;
		memset(&m_audio_sector, 0, sizeof(m_audio_sector));
		memset(&m_frame, 0, sizeof(m_frame));
		m_packet_info_idx = 0;
		m_file->seek((uint64_t)m_track_current_lsn * (uint64_t)m_sector_size);
		return true;
	}
	m_track_area = AREA_BOTH;
	return false;
}

bool sacd_disc_t::read_frame(uint8_t* frame_data, size_t* frame_size, frame_type_e* frame_type) {
	m_sector_bad_reads = 0;
	while (m_track_current_lsn < m_track_start_lsn + m_track_length_lsn) {
		if (m_sector_bad_reads > 0) {
			m_buffer_offset = 0;
			m_packet_info_idx = 0;
			memset(&m_audio_sector, 0, sizeof(m_audio_sector));
			memset(&m_frame, 0, sizeof(m_frame));
			*frame_type = FRAME_INVALID;
			return true;
		}
		if (m_packet_info_idx == m_audio_sector.header.packet_info_count) {
			// obtain the next sector data block
			m_buffer_offset = 0;
			m_packet_info_idx = 0;
			t_size read_bytes = m_file->read(m_sector_buffer, m_sector_size);
			m_track_current_lsn++;
			if (read_bytes != m_sector_size) {
				m_sector_bad_reads++;
				continue;
			}
			memcpy(&m_audio_sector.header, m_buffer + m_buffer_offset, AUDIO_SECTOR_HEADER_SIZE);
			m_buffer_offset += AUDIO_SECTOR_HEADER_SIZE;
			for (uint8_t i = 0; i < m_audio_sector.header.packet_info_count; i++) {
				m_audio_sector.packet[i].frame_start = ((m_buffer + m_buffer_offset)[0] >> 7) & 1;
				m_audio_sector.packet[i].data_type = ((m_buffer + m_buffer_offset)[0] >> 3) & 7;
				m_audio_sector.packet[i].packet_length = ((m_buffer + m_buffer_offset)[0] & 7) << 8 | (m_buffer + m_buffer_offset)[1];
				m_buffer_offset += AUDIO_PACKET_INFO_SIZE;
			}
			if (m_audio_sector.header.dst_encoded) {
				memcpy(m_audio_sector.frame, m_buffer + m_buffer_offset, AUDIO_FRAME_INFO_SIZE * m_audio_sector.header.frame_info_count);
				m_buffer_offset += AUDIO_FRAME_INFO_SIZE * m_audio_sector.header.frame_info_count;
			}
			else {
				for (uint8_t i = 0; i < m_audio_sector.header.frame_info_count; i++) {
					memcpy(&m_audio_sector.frame[i], m_buffer + m_buffer_offset, AUDIO_FRAME_INFO_SIZE - 1);
					m_buffer_offset += AUDIO_FRAME_INFO_SIZE - 1;
				}
			}
		}
		while (m_packet_info_idx < m_audio_sector.header.packet_info_count && m_sector_bad_reads == 0) {
			audio_packet_info_t* packet = &m_audio_sector.packet[m_packet_info_idx];
			switch (packet->data_type) {
			case DATA_TYPE_AUDIO:
				if (m_frame.started) {
					if (packet->frame_start) {
						if ((size_t)m_frame.size <= *frame_size) {
							memcpy(frame_data, m_frame.data, m_frame.size);
							*frame_size = m_frame.size;
						}
						else {
							m_sector_bad_reads++;
							continue;
						}
						*frame_type = m_sector_bad_reads > 0 ? FRAME_INVALID : m_frame.dst_encoded ? FRAME_DST : FRAME_DSD;
						m_frame.started = false;
						return true;
					}
				}
				else {
					if (packet->frame_start) {
						m_frame.size = 0;
						m_frame.dst_encoded = m_audio_sector.header.dst_encoded;
						m_frame.started = true;
					}
				}
				if (m_frame.started) {
					if ((size_t)m_frame.size + packet->packet_length <= *frame_size && m_buffer_offset + packet->packet_length <= SACD_LSN_SIZE) {
						memcpy(m_frame.data + m_frame.size, m_buffer + m_buffer_offset, packet->packet_length);
						m_frame.size += packet->packet_length;
					}
					else {
						m_sector_bad_reads++;
						continue;
					}
				}
				break;
			case DATA_TYPE_SUPPLEMENTARY:
			case DATA_TYPE_PADDING:
				break;
			default:
				break;
			}
			m_buffer_offset += packet->packet_length;
			m_packet_info_idx++;
		}
	}
	if (m_frame.started) {
		if ((size_t)m_frame.size <= *frame_size) {
			memcpy(frame_data, m_frame.data, m_frame.size);
			*frame_size = m_frame.size;
		}
		else {
			m_sector_bad_reads++;
			m_buffer_offset = 0;
			m_packet_info_idx = 0;
			memset(&m_audio_sector, 0, sizeof(m_audio_sector));
			memset(&m_frame, 0, sizeof(m_frame));
		}
		m_frame.started = false;
		*frame_type = m_sector_bad_reads > 0 ? FRAME_INVALID : m_frame.dst_encoded ? FRAME_DST : FRAME_DSD;
		return true;
	}
	*frame_type = FRAME_INVALID;
	return false;
}

bool sacd_disc_t::seek(double seconds) {
	uint64_t offset = (uint64_t)(get_size() * seconds / get_duration());
	return set_track(get_track_number(), get_track_area_id(), (uint32_t)(offset / m_sector_size));
}

bool sacd_disc_t::read_blocks_raw(uint32_t lb_start, size_t block_count, uint8_t* data) {
	switch (m_sector_size) {
	case SACD_LSN_SIZE: 
		m_file->seek((uint64_t)lb_start * (uint64_t)SACD_LSN_SIZE);
		if (m_file->read(data, block_count * SACD_LSN_SIZE) != block_count * SACD_LSN_SIZE) {
			m_sector_bad_reads++;
			return false;
		}
		break;
	case SACD_PSN_SIZE: 
		for (uint32_t i = 0; i < block_count; i++) {
			m_file->seek((uint64_t)(lb_start + i) * (uint64_t)SACD_PSN_SIZE + 12);
			if (m_file->read(data + i * SACD_LSN_SIZE, SACD_LSN_SIZE) != SACD_LSN_SIZE) {
				m_sector_bad_reads++;
				return false;
			}
		}
		break;
	}
	return true;
}

bool sacd_disc_t::read_master_toc() {
	uint8_t*      p;
	master_toc_t* master_toc;

	m_sb.master_data = (uint8_t*)malloc(MASTER_TOC_LEN * SACD_LSN_SIZE);
	if (!m_sb.master_data)
		return false;

	if (!read_blocks_raw(START_OF_MASTER_TOC, MASTER_TOC_LEN, m_sb.master_data))
		return false;

	master_toc = m_sb.master_toc = (master_toc_t*)m_sb.master_data;
	if (strncmp("SACDMTOC", master_toc->id, 8) != 0)
		return false;

	SWAP16(master_toc->album_set_size);
	SWAP16(master_toc->album_sequence_number);
	SWAP32(master_toc->area_1_toc_1_start);
	SWAP32(master_toc->area_1_toc_2_start);
	SWAP16(master_toc->area_1_toc_size);
	SWAP32(master_toc->area_2_toc_1_start);
	SWAP32(master_toc->area_2_toc_2_start);
	SWAP16(master_toc->area_2_toc_size);
	SWAP16(master_toc->disc_date_year);

	if (master_toc->version.major > SUPPORTED_VERSION_MAJOR || master_toc->version.minor > SUPPORTED_VERSION_MINOR)
		return false;

	// point to eof master header
	p = m_sb.master_data + SACD_LSN_SIZE;

	// set pointers to text content
	for (int i = 0; i < MAX_LANGUAGE_COUNT; i++) {
		master_sacd_text_t* master_text = (master_sacd_text_t*)p;

		if (strncmp("SACDText", master_text->id, 8) != 0)
			return false;

		SWAP16(master_text->album_title_position);
		SWAP16(master_text->album_artist_position);
		SWAP16(master_text->album_publisher_position);
		SWAP16(master_text->album_copyright_position);
		SWAP16(master_text->album_title_phonetic_position);
		SWAP16(master_text->album_artist_phonetic_position);
		SWAP16(master_text->album_publisher_phonetic_position);
		SWAP16(master_text->album_copyright_phonetic_position);
		SWAP16(master_text->disc_title_position);
		SWAP16(master_text->disc_artist_position);
		SWAP16(master_text->disc_publisher_position);
		SWAP16(master_text->disc_copyright_position);
		SWAP16(master_text->disc_title_phonetic_position);
		SWAP16(master_text->disc_artist_phonetic_position);
		SWAP16(master_text->disc_publisher_phonetic_position);
		SWAP16(master_text->disc_copyright_phonetic_position);

		// we only use the first SACDText entry
		if (i == 0) {
			uint8_t current_charset = m_sb.master_toc->locales[i].character_set & 0x07;

			if (master_text->album_title_position)
				m_sb.master_text.album_title = charset_convert((char*)master_text + master_text->album_title_position, strlen((char*)master_text + master_text->album_title_position), current_charset);
			if (master_text->album_title_phonetic_position)
				m_sb.master_text.album_title_phonetic = charset_convert((char*)master_text + master_text->album_title_phonetic_position, strlen((char*)master_text + master_text->album_title_phonetic_position), current_charset);
			if (master_text->album_artist_position)
				m_sb.master_text.album_artist = charset_convert((char*)master_text + master_text->album_artist_position, strlen((char*)master_text + master_text->album_artist_position), current_charset);
			if (master_text->album_artist_phonetic_position)
				m_sb.master_text.album_artist_phonetic = charset_convert((char*)master_text + master_text->album_artist_phonetic_position, strlen((char*)master_text + master_text->album_artist_phonetic_position), current_charset);
			if (master_text->album_publisher_position)
				m_sb.master_text.album_publisher = charset_convert((char*)master_text + master_text->album_publisher_position, strlen((char*)master_text + master_text->album_publisher_position), current_charset);
			if (master_text->album_publisher_phonetic_position)
				m_sb.master_text.album_publisher_phonetic = charset_convert((char*)master_text + master_text->album_publisher_phonetic_position, strlen((char*)master_text + master_text->album_publisher_phonetic_position), current_charset);
			if (master_text->album_copyright_position)
				m_sb.master_text.album_copyright = charset_convert((char*)master_text + master_text->album_copyright_position, strlen((char*)master_text + master_text->album_copyright_position), current_charset);
			if (master_text->album_copyright_phonetic_position)
				m_sb.master_text.album_copyright_phonetic = charset_convert((char*)master_text + master_text->album_copyright_phonetic_position, strlen((char*)master_text + master_text->album_copyright_phonetic_position), current_charset);

			if (master_text->disc_title_position)
				m_sb.master_text.disc_title = charset_convert((char*)master_text + master_text->disc_title_position, strlen((char*)master_text + master_text->disc_title_position), current_charset);
			if (master_text->disc_title_phonetic_position)
				m_sb.master_text.disc_title_phonetic = charset_convert((char*)master_text + master_text->disc_title_phonetic_position, strlen((char*)master_text + master_text->disc_title_phonetic_position), current_charset);
			if (master_text->disc_artist_position)
				m_sb.master_text.disc_artist = charset_convert((char*)master_text + master_text->disc_artist_position, strlen((char*)master_text + master_text->disc_artist_position), current_charset);
			if (master_text->disc_artist_phonetic_position)
				m_sb.master_text.disc_artist_phonetic = charset_convert((char*)master_text + master_text->disc_artist_phonetic_position, strlen((char*)master_text + master_text->disc_artist_phonetic_position), current_charset);
			if (master_text->disc_publisher_position)
				m_sb.master_text.disc_publisher = charset_convert((char*)master_text + master_text->disc_publisher_position, strlen((char*)master_text + master_text->disc_publisher_position), current_charset);
			if (master_text->disc_publisher_phonetic_position)
				m_sb.master_text.disc_publisher_phonetic = charset_convert((char*)master_text + master_text->disc_publisher_phonetic_position, strlen((char*)master_text + master_text->disc_publisher_phonetic_position), current_charset);
			if (master_text->disc_copyright_position)
				m_sb.master_text.disc_copyright = charset_convert((char*)master_text + master_text->disc_copyright_position, strlen((char*)master_text + master_text->disc_copyright_position), current_charset);
			if (master_text->disc_copyright_phonetic_position)
				m_sb.master_text.disc_copyright_phonetic = charset_convert((char*)master_text + master_text->disc_copyright_phonetic_position, strlen((char*)master_text + master_text->disc_copyright_phonetic_position), current_charset);
		}
		p += SACD_LSN_SIZE;
	}

	m_sb.master_man = (master_man_t*)p;
	if (strncmp("SACD_Man", m_sb.master_man->id, 8) != 0)
		return false;
	return true;
}

bool sacd_disc_t::read_area_toc(int area_idx) {
	area_toc_t*         area_toc;
	uint8_t*            area_data;
	uint8_t*            p;
	int                 sacd_text_idx = 0;
	scarletbook_area_t* area = &m_sb.area[area_idx];
	uint8_t             current_charset;

	p = area_data = area->area_data;
	area_toc = area->area_toc = (area_toc_t*)area_data;

	if (strncmp("TWOCHTOC", area_toc->id, 8) != 0 && strncmp("MULCHTOC", area_toc->id, 8) != 0)
		return false;

	SWAP16(area_toc->size);
	SWAP32(area_toc->track_start);
	SWAP32(area_toc->track_end);
	SWAP16(area_toc->area_description_offset);
	SWAP16(area_toc->copyright_offset);
	SWAP16(area_toc->area_description_phonetic_offset);
	SWAP16(area_toc->copyright_phonetic_offset);
	SWAP32(area_toc->max_byte_rate);
	SWAP16(area_toc->track_text_offset);
	SWAP16(area_toc->index_list_offset);
	SWAP16(area_toc->access_list_offset);

	current_charset = area->area_toc->languages[sacd_text_idx].character_set & 0x07;

	if (area_toc->copyright_offset)
		area->copyright = charset_convert((char*)area_toc + area_toc->copyright_offset, strlen((char*)area_toc + area_toc->copyright_offset), current_charset);
	if (area_toc->copyright_phonetic_offset)
		area->copyright_phonetic = charset_convert((char*)area_toc + area_toc->copyright_phonetic_offset, strlen((char*)area_toc + area_toc->copyright_phonetic_offset), current_charset);
	if (area_toc->area_description_offset)
		area->description = charset_convert((char*)area_toc + area_toc->area_description_offset, strlen((char*)area_toc + area_toc->area_description_offset), current_charset);
	if (area_toc->area_description_phonetic_offset)
		area->description_phonetic = charset_convert((char*)area_toc + area_toc->area_description_phonetic_offset, strlen((char*)area_toc + area_toc->area_description_phonetic_offset), current_charset);

	if (area_toc->version.major > SUPPORTED_VERSION_MAJOR || area_toc->version.minor > SUPPORTED_VERSION_MINOR)
		return false;
	
	// is this the 2 channel?
	if (area_toc->channel_count == 2 && area_toc->loudspeaker_config == 0)
		m_sb.twoch_area_idx = area_idx;
	else
		m_sb.mulch_area_idx = area_idx;

	// Area TOC size is SACD_LSN_SIZE
	p += SACD_LSN_SIZE;

	while (p < (area_data + area_toc->size * SACD_LSN_SIZE)) {
		if (strncmp((char*)p, "SACDTTxt", 8) == 0) {
			// we discard all other SACDTTxt entries
			if (sacd_text_idx == 0) {
				for (uint8_t i = 0; i < area_toc->track_count; i++) {
					area_text_t* area_text;
					uint8_t      track_type, track_amount;
					char*        track_ptr;
					area_text = area->area_text = (area_text_t*)p;
					SWAP16(area_text->track_text_position[i]);
					if (area_text->track_text_position[i] > 0) {
						track_ptr = (char*)(p + area_text->track_text_position[i]);
						track_amount = *track_ptr;
						track_ptr += 4;
						for (uint8_t j = 0; j < track_amount; j++) {
							track_type = *track_ptr;
							track_ptr++;
							track_ptr++;                         // skip unknown 0x20
							if (*track_ptr != 0) {
								switch (track_type) {
								case TRACK_TYPE_TITLE:
									area->area_track_text[i].track_type_title = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_PERFORMER:
									area->area_track_text[i].track_type_performer = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_SONGWRITER:
									area->area_track_text[i].track_type_songwriter = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_COMPOSER:
									area->area_track_text[i].track_type_composer = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_ARRANGER:
									area->area_track_text[i].track_type_arranger = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_MESSAGE:
									area->area_track_text[i].track_type_message = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_EXTRA_MESSAGE:
									area->area_track_text[i].track_type_extra_message = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_TITLE_PHONETIC:
									area->area_track_text[i].track_type_title_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_PERFORMER_PHONETIC:
									area->area_track_text[i].track_type_performer_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_SONGWRITER_PHONETIC:
									area->area_track_text[i].track_type_songwriter_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_COMPOSER_PHONETIC:
									area->area_track_text[i].track_type_composer_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_ARRANGER_PHONETIC:
										area->area_track_text[i].track_type_arranger_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
										break;
								case TRACK_TYPE_MESSAGE_PHONETIC:
									area->area_track_text[i].track_type_message_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_EXTRA_MESSAGE_PHONETIC:
									area->area_track_text[i].track_type_extra_message_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								}
							}
							if (j < track_amount - 1) {
								while (*track_ptr != 0)
									track_ptr++;
								while (*track_ptr == 0)
									track_ptr++;
							}
						}
					}
				}
			}
			sacd_text_idx++;
			p += SACD_LSN_SIZE;
		}
		else if (strncmp((char*)p, "SACD_IGL", 8) == 0) {
			area->area_isrc_genre = (area_isrc_genre_t*)p;
			p += SACD_LSN_SIZE * 2;
		}
		else if (strncmp((char*)p, "SACD_ACC", 8) == 0) {
			// skip
			p += SACD_LSN_SIZE * 32;
		}
		else if (strncmp((char*)p, "SACDTRL1", 8) == 0) {
			area_tracklist_offset_t* tracklist;
			tracklist = area->area_tracklist_offset = (area_tracklist_offset_t*)p;
			for (uint8_t i = 0; i < area_toc->track_count; i++) {
				SWAP32(tracklist->track_start_lsn[i]);
				SWAP32(tracklist->track_length_lsn[i]);
			}
			p += SACD_LSN_SIZE;
		}
		else if (strncmp((char*)p, "SACDTRL2", 8) == 0) {
			area_tracklist_time_t* tracklist;
			tracklist = area->area_tracklist_time = (area_tracklist_time_t*)p;
			p += SACD_LSN_SIZE;
		}
		else {
			break;
		}
	}
	return true;
}

void sacd_disc_t::free_area(scarletbook_area_t* area) {
	for (uint8_t i = 0; i < area->area_toc->track_count; i++) {
		area->area_track_text[i].track_type_title.clear();
		area->area_track_text[i].track_type_performer.clear();
		area->area_track_text[i].track_type_songwriter.clear();
		area->area_track_text[i].track_type_composer.clear();
		area->area_track_text[i].track_type_arranger.clear();
		area->area_track_text[i].track_type_message.clear();
		area->area_track_text[i].track_type_extra_message.clear();
		area->area_track_text[i].track_type_title_phonetic.clear();
		area->area_track_text[i].track_type_performer_phonetic.clear();
		area->area_track_text[i].track_type_songwriter_phonetic.clear();
		area->area_track_text[i].track_type_composer_phonetic.clear();
		area->area_track_text[i].track_type_arranger_phonetic.clear();
		area->area_track_text[i].track_type_message_phonetic.clear();
		area->area_track_text[i].track_type_extra_message_phonetic.clear();
	}
	area->description.clear();
	area->copyright.clear();
	area->description_phonetic.clear();
	area->copyright_phonetic.clear();
}
