#include "onf/read/page_meta.h"

#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

#include "onf/limits.h"
#include "onf/primitives/utf8.h"
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

        // top_margin is optional and only read for these three types (spec 6.3.1).
        if (const JsonValue* margin = value.find("top_margin")) {
            const std::int64_t* number = margin->get_integer();
            if (number == nullptr || *number < 0 || *number > kCoordMax) {
                return context.fail(ErrorCode::BackgroundInvalidField, "background: field \"top_margin\" must be an integer from 0 to 2147483647");
            }
            background.top_margin = static_cast<Coord>(*number);
        }
    }
    return background;
}


// Typed access to the fields of one JSON object. Every failure names the object (`where`) and the field.
class Fields {
public:
    Fields(const Context& context, const JsonValue& object, std::string where, ErrorCode code = ErrorCode::MetaInvalidField)
        : context_(context), object_(object), where_(std::move(where)), code_(code) {}

    Unexpected<PageError> bad(std::string_view key, std::string_view problem, ErrorCode code) const {
        return context_.fail(code, where_ + ": field \"" + std::string(key) + "\" " + std::string(problem));
    }
    Unexpected<PageError> bad(std::string_view key, std::string_view problem) const { return bad(key, problem, code_); }

    Result<std::string, PageError> string(std::string_view key) const {
        const JsonValue* value = object_.find(key);
        if (value == nullptr) return bad(key, "is missing");
        if (value->get_string() == nullptr) return bad(key, "must be a string");
        return *value->get_string();
    }

    Result<bool, PageError> boolean(std::string_view key) const {
        const JsonValue* value = object_.find(key);
        if (value == nullptr) return bad(key, "is missing");
        if (value->get_bool() == nullptr) return bad(key, "must be a boolean");
        return *value->get_bool();
    }

    Result<std::int64_t, PageError> integer(std::string_view key, std::int64_t min, std::int64_t max) const {
        const JsonValue* value = object_.find(key);
        if (value == nullptr) return bad(key, "is missing");
        if (value->get_integer() == nullptr) return bad(key, "must be an integer");
        if (*value->get_integer() < min || *value->get_integer() > max) return bad(key, "is out of range");
        return *value->get_integer();
    }

    Result<Coord, PageError> coord(std::string_view key) const {
        auto value = integer(key, std::numeric_limits<Coord>::min(), kCoordMax);
        if (!value) return unexpected(std::move(value).error());
        return static_cast<Coord>(value.value());
    }

    Result<Coord, PageError> positive_coord(std::string_view key) const {
        auto value = integer(key, 1, kCoordMax);
        if (!value) return unexpected(std::move(value).error());
        return static_cast<Coord>(value.value());
    }

    Result<Color, PageError> color(std::string_view key) const {
        auto text = string(key);
        if (!text) return unexpected(std::move(text).error());
        const auto parsed = parse_color(text.value());
        if (!parsed) return bad(key, "is not a color like #FFFFFFFF");
        return *parsed;
    }

    // The layer field (spec 5.6): an integer from 1 to 65535.
    Result<std::uint16_t, PageError> layer() const {
        const JsonValue* value = object_.find("layer");
        if (value == nullptr) return bad("layer", "is missing");
        if (value->get_integer() == nullptr) return bad("layer", "must be an integer");
        const std::int64_t layer = *value->get_integer();
        if (layer < limits::kMinLayer || layer > limits::kMaxLayer) {
            return bad("layer", "must be between 1 and 65535", ErrorCode::LayerInvalid);
        }
        return static_cast<std::uint16_t>(layer);
    }

    // Optional created_at (spec 5.5): absent means unknown.
    Result<std::optional<MsTime>, PageError> created_at() const {
        if (object_.find("created_at") == nullptr) return std::optional<MsTime>{};
        auto value = integer("created_at", 0, limits::kJsonMaxInteger);
        if (!value) return unexpected(std::move(value).error());
        return std::optional<MsTime>{static_cast<MsTime>(value.value())};
    }

    bool has(std::string_view key) const { return object_.find(key) != nullptr; }

private:
    const Context& context_;
    const JsonValue& object_;
    std::string where_;
    ErrorCode code_;
};

#define ONF_TRY_ASSIGN(target, expression)                              \
    do {                                                                \
        auto onf_result = (expression);                                 \
        if (!onf_result) return unexpected(std::move(onf_result).error()); \
        (target) = std::move(onf_result).value();                       \
    } while (false)

