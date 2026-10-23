#include "doctest.h"

#include <cmath>
#include <random>
#include <set>
#include <unordered_set>
#include <variant>
#include <vector>

#include "onf/edit/erase.h"
#include "onf/write/writer.h"

using namespace onf;

namespace {

struct P {
    Coord x, y;
    std::uint8_t pressure;
    Timestamp t;
};

Stroke make(std::uint32_t id, std::uint16_t layer, ToolType tool, std::uint16_t width, const std::vector<P>& points, std::uint16_t flags = 0) {
    Stroke s;
    s.stroke_id = id;
    s.layer = layer;
    s.color = {0, 0, 0, 255};
    s.tool = tool;
    s.tool_type_raw = static_cast<std::uint16_t>(tool);
    s.base_width = width;
    s.flags = StrokeFlags{flags};
    for (const P& p : points) {
        s.samples.x.push_back(p.x);
        s.samples.y.push_back(p.y);
        s.samples.pressure.push_back(p.pressure);
        if (!s.flags.no_timestamp()) s.samples.timestamp.push_back(p.t);
    }
    return s;
}

// A horizontal line from x = 0 to 1000 with a sample every 100 units and 1 ms per unit.
Stroke horizontal(std::uint32_t id, std::uint16_t layer, ToolType tool = ToolType::Fountain, std::uint16_t flags = 0) {
    std::vector<P> points;
    for (Coord x = 0; x <= 1000; x += 100) points.push_back({x, 0, 200, static_cast<Timestamp>(x)});
    return make(id, layer, tool, 20, points, flags);
}

const Stroke& stroke(const std::vector<InkItem>& ink, std::size_t i) { return std::get<Stroke>(ink.at(i)); }

}  // namespace

TEST_CASE("erase: nothing happens when the disk is far away or on another layer") {
    std::vector<InkItem> ink = {horizontal(1, 1)};
    CHECK_FALSE(erase_disk(ink, 1, {500, 5000, 100}).changed());
    CHECK_FALSE(erase_disk(ink, 2, {500, 0, 100}).changed());
    REQUIRE(ink.size() == 1);
    CHECK(stroke(ink, 0).samples.size() == 11);
}

TEST_CASE("erase: a disk in the middle splits a stroke in two parts with interpolated border samples") {
    std::vector<InkItem> ink = {horizontal(1, 1)};
    const EraseSummary summary = erase_disk(ink, 1, {500, 0, 100});  // reaches 110 with half the width
    CHECK(summary.strokes_cut == 1);
    REQUIRE(ink.size() == 2);

    const Stroke& a = stroke(ink, 0);
    CHECK(a.stroke_id == 1);
    CHECK(a.samples.x == std::vector<Coord>{0, 100, 200, 300, 390});
    CHECK(a.samples.timestamp == std::vector<Timestamp>{0, 100, 200, 300, 390});
    CHECK(a.flags.no_taper_end());
    CHECK_FALSE(a.flags.no_taper_start());

    const Stroke& b = stroke(ink, 1);
    CHECK(b.stroke_id == 2);
    CHECK(b.samples.x == std::vector<Coord>{610, 700, 800, 900, 1000});
    CHECK(b.samples.timestamp == std::vector<Timestamp>{610, 700, 800, 900, 1000});
    CHECK(b.flags.no_taper_start());
    CHECK_FALSE(b.flags.no_taper_end());
    CHECK(b.layer == 1);
    CHECK(b.base_width == 20);
}

TEST_CASE("erase: the taper flags are only set for the fountain pen") {
    std::vector<InkItem> ink = {horizontal(1, 1, ToolType::Ballpoint)};
    erase_disk(ink, 1, {500, 0, 100});
    REQUIRE(ink.size() == 2);
    CHECK_FALSE(stroke(ink, 0).flags.no_taper_end());
    CHECK_FALSE(stroke(ink, 1).flags.no_taper_start());
}

TEST_CASE("erase: a disk over the start keeps only the rest, a disk over everything removes the stroke") {
    std::vector<InkItem> ink = {horizontal(1, 1)};
    CHECK(erase_disk(ink, 1, {0, 0, 100}).strokes_cut == 1);
    REQUIRE(ink.size() == 1);
    CHECK(stroke(ink, 0).stroke_id == 1);
    CHECK(stroke(ink, 0).samples.x.front() == 110);
    CHECK(stroke(ink, 0).flags.no_taper_start());
    CHECK_FALSE(stroke(ink, 0).flags.no_taper_end());

    CHECK(erase_disk(ink, 1, {500, 0, 2000}).strokes_removed == 1);
    CHECK(ink.empty());
}

TEST_CASE("erase: a long segment that crosses the disk between two samples is cut") {
    std::vector<InkItem> ink = {make(1, 1, ToolType::Fountain, 10, {{0, 0, 255, 0}, {1000, 0, 255, 1000}})};
    erase_disk(ink, 1, {500, 0, 50});
    REQUIRE(ink.size() == 2);
    CHECK(stroke(ink, 0).samples.x == std::vector<Coord>{0, 445});
    CHECK(stroke(ink, 1).samples.x == std::vector<Coord>{555, 1000});
}

