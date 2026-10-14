/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Core Input Virtual Channel Extension [MS-RDPECI]
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

#ifndef FREERDP_CHANNEL_COREINPUT_COMMON_H
#define FREERDP_CHANNEL_COREINPUT_COMMON_H

#include <winpr/assert.h>
#include <winpr/stream.h>

#include <freerdp/channels/coreinput.h>

/* [MS-RDPECI] 2.2.2.1 RDP_CORE_INPUT_HEADER */
static inline void coreinput_write_header(wStream* s, BYTE pduType, BYTE eventCount)
{
	WINPR_ASSERT(s);
	Stream_Write_UINT8(s, COREINPUT_HEADER_SIGNATURE); /* signature */
	Stream_Write_UINT8(s, pduType);                    /* pduType */
	Stream_Write_UINT8(s, eventCount);                 /* eventCount */
	Stream_Write_UINT8(s, 0);                          /* padding */
}

/* [MS-RDPECI] 2.2.3.1 RDP_CORE_INPUT_CS_INIT_REQUEST_PDU, 16 bytes */
WINPR_ATTR_NODISCARD
static inline BOOL coreinput_write_init_request(wStream* s)
{
	WINPR_ASSERT(s);
	if (!Stream_EnsureRemainingCapacity(s, COREINPUT_HEADER_LENGTH + 12))
		return FALSE;
	coreinput_write_header(s, COREINPUT_PDUTYPE_CS_INIT_REQUEST, 0);
	Stream_Write_UINT16(s, COREINPUT_PROTOCOL_VERSION_1_0); /* protocolVersionMin */
	Stream_Write_UINT16(s, COREINPUT_PROTOCOL_VERSION_1_0); /* protocolVersionMax */
	Stream_Write_UINT64(s, 0);                              /* reserved */
	return TRUE;
}

/* [MS-RDPECI] 2.2.3.3 RDP_CORE_INPUT_CS_KEYBOARD_AND_MOUSE_INPUT_PDU. The input events are
 * RDP_CORE_INPUT_EVENT_CONTAINERs, whose type and flags byte and payloads are the same as those of
 * fast-path input events ([MS-RDPBCGR] 2.2.8.1.2.2), so they are taken as they are. */
WINPR_ATTR_NODISCARD
static inline BOOL coreinput_write_input_pdu(wStream* s, const BYTE* events, size_t length,
                                             size_t count)
{
	WINPR_ASSERT(s);
	if (!events || (length == 0) || (count == 0) || (count > COREINPUT_MAX_EVENTS))
		return FALSE;
	if (!Stream_EnsureRemainingCapacity(s, COREINPUT_HEADER_LENGTH + length))
		return FALSE;
	coreinput_write_header(s, COREINPUT_PDUTYPE_CS_KEYBOARD_AND_MOUSE_INPUT,
	                       WINPR_ASSERTING_INT_CAST(BYTE, count));
	Stream_Write(s, events, length);
	return TRUE;
}

/* [MS-RDPECI] 2.2.3.2 RDP_CORE_INPUT_SC_INIT_RESPONSE_PDU: the version the server selected, 0
 * for anything that is not a valid response. */
WINPR_ATTR_NODISCARD
static inline UINT16 coreinput_read_init_response(wStream* s)
{
	WINPR_ASSERT(s);
	if (!Stream_CheckAndLogRequiredLength("com.freerdp.channels.coreinput", s,
	                                      COREINPUT_HEADER_LENGTH + 12))
		return 0;

	const BYTE signature = Stream_Get_UINT8(s);
	const BYTE pduType = Stream_Get_UINT8(s);
	Stream_Seek(s, 2); /* eventCount, padding */
	if ((signature != COREINPUT_HEADER_SIGNATURE) ||
	    (pduType != COREINPUT_PDUTYPE_SC_INIT_RESPONSE))
		return 0;

	const UINT16 selected = Stream_Get_UINT16(s); /* selectedProtocolVersion */
	Stream_Seek(s, 2 + 8);                        /* protocolVersionMax, reserved */
	return selected;
}

#endif /* FREERDP_CHANNEL_COREINPUT_COMMON_H */
