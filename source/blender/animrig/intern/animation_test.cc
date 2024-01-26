/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "ANIM_animation.hh"

#include "BKE_animation.h"
#include "BKE_fcurve.h"
#include "BKE_idtype.hh"

#include "DNA_anim_types.h"

#include "BLI_listbase.h"
#include "BLI_string_utf8.h"

#include <limits>

#include "testing/testing.h"

namespace blender::animrig::tests {
class AnimationLayersTest : public testing::Test {
 public:
  static void SetUpTestSuite()
  {
    /* To make id_can_have_animdata() and friends work, the `id_types` array needs to be set up. */
    BKE_idtype_init();
  }
};

TEST_F(AnimationLayersTest, add_layer)
{
  Animation anim = {};
  Layer *layer = anim.layer_add("layer name");

  EXPECT_EQ(anim.layer(0), layer);
  EXPECT_EQ("layer name", std::string(layer->name));
  EXPECT_EQ(1.0f, layer->influence) << "Expected DNA defaults to be used.";
  EXPECT_EQ(0, anim.layer_active_index)
      << "Expected newly added layer to become the active layer.";
  ASSERT_EQ(0, layer->strips().size()) << "Expected newly added layer to have no strip.";

  Strip *strip = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  ASSERT_EQ(1, layer->strips().size());

  constexpr float inf = std::numeric_limits<float>::infinity();
  EXPECT_EQ(-inf, strip->frame_start) << "Expected strip to be infinite.";
  EXPECT_EQ(inf, strip->frame_end) << "Expected strip to be infinite.";
  EXPECT_EQ(0, strip->frame_offset) << "Expected infinite strip to have no offset.";

  BKE_animation_free_data(&anim);
}

TEST_F(AnimationLayersTest, add_output)
{
  Animation anim = {};
  ID cube = {};
  STRNCPY_UTF8(cube.name, "OBKüüübus");

  Output *out = anim.output_add();
  EXPECT_EQ(1, anim.last_output_stable_index);
  ASSERT_NE(nullptr, out);
  EXPECT_EQ(1, out->stable_index);

  EXPECT_EQ("", std::string(out->fallback));
  EXPECT_EQ(0, out->idtype);

  out->assign_id(&cube);
  EXPECT_EQ("Küüübus", std::string(out->fallback));
  EXPECT_EQ(GS(cube.name), out->idtype);

  BKE_animation_free_data(&anim);
}

TEST_F(AnimationLayersTest, add_output_multiple)
{
  Animation anim = {};
  ID cube = {};
  STRNCPY_UTF8(cube.name, "OBKüüübus");
  ID suzanne = {};
  STRNCPY_UTF8(suzanne.name, "OBSuzanne");

  Output *out_cube = anim.output_add();
  Output *out_suzanne = anim.output_add();
  out_cube->assign_id(&cube);
  out_suzanne->assign_id(&suzanne);

  EXPECT_EQ(2, anim.last_output_stable_index);
  EXPECT_EQ(1, out_cube->stable_index);
  EXPECT_EQ(2, out_suzanne->stable_index);

  BKE_animation_free_data(&anim);
}

TEST_F(AnimationLayersTest, strip)
{
  constexpr float inf = std::numeric_limits<float>::infinity();
  Strip strip;

  strip.resize(-inf, inf);
  EXPECT_TRUE(strip.contains_frame(0.0f));
  EXPECT_TRUE(strip.contains_frame(-100000.0f));
  EXPECT_TRUE(strip.contains_frame(100000.0f));
  EXPECT_TRUE(strip.is_last_frame(inf));

  strip.resize(1.0f, 2.0f);
  EXPECT_FALSE(strip.contains_frame(0.0f))
      << "Strip should not contain frames before its first frame";
  EXPECT_TRUE(strip.contains_frame(1.0f)) << "Strip should contain its first frame.";
  EXPECT_TRUE(strip.contains_frame(2.0f)) << "Strip should contain its last frame.";
  EXPECT_FALSE(strip.contains_frame(2.0001f))
      << "Strip should not contain frames after its last frame";

  EXPECT_FALSE(strip.is_last_frame(1.0f));
  EXPECT_FALSE(strip.is_last_frame(1.5f));
  EXPECT_FALSE(strip.is_last_frame(1.9999f));
  EXPECT_TRUE(strip.is_last_frame(2.0f));
  EXPECT_FALSE(strip.is_last_frame(2.0001f));

  /* Same test as above, but with much larger end frame number. This is 2 hours at 24 FPS. */
  strip.resize(1.0f, 172800.0f);
  EXPECT_TRUE(strip.contains_frame(172800.0f)) << "Strip should contain its last frame.";
  EXPECT_FALSE(strip.contains_frame(172800.1f))
      << "Strip should not contain frames after its last frame";

  /* You can't get much closer to the end frame before it's considered equal. */
  EXPECT_FALSE(strip.is_last_frame(172799.925f));
  EXPECT_TRUE(strip.is_last_frame(172800.0f));
  EXPECT_FALSE(strip.is_last_frame(172800.075f));
}

TEST_F(AnimationLayersTest, KeyframeStrip__keyframe_insert)
{
  Animation anim = {};
  ID cube = {};
  STRNCPY_UTF8(cube.name, "OBKüüübus");
  Output *out = anim.output_add();
  out->assign_id(&cube);
  Layer *layer = anim.layer_add("Kübus layer");

  Strip *strip = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  KeyframeStrip &key_strip = strip->as<KeyframeStrip>();

  const KeyframeSettings settings = get_keyframe_settings(false);
  FCurve *fcurve_loc_a = key_strip.keyframe_insert(*out, "location", 0, {1.0f, 47.0f}, settings);
  ASSERT_NE(nullptr, fcurve_loc_a)
      << "Expect all the necessary data structures to be created on insertion of a key";

  /* Check the strip was created correctly, with the channels for the output. */
  ASSERT_EQ(1, key_strip.channels_for_output().size());
  ChannelsForOutput *chan_for_out = key_strip.channel_for_output(0);
  EXPECT_EQ(out->stable_index, chan_for_out->output_stable_index);

  /* Insert a second key, should insert into the same FCurve as before. */
  FCurve *fcurve_loc_b = key_strip.keyframe_insert(*out, "location", 0, {5.0f, 47.1f}, settings);
  ASSERT_EQ(fcurve_loc_a, fcurve_loc_b)
      << "Expect same (output/rna path/array index) tuple to return the same FCurve.";

  EXPECT_EQ(2, fcurve_loc_b->totvert);
  EXPECT_EQ(47.0f, evaluate_fcurve(fcurve_loc_a, 1.0f));
  EXPECT_EQ(47.1f, evaluate_fcurve(fcurve_loc_a, 5.0f));

  /* Insert another key for another property, should create another FCurve. */
  FCurve *fcurve_rot = key_strip.keyframe_insert(
      *out, "rotation_quaternion", 0, {1.0f, 0.25f}, settings);
  EXPECT_NE(fcurve_loc_b, fcurve_rot)
      << "Expected rotation and location curves to be different FCurves.";
  EXPECT_EQ(2, chan_for_out->fcurves().size()) << "Expected a second FCurve to be created.";

  /* TODO: test with finite strips & strip offsets. */

  BKE_animation_free_data(&anim);
}

}  // namespace blender::animrig::tests
