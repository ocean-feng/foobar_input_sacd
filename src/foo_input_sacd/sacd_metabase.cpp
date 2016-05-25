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

#include "sacd_metabase.h"

#define MD5LEN 16

sacd_metabase_t::sacd_metabase_t(sacd_disc_t* p_disc, const char* p_metafile, abort_callback&p_abort) {
	media_abort = p_abort;
	xmldoc = nullptr;
	xmldoc_changed = false;
	VariantInit(&xmlfile);
	metabase_initialized = false;
	uint8_t tag_data[MASTER_TOC_LEN * SACD_LSN_SIZE];
	if (p_disc->read_blocks_raw(START_OF_MASTER_TOC, MASTER_TOC_LEN, tag_data)) {
		HCRYPTPROV hProv = 0;
		HCRYPTHASH hHash = 0;
		BYTE rgbHash[MD5LEN];
		DWORD cbHash = MD5LEN;
		if (CryptAcquireContext(&hProv,  nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
			if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
				if (CryptHashData(hHash, tag_data, MASTER_TOC_LEN * SACD_LSN_SIZE, 0)) {
					if (CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0)) {
						for (DWORD i = 0; i < cbHash; i++) {
							string_printf hex_byte = string_printf("%02X", rgbHash[i]); 
							store_id << hex_byte;
						}
						metafile.reset();
						if (p_metafile) {
							metafile = p_metafile;
						}
						store_root = core_api::get_profile_path();
						store_path = store_root;
						store_path << "\\" <<  METABASE_CATALOG;
						store_file = store_path;
						store_file << "\\" << store_id << ".xml";
						if (filesystem::g_exists(store_file, media_abort)) {
							if (!metafile.is_empty() && !filesystem::g_exists(metafile, media_abort)) {
								filesystem::g_copy(store_file, metafile, media_abort);
								string_printf copy_msg = string_printf("Metabase file is copied from '%s' to '%s'", (const char*)store_file, (const char*)metafile);
								if (!filesystem::g_exists(metafile, media_abort)) {
									string_printf metabase_msg = string_printf("Cannot copy tag file to '%s'", (const char*)metafile);
									popup_message::g_show(metabase_msg, METABASE_CATALOG);
								}
							}
						}
						CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
						if (xmldoc.CreateInstance(__uuidof(MSXML2::DOMDocument60)) == S_OK) {
							xml_file = !metafile.is_empty() ? metafile : store_file; 
							if (xml_file.find_first("file://") == 0) {
								xml_file.remove_chars(0, 7);
							}
							V_BSTR(&xmlfile) = SysAllocString(string_wide_from_utf8(xml_file));
							V_VT(&xmlfile) = VT_BSTR;
							metabase_initialized = init_xmldoc("SACD");
							if (!metabase_initialized) {
								console::error("Cannot initialize XML document");
							}
						}
						else {
							console::error("Cannot create XML instance");
						}
					}
					else {
						console::error("Cannot get MD5 hash value");
					}
				}
				else {
					console::error("Cannot create hash MD5 data");
				}
				CryptDestroyHash(hHash);
			}
			else {
				console::error("Cannot create MD5 hash");
			}
			CryptReleaseContext(hProv, 0);
		}
		else {
			console::error("Cannot acquire crypto context");
		}
	}
	else {
		console::error("Cannot read tag MD5 hash");
	}
}

sacd_metabase_t::~sacd_metabase_t() {
	if (xmldoc) {
		if (xmldoc_changed) {
			string_printf info_msg = string_printf("Saving metadata to XML file");
			console::info(info_msg);
			if (FAILED(xmldoc->save(xmlfile))) {
				console::error("Cannot save tag file");
			}
		}
		xmldoc.Release();
	}
	VariantClear(&xmlfile);
}

