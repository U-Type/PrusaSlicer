#include "libslic3r/GCode/SeamRear.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/GCode/SeamChoice.hpp"
#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/GCode/SeamShells.hpp"

namespace Slic3r::Seams::Rear {
using Perimeters::PointType;
using Perimeters::PointClassification;

namespace Impl {

BoundingBoxf get_bounding_box(const Shells::Shell<> &shell) {
    BoundingBoxf result;
    for (const Shells::Slice<> &slice : shell) {
        result.merge(BoundingBoxf{slice.boundary.positions});
    }
    return result;
}

std::optional<SeamChoice> get_clear_max_y_corner(
    const std::vector<PerimeterLine> &possible_lines,
    const Perimeters::Perimeter &perimeter,
    const SeamChoice &max_y_choice,
    const double rear_tolerance,
    const bool front
) {
    const double y_direction{front ? -1.0 : 1.0};
    if (perimeter.angle_types[max_y_choice.previous_index] != Perimeters::AngleType::concave) {
        return std::nullopt;
    }

    const double epsilon{1e-2};

    // Check if there are two max y corners (e.g. on a cube).
    for (const PerimeterLine &line : possible_lines) {
        if (
            line.previous_index != max_y_choice.previous_index
            && perimeter.angle_types[line.previous_index] == Perimeters::AngleType::concave
            && y_direction * max_y_choice.position.y() < y_direction * line.a.y() + epsilon
            && (max_y_choice.position - line.a).norm() > epsilon
        ) {
            return std::nullopt;
        }
        if (
            line.next_index != max_y_choice.next_index
            && perimeter.angle_types[line.next_index] == Perimeters::AngleType::concave
            && y_direction * max_y_choice.position.y() < y_direction * line.b.y() + epsilon
            && (max_y_choice.position - line.b).norm() > epsilon
        ) {
            return std::nullopt;
        }
    }

    return max_y_choice;
}

SeamChoice get_max_y_choice(const std::vector<PerimeterLine> &possible_lines, const bool front) {
    const double y_direction{front ? -1.0 : 1.0};
    if (possible_lines.empty()) {
        throw std::runtime_error{"No possible lines!"};
    }

    Vec2d point{possible_lines.front().a};
    std::size_t point_index{possible_lines.front().previous_index};

    for (const PerimeterLine &line : possible_lines) {
        if (y_direction * line.a.y() > y_direction * point.y()) {
            point = line.a;
            point_index = line.previous_index;
        }
        if (y_direction * line.b.y() > y_direction * point.y()) {
            point = line.b;
            point_index = line.next_index;
        }
    }

    return SeamChoice{point_index, point_index, point};
}

SeamChoice get_nearest(
    const AABBTreeLines::LinesDistancer<PerimeterLine>& distancer,
    const Vec2d point
) {
    const auto [_, line_index, resulting_point] = distancer.distance_from_lines_extra<false>(point);
    return SeamChoice{
        distancer.get_lines()[line_index].previous_index,
        distancer.get_lines()[line_index].next_index,
        resulting_point
    };
}

struct RearestPointCalculator {
    double rear_tolerance;
    double rear_y_offset;
    BoundingBoxf bounding_box;
    bool front;

    std::optional<SeamChoice> operator()(
        const Perimeters::Perimeter &perimeter,
        const PointType point_type,
        const PointClassification point_classification
    ) {
        std::vector<PerimeterLine> possible_lines;
        for (std::size_t i{0}; i < perimeter.positions.size(); ++i) {
            const std::size_t next_index{i == perimeter.positions.size() - 1 ? 0 : i + 1};
            if (perimeter.point_types[i] != point_type) {
                continue;
            }
            if (perimeter.point_classifications[i] != point_classification) {
                continue;
            }
            if (perimeter.point_types[next_index] != point_type) {
                continue;
            }
            if (perimeter.point_classifications[next_index] != point_classification) {
                continue;
            }
            possible_lines.push_back(PerimeterLine{perimeter.positions[i], perimeter.positions[next_index], i, next_index});
        }
        if (possible_lines.empty()) {
            return std::nullopt;
        }

        const double y_direction{front ? -1.0 : 1.0};
        const SeamChoice max_y_choice{get_max_y_choice(possible_lines, front)};

        if (const auto clear_max_y_corner{get_clear_max_y_corner(
            possible_lines,
            perimeter,
            max_y_choice,
            rear_tolerance,
            front
        )}) {
            return *clear_max_y_corner;
        }

        const BoundingBoxf bounding_box{perimeter.positions};
        const AABBTreeLines::LinesDistancer<PerimeterLine> possible_distancer{possible_lines};
        const double center_x{(bounding_box.max.x() + bounding_box.min.x()) / 2.0};
        // MOD: Front is the minimum-Y counterpart of Rear's maximum-Y target.
        const double boundary_y{front ? bounding_box.min.y() : bounding_box.max.y()};
        const Vec2d prefered_position{center_x, boundary_y + y_direction * rear_y_offset};
        auto [_, line_index, point] = possible_distancer.distance_from_lines_extra<false>(prefered_position);
        const Vec2d location_at_bb{center_x, boundary_y};
        auto [_d, line_index_at_bb, point_bb] = possible_distancer.distance_from_lines_extra<false>(location_at_bb);
        const double y_distance{y_direction * (point.y() - point_bb.y())};

        SeamChoice result{possible_lines[line_index].previous_index, possible_lines[line_index].next_index, point};

        if (y_distance < 0) {
            result = get_nearest(
                possible_distancer,
                point_bb
            );
        } else if (y_distance <= rear_tolerance) {
            const double factor{y_distance / rear_tolerance};
            result = get_nearest(
                possible_distancer,
                factor * point +  (1 - factor) * point_bb
            );
        }

        if (y_direction * (boundary_y - result.position.y()) > rear_tolerance) {
            return max_y_choice;
        }

        return result;
    }
};
} // namespace Impl

std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(
    std::vector<std::vector<Perimeters::BoundedPerimeter>> &&perimeters,
    const double rear_tolerance,
    const double rear_y_offset,
    const bool front
) {
    std::vector<std::vector<SeamPerimeterChoice>> result;

    for (std::vector<Perimeters::BoundedPerimeter> &layer : perimeters) {
        result.emplace_back();
        for (Perimeters::BoundedPerimeter &perimeter : layer) {
            if (perimeter.perimeter.is_degenerate) {
                std::optional<Seams::SeamChoice> seam_choice{
                    Seams::choose_degenerate_seam_point(perimeter.perimeter)};
                if (seam_choice) {
                    result.back().push_back(
                        SeamPerimeterChoice{*seam_choice, std::move(perimeter.perimeter)}
                    );
                } else {
                    result.back().push_back(SeamPerimeterChoice{SeamChoice{}, std::move(perimeter.perimeter)});
                }
            } else {
                BoundingBoxf bounding_box{unscaled(perimeter.bounding_box)};
                const SeamChoice seam_choice{Seams::choose_seam_point(
                    perimeter.perimeter,
                    Impl::RearestPointCalculator{rear_tolerance, rear_y_offset, bounding_box, front}
                )};
                result.back().push_back(
                    SeamPerimeterChoice{seam_choice, std::move(perimeter.perimeter)}
                );
            }
        }
    }

    return result;
}
} // namespace Slic3r::Seams::Rear
