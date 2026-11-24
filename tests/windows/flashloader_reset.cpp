// Exercise the patched NXP reset command, without USB or firmware.
#include "blfwk/Command.h"
#include "blfwk/Logging.h"
#include <cassert>
#include <cstdio>

using namespace blfwk;

class ResetTransport : public Packetizer {
public:
    ResetTransport() : Packetizer(nullptr, 0) {}
    status_t sendStatus = kStatus_Success;
    status_t readStatus = kStatus_Success;
    generic_response_packet_t ack{};
    unsigned reads = 0;
    uint32_t size = sizeof ack;
    status_t writePacket(const uint8_t *p, uint32_t n, packet_type_t type) override {
        assert(type == kPacketType_Command && n == sizeof(command_packet_t) && p[0] == kCommandTag_Reset);
        return sendStatus;
    }
    status_t readPacket(uint8_t **p, uint32_t *n, packet_type_t type) override {
        assert(type == kPacketType_Command);
        ++reads;
        *p = reinterpret_cast<uint8_t*>(&ack);
        *n = size;
        return readStatus;
    }
    void abortPacket() override {}
    void sync() override {}
    void finalize() override {}
    void enableSimulatorPump() override {}
    void pumpSimulator() override {}
    void setAborted(bool) override {}
    uint32_t getMaxPacketSize() override { return 64; }
    uint32_t resetStatus() {
        Reset cmd;
        cmd.sendTo(*this);
        assert(cmd.getResponseValues()->size() == 1);
        return cmd.getResponseValues()->at(0);
    }
};

int main() {
    StdoutLogger logger;
    Log::setLogger(&logger);
    ResetTransport usb;
    usb.ack.commandPacket.commandTag = kCommandTag_GenericResponse;
    usb.ack.commandPacket.parameterCount = 2;
    usb.ack.commandTag = kCommandTag_Reset;
    assert(usb.resetStatus() == kStatus_Success);
    usb.sendStatus = kStatus_Fail;
    assert(usb.resetStatus() == kStatus_Fail && usb.reads == 1);
    usb.sendStatus = kStatus_Success;
    usb.ack.status = kStatus_InvalidArgument;
    assert(usb.resetStatus() == kStatus_InvalidArgument);
    usb.size = 1;
    assert(usb.resetStatus() != kStatus_Success);
    usb.readStatus = kStatus_Timeout; // reset write succeeded; device disappeared before ACK
    assert(usb.resetStatus() == kStatus_Success);
    puts("Flashloader reset: 5 cases passed");
}
