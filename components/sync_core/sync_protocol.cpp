#include "sync_protocol.h"
#include <cctype>

namespace luxflux {
namespace {
bool validName(const std::string &name)
{
    if (name.empty() || name.size() > 24) return false;
    for (unsigned char ch : name)
        if (!std::isalnum(ch) && ch != '_' && ch != '-') return false;
    return true;
}
SyncStatus unexpected(LineStream &line)
{
    line.write("NACK");
    return SyncStatus::UnexpectedMessage;
}
}

const LightSequence *SequenceStore::find(const std::string &name) const
{
    for (const auto &item : sequences_) if (item.name == name) return &item.sequence;
    return nullptr;
}

const LightSequence *SequenceStore::active() const { return find(active_name_); }

std::size_t SequenceStore::storedBytes() const
{
    std::size_t bytes = 0;
    for (const auto &item : sequences_) bytes += estimatedSequenceBytes(item.sequence);
    return bytes;
}

SyncStatus SequenceStore::replace(std::string name, LightSequence sequence)
{
    if (!validName(name) || !validateSequence(sequence, limits_)) return SyncStatus::Malformed;
    const std::size_t new_bytes = estimatedSequenceBytes(sequence);
    std::size_t aggregate = new_bytes;
    for (const auto &entry : sequences_)
        if (entry.name != name) {
            const std::size_t bytes = estimatedSequenceBytes(entry.sequence);
            if (bytes > limits_.max_total_bytes - aggregate) return SyncStatus::LimitExceeded;
            aggregate += bytes;
        }
    for (auto &entry : sequences_) {
        if (entry.name == name) {
            entry.sequence.frames.swap(sequence.frames);
            active_name_ = std::move(name);
            return SyncStatus::Ok;
        }
    }
    if (sequences_.size() >= limits_.max_sequences) return SyncStatus::LimitExceeded;
    sequences_.push_back({name, std::move(sequence)});
    active_name_ = std::move(name);
    return SyncStatus::Ok;
}

void SyncProtocol::emit(LightSyncEvent event, std::uint32_t completed,
                        std::uint32_t total) const
{
    if (callback_) callback_({event, completed, total});
}

SyncStatus SyncProtocol::receive(ByteStream &socket, const std::string &sequence_name,
                                  SequenceStore &store)
{
    // Client handshake: SYNC -> READY TO SYNC -> ACK -> SEQ <name>.
    if (!validName(sequence_name)) return SyncStatus::Malformed;
    LineStream line(socket, limits_);
    emit(LightSyncEvent::LoadStarted);
    auto fail = [this, &line](SyncStatus status) {
        if (status != SyncStatus::Disconnected) line.write("NACK");
        emit(LightSyncEvent::TransferFailed);
        return status;
    };
    SyncStatus status = line.write("SYNC");
    if (status != SyncStatus::Ok) return fail(status);
    std::string message;
    status = line.read(message);
    if (status != SyncStatus::Ok) return fail(status);
    if (message != "READY TO SYNC") return fail(SyncStatus::UnexpectedMessage);
    status = line.write("ACK");
    if (status != SyncStatus::Ok) return fail(status);
    status = line.write("SEQ " + sequence_name);
    if (status != SyncStatus::Ok) return fail(status);
    emit(LightSyncEvent::TransferStarted);

    // Keep new frames separate from the active sequence until EOF validates
    // the whole transfer. A bad frame leaves the previous sequence intact.
    LightSequence staged;
    std::size_t bytes = 0;
    for (;;) {
        status = line.read(message);
        if (status != SyncStatus::Ok) return fail(status);
        if (message == "NACK") return fail(SyncStatus::NotFound);
        if (message == "EOF") break;
        if (staged.frames.size() >= limits_.max_frames_per_sequence) return fail(SyncStatus::LimitExceeded);
        LightFrame frame;
        status = parseLegacyFrame(message, limits_, frame);
        if (status != SyncStatus::Ok) return fail(status);
        if (frame_callback_) frame_callback_(frame);
        const std::size_t extra = sizeof(LightFrame) + frame.groups.size() * sizeof(PixelGroup);
        const std::size_t stored = store.storedBytes();
        if (stored > limits_.max_total_bytes || bytes > limits_.max_total_bytes - stored ||
            extra > limits_.max_total_bytes - stored - bytes)
            return fail(SyncStatus::LimitExceeded);
        bytes += extra;
        staged.frames.push_back(std::move(frame));
        emit(LightSyncEvent::FrameReceived, static_cast<std::uint32_t>(staged.frames.size()));
        status = line.write("ACK"); // Acknowledge this complete, valid frame.
        if (status != SyncStatus::Ok) return fail(status);
    }
    if (!validateSequence(staged, limits_)) return fail(SyncStatus::Malformed);
    // Only now does the new named sequence become active for playback.
    status = store.replace(sequence_name, std::move(staged));
    if (status != SyncStatus::Ok) return fail(status);
    emit(LightSyncEvent::TransferCompleted);
    emit(LightSyncEvent::SequenceActivated);
    return SyncStatus::Ok;
}

SyncStatus SyncProtocol::serve(ByteStream &socket, const SequenceStore &store)
{
    // The ESP32 SoftAP-server role uses this path. The Python test server has
    // its own implementation of the same handshake and frame format.
    LineStream line(socket, limits_);
    std::string message;
    SyncStatus status = line.read(message);
    if (status != SyncStatus::Ok) return status;
    if (message != "SYNC") return unexpected(line);
    status = line.write("READY TO SYNC");
    if (status != SyncStatus::Ok) return status;
    status = line.read(message);
    if (status != SyncStatus::Ok) return status;
    if (message != "ACK") return unexpected(line);
    status = line.read(message);
    if (status != SyncStatus::Ok) return status;
    const std::string name = message.compare(0, 4, "SEQ ") == 0 ? message.substr(4) : message;
    if (!validName(name)) return unexpected(line);
    const LightSequence *sequence = store.find(name);
    if (!sequence) { line.write("NACK"); return SyncStatus::NotFound; }
    emit(LightSyncEvent::TransferStarted, 0, static_cast<std::uint32_t>(sequence->frames.size()));
    for (std::size_t i = 0; i < sequence->frames.size(); ++i) {
        std::string record;
        status = formatLegacyFrame(sequence->frames[i], limits_, record);
        if (status != SyncStatus::Ok) return status;
        status = line.write(record);
        if (status != SyncStatus::Ok) return status;
        status = line.read(message);
        if (status != SyncStatus::Ok) return status;
        if (message != "ACK") return unexpected(line);
        emit(LightSyncEvent::ProgressUpdated, static_cast<std::uint32_t>(i + 1),
             static_cast<std::uint32_t>(sequence->frames.size()));
    }
    status = line.write("EOF");
    if (status != SyncStatus::Ok) return status;
    emit(LightSyncEvent::TransferCompleted);
    return SyncStatus::Ok;
}
}
