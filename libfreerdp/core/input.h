/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Input PDUs
 *
 * Copyright 2011 Marc-Andre Moreau <marcandre.moreau@gmail.com>
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

#ifndef FREERDP_LIB_CORE_INPUT_H
#define FREERDP_LIB_CORE_INPUT_H

#include "rdp.h"
#include "fastpath.h"
#include "message.h"

#include <freerdp/input.h>
#include <freerdp/freerdp.h>
#include <freerdp/api.h>

#include <winpr/stream.h>
#include <winpr/synch.h>

/* Where fast-path input events go instead, see freerdp_input_set_events_redirect. */
struct rdp_input_redirect;

typedef struct
{
	rdpInput common;
	/* Internal */

	rdpInputProxy* proxy;
	wMessageQueue* queue;

	UINT64 lastInputTimestamp;
	UINT16 lastX;
	UINT16 lastY;
	wLog* log;

	CRITICAL_SECTION redirectLock;
	struct rdp_input_redirect* redirect;
} rdp_input_internal;

WINPR_ATTR_NODISCARD
static inline rdp_input_internal* input_cast(rdpInput* input)
{
	union
	{
		rdpInput* pub;
		rdp_input_internal* internal;
	} cnv;

	WINPR_ASSERT(input);
	cnv.pub = input;
	return cnv.internal;
}

WINPR_ATTR_NODISCARD
FREERDP_LOCAL BOOL input_recv(rdpInput* input, wStream* s);

WINPR_ATTR_NODISCARD
FREERDP_LOCAL int input_process_events(rdpInput* input);

WINPR_ATTR_NODISCARD
FREERDP_LOCAL BOOL input_register_client_callbacks(rdpInput* input);

/** Sends the events of a fast-path input PDU with the redirect set by
 *  freerdp_input_set_events_redirect, if there is one: FALSE leaves them to the fast-path PDU.
 *  @param sent Set to the result of the redirect's send when it took the events */
WINPR_ATTR_NODISCARD
FREERDP_LOCAL BOOL input_redirect_events(rdpInput* input, const BYTE* events, size_t length,
                                         size_t count, BOOL* sent);

FREERDP_LOCAL void input_free(rdpInput* input);

WINPR_ATTR_MALLOC(input_free, 1)
WINPR_ATTR_NODISCARD
FREERDP_LOCAL rdpInput* input_new(rdpRdp* rdp);

#endif /* FREERDP_LIB_CORE_INPUT_H */
