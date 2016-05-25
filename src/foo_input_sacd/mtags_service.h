/*
* Copyright(c) 2011 - 2015 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
*
* This program is free software; you can redistribute it and / or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or(at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
*but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with FFmpeg; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110 - 1301 USA
*/

#ifndef _MTAGS_SERVICE_H_INCLUDED
#define _MTAGS_SERVICE_H_INCLUDED

#include <foobar2000.h>

class mtags_service_t {
	service_ptr_t<input_entry> mtags_service;
	bool mtags_service_ok;
	service_ptr_t<input_info_writer> mtags_writer;
public:
	static bool in_service();
	mtags_service_t(const char* p_path, abort_callback& p_abort);
	virtual ~mtags_service_t();
	void get_info(t_uint32 p_subsong, file_info& p_info, abort_callback& p_abort);
	void set_info(t_uint32 p_subsong, const file_info& p_info, abort_callback& p_abort);
	void commit(abort_callback& p_abort);
};

#endif
