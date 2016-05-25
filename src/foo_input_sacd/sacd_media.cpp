/*
* SACD Decoder plugin
* Copyright (c) 2011-2013 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
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

#include "scarletbook.h"
#include "sacd_media.h"

sacd_media_disc_t::sacd_media_disc_t(abort_callback& p_abort) : sacd_media_t(p_abort) {
	media_disc    = INVALID_HANDLE_VALUE;
	file_position = -1;
}

sacd_media_disc_t::~sacd_media_disc_t() {
	close();
}

bool sacd_media_disc_t::open(service_ptr_t<file> filehint, const char* path, t_input_open_reason reason) {
	TCHAR device[8];
	_tcscpy_s(device, 8, _T("\\\\.\\?:"));
	device[4] = path[7];
	media_disc = ::CreateFile(device, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
	if (media_disc == INVALID_HANDLE_VALUE) {
		media_disc = ::CreateFile(device, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
	}
	if (media_disc != INVALID_HANDLE_VALUE) {
		file_position = 0;
		return true;
	}
	return false;
}

bool sacd_media_disc_t::close() {
	if (media_disc == INVALID_HANDLE_VALUE) {
		return true;
	}
	BOOL hr = CloseHandle(media_disc);
	media_disc    = INVALID_HANDLE_VALUE;
	file_position = -1;
	return hr == TRUE;
}

bool sacd_media_disc_t::can_seek() {
	return true;
}

bool sacd_media_disc_t::seek(int64_t position, file::t_seek_mode mode) {
	switch (mode) {
	case file::seek_from_beginning:
		file_position = position;
		break;
	case file::seek_from_current:
		file_position += position;
		break;
	case file::seek_from_eof:
		file_position = get_size() - position;
		break;
	}
	return true;
}

int64_t sacd_media_disc_t::get_position() {
	return file_position;
}

int64_t sacd_media_disc_t::get_size() {
	LARGE_INTEGER liFileSize;
	liFileSize.QuadPart = 0;
	::GetFileSizeEx(media_disc, &liFileSize);
	return liFileSize.QuadPart;
}

t_filestats sacd_media_disc_t::get_stats() {
	t_filestats filestats;
	::GetFileTime(media_disc, NULL, NULL, (LPFILETIME)&filestats.m_timestamp);
	filestats.m_size = get_size();
	return filestats;
}

size_t sacd_media_disc_t::read(void* data, size_t size) {
	int64_t read_start = file_position & (int64_t)(~(SACD_LSN_SIZE - 1));
	size_t read_length = (size_t)((file_position + size - read_start + SACD_LSN_SIZE - 1) / SACD_LSN_SIZE) * SACD_LSN_SIZE;
	size_t start_offset = file_position & (SACD_LSN_SIZE - 1);
	size_t end_offset = (file_position + size) & (SACD_LSN_SIZE - 1);
	LARGE_INTEGER liPosition;
	liPosition.QuadPart = read_start;
	if (::SetFilePointerEx(media_disc, liPosition, &liPosition, FILE_BEGIN) == FALSE) {
		return 0;
	}
	DWORD cbRead;
	bool bReadOk = true;
	if (start_offset == 0 && end_offset == 0) {
		file_position += size;
		if (::ReadFile(media_disc, data, (DWORD)size, &cbRead, NULL) == FALSE) {
			bReadOk = false;
		}
		return bReadOk ? size : 0;
	}
	uint8_t sacd_block[SACD_LSN_SIZE];
	size_t bytes_to_read;
	size_t copy_bytes;
	bytes_to_read = size;
	copy_bytes = min(SACD_LSN_SIZE - start_offset, bytes_to_read);
	if (::ReadFile(media_disc, sacd_block, SACD_LSN_SIZE, &cbRead, NULL) == FALSE) {
		bReadOk = false;
	}
	memcpy(data, sacd_block + start_offset, copy_bytes);
	data = (uint8_t*)data + copy_bytes;
	file_position += (int64_t)copy_bytes;
	bytes_to_read -= copy_bytes;
	if (bytes_to_read <= 0) {
		return bReadOk ? size : 0;
	}
	size_t full_blocks_to_read = (bytes_to_read - end_offset) / SACD_LSN_SIZE;
	if (full_blocks_to_read > 0) {
		if (::ReadFile(media_disc, data, full_blocks_to_read * SACD_LSN_SIZE, &cbRead, NULL) == FALSE) {
			bReadOk = false;
		}
		data = (uint8_t*)data + full_blocks_to_read * SACD_LSN_SIZE;
		file_position += (int64_t)(full_blocks_to_read * SACD_LSN_SIZE);
		bytes_to_read -= full_blocks_to_read * SACD_LSN_SIZE;
	}
	if (bytes_to_read <= 0) {
	}
	if (::ReadFile(media_disc, sacd_block, SACD_LSN_SIZE, &cbRead, NULL) == FALSE) {
		bReadOk = false;
	}
	memcpy(data, sacd_block, end_offset);
	data = (uint8_t*)data + cbRead;
	file_position += (int64_t)end_offset;
	return bReadOk ? size : 0;
}

size_t sacd_media_disc_t::write(const void* data, size_t size) {
	return 0;
}

int64_t sacd_media_disc_t::skip(int64_t bytes) {
	file_position += bytes;
	return file_position;
}

void sacd_media_disc_t::truncate(int64_t position) {
}

void sacd_media_disc_t::on_idle() {
}


sacd_media_file_t::sacd_media_file_t(abort_callback& p_abort) : sacd_media_t(p_abort) {
}

sacd_media_file_t::~sacd_media_file_t() {
}

bool sacd_media_file_t::open(service_ptr_t<file> filehint, const char* path, t_input_open_reason reason) {
	media_file = filehint;
	input_open_file_helper(media_file, path, reason, media_abort);
	return true;
}

bool sacd_media_file_t::close() {
	return true;
}

bool sacd_media_file_t::can_seek() {
	return media_file->can_seek();
}

bool sacd_media_file_t::seek(int64_t position, file::t_seek_mode mode) {
	media_file->seek_ex(position, mode, media_abort);
	return true;
}

int64_t sacd_media_file_t::get_position() {
	return media_file->get_position(media_abort);
}

int64_t sacd_media_file_t::get_size() {
	return media_file->get_size(media_abort);
}

t_filestats sacd_media_file_t::get_stats() {
	return media_file->get_stats(media_abort);
}

size_t sacd_media_file_t::read(void* data, size_t size) {
	return media_file->read(data, size, media_abort);
}

size_t sacd_media_file_t::write(const void* data, size_t size) {
	media_file->write(data, size, media_abort);
	return size;
}

int64_t sacd_media_file_t::skip(int64_t bytes) {
	return media_file->skip(bytes, media_abort);
}

void sacd_media_file_t::truncate(int64_t position) {
	media_file->truncate(position, media_abort);
}

void sacd_media_file_t::on_idle() {
	media_file->on_idle(media_abort);
}
