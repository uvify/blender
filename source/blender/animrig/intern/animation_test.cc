/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "ANIM_animation.hh"

#include "BKE_animation.h"
#include "BKE_fcurve.h"

#include "DNA_anim_types.h"

#include "BLI_listbase.h"
#include "BLI_string_utf8.h"

#include <limits>

#include "testing/testing.h"

namespace blender::animrig::tests {

TEST(ANIM_animation_layers, add_layer)
{
  Animation anim = {};
  Layer *layer = anim.layer_add("layer name");

  EXPECT_EQ(anim.layer(0), layer);
  EXPECT_EQ("layer name", std::string(layer->name));
  EXPECT_EQ(1.0f, layer->influence) << "Expected DNA defaults to be used.";
  EXPECT_EQ(0, anim.layer_active_index)
      << "Expected newly added layer to become the active layer.";

  ASSERT_EQ(1, layer->strips().size()) << "Expected newly added layer to have a single strip.";

  Strip *strip = layer->strip(0);
  constexpr float inf = std::numeric_limits<float>::infinity();
  EXPECT_EQ(-inf, strip->frame_start) << "Expected strip to be infinite.";
  EXPECT_EQ(inf, strip->frame_end) << "Expected strip to be infinite.";
  EXPECT_EQ(0, strip->frame_offset) << "Expected infinite strip to have no offset.";

  BKE_animation_free_data(&anim);
}

TEST(ANIM_animation_layers, add_output)
{
  Animation anim = {};
  ID cube = {};
  STRNCPY_UTF8(cube.name, "OBKüüübus");

  Output *out = anim.output_add(&cube);
  EXPECT_EQ(1, anim.last_output_stable_index);
  EXPECT_EQ(1, out->stable_index);
  EXPECT_EQ("Küüübus", std::string(out->fallback));
  EXPECT_EQ(GS(cube.name), out->idtype);
  ASSERT_EQ(1, out->runtime->ids.size());
  EXPECT_EQ(&cube, out->runtime->ids[0]);

  BKE_animation_free_data(&anim);
}

TEST(ANIM_animation_layers, add_output_multiple)
{
  Animation anim = {};
  ID cube = {};
  STRNCPY_UTF8(cube.name, "OBKüüübus");
  ID suzanne = {};
  STRNCPY_UTF8(suzanne.name, "OBSuzanne");

  Output *out_cube = anim.output_add(&cube);
  Output *out_suzanne = anim.output_add(&suzanne);

  EXPECT_EQ(2, anim.last_output_stable_index);

  EXPECT_EQ(1, out_cube->stable_index);
  ASSERT_EQ(1, out_cube->runtime->ids.size());
  EXPECT_EQ(&cube, out_cube->runtime->ids[0]);

  EXPECT_EQ(2, out_suzanne->stable_index);
  ASSERT_EQ(1, out_suzanne->runtime->ids.size());
  EXPECT_EQ(&suzanne, out_suzanne->runtime->ids[0]);

  BKE_animation_free_data(&anim);
}

TEST(ANIM_animation_layers, keyframe_insert)
{
  Animation anim = {};
  ID cube = {};
  STRNCPY_UTF8(cube.name, "OBKüüübus");
  Output *out = anim.output_add(&cube);
  Layer *layer = anim.layer_add("Kübus layer");
  Strip *strip = layer->strip(0);

  FCurve *fcurve_loc_a = keyframe_insert(
      strip, *out, "location", 0, 47.0f, 1.0f, BEZT_KEYTYPE_KEYFRAME);
  ASSERT_NE(nullptr, fcurve_loc_a)
      << "Expect all the necessary data structures to be created on insertion of a key";

  /* Check the strip was created correctly, with the channels for the output. */
  KeyframeStrip &key_strip = strip->wrap().as<KeyframeStrip>();
  ASSERT_EQ(1, key_strip.channels_for_output().size());
  ChannelsForOutput *chan_for_out = key_strip.channel_for_output(0);
  EXPECT_EQ(out->stable_index, chan_for_out->output_stable_index);

  /* Insert a second key, should insert into the same FCurve as before. */
  FCurve *fcurve_loc_b = keyframe_insert(
      strip, *out, "location", 0, 47.1f, 5.0f, BEZT_KEYTYPE_KEYFRAME);
  ASSERT_EQ(fcurve_loc_a, fcurve_loc_b)
      << "Expect same (output/rna path/array index) tuple to return the same FCurve.";

  EXPECT_EQ(2, fcurve_loc_b->totvert);
  EXPECT_EQ(47.0f, evaluate_fcurve(fcurve_loc_a, 1.0f));
  EXPECT_EQ(47.1f, evaluate_fcurve(fcurve_loc_a, 5.0f));

  /* Insert another key for another property, should create another FCurve. */
  FCurve *fcurve_rot = keyframe_insert(
      strip, *out, "rotation_quaternion", 0, 0.25f, 1.0f, BEZT_KEYTYPE_KEYFRAME);
  EXPECT_NE(fcurve_loc_b, fcurve_rot)
      << "Expected rotation and location curves to be different FCurves.";
  EXPECT_EQ(2, chan_for_out->fcurves().size()) << "Expected a second FCurve to be created.";

  /* TODO: test with finite strips & strip offsets. */

  BKE_animation_free_data(&anim);
}

}  // namespace blender::animrig::tests
