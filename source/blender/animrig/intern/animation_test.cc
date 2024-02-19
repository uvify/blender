/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "ANIM_animation.hh"

#include "BKE_anim_data.h"
#include "BKE_animation.hh"
#include "BKE_fcurve.h"
#include "BKE_idtype.hh"
#include "BKE_lib_id.hh"
#include "BKE_object.hh"

#include "DNA_anim_types.h"
#include "DNA_object_types.h"

#include "BLI_listbase.h"
#include "BLI_string_utf8.h"

#include <limits>

#include "CLG_log.h"
#include "testing/testing.h"

namespace blender::animrig::tests {
class AnimationLayersTest : public testing::Test {
 public:
  Animation anim;

  Object *cube;
  Object *suzanne;

  static void SetUpTestSuite()
  {
    /* BKE_id_free() hits a code path that uses CLOG, which crashes if not initialised properly. */
    CLG_init();

    /* To make id_can_have_animdata() and friends work, the `id_types` array needs to be set up. */
    BKE_idtype_init();
  }

  static void TearDownTestSuite()
  {
    CLG_exit();
  }

  void SetUp() override
  {
    anim = {};
    STRNCPY_UTF8(anim.id.name, "ANÄnimåtië");

    cube = BKE_object_add_only_object(nullptr, OB_EMPTY, "Küüübus");
    suzanne = BKE_object_add_only_object(nullptr, OB_EMPTY, "OBSuzanne");
  }