// One entry of the spans array of a text object (spec 8.3.1). Unknown keys are ignored.
Result<std::vector<Span>, PageError> parse_spans(const JsonValue& value, std::size_t content_length,
                                                 const Context& context, const std::string& where) {
    const JsonValue::Array* array = value.get_array();
    if (array == nullptr) return context.fail(ErrorCode::SpansInvalid, where + ": field \"spans\" must be an array");
    if (array->size() > limits::kMaxSpans) return context.fail(ErrorCode::SpansInvalid, where + ": more than 65535 spans");

    std::vector<Span> spans;
    spans.reserve(array->size());
    std::uint64_t previous_end = 0;

    for (const JsonValue& entry : *array) {
        if (entry.get_object() == nullptr) return context.fail(ErrorCode::SpansInvalid, where + ": a span must be an object");
        const Fields fields(context, entry, where + " span", ErrorCode::SpansInvalid);

        Span span;
        std::int64_t start = 0, end = 0;
        ONF_TRY_ASSIGN(start, fields.integer("start", 0, std::numeric_limits<std::int32_t>::max()));
        ONF_TRY_ASSIGN(end, fields.integer("end", 0, std::numeric_limits<std::int32_t>::max()));
        if (end <= start) return fields.bad("end", "must be greater than start");
        if (static_cast<std::uint64_t>(start) < previous_end) return fields.bad("start", "overlaps or precedes the previous span");
        if (static_cast<std::uint64_t>(end) > content_length) return fields.bad("end", "is beyond the end of the content");
        previous_end = static_cast<std::uint64_t>(end);
        span.start = static_cast<std::uint32_t>(start);
        span.end = static_cast<std::uint32_t>(end);

        for (const auto& [key, target] : {std::pair<const char*, std::optional<bool>*>{"bold", &span.bold},
                                          {"italic", &span.italic},
                                          {"underline", &span.underline},
                                          {"strikethrough", &span.strikethrough}}) {
            if (!fields.has(key)) continue;
            auto flag = fields.boolean(key);
            if (!flag) return unexpected(std::move(flag).error());
            *target = flag.value();
        }
        if (fields.has("color")) {
            auto color = fields.color("color");
            if (!color) return unexpected(std::move(color).error());
            span.color = color.value();
        }
        if (fields.has("font_size")) {
            auto size = fields.integer("font_size", 1, std::numeric_limits<std::int32_t>::max());
            if (!size) return unexpected(std::move(size).error());
            span.font_size = static_cast<std::int32_t>(size.value());
        }
        if (fields.has("font_family")) {
            auto family = fields.string("font_family");
            if (!family) return unexpected(std::move(family).error());
            span.font_family = std::move(family).value();
        }
        if (fields.has("generic_family")) {
            auto family = fields.string("generic_family");
            if (!family) return unexpected(std::move(family).error());
            span.generic_family = std::move(family).value();
        }
        if (span.font_family && !span.generic_family) return fields.bad("generic_family", "is required together with font_family");
        spans.push_back(std::move(span));
    }
    return spans;
}

Result<TextObject, PageError> parse_text_object(const JsonValue& value, const Context& context) {
    if (value.get_object() == nullptr) return context.fail(ErrorCode::MetaInvalidField, "text_objects: an entry must be an object");
    const Fields fields(context, value, "text object");

    TextObject text;
    ONF_TRY_ASSIGN(text.id, fields.string("id"));
    const Fields named(context, value, "text object \"" + text.id + "\"");

    ONF_TRY_ASSIGN(text.layer, named.layer());
    ONF_TRY_ASSIGN(text.created_at, named.created_at());
    ONF_TRY_ASSIGN(text.x, named.coord("x"));
    ONF_TRY_ASSIGN(text.y, named.coord("y"));
    ONF_TRY_ASSIGN(text.width, named.positive_coord("width"));
    ONF_TRY_ASSIGN(text.height, named.positive_coord("height"));
    ONF_TRY_ASSIGN(text.content, named.string("content"));
    ONF_TRY_ASSIGN(text.font_family, named.string("font_family"));
    ONF_TRY_ASSIGN(text.generic_family, named.string("generic_family"));  // an unknown value is not an error (spec 8.4.1)
    std::int64_t font_size = 0;
    ONF_TRY_ASSIGN(font_size, named.integer("font_size", 1, std::numeric_limits<std::int32_t>::max()));
    text.font_size = static_cast<std::int32_t>(font_size);
    ONF_TRY_ASSIGN(text.color, named.color("color"));
    ONF_TRY_ASSIGN(text.bold, named.boolean("bold"));
    ONF_TRY_ASSIGN(text.italic, named.boolean("italic"));

    if (const JsonValue* spans = value.find("spans")) {
        ONF_TRY_ASSIGN(text.spans, parse_spans(*spans, count_code_points(text.content), context, "text object \"" + text.id + "\""));
    }
    return text;
}

