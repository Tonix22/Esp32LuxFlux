#include "nvm_store.h"
#include "legacy_parser.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <cctype>
#include <vector>

namespace luxflux::nvm {
namespace {
constexpr char kNamespace[] = "luxflux_seq";
constexpr char kKey[] = "active";
constexpr std::uint8_t kVersion = 1;

void put16(std::vector<std::uint8_t> &data, std::uint16_t value)
{
    data.push_back(static_cast<std::uint8_t>(value));
    data.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put32(std::vector<std::uint8_t> &data, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) data.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

bool get16(const std::vector<std::uint8_t> &data, std::size_t &at, std::uint16_t &value)
{
    if (at + 2 > data.size()) return false;
    value = static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8));
    at += 2;
    return true;
}

bool get32(const std::vector<std::uint8_t> &data, std::size_t &at, std::uint32_t &value)
{
    if (at + 4 > data.size()) return false;
    value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(data[at++]) << (8 * i);
    return true;
}

std::uint32_t checksum(const std::vector<std::uint8_t> &data, std::size_t length)
{
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < length; ++i) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

bool validName(const std::string &name)
{
    if (name.empty() || name.size() > 24) return false;
    for (unsigned char ch : name)
        if (!std::isalnum(ch) && ch != '_' && ch != '-') return false;
    return true;
}
}

esp_err_t NvmStore::saveLedConfiguration(const LedConfiguration &configuration)
{
    (void)configuration;
    // TODO: validate fields, then persist a versioned record using ESP-IDF NVS.
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t NvmStore::loadLedConfiguration(LedConfiguration &configuration)
{
    (void)configuration;
    // TODO: read and validate the stored record before changing the output.
    // Leaving configuration untouched ensures this stub cannot supply bad data.
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t NvmStore::saveSequence(const std::string &name, const LightSequence &sequence,
                                 const SyncLimits &limits)
{
    if (!validName(name) || !validateSequence(sequence, limits)) return ESP_ERR_INVALID_ARG;
    std::vector<std::uint8_t> data = {'L', 'X', 'S', kVersion,
                                      static_cast<std::uint8_t>(name.size())};
    put16(data, static_cast<std::uint16_t>(sequence.frames.size()));
    data.insert(data.end(), name.begin(), name.end());
    for (const auto &frame : sequence.frames) {
        std::string record;
        if (formatLegacyFrame(frame, limits, record) != SyncStatus::Ok || record.size() > UINT16_MAX)
            return ESP_ERR_INVALID_ARG;
        put16(data, static_cast<std::uint16_t>(record.size()));
        data.insert(data.end(), record.begin(), record.end());
    }
    put32(data, checksum(data, data.size()));
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) return result;
    nvs_handle_t handle;
    result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_blob(handle, kKey, data.data(), data.size());
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}

esp_err_t NvmStore::loadSequence(std::string &name, LightSequence &sequence,
                                 const SyncLimits &limits)
{
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) return result;
    nvs_handle_t handle;
    result = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (result != ESP_OK) return result;
    std::size_t size = 0;
    result = nvs_get_blob(handle, kKey, nullptr, &size);
    if (result != ESP_OK) {
        nvs_close(handle);
        return result == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : result;
    }
    // The textual encoding is bounded by roughly four bytes per in-memory
    // sequence byte. Reject oversized records before allocating any buffer.
    if (size < 11 || size > 4 * limits.max_total_bytes + 1024) {
        nvs_close(handle);
        return ESP_ERR_INVALID_SIZE;
    }
    std::vector<std::uint8_t> data(size);
    result = nvs_get_blob(handle, kKey, data.data(), &size);
    nvs_close(handle);
    if (result != ESP_OK) return result;
    data.resize(size);
    if (data.size() < 11 || data[0] != 'L' || data[1] != 'X' ||
        data[2] != 'S' || data[3] != kVersion) return ESP_ERR_INVALID_RESPONSE;
    std::size_t crc_at = data.size() - 4;
    std::size_t at = crc_at;
    std::uint32_t stored_crc;
    if (!get32(data, at, stored_crc) || stored_crc != checksum(data, crc_at))
        return ESP_ERR_INVALID_CRC;
    const std::size_t name_length = data[4];
    at = 5;
    std::uint16_t frame_count;
    if (!get16(data, at, frame_count) || at + name_length > crc_at ||
        frame_count == 0 || frame_count > limits.max_frames_per_sequence)
        return ESP_ERR_INVALID_RESPONSE;
    std::string loaded_name(reinterpret_cast<const char *>(data.data() + at), name_length);
    at += name_length;
    if (!validName(loaded_name)) return ESP_ERR_INVALID_RESPONSE;
    LightSequence loaded;
    for (std::uint16_t i = 0; i < frame_count; ++i) {
        std::uint16_t length;
        if (!get16(data, at, length) || length == 0 || length > limits.max_payload_length ||
            at + length > crc_at) return ESP_ERR_INVALID_RESPONSE;
        LightFrame frame;
        const auto *record = reinterpret_cast<const char *>(data.data() + at);
        if (parseLegacyFrame(std::string_view(record, length), limits, frame) != SyncStatus::Ok)
            return ESP_ERR_INVALID_RESPONSE;
        loaded.frames.push_back(std::move(frame));
        at += length;
        if (estimatedSequenceBytes(loaded) > limits.max_total_bytes)
            return ESP_ERR_INVALID_SIZE;
    }
    if (at != crc_at || !validateSequence(loaded, limits)) return ESP_ERR_INVALID_RESPONSE;
    name = std::move(loaded_name);
    sequence = std::move(loaded);
    return ESP_OK;
}

} // namespace luxflux::nvm
