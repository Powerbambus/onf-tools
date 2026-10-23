#include <map>
#include <set>
#include <utility>
#include <string>
#include <unordered_set>
#include <variant>

#include "meta_encoder.h"
#include "onf/limits.h"
#include "onf/primitives/sha256.h"
#include "onf/primitives/utf8.h"
#include "onf/read/asset_check.h"
#include "onf/read/values.h"
#include "onf/write/writer.h"

namespace onf {

namespace {

// Each check returns an empty optional if the rule holds, or the message of the broken rule.
using Problem = std::optional<std::string>;

Problem problem(const std::string& where, const std::string& what, const char* section) {
    return where + ": " + what + " (spec " + section + ")";
}

bool valid_utf8(const std::string& text) {
    return is_valid_utf8(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

Problem check_text(const std::string& where, const std::string& text, std::size_t max_code_points = limits::kJsonMaxStringCodePoints) {
    if (!valid_utf8(text)) return problem(where, "is not valid UTF-8", "2.3.5");
    if (count_code_points(text) > max_code_points) return problem(where, "is too long", "12.6");
    return std::nullopt;
}

Problem check_time(const std::string& where, const UtcDateTime& time) {
    const auto parsed = parse_utc_datetime(format_utc_datetime(time));
    if (!parsed || !(*parsed == time)) return problem(where, "is not a valid UTC date and time", "6.1.2");
    return std::nullopt;
}

bool known_generic_family(const std::string& value) { return value == "sans-serif" || value == "serif" || value == "monospace"; }

Problem check_background(const std::string& where, const Background& background) {
    const std::string& type = background.type;
    if (type != "blank" && type != "ruled" && type != "grid" && type != "dotted") {
        return problem(where, "background type \"" + type + "\" is not one of blank, ruled, grid, dotted", "6.3.1");
    }
    if (type == "blank" && background.top_margin != 0) return problem(where, "top_margin is only for ruled, grid and dotted", "6.3.1");
    if (background.top_margin < 0) return problem(where, "top_margin must be at least 0", "6.3.1");
    if (type != "blank") {
        if (!background.foreground_color) return problem(where, "background foreground_color is required for this type", "6.3.1");
        if (!background.spacing || *background.spacing < 1) return problem(where, "background spacing must be positive", "6.3.1");
    }
    return std::nullopt;
}

Problem check_created_at(const std::string& where, const std::optional<MsTime>& created_at) {
    if (created_at && *created_at > static_cast<MsTime>(limits::kJsonMaxInteger)) return problem(where, "created_at is larger than 9007199254740991", "2.3.5");
    return std::nullopt;
}

Problem check_layer(const std::string& where, std::uint16_t layer) {
    if (layer < limits::kMinLayer || layer > limits::kMaxLayer) return problem(where, "layer must be between 1 and 65535", "5.6");
    return std::nullopt;
}

Problem check_stroke(const std::string& where, const Stroke& stroke) {
    if (auto p = check_layer(where, stroke.layer)) return p;
    if (auto p = check_created_at(where, stroke.created_at)) return p;
    if (stroke.tool_type_raw > static_cast<std::uint16_t>(ToolType::Highlighter)) return problem(where, "tool type must be 0 to 3", "7.4.2");
    if (stroke.base_width == 0) return problem(where, "base_width must be greater than 0", "7.4.1");
    if ((stroke.flags.raw() & ~0x007Fu) != 0) return problem(where, "reserved flag bits must be 0", "7.4.3");

    const SampleColumns& s = stroke.samples;
    const std::size_t n = s.size();
    if (n == 0) return problem(where, "sample_count must be greater than 0", "7.4.1");
    if (n > 0xFFFFFFFFull) return problem(where, "too many samples", "7.4.1");
    if (s.y.size() != n || s.pressure.size() != n) return problem(where, "the sample columns have different lengths", "7.5.1");
    if (stroke.flags.no_timestamp() ? !s.timestamp.empty() : s.timestamp.size() != n) {
        return problem(where, "timestamp column must be empty with NO_TIMESTAMP and complete otherwise", "7.5.1");
    }
    if (stroke.flags.has_tilt() ? (s.tilt_x.size() != n || s.tilt_y.size() != n) : (!s.tilt_x.empty() || !s.tilt_y.empty())) {
        return problem(where, "tilt columns must match the HAS_TILT flag", "7.5.1");
    }
    if (stroke.flags.has_azimuth() ? s.azimuth.size() != n : !s.azimuth.empty()) return problem(where, "azimuth column must match the HAS_AZIMUTH flag", "7.5.1");

    for (std::size_t i = 0; i < n; ++i) {
        if (stroke.flags.no_pressure() && s.pressure[i] != 255) return problem(where, "NO_PRESSURE requires pressure 255 in every sample", "7.5.4");
        if (!stroke.flags.no_timestamp() && i > 0 && s.timestamp[i] < s.timestamp[i - 1]) return problem(where, "timestamps must not decrease", "5.3");
        if (stroke.flags.has_tilt() && (s.tilt_x[i] < -90 || s.tilt_x[i] > 90 || s.tilt_y[i] < -90 || s.tilt_y[i] > 90)) {
            return problem(where, "tilt must be between -90 and 90", "7.5.1");
        }
        if (stroke.flags.has_azimuth() && s.azimuth[i] > 35999) return problem(where, "azimuth must be between 0 and 35999", "7.5.1");
    }
    return std::nullopt;
}

Problem check_shape(const std::string& where, const Shape& shape) {
    if (auto p = check_layer(where, shape.layer)) return p;
    if (auto p = check_created_at(where, shape.created_at)) return p;
    if (shape.tool_type_raw > static_cast<std::uint16_t>(ToolType::Highlighter)) return problem(where, "a shape needs tool type 0 to 3", "7.9.2");
    if (shape.base_width == 0) return problem(where, "base_width must be greater than 0", "7.9.2");

    if (const Ellipse* e = std::get_if<Ellipse>(&shape.geometry)) {
        if (e->rx <= 0 || e->ry <= 0) return problem(where, "rx and ry must be greater than 0", "7.9.4");
        if (e->rotation > limits::kMaxRotation) return problem(where, "rotation must be 0 to 35999", "7.9.6");
    } else if (const Rectangle* r = std::get_if<Rectangle>(&shape.geometry)) {
        if (r->width <= 0 || r->height <= 0) return problem(where, "width and height must be greater than 0", "7.9.4");
        if (r->rotation > limits::kMaxRotation) return problem(where, "rotation must be 0 to 35999", "7.9.6");
    }
    return std::nullopt;
}

Problem check_spans(const std::string& where, const TextObject& text) {
    if (text.spans.size() > limits::kMaxSpans) return problem(where, "more than 65535 spans", "8.3.1");
    const std::size_t length = count_code_points(text.content);
    std::uint64_t previous_end = 0;
    for (const Span& span : text.spans) {
        if (span.end <= span.start) return problem(where, "a span must end after it starts", "8.3.1");
        if (span.start < previous_end) return problem(where, "spans must be sorted and must not overlap", "8.3.1");
        if (span.end > length) return problem(where, "a span reaches beyond the content", "8.3.1");
        previous_end = span.end;
        if (span.font_size && *span.font_size <= 0) return problem(where, "a span font_size must be positive", "8.3.1");
        if (span.font_family && !span.generic_family) return problem(where, "a span with font_family needs generic_family", "8.3.1");
        if (span.font_family) {
            if (auto p = check_text(where + " span font_family", *span.font_family)) return p;
        }
    }
    return std::nullopt;
}

Problem check_text_object(const std::string& where, const TextObject& text) {
    if (auto p = check_text(where + " id", text.id)) return p;
    if (auto p = check_layer(where, text.layer)) return p;
    if (auto p = check_created_at(where, text.created_at)) return p;
    if (text.width <= 0 || text.height <= 0) return problem(where, "width and height must be greater than 0", "8.3");
    if (text.font_size <= 0) return problem(where, "font_size must be greater than 0", "8.3");
    if (auto p = check_text(where + " content", text.content, limits::kJsonMaxContentCodePoints)) return p;
    if (auto p = check_text(where + " font_family", text.font_family)) return p;
    if (!known_generic_family(text.generic_family)) return problem(where, "generic_family must be sans-serif, serif or monospace", "8.4.1");
    return check_spans(where, text);
}

}  // namespace

std::optional<std::string> check_writable(const WriteDocument& document) {
    if (document.pages.empty() || document.pages.size() > limits::kMaxPages) return problem("document", "needs 1 to 99999 pages", "4.2");
    if (auto p = check_text("title", document.title)) return p;
    if (document.generator) {
        if (auto p = check_text("generator", *document.generator)) return p;
    }
    if (auto p = check_time("created_at", document.created_at)) return p;
    if (auto p = check_time("modified_at", document.modified_at)) return p;

    // The files that will be embedded, by archive name, with the pixel size of an image (0 for a pdf).
    std::map<std::string, std::pair<std::int64_t, std::int64_t>> embedded;
    for (const WriteAsset& asset : document.assets) {
        const std::string name = sha256_hex(asset.bytes) + "." + asset.extension;
        if (!is_supported_asset_extension(asset.extension)) return problem("asset " + name, "format is not one of webp, png, jpg, pdf", "9.2.2");
        const AssetCheck check = check_asset_content(asset.extension, asset.bytes);
        if (check.status != AssetStatus::Ok) return problem("asset " + name, check.detail, "9.2.3");
        embedded[name] = {check.width.value_or(0), check.height.value_or(0)};
    }
    if (document.thumbnail) {
        const AssetCheck check = check_asset_content("png", *document.thumbnail);
        if (check.status != AssetStatus::Ok) return problem("thumbnail.png", check.detail, "4.3");
        if (*check.width > 256 || *check.height > 256) return problem("thumbnail.png", "the longest side must not exceed 256 pixels", "4.3");
    }

    for (std::size_t index = 0; index < document.pages.size(); ++index) {
        const WritePage& page = document.pages[index];
        const std::string where = "page " + page_directory_name(index + 1);
        const PageMeta& meta = page.meta;

        if (meta.width < 1 || meta.height < 1) return problem(where, "width and height must be positive", "6.2.2");
        if (auto p = check_background(where, meta.background)) return p;

        std::unordered_set<std::uint32_t> ink_ids;
        for (const InkItem& item : page.ink) {
            if (const Stroke* stroke = std::get_if<Stroke>(&item)) {
                const std::string w = where + " stroke " + std::to_string(stroke->stroke_id);
                if (!ink_ids.insert(stroke->stroke_id).second) return problem(w, "stroke_id is not unique", "7.4.1");
                if (auto p = check_stroke(w, *stroke)) return p;
            } else {
                const Shape& shape = std::get<Shape>(item);
                const std::string w = where + " shape " + std::to_string(shape.stroke_id);
                if (!ink_ids.insert(shape.stroke_id).second) return problem(w, "stroke_id is not unique", "7.4.1");
                if (auto p = check_shape(w, shape)) return p;
            }
        }

        std::unordered_set<std::string> text_ids;
        for (const TextObject& text : meta.text_objects) {
            const std::string w = where + " text object \"" + text.id + "\"";
            if (!text_ids.insert(text.id).second) return problem(w, "id is not unique", "8.3");
            if (auto p = check_text_object(w, text)) return p;
        }

        std::unordered_set<std::string> asset_ids;
        for (const AssetRef& asset : meta.assets) {
            const std::string w = where + " asset \"" + asset.id + "\"";
            if (auto p = check_text(w + " id", asset.id)) return p;
            if (!asset_ids.insert(asset.id).second) return problem(w, "id is not unique", "9.3.2");
            if (auto p = check_layer(w, asset.layer)) return p;
            if (auto p = check_created_at(w, asset.created_at)) return p;
            if (asset.width <= 0 || asset.height <= 0) return problem(w, "width and height must be greater than 0", "9.3.2");
            if (!is_supported_asset_extension(asset.extension)) return problem(w, "extension is not one of webp, png, jpg, pdf", "9.2.2");
            if (asset.extension == "pdf" ? (!asset.pdf_page || *asset.pdf_page < 1) : asset.pdf_page.has_value()) {
                return problem(w, "pdf_page is required for pdf (1 or more) and must not be written otherwise", "9.3.2");
            }
            const auto file = embedded.find(asset.hash + "." + asset.extension);
            if (file == embedded.end()) {
                return problem(w, "the file " + asset.hash + "." + asset.extension + " is not among the assets to embed", "9.5");
            }
            if (asset.crop) {
                if (asset.extension == "pdf") return problem(w, "crop must not be written for a pdf", "9.3.2");
                const Crop& crop = *asset.crop;
                if (crop.x < 0 || crop.y < 0) return problem(w, "crop x and y must be at least 0", "9.3.2");
                if (crop.width < 1 || crop.height < 1) return problem(w, "crop width and height must be at least 1", "9.3.2");
                if (static_cast<std::int64_t>(crop.x) + crop.width > file->second.first ||
                    static_cast<std::int64_t>(crop.y) + crop.height > file->second.second) {
                    return problem(w, "the crop region must lie inside the image", "9.4.5");
                }
            }
        }
    }
    return std::nullopt;
}

}  // namespace onf
