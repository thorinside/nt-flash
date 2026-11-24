/*
 * WinMM backend (Windows)
 */
#include "midi.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>

static char g_error[256];

const char* es_midi_backend(void) { return "WinMM"; }

const char* es_midi_error(void) { return g_error[0] ? g_error : NULL; }

static int containsNoCase(const char* haystack, const char* needle) {
    size_t nl = strlen(needle);
    if (nl == 0) return 1;
    for (const char* p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return 1;
    }
    return 0;
}

static void deviceName(UINT id, char* out, size_t n) {
    MIDIOUTCAPSA caps;
    out[0] = '\0';
    if (midiOutGetDevCapsA((UINT_PTR)id, &caps, sizeof caps) == MMSYSERR_NOERROR) {
        snprintf(out, n, "%s", caps.szPname);
    }
}

int es_midi_list_outputs(char* out, int maxPorts) {
    g_error[0] = '\0';
    UINT count = midiOutGetNumDevs();
    int n = 0;
    for (UINT i = 0; i < count && n < maxPorts; i++) {
        deviceName(i, out + (size_t)n * ES_MIDI_PORT_NAME_MAX, ES_MIDI_PORT_NAME_MAX);
        n++;
    }
    return n;
}

int es_midi_send_sysex(const char* portNameSubstr, const uint8_t* data, size_t len) {
    g_error[0] = '\0';
    if (!data || len == 0) {
        snprintf(g_error, sizeof g_error, "empty SysEx message");
        return -1;
    }

    UINT count = midiOutGetNumDevs();
    UINT chosen = (UINT)-1;
    char chosenName[ES_MIDI_PORT_NAME_MAX] = {0};
    for (UINT i = 0; i < count; i++) {
        char name[ES_MIDI_PORT_NAME_MAX];
        deviceName(i, name, sizeof name);
        if (!portNameSubstr || !*portNameSubstr || containsNoCase(name, portNameSubstr)) {
            chosen = i;
            snprintf(chosenName, sizeof chosenName, "%s", name);
            break;
        }
    }
    if (chosen == (UINT)-1) {
        snprintf(g_error, sizeof g_error, "no MIDI output device matching \"%s\"",
                 portNameSubstr && *portNameSubstr ? portNameSubstr : "*");
        return -1;
    }

    HMIDIOUT handle;
    MMRESULT res = midiOutOpen(&handle, chosen, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
        snprintf(g_error, sizeof g_error, "midiOutOpen(\"%s\") failed (%u)", chosenName, (unsigned)res);
        return -1;
    }

    MIDIHDR hdr;
    memset(&hdr, 0, sizeof hdr);
    hdr.lpData = (LPSTR)data;
    hdr.dwBufferLength = (DWORD)len;
    hdr.dwBytesRecorded = (DWORD)len;

    res = midiOutPrepareHeader(handle, &hdr, sizeof hdr);
    if (res != MMSYSERR_NOERROR) {
        snprintf(g_error, sizeof g_error, "midiOutPrepareHeader failed (%u)", (unsigned)res);
        midiOutClose(handle);
        return -1;
    }

    res = midiOutLongMsg(handle, &hdr, sizeof hdr);
    if (res != MMSYSERR_NOERROR) {
        snprintf(g_error, sizeof g_error, "midiOutLongMsg to \"%s\" failed (%u)", chosenName, (unsigned)res);
        midiOutUnprepareHeader(handle, &hdr, sizeof hdr);
        midiOutClose(handle);
        return -1;
    }

    /* Wait for the driver to finish with the buffer before freeing it. */
    for (int i = 0; i < 200 && !(hdr.dwFlags & MHDR_DONE); i++) Sleep(10);

    midiOutUnprepareHeader(handle, &hdr, sizeof hdr);
    midiOutClose(handle);
    return 0;
}
