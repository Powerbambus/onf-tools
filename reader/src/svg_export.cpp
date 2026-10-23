#include "svg_export.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "base64.h"

namespace onf_reader {

namespace {

using namespace onf;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::string num(double value) {
    if (std::abs(value) < 1e-9) return "0";
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << value;
    std::string text = out.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

std::string escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string rgb(const Color& color) {
    return "rgb(" + std::to_string(color.red) + "," + std::to_string(color.green) + "," + std::to_string(color.blue) + ")";
}

double alpha_of(const Color& color) { return color.alpha / 255.0; }

double lerp(double min, double max, double t) { return min + (max - min) * t; }

// Content type order inside a layer (spec 3.4): assets, text objects, ink.
struct LayerContent {
    std::vector<const AssetRef*> assets;
    std::vector<const TextObject*> texts;
    std::vector<const InkItem*> ink;
};

// ---------------------------------------------------------------------------
// Background, spec 6.3.2
// ---------------------------------------------------------------------------

std::string background(const PageMeta& meta) {
    const Background& bg = meta.background;
    std::ostringstream out;
    out << "<rect x=\"0\" y=\"0\" width=\"" << meta.width << "\" height=\"" << meta.height << "\" fill=\"" << rgb(bg.color)
        << "\" fill-opacity=\"" << num(alpha_of(bg.color)) << "\"/>\n";

    if (!bg.foreground_color || !bg.spacing) return out.str();  // blank, or a type this reader does not know
    const Color& fg = *bg.foreground_color;
    const std::int64_t spacing = *bg.spacing;

    switch (bg.kind) {
        case BackgroundKind::Ruled:
        case BackgroundKind::Grid: {
            // One physical pixel wide, at 20% of the line colour's opacity, none at coordinate 0.
            out << "<g stroke=\"" << rgb(fg) << "\" stroke-opacity=\"" << num(0.2 * alpha_of(fg)) << "\" stroke-width=\"1\" fill=\"none\">\n";
            for (std::int64_t y = spacing; y <= meta.height; y += spacing) {
                out << "<line x1=\"0\" y1=\"" << y << "\" x2=\"" << meta.width << "\" y2=\"" << y << "\" vector-effect=\"non-scaling-stroke\"/>\n";
            }
            if (bg.kind == BackgroundKind::Grid) {
                for (std::int64_t x = spacing; x <= meta.width; x += spacing) {
                    out << "<line x1=\"" << x << "\" y1=\"0\" x2=\"" << x << "\" y2=\"" << meta.height << "\" vector-effect=\"non-scaling-stroke\"/>\n";
                }
            }
            out << "</g>\n";
            break;
        }
        case BackgroundKind::Dotted: {
            // Dots of 20% of the spacing in diameter at 25% opacity.
            out << "<g fill=\"" << rgb(fg) << "\" fill-opacity=\"" << num(0.25 * alpha_of(fg)) << "\">\n";
            const double radius = 0.1 * static_cast<double>(spacing);
            for (std::int64_t y = spacing; y <= meta.height; y += spacing) {
                for (std::int64_t x = spacing; x <= meta.width; x += spacing) {
                    out << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"" << num(radius) << "\"/>\n";
                }
            }
            out << "</g>\n";
            break;
        }
        default: break;
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// Ink, spec 7.7, 7.8 and 7.9.5
// ---------------------------------------------------------------------------

struct ToolRules {
    double min_width = 1.0;        // fraction of base_width at pressure 0
    double min_opacity = 1.0;      // fraction of the colour's alpha at pressure 0
    bool taper = false;
    bool multiply = false;
};

ToolRules rules_for(ToolType tool) {
    switch (tool) {
        case ToolType::Fountain:    return {0.1, 1.0, true, false};
        case ToolType::Pencil:      return {1.0, 0.2, false, false};
        case ToolType::Highlighter: return {1.0, 1.0, false, true};
        case ToolType::LineEraser:  return {0.6, 1.0, false, false};
        case ToolType::Ballpoint:
        default:                    return {0.4, 1.0, false, false};
    }
}

// Spec 7.7.3.1: K = floor(0.05 * N); the first and last K samples are scaled down.
double taper_factor(std::size_t index, std::size_t count) {
    const std::size_t k = count / 20;
    if (k == 0) return 1.0;
    if (index < k) return static_cast<double>(index) / static_cast<double>(k);
    if (index + 1 + k > count) return static_cast<double>(count - 1 - index) / static_cast<double>(k);
    return 1.0;
}

std::string group_open(const Color& color, double opacity, bool multiply) {
    std::string out = "<g fill=\"" + rgb(color) + "\" stroke=\"" + rgb(color) + "\"";
    if (opacity < 1.0) out += " opacity=\"" + num(opacity) + "\"";
    if (multiply) out += " style=\"mix-blend-mode:multiply\"";
    return out + ">\n";
}

// The pressure of sample i, 255 for strokes without pressure (spec 7.5.4).
double pressure_at(const Stroke& stroke, std::size_t i) {
    return stroke.flags.no_pressure() ? 1.0 : stroke.samples.pressure[i] / 255.0;
}

// A stroke as SVG. For the eraser the same function draws the mask shape in black.
std::string stroke_svg(const Stroke& stroke, bool as_mask) {
    const ToolRules rules = rules_for(stroke.tool);
    const SampleColumns& s = stroke.samples;
    const std::size_t n = s.size();
    const double base = stroke.base_width;
    const Color color = as_mask ? Color{0, 0, 0, 255} : stroke.color;
    const bool is_pencil = stroke.tool == ToolType::Pencil;
    const double group_opacity = as_mask || is_pencil ? 1.0 : alpha_of(color);

    std::string out = group_open(color, group_opacity, !as_mask && rules.multiply);

    const auto width_at = [&](std::size_t i) {
        double w = base * lerp(rules.min_width, 1.0, pressure_at(stroke, i));
        if (rules.taper) w *= taper_factor(i, n);
        return w;
    };

    if (n == 1) {
        // A single sample is a filled circle of base_width in diameter, whatever the pressure (spec 7.8).
        double opacity = 1.0;
        if (is_pencil && !as_mask) opacity = alpha_of(color) * lerp(rules.min_opacity, 1.0, pressure_at(stroke, 0));
        out += "<circle cx=\"" + std::to_string(s.x[0]) + "\" cy=\"" + std::to_string(s.y[0]) + "\" r=\"" + num(base / 2) +
               "\" stroke=\"none\"" + (opacity < 1.0 ? " fill-opacity=\"" + num(opacity) + "\"" : "") + "/>\n";
        return out + "</g>\n";
    }

    const bool constant_width = stroke.flags.no_pressure() || rules.min_width == 1.0;
    if (constant_width && !rules.taper && !is_pencil) {
        // One polyline for the whole stroke: it is composited once, so alpha does not build up at overlaps.
        std::string points;
        for (std::size_t i = 0; i < n; ++i) points += std::to_string(s.x[i]) + "," + std::to_string(s.y[i]) + " ";
        out += "<polyline points=\"" + points + "\" fill=\"none\" stroke-width=\"" + num(base) +
               "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
        return out + "</g>\n";
    }

    for (std::size_t i = 0; i + 1 < n; ++i) {
        const double w = (width_at(i) + width_at(i + 1)) / 2;
        std::string opacity;
        if (is_pencil && !as_mask) {
            const double o = alpha_of(color) * lerp(rules.min_opacity, 1.0, (pressure_at(stroke, i) + pressure_at(stroke, i + 1)) / 2);
            opacity = " stroke-opacity=\"" + num(o) + "\"";
        }
        out += "<line x1=\"" + std::to_string(s.x[i]) + "\" y1=\"" + std::to_string(s.y[i]) + "\" x2=\"" + std::to_string(s.x[i + 1]) +
               "\" y2=\"" + std::to_string(s.y[i + 1]) + "\" stroke-width=\"" + num(w) + "\" stroke-linecap=\"round\"" + opacity + "/>\n";
    }
    return out + "</g>\n";
}

// A shape as one stroked outline with a single opacity (spec 7.9.5: pressure 255, composited once).
std::string shape_svg(const Shape& shape) {
    const ToolRules rules = rules_for(shape.tool);
    const double base = shape.base_width;
    std::string out = group_open(shape.color, alpha_of(shape.color), rules.multiply);
    const std::string stroke_attrs = " fill=\"none\" stroke-width=\"" + num(base) + "\" stroke-linejoin=\"round\" stroke-linecap=\"round\"";

    struct Visitor {
        const std::string& attrs;
        double base;

        std::string operator()(const Ellipse& e) const {
            return "<ellipse cx=\"" + std::to_string(e.cx) + "\" cy=\"" + std::to_string(e.cy) + "\" rx=\"" + std::to_string(e.rx) +
                   "\" ry=\"" + std::to_string(e.ry) + "\"" + attrs + rotation(e.rotation, e.cx, e.cy) + "/>\n";
        }
        std::string operator()(const Rectangle& r) const {
            const double x = static_cast<double>(r.cx) - static_cast<double>(r.width) / 2;
            const double y = static_cast<double>(r.cy) - static_cast<double>(r.height) / 2;
            return "<rect x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + std::to_string(r.width) + "\" height=\"" +
                   std::to_string(r.height) + "\"" + attrs + rotation(r.rotation, r.cx, r.cy) + "/>\n";
        }
        std::string operator()(const Line& l) const {
            if (l.x1 == l.x2 && l.y1 == l.y2) {
                // Identical end points: a filled circle of base_width in diameter (spec 7.9.5).
                return "<circle cx=\"" + std::to_string(l.x1) + "\" cy=\"" + std::to_string(l.y1) + "\" r=\"" + num(base / 2) + "\" stroke=\"none\"/>\n";
            }
            return "<line x1=\"" + std::to_string(l.x1) + "\" y1=\"" + std::to_string(l.y1) + "\" x2=\"" + std::to_string(l.x2) +
                   "\" y2=\"" + std::to_string(l.y2) + "\"" + attrs + "/>\n";
        }
        std::string operator()(const Triangle& t) const {
            return "<polygon points=\"" + std::to_string(t.x1) + "," + std::to_string(t.y1) + " " + std::to_string(t.x2) + "," +
                   std::to_string(t.y2) + " " + std::to_string(t.x3) + "," + std::to_string(t.y3) + "\"" + attrs + "/>\n";
        }
        static std::string rotation(std::uint16_t hundredths, Coord cx, Coord cy) {
            if (hundredths == 0) return "";
            // Positive rotates clockwise as seen on the page, which is the direction of SVG's rotate().
            return " transform=\"rotate(" + num(hundredths / 100.0) + " " + std::to_string(cx) + " " + std::to_string(cy) + ")\"";
        }
    };
    out += std::visit(Visitor{stroke_attrs, base}, shape.geometry);
    return out + "</g>\n";
}

// All ink of one layer. A line eraser masks the ink that came before it in this layer, nothing else (spec 7.4.4).
std::string ink_svg(const std::vector<const InkItem*>& ink, std::string& defs, int& mask_counter, const PageMeta& meta) {
    std::string accumulated;
    for (const InkItem* item : ink) {
        if (const Shape* shape = std::get_if<Shape>(item)) {
            accumulated += shape_svg(*shape);
            continue;
        }
        const Stroke& stroke = std::get<Stroke>(*item);
        if (stroke.tool != ToolType::LineEraser) {
            accumulated += stroke_svg(stroke, false);
            continue;
        }
        const std::string id = "erase" + std::to_string(++mask_counter);
        defs += "<mask id=\"" + id + "\" maskUnits=\"userSpaceOnUse\" x=\"" + std::to_string(-static_cast<std::int64_t>(meta.width)) + "\" y=\"" +
                std::to_string(-static_cast<std::int64_t>(meta.height)) + "\" width=\"" + std::to_string(3 * static_cast<std::int64_t>(meta.width)) +
                "\" height=\"" + std::to_string(3 * static_cast<std::int64_t>(meta.height)) + "\">\n<rect x=\"" +
                std::to_string(-static_cast<std::int64_t>(meta.width)) + "\" y=\"" + std::to_string(-static_cast<std::int64_t>(meta.height)) +
                "\" width=\"" + std::to_string(3 * static_cast<std::int64_t>(meta.width)) + "\" height=\"" +
                std::to_string(3 * static_cast<std::int64_t>(meta.height)) + "\" fill=\"white\"/>\n" + stroke_svg(stroke, true) + "</mask>\n";
        accumulated = "<g mask=\"url(#" + id + ")\">\n" + accumulated + "</g>\n";
    }
    return accumulated;
}

// ---------------------------------------------------------------------------
// Text objects, spec 8
// ---------------------------------------------------------------------------

struct Style {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    Color color;
    std::int32_t size = 0;
    std::string family;
    std::string generic;

    friend bool operator==(const Style&, const Style&) = default;
};

std::vector<std::string> code_points(const std::string& text) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        out.push_back(text.substr(i, length));
        i += length;
    }
    return out;
}

std::string generic_or_default(const std::string& generic) {
    return generic == "serif" || generic == "monospace" ? generic : "sans-serif";  // spec 8.4.1
}

std::string text_svg(const TextObject& text, std::string& defs, int& clip_counter) {
    const std::vector<std::string> chars = code_points(text.content);

    Style base;
    base.bold = text.bold;
    base.italic = text.italic;
    base.color = text.color;
    base.size = text.font_size;
    base.family = text.font_family;
    base.generic = text.generic_family;

    std::vector<Style> styles(chars.size(), base);
    for (const Span& span : text.spans) {
        for (std::uint32_t i = span.start; i < span.end && i < styles.size(); ++i) {
            Style& st = styles[i];
            if (span.bold) st.bold = *span.bold;
            if (span.italic) st.italic = *span.italic;
            if (span.underline) st.underline = *span.underline;
            if (span.strikethrough) st.strikethrough = *span.strikethrough;
            if (span.color) st.color = *span.color;
            if (span.font_size) st.size = *span.font_size;
            if (span.font_family) {
                st.family = *span.font_family;
                st.generic = span.generic_family.value_or(st.generic);
            }
        }
    }

    const std::string clip_id = "text" + std::to_string(++clip_counter);
    defs += "<clipPath id=\"" + clip_id + "\"><rect x=\"" + std::to_string(text.x) + "\" y=\"" + std::to_string(text.y) + "\" width=\"" +
            std::to_string(text.width) + "\" height=\"" + std::to_string(text.height) + "\"/></clipPath>\n";
    std::string out = "<g clip-path=\"url(#" + clip_id + ")\" xml:space=\"preserve\">\n";

    // Lines end at a newline, which counts as a character of its line (spec 8.6.1). Line height is 120% of the
    // largest font size in the line; an empty last line uses the font size of the text object.
    double line_top = static_cast<double>(text.y);
    std::size_t at = 0;
    bool last_line_done = false;
    while (!last_line_done) {
        std::size_t end = at;
        while (end < chars.size() && chars[end] != "\n") ++end;
        const bool has_newline = end < chars.size();
        const std::size_t line_end_with_break = has_newline ? end + 1 : end;
        last_line_done = !has_newline;

        std::int32_t largest = at == end && !has_newline ? text.font_size : 0;
        for (std::size_t i = at; i < line_end_with_break; ++i) largest = std::max(largest, styles[i].size);
        if (largest == 0) largest = text.font_size;
        const double line_height = 1.2 * largest;

        if (end > at) {
            out += "<text x=\"" + std::to_string(text.x) + "\" y=\"" + num(line_top + 0.9 * largest) + "\">";
            std::size_t run = at;
            while (run < end) {
                std::size_t run_end = run + 1;
                while (run_end < end && styles[run_end] == styles[run]) ++run_end;
                const Style& st = styles[run];
                std::string content;
                for (std::size_t i = run; i < run_end; ++i) content += chars[i];

                out += "<tspan font-family=\"" + escape(st.family) + ", " + generic_or_default(st.generic) + "\" font-size=\"" + std::to_string(st.size) + "\"";
                if (st.bold) out += " font-weight=\"bold\"";
                if (st.italic) out += " font-style=\"italic\"";
                if (st.underline || st.strikethrough) {
                    out += std::string(" text-decoration=\"") + (st.underline ? "underline" : "") + (st.underline && st.strikethrough ? " " : "") +
                           (st.strikethrough ? "line-through" : "") + "\"";
                }
                out += " fill=\"" + rgb(st.color) + "\"";
                if (st.color.alpha != 255) out += " fill-opacity=\"" + num(alpha_of(st.color)) + "\"";
                out += ">" + escape(content) + "</tspan>";
                run = run_end;
            }
            out += "</text>\n";
        }
        line_top += line_height;
        at = line_end_with_break;
    }
    return out + "</g>\n";
}

// ---------------------------------------------------------------------------
// Assets, spec 9
// ---------------------------------------------------------------------------

std::string placeholder(const AssetRef& ref, const std::string& label, bool is_error) {
    const std::string stroke = is_error ? "rgb(200,30,30)" : "rgb(120,120,120)";
    const std::string fill = is_error ? "rgb(255,235,235)" : "rgb(240,240,240)";
    const std::int64_t x = ref.x, y = ref.y, w = ref.width, h = ref.height;
    std::string out = "<g>\n<rect x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\" width=\"" + std::to_string(w) + "\" height=\"" +
                      std::to_string(h) + "\" fill=\"" + fill + "\" stroke=\"" + stroke + "\" stroke-width=\"20\"/>\n";
    if (is_error) {
        out += "<path d=\"M" + std::to_string(x) + "," + std::to_string(y) + " L" + std::to_string(x + w) + "," + std::to_string(y + h) + " M" +
               std::to_string(x + w) + "," + std::to_string(y) + " L" + std::to_string(x) + "," + std::to_string(y + h) + "\" stroke=\"" +
               stroke + "\" stroke-width=\"20\" fill=\"none\"/>\n";
    }
    // The label has to fit into the box: about 0.55 em per character.
    const std::int64_t by_width = static_cast<std::int64_t>(static_cast<double>(w) / (0.55 * static_cast<double>(std::max<std::size_t>(label.size(), 1)) + 2));
    const std::int64_t font = std::max<std::int64_t>(50, std::min({h / 12, std::int64_t{500}, by_width}));
    out += "<text x=\"" + std::to_string(x + font) + "\" y=\"" + std::to_string(y + 2 * font) + "\" font-size=\"" + std::to_string(font) +
           "\" font-family=\"sans-serif\" fill=\"" + stroke + "\">" + escape(label) + "</text>\n</g>\n";
    return out;
}

std::string asset_svg(const Document& document, const std::string& page_id, const AssetRef& ref, DiagnosticsSink* sink) {
    auto content = document.asset(ref, page_id, sink);
    if (!content) return placeholder(ref, "asset unreadable: " + content.error().message, true);

    const AssetCheck& check = content.value().check;
    switch (check.status) {
        case AssetStatus::Missing:           return placeholder(ref, "missing asset", true);
        case AssetStatus::UnsupportedFormat: return placeholder(ref, "unsupported asset format", true);
        case AssetStatus::Corrupt:           return placeholder(ref, "corrupt asset", true);
        case AssetStatus::Ok:                break;
    }

    if (ref.extension == "pdf") {
        // No PDF renderer: a neutral placeholder that says which page is meant.
        return placeholder(ref, "PDF page " + std::to_string(ref.pdf_page.value_or(1)) + " (not rendered by onf-reader)", false);
    }
    const char* mime = ref.extension == "png" ? "image/png" : ref.extension == "jpg" ? "image/jpeg" : "image/webp";
    return "<image x=\"" + std::to_string(ref.x) + "\" y=\"" + std::to_string(ref.y) + "\" width=\"" + std::to_string(ref.width) + "\" height=\"" +
           std::to_string(ref.height) + "\" preserveAspectRatio=\"none\" style=\"image-orientation:none\" href=\"data:" + mime + ";base64," +
           base64_encode(content.value().bytes) + "\"/>\n";
}

}  // namespace

std::string export_page_svg(const Document& document, const Page& page, DiagnosticsSink* sink) {
    const PageMeta& meta = page.meta;

    std::map<std::uint16_t, LayerContent> layers;  // ascending by layer value (spec 3.4)
    for (const AssetRef& asset : meta.assets) layers[asset.layer].assets.push_back(&asset);
    for (const TextObject& text : meta.text_objects) layers[text.layer].texts.push_back(&text);
    for (const InkItem& item : page.ink) {
        const std::uint16_t layer = std::visit([](const auto& v) { return v.layer; }, item);
        layers[layer].ink.push_back(&item);
    }

    std::string defs;
    std::string body;
    int mask_counter = 0;
    int clip_counter = 0;
    for (const auto& [layer, content] : layers) {
        body += "<g id=\"layer" + std::to_string(layer) + "\">\n";
        for (const AssetRef* asset : content.assets) body += asset_svg(document, page.id, *asset, sink);
        for (const TextObject* text : content.texts) body += text_svg(*text, defs, clip_counter);
        body += ink_svg(content.ink, defs, mask_counter, meta);
        body += "</g>\n";
    }

    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << num(meta.width / 100.0) << "mm\" height=\"" << num(meta.height / 100.0)
        << "mm\" viewBox=\"0 0 " << meta.width << " " << meta.height << "\">\n";
    out << "<defs>\n<clipPath id=\"page\"><rect x=\"0\" y=\"0\" width=\"" << meta.width << "\" height=\"" << meta.height << "\"/></clipPath>\n"
        << defs << "</defs>\n";
    out << "<g clip-path=\"url(#page)\">\n" << background(meta) << body << "</g>\n</svg>\n";
    return out.str();
}

}  // namespace onf_reader
