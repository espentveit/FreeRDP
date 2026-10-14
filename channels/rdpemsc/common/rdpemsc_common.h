/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Mouse Cursor Virtual Channel Extension [MS-RDPEMSC]
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FREERDP_CHANNEL_RDPEMSC_COMMON_H
#define FREERDP_CHANNEL_RDPEMSC_COMMON_H

#include <winpr/assert.h>
#include <winpr/stream.h>

#include <freerdp/channels/rdpemsc.h>

/* [MS-RDPEMSC] 2.2.2.2 RDP_MOUSE_CURSOR_CAPSET signature, "CAPS" */
#define RDPEMSC_CAPSET_SIGNATURE 0x53504143
#define RDPEMSC_CAPSET_V1_SIZE 12

/* [MS-RDPEMSC] 2.2.3.1 RDP_MOUSE_CURSOR_CAPS_ADVERTISE_PDU with the one capability set version
 * 1, 16 bytes */
WINPR_ATTR_NODISCARD
static inline BOOL rdpemsc_write_caps_advertise(wStream* s)
{
	if (!Stream_EnsureRemainingCapacity(s, RDPEMSC_HEADER_SIZE + RDPEMSC_CAPSET_V1_SIZE))
		return FALSE;
	Stream_Write_UINT8(s, PDUTYPE_CS_CAPS_ADVERTISE);      /* pduType */
	Stream_Write_UINT8(s, 0);                              /* updateType */
	Stream_Write_UINT16(s, 0);                             /* reserved */
	Stream_Write_UINT32(s, RDPEMSC_CAPSET_SIGNATURE);      /* signature */
	Stream_Write_UINT32(s, RDP_MOUSE_CURSOR_CAPVERSION_1); /* version */
	Stream_Write_UINT32(s, RDPEMSC_CAPSET_V1_SIZE);        /* size */
	return TRUE;
}

/* [MS-RDPEMSC] 2.2.2.1 RDP_MOUSE_CURSOR_HEADER */
WINPR_ATTR_NODISCARD
static inline BOOL rdpemsc_read_header(wStream* s, RDP_MOUSE_CURSOR_HEADER* header)
{
	WINPR_ASSERT(header);
	if (!Stream_CheckAndLogRequiredLength("com.freerdp.channels.rdpemsc", s, RDPEMSC_HEADER_SIZE))
		return FALSE;
	header->pduType = (RDP_MOUSE_CURSOR_PDUTYPE)Stream_Get_UINT8(s);
	header->updateType = (TS_UPDATETYPE_MOUSEPTR)Stream_Get_UINT8(s);
	header->reserved = Stream_Get_UINT16(s);
	return TRUE;
}

/* [MS-RDPEMSC] 2.2.3.2 RDP_MOUSE_CURSOR_CAPS_CONFIRM_PDU, after its header: the version of the
 * capability set the server chose, RDP_MOUSE_CURSOR_CAPVERSION_INVALID if it is not valid. */
WINPR_ATTR_NODISCARD
static inline RDP_MOUSE_CURSOR_CAPVERSION rdpemsc_read_caps_confirm(wStream* s)
{
	if (!Stream_CheckAndLogRequiredLength("com.freerdp.channels.rdpemsc", s, 12))
		return RDP_MOUSE_CURSOR_CAPVERSION_INVALID;

	const UINT32 signature = Stream_Get_UINT32(s);
	const UINT32 version = Stream_Get_UINT32(s);
	const UINT32 size = Stream_Get_UINT32(s);
	if ((signature != RDPEMSC_CAPSET_SIGNATURE) || (size < 12) ||
	    !Stream_CheckAndLogRequiredLength("com.freerdp.channels.rdpemsc", s, size - 12))
		return RDP_MOUSE_CURSOR_CAPVERSION_INVALID;
	Stream_Seek(s, size - 12);
	return (version == RDP_MOUSE_CURSOR_CAPVERSION_1) ? RDP_MOUSE_CURSOR_CAPVERSION_1
	                                                  : RDP_MOUSE_CURSOR_CAPVERSION_INVALID;
}

/* [MS-RDPEMSC] 2.2.3.3: the update types of RDP_MOUSE_CURSOR_MOUSEPTR_UPDATE_PDU, which are the
 * fast-path pointer update types with the same payloads */
WINPR_ATTR_NODISCARD
static inline BOOL rdpemsc_is_mouseptr_update_type(TS_UPDATETYPE_MOUSEPTR updateType)
{
	switch (updateType)
	{
		case TS_UPDATETYPE_MOUSEPTR_SYSTEM_NULL:
		case TS_UPDATETYPE_MOUSEPTR_SYSTEM_DEFAULT:
		case TS_UPDATETYPE_MOUSEPTR_POSITION:
		case TS_UPDATETYPE_MOUSEPTR_CACHED:
		case TS_UPDATETYPE_MOUSEPTR_POINTER:
		case TS_UPDATETYPE_MOUSEPTR_LARGE_POINTER:
			return TRUE;
		default:
			return FALSE;
	}
}

#endif /* FREERDP_CHANNEL_RDPEMSC_COMMON_H */
