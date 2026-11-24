// Exercise reset packet handling and PnP observation, without USB or firmware.
#include "win_handoff.h"
#include "blfwk/Logging.h"
#include <cassert>
#include <cstdio>

using namespace blfwk;
using namespace win_handoff;

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
};

static void expectHandoff(Presence before, Presence after, Reply reply, Outcome expected) {
    int probes = 0;
    bool closed = false;
    const auto result = handoff(
        [&] {
            if (!probes++) { assert(!closed); return before; }
            assert(closed);
            return after;
        },
        [&] { assert(probes == 1 && !closed); return reply; },
        [&] { assert(!closed); closed = true; },
        [&] { assert(closed); });
    assert(result == expected && closed);
    assert(probes <= 27);
    if (before != Presence::present) assert(probes == 1);
}

int main() {
    StdoutLogger logger;
    Log::setLogger(&logger);
    ResetTransport usb;
    usb.ack.commandPacket.commandTag = kCommandTag_GenericResponse;
    usb.ack.commandPacket.parameterCount = 2;
    usb.ack.commandTag = kCommandTag_Reset;
    assert(sendReset(usb) == Reply::accepted);
    usb.sendStatus = kStatus_Fail;
    assert(sendReset(usb) == Reply::writeError && usb.reads == 1);
    usb.sendStatus = kStatus_Success;
    usb.ack.status = kStatus_InvalidArgument;
    assert(sendReset(usb) == Reply::rejected);
    usb.size = 1;
    assert(sendReset(usb) == Reply::noAck);
    usb.readStatus = kStatus_Timeout; // reset write succeeded; device disappeared before ACK
    assert(sendReset(usb) == Reply::noAck);
    expectHandoff(Presence::present, Presence::absent, Reply::noAck, Outcome::removed);
    expectHandoff(Presence::present, Presence::present, Reply::accepted, Outcome::unverified);
    expectHandoff(Presence::present, Presence::unknown, Reply::noAck, Outcome::unverified);
    expectHandoff(Presence::absent, Presence::absent, Reply::accepted, Outcome::unverified);
    expectHandoff(Presence::unknown, Presence::absent, Reply::noAck, Outcome::unverified);
    expectHandoff(Presence::present, Presence::absent, Reply::rejected, Outcome::failed);
    expectHandoff(Presence::present, Presence::absent, Reply::writeError, Outcome::unverified);
    expectHandoff(Presence::present, Presence::present, Reply::writeError, Outcome::failed);
    puts("Flashloader: reset replies and Windows removal checks passed");
}
