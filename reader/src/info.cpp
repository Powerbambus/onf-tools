#include "info.h"

#include <map>
#include <set>
#include <string>
#include <variant>

namespace onf_reader {

namespace {

using namespace onf;

std::string two_digits(unsigned v) { return (v < 10 ? "0" : "") + std::to_string(v); }

std::string three_digits(unsigned v) { return std::string(v < 10 ? "00" : v < 100 ? "0" : "") + std::to_string(v); }

std::string format_time(const UtcDateTime& t) {
    return std::to_string(t.year) + "-" + two_digits(t.month) + "-" + two_digits(t.day) + " " + two_digits(t.hour) + ":" +
           two_digits(t.minute) + ":" + two_digits(t.second) + "." + three_digits(t.millisecond) + " UTC";
}

std::string millimetres(Coord value) {
    return std::to_string(value / 100) + "." + two_digits(static_cast<unsigned>(value % 100)) + " mm";
}

void print_page(const Page& page, std::ostream& out) {
    const PageMeta& meta = page.meta;
    out << "    size:        " << millimetres(meta.width) << " x " << millimetres(meta.height) << "\n";
    out << "    background:  " << meta.background.type << "\n";

    std::size_t strokes = 0, shapes = 0, samples = 0, erasers = 0;
    std::set<std::uint16_t> layers;
    for (const InkItem& item : page.ink) {
        if (const Stroke* stroke = std::get_if<Stroke>(&item)) {
            ++strokes;
            samples += stroke->samples.size();
            if (stroke->tool == ToolType::LineEraser) ++erasers;
            layers.insert(stroke->layer);
        } else {
            ++shapes;
            layers.insert(std::get<Shape>(item).layer);
        }
    }
    for (const TextObject& text : meta.text_objects) layers.insert(text.layer);
    for (const AssetRef& asset : meta.assets) layers.insert(asset.layer);

    out << "    ink:         " << strokes << " strokes (" << samples << " samples, " << erasers << " line erasers), " << shapes << " shapes\n";
    out << "    text:        " << meta.text_objects.size() << " text objects\n";

    std::map<std::string, std::size_t> by_extension;
    for (const AssetRef& asset : meta.assets) ++by_extension[asset.extension];
    out << "    assets:      " << meta.assets.size() << " placements";
    const char* separator = " (";
    for (const auto& [extension, count] : by_extension) {
        out << separator << count << " " << extension;
        separator = ", ";
    }
    out << (by_extension.empty() ? "" : ")") << "\n";

    out << "    layers:      ";
    if (layers.empty()) out << "none";
    const char* comma = "";
    for (const std::uint16_t layer : layers) {
        out << comma << layer;
        comma = ", ";
    }
    out << "\n";
}

}  // namespace

std::string describe_error(ErrorCode code, const std::string& message) {
    return std::to_string(static_cast<unsigned>(code)) + " " + std::string(error_name(code)) + " (spec " + std::string(error_spec_ref(code)) +
           "): " + message;
}

int print_info(const Document& document, std::ostream& out, DiagnosticsSink* sink) {
    const Manifest& manifest = document.manifest();
    out << "version:     " << to_string(manifest.onf_version) << "\n";
    if (manifest.generator) out << "generator:   " << *manifest.generator << "\n";
    out << "title:       " << manifest.title << "\n";
    out << "created:     " << format_time(manifest.created_at) << "\n";
    out << "modified:    " << format_time(manifest.modified_at) << "\n";
    out << "pages:       " << manifest.pages.size() << "\n";

    int unreadable = 0;
    for (const PageEntry& entry : document.pages()) {
        out << "\n  page " << entry.order << " (" << entry.id << ")\n";
        auto page = document.page(entry.id, sink);
        if (!page) {
            ++unreadable;
            out << "    UNREADABLE: " << describe_error(page.error().code, page.error().message);
            if (page.error().offset) out << " at byte " << *page.error().offset;
            out << "\n";
            continue;
        }
        print_page(page.value(), out);
    }
    return unreadable;
}

}  // namespace onf_reader
