#include "ProtocolDump.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

#include "Editor/Protocol/Cbor.h"
#include "Editor/Protocol/FrameTransport.h"
#include "Editor/Remote/Connection.h"

namespace ned::editor::remote {

namespace {

    // Leading bytes shown of a byte string that isn't UTF-8.
    constexpr std::size_t kHexPreviewBytes = 32;

    bool IsUtf8(std::string_view bytes) {
        for (std::size_t i = 0; i < bytes.size();) {
            const auto  lead   = static_cast<unsigned char>(bytes[i]);
            std::size_t length = 0;
            char32_t    cp     = 0;
            if (lead < 0x80) {
                ++i;
                continue;
            }
            if ((lead & 0xE0) == 0xC0) {
                length = 2;
                cp     = lead & 0x1F;
            }
            else if ((lead & 0xF0) == 0xE0) {
                length = 3;
                cp     = lead & 0x0F;
            }
            else if ((lead & 0xF8) == 0xF0) {
                length = 4;
                cp     = lead & 0x07;
            }
            else {
                return false;
            }
            if (i + length > bytes.size()) {
                return false;
            }
            for (std::size_t k = 1; k < length; ++k) {
                const auto next = static_cast<unsigned char>(bytes[i + k]);
                if ((next & 0xC0) != 0x80) {
                    return false;
                }
                cp = (cp << 6) | (next & 0x3F);
            }
            constexpr char32_t kShortest[] = {0, 0, 0x80, 0x800, 0x10000};
            if (cp < kShortest[length] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                return false;
            }
            i += length;
        }
        return true;
    }

    std::string Hex(std::string_view bytes) {
        static constexpr char kDigits[] = "0123456789abcdef";
        std::string           hex;
        for (const char c : bytes) {
            hex += kDigits[static_cast<unsigned char>(c) >> 4];
            hex += kDigits[static_cast<unsigned char>(c) & 0x0F];
        }
        return hex;
    }

    // The bytes after an agent session's start marker line, or all of them
    // when there's none.
    std::string_view PastPreamble(std::string_view capture) {
        const std::string marker = std::string(kAgentStartMarker) + " ";
        const auto        at     = capture.find(marker);
        if (at == std::string_view::npos || (at != 0 && capture[at - 1] != '\n')) {
            return capture;
        }
        const auto end = capture.find('\n', at);
        return end == std::string_view::npos ? std::string_view() : capture.substr(end + 1);
    }

} // namespace

nlohmann::json ReadableMessage(const nlohmann::json& value) {
    if (value.is_binary()) {
        const auto&            binary = value.get_binary();
        const std::string_view bytes(reinterpret_cast<const char*>(binary.data()), binary.size());
        if (IsUtf8(bytes)) {
            return std::string(bytes);
        }
        return nlohmann::json{{"bytes", bytes.size()}, {"hex", Hex(bytes.substr(0, kHexPreviewBytes))}};
    }
    if (value.is_array()) {
        nlohmann::json array = nlohmann::json::array();
        for (const nlohmann::json& element : value) {
            array.push_back(ReadableMessage(element));
        }
        return array;
    }
    if (value.is_object()) {
        nlohmann::json object = nlohmann::json::object();
        for (const auto& [key, element] : value.items()) {
            object[key] = ReadableMessage(element);
        }
        return object;
    }
    return value;
}

bool DumpProtocol(std::string capture, std::ostream& out, std::ostream& err) {
    std::string bytes(PastPreamble(capture));
    bool        clean = true;
    try {
        std::size_t index = 0;
        for (const std::string& payload : protocol::TakeFrames(bytes)) {
            ++index;
            try {
                out << ReadableMessage(protocol::DecodeCbor(payload)).dump() << '\n';
            }
            catch (const std::exception& e) {
                err << "frame " << index << ": " << e.what() << '\n';
                clean = false;
            }
        }
    }
    catch (const std::runtime_error& e) {
        err << e.what() << '\n';
        return false;
    }
    if (!bytes.empty()) {
        err << "ned: the capture ends partway through a frame (" << bytes.size() << " bytes left)\n";
        return false;
    }
    return clean;
}

int RunProtocolDump(const std::vector<std::string>& paths, std::ostream& out, std::ostream& err) {
    const std::vector<std::string> inputs = paths.empty() ? std::vector<std::string>{"-"} : paths;
    bool                           clean  = true;
    for (const std::string& path : inputs) {
        std::string capture;
        if (path == "-") {
            capture.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
        }
        else {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                err << "ned --protocol-dump: cannot read " << path << '\n';
                clean = false;
                continue;
            }
            capture.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        clean = DumpProtocol(std::move(capture), out, err) && clean;
    }
    return clean ? 0 : 1;
}

} // namespace ned::editor::remote
