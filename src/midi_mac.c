/*
 * CoreMIDI backend (macOS)
 */
#include "midi.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char g_error[256];

const char* es_midi_backend(void) { return "CoreMIDI"; }

const char* es_midi_error(void) { return g_error[0] ? g_error : NULL; }

static int nameOf(MIDIObjectRef obj, char* buf, size_t n) {
    CFStringRef s = NULL;
    if (MIDIObjectGetStringProperty(obj, kMIDIPropertyDisplayName, &s) != noErr && s == NULL) {
        if (MIDIObjectGetStringProperty(obj, kMIDIPropertyName, &s) != noErr) return 0;
    }
    if (!s) return 0;
    Boolean ok = CFStringGetCString(s, buf, (CFIndex)n, kCFStringEncodingUTF8);
    CFRelease(s);
    return ok ? 1 : 0;
}

/* Case insensitive substring search that avoids strcasestr's feature macros. */
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

int es_midi_list_outputs(char* out, int maxPorts) {
    g_error[0] = '\0';
    MIDIClientRef client;
    OSStatus st = MIDIClientCreate(CFSTR("es-flash"), NULL, NULL, &client);
    if (st != noErr) {
        snprintf(g_error, sizeof g_error, "MIDIClientCreate failed (%d)", (int)st);
        return -1;
    }
    int n = 0;
    ItemCount count = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < count && n < maxPorts; i++) {
        char* slot = out + (size_t)n * ES_MIDI_PORT_NAME_MAX;
        slot[0] = '\0';
        nameOf(MIDIGetDestination(i), slot, ES_MIDI_PORT_NAME_MAX);
        n++;
    }
    MIDIClientDispose(client);
    return n;
}

int es_midi_send_sysex(const char* portNameSubstr, const uint8_t* data, size_t len) {
    g_error[0] = '\0';
    if (!data || len == 0) {
        snprintf(g_error, sizeof g_error, "empty SysEx message");
        return -1;
    }

    MIDIClientRef client;
    OSStatus st = MIDIClientCreate(CFSTR("es-flash"), NULL, NULL, &client);
    if (st != noErr) {
        snprintf(g_error, sizeof g_error, "MIDIClientCreate failed (%d)", (int)st);
        return -1;
    }

    MIDIEndpointRef dest = 0;
    char destName[ES_MIDI_PORT_NAME_MAX] = {0};
    char buf[ES_MIDI_PORT_NAME_MAX];
    ItemCount count = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < count; i++) {
        MIDIEndpointRef ep = MIDIGetDestination(i);
        buf[0] = '\0';
        if (!nameOf(ep, buf, sizeof buf)) continue;
        if (!portNameSubstr || !*portNameSubstr || containsNoCase(buf, portNameSubstr)) {
            dest = ep;
            snprintf(destName, sizeof destName, "%s", buf);
            break;
        }
    }
    if (!dest) {
        snprintf(g_error, sizeof g_error, "no MIDI output port matching \"%s\"",
                 portNameSubstr && *portNameSubstr ? portNameSubstr : "*");
        MIDIClientDispose(client);
        return -1;
    }

    MIDIPortRef port;
    st = MIDIOutputPortCreate(client, CFSTR("es-flash-out"), &port);
    if (st != noErr) {
        snprintf(g_error, sizeof g_error, "MIDIOutputPortCreate failed (%d)", (int)st);
        MIDIClientDispose(client);
        return -1;
    }

    /* A MIDIPacketList requires 4-byte alignment; a plain Byte array only
     * guarantees 1, so use a union to get correctly aligned storage. */
    union {
        MIDIPacketList list;
        Byte bytes[512];
    } packetStorage;
    if (len > 256) {
        snprintf(g_error, sizeof g_error, "SysEx message too long for a single packet (%zu bytes)", len);
        MIDIPortDispose(port);
        MIDIClientDispose(client);
        return -1;
    }
    MIDIPacketList* list = &packetStorage.list;
    MIDIPacket* packet = MIDIPacketListInit(list);
    packet = MIDIPacketListAdd(list, sizeof packetStorage.bytes, packet, 0, (ByteCount)len, data);
    if (!packet) {
        snprintf(g_error, sizeof g_error, "could not build MIDI packet");
        MIDIPortDispose(port);
        MIDIClientDispose(client);
        return -1;
    }

    st = MIDISend(port, dest, list);
    if (st != noErr) {
        snprintf(g_error, sizeof g_error, "MIDISend to \"%s\" failed (%d)", destName, (int)st);
        MIDIPortDispose(port);
        MIDIClientDispose(client);
        return -1;
    }

    /* MIDISend is asynchronous; give the system a moment to flush before we exit. */
    usleep(200000);

    MIDIPortDispose(port);
    MIDIClientDispose(client);
    return 0;
}
