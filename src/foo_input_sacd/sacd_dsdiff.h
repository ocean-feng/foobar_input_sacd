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

#ifndef _SACD_DSDIFF_H_INCLUDED
#define _SACD_DSDIFF_H_INCLUDED

#include <minmax.h>
#include <stdint.h>
#include <vector>
#include <foobar2000.h>
#include "endianess.h"
#include "scarletbook.h"
#include "sacd_reader.h"
#include "sacd_dsd.h"

using namespace std;

#pragma pack(1)

class FormDSDChunk : public Chunk {
public:
	ID formType;
};

class DSTFrameIndex {
public:
	uint64_t offset;
	uint32_t length;
};

enum MarkType {TrackStart = 0, TrackStop = 1, ProgramStart = 2, Index = 4};

class Marker {
public:
	uint16_t hours;
	uint8_t  minutes;
	uint8_t  seconds;
	uint32_t samples;
	int32_t  offset;
	uint16_t markType;
	uint16_t markChannel;
	uint16_t TrackFlags;
	uint32_t count;
};

#pragma pack()

class subsong_t {
public:
	double start_time;
	double stop_time;
};

class id3tags_t {
public:
	uint32_t index;
	uint64_t offset;
	uint64_t size;
	vector<uint8_t> data;
};

class sacd_dsdiff_t : public sacd_reader_t {
	sacd_media_t*       m_file;
	uint32_t            m_version;
	uint32_t            m_samplerate;
	uint16_t            m_channel_count;
	int                 m_loudspeaker_config;
	int                 m_dst_encoded;
	uint64_t            m_frm8_size;
	uint64_t            m_dsti_offset;
	uint64_t            m_dsti_size;
	uint64_t            m_data_offset;
	uint64_t            m_data_size;
	uint16_t            m_framerate;
	uint32_t            m_frame_size;
	uint32_t            m_frame_count;
	vector<subsong_t>   m_subsong;
	uint64_t            m_id3_offset;
	vector<id3tags_t>   m_id3tags;
	bool                m_id3tags_indexed;
	uint32_t            m_current_subsong;                
	uint64_t            m_current_offset;
	uint64_t            m_current_size;
public:
	sacd_dsdiff_t();
	~sacd_dsdiff_t();
	sacd_media_t* get_media();
	uint32_t get_track_count(area_id_e area_id = AREA_BOTH);
	int get_channels();
	int get_loudspeaker_config();
	int get_samplerate();
	int get_framerate();
	uint64_t get_size();
	uint64_t get_offset();
	double get_duration();
	double get_duration(uint32_t subsong);
	bool is_dst();
	bool open(sacd_media_t* p_file, uint32_t mode = 0);
	bool close();
	void set_area(area_id_e area_id);
	void set_emaster(bool emaster);
	bool set_track(uint32_t track_number, area_id_e area_id = AREA_BOTH, uint32_t offset = 0);
	bool read_frame(uint8_t* frame_data, size_t* frame_size, frame_type_e* frame_type);
	bool seek(double seconds);
	void get_info(uint32_t subsong, file_info& info);
	void set_info(uint32_t subsong, const file_info& info);
	bool commit();
	bool set_samplerate(sacd_media_t* p_file, uint32_t p_samplerate);
private:
	uint64_t get_dsti_for_frame(uint32_t frame_nr);
	void get_id3tags(uint32_t subsong, file_info& info);
	void set_id3tags(uint32_t subsong, const file_info& info);
	void index_id3tags(file_info& info);
	void write_id3tag(const void* data, uint32_t size);
};

#endif
