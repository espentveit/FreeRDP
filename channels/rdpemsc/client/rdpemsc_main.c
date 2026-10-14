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

#include <freerdp/config.h>

#include <winpr/crt.h>
#include <winpr/assert.h>
#include <winpr/stream.h>

#include <freerdp/freerdp.h>
#include <freerdp/update.h>
#include <freerdp/channels/log.h>
#include <freerdp/client/channels.h>
#include <freerdp/channels/rdpemsc.h>

#include "../common/rdpemsc_common.h"

#define TAG CHANNELS_TAG("rdpemsc.client")

/* [MS-RDPEMSC] carries the mouse pointer updates in place of the fast-path pointer updates
 * ([MS-RDPBCGR] 2.2.9.1.2.1.5 and on), with the same update types and payloads, so they go to the
 * same pointer callbacks (freerdp_update_recv_pointer). With UDP multitransport the channel moves
 * to the tunnel with Soft-Sync, and with it the pointer updates. */
typedef struct
{
	GENERIC_DYNVC_PLUGIN base;
	rdpContext* rdpcontext;
} RDPEMSC_PLUGIN;

/* [MS-RDPEMSC] 3.2.5.1: the client advertises its capabilities when the channel is open. */
WINPR_ATTR_NODISCARD
static UINT rdpemsc_on_open(IWTSVirtualChannelCallback* pChannelCallback)
{
	GENERIC_CHANNEL_CALLBACK* callback = (GENERIC_CHANNEL_CALLBACK*)pChannelCallback;
	WINPR_ASSERT(callback);
	WINPR_ASSERT(callback->channel);
	WINPR_ASSERT(callback->channel->Write);

	BYTE buffer[RDPEMSC_HEADER_SIZE + RDPEMSC_CAPSET_V1_SIZE] = WINPR_C_ARRAY_INIT;
	wStream sbuffer = WINPR_C_ARRAY_INIT;
	wStream* s = Stream_StaticInit(&sbuffer, buffer, sizeof(buffer));
	if (!rdpemsc_write_caps_advertise(s))
		return ERROR_INTERNAL_ERROR;

	WLog_DBG(TAG, "sending Caps Advertise, version %d", RDP_MOUSE_CURSOR_CAPVERSION_1);
	return callback->channel->Write(callback->channel, (ULONG)Stream_GetPosition(s), buffer,
	                                nullptr);
}

WINPR_ATTR_NODISCARD
static UINT rdpemsc_on_data_received(IWTSVirtualChannelCallback* pChannelCallback, wStream* data)
{
	GENERIC_CHANNEL_CALLBACK* callback = (GENERIC_CHANNEL_CALLBACK*)pChannelCallback;
	WINPR_ASSERT(callback);

	RDPEMSC_PLUGIN* rdpemsc = (RDPEMSC_PLUGIN*)callback->plugin;
	WINPR_ASSERT(rdpemsc);
	WINPR_ASSERT(rdpemsc->rdpcontext);

	RDP_MOUSE_CURSOR_HEADER header = WINPR_C_ARRAY_INIT;
	if (!rdpemsc_read_header(data, &header))
		return ERROR_INVALID_DATA;

	switch (header.pduType)
	{
		case PDUTYPE_SC_CAPS_CONFIRM:
		{
			const RDP_MOUSE_CURSOR_CAPVERSION version = rdpemsc_read_caps_confirm(data);
			if (version == RDP_MOUSE_CURSOR_CAPVERSION_INVALID)
				WLog_WARN(TAG, "Caps Confirm without a valid capability set");
			else
				WLog_DBG(TAG, "Caps Confirm, version %d: pointer updates come on the channel",
				         version);
			return CHANNEL_RC_OK;
		}

		case PDUTYPE_SC_MOUSEPTR_UPDATE:
			if (!rdpemsc_is_mouseptr_update_type(header.updateType))
			{
				WLog_WARN(TAG, "unknown pointer update type 0x%02x, ignored", header.updateType);
				return CHANNEL_RC_OK;
			}
			if (!freerdp_update_recv_pointer(rdpemsc->rdpcontext->update, (UINT16)header.updateType,
			                                 data))
			{
				WLog_WARN(TAG, "pointer update type 0x%02x failed", header.updateType);
				return ERROR_INVALID_DATA;
			}
			return CHANNEL_RC_OK;

		default:
			WLog_WARN(TAG, "unknown PDU type 0x%02x, ignored", header.pduType);
			return CHANNEL_RC_OK;
	}
}

WINPR_ATTR_NODISCARD
static UINT rdpemsc_on_close(IWTSVirtualChannelCallback* pChannelCallback)
{
	free(pChannelCallback);
	return CHANNEL_RC_OK;
}

WINPR_ATTR_NODISCARD
static UINT init_plugin_cb(GENERIC_DYNVC_PLUGIN* base, rdpContext* rcontext,
                           WINPR_ATTR_UNUSED rdpSettings* settings)
{
	RDPEMSC_PLUGIN* rdpemsc = (RDPEMSC_PLUGIN*)base;
	WINPR_ASSERT(rdpemsc);

	rdpemsc->rdpcontext = rcontext;
	return CHANNEL_RC_OK;
}

static void terminate_plugin_cb(WINPR_ATTR_UNUSED GENERIC_DYNVC_PLUGIN* base)
{
}

static const IWTSVirtualChannelCallback rdpemsc_functions = { rdpemsc_on_data_received,
	                                                          rdpemsc_on_open, rdpemsc_on_close,
	                                                          nullptr };

FREERDP_ENTRY_POINT(UINT VCAPITYPE rdpemsc_DVCPluginEntry(IDRDYNVC_ENTRY_POINTS* pEntryPoints))
{
	return freerdp_generic_DVCPluginEntry(pEntryPoints, TAG, RDPEMSC_DVC_CHANNEL_NAME,
	                                      sizeof(RDPEMSC_PLUGIN), sizeof(GENERIC_CHANNEL_CALLBACK),
	                                      &rdpemsc_functions, init_plugin_cb, terminate_plugin_cb);
}
