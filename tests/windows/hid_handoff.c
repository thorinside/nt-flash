// Use a private named pipe to exercise real overlapped I/O without USB.
#include <assert.h>
#include <stdio.h>
#include <windows.h>

static OVERLAPPED *cancelled;
static unsigned drains;

static BOOL observe_cancel(HANDLE h, OVERLAPPED *io) {
    assert(io && !cancelled);
    cancelled = io;
    return CancelIoEx(h, io);
}
static BOOL observe_result(HANDLE h, OVERLAPPED *io, DWORD *n, BOOL wait) {
    BOOL ok = GetOverlappedResult(h, io, n, wait);
    if (io == cancelled) {
        assert(wait && (ok || GetLastError() == ERROR_OPERATION_ABORTED));
        cancelled = NULL;
        ++drains;
    }
    return ok;
}
#define CancelIoEx observe_cancel
#define GetOverlappedResult observe_result
#include "hid-windows.c"

int main(void) {
    char name[100];
    snprintf(name, sizeof name, "\\\\.\\pipe\\es-flash-io-%lu", GetCurrentProcessId());
    HANDLE server = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
    assert(server != INVALID_HANDLE_VALUE);
    HANDLE peer = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    assert(peer != INVALID_HANDLE_VALUE);

    hid_device *dev = new_hid_device();
    dev->device_handle = server;
    dev->input_report_length = 8;
    dev->read_buf = malloc(dev->input_report_length);
    unsigned char reply[8];
    assert(hid_read_timeout(dev, reply, sizeof reply, 0) == 0 && dev->read_pending);

    // Write much more than the pipe quota. With nobody reading it this
    // times out. Only the write may be cancelled, not the outstanding read.
    const size_t size = 1024 * 1024;
    unsigned char *data = calloc(1, size);
    assert(data && hid_write_timeout(dev, data, size, 0) == -1);
    free(data);
    assert(drains == 1 && !cancelled);

    DWORD written;
    assert(WriteFile(peer, "\x01", 1, &written, NULL) && written == 1);
    assert(hid_read_timeout(dev, reply, sizeof reply, 1000) == 1 && reply[0] == 1);
    assert(hid_read_timeout(dev, reply, sizeof reply, 0) == 0 && dev->read_pending);
    hid_close(dev);
    assert(drains == 2 && !cancelled);
    CloseHandle(peer);
    puts("HID handoff: timed-out write and close with pending read passed");
}
