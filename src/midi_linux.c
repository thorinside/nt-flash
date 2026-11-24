/*
 * ALSA sequencer backend (Linux)
 *
 * Requires libasound2-dev (Debian/Ubuntu) / alsa-lib-devel (Fedora) /
 * alsa-lib (Arch) at build time.
 */
#include "midi.h"

#include <alsa/asoundlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char g_error[256];

const char* es_midi_backend(void) { return "ALSA"; }

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

static snd_seq_t* openSeq(void) {
    snd_seq_t* seq = NULL;
    int err = snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0);
    if (err < 0) {
        snprintf(g_error, sizeof g_error, "snd_seq_open failed: %s", snd_strerror(err));
        return NULL;
    }
    snd_seq_set_client_name(seq, "es-flash");
    return seq;
}

/* Port name as shown to other ALSA clients (see --list-midi-ports). */
static void portName(snd_seq_t* seq, int client, int port, char* out, size_t n) {
    snd_seq_port_info_t* info;
    snd_seq_port_info_alloca(&info);
    out[0] = '\0';
    if (snd_seq_get_any_port_info(seq, client, port, info) < 0) return;
    const char* pname = snd_seq_port_info_get_name(info);
    if (pname && *pname) {
        snprintf(out, n, "%s", pname);
        return;
    }
    snd_seq_client_info_t* cinfo;
    snd_seq_client_info_alloca(&cinfo);
    if (snd_seq_get_any_client_info(seq, client, cinfo) >= 0) {
        const char* cname = snd_seq_client_info_get_name(cinfo);
        if (cname && *cname) snprintf(out, n, "%s", cname);
    }
}

int es_midi_list_outputs(char* out, int maxPorts) {
    g_error[0] = '\0';
    snd_seq_t* seq = openSeq();
    if (!seq) return -1;

    int n = 0;
    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t* pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pinfo);
            if (!(caps & SND_SEQ_PORT_CAP_WRITE) || !(caps & SND_SEQ_PORT_CAP_SUBS_WRITE)) continue;
            if (n >= maxPorts) break;
            char* slot = out + (size_t)n * ES_MIDI_PORT_NAME_MAX;
            portName(seq, snd_seq_port_info_get_client(pinfo), snd_seq_port_info_get_port(pinfo), slot,
                     ES_MIDI_PORT_NAME_MAX);
            n++;
        }
    }

    snd_seq_close(seq);
    return n;
}

int es_midi_send_sysex(const char* portNameSubstr, const uint8_t* data, size_t len) {
    g_error[0] = '\0';
    if (!data || len == 0) {
        snprintf(g_error, sizeof g_error, "empty SysEx message");
        return -1;
    }

    snd_seq_t* seq = openSeq();
    if (!seq) return -1;

    int srcPort = snd_seq_create_simple_port(seq, "out",
                                             SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                             SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (srcPort < 0) {
        snprintf(g_error, sizeof g_error, "snd_seq_create_simple_port failed: %s", snd_strerror(srcPort));
        snd_seq_close(seq);
        return -1;
    }

    int destClient = -1, destPort = -1;
    char destName[ES_MIDI_PORT_NAME_MAX] = {0};
    int ownClient = snd_seq_client_id(seq);

    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t* pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (destClient < 0 && snd_seq_query_next_client(seq, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        /* Never pick the ALSA System client (timer) or our own port. */
        if (client == SND_SEQ_CLIENT_SYSTEM || client == ownClient) continue;
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pinfo);
            if (!(caps & SND_SEQ_PORT_CAP_WRITE) || !(caps & SND_SEQ_PORT_CAP_SUBS_WRITE)) continue;
            char name[ES_MIDI_PORT_NAME_MAX];
            int port = snd_seq_port_info_get_port(pinfo);
            portName(seq, client, port, name, sizeof name);
            if (!portNameSubstr || !*portNameSubstr || containsNoCase(name, portNameSubstr)) {
                destClient = client;
                destPort = port;
                snprintf(destName, sizeof destName, "%s", name);
                break;
            }
        }
    }

    if (destClient < 0) {
        snprintf(g_error, sizeof g_error, "no ALSA MIDI port matching \"%s\"",
                 portNameSubstr && *portNameSubstr ? portNameSubstr : "*");
        snd_seq_close(seq);
        return -1;
    }

    /* Subscribing makes ALSA open the destination's RawMIDI port; without it a
     * normally idle hardware port receives nothing. */
    int subErr = snd_seq_connect_to(seq, srcPort, destClient, destPort);
    if (subErr < 0) {
        snprintf(g_error, sizeof g_error, "could not subscribe to \"%s\": %s", destName,
                 snd_strerror(subErr));
        snd_seq_close(seq);
        return -1;
    }

    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, srcPort);
    snd_seq_ev_set_dest(&ev, destClient, destPort);
    /* Without this the event targets kernel queue 0, which this client has not
     * joined, and the kernel rejects it with -EINVAL. */
    snd_seq_ev_set_direct(&ev);
    snd_seq_ev_set_sysex(&ev, len, (void*)data);

    int err = snd_seq_event_output_direct(seq, &ev);
    if (err < 0) {
        snprintf(g_error, sizeof g_error, "snd_seq_event_output_direct to \"%s\" failed: %s", destName,
                 snd_strerror(err));
        snd_seq_close(seq);
        return -1;
    }

    snd_seq_close(seq);
    return 0;
}
