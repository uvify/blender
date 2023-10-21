/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BKE_curves.hh"
#include "BKE_mesh.hh"

#include "BLI_array_utils.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_bridge_curves_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Geometry>("Curves");
  b.add_input<decl::Bool>("Selection").default_value(true).field_on_all();
  b.add_input<decl::Int>("Group ID").hide_value().field_on_all();
  b.add_input<decl::Float>("Weight").field_on_all().hide_value();
  b.add_output<decl::Geometry>("Mesh").propagate_all();
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet curves_geometry = params.extract_input<GeometrySet>("Curves");
  GeometrySet mesh_geometry;
  if (const Curves *curves_id = curves_geometry.get_curves()) {
    const bke::CurvesGeometry &curves = curves_id->geometry.wrap();
    const Span<float3> evaluated_curve_positions = curves.evaluated_positions();
    const OffsetIndices evaluated_points_by_curve = curves.evaluated_points_by_curve();
    const VArray<bool> cyclic = curves.cyclic();

    const int curves_num = curves.curves_num();

    Array<int> curve_indices_to_bridge(curves_num);
    array_utils::fill_index_range<int>(curve_indices_to_bridge);
    const bool bridge_cyclic = false;
    const int bridges_num = bke::curves::segments_num(curve_indices_to_bridge.size(),
                                                      bridge_cyclic);

    Array<int> bridge_face_offsets(curves_num);
    Array<int> bridge_corner_offsets(curves_num);

    int face_counter = 0;
    int corner_counter = 0;

    for (const int i : IndexRange(bridges_num)) {
      const int next_i = (i + 1) % curve_indices_to_bridge.size();
      const int curve_i = curve_indices_to_bridge[i];
      const int next_curve_i = curve_indices_to_bridge[next_i];
      const bool is_cyclic = cyclic[curve_i];
      const bool next_is_cyclic = cyclic[next_curve_i];

      const IndexRange points = evaluated_points_by_curve[curve_i];
      const IndexRange next_points = evaluated_points_by_curve[next_curve_i];

      bridge_face_offsets[curve_i] = face_counter;
      bridge_corner_offsets[curve_i] = corner_counter;

      if (points.is_empty() || next_points.is_empty()) {
        continue;
      }

      const int segments_num = bke::curves::segments_num(points.size(), is_cyclic);
      const int next_segments_num = bke::curves::segments_num(next_points.size(), next_is_cyclic);
      const int bridge_face_num = std::max(segments_num, next_segments_num);
      const int bridge_quad_num = std::min(segments_num, next_segments_num);
      const int bridge_tri_num = bridge_face_num - bridge_quad_num;
      const int bridge_corner_num = 4 * bridge_quad_num + 3 * bridge_tri_num;

      face_counter += bridge_face_num;
      corner_counter += bridge_corner_num;
    }

    Mesh *mesh = BKE_mesh_new_nomain(
        evaluated_curve_positions.size(), 0, face_counter, corner_counter);

    MutableSpan<int> dst_face_offsets = mesh->face_offsets_for_write();
    MutableSpan<int> dst_corner_verts = mesh->corner_verts_for_write();

    MutableSpan<float3> mesh_positions = mesh->vert_positions_for_write();
    mesh_positions.copy_from(evaluated_curve_positions);
    BKE_mesh_tag_positions_changed(mesh);

    if (face_counter > 0) {
      dst_face_offsets[0] = 0;
    }

    for (const int i : IndexRange(bridges_num)) {
      const int next_i = (i + 1) % curve_indices_to_bridge.size();
      const int curve_i = curve_indices_to_bridge[i];
      const int next_curve_i = curve_indices_to_bridge[next_i];
      const bool is_cyclic = cyclic[curve_i];
      const bool next_is_cyclic = cyclic[next_curve_i];

      const int face_offset = bridge_face_offsets[curve_i];
      const int corner_offset = bridge_corner_offsets[curve_i];

      const IndexRange points = evaluated_points_by_curve[curve_i];
      const IndexRange next_points = evaluated_points_by_curve[next_curve_i];

      if (points.is_empty() || next_points.is_empty()) {
        continue;
      }

      const int segments_num = bke::curves::segments_num(points.size(), is_cyclic);
      const int next_segments_num = bke::curves::segments_num(next_points.size(), next_is_cyclic);
      const int bridge_face_num = std::max(segments_num, next_segments_num);
      const int bridge_quad_num = std::min(segments_num, next_segments_num);
      const int bridge_tri_num = bridge_face_num - bridge_quad_num;
      const int bridge_corner_num = 4 * bridge_quad_num + 3 * bridge_tri_num;

      int corner_index = corner_offset;
      for (const int face_i : IndexRange(bridge_face_num)) {
        const bool from_single_point = face_i >= segments_num;
        const bool to_single_point = face_i >= next_segments_num;
        if (!from_single_point && !to_single_point) {
          dst_corner_verts[corner_index + 0] = points[face_i];
          dst_corner_verts[corner_index + 1] = next_points[face_i];
          dst_corner_verts[corner_index + 2] = next_points[(face_i + 1) % next_points.size()];
          dst_corner_verts[corner_index + 3] = points[(face_i + 1) % points.size()];
          corner_index += 4;
        }
        else if (from_single_point) {
          dst_corner_verts[corner_index + 0] =
              points[std::min(face_i, segments_num) % points.size()];
          dst_corner_verts[corner_index + 1] = next_points[face_i];
          dst_corner_verts[corner_index + 2] = next_points[(face_i + 1) % next_points.size()];
          corner_index += 3;
        }
        else {
          BLI_assert(to_single_point);
          dst_corner_verts[corner_index + 0] = points[face_i];
          dst_corner_verts[corner_index + 1] =
              next_points[std::min(face_i, next_segments_num) % next_points.size()];
          dst_corner_verts[corner_index + 2] = points[(face_i + 1) % points.size()];
          corner_index += 3;
        }
        dst_face_offsets[face_offset + face_i + 1] = corner_index;
      }
    }

    BKE_mesh_calc_edges(mesh, false, false);
    mesh_geometry.replace_mesh(mesh);
  }

  params.set_output("Mesh", mesh_geometry);
}

static void node_register()
{
  static bNodeType ntype;
  geo_node_type_base(&ntype, GEO_NODE_BRIDGE_CURVES, "Bridge Curves", NODE_CLASS_GEOMETRY);
  ntype.geometry_node_execute = node_geo_exec;
  ntype.declare = node_declare;
  nodeRegisterType(&ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_bridge_curves_cc
