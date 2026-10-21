#include "libslic3r/GCode/SeamChoice.hpp"

#include <vector>
#include <algorithm>
#include <limits>

namespace Slic3r::Seams {
std::optional<SeamChoice> maybe_choose_seam_point(
    const Perimeters::Perimeter &perimeter, const SeamPicker &seam_picker
) {
    using Perimeters::PointType;
    using Perimeters::PointClassification;

    std::vector<PointType>
        type_search_order{PointType::enforcer, PointType::common, PointType::blocker};
    std::vector<PointClassification> classification_search_order{
        PointClassification::embedded, PointClassification::common, PointClassification::overhang};
    for (const PointType point_type : type_search_order) {
        for (const PointClassification point_classification : classification_search_order) {
            if (std::optional<SeamChoice> seam_choice{
                    seam_picker(perimeter, point_type, point_classification)}) {
                return seam_choice;
            }
        }
        if (!Perimeters::extract_points(perimeter, point_type).empty()) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

SeamChoice choose_seam_point(const Perimeters::Perimeter &perimeter, const SeamPicker &seam_picker) {
    using Perimeters::PointType;
    using Perimeters::PointClassification;

    std::optional<SeamChoice> seam_choice{maybe_choose_seam_point(perimeter, seam_picker)};

    if (seam_choice) {
        return *seam_choice;
    }

    // Failed to choose any reasonable point!
    return SeamChoice{0, 0, perimeter.positions.front()};
}

std::optional<SeamChoice> choose_degenerate_seam_point(const Perimeters::Perimeter &perimeter) {
    if (!perimeter.positions.empty()) {
        return SeamChoice{0, 0, perimeter.positions.front()};
    }
    return std::nullopt;
}


SeamChoice center_painted_seam(const Perimeters::Perimeter &perimeter, const SeamChoice &choice) {
    using Perimeters::PointType;
    const std::size_t count = perimeter.positions.size();
    if (count < 2 || perimeter.point_types.size() != count ||
        choice.previous_index >= count || choice.next_index >= count)
        return choice;

    const auto enforced = [&](std::size_t i) {
        return perimeter.point_types[i] == PointType::enforcer;
    };
    // Only influence user-painted seams. Unpainted placement remains unchanged.
    if (!enforced(choice.previous_index) && !enforced(choice.next_index))
        return choice;

    const auto break_it = std::find_if(perimeter.point_types.begin(), perimeter.point_types.end(),
        [](PointType t) { return t != PointType::enforcer; });
    if (break_it == perimeter.point_types.end())
        return choice; // Entire contour painted; no unique band center.

    const std::size_t break_index = std::size_t(break_it - perimeter.point_types.begin());
    std::size_t index = (break_index + 1) % count;
    double best_distance2 = std::numeric_limits<double>::infinity();
    SeamChoice best = choice;

    // Enumerate maximal runs of painted vertices, including those crossing the
    // loop's indexing boundary. Choose the run nearest the original candidate.
    while (index != break_index) {
        if (!enforced(index)) {
            index = (index + 1) % count;
            continue;
        }

        const std::size_t start = index;
        std::size_t end = index;
        double length = 0.0;
        double nearest_distance2 = (perimeter.positions[start] - choice.position).squaredNorm();
        while (true) {
            const std::size_t next = (end + 1) % count;
            if (next == break_index || !enforced(next))
                break;
            length += (perimeter.positions[next] - perimeter.positions[end]).norm();
            end = next;
            nearest_distance2 = std::min(nearest_distance2,
                (perimeter.positions[end] - choice.position).squaredNorm());
        }

        if (nearest_distance2 < best_distance2) {
            best_distance2 = nearest_distance2;
            if (start == end || length <= 1e-12) {
                best = {start, start, perimeter.positions[start]};
            } else {
                const double halfway = length / 2.0;
                std::size_t segment = start;
                double traveled = 0.0;
                while (segment != end) {
                    const std::size_t next = (segment + 1) % count;
                    const Vec2d &a = perimeter.positions[segment];
                    const Vec2d &b = perimeter.positions[next];
                    const double segment_length = (b - a).norm();
                    if (traveled + segment_length >= halfway) {
                        const double fraction = segment_length > 1e-12 ?
                            (halfway - traveled) / segment_length : 0.0;
                        best = {segment, next, a + fraction * (b - a)};
                        break;
                    }
                    traveled += segment_length;
                    segment = next;
                }
            }
        }
        index = (end + 1) % count;
    }
    return best;
}

} // namespace Slic3r::Seams
