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

#ifndef _SACD_DISC_H_INCLUDED
#define _SACD_DISC_H_INCLUDED

#include <stdint.h>
#include <foobar2000.h>
#include "endianess.h"
#include "scarletbook.h"
#include "sacd_reader.h"

#define SACD_PSN_SIZE 2064
#define MAX_DST_SIZE (1024 * 64)

typedef struct {
	uint8_t data[MAX_DST_SIZE];
	int     size;
	int     complete;
	bool    started;
	int     sector_count;
	int     channel_count;
	int     dst_encoded;
} audio_frame_t;

class sacd_disc_t : public sacd_reader_t {
private:
	sacd_media_t*        m_file;
	scarletbook_handle_t m_sb;
	area_id_e            m_track_area;
	uint8_t              m_track_number;
	uint32_t             m_track_start_lsn;
	uint32_t             m_track_length_lsn;
	uint32_t             m_track_current_lsn;
	uint8_t              m_channel_count;
	bool                 m_emaster;
	bool                 m_dst_encoded;
	audio_sector_t       m_audio_sector;
	audio_frame_t        m_frame;
	int                  m_frame_info_counter;
	int                  m_packet_info_idx;
	uint8_t              m_sector_buffer[SACD_PSN_SIZE];
	uint32_t             m_sector_size;
	int                  m_sector_bad_reads;
	uint8_t*             m_buffer;
	int                  m_buffer_offset;
public:
	static bool g_is_sacd(const char* p_path);
	static bool g_is_sacd(const char p_drive);
	sacd_disc_t();
	~sacd_disc_t();
	sacd_media_t* get_media();
	scarletbook_handle_t* get_handle();
	scarletbook_area_t* get_area(area_id_e area_id);
	uint32_t get_track_count(area_id_e area_id = AREA_BOTH);
	area_id_e get_track_area_id();
	uint8_t get_track_number();
	int get_channels();
	int get_loudspeaker_config();
	int get_samplerate();
	int get_framerate();
	uint64_t get_size();
	uint64_t get_offset();
	double get_duration();
	double get_duration(uint32_t subsong);
	void get_info(uint32_t subsong, file_info& info);
	void set_info(uint32_t subsong, const file_info& info);
	bool commit();
	uint32_t get_track_length_lsn();
	bool is_dst();
	bool open(sacd_media_t* p_file, uint32_t mode = 0);
	bool close();
	void set_emaster(bool emaster);
	void set_area(area_id_e area_id);
	bool set_track(uint32_t track_number, area_id_e area_id = AREA_BOTH, uint32_t offset = 0);
	bool read_frame(uint8_t* frame_data, size_t* frame_size, frame_type_e* frame_type);
	bool seek(double seconds);
	bool read_blocks_raw(uint32_t lb_start, size_t block_count, uint8_t* data);
private:
	bool read_master_toc();
	bool read_area_toc(int area_idx);
	void free_area(scarletbook_area_t* area);
};

#endif