bool is_lowercase_sha256(const std::string& text) noexcept {
    if (text.size() != 64) return false;
    for (const char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

Result<AssetRef, PageError> parse_asset(const JsonValue& value, const Context& context) {
    if (value.get_object() == nullptr) return context.fail(ErrorCode::MetaInvalidField, "assets: an entry must be an object");
    const Fields fields(context, value, "asset");

    AssetRef asset;
    ONF_TRY_ASSIGN(asset.id, fields.string("id"));
    const Fields named(context, value, "asset \"" + asset.id + "\"");

    ONF_TRY_ASSIGN(asset.layer, named.layer());
    ONF_TRY_ASSIGN(asset.created_at, named.created_at());
    ONF_TRY_ASSIGN(asset.hash, named.string("hash"));
    if (!is_lowercase_sha256(asset.hash)) return named.bad("hash", "must be 64 lowercase hexadecimal digits", ErrorCode::InvalidIdentifier);
    ONF_TRY_ASSIGN(asset.extension, named.string("extension"));  // an unknown extension is not an error (spec 9.2.2)
    ONF_TRY_ASSIGN(asset.x, named.coord("x"));
    ONF_TRY_ASSIGN(asset.y, named.coord("y"));
    ONF_TRY_ASSIGN(asset.width, named.positive_coord("width"));
    ONF_TRY_ASSIGN(asset.height, named.positive_coord("height"));

    // pdf_page is required for PDF and ignored for every other extension (spec 9.3.2).
    if (asset.extension == "pdf") {
        std::int64_t page = 0;
        ONF_TRY_ASSIGN(page, named.integer("pdf_page", 1, limits::kMaxPdfPage));
        asset.pdf_page = static_cast<std::int32_t>(page);
    } else if (const JsonValue* crop = value.find("crop")) {
        // crop is for images only; on a PDF placement it is ignored, even if it is not valid (spec 9.3.2).
        // Whether the region lies inside the image needs the image header and is checked when the asset is
        // loaded (spec 9.4.5), where a region outside is a corrupt asset and no page error.
        if (crop->get_object() == nullptr) {
            return context.fail(ErrorCode::CropInvalid, "asset \"" + asset.id + "\": field \"crop\" must be an object");
        }
        const Fields members(context, *crop, "asset \"" + asset.id + "\" crop", ErrorCode::CropInvalid);
        constexpr std::int64_t kMax = limits::kMaxCropValue;
        Crop region;
        std::int64_t x = 0, y = 0, width = 0, height = 0;
        ONF_TRY_ASSIGN(x, members.integer("x", 0, kMax));
        ONF_TRY_ASSIGN(y, members.integer("y", 0, kMax));
        ONF_TRY_ASSIGN(width, members.integer("width", 1, kMax));
        ONF_TRY_ASSIGN(height, members.integer("height", 1, kMax));
        region.x = static_cast<std::int32_t>(x);
        region.y = static_cast<std::int32_t>(y);
        region.width = static_cast<std::int32_t>(width);
        region.height = static_cast<std::int32_t>(height);
        asset.crop = region;
    }
    return asset;
}

template <typename T, typename Parser>
Result<std::vector<T>, PageError> parse_list(const JsonValue& root, std::string_view key, Parser parser, const Context& context) {
    const JsonValue* value = root.find(key);
    if (value == nullptr) return context.fail(ErrorCode::MetaInvalidField, "missing field \"" + std::string(key) + "\"");
    const JsonValue::Array* array = value->get_array();
    if (array == nullptr) return context.fail(ErrorCode::MetaInvalidField, "field \"" + std::string(key) + "\" must be an array");

    std::vector<T> items;
    items.reserve(array->size());
    std::unordered_set<std::string> ids;
    for (const JsonValue& entry : *array) {
        auto item = parser(entry, context);
        if (!item) return unexpected(std::move(item).error());
        if (!ids.insert(item.value().id).second) {
            return context.fail(ErrorCode::DuplicateObjectId, std::string(key) + ": duplicate id \"" + item.value().id + "\"");
        }
        items.push_back(std::move(item).value());
    }
    return items;
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

    auto text_objects = parse_list<TextObject>(root, "text_objects", parse_text_object, context);
    if (!text_objects) return unexpected(std::move(text_objects).error());
    meta.text_objects = std::move(text_objects).value();

    auto assets = parse_list<AssetRef>(root, "assets", parse_asset, context);
    if (!assets) return unexpected(std::move(assets).error());
    meta.assets = std::move(assets).value();
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
