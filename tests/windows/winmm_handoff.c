/* Exercise the real Windows send/cleanup code with deterministic WinMM returns.
 * No installed MIDI device, service restart, or timed sleeps are required. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>

enum Case { NORMAL, BUSY_ONCE, TIMEOUT_RELEASED, NEVER_RELEASED, SUBMIT_ERROR, CLOSE_ERROR };
static enum Case scenario;
static DWORD elapsed;
static unsigned int prepares, resets, closes, releases, frees;
static MIDIHDR *loan;
static const BYTE message[] = {0xf0, 0x7d, 0x01, 0xf7};

static MMRESULT open_port(LPHMIDIOUT out, UINT device, DWORD_PTR cb, DWORD_PTR instance, DWORD flags) {
    assert(device == 0 && !cb && !instance && flags == CALLBACK_NULL);
    *out = (HMIDIOUT)(UINT_PTR)1;
    return MMSYSERR_NOERROR;
}
static MMRESULT fake_caps(UINT_PTR id, LPMIDIOUTCAPSA dest, UINT bytes) {
    assert(id == 0 && bytes == sizeof *dest);
    strcpy(dest->szPname, "test port");
    return MMSYSERR_NOERROR;
}
static MMRESULT prepare(HMIDIOUT out, MIDIHDR *h, UINT size) {
    assert(out && size == sizeof *h && !loan && h->dwBufferLength == sizeof message);
    assert(h->lpData != (LPSTR)message && memcmp(h->lpData, message, sizeof message) == 0);
    loan = h;
    ++prepares;
    return MMSYSERR_NOERROR;
}
static MMRESULT submit(HMIDIOUT out, MIDIHDR *h, UINT size) {
    assert(out && h == loan && size == sizeof *h);
    return scenario == SUBMIT_ERROR ? MMSYSERR_ERROR : MMSYSERR_NOERROR;
}
static MMRESULT release(HMIDIOUT out, MIDIHDR *h, UINT size) {
    assert(out && h == loan && size == sizeof *h);
    assert(memcmp(h->lpData, message, sizeof message) == 0);
    ++releases;
    if (scenario == NEVER_RELEASED || (scenario == TIMEOUT_RELEASED && !resets) ||
        (scenario == BUSY_ONCE && releases == 1)) return MIDIERR_STILLPLAYING;
    loan = NULL;
    return MMSYSERR_NOERROR;
}
static MMRESULT reset_port(HMIDIOUT out) {
    assert(out && loan);
    ++resets;
    return MMSYSERR_NOERROR;
}
static MMRESULT close_port(HMIDIOUT out) {
    assert(out);
    ++closes;
    if (loan) return MIDIERR_STILLPLAYING;
    return scenario == CLOSE_ERROR ? MMSYSERR_ERROR : MMSYSERR_NOERROR;
}
static void checked_free(void *address) {
    assert(!loan || (address != loan && address != loan->lpData));
    ++frees;
    free(address);
}

#define midiOutGetNumDevs() 1
#define midiOutGetDevCapsA fake_caps
#define midiOutOpen open_port
#define midiOutPrepareHeader prepare
#define midiOutLongMsg submit
#define midiOutUnprepareHeader release
#define midiOutReset reset_port
#define midiOutClose close_port
#define midiOutGetErrorTextA(code, text, len) MMSYSERR_ERROR
#define GetTickCount64() ((ULONGLONG)elapsed)
#define Sleep(ms) (elapsed += (ms))
#define free checked_free
#include "midi_win.c"
#undef free

int main(void) {
    for (int i = NORMAL; i <= CLOSE_ERROR; ++i) {
        scenario = (enum Case)i;
        elapsed = prepares = resets = closes = releases = frees = 0;
        int ok = es_midi_send_sysex("test port", message, sizeof message) == 0;
        assert(ok == (scenario == NORMAL || scenario == BUSY_ONCE));
        assert((es_midi_error() == NULL) == ok);
        assert(prepares == 1 && closes == 1 && releases >= 1);
        assert(resets == (unsigned)(scenario == TIMEOUT_RELEASED || scenario == NEVER_RELEASED));
        assert(frees == (unsigned)(scenario != NEVER_RELEASED));
        if (scenario == NEVER_RELEASED) {
            assert(loan && memcmp(loan->lpData, message, sizeof message) == 0);
            free(loan); /* simulated driver finally released it */
            loan = NULL;
        }
    }
    puts("WinMM handoff: 6 cases passed");
    return 0;
}
