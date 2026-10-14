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

#include <freerdp/config.h>

#include <winpr/crt.h>
#include <winpr/assert.h>
#include <winpr/stream.h>
#include <winpr/synch.h>

#include <freerdp/freerdp.h>
#include <freerdp/input.h>
#include <freerdp/channels/log.h>
#include <freerdp/client/channels.h>
#include <freerdp/channels/coreinput.h>

#include "../common/coreinput_common.h"

#define TAG CHANNELS_TAG("coreinput.client")

/* [MS-RDPECI] carries the keyboard and mouse input in place of the fast-path input PDUs
 * ([MS-RDPBCGR] 2.2.8.1.2). Once the server answers the Init Request with a version this
 * client supports, all fast-path input events go on the channel
 * (freerdp_input_set_events_redirect), and with it on the UDP multitransport tunnel once Soft-Sync
 * moved the channel there. */
typedef struct
{
	GENERIC_DYNVC_PLUGIN base;
	rdpContext* rdpcontext;

	/* guards callback: a send holds it for its write, so the channel does not close under it */
	CRITICAL_SECTION lock;
	GENERIC_CHANNEL_CALLBACK* callback;
} COREINPUT_PLUGIN;

/* The events of one fast-path input PDU, as an RDP_CORE_INPUT_CS_KEYBOARD_AND_MOUSE_INPUT_PDU.
 * Called by the core for each batch of input, see freerdp_input_set_events_redirect. */
WINPR_ATTR_NODISCARD
static BOOL coreinput_send_events(void* custom, const BYTE* events, size_t length, size_t count)
{
	COREINPUT_PLUGIN* coreinput = custom;
	WINPR_ASSERT(coreinput);

	wStream* s = Stream_New(nullptr, COREINPUT_HEADER_LENGTH + length);
	if (!s)
		return FALSE;

	BOOL rc = FALSE;
	if (!coreinput_write_input_pdu(s, events, length, count))
		goto out;
	Stream_SealLength(s);

	EnterCriticalSection(&coreinput->lock);
	GENERIC_CHANNEL_CALLBACK* callback = coreinput->callback;
	if (callback && callback->channel && callback->channel->Write)
		rc = callback->channel->Write(callback->channel, (ULONG)Stream_Length(s), Stream_Buffer(s),
		                              nullptr) == CHANNEL_RC_OK;
	LeaveCriticalSection(&coreinput->lock);

out:
	Stream_Free(s, TRUE);
	return rc;
}

static void coreinput_stop_redirect(COREINPUT_PLUGIN* coreinput)
{
	WINPR_ASSERT(coreinput);
	if (coreinput->rdpcontext && coreinput->rdpcontext->input &&
	    !freerdp_input_set_events_redirect(coreinput->rdpcontext->input, nullptr, nullptr,
	                                       coreinput))
		WLog_WARN(TAG, "input still goes on the core input channel");
}

/* [MS-RDPECI] 3.2.5.1: the client sends its versions when the channel is open. */
WINPR_ATTR_NODISCARD
static UINT coreinput_on_open(IWTSVirtualChannelCallback* pChannelCallback)
{
	GENERIC_CHANNEL_CALLBACK* callback = (GENERIC_CHANNEL_CALLBACK*)pChannelCallback;
	WINPR_ASSERT(callback);
	WINPR_ASSERT(callback->channel);
	WINPR_ASSERT(callback->channel->Write);

	COREINPUT_PLUGIN* coreinput = (COREINPUT_PLUGIN*)callback->plugin;
	WINPR_ASSERT(coreinput);

	BYTE buffer[COREINPUT_HEADER_LENGTH + 12] = WINPR_C_ARRAY_INIT;
	wStream sbuffer = WINPR_C_ARRAY_INIT;
	wStream* s = Stream_StaticInit(&sbuffer, buffer, sizeof(buffer));
	if (!coreinput_write_init_request(s))
		return ERROR_INTERNAL_ERROR;

	EnterCriticalSection(&coreinput->lock);
	coreinput->callback = callback;
	LeaveCriticalSection(&coreinput->lock);

	WLog_DBG(TAG, "sending Init Request, version 0x%04x", COREINPUT_PROTOCOL_VERSION_1_0);
	return callback->channel->Write(callback->channel, (ULONG)Stream_GetPosition(s), buffer,
	                                nullptr);
}

