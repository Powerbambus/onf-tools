#include "onf/edit/erase.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>

namespace onf {

namespace {

// One sample with every field a stroke can have; the fields that the stroke does not store are ignored.
struct Sample {
    double x = 0;
    double y = 0;
    double pressure = 255;
    double timestamp = 0;
    std::int8_t tilt_x = 0;
    std::int8_t tilt_y = 0;
    std::uint16_t azimuth = 0;
};

Sample sample_at(const SampleColumns& columns, std::size_t i, const StrokeFlags& flags) {
    Sample s;
    s.x = columns.x[i];
    s.y = columns.y[i];
    s.pressure = columns.pressure[i];
    if (!flags.no_timestamp()) s.timestamp = columns.timestamp[i];
    if (flags.has_tilt()) {
        s.tilt_x = columns.tilt_x[i];
        s.tilt_y = columns.tilt_y[i];
    }
    if (flags.has_azimuth()) s.azimuth = columns.azimuth[i];
    return s;
}

Sample interpolate(const Sample& a, const Sample& b, double t) {
    Sample s;
    s.x = a.x + (b.x - a.x) * t;
    s.y = a.y + (b.y - a.y) * t;
    s.pressure = a.pressure + (b.pressure - a.pressure) * t;
    s.timestamp = a.timestamp + (b.timestamp - a.timestamp) * t;
    // Tilt and azimuth do not interpolate well (azimuth wraps around at 360 degrees): take the nearer neighbour.
    const Sample& near = t < 0.5 ? a : b;
    s.tilt_x = near.tilt_x;
    s.tilt_y = near.tilt_y;
    s.azimuth = near.azimuth;
    return s;
}

struct Piece {
    std::vector<Sample> samples;
    bool starts_cut = false;
    bool ends_cut = false;
};

struct Disk {
    double x, y, radius;
    bool contains(double px, double py) const noexcept {
        const double dx = px - x, dy = py - y;
        return dx * dx + dy * dy <= radius * radius;
    }
};

// The part of the segment a-b that lies in the disk, as parameters from 0 to 1. Nothing if the segment stays
// outside or only touches the border at one point.
std::optional<std::pair<double, double>> inside_interval(const Sample& a, const Sample& b, const Disk& disk) {
    const bool a_inside = disk.contains(a.x, a.y);
    const bool b_inside = disk.contains(b.x, b.y);

    const double dx = b.x - a.x, dy = b.y - a.y;
    const double fx = a.x - disk.x, fy = a.y - disk.y;
    const double qa = dx * dx + dy * dy;
    const double qb = 2 * (fx * dx + fy * dy);
    const double qc = fx * fx + fy * fy - disk.radius * disk.radius;

    double t1 = 0, t2 = 1;
    if (qa > 0) {
        const double discriminant = qb * qb - 4 * qa * qc;
        if (discriminant > 0) {
            const double root = std::sqrt(discriminant);
            t1 = (-qb - root) / (2 * qa);
            t2 = (-qb + root) / (2 * qa);
        } else if (!a_inside && !b_inside) {
            return std::nullopt;  // misses the disk, or touches it at one point
        }
    } else if (!a_inside) {
        return std::nullopt;  // both ends are the same point, outside
    }

    double from = std::max(t1, 0.0), to = std::min(t2, 1.0);
    if (a_inside) from = 0;  // an end that is inside is inside, whatever the rounding says
    if (b_inside) to = 1;
    if (from > to || (from == to && !a_inside && !b_inside)) return std::nullopt;
    return std::make_pair(from, to);
}

// The parts of the polyline that lie outside the disk, or nothing if no sample or segment is affected.
std::optional<std::vector<Piece>> cut_polyline(const std::vector<Sample>& samples, const Disk& disk) {
    if (samples.empty()) return std::nullopt;

    std::vector<Piece> pieces;
    std::optional<Piece> open;
    bool touched = false;

    if (disk.contains(samples[0].x, samples[0].y)) {
        touched = true;
    } else {
        open = Piece{{samples[0]}, false, false};
    }

    for (std::size_t i = 1; i < samples.size(); ++i) {
        const Sample& a = samples[i - 1];
        const Sample& b = samples[i];
        const auto interval = inside_interval(a, b, disk);
        if (!interval) {
            open->samples.push_back(b);  // a segment that is completely outside, so a was outside and a part is open
            continue;
        }
        touched = true;
        const auto [from, to] = *interval;
        if (from > 0) {  // the path enters the disk here
            open->samples.push_back(interpolate(a, b, from));
            open->ends_cut = true;
            pieces.push_back(std::move(*open));
            open.reset();
        }
        if (to < 1) {  // and leaves it here
            open = Piece{{interpolate(a, b, to), b}, true, false};
        }
    }
    if (open) pieces.push_back(std::move(*open));
    if (!touched) return std::nullopt;
    return pieces;
}

std::int32_t to_coord(double value) {
    const double rounded = std::round(value);
    return static_cast<std::int32_t>(std::clamp(rounded, -2147483648.0, 2147483647.0));
}

Stroke make_piece(const Stroke& original, const Piece& piece, std::uint32_t id) {
    Stroke stroke;
    stroke.stroke_id = id;
    stroke.layer = original.layer;
    stroke.created_at = original.created_at;
    stroke.color = original.color;
    stroke.tool_type_raw = original.tool_type_raw;
    stroke.tool = original.tool;
    stroke.base_width = original.base_width;

    std::uint16_t flags = original.flags.raw();
    if (original.tool == ToolType::Fountain) {  // the taper only exists for the fountain pen (spec 7.7.3.1)
        if (piece.starts_cut) flags = static_cast<std::uint16_t>(flags | (1u << 5));
        if (piece.ends_cut) flags = static_cast<std::uint16_t>(flags | (1u << 6));
    }
    stroke.flags = StrokeFlags{flags};

    SampleColumns& c = stroke.samples;
    Timestamp last_timestamp = 0;
    for (const Sample& s : piece.samples) {
        c.x.push_back(to_coord(s.x));
        c.y.push_back(to_coord(s.y));
        c.pressure.push_back(static_cast<std::uint8_t>(std::clamp(std::lround(s.pressure), 0L, 255L)));
        if (!stroke.flags.no_timestamp()) {
            // The rounded time must not decrease (spec 5.3).
            const auto t = static_cast<Timestamp>(std::clamp(std::llround(s.timestamp), 0LL, 4294967295LL));
            last_timestamp = std::max(last_timestamp, t);
            c.timestamp.push_back(last_timestamp);
        }
        if (stroke.flags.has_tilt()) {
            c.tilt_x.push_back(s.tilt_x);
            c.tilt_y.push_back(s.tilt_y);
        }
        if (stroke.flags.has_azimuth()) c.azimuth.push_back(s.azimuth);
    }
    return stroke;
}

// Ids that are not used by any ink of the page.
class IdPool {
public:
    explicit IdPool(const std::vector<InkItem>& ink) {
        for (const InkItem& item : ink) {
            used_.insert(std::visit([](const auto& v) { return v.stroke_id; }, item));
        }
    }
    std::uint32_t take() {
        while (used_.contains(next_)) ++next_;
        used_.insert(next_);
        return next_++;
    }

private:
    std::unordered_set<std::uint32_t> used_;
    std::uint32_t next_ = 1;
};

// The pieces of a stroke after erasing, as strokes. Empty optional: the stroke is not touched.
std::optional<std::vector<Stroke>> erase_from_stroke(const Stroke& stroke, const EraseDisk& eraser, IdPool& ids) {
    const Disk disk{eraser.x, eraser.y, eraser.radius + stroke.base_width / 2.0};
    std::vector<Sample> samples;
    samples.reserve(stroke.samples.size());
    for (std::size_t i = 0; i < stroke.samples.size(); ++i) samples.push_back(sample_at(stroke.samples, i, stroke.flags));

    auto pieces = cut_polyline(samples, disk);
    if (!pieces) return std::nullopt;

    // A part with a single sample would be drawn as a dot at the border (spec 7.8). A stroke that was one sample
    // from the start is either inside the disk, then it is gone, or it was not touched.
    std::vector<Stroke> result;
    for (const Piece& piece : *pieces) {
        if (piece.samples.size() < 2) continue;
        result.push_back(make_piece(stroke, piece, result.empty() ? stroke.stroke_id : ids.take()));
    }
    return result;
}

// The outline of a shape as a polyline (spec 7.9.4, 7.9.6). Rotation is clockwise on the page, the Y axis points down.
std::vector<Sample> outline_of(const Shape& shape) {
    const auto point = [](double x, double y) {
        Sample s;
        s.x = x;
        s.y = y;
        return s;
    };
    const auto rotated = [&](double cx, double cy, double dx, double dy, std::uint16_t hundredths) {
        const double angle = hundredths / 100.0 * std::numbers::pi / 180.0;
        const double c = std::cos(angle), s = std::sin(angle);
        return point(cx + dx * c - dy * s, cy + dx * s + dy * c);
    };

    std::vector<Sample> out;
    if (const Ellipse* e = std::get_if<Ellipse>(&shape.geometry)) {
        constexpr int kCorners = 96;
        for (int k = 0; k <= kCorners; ++k) {
            const double t = 2 * std::numbers::pi * k / kCorners;
            out.push_back(rotated(e->cx, e->cy, e->rx * std::cos(t), e->ry * std::sin(t), e->rotation));
        }
    } else if (const Rectangle* r = std::get_if<Rectangle>(&shape.geometry)) {
        const double w = r->width / 2.0, h = r->height / 2.0;
        for (const auto& [dx, dy] : {std::pair{-w, -h}, {w, -h}, {w, h}, {-w, h}, {-w, -h}}) {
            out.push_back(rotated(r->cx, r->cy, dx, dy, r->rotation));
        }
    } else if (const Line* l = std::get_if<Line>(&shape.geometry)) {
        out.push_back(point(l->x1, l->y1));
        if (l->x1 != l->x2 || l->y1 != l->y2) out.push_back(point(l->x2, l->y2));
    } else if (const Triangle* t = std::get_if<Triangle>(&shape.geometry)) {
        out = {point(t->x1, t->y1), point(t->x2, t->y2), point(t->x3, t->y3), point(t->x1, t->y1)};
    }
    return out;
}

Stroke stroke_of_outline(const Shape& shape) {
    Stroke stroke;
    stroke.stroke_id = shape.stroke_id;
    stroke.layer = shape.layer;
    stroke.created_at = shape.created_at;
    stroke.color = shape.color;
    stroke.tool_type_raw = shape.tool_type_raw;
    stroke.tool = shape.tool;
    stroke.base_width = shape.base_width;
    // A shape has no samples, no pressure and no taper: the strokes that replace it have none either.
    stroke.flags = StrokeFlags{static_cast<std::uint16_t>((1u << 2) | (1u << 3) | (1u << 5) | (1u << 6))};

    for (const Sample& s : outline_of(shape)) {
        stroke.samples.x.push_back(to_coord(s.x));
        stroke.samples.y.push_back(to_coord(s.y));
        stroke.samples.pressure.push_back(255);
    }
    return stroke;
}

}  // namespace

EraseSummary erase_disk(std::vector<InkItem>& ink, std::uint16_t layer, const EraseDisk& disk) {
    EraseSummary summary;
    IdPool ids(ink);
    std::vector<InkItem> result;
    result.reserve(ink.size());

    for (InkItem& item : ink) {
        if (const Stroke* stroke = std::get_if<Stroke>(&item)) {
            if (stroke->layer != layer) {
                result.push_back(std::move(item));
                continue;
            }
            if (auto pieces = erase_from_stroke(*stroke, disk, ids)) {
                if (pieces->empty()) ++summary.strokes_removed; else ++summary.strokes_cut;
                for (Stroke& piece : *pieces) result.push_back(std::move(piece));
                continue;
            }
        } else {
            const Shape& shape = std::get<Shape>(item);
            if (shape.layer == layer) {
                // Only the shapes that are really touched stop being shapes.
                if (auto pieces = erase_from_stroke(stroke_of_outline(shape), disk, ids)) {
                    if (pieces->empty()) ++summary.shapes_removed; else ++summary.shapes_replaced;
                    for (Stroke& piece : *pieces) result.push_back(std::move(piece));
                    continue;
                }
            }
        }
        result.push_back(std::move(item));
    }
    ink = std::move(result);
    return summary;
}

}  // namespace onf
