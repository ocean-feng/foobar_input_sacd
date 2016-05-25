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

#include "mtags_service.h"

using namespace pfc;

__declspec(thread) static volatile long g_mtags_refs = 0;

bool mtags_service_t::in_service() {
	return g_mtags_refs > 0;
}

mtags_service_t::mtags_service_t(const char* p_path, abort_callback& p_abort) {
	const char* tags_path = nullptr;
	string_replace_extension tagfile_path(p_path, "tags");
	mtags_service_ok = input_entry::g_find_service_by_path(mtags_service, "!.tags");
	if (mtags_service_ok) {
		if (g_mtags_refs == 0) {
			InterlockedIncrement(&g_mtags_refs);
			mtags_service->open_for_info_write(mtags_writer, nullptr, tagfile_path, p_abort);
			InterlockedDecrement(&g_mtags_refs);
		}
	}
}

mtags_service_t::~mtags_service_t() {
}

void mtags_service_t::get_info(t_uint32 p_subsong, file_info& p_info, abort_callback& p_abort) {
	if (mtags_service_ok) {
		if (g_mtags_refs == 0) {
			InterlockedIncrement(&g_mtags_refs);
			mtags_writer->get_info(p_subsong + 1, p_info, p_abort);
			InterlockedDecrement(&g_mtags_refs);
		}
	}
}

void mtags_service_t::set_info(t_uint32 p_subsong, const file_info& p_info, abort_callback& p_abort) {
	if (mtags_service_ok) {
		if (g_mtags_refs == 0) {
			InterlockedIncrement(&g_mtags_refs);
			mtags_writer->set_info(p_subsong + 1, p_info, p_abort);
			InterlockedDecrement(&g_mtags_refs);
		}
	}
}

void mtags_service_t::commit(abort_callback& p_abort) {
	if (mtags_service_ok) {
		if (g_mtags_refs == 0) {
			InterlockedIncrement(&g_mtags_refs);
			mtags_writer->commit(p_abort);
			InterlockedDecrement(&g_mtags_refs);
		}
	}
}
