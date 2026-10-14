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
#include <winpr/interlocked.h>
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

/* What a send uses. The plugin holds one reference and each redirect set with it another, which
 * the core gives back once that redirect is gone and no send of it is in progress, so a send that
 * is still running when the plugin terminates does not find it freed. */
typedef struct
{
	volatile LONG refs;

	/* guards callback: a send holds it for its write, so the channel does not close under it */
	CRITICAL_SECTION lock;
	GENERIC_CHANNEL_CALLBACK* callback;
} COREINPUT_SENDER;

typedef struct
{
	GENERIC_DYNVC_PLUGIN base;
	rdpContext* rdpcontext;
	COREINPUT_SENDER* sender;
} COREINPUT_PLUGIN;

static void coreinput_sender_unref(void* custom)
{
	COREINPUT_SENDER* sender = custom;
	if (!sender || (InterlockedDecrement(&sender->refs) != 0))
		return;
	DeleteCriticalSection(&sender->lock);
	free(sender);
}

WINPR_ATTR_MALLOC(coreinput_sender_unref, 1)
WINPR_ATTR_NODISCARD
static COREINPUT_SENDER* coreinput_sender_new(void)
{
	COREINPUT_SENDER* sender = calloc(1, sizeof(COREINPUT_SENDER));
	if (!sender)
		return nullptr;
	if (!InitializeCriticalSectionEx(&sender->lock, 0, 0))
	{
		free(sender);
		return nullptr;
	}
	sender->refs = 1;
	return sender;
}

static void coreinput_sender_set_callback(COREINPUT_SENDER* sender,
                                          GENERIC_CHANNEL_CALLBACK* callback)
{
	WINPR_ASSERT(sender);
	EnterCriticalSection(&sender->lock);
	sender->callback = callback;
	LeaveCriticalSection(&sender->lock);
}

/* The events of one fast-path input PDU, as an RDP_CORE_INPUT_CS_KEYBOARD_AND_MOUSE_INPUT_PDU.
 * Called by the core for each batch of input, see freerdp_input_set_events_redirect. */
WINPR_ATTR_NODISCARD
static BOOL coreinput_send_events(void* custom, const BYTE* events, size_t length, size_t count)
{
	COREINPUT_SENDER* sender = custom;
	WINPR_ASSERT(sender);

	wStream* s = Stream_New(nullptr, COREINPUT_HEADER_LENGTH + length);
	if (!s)
		return FALSE;

	BOOL rc = FALSE;
	if (!coreinput_write_input_pdu(s, events, length, count))
		goto out;
	Stream_SealLength(s);

	EnterCriticalSection(&sender->lock);
	GENERIC_CHANNEL_CALLBACK* callback = sender->callback;
	if (callback && callback->channel && callback->channel->Write)
		rc = callback->channel->Write(callback->channel,
		                              WINPR_ASSERTING_INT_CAST(ULONG, Stream_Length(s)),
		                              Stream_Buffer(s), nullptr) == CHANNEL_RC_OK;
	LeaveCriticalSection(&sender->lock);

out:
	Stream_Free(s, TRUE);
	return rc;
}

static void coreinput_stop_redirect(COREINPUT_PLUGIN* coreinput)
{
	WINPR_ASSERT(coreinput);
	if (coreinput->rdpcontext && coreinput->rdpcontext->input &&
	    !freerdp_input_set_events_redirect(coreinput->rdpcontext->input, nullptr, nullptr,
	                                       coreinput->sender))
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

	coreinput_sender_set_callback(coreinput->sender, callback);

	WLog_DBG(TAG, "sending Init Request, version 0x%04" PRIx16,
	         (UINT16)COREINPUT_PROTOCOL_VERSION_1_0);
	return callback->channel->Write(
	    callback->channel, WINPR_ASSERTING_INT_CAST(ULONG, Stream_GetPosition(s)), buffer, nullptr);
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
	WINPR_ASSERT(coreinput->sender);

	const UINT16 version = coreinput_read_init_response(data);
	if (version != COREINPUT_PROTOCOL_VERSION_1_0)
	{
		WLog_WARN(TAG,
		          "no Init Response with a supported version (0x%04" PRIx16 "), input stays "
		          "in fast-path PDUs",
		          version);
		return CHANNEL_RC_OK;
	}

	/* the redirect holds a reference on the sender, the core gives it back */
	(void)InterlockedIncrement(&coreinput->sender->refs);
	if (!freerdp_input_set_events_redirect(coreinput->rdpcontext->input, coreinput_send_events,
	                                       coreinput_sender_unref, coreinput->sender))
	{
		coreinput_sender_unref(coreinput->sender);
		return CHANNEL_RC_NO_MEMORY;
	}
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
	COREINPUT_SENDER* sender = coreinput->sender;
	WINPR_ASSERT(sender);

	/* fast-path input again; a send in progress holds the lock until its write is done */
	coreinput_stop_redirect(coreinput);
	EnterCriticalSection(&sender->lock);
	if (sender->callback == callback)
		sender->callback = nullptr;
	free(callback);
	LeaveCriticalSection(&sender->lock);
	return CHANNEL_RC_OK;
}

WINPR_ATTR_NODISCARD
static UINT init_plugin_cb(GENERIC_DYNVC_PLUGIN* base, rdpContext* rcontext,
                           WINPR_ATTR_UNUSED rdpSettings* settings)
{
	COREINPUT_PLUGIN* coreinput = (COREINPUT_PLUGIN*)base;
	WINPR_ASSERT(coreinput);

	coreinput->sender = coreinput_sender_new();
	if (!coreinput->sender)
		return CHANNEL_RC_NO_MEMORY;
	coreinput->rdpcontext = rcontext;
	return CHANNEL_RC_OK;
}

static void terminate_plugin_cb(GENERIC_DYNVC_PLUGIN* base)
{
	COREINPUT_PLUGIN* coreinput = (COREINPUT_PLUGIN*)base;
	WINPR_ASSERT(coreinput);

	if (!coreinput->sender)
		return;

	/* a send still in progress keeps the sender until it is done */
	coreinput_stop_redirect(coreinput);
	coreinput_sender_set_callback(coreinput->sender, nullptr);
	coreinput_sender_unref(coreinput->sender);
	coreinput->sender = nullptr;
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
