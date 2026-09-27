#include "legacy_parser.h"
#include "line_stream.h"
#include "sync_protocol.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace luxflux;

class FakeStream final : public ByteStream {
public:
    std::vector<std::string> chunks;
    std::string writes;
    bool timeout = false;
    unsigned initial_timeouts = 0;
    explicit FakeStream(std::vector<std::string> input) : chunks(std::move(input)) {}
    IoResult receive(std::uint8_t *buffer, std::size_t capacity) override {
        if (initial_timeouts) { --initial_timeouts; return {IoState::Timeout, 0}; }
        if (timeout) return {IoState::Timeout, 0};
        if (chunks.empty()) return {IoState::Disconnected, 0};
        std::string chunk = chunks.front();
        chunks.erase(chunks.begin());
        assert(chunk.size() <= capacity);
        std::memcpy(buffer, chunk.data(), chunk.size());
        return {IoState::Data, chunk.size()};
    }
    IoResult send(const std::uint8_t *data, std::size_t length) override {
        writes.append(reinterpret_cast<const char *>(data), length);
        return {IoState::Data, length};
    }
};

int main()
{
    SyncLimits limits;
    limits.led_count = 8;
    LightFrame frame;
    assert(parseLegacyFrame("8(255,0,0),40\n", limits, frame) == SyncStatus::Ok);
    assert(frame.groups.size() == 1 && frame.duration_ms == 40);
    assert(parseLegacyFrame("4(255,0,0),4(0,255,0),40\n", limits, frame) == SyncStatus::Ok);
    assert(frame.groups.size() == 2 && frame.duration_ms == 40);
    assert(frame.groups[0].pixel_count == 4 && frame.groups[0].color.red == 255);
    assert(frame.groups[1].pixel_count == 4 && frame.groups[1].color.green == 255);
    assert(parseLegacyFrame("7(255,0,0),40", limits, frame) != SyncStatus::Ok);
    assert(parseLegacyFrame("8(256,0,0),40", limits, frame) != SyncStatus::Ok);
    assert(parseLegacyFrame("8(255,0,0),", limits, frame) != SyncStatus::Ok);
    assert(parseLegacyFrame("0(255,0,0),40", limits, frame) != SyncStatus::Ok);
    assert(parseLegacyFrame("8(255,0,0),4294967296", limits, frame) != SyncStatus::Ok);
    assert(parseLegacyFrame("8(255,0,0),40\r\n\0", limits, frame) == SyncStatus::Ok);

    FakeStream fragments({"REA", "DY TO SY", "NC\r\n\0", "4(255,0,0),",
                          "4(0,255,0),40\n", "EOF\n"});
    SequenceStore store(limits);
    SyncProtocol protocol(limits);
    assert(protocol.receive(fragments, "default", store) == SyncStatus::Ok);
    assert(store.active() && store.active()->frames.size() == 1);
    assert(fragments.writes == "SYNC\nACK\nSEQ default\nACK\n");

    std::vector<LightSyncEvent> events;
    SyncProtocol observed(limits, [&events](SyncProgress progress) { events.push_back(progress.event); });
    FakeStream coalesced({"READY TO SYNC\n4(255,0,0),4(0,255,0),40\n"
                          "8(0,0,255),60\nEOF\n"});
    assert(observed.receive(coalesced, "default", store) == SyncStatus::Ok);
    assert(store.active()->frames.size() == 2);
    assert(events.back() == LightSyncEvent::SequenceActivated);
    const auto previous_duration = store.active()->frames[0].duration_ms;

    FakeStream malformed({"READY TO SYNC\n8(256,0,0),40\n"});
    assert(protocol.receive(malformed, "default", store) == SyncStatus::Malformed);
    assert(malformed.writes.find("NACK\n") != std::string::npos);
    assert(store.active()->frames.size() == 2 && store.active()->frames[0].duration_ms == previous_duration);

    SyncLimits short_line = limits;
    short_line.max_payload_length = 10;
    FakeStream overlong({"READY TO SYNC\n8(255,0,0),40\n"});
    assert(SyncProtocol(short_line).receive(overlong, "default", store) == SyncStatus::LimitExceeded);
    assert(store.active()->frames.size() == 2);

    SyncLimits one_frame = limits;
    one_frame.max_frames_per_sequence = 1;
    FakeStream too_many({"READY TO SYNC\n8(255,0,0),40\n8(0,255,0),40\nEOF\n"});
    assert(SyncProtocol(one_frame).receive(too_many, "default", store) == SyncStatus::LimitExceeded);
    assert(store.active()->frames.size() == 2);

    SyncLimits memory = limits;
    memory.max_total_bytes = store.storedBytes() + sizeof(LightFrame) - 1;
    FakeStream no_budget({"READY TO SYNC\n8(255,0,0),40\nEOF\n"});
    assert(SyncProtocol(memory).receive(no_budget, "default", store) == SyncStatus::LimitExceeded);
    assert(store.active()->frames.size() == 2);

    FakeStream disconnect({"READY TO SYNC\n4(255,0,0),"});
    assert(protocol.receive(disconnect, "default", store) == SyncStatus::Disconnected);
    assert(store.active()->frames.size() == 2);

    events.clear();
    FakeStream before_eof({"READY TO SYNC\n8(255,0,0),40\n"});
    assert(observed.receive(before_eof, "default", store) == SyncStatus::Disconnected);
    assert(before_eof.writes == "SYNC\nACK\nSEQ default\nACK\n");
    assert(store.active()->frames.size() == 2 && store.active()->frames[0].duration_ms == previous_duration);
    assert(std::find(events.begin(), events.end(), LightSyncEvent::SequenceActivated) == events.end());

    FakeStream timed_out({});
    timed_out.timeout = true;
    assert(protocol.receive(timed_out, "default", store) == SyncStatus::Timeout);
    assert(timed_out.writes.find("NACK\n") != std::string::npos);

    FakeStream retry({"READY TO SYNC\n8(0,0,255),50\nEOF\n"});
    retry.initial_timeouts = 1;
    assert(protocol.receive(retry, "default", store) == SyncStatus::Ok);
    assert(store.active()->frames.size() == 1);

    FakeStream peer_nack({"NACK\n"});
    assert(protocol.receive(peer_nack, "default", store) == SyncStatus::UnexpectedMessage);
    assert(store.active()->frames.size() == 1);

    FakeStream server({"SYNC\nACK\nSEQ default\nACK\n"});
    assert(protocol.serve(server, store) == SyncStatus::Ok);
    assert(server.writes.find("READY TO SYNC\n") != std::string::npos);
    assert(server.writes.find("EOF\n") != std::string::npos);

    FakeStream bad_ack({"SYNC\nNACK\n"});
    assert(protocol.serve(bad_ack, store) == SyncStatus::UnexpectedMessage);
    assert(bad_ack.writes.find("NACK\n") != std::string::npos);
    std::cout << "sync core tests passed\n";
}