  void TearDown() override
  {
    BKE_id_free(nullptr, &cube->id);
    BKE_id_free(nullptr, &suzanne->id);
    BKE_animation_free_data(&anim);
  }
};

TEST_F(AnimationLayersTest, add_layer)
{
  Layer *layer = anim.layer_add("layer name");

  EXPECT_EQ(anim.layer(0), layer);
  EXPECT_EQ("layer name", std::string(layer->name));
  EXPECT_EQ(1.0f, layer->influence) << "Expected DNA defaults to be used.";
  EXPECT_EQ(0, anim.layer_active_index)
      << "Expected newly added layer to become the active layer.";
  ASSERT_EQ(0, layer->strips().size()) << "Expected newly added layer to have no strip.";
}

TEST_F(AnimationLayersTest, remove_layer)
{
  Layer &layer0 = *anim.layer_add("Test Læür nul");
  Layer &layer1 = *anim.layer_add("Test Læür één");
  Layer &layer2 = *anim.layer_add("Test Læür twee");

  /* Add some strips to check that they are freed correctly too (implicitly by the
   * memory leak checker). */
  layer0.strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  layer1.strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  layer2.strip_add(ANIM_STRIP_TYPE_KEYFRAME);

  { /* Test removing a layer that is not owned. */
    Animation other_anim = {};
    Layer &other_layer = *other_anim.layer_add("Another Layer");
    EXPECT_FALSE(anim.layer_remove(other_layer))
        << "Removing a layer not owned by the animation should be gracefully rejected";
    BKE_animation_free_data(&other_anim);
  }

  EXPECT_TRUE(anim.layer_remove(layer1));
  EXPECT_EQ(2, anim.layers().size());

  EXPECT_TRUE(anim.layer_remove(layer2));
  EXPECT_EQ(1, anim.layers().size());

  EXPECT_TRUE(anim.layer_remove(layer0));
  EXPECT_EQ(0, anim.layers().size());
}

TEST_F(AnimationLayersTest, add_strip)
{
  Layer *layer = anim.layer_add("Test Læür");

  Strip *strip = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  ASSERT_EQ(1, layer->strips().size());
  EXPECT_EQ(strip, layer->strip(0));

  constexpr float inf = std::numeric_limits<float>::infinity();
  EXPECT_EQ(-inf, strip->frame_start) << "Expected strip to be infinite.";
  EXPECT_EQ(inf, strip->frame_end) << "Expected strip to be infinite.";
  EXPECT_EQ(0, strip->frame_offset) << "Expected infinite strip to have no offset.";

  Strip *another_strip = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  ASSERT_EQ(2, layer->strips().size());
  EXPECT_EQ(another_strip, layer->strip(1));

  EXPECT_EQ(-inf, another_strip->frame_start) << "Expected strip to be infinite.";
  EXPECT_EQ(inf, another_strip->frame_end) << "Expected strip to be infinite.";
  EXPECT_EQ(0, another_strip->frame_offset) << "Expected infinite strip to have no offset.";

  /* Add some keys to check that also the strip data is freed correctly. */
  const KeyframeSettings settings = get_keyframe_settings(false);
  Output &out = *anim.output_add();
  strip->as<KeyframeStrip>().keyframe_insert(out, "location", 0, {1.0f, 47.0f}, settings);
  another_strip->as<KeyframeStrip>().keyframe_insert(out, "location", 0, {1.0f, 47.0f}, settings);
}

TEST_F(AnimationLayersTest, remove_strip)
{
  Layer &layer = *anim.layer_add("Test Læür");
  Strip &strip0 = *layer.strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  Strip &strip1 = *layer.strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  Strip &strip2 = *layer.strip_add(ANIM_STRIP_TYPE_KEYFRAME);

  /* Add some keys to check that also the strip data is freed correctly. */
  const KeyframeSettings settings = get_keyframe_settings(false);
  Output &out = *anim.output_add();
  strip0.as<KeyframeStrip>().keyframe_insert(out, "location", 0, {1.0f, 47.0f}, settings);
  strip1.as<KeyframeStrip>().keyframe_insert(out, "location", 0, {1.0f, 47.0f}, settings);
  strip2.as<KeyframeStrip>().keyframe_insert(out, "location", 0, {1.0f, 47.0f}, settings);

  EXPECT_TRUE(layer.strip_remove(strip1));
  EXPECT_EQ(2, layer.strips().size());

  EXPECT_TRUE(layer.strip_remove(strip2));
  EXPECT_EQ(1, layer.strips().size());

  EXPECT_TRUE(layer.strip_remove(strip0));
  EXPECT_EQ(0, layer.strips().size());

  { /* Test removing a strip that is not owned. */
    Layer &other_layer = *anim.layer_add("Another Layer");
    Strip &other_strip = *other_layer.strip_add(ANIM_STRIP_TYPE_KEYFRAME);

    EXPECT_FALSE(layer.strip_remove(other_strip))
        << "Removing a strip not owned by the layer should be gracefully rejected";
  }
}

TEST_F(AnimationLayersTest, add_output)
{
  Output *out = anim.output_add();
  EXPECT_EQ(1, anim.last_output_stable_index);
  ASSERT_NE(nullptr, out);
  EXPECT_EQ(1, out->stable_index);

  EXPECT_EQ("", std::string(out->name));
  EXPECT_EQ(0, out->idtype);

  EXPECT_TRUE(out->assign_id(&cube->id));
  EXPECT_EQ("OBKüüübus", std::string(out->name));
  EXPECT_EQ(GS(cube->id.name), out->idtype);
}

TEST_F(AnimationLayersTest, add_output_multiple)
{
  Output *out_cube = anim.output_add();
  Output *out_suzanne = anim.output_add();
  EXPECT_TRUE(out_cube->assign_id(&cube->id));
  EXPECT_TRUE(out_suzanne->assign_id(&suzanne->id));

  EXPECT_EQ(2, anim.last_output_stable_index);
  EXPECT_EQ(1, out_cube->stable_index);
  EXPECT_EQ(2, out_suzanne->stable_index);
}

TEST_F(AnimationLayersTest, anim_assign_id)
{
  Output *out_cube = anim.output_add();
  ASSERT_TRUE(anim.assign_id(out_cube, &cube->id));

  // TODO: expand this test.
}

TEST_F(AnimationLayersTest, find_suitable_output)
{
  /* ===
   * Empty case, no outputs exist yet and the ID doesn't even have an AnimData. */
  EXPECT_EQ(nullptr, anim.find_suitable_output_for(&cube->id));

  /* ===
   * Output exists with the same name & type as the ID, but the ID doesn't have any AnimData yet.
   * These should nevertheless be matched up. */
  Output *out = anim.output_add();
  out->stable_index = 327;
  STRNCPY_UTF8(out->name, "OBKüüübus");
  out->idtype = GS(cube->id.name);
  EXPECT_EQ(out, anim.find_suitable_output_for(&cube->id));

  /* ===
   * Output exists with the same name & type as the ID, and the ID has an AnimData with the same
   * output name, but a different output_stable_index. Since the Animation has not yet been
   * assigned to this ID, the output_stable_index should be ignored, and the output name used for
   * matching. */

  /* Create an output with the stable index that should be ignored.*/
  Output *other_out = anim.output_add();
  other_out->stable_index = 47;

  AnimData *adt = BKE_animdata_ensure_id(&cube->id);
  adt->animation = nullptr;
  /* Configure adt to use the stable index of one output, and the name of the other. */
  adt->output_stable_index = other_out->stable_index;
  STRNCPY_UTF8(adt->output_name, out->name);
  EXPECT_EQ(out, anim.find_suitable_output_for(&cube->id));

  /* ===
   * Same situation as above (AnimData has name of one output, but stable index of another), except
   * that the animation data-block has already been assigned. In this case the stable index should
   * take precedence. */
  adt->animation = &anim;
  id_us_plus(&anim.id);
  EXPECT_EQ(other_out, anim.find_suitable_output_for(&cube->id));

  /* ===
   * An output exists, but doesn't match anything in the anim data of the cube. This should fall
   * back to using the ID name. */
  adt->output_stable_index = 161;
  STRNCPY_UTF8(adt->output_name, "¿¿What's this??");
  EXPECT_EQ(out, anim.find_suitable_output_for(&cube->id));
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
  Output *out = anim.output_add();
  EXPECT_TRUE(out->assign_id(&cube->id));
  Layer *layer = anim.layer_add("Kübus layer");

  Strip *strip = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  KeyframeStrip &key_strip = strip->as<KeyframeStrip>();

  const KeyframeSettings settings = get_keyframe_settings(false);
  FCurve *fcurve_loc_a = key_strip.keyframe_insert(*out, "location", 0, {1.0f, 47.0f}, settings);
  ASSERT_NE(nullptr, fcurve_loc_a)
      << "Expect all the necessary data structures to be created on insertion of a key";

  /* Check the strip was created correctly, with the channels for the output. */
  ASSERT_EQ(1, key_strip.channels_for_output_span().size());
  ChannelsForOutput *chan_for_out = key_strip.channels_for_output_at(0);
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
}

}  // namespace blender::animrig::tests