TEST_CASE("erase: a segment that only passes the disk at a distance stays whole") {
    std::vector<InkItem> ink = {make(1, 1, ToolType::Fountain, 10, {{0, 0, 255, 0}, {1000, 0, 255, 1000}})};
    CHECK_FALSE(erase_disk(ink, 1, {500, 100, 50}).changed());
    CHECK(stroke(ink, 0).samples.size() == 2);
}

TEST_CASE("erase: a remaining part with a single sample is dropped") {
    std::vector<InkItem> ink = {make(1, 1, ToolType::Ballpoint, 10, {{0, 0, 255, 0}, {100, 0, 255, 100}, {200, 0, 255, 200}})};
    erase_disk(ink, 1, {150, 0, 20});  // eats the border sample at 125 and leaves the part 0..125 and 175..200
    REQUIRE(ink.size() == 2);
    CHECK(stroke(ink, 0).samples.x == std::vector<Coord>{0, 100, 125});
    CHECK(stroke(ink, 1).samples.x == std::vector<Coord>{175, 200});

    ink = {make(1, 1, ToolType::Ballpoint, 10, {{0, 0, 255, 0}, {100, 0, 255, 100}})};
    erase_disk(ink, 1, {95, 0, 20});  // the cut at 70..120 leaves 0..70 and nothing behind it, because 100 is erased
    REQUIRE(ink.size() == 1);
    CHECK(stroke(ink, 0).samples.x == std::vector<Coord>{0, 70});
}

TEST_CASE("erase: a stroke of one sample is kept or removed as a whole") {
    std::vector<InkItem> ink = {make(1, 1, ToolType::Ballpoint, 10, {{500, 500, 255, 0}}), make(2, 1, ToolType::Ballpoint, 10, {{5000, 500, 255, 0}})};
    const EraseSummary summary = erase_disk(ink, 1, {500, 500, 10});
    CHECK(summary.strokes_removed == 1);
    REQUIRE(ink.size() == 1);
    CHECK(stroke(ink, 0).stroke_id == 2);
}

TEST_CASE("erase: new ids are unused and the order of the ink is kept") {
    std::vector<InkItem> ink = {horizontal(5, 1), horizontal(2, 2), horizontal(9, 1)};
    erase_disk(ink, 1, {500, 0, 100});
    REQUIRE(ink.size() == 5);
    CHECK(stroke(ink, 0).stroke_id == 5);
    CHECK(stroke(ink, 2).stroke_id == 2);
    CHECK(stroke(ink, 3).stroke_id == 9);

    std::set<std::uint32_t> ids;
    for (const InkItem& item : ink) CHECK(ids.insert(std::get<Stroke>(item).stroke_id).second);
    CHECK(stroke(ink, 2).samples.size() == 11);  // the other layer is untouched
}

TEST_CASE("erase: pressure is interpolated, tilt and azimuth come from the nearer sample") {
    Stroke s = make(1, 1, ToolType::Fountain, 0 + 2, {{0, 0, 0, 0}, {1000, 0, 100, 1000}}, 0x3);
    s.samples.tilt_x = {10, -10};
    s.samples.tilt_y = {20, -20};
    s.samples.azimuth = {100, 35000};
    std::vector<InkItem> ink = {s};
    erase_disk(ink, 1, {500, 0, 99});  // the border samples lie at 400 and 600
    REQUIRE(ink.size() == 2);
    const Stroke& a = stroke(ink, 0);
    CHECK(a.samples.x.back() == 400);
    CHECK(a.samples.pressure.back() == 40);
    CHECK(a.samples.tilt_x.back() == 10);
    CHECK(a.samples.azimuth.back() == 100);
    const Stroke& b = stroke(ink, 1);
    CHECK(b.samples.x.front() == 600);
    CHECK(b.samples.pressure.front() == 60);
    CHECK(b.samples.tilt_x.front() == -10);
    CHECK(b.samples.azimuth.front() == 35000);
}

TEST_CASE("erase: a stroke without timestamps stays without") {
    std::vector<InkItem> ink = {horizontal(1, 1, ToolType::Fountain, 0x08)};
    erase_disk(ink, 1, {500, 0, 100});
    REQUIRE(ink.size() == 2);
    CHECK(stroke(ink, 0).samples.timestamp.empty());
    CHECK(stroke(ink, 1).samples.timestamp.empty());
}

