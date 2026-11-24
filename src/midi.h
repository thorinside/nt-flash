/*
 * Minimal MIDI SysEx output for the Expert Sleepers firmware tools
 *
 * These modules only expose their USB bootloader after being told, over MIDI
 * SysEx, to reboot into bootloader mode. These functions send that message
 * without pulling in a MIDI library.
 *
 * One backend per platform, selected by the Makefile:
 *   midi_mac.c    CoreMIDI   (macOS)
 *   midi_linux.c  ALSA seq   (Linux, libasound2-dev)
 *   midi_win.c    WinMM      (Windows)
 *   midi_none.c   stub       (anything else)
 */
#ifndef ES_MIDI_H
#define ES_MIDI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed stride of the names buffer filled by es_midi_list_outputs(). */
#define ES_MIDI_PORT_NAME_MAX 128

/* Human readable backend name, e.g. "CoreMIDI", "ALSA", "WinMM", "none". */
const char* es_midi_backend(void);

/*
 * Fill out with up to maxPorts output port names, each a NUL terminated string
 * of at most ES_MIDI_PORT_NAME_MAX bytes, laid out at that stride.
 * Returns the number of names written, or -1 on error.
 */
int es_midi_list_outputs(char* out, int maxPorts);

/*
 * Send one SysEx message to the first output port whose name contains
 * portNameSubstr (case insensitive). If portNameSubstr is NULL or empty the
 * first available output port is used.
 * Returns 0 on success, non-zero on failure (see es_midi_error()).
 */
int es_midi_send_sysex(const char* portNameSubstr, const uint8_t* data, size_t len);

/* Description of the last failure, or NULL. */
const char* es_midi_error(void);

#ifdef __cplusplus
}
#endif

#endif /* ES_MIDI_H */
