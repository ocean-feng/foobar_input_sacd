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

#ifndef _SACD_METABASE_H_INCLUDED
#define _SACD_METABASE_H_INCLUDED

#include <foobar2000.h>
#include "sacd_disc.h"

#import <msxml6.dll>

#pragma warning(disable:4996)

using namespace pfc;
using namespace stringcvt;

#define TAG_ROOT       "root"
#define TAG_STORE      "store"
#define TAG_TRACK      "track"
#define TAG_INFO       "info"
#define TAG_META       "meta"
#define TAG_REPLAYGAIN "replaygain"

#define ATT_ID      "id"
#define ATT_NAME    "name"
#define ATT_TYPE    "type"
#define ATT_VALUE   "value"
#define ATT_VALSEP  ";"
#define ATT_VERSION "version"

#define METABASE_CATALOG "sacd_metabase"
#define METABASE_VERSION "1.1"

enum replaygain_mode_e {GET_REPLAYGAIN_INFO = 1, SET_REPLAYGAIN_INFO = 2};

class sacd_metabase_t {
	abort_callback_impl default_abort;
	abort_callback& media_abort = default_abort;
	string_formatter(metafile);
	string_formatter(store_id);
	string_formatter(store_root);
	string_formatter(store_path);
	string_formatter(store_file);
	string_formatter(xml_file);
	MSXML2::IXMLDOMDocumentPtr xmldoc;
	bool xmldoc_changed;
	VARIANT xmlfile;
	bool metabase_initialized;
public:
	sacd_metabase_t(sacd_disc_t* p_disc, const char* p_metafile, abort_callback& p_abort);
	~sacd_metabase_t();
	bool get_meta_info(t_uint32 subsong, file_info& info);
	bool get_replaygain(t_uint32 subsong, file_info& info, float dB_gain_adjust);
	bool set_meta_info(t_uint32 subsong, const file_info& info, bool is_linked);
	bool set_replaygain(t_uint32 subsong, const file_info& info, float dB_gain_adjust);
	bool commit();
private:
	bool create_metabase_catalog();
	bool init_xmldoc(const char* store_type);
	MSXML2::IXMLDOMNodePtr get_track_node(t_uint32 subsong);
	MSXML2::IXMLDOMNodePtr new_track_node(t_uint32 subsong);
	bool is_linked_tag(string_formatter& tag_name);
	void delete_info_tags(MSXML2::IXMLDOMNodePtr& node_track, const char* tag_type, bool is_linked);
	bool add_info_tag(MSXML2::IXMLDOMNodePtr& node_track, const char* tag_type, string_formatter& tag_name, string_formatter& tag_value);
	void utf2xml(string_formatter& src, string_formatter& dst);
	void xml2utf(string_formatter& src, string_formatter& dst);
	void adjust_replaygain(replaygain_mode_e mode, replaygain_info& rg_info, float dB_gain_adjust);
};

#endif
