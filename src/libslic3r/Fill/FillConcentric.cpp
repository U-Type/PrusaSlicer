///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas
///|/
///|/ ported from lib/Slic3r/Fill/Concentric.pm:
///|/ Copyright (c) Prusa Research 2016 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 Mark Hindess
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <algorithm>
#include <vector>
#include <cassert>
#include <cstddef>
#include <limits>
#include "libslic3r/Line.hpp"

#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "FillConcentric.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

// MOD: Select the projection onto the closest segment, rather than the
// nearest polygon vertex. With a square, vertex-only splitting started
// Concentric top/bottom infill at a corner, far from the perimeter's seam.
struct ConcentricSeamPoint {
    size_t segment;
    Point position;
};

static ConcentricSeamPoint concentric_seam_point(const Points &points, const Point &target, bool duplicate_endpoint)
{
    const size_t segment_count = points.size() - (duplicate_endpoint ? 1 : 0);
    double nearest_distance = std::numeric_limits<double>::max();
    ConcentricSeamPoint result{0, points.front()};
    for (size_t i = 0; i < segment_count; ++i) {
        Point projected;
        const double distance = line_alg::distance_to_squared(
            Line(points[i], points[(i + 1) % points.size()]), target, &projected);
        if (distance < nearest_distance) {
            nearest_distance = distance;
            result = {i, projected};
        }
    }
    return result;
}

static Point concentric_seam_target(const BoundingBox &box, int side)
{
    return {box.min.x() + (box.max.x() - box.min.x()) / 2,
        side > 0 ? box.max.y() + scaled<coord_t>(20.) :
                   box.min.y() - scaled<coord_t>(20.)};
}

// Split on the chosen edge, inserting its interior point when necessary.
// Retain original winding and all geometric segments prior to gap clipping.
static Polyline concentric_split_at_side(const Polygon &polygon, const Point &target)
{
    const ConcentricSeamPoint seam = concentric_seam_point(polygon.points, target, false);
    Polyline path;
    path.points.reserve(polygon.points.size() + 2);
    path.points.emplace_back(seam.position);
    for (size_t n = 1; n <= polygon.points.size(); ++n) {
        const Point &p = polygon.points[(seam.segment + n) % polygon.points.size()];
        if (p != seam.position)
            path.points.emplace_back(p);
    }
    path.points.emplace_back(seam.position);
    return path;
}

// For Arachne's variable-width lines, preserve interpolation of extrusion
// widths when a seam falls inside a segment (two endpoint widths per edge).
static void concentric_start_at_side(ThickPolyline &line, const Point &target)
{
    if (line.points.size() < 4 || line.points.front() != line.points.back())
        return;

    const ConcentricSeamPoint seam = concentric_seam_point(line.points, target, true);
    const size_t next = seam.segment + 1;
    size_t index;
    if (seam.position == line.points[seam.segment]) {
        index = seam.segment;
    } else if (seam.position == line.points[next]) {
        index = next == line.points.size() - 1 ? 0 : next;
    } else {
        const double length = (line.points[next] - line.points[seam.segment]).cast<double>().norm();
        if (length <= 0.0)
            return;
        const double fraction = std::min(1.0,
            (seam.position - line.points[seam.segment]).cast<double>().norm() / length);
        const coordf_t width = line.width[2 * seam.segment] +
            fraction * (line.width[2 * seam.segment + 1] - line.width[2 * seam.segment]);
        line.points.insert(line.points.begin() + next, seam.position);
        line.width.insert(line.width.begin() + 2 * seam.segment + 1, {width, width});
        index = next;
    }
    line.start_at_index(static_cast<int>(index));
}

void FillConcentric::_fill_surface_single(
    const FillParams                &params,
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction,
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    // no rotation is supported for this infill pattern
    BoundingBox bounding_box = expolygon.contour.bounding_box();

    coord_t min_spacing = scaled<coord_t>(this->spacing);
    coord_t distance    = coord_t(min_spacing / params.density);

    if (params.density > 0.9999f && !params.dont_adjust) {
        distance      = Slic3r::FillConcentric::_adjust_solid_spacing(bounding_box.size()(0), distance);
        this->spacing = unscale<double>(distance);
    }

    Polygons   loops = to_polygons(expolygon);
    ExPolygons last { std::move(expolygon) };
    while (! last.empty()) {
        last = offset2_ex(last, -(distance + min_spacing/2), +min_spacing/2);
        append(loops, to_polygons(last));
    }

    // generate paths from the outermost to the innermost, to avoid
    // adhesion problems of the first central tiny loops
    loops = union_pt_chained_outside_in(loops);

    // split paths using a nearest neighbor search
    size_t iPathFirst = polylines_out.size();
    Point last_pos(0, 0);
    for (const Polygon &loop : loops) {
        // MOD: For top / bottom concentric infill, align each loop independently
        // to its own back/front extreme. Do not chain its start to the previous
        // path's end, as that drifts seams around offset and island boundaries.
        if (params.concentric_seam_y_direction != 0 || params.force_clockwise_concentric) {
            Polygon oriented_loop = loop;
            if (params.force_clockwise_concentric)
                oriented_loop.make_clockwise(); // contours and holes
            else if (params.prefer_clockwise_movements)
                oriented_loop.reverse(); // retain the older printer-level behavior

            if (params.concentric_seam_y_direction != 0)
                polylines_out.emplace_back(concentric_split_at_side(oriented_loop,
                    concentric_seam_target(oriented_loop.bounding_box(), params.concentric_seam_y_direction)));
            else
                polylines_out.emplace_back(oriented_loop.split_at_index(
                    nearest_point_index(oriented_loop.points, last_pos)));
        } else {
            polylines_out.emplace_back(loop.split_at_index(nearest_point_index(loop.points, last_pos)));
        }
        last_pos = polylines_out.back().last_point();
    }

    // clip the paths to prevent the extruder from getting exactly on the first point of the loop
    // Keep valid paths only.
    size_t j = iPathFirst;
    for (size_t i = iPathFirst; i < polylines_out.size(); ++i) {
        polylines_out[i].clip_end(this->loop_clipping);
        if (polylines_out[i].is_valid()) {
            if (params.prefer_clockwise_movements && !params.force_clockwise_concentric && params.concentric_seam_y_direction == 0)
                polylines_out[i].reverse();

            if (j < i)
                polylines_out[j] = std::move(polylines_out[i]);

            ++j;
        }
    }

    if (j < polylines_out.size())
        polylines_out.erase(polylines_out.begin() + int(j), polylines_out.end());
    //TODO: return ExtrusionLoop objects to get better chained paths,
    // otherwise the outermost loop starts at the closest point to (0, 0).
    // We want the loops to be split inside the G-code generator to get optimum path planning.
}

