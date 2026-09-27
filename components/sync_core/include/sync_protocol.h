#pragma once
#include <functional>
#include <string>
#include <vector>
#include "legacy_parser.h"
#include "line_stream.h"

namespace luxflux {
class SequenceStore {
public:
    explicit SequenceStore(SyncLimits limits) : limits_(limits) {}
    const LightSequence *find(const std::string &name) const;
    const LightSequence *active() const;
    const std::string &activeName() const { return active_name_; }
    std::size_t storedBytes() const;
    SyncStatus replace(std::string name, LightSequence sequence);
private:
    struct NamedSequence { std::string name; LightSequence sequence; };
    SyncLimits limits_;
    std::vector<NamedSequence> sequences_;
    std::string active_name_;
};

using SyncEventCallback = std::function<void(SyncProgress)>;
using SyncFrameCallback = std::function<void(const LightFrame &)>;

class SyncProtocol {
public:
    explicit SyncProtocol(SyncLimits limits, SyncEventCallback callback = {},
                          SyncFrameCallback frame_callback = {})
        : limits_(limits), callback_(std::move(callback)), frame_callback_(std::move(frame_callback)) {}
    SyncStatus receive(ByteStream &socket, const std::string &sequence_name, SequenceStore &store);
    SyncStatus serve(ByteStream &socket, const SequenceStore &store);
private:
    SyncLimits limits_;
    SyncEventCallback callback_;
    SyncFrameCallback frame_callback_;
    void emit(LightSyncEvent event, std::uint32_t completed = 0,
              std::uint32_t total = 0) const;
};
}
