/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "ANIM_animation.hh"
#include "ANIM_evaluation.hh"
#include "evaluation_internal.hh"

#include "BKE_animation.h"
#include "BKE_animsys.h"
#include "BKE_idtype.h"

#include "DNA_object_types.h"

#include "RNA_access.hh"
#include "RNA_prototypes.h"

#include "BLI_string_utf8.h"

#include <optional>

#include "testing/testing.h"

namespace blender::animrig::tests {

using namespace blender::animrig::internal;

class AnimationEvaluationTest : public testing::Test {
 protected:
  Animation anim = {};
  Object cube = {};
  Output *out;
  Layer *layer;

 public:
  static void SetUpTestSuite()
  {
    /* To make id_can_have_animdata() and friends work, the `id_types` array needs to be set up. */
    BKE_idtype_init();
  }

  void SetUp() override
  {
    STRNCPY_UTF8(cube.id.name, "OBKüüübus");
    out = anim.output_add();
    out->assign_id(&cube.id);
    layer = anim.layer_add("Kübus layer");
  }

  void TearDown() override
  {
    BKE_animation_free_data(&anim);
  }
};

TEST_F(AnimationEvaluationTest, evaluate_layer__keyframes)
{
  Strip *strip = layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);
  KeyframeStrip &key_strip = strip->as<KeyframeStrip>();

  KeyframeSettings settings = get_keyframe_settings(false);
  settings.interpolation = BEZT_IPO_LIN; /* Makes it easier to predict test values. */

  /* Set some keys. */
  key_strip.keyframe_insert(*out, "location", 0, {1.0f, 47.1f}, settings);
  key_strip.keyframe_insert(*out, "location", 0, {5.0f, 47.5f}, settings);
  key_strip.keyframe_insert(*out, "rotation_euler", 1, {1.0f, 0.0f}, settings);
  key_strip.keyframe_insert(*out, "rotation_euler", 1, {5.0f, 3.14f}, settings);

  /* Set the animated properties to some values. These should not be overwritten
   * by the evaluation itself. */
  cube.loc[0] = 3.0f;
  cube.loc[1] = 2.0f;
  cube.loc[2] = 7.0f;
  cube.rot[0] = 3.0f;
  cube.rot[1] = 2.0f;
  cube.rot[2] = 7.0f;

  /* Evaluate. */
  PointerRNA animated_id_ptr = RNA_pointer_create(&cube.id, &RNA_Object, &cube.id);
  AnimationEvalContext anim_eval_context = {};
  anim_eval_context.eval_time = 3.0f;

  std::optional<EvaluationResult> result = evaluate_layer(
      &animated_id_ptr, *layer, out->stable_index, anim_eval_context);

  /* Check the result. */
  ASSERT_TRUE(result.has_value());
  AnimatedProperty *loc0_result = result->lookup_ptr(PropIdentifier("location", 0));
  ASSERT_NE(nullptr, loc0_result) << "location[0] should have been animated";
  EXPECT_EQ(47.3f, loc0_result->value);

  EXPECT_EQ(3.0f, cube.loc[0]) << "Evaluation should not modify the animated ID";
  EXPECT_EQ(2.0f, cube.loc[1]) << "Evaluation should not modify the animated ID";
  EXPECT_EQ(7.0f, cube.loc[2]) << "Evaluation should not modify the animated ID";
  EXPECT_EQ(3.0f, cube.rot[0]) << "Evaluation should not modify the animated ID";
  EXPECT_EQ(2.0f, cube.rot[1]) << "Evaluation should not modify the animated ID";
  EXPECT_EQ(7.0f, cube.rot[2]) << "Evaluation should not modify the animated ID";
}

}  // namespace blender::animrig::tests