bool sacd_metabase_t::get_meta_info(t_uint32 subsong, file_info& info) {
	if (!metabase_initialized) {
		return false;
	}
	MSXML2::IXMLDOMNodePtr node_track;
	node_track = get_track_node(subsong);
	if (node_track == nullptr) {
		return true;
	}
	MSXML2::IXMLDOMNodeListPtr list_tags;
	list_tags = node_track->GetchildNodes();
	if (list_tags == nullptr) {
		return false;
	}
	for (long i = 0; i < list_tags->Getlength(); i++) {
		MSXML2::IXMLDOMNodePtr node_tag;
		node_tag = list_tags->Getitem(i);
		if (node_tag) {
			_bstr_t bstr_node_name = node_tag->GetnodeName();
			string_formatter(node_name);
			node_name << string_utf8_from_wide(bstr_node_name);
			MSXML2::IXMLDOMNamedNodeMapPtr nodemap_attr;
			nodemap_attr = node_tag->Getattributes();
			if (nodemap_attr != nullptr) {
				string_formatter(tag_name);
				string_formatter(tag_value);
				string_formatter(xml_tag_value);
				MSXML2::IXMLDOMNodePtr node_attr;
				node_attr = nodemap_attr->getNamedItem(ATT_NAME);
				if (node_attr != nullptr) {
					VARIANT attr_value;
					VariantInit(&attr_value);
					V_VT(&attr_value) = VT_BSTR;
					node_attr->get_nodeValue(&attr_value);
					tag_name << string_utf8_from_wide(V_BSTR(&attr_value));
					VariantClear(&attr_value);
				}
				node_attr = nodemap_attr->getNamedItem(ATT_VALUE);
				if (node_attr != nullptr) {
					VARIANT attr_value;
					VariantInit(&attr_value);
					V_VT(&attr_value) = VT_BSTR;
					node_attr->get_nodeValue(&attr_value);
					xml_tag_value << string_utf8_from_wide(V_BSTR(&attr_value));
					xml2utf(xml_tag_value, tag_value);
					VariantClear(&attr_value);
				}
				if (tag_name.length() > 0) {
					if (strcmp(node_name, TAG_META) == 0) {
						bool head_chunk = true;
						t_size sep_pos;
						do {
							sep_pos = string_find_first(tag_value, ATT_VALSEP);
							string_formatter(tag_value_head);
							tag_value_head.add_string(tag_value, sep_pos);
							while (tag_value_head.length() > 0 && tag_value_head[0] == ' ') {
								tag_value_head.remove_chars(0, 1);
							}
							if (head_chunk) {
								info.meta_set(tag_name, tag_value_head);
								head_chunk = false;
							}
							else {
								info.meta_add(tag_name, tag_value_head);
							}
							tag_value.remove_chars(0, sep_pos + 1);
						} while (sep_pos != ~0);
					}
				}
			}
		}
	}
	return true;
}

bool sacd_metabase_t::get_replaygain(t_uint32 subsong, file_info& info, float dB_gain_adjust) {
	if (!metabase_initialized) {
		return false;
	}
	MSXML2::IXMLDOMNodePtr node_track;
	node_track = get_track_node(subsong);
	if (node_track == nullptr) {
		return true;
	}
	MSXML2::IXMLDOMNodeListPtr list_tags;
	list_tags = node_track->GetchildNodes();
	if (list_tags == nullptr) {
		return false;
	}
	replaygain_info rg_info = info.get_replaygain();
	for (long i = 0; i < list_tags->Getlength(); i++) {
		MSXML2::IXMLDOMNodePtr node_tag;
		node_tag = list_tags->Getitem(i);
		if (node_tag) {
			_bstr_t bstr_node_name = node_tag->GetnodeName();
			string_formatter(node_name);
			node_name << string_utf8_from_wide(bstr_node_name);
			MSXML2::IXMLDOMNamedNodeMapPtr nodemap_attr;
			nodemap_attr = node_tag->Getattributes();
			if (nodemap_attr != nullptr) {
				string_formatter(tag_name);
				string_formatter(tag_value);
				string_formatter(xml_tag_value);
				MSXML2::IXMLDOMNodePtr node_attr;
				node_attr = nodemap_attr->getNamedItem(ATT_NAME);
				if (node_attr != nullptr) {
					VARIANT attr_value;
					VariantInit(&attr_value);
					V_VT(&attr_value) = VT_BSTR;
					node_attr->get_nodeValue(&attr_value);
					tag_name << string_utf8_from_wide(V_BSTR(&attr_value));
					VariantClear(&attr_value);
				}
				node_attr = nodemap_attr->getNamedItem(ATT_VALUE);
				if (node_attr != nullptr) {
					VARIANT attr_value;
					VariantInit(&attr_value);
					V_VT(&attr_value) = VT_BSTR;
					node_attr->get_nodeValue(&attr_value);
					xml_tag_value << string_utf8_from_wide(V_BSTR(&attr_value));
					xml2utf(xml_tag_value, tag_value);
					VariantClear(&attr_value);
				}
				if (tag_name.length() > 0) {
					if (strcmp(node_name, TAG_REPLAYGAIN) == 0) {
						if (strcmp(tag_name.get_ptr(), "replaygain_track_gain") == 0) {
							rg_info.set_track_gain_text(tag_value);
						}
						if (strcmp(tag_name.get_ptr(), "replaygain_track_peak") == 0) {
							rg_info.set_track_peak_text(tag_value);
						}
						if (strcmp(tag_name.get_ptr(), "replaygain_album_gain") == 0) {
							rg_info.set_album_gain_text(tag_value);
						}
						if (strcmp(tag_name.get_ptr(), "replaygain_album_peak") == 0) {
							rg_info.set_album_peak_text(tag_value);
						}
					}
				}
			}
		}
	}
	adjust_replaygain(GET_REPLAYGAIN_INFO, rg_info, dB_gain_adjust);
	info.set_replaygain(rg_info);
	return true;
}

