#include "Cbor.h"

#include <utility>
#include <vector>

namespace ned::editor::protocol {

namespace {

    using Json = nlohmann::json;

    // nlohmann's DOM builder, plus a depth check that stops the reader
    // before it recurses into a level past the cap.
    class DepthLimitedBuilder {
      public:
        bool null() {
            return Place(Json(nullptr)) != nullptr;
        }
        bool boolean(bool value) {
            return Place(Json(value)) != nullptr;
        }
        bool number_integer(Json::number_integer_t value) {
            return Place(Json(value)) != nullptr;
        }
        bool number_unsigned(Json::number_unsigned_t value) {
            return Place(Json(value)) != nullptr;
        }
        bool number_float(Json::number_float_t value, const Json::string_t&) {
            return Place(Json(value)) != nullptr;
        }
        bool string(Json::string_t& value) {
            return Place(Json(std::move(value))) != nullptr;
        }
        bool binary(Json::binary_t& value) {
            return Place(Json::binary(std::move(value))) != nullptr;
        }
        bool start_object(std::size_t) {
            return Open(Json::object());
        }
        bool key(Json::string_t& value) {
            key_ = std::move(value);
            return true;
        }
        bool end_object() {
            stack_.pop_back();
            return true;
        }
        bool start_array(std::size_t) {
            return Open(Json::array());
        }
        bool end_array() {
            stack_.pop_back();
            return true;
        }
        bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception& error) {
            error_ = error.what();
            return false;
        }

        [[nodiscard]] Json TakeRoot() {
            return std::move(root_);
        }
        [[nodiscard]] const std::string& Error() const noexcept {
            return error_;
        }

      private:
        bool Open(Json container) {
            if (stack_.size() >= kMaxCborDepth) {
                error_ = "CBOR nesting exceeds the depth cap";
                return false;
            }
            Json* placed = Place(std::move(container));
            stack_.push_back(placed);
            return true;
        }

        // A pointer into a container stays valid while it's on the stack:
        // its parent only grows again once it's closed.
        Json* Place(Json value) {
            if (stack_.empty()) {
                root_ = std::move(value);
                return &root_;
            }
            Json& parent = *stack_.back();
            if (parent.is_array()) {
                parent.push_back(std::move(value));
                return &parent.back();
            }
            Json& slot = parent[key_];
            slot       = std::move(value);
            return &slot;
        }

        Json               root_;
        std::vector<Json*> stack_;
        std::string        key_;
        std::string        error_;
    };

} // namespace

std::string EncodeCbor(const Json& value) {
    std::string out;
    Json::to_cbor(value, out);
    return out;
}

Json DecodeCbor(std::string_view bytes) {
    DepthLimitedBuilder builder;
    if (!Json::sax_parse(bytes.begin(), bytes.end(), &builder, Json::input_format_t::cbor)) {
        throw CborError("ned: malformed CBOR: " + (builder.Error().empty() ? std::string("rejected") : builder.Error()));
    }
    return builder.TakeRoot();
}

} // namespace ned::editor::protocol
