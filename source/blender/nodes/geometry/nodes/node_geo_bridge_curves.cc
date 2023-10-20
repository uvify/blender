/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BKE_curves.hh"
#include "BKE_mesh.hh"

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
    Mesh *mesh = BKE_mesh_new_nomain(evaluated_curve_positions.size(), 0, 0, 0);

    MutableSpan<float3> mesh_positions = mesh->vert_positions_for_write();
    mesh_positions.copy_from(evaluated_curve_positions);
    BKE_mesh_tag_positions_changed(mesh);

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