bool sacd_metabase_t::set_meta_info(t_uint32 subsong, const file_info& info, bool is_linked) {
	if (!metabase_initialized) {
		return false;
	}
	create_metabase_catalog();
	MSXML2::IXMLDOMNodePtr node_track;
	node_track = get_track_node(subsong);
	if (node_track == nullptr) {
		node_track = new_track_node(subsong);
		if (node_track == nullptr) {
			return false;
		}
	}
	delete_info_tags(node_track, TAG_META, is_linked);
	for (t_size i = 0; i < info.meta_get_count(); i++) {
		string_formatter(tag_name);
		string_formatter(tag_value);
		tag_name << info.meta_enum_name(i);
		tag_value << info.meta_enum_value(i, 0);
		for (t_size j = 1; j < info.meta_enum_value_count(i); j++) {
			tag_value << ATT_VALSEP << info.meta_enum_value(i, j);
		}
		if (!is_linked || (is_linked && is_linked_tag(tag_name))) {
			add_info_tag(node_track, TAG_META, tag_name, tag_value);
		}
	}
	return true;
}

bool sacd_metabase_t::set_replaygain(t_uint32 subsong, const file_info& info, float dB_gain_adjust) {
	if (!metabase_initialized) {
		return false;
	}
	create_metabase_catalog();
	MSXML2::IXMLDOMNodePtr node_track;
	node_track = get_track_node(subsong);
	if (node_track == nullptr) {
		node_track = new_track_node(subsong);
		if (node_track == nullptr) {
			return false;
		}
	}
	delete_info_tags(node_track, TAG_REPLAYGAIN, false);
	replaygain_info rg_info = info.get_replaygain();
	adjust_replaygain(SET_REPLAYGAIN_INFO, rg_info, dB_gain_adjust);
	replaygain_info::t_text_buffer rg_value;
	if (rg_info.is_track_gain_present()) {
		if (rg_info.format_track_gain(rg_value)) {
			string_formatter(tag_name);
			string_formatter(tag_value);
			tag_name << "replaygain_track_gain";
			tag_value << rg_value;
			add_info_tag(node_track, TAG_REPLAYGAIN, tag_name, tag_value);
		}
	}
	if (rg_info.is_track_peak_present()) {
		if (rg_info.format_track_peak(rg_value)) {
			string_formatter(tag_name);
			string_formatter(tag_value);
			tag_name << "replaygain_track_peak";
			tag_value << rg_value;
			add_info_tag(node_track, TAG_REPLAYGAIN, tag_name, tag_value);
		}
	}
	if (rg_info.is_album_gain_present()) {
		if (rg_info.format_album_gain(rg_value)) {
			string_formatter(tag_name);
			string_formatter(tag_value);
			tag_name << "replaygain_album_gain";
			tag_value << rg_value;
			add_info_tag(node_track, TAG_REPLAYGAIN, tag_name, tag_value);
		}
	}
	if (rg_info.is_album_peak_present()) {
		if (rg_info.format_album_peak(rg_value)) {
			string_formatter(tag_name);
			string_formatter(tag_value);
			tag_name << "replaygain_album_peak";
			tag_value << rg_value;
			add_info_tag(node_track, TAG_REPLAYGAIN, tag_name, tag_value);
		}
	}
	return true;
}

bool sacd_metabase_t::commit() {
	xmldoc_changed = true;
	return true;
}

bool sacd_metabase_t::create_metabase_catalog() {
	if (metafile.is_empty()) {
		if (!filesystem::g_exists(store_path, media_abort)) {
			filesystem::g_create_directory(store_path, media_abort);
			if (filesystem::g_exists(store_path, media_abort)) {
				string_printf metabase_msg = string_printf("SACD metabase created '%s'", (const char *)store_path);
				popup_message::g_show(metabase_msg, METABASE_CATALOG);
			}
			else {
				string_printf metabase_msg = string_printf("Cannot create SACD metabase '%s'", (const char *)store_path);
				popup_message::g_show(metabase_msg, METABASE_CATALOG, popup_message::icon_error);
				return false;
			}
		}
	}
	return true;
}

