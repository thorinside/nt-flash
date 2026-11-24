/*
 * WinMM backend (Windows)
 */
#include "midi.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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

static void setWinError(const char* what, MMRESULT code) {
    char text[160] = {0};
    if (midiOutGetErrorTextA(code, text, sizeof text) != MMSYSERR_NOERROR) {
        snprintf(text, sizeof text, "code %u", (unsigned)code);
    }
    snprintf(g_error, sizeof g_error, "%s: %s", what, text);
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
        char what[192];
        snprintf(what, sizeof what, "midiOutOpen(\"%s\") failed (is another application, e.g. a browser, holding the port?)",
                 chosenName);
        setWinError(what, res);
        return -1;
    }

    /* volatile: the driver writes dwFlags from another thread. */
    volatile MIDIHDR* hdr = (volatile MIDIHDR*)calloc(1, sizeof(MIDIHDR));
    if (!hdr) {
        snprintf(g_error, sizeof g_error, "out of memory");
        midiOutClose(handle);
        return -1;
    }
    hdr->lpData = (LPSTR)data;
    hdr->dwBufferLength = (DWORD)len;
    hdr->dwBytesRecorded = (DWORD)len;

    res = midiOutPrepareHeader(handle, (LPMIDIHDR)hdr, sizeof(MIDIHDR));
    if (res != MMSYSERR_NOERROR) {
        setWinError("midiOutPrepareHeader failed", res);
        free((void*)hdr);
        midiOutClose(handle);
        return -1;
    }

    res = midiOutLongMsg(handle, (LPMIDIHDR)hdr, sizeof(MIDIHDR));
    if (res != MMSYSERR_NOERROR) {
        setWinError("midiOutLongMsg failed", res);
        midiOutUnprepareHeader(handle, (LPMIDIHDR)hdr, sizeof(MIDIHDR));
        free((void*)hdr);
        midiOutClose(handle);
        return -1;
    }

    /* Wait for the driver to hand the buffer back before freeing it. */
    int done = 0;
    for (int i = 0; i < 200; i++) {
        if (hdr->dwFlags & MHDR_DONE) {
            done = 1;
            break;
        }
        Sleep(10);
    }

    int rc = 0;
    if (!done) {
        /* The driver still owns the buffer: stop playback and wait for it to be
         * returned. Reporting success here would free memory the driver may
         * still write to. */
        midiOutReset(handle);
        for (int i = 0; i < 200 && !(hdr->dwFlags & MHDR_DONE); i++) Sleep(10);
        snprintf(g_error, sizeof g_error, "timed out sending SysEx to \"%s\"", chosenName);
        rc = -1;
    }

    if (midiOutUnprepareHeader(handle, (LPMIDIHDR)hdr, sizeof(MIDIHDR)) == MIDIERR_STILLPLAYING) {
        if (rc == 0) {
            snprintf(g_error, sizeof g_error, "MIDI driver still holds the SysEx buffer for \"%s\"",
                     chosenName);
            rc = -1;
        }
    }
    free((void*)hdr);
    midiOutClose(handle);
    return rc;
}