void FillConcentric::_fill_surface_single(const FillParams              &params,
                                          unsigned int                   thickness_layers,
                                          const std::pair<float, Point> &direction,
                                          ExPolygon                      expolygon,
                                          ThickPolylines                &thick_polylines_out)
{
    assert(params.use_arachne);
    assert(this->print_config != nullptr && this->print_object_config != nullptr);

    // no rotation is supported for this infill pattern
    Point   bbox_size   = expolygon.contour.bounding_box().size();
    coord_t min_spacing = scaled<coord_t>(this->spacing);

    if (params.density > 0.9999f && !params.dont_adjust) {
        coord_t                loops_count = std::max(bbox_size.x(), bbox_size.y()) / min_spacing + 1;
        Polygons               polygons    = offset(expolygon, float(min_spacing) / 2.f);
        Arachne::WallToolPaths wallToolPaths(polygons, min_spacing, min_spacing, loops_count, 0, params.layer_height, *this->print_object_config, *this->print_config);

        std::vector<Arachne::VariableWidthLines>    loops = wallToolPaths.getToolPaths();
        std::vector<const Arachne::ExtrusionLine *> all_extrusions;
        for (Arachne::VariableWidthLines &loop : loops) {
            if (loop.empty())
                continue;

            for (const Arachne::ExtrusionLine &wall : loop)
                all_extrusions.emplace_back(&wall);
        }

        // Split paths using a nearest neighbor search.
        size_t firts_poly_idx = thick_polylines_out.size();
        Point  last_pos(0, 0);
        for (const Arachne::ExtrusionLine *extrusion : all_extrusions) {
            if (extrusion->empty())
                continue;

            ThickPolyline thick_polyline = Arachne::to_thick_polyline(*extrusion);
            if (extrusion->is_closed) {
                // Arachne produces contour with clockwise orientation and holes with counterclockwise orientation.
                if (params.force_clockwise_concentric) {
                    // Determine winding from the generated geometry; Arachne's
                    // contour/hole flag alone is insufficient for every island.
                    if (Polygon(thick_polyline.points).is_counter_clockwise())
                        thick_polyline.reverse();
                } else if (const bool extrusion_reverse =
                        params.prefer_clockwise_movements ? !extrusion->is_contour() : extrusion->is_contour();
                        extrusion_reverse) {
                    thick_polyline.reverse();
                }

                if (params.concentric_seam_y_direction != 0) {
                    concentric_start_at_side(thick_polyline,
                        concentric_seam_target(BoundingBox(thick_polyline.points),
                                              params.concentric_seam_y_direction));
                } else {
                    thick_polyline.start_at_index(nearest_point_index(thick_polyline.points, last_pos));
                }
            }

            thick_polylines_out.emplace_back(std::move(thick_polyline));
            last_pos = thick_polylines_out.back().last_point();
        }

        // clip the paths to prevent the extruder from getting exactly on the first point of the loop
        // Keep valid paths only.
        size_t j = firts_poly_idx;
        for (size_t i = firts_poly_idx; i < thick_polylines_out.size(); ++i) {
            thick_polylines_out[i].clip_end(this->loop_clipping);
            if (thick_polylines_out[i].is_valid()) {
                if (j < i)
                    thick_polylines_out[j] = std::move(thick_polylines_out[i]);
                ++j;
            }
        }
        if (j < thick_polylines_out.size())
            thick_polylines_out.erase(thick_polylines_out.begin() + int(j), thick_polylines_out.end());
    } else {
        Polylines polylines;
        this->_fill_surface_single(params, thickness_layers, direction, expolygon, polylines);
        append(thick_polylines_out, to_thick_polylines(std::move(polylines), min_spacing));
    }
}

} // namespace Slic3r