bool sacd_metabase_t::init_xmldoc(const char* store_type) {
	xmldoc->async = VARIANT_FALSE;
	xmldoc->preserveWhiteSpace = VARIANT_TRUE;
	if (xmldoc->load(xmlfile) != VARIANT_TRUE) {
		MSXML2::IXMLDOMProcessingInstructionPtr inst_root;
		inst_root = xmldoc->createProcessingInstruction("xml", "version='1.0' encoding='utf-8'");
		if (inst_root == nullptr) {
			return false;
		}
		xmldoc->appendChild(inst_root);
		MSXML2::IXMLDOMCommentPtr comm_root;
		comm_root = xmldoc->createComment("SACD metabase file");
		if (comm_root == nullptr) {
			return false;
		}
		xmldoc->appendChild(comm_root);
		MSXML2::IXMLDOMElementPtr elem_root;
		elem_root = xmldoc->createElement(TAG_ROOT);
		if (elem_root == nullptr) {
			return false;
		}
		xmldoc->appendChild(elem_root);
		MSXML2::IXMLDOMElementPtr elem_store;
		elem_store = xmldoc->createElement(TAG_STORE);
		if (elem_store == nullptr) {
			return false;
		}
		elem_root->appendChild(elem_store);
		MSXML2::IXMLDOMAttributePtr attr_store;
		attr_store = xmldoc->createAttribute(ATT_ID);
		if (attr_store == nullptr) {
			return false;
		}
		attr_store->value = (const wchar_t*)string_wide_from_utf8(store_id);
		elem_store->setAttributeNode(attr_store);
		attr_store = xmldoc->createAttribute(ATT_TYPE);
		if (attr_store == nullptr) {
			return false;
		}
		attr_store->value = store_type;
		elem_store->setAttributeNode(attr_store);
		attr_store = xmldoc->createAttribute(ATT_VERSION);
		if (attr_store == nullptr) {
			return false;
		}
		attr_store->value = METABASE_VERSION;
		elem_store->setAttributeNode(attr_store);
		return true;
	}
	return true;
}

MSXML2::IXMLDOMNodePtr sacd_metabase_t::get_track_node(t_uint32 subsong) {
	string_formatter(subsong_id);
	subsong_id << (subsong + 1);
	string_formatter(xpath_track);
	xpath_track << TAG_ROOT << "/" << TAG_STORE  << "[@" << ATT_ID << "='" << store_id << "']" << "/" << TAG_TRACK << "[@" << ATT_ID << "='" << subsong_id << "']";
	MSXML2::IXMLDOMNodePtr node_track;
	node_track = xmldoc->selectSingleNode((const wchar_t*)string_wide_from_utf8(xpath_track));
	return node_track;
}

MSXML2::IXMLDOMNodePtr sacd_metabase_t::new_track_node(t_uint32 subsong) {
	MSXML2::IXMLDOMNodePtr node_track = nullptr;
	string_formatter(subsong_id);
	subsong_id << (subsong + 1);
	string_formatter(xpath_store);
	xpath_store << TAG_ROOT << "/" << TAG_STORE  << "[@" << ATT_ID << "='" << store_id << "']";
	MSXML2::IXMLDOMNodePtr node_store;
	node_store = xmldoc->selectSingleNode((const wchar_t*)string_wide_from_utf8(xpath_store));
	if (node_store != nullptr) {
		MSXML2::IXMLDOMElementPtr elem_track;
		elem_track = xmldoc->createElement(TAG_TRACK);
		if (elem_track != nullptr) {
			MSXML2::IXMLDOMAttributePtr attr_track;
			attr_track = xmldoc->createAttribute(ATT_ID);
			if (attr_track != nullptr) {
				attr_track->value = (const wchar_t*)string_wide_from_utf8(subsong_id);
				elem_track->setAttributeNode(attr_track);
			}
			node_track = node_store->appendChild(elem_track);
		}
	}
	return node_track;
}

bool sacd_metabase_t::is_linked_tag(string_formatter& tag_name) {
	return stricmp(tag_name.get_ptr(), "dynamic range") && stricmp(tag_name.get_ptr(), "album dynamic range");
}

