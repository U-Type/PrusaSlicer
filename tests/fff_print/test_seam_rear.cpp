#include <libslic3r/Point.hpp>
#include <catch2/catch_test_macros.hpp>
#include <libslic3r/GCode/SeamRear.hpp>
#include "test_data.hpp"
#include <fstream>

using namespace Slic3r;
using namespace Slic3r::Seams;

constexpr bool debug_files{false};

namespace RearTest {
Perimeters::Perimeter get_perimeter() {
    const double slice_z{1.0};
    const std::size_t layer_index{};
    std::vector<Vec2d> positions{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.5, 1.0}, {0.0, 1.0}};
    std::vector<double> angles(positions.size(), -M_PI / 2.0);
    angles[3] = 0.0;
    std::vector<Perimeters::PointType> point_types(positions.size(), Perimeters::PointType::common);
    std::vector<Perimeters::PointClassification>
        point_classifications{positions.size(), Perimeters::PointClassification::common};
    std::vector<Perimeters::AngleType> angle_type(positions.size(), Perimeters::AngleType::concave);
    angle_type[3] = Perimeters::AngleType::smooth;

    return {
        slice_z,
        layer_index,
        false,
        std::move(positions),
        std::move(angles),
        std::move(point_types),
        std::move(point_classifications),
        std::move(angle_type)};
}
} // namespace RearTest

TEST_CASE_METHOD(Test::SeamsFixture, "Generate rear seam", "[Seams][SeamRear][Integration]") {
    Seams::Perimeters::LayerPerimeters perimeters{
        Seams::Perimeters::create_perimeters(projected, layer_infos, painting, params.perimeter)};
    const std::vector<std::vector<SeamPerimeterChoice>> seams{
        Rear::get_object_seams(std::move(perimeters), params.rear_tolerance, params.rear_y_offset)};

    if constexpr (debug_files) {
        std::ofstream csv{"rear_seam.csv"};
        Test::serialize_seam(csv, seams);
    }
}

// MOD: The Front seam must use the same perimeter choices as Rear,
// with the Y preference inverted rather than altering the perimeter winding.
TEST_CASE_METHOD(Test::SeamsFixture, "Front seam mirrors rear side selection", "[Seams][SeamRear][Integration]") {
    auto rear_perimeters = Seams::Perimeters::create_perimeters(projected, layer_infos, painting, params.perimeter);
    auto front_perimeters = Seams::Perimeters::create_perimeters(projected, layer_infos, painting, params.perimeter);
    const auto rear_seams = Rear::get_object_seams(std::move(rear_perimeters), params.rear_tolerance, params.rear_y_offset);
    const auto front_seams = Rear::get_object_seams(std::move(front_perimeters), params.rear_tolerance, params.rear_y_offset, true);

    REQUIRE(front_seams.size() == rear_seams.size());
    for (std::size_t layer = 0; layer < rear_seams.size(); ++layer) {
        REQUIRE(front_seams[layer].size() == rear_seams[layer].size());
        for (std::size_t i = 0; i < rear_seams[layer].size(); ++i) {
            if (!rear_seams[layer][i].perimeter.is_degenerate &&
                !front_seams[layer][i].perimeter.is_degenerate) {
                CHECK(front_seams[layer][i].choice.position.y() <=
                      rear_seams[layer][i].choice.position.y() + 1e-6);
            }
        }
    }
}