/* [MS-RDPECI] 3.2.5.2: with a supported version the input goes on the channel from now on. */
WINPR_ATTR_NODISCARD
static UINT coreinput_on_data_received(IWTSVirtualChannelCallback* pChannelCallback, wStream* data)
{
	GENERIC_CHANNEL_CALLBACK* callback = (GENERIC_CHANNEL_CALLBACK*)pChannelCallback;
	WINPR_ASSERT(callback);

	COREINPUT_PLUGIN* coreinput = (COREINPUT_PLUGIN*)callback->plugin;
	WINPR_ASSERT(coreinput);
	WINPR_ASSERT(coreinput->rdpcontext);

	const UINT16 version = coreinput_read_init_response(data);
	if (version != COREINPUT_PROTOCOL_VERSION_1_0)
	{
		WLog_WARN(TAG,
		          "no Init Response with a supported version (0x%04" PRIx16 "), input stays "
		          "in fast-path PDUs",
		          version);
		return CHANNEL_RC_OK;
	}

	if (!freerdp_input_set_events_redirect(coreinput->rdpcontext->input, coreinput_send_events,
	                                       nullptr, coreinput))
		return CHANNEL_RC_NO_MEMORY;
	WLog_DBG(TAG, "core input version 0x%04" PRIx16 ", input goes on the channel", version);
	return CHANNEL_RC_OK;
}

WINPR_ATTR_NODISCARD
static UINT coreinput_on_close(IWTSVirtualChannelCallback* pChannelCallback)
{
	GENERIC_CHANNEL_CALLBACK* callback = (GENERIC_CHANNEL_CALLBACK*)pChannelCallback;
	if (!callback)
		return CHANNEL_RC_OK;

	COREINPUT_PLUGIN* coreinput = (COREINPUT_PLUGIN*)callback->plugin;
	WINPR_ASSERT(coreinput);

	/* fast-path input again; a send in progress holds the lock until its write is done */
	coreinput_stop_redirect(coreinput);
	EnterCriticalSection(&coreinput->lock);
	if (coreinput->callback == callback)
		coreinput->callback = nullptr;
	free(callback);
	LeaveCriticalSection(&coreinput->lock);
	return CHANNEL_RC_OK;
}

WINPR_ATTR_NODISCARD
static UINT init_plugin_cb(GENERIC_DYNVC_PLUGIN* base, rdpContext* rcontext,
                           WINPR_ATTR_UNUSED rdpSettings* settings)
{
	COREINPUT_PLUGIN* coreinput = (COREINPUT_PLUGIN*)base;
	WINPR_ASSERT(coreinput);

	InitializeCriticalSection(&coreinput->lock);
	coreinput->rdpcontext = rcontext;
	return CHANNEL_RC_OK;
}

static void terminate_plugin_cb(GENERIC_DYNVC_PLUGIN* base)
{
	COREINPUT_PLUGIN* coreinput = (COREINPUT_PLUGIN*)base;
	WINPR_ASSERT(coreinput);

	coreinput_stop_redirect(coreinput);
	DeleteCriticalSection(&coreinput->lock);
}

static const IWTSVirtualChannelCallback coreinput_functions = { coreinput_on_data_received,
	                                                            coreinput_on_open,
	                                                            coreinput_on_close, nullptr };

FREERDP_ENTRY_POINT(UINT VCAPITYPE coreinput_DVCPluginEntry(IDRDYNVC_ENTRY_POINTS* pEntryPoints))
{
	return freerdp_generic_DVCPluginEntry(pEntryPoints, TAG, COREINPUT_DVC_CHANNEL_NAME,
	                                      sizeof(COREINPUT_PLUGIN),
	                                      sizeof(GENERIC_CHANNEL_CALLBACK), &coreinput_functions,
	                                      init_plugin_cb, terminate_plugin_cb);
}
