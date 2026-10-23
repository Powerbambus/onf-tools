#include "meta_encoder.h"

#include <cstdio>

#include "json_text.h"
#include "onf/read/version_policy.h"

namespace onf {

std::string format_utc_datetime(const UtcDateTime& t) {
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", static_cast<unsigned>(t.year), static_cast<unsigned>(t.month),
                  static_cast<unsigned>(t.day), static_cast<unsigned>(t.hour), static_cast<unsigned>(t.minute),
                  static_cast<unsigned>(t.second), static_cast<unsigned>(t.millisecond));
    return buffer;
}

std::string format_color(const Color& c) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "#%02X%02X%02X%02X", c.red, c.green, c.blue, c.alpha);
    return buffer;
}

std::string page_directory_name(std::size_t index) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "p%05zu", index);
    return buffer;
}

JsonValue manifest_to_json(const WriteDocument& document) {
    JsonValue manifest = json_object();
    json_add(manifest, "onf_version", json_string(to_string(kImplementedVersion)));
    if (document.generator) json_add(manifest, "generator", json_string(*document.generator));
    json_add(manifest, "title", json_string(document.title));
    json_add(manifest, "created_at", json_string(format_utc_datetime(document.created_at)));
    json_add(manifest, "modified_at", json_string(format_utc_datetime(document.modified_at)));

    JsonValue pages = json_array();
    for (std::size_t i = 0; i < document.pages.size(); ++i) {
        JsonValue entry = json_object();
        json_add(entry, "id", json_string(page_directory_name(i + 1)));
        json_add(entry, "order", json_integer(static_cast<std::int64_t>(i + 1)));
        json_push(pages, std::move(entry));
    }
    json_add(manifest, "pages", std::move(pages));
    return manifest;
}

namespace {

JsonValue span_to_json(const Span& span) {
    JsonValue out = json_object();
    json_add(out, "start", json_integer(span.start));
    json_add(out, "end", json_integer(span.end));
    if (span.bold) json_add(out, "bold", json_bool(*span.bold));
    if (span.italic) json_add(out, "italic", json_bool(*span.italic));
    if (span.underline) json_add(out, "underline", json_bool(*span.underline));
    if (span.strikethrough) json_add(out, "strikethrough", json_bool(*span.strikethrough));
    if (span.color) json_add(out, "color", json_string(format_color(*span.color)));
    if (span.font_size) json_add(out, "font_size", json_integer(*span.font_size));
    if (span.font_family) json_add(out, "font_family", json_string(*span.font_family));
    if (span.generic_family) json_add(out, "generic_family", json_string(*span.generic_family));
    return out;
}

JsonValue text_to_json(const TextObject& text) {
    JsonValue out = json_object();
    json_add(out, "id", json_string(text.id));
    json_add(out, "layer", json_integer(text.layer));
    json_add(out, "x", json_integer(text.x));
    json_add(out, "y", json_integer(text.y));
    json_add(out, "width", json_integer(text.width));
    json_add(out, "height", json_integer(text.height));
    json_add(out, "content", json_string(text.content));
    json_add(out, "font_family", json_string(text.font_family));
    json_add(out, "generic_family", json_string(text.generic_family));
    json_add(out, "font_size", json_integer(text.font_size));
    json_add(out, "color", json_string(format_color(text.color)));
    json_add(out, "bold", json_bool(text.bold));
    json_add(out, "italic", json_bool(text.italic));
    if (!text.spans.empty()) {
        JsonValue spans = json_array();
        for (const Span& span : text.spans) json_push(spans, span_to_json(span));
        json_add(out, "spans", std::move(spans));
    }
    if (text.created_at) json_add(out, "created_at", json_integer(static_cast<std::int64_t>(*text.created_at)));
    return out;
}

JsonValue asset_to_json(const AssetRef& asset) {
    JsonValue out = json_object();
    json_add(out, "id", json_string(asset.id));
    json_add(out, "layer", json_integer(asset.layer));
    json_add(out, "hash", json_string(asset.hash));
    json_add(out, "extension", json_string(asset.extension));
    if (asset.pdf_page) json_add(out, "pdf_page", json_integer(*asset.pdf_page));
    json_add(out, "x", json_integer(asset.x));
    json_add(out, "y", json_integer(asset.y));
    json_add(out, "width", json_integer(asset.width));
    json_add(out, "height", json_integer(asset.height));
    if (asset.created_at) json_add(out, "created_at", json_integer(static_cast<std::int64_t>(*asset.created_at)));
    return out;
}

}  // namespace

JsonValue page_meta_to_json(const PageMeta& meta) {
    JsonValue out = json_object();
    json_add(out, "width", json_integer(meta.width));
    json_add(out, "height", json_integer(meta.height));

    // Every required background field is written explicitly (spec 6.3.1).
    JsonValue background = json_object();
    json_add(background, "type", json_string(meta.background.type));
    json_add(background, "color", json_string(format_color(meta.background.color)));
    if (meta.background.foreground_color) json_add(background, "foreground_color", json_string(format_color(*meta.background.foreground_color)));
    if (meta.background.spacing) json_add(background, "spacing", json_integer(*meta.background.spacing));
    json_add(out, "background", std::move(background));

    JsonValue texts = json_array();
    for (const TextObject& text : meta.text_objects) json_push(texts, text_to_json(text));
    json_add(out, "text_objects", std::move(texts));

    JsonValue assets = json_array();
    for (const AssetRef& asset : meta.assets) json_push(assets, asset_to_json(asset));
    json_add(out, "assets", std::move(assets));
    return out;
}

}  // namespace onf