TEST_CASE("erase: shapes stay shapes until the eraser touches them") {
    Shape ellipse;
    ellipse.stroke_id = 1;
    ellipse.layer = 1;
    ellipse.color = {0, 0, 0, 255};
    ellipse.tool_type_raw = 1;
    ellipse.tool = ToolType::Fountain;
    ellipse.base_width = 20;
    ellipse.geometry = Ellipse{0, 0, 1000, 500, 0};
    std::vector<InkItem> ink = {ellipse};

    CHECK_FALSE(erase_disk(ink, 1, {0, 0, 100}).changed());  // inside the ellipse, away from its outline
    CHECK(std::holds_alternative<Shape>(ink[0]));

    const EraseSummary summary = erase_disk(ink, 1, {1000, 0, 100});  // on the outline
    CHECK(summary.shapes_replaced == 1);
    REQUIRE(ink.size() == 1);
    const Stroke& rest = stroke(ink, 0);
    CHECK(rest.stroke_id == 1);
    CHECK(rest.tool == ToolType::Fountain);
    CHECK(rest.base_width == 20);
    CHECK(rest.flags.no_pressure());
    CHECK(rest.flags.no_timestamp());
    CHECK(rest.flags.no_taper_start());
    CHECK(rest.flags.no_taper_end());
    for (std::size_t i = 0; i < rest.samples.size(); ++i) {
        const double dx = rest.samples.x[i] - 1000.0, dy = rest.samples.y[i];
        CHECK(std::hypot(dx, dy) >= 109.0);
    }
}

TEST_CASE("erase: a rectangle, a line and a triangle are replaced by strokes along their outline") {
    Shape rect;
    rect.stroke_id = 1;
    rect.layer = 1;
    rect.color = {0, 0, 0, 255};
    rect.base_width = 10;
    rect.geometry = Rectangle{0, 0, 2000, 1000, 0};
    Shape line = rect;
    line.stroke_id = 2;
    line.geometry = Line{-1000, 3000, 1000, 3000};
    Shape triangle = rect;
    triangle.stroke_id = 3;
    triangle.geometry = Triangle{-1000, 5000, 1000, 5000, 0, 7000};
    std::vector<InkItem> ink = {rect, line, triangle};

    CHECK(erase_disk(ink, 1, {1000, 0, 50}).shapes_replaced == 1);   // the right edge of the rectangle
    CHECK(erase_disk(ink, 1, {0, 3000, 50}).shapes_replaced == 1);   // the middle of the line
    CHECK(erase_disk(ink, 1, {0, 5000, 50}).shapes_replaced == 1);   // the base of the triangle
    CHECK(ink.size() >= 4);
    for (const InkItem& item : ink) CHECK(std::holds_alternative<Stroke>(item));
}

TEST_CASE("erase: random strokes and disks always give valid results") {
    std::mt19937 random(12345);
    std::uniform_int_distribution<int> coord(-2000, 2000);
    std::uniform_int_distribution<int> radius(0, 600);
    std::uniform_int_distribution<int> count(1, 12);

    for (int round = 0; round < 400; ++round) {
        std::vector<InkItem> ink;
        for (std::uint32_t id = 1; id <= 4; ++id) {
            std::vector<P> points;
            Timestamp t = 0;
            for (int i = count(random); i > 0; --i) {
                t += static_cast<Timestamp>(random() % 20);
                points.push_back({coord(random), coord(random), static_cast<std::uint8_t>(random() % 256), t});
            }
            ink.push_back(make(id, 1, ToolType::Fountain, static_cast<std::uint16_t>(1 + random() % 80), points));
        }
        const EraseDisk disk{static_cast<double>(coord(random)), static_cast<double>(coord(random)), static_cast<double>(radius(random))};
        erase_disk(ink, 1, disk);

        WriteDocument document;
        document.title = "T";
        document.created_at = UtcDateTime{2026, 6, 14, 10, 32, 0, 0};
        document.modified_at = document.created_at;
        WritePage page;
        page.meta.width = 5000;
        page.meta.height = 5000;
        page.meta.background.type = "blank";
        page.meta.background.color = {255, 255, 255, 255};
        page.ink = ink;
        document.pages.push_back(page);
        const auto problem = check_writable(document);
        CHECK_MESSAGE(!problem.has_value(), problem.value_or(""));

        std::unordered_set<std::uint32_t> ids;
        for (const InkItem& item : ink) {
            const Stroke& s = std::get<Stroke>(item);
            CHECK(ids.insert(s.stroke_id).second);
            CHECK(s.layer == 1);
            CHECK(s.samples.size() >= 1);
            CHECK(s.samples.timestamp.size() == s.samples.size());
            for (std::size_t i = 1; i < s.samples.size(); ++i) CHECK(s.samples.timestamp[i] >= s.samples.timestamp[i - 1]);
            const double reach = disk.radius + s.base_width / 2.0;
            for (std::size_t i = 0; i < s.samples.size(); ++i) {
                // The border samples are rounded to whole coord units, so they may sit up to 1 unit inside.
                CHECK(std::hypot(s.samples.x[i] - disk.x, s.samples.y[i] - disk.y) >= reach - 1.5);
            }
        }
    }
}
