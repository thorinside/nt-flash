/*
 * No-op MIDI backend (platforms without an implementation)
 */
#include "midi.h"

#include <stdio.h>

static char g_error[256] = "MIDI SysEx is not supported on this platform";

const char* es_midi_backend(void) { return "none"; }

const char* es_midi_error(void) { return g_error; }

int es_midi_list_outputs(char* out, int maxPorts) {
    (void)out;
    (void)maxPorts;
    snprintf(g_error, sizeof g_error, "MIDI SysEx is not supported on this platform");
    return -1;
}

int es_midi_send_sysex(const char* portNameSubstr, const uint8_t* data, size_t len) {
    (void)portNameSubstr;
    (void)data;
    (void)len;
    snprintf(g_error, sizeof g_error, "MIDI SysEx is not supported on this platform");
    return -1;
}
