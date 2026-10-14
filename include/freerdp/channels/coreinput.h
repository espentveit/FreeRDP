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

#ifndef FREERDP_CHANNEL_COREINPUT_H
#define FREERDP_CHANNEL_COREINPUT_H

#include <freerdp/api.h>
#include <freerdp/types.h>

#define COREINPUT_CHANNEL_NAME "coreinput"
#define COREINPUT_DVC_CHANNEL_NAME "Microsoft::Windows::RDS::CoreInput"

/** @since version 3.33.0 */
#define COREINPUT_HEADER_SIGNATURE 0x03
/** @since version 3.33.0 */
#define COREINPUT_HEADER_LENGTH 4
/** @since version 3.33.0 */
#define COREINPUT_PROTOCOL_VERSION_1_0 0x0100
/** Events in one RDP_CORE_INPUT_CS_KEYBOARD_AND_MOUSE_INPUT_PDU (eventCount is a byte).
 *  @since version 3.33.0 */
#define COREINPUT_MAX_EVENTS 255

#ifdef __cplusplus
extern "C"
{
#endif

	/** [MS-RDPECI] 2.2.2.1 pduType
	 *  @since version 3.33.0 */
	typedef enum
	{
		COREINPUT_PDUTYPE_CS_INIT_REQUEST = 0x01,
		COREINPUT_PDUTYPE_SC_INIT_RESPONSE = 0x02,
		COREINPUT_PDUTYPE_CS_KEYBOARD_AND_MOUSE_INPUT = 0x03
	} eCoreInputPduType;

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CHANNEL_COREINPUT_H */