void sacd_metabase_t::delete_info_tags(MSXML2::IXMLDOMNodePtr& node_track, const char* tag_type, bool is_linked) {
	MSXML2::IXMLDOMNodeListPtr list_tags;
	list_tags = node_track->GetchildNodes();
	if (list_tags == nullptr) {
		return;
	}
	bool node_removed;
	do {
		node_removed = false;
		for (long i = 0; i < list_tags->Getlength(); i++) {
			MSXML2::IXMLDOMNodePtr node_tag;
			node_tag = list_tags->Getitem(i);
			if (node_tag) {
				_bstr_t bstr_node_name = node_tag->GetnodeName();
				string_formatter(node_name);
				node_name << string_utf8_from_wide(bstr_node_name);
				MSXML2::IXMLDOMNamedNodeMapPtr nodemap_attr;
				nodemap_attr = node_tag->Getattributes();
				if (nodemap_attr != nullptr) {
					string_formatter(tag_name);
					string_formatter(tag_value);
					string_formatter(xml_tag_value);
					MSXML2::IXMLDOMNodePtr node_attr;
					node_attr = nodemap_attr->getNamedItem(ATT_NAME);
					if (node_attr != nullptr) {
						VARIANT attr_value;
						VariantInit(&attr_value);
						V_VT(&attr_value) = VT_BSTR;
						node_attr->get_nodeValue(&attr_value);
						tag_name << string_utf8_from_wide(V_BSTR(&attr_value));
						VariantClear(&attr_value);
					}
					if (node_name.length() > 0 && strcmp(node_name.get_ptr(), tag_type) == 0) {
						if (!is_linked || (is_linked && is_linked_tag(tag_name))) {
							node_track->removeChild(node_tag);
							node_removed = true;
							break;
						}
					}
				}
			}
		}
	} while (node_removed);
}

bool sacd_metabase_t::add_info_tag(MSXML2::IXMLDOMNodePtr& node_track, const char* tag_type, string_formatter& tag_name, string_formatter& tag_value) {
	MSXML2::IXMLDOMElementPtr elem_tag;
	elem_tag = xmldoc->createElement(tag_type);
	if (elem_tag != nullptr) {
		node_track->appendChild(elem_tag);
		MSXML2::IXMLDOMAttributePtr attr_tag;
		attr_tag = xmldoc->createAttribute(ATT_NAME);
		if (attr_tag != nullptr) {
			attr_tag->value = (const wchar_t*)string_wide_from_utf8(tag_name);
			elem_tag->setAttributeNode(attr_tag);
		}
		else {
			return false;
		}
		attr_tag = xmldoc->createAttribute(ATT_VALUE);
		if (attr_tag != nullptr) {
			string_formatter(xml_tag_value);
			utf2xml(tag_value, xml_tag_value);
			attr_tag->value = (const wchar_t*)string_wide_from_utf8(xml_tag_value);
			elem_tag->setAttributeNode(attr_tag);
		}
		else {
			return false;
		}
	}
	else {
		return false;
	}
	return true;
}

void sacd_metabase_t::utf2xml(string_formatter& src, string_formatter& dst) {
	dst.truncate(0);
	for (t_size i = 0; i < src.length(); i++) {
		if (src[i] == '\r') {
			dst.add_string("&#13;");
		}
		else if (src[i] == '\n') {
			dst.add_string("&#10;");
		}
		else {
			dst.add_byte(src[i]);
		}
	}
}

void sacd_metabase_t::xml2utf(string_formatter& src, string_formatter& dst) {
	dst.truncate(0);
	for (t_size i = 0; i < src.length(); i++) {
		if (strncmp(&src.get_ptr()[i], "&#13;", 5) == 0) {
			dst.add_byte('\r');
			i += 4;
		}
		else if (strncmp(&src.get_ptr()[i], "&#10;", 5) == 0) {
			dst.add_byte('\n');
			i += 4;
		}
		else {
			dst.add_byte(src[i]);
		}
	}
}

void sacd_metabase_t::adjust_replaygain(replaygain_mode_e mode, replaygain_info& rg_info, float dB_gain_adjust) {
	float adj_coef_dB = 0.0f;
	switch (mode) {
	case GET_REPLAYGAIN_INFO:
		adj_coef_dB = +dB_gain_adjust;
		break;
	case SET_REPLAYGAIN_INFO:
		adj_coef_dB = -dB_gain_adjust;
		break;
	}
	float adj_coef = pow(10.0f, adj_coef_dB / 20.0f);
	if (rg_info.is_album_gain_present()) {
		rg_info.m_album_gain -= adj_coef_dB;
	}
	if (rg_info.is_album_peak_present()) {
		rg_info.m_album_peak *= adj_coef;
	}
	if (rg_info.is_track_gain_present()) {
		rg_info.m_track_gain -= adj_coef_dB;
	}
	if (rg_info.is_track_peak_present()) {
		rg_info.m_track_peak *= adj_coef;
	}
}
