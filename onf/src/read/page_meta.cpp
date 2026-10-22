#include "onf/read/page_meta.h"

#include <limits>
#include <string>
#include <utility>

#include "onf/read/values.h"

namespace onf {

namespace {

constexpr std::int64_t kCoordMax = std::numeric_limits<Coord>::max();

class Context {
public:
    explicit Context(std::string_view page_id) : page_id_(page_id) {}

    Unexpected<PageError> fail(ErrorCode code, std::string message) const {
        return unexpected(PageError{std::string(page_id_), code, std::move(message), std::nullopt});
    }

    Result<Coord, PageError> positive_coord(const JsonValue& object, std::string_view key, ErrorCode code,
                                            std::string_view where) const {
        const JsonValue* value = object.find(key);
        if (value == nullptr) return fail(code, std::string(where) + ": missing field \"" + std::string(key) + "\"");
        const std::int64_t* number = value->get_integer();
        if (number == nullptr) return fail(code, std::string(where) + ": field \"" + std::string(key) + "\" must be an integer");
        if (*number < 1 || *number > kCoordMax) {
            return fail(code, std::string(where) + ": field \"" + std::string(key) + "\" must be positive and fit into 32 bits");
        }
        return static_cast<Coord>(*number);
    }

    Result<Color, PageError> color(const JsonValue& object, std::string_view key) const {
        const JsonValue* value = object.find(key);
        if (value == nullptr) return fail(ErrorCode::BackgroundInvalidField, "background: missing field \"" + std::string(key) + "\"");
        const std::string* text = value->get_string();
        const auto parsed = text != nullptr ? parse_color(*text) : std::nullopt;
        if (!parsed) return fail(ErrorCode::BackgroundInvalidField, "background: field \"" + std::string(key) + "\" is not a color like #FFFFFFFF");
        return *parsed;
    }

    Result<void, PageError> array(const JsonValue& object, std::string_view key) const {
        const JsonValue* value = object.find(key);
        if (value == nullptr) return fail(ErrorCode::MetaInvalidField, "missing field \"" + std::string(key) + "\"");
        if (value->get_array() == nullptr) return fail(ErrorCode::MetaInvalidField, "field \"" + std::string(key) + "\" must be an array");
        return {};
    }

private:
    std::string_view page_id_;
};

BackgroundKind kind_of(std::string_view type) {
    if (type == "blank") return BackgroundKind::Blank;
    if (type == "ruled") return BackgroundKind::Ruled;
    if (type == "grid") return BackgroundKind::Grid;
    if (type == "dotted") return BackgroundKind::Dotted;
    return BackgroundKind::Unknown;
}

Result<Background, PageError> parse_background(const JsonValue& value, const Context& context) {
    if (value.get_object() == nullptr) return context.fail(ErrorCode::BackgroundInvalidField, "field \"background\" must be an object");

    const JsonValue* type = value.find("type");
    if (type == nullptr || type->get_string() == nullptr) {
        return context.fail(ErrorCode::BackgroundInvalidField, "background: field \"type\" must be a string");
    }

    Background background;
    background.type = *type->get_string();
    background.kind = kind_of(background.type);

    auto color = context.color(value, "color");
    if (!color) return unexpected(std::move(color).error());
    background.color = color.value();

    if (background.kind == BackgroundKind::Ruled || background.kind == BackgroundKind::Grid ||
        background.kind == BackgroundKind::Dotted) {
        auto foreground = context.color(value, "foreground_color");
        if (!foreground) return unexpected(std::move(foreground).error());
        background.foreground_color = foreground.value();

        auto spacing = context.positive_coord(value, "spacing", ErrorCode::BackgroundInvalidField, "background");
        if (!spacing) return unexpected(std::move(spacing).error());
        background.spacing = spacing.value();
    }
    return background;
}

}  // namespace

Result<PageMeta, PageError> parse_page_meta(const JsonValue& root, std::string_view page_id) {
    const Context context(page_id);
    if (root.get_object() == nullptr) return context.fail(ErrorCode::MetaInvalidField, "meta.json must contain a JSON object");

    PageMeta meta;

    auto width = context.positive_coord(root, "width", ErrorCode::MetaInvalidField, "meta.json");
    if (!width) return unexpected(std::move(width).error());
    meta.width = width.value();

    auto height = context.positive_coord(root, "height", ErrorCode::MetaInvalidField, "meta.json");
    if (!height) return unexpected(std::move(height).error());
    meta.height = height.value();

    const JsonValue* background = root.find("background");
    if (background == nullptr) return context.fail(ErrorCode::MetaInvalidField, "missing field \"background\"");
    auto parsed_background = parse_background(*background, context);
    if (!parsed_background) return unexpected(std::move(parsed_background).error());
    meta.background = std::move(parsed_background).value();

    for (const char* key : {"text_objects", "assets"}) {
        auto checked = context.array(root, key);
        if (!checked) return unexpected(std::move(checked).error());
    }
    return meta;
}

Result<PageMeta, PageError> read_page_meta(const ZipArchive& archive, std::string_view page_id) {
    const std::string name = "pages/" + std::string(page_id) + "/meta.json";
    const auto index = archive.find(name);
    if (!index) {
        return unexpected(PageError{std::string(page_id), ErrorCode::PageFileMissing, name + " is missing", std::nullopt});
    }

    auto bytes = archive.read(*index);
    if (!bytes) {
        return unexpected(PageError{std::string(page_id), bytes.error().code, bytes.error().message, std::nullopt});
    }

    auto json = parse_json(bytes.value(), JsonOptions{true});
    if (!json) {
        const JsonError& e = json.error();
        return unexpected(PageError{std::string(page_id), e.code, name + ": " + e.message, e.offset});
    }
    return parse_page_meta(json.value(), page_id);
}

}  // namespace onf
