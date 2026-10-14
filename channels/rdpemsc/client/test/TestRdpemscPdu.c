#include <stdio.h>
#include <string.h>

#include <winpr/crt.h>
#include <winpr/stream.h>

#include "../../common/rdpemsc_common.h"

/* [MS-RDPEMSC] 2.2.3.1, as mstsc sends it to Windows 11: header, then one capability set
 * "CAPS", version 1, 12 bytes */
static BOOL test_caps_advertise(void)
{
	const BYTE expected[] = { 0x01, 0x00, 0x00, 0x00, 0x43, 0x41, 0x50, 0x53,
		                      0x01, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00 };
	BYTE buffer[32] = WINPR_C_ARRAY_INIT;
	wStream sbuffer = WINPR_C_ARRAY_INIT;
	wStream* s = Stream_StaticInit(&sbuffer, buffer, sizeof(buffer));

	if (!rdpemsc_write_caps_advertise(s) || (Stream_GetPosition(s) != sizeof(expected)) ||
	    (memcmp(buffer, expected, sizeof(expected)) != 0))
	{
		(void)fprintf(stderr, "caps advertise: unexpected bytes\n");
		return FALSE;
	}
	return TRUE;
}

static RDP_MOUSE_CURSOR_CAPVERSION read_confirm(const BYTE* data, size_t length, BYTE* pduType)
{
	wStream sbuffer = WINPR_C_ARRAY_INIT;
	wStream* s = Stream_StaticConstInit(&sbuffer, data, length);
	BYTE updateType = 0;
	if (!rdpemsc_read_header(s, pduType, &updateType))
		return RDP_MOUSE_CURSOR_CAPVERSION_INVALID;
	return rdpemsc_read_caps_confirm(s);
}

/* [MS-RDPEMSC] 2.2.3.2, as Windows 11 answers mstsc */
static BOOL test_caps_confirm(void)
{
	const BYTE windows[] = { 0x02, 0x00, 0x00, 0x00, 0x43, 0x41, 0x50, 0x53,
		                     0x01, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00 };
	const BYTE badSignature[] = { 0x02, 0x00, 0x00, 0x00, 0x43, 0x41, 0x50, 0x54,
		                          0x01, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00 };
	const BYTE version2[] = { 0x02, 0x00, 0x00, 0x00, 0x43, 0x41, 0x50, 0x53,
		                      0x02, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00 };
	const BYTE sizeTooLarge[] = { 0x02, 0x00, 0x00, 0x00, 0x43, 0x41, 0x50, 0x53,
		                          0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00 };
	BYTE pduType = 0;

	if ((read_confirm(windows, sizeof(windows), &pduType) != RDP_MOUSE_CURSOR_CAPVERSION_1) ||
	    (pduType != PDUTYPE_SC_CAPS_CONFIRM))
	{
		(void)fprintf(stderr, "caps confirm: Windows' confirm not read\n");
		return FALSE;
	}
	if ((read_confirm(badSignature, sizeof(badSignature), &pduType) !=
	     RDP_MOUSE_CURSOR_CAPVERSION_INVALID) ||
	    (read_confirm(version2, sizeof(version2), &pduType) !=
	     RDP_MOUSE_CURSOR_CAPVERSION_INVALID) ||
	    (read_confirm(sizeTooLarge, sizeof(sizeTooLarge), &pduType) !=
	     RDP_MOUSE_CURSOR_CAPVERSION_INVALID) ||
	    (read_confirm(windows, sizeof(windows) - 1, &pduType) !=
	     RDP_MOUSE_CURSOR_CAPVERSION_INVALID))
	{
		(void)fprintf(stderr, "caps confirm: invalid confirm accepted\n");
		return FALSE;
	}
	return TRUE;
}

/* [MS-RDPEMSC] 2.2.3.3: the start of the first pointer update Windows 11 sent mstsc, a 64x64
 * 32 bpp pointer (type 0x0B) whose payload is that of a fast-path new pointer update */
static BOOL test_mouseptr_update_header(void)
{
	const BYTE windows[] = { 0x03, 0x0b, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
		                     0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x40, 0x00 };
	wStream sbuffer = WINPR_C_ARRAY_INIT;
	wStream* s = Stream_StaticConstInit(&sbuffer, windows, sizeof(windows));
	BYTE pduType = 0;
	BYTE updateType = 0;

	if (!rdpemsc_read_header(s, &pduType, &updateType) || (pduType != PDUTYPE_SC_MOUSEPTR_UPDATE) ||
	    (updateType != TS_UPDATETYPE_MOUSEPTR_POINTER) ||
	    (Stream_GetPosition(s) != RDPEMSC_HEADER_SIZE))
	{
		(void)fprintf(stderr, "pointer update: header not read\n");
		return FALSE;
	}

	const BYTE known[] = {
		TS_UPDATETYPE_MOUSEPTR_SYSTEM_NULL, TS_UPDATETYPE_MOUSEPTR_SYSTEM_DEFAULT,
		TS_UPDATETYPE_MOUSEPTR_POSITION,    TS_UPDATETYPE_MOUSEPTR_CACHED,
		TS_UPDATETYPE_MOUSEPTR_POINTER,     TS_UPDATETYPE_MOUSEPTR_LARGE_POINTER
	};
	for (size_t x = 0; x < ARRAYSIZE(known); x++)
	{
		if (!rdpemsc_is_mouseptr_update_type(known[x]))
		{
			(void)fprintf(stderr, "pointer update: type 0x%02" PRIx8 " refused\n", known[x]);
			return FALSE;
		}
	}
	/* the color pointer (0x09) is not an [MS-RDPEMSC] update type */
	if (rdpemsc_is_mouseptr_update_type(0x09) || rdpemsc_is_mouseptr_update_type(0x00))
	{
		(void)fprintf(stderr, "pointer update: unknown type accepted\n");
		return FALSE;
	}
	return TRUE;
}

int TestRdpemscPdu(int argc, char* argv[])
{
	WINPR_UNUSED(argc);
	WINPR_UNUSED(argv);

	if (!test_caps_advertise() || !test_caps_confirm() || !test_mouseptr_update_header())
		return -1;
	return 0;
}
