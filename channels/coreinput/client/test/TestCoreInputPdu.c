#include <stdio.h>
#include <string.h>

#include <winpr/crt.h>
#include <winpr/stream.h>

#include "../../common/coreinput_common.h"

static BOOL check_bytes(const char* what, const BYTE* got, size_t gotLength, const BYTE* expected,
                        size_t expectedLength)
{
	if ((gotLength == expectedLength) && (memcmp(got, expected, expectedLength) == 0))
		return TRUE;

	(void)fprintf(stderr, "%s: got %" PRIuz " bytes, expected %" PRIuz "\n", what, gotLength,
	              expectedLength);
	return FALSE;
}

/* [MS-RDPECI] 2.2.3.1: header, then min and max version 1.0 and 8 reserved bytes */
static BOOL test_init_request(void)
{
	const BYTE expected[] = { 0x03, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01,
		                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	BYTE buffer[32] = WINPR_C_ARRAY_INIT;
	wStream sbuffer = WINPR_C_ARRAY_INIT;
	wStream* s = Stream_StaticInit(&sbuffer, buffer, sizeof(buffer));

	if (!coreinput_write_init_request(s))
		return FALSE;
	return check_bytes("init request", buffer, Stream_GetPosition(s), expected, sizeof(expected));
}

/* [MS-RDPECI] 2.2.3.3: the event bytes of a fast-path input PDU are taken as they are. A
 * scancode down (type 0, no flags, key 0x1E) and a mouse move (type 1, PTRFLAGS_MOVE, 100x200). */
static BOOL test_input_pdu(void)
{
	const BYTE events[] = { 0x00, 0x1E, 0x20, 0x00, 0x08, 0x64, 0x00, 0xC8, 0x00 };
	const BYTE expected[] = { 0x03, 0x03, 0x02, 0x00, 0x00, 0x1E, 0x20,
		                      0x00, 0x08, 0x64, 0x00, 0xC8, 0x00 };
	wStream* s = Stream_New(nullptr, 4);
	if (!s)
		return FALSE;

	BOOL rc = coreinput_write_input_pdu(s, events, sizeof(events), 2) &&
	          check_bytes("input pdu", Stream_Buffer(s), Stream_GetPosition(s), expected,
	                      sizeof(expected));

	/* no events, too many events */
	Stream_ResetPosition(s);
	if (coreinput_write_input_pdu(s, events, sizeof(events), 0) ||
	    coreinput_write_input_pdu(s, events, sizeof(events), COREINPUT_MAX_EVENTS + 1) ||
	    coreinput_write_input_pdu(s, events, 0, 1) ||
	    coreinput_write_input_pdu(s, nullptr, sizeof(events), 1))
	{
		(void)fprintf(stderr, "input pdu: invalid batch accepted\n");
		rc = FALSE;
	}
	Stream_Free(s, TRUE);
	return rc;
}

static UINT16 read_response(const BYTE* data, size_t length)
{
	wStream sbuffer = WINPR_C_ARRAY_INIT;
	wStream* s = Stream_StaticConstInit(&sbuffer, data, length);
	return coreinput_read_init_response(s);
}

/* [MS-RDPECI] 2.2.3.2: header, selected version and the server's max version */
static BOOL test_init_response(void)
{
	const BYTE valid[] = { 0x03, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01,
		                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const BYTE badSignature[] = { 0x04, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01,
		                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const BYTE request[] = { 0x03, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01,
		                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const BYTE version2[] = { 0x03, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02,
		                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	if (read_response(valid, sizeof(valid)) != COREINPUT_PROTOCOL_VERSION_1_0)
	{
		(void)fprintf(stderr, "init response: valid response not read\n");
		return FALSE;
	}
	if ((read_response(badSignature, sizeof(badSignature)) != 0) ||
	    (read_response(request, sizeof(request)) != 0) ||
	    (read_response(valid, sizeof(valid) - 1) != 0))
	{
		(void)fprintf(stderr, "init response: invalid response accepted\n");
		return FALSE;
	}
	/* a version this client does not know is passed on, the plugin refuses it */
	if (read_response(version2, sizeof(version2)) != 0x0200)
	{
		(void)fprintf(stderr, "init response: version not passed on\n");
		return FALSE;
	}
	return TRUE;
}

int TestCoreInputPdu(int argc, char* argv[])
{
	WINPR_UNUSED(argc);
	WINPR_UNUSED(argv);

	if (!test_init_request() || !test_input_pdu() || !test_init_response())
		return -1;
	return 0;
}
