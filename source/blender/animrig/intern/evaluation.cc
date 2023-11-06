/* SPDX-FileCopyrightText: 2023 Blender Developers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "ANIM_evaluation.hh"

#include "RNA_access.hh"

#include "BKE_animsys.h"
#include "BKE_fcurve.h"

#include "BLI_map.hh"

#include <optional>

namespace blender::animrig {

namespace {

class PropIdentifier {
 public:
  std::string rna_path;
  int array_index;

  PropIdentifier() = default;
  ~PropIdentifier() = default;

  /* TODO: should we use the PointerRNA or PropertyRNA type here instead? */
  PropIdentifier(const StringRefNull rna_path, const int array_index)
      : rna_path(rna_path), array_index(array_index)
  {
  }

  bool operator==(const PropIdentifier &other) const
  {
    return rna_path == other.rna_path && array_index == other.array_index;
  }
  bool operator!=(const PropIdentifier &other) const
  {
    return !(*this == other);
  }

  uint64_t hash() const
  {
    return get_default_hash_2(rna_path, array_index);
  }
};

class AnimatedProperty {
 public:
  float value;
  PathResolvedRNA prop_rna;

  AnimatedProperty(const float value, const PathResolvedRNA &prop_rna)
      : value(value), prop_rna(prop_rna)
  {
  }
  ~AnimatedProperty() = default;
};

/* Evaluated FCurves for some animation output.
 * Mapping from property identifier to its float value.
 *
 * Can be fed to the evaluation of the next layer, mixed with another strip, or
 * used to modify actual RNA properties.
 *
 * TODO: see if this is efficient, and contains enough info, for mixing. For now
 * this just captures the FCurve evaluation result, but doesn't have any info
 * about how to do the mixing (LERP, quaternion SLERP, etc.).
 */
class EvaluationResult {
 public:
  EvaluationResult() = default;
  EvaluationResult(const EvaluationResult &other) = default;
  ~EvaluationResult() = default;

 protected:
  using EvaluationMap = Map<PropIdentifier, AnimatedProperty>;
  EvaluationMap result_;

 public:
  void store(const StringRefNull rna_path,
             const int array_index,
             const float value,
             const PathResolvedRNA &prop_rna)
  {
    PropIdentifier key(rna_path, array_index);
    AnimatedProperty anim_prop(value, prop_rna);
    result_.add(key, anim_prop);
  }

  AnimatedProperty value(const StringRefNull rna_path, const int array_index) const
  {
    PropIdentifier key(rna_path, array_index);
    return result_.lookup(key);
  }

  const AnimatedProperty *lookup_ptr(const PropIdentifier &key) const
  {
    return result_.lookup_ptr(key);
  }
  AnimatedProperty *lookup_ptr(const PropIdentifier &key)
  {
    return result_.lookup_ptr(key);
  }

  EvaluationMap::ItemIterator items() const
  {
    return result_.items();
  }
};
}  // namespace

/**
 * Evaluate the animation data on the given layer, for the given output. This
 * just returns the evaluation result, without taking any other layers,
 * blending, influence, etc. into account. */
std::optional<EvaluationResult> evaluate_layer(PointerRNA *animated_id_ptr,
                                               Layer &layer,
                                               output_index_t output_index,
                                               const AnimationEvalContext &anim_eval_context);

/**
 * Blend the 'current layer' with the 'last evaluation result', returning the
 * blended result.
 */
EvaluationResult blend_layer_results(const EvaluationResult &last_result,
                                     const EvaluationResult &current_result,
                                     const Layer &current_layer);

/**
 * Apply the result of the animation evaluation to the given data-block.
 *
 * \param flush_to_original when true, look up the original data-block (assuming the given one is
 * an evaluated copy) and update that too.
 */
void apply_evaluation_result(const EvaluationResult &evaluation_result,
                             PointerRNA *animated_id_ptr,
                             bool flush_to_original);

/**
 * Top level animation evaluation function.
 *
 * Animate the given ID, using the animation data-block and the given output.
 *
 * \param flush_to_original when true, look up the original data-block (assuming the given one is
 * an evaluated copy) and update that too.
 */
void evaluate_animation(PointerRNA *animated_id_ptr,
                        Animation &animation,
                        const output_index_t output_index,
                        const AnimationEvalContext &anim_eval_context,
                        const bool flush_to_original)
{
  std::optional<EvaluationResult> last_result;

  /* Evaluate each layer in order. */
  for (Layer *layer : animation.layers()) {
    if (layer->influence <= 0.0f) {
      /* Don't bother evaluating layers without influence. */
      continue;
    }

    auto layer_result = evaluate_layer(animated_id_ptr, *layer, output_index, anim_eval_context);
    if (!layer_result) {
      continue;
    }

    if (!last_result) {
      /* Simple case: no results so far, so just use this layer as-is. There is
       * nothing to blend/combine with, so ignore the influence and combination
       * options. */
      last_result = layer_result;
      continue;
    }

    /* Complex case: blend this layer's result into the previous layer's result. */
    last_result = blend_layer_results(*last_result, *layer_result, *layer);
  }

  if (!last_result) {
    return;
  }

  apply_evaluation_result(*last_result, animated_id_ptr, flush_to_original);
}

/* Copy of the same-named function in anim_sys.cc, with the check on action groups removed. */
static bool is_fcurve_evaluatable(const FCurve *fcu)
{
  if (fcu->flag & (FCURVE_MUTED | FCURVE_DISABLED)) {
    return false;
  }
  if (BKE_fcurve_is_empty(fcu)) {
    return false;
  }
  return true;
}

/* Copy of the same-named function in anim_sys.cc, but with the special handling for NLA strips
 * removed. */
static void animsys_construct_orig_pointer_rna(const PointerRNA *ptr, PointerRNA *ptr_orig)
{
  *ptr_orig = *ptr;
  /* Original note from anim_sys.cc:
   * -----------
   * NOTE: nlastrip_evaluate_controls() creates PointerRNA with ID of nullptr. Technically, this is
   * not a valid pointer, but there are exceptions in various places of this file which handles
   * such pointers.
   * We do special trickery here as well, to quickly go from evaluated to original NlaStrip.
   * -----------
   * And this is all not ported to the new layered animation system. */
  BLI_assert_msg(ptr->owner_id, "NLA support was not ported to the layered animation system");
  ptr_orig->owner_id = ptr_orig->owner_id->orig_id;
  ptr_orig->data = ptr_orig->owner_id;
}

/* Copy of the same-named function in anim_sys.cc. */
static void animsys_write_orig_anim_rna(PointerRNA *ptr,
                                        const char *rna_path,
                                        const int array_index,
                                        const float value)
{
  PointerRNA ptr_orig;
  animsys_construct_orig_pointer_rna(ptr, &ptr_orig);

  PathResolvedRNA orig_anim_rna;
  /* TODO(sergey): Should be possible to cache resolved path in dependency graph somehow. */
  if (BKE_animsys_rna_path_resolve(&ptr_orig, rna_path, array_index, &orig_anim_rna)) {
    BKE_animsys_write_to_rna_path(&orig_anim_rna, value);
  }
}

/* Returns whether the strip was evaluated. */
static std::optional<EvaluationResult> evaluate_keyframe_strip(
    PointerRNA *animated_id_ptr,
    KeyframeStrip &key_strip,
    const output_index_t output_index,
    const AnimationEvalContext &offset_eval_context)
{
  ChannelsForOutput *chans_for_out = key_strip.chans_for_out(output_index);
  if (!chans_for_out) {
    return {};
  }

  EvaluationResult evaluation_result;
  for (FCurve *fcu : chans_for_out->fcurves()) {
    /* Blatant copy of animsys_evaluate_fcurves(). */

    if (!is_fcurve_evaluatable(fcu)) {
      continue;
    }

    PathResolvedRNA anim_rna;
    if (!BKE_animsys_rna_path_resolve(animated_id_ptr, fcu->rna_path, fcu->array_index, &anim_rna))
    {
      continue;
    }

    const float curval = calculate_fcurve(&anim_rna, fcu, &offset_eval_context);
    evaluation_result.store(fcu->rna_path, fcu->array_index, curval, anim_rna);
  }

  return evaluation_result;
}

void apply_evaluation_result(const EvaluationResult &evaluation_result,
                             PointerRNA *animated_id_ptr,
                             const bool flush_to_original)
{
  for (auto channel_result : evaluation_result.items()) {
    const PropIdentifier &prop_ident = channel_result.key;
    const AnimatedProperty &anim_prop = channel_result.value;
    const float animated_value = anim_prop.value;
    PathResolvedRNA anim_rna = anim_prop.prop_rna;

    BKE_animsys_write_to_rna_path(&anim_rna, animated_value);

    if (flush_to_original) {
      animsys_write_orig_anim_rna(
          animated_id_ptr, prop_ident.rna_path.c_str(), prop_ident.array_index, animated_value);
    }
  }
}

/* Returns whether the strip was evaluated. */
static std::optional<EvaluationResult> evaluate_strip(
    PointerRNA *animated_id_ptr,
    Strip &strip,
    const output_index_t output_index,
    const AnimationEvalContext &anim_eval_context)
{
  AnimationEvalContext offset_eval_context = anim_eval_context;
  /* Positive offset means the entire strip is pushed "to the right", so
   * evaluation needs to happen further "to the left". */
  offset_eval_context.eval_time -= strip.frame_offset;

  switch (strip.type) {
    case ANIM_STRIP_TYPE_KEYFRAME: {
      KeyframeStrip &key_strip = strip.as<KeyframeStrip>();
      return evaluate_keyframe_strip(
          animated_id_ptr, key_strip, output_index, offset_eval_context);
    }
  }

  return {};
}

std::optional<EvaluationResult> evaluate_layer(PointerRNA *animated_id_ptr,
                                               Layer &layer,
                                               const output_index_t output_index,
                                               const AnimationEvalContext &anim_eval_context)
{

  for (Strip *strip : layer.strips()) {
    if (!strip->contains_frame(anim_eval_context.eval_time)) {
      continue;
    }

    const auto strip_result = evaluate_strip(
        animated_id_ptr, *strip, output_index, anim_eval_context);
    if (!strip_result) {
      continue;
    }

    /* TODO: evaluate overlapping strips indepently, and mix the results. For
     * now, just limit to the first available strip on this layer. */
    return strip_result;
  }

  return {};
}

static float lerp(const float t, const float a, const float b)
{
  return (a + t * (b - a));
}

EvaluationResult blend_layer_results(const EvaluationResult &last_result,
                                     const EvaluationResult &current_result,
                                     const Layer &current_layer)
{
  /* TODO?: store the layer results sequentially, so that we can step through
   * them in parallel, instead of iterating over one and doing map lookups on
   * the other. */

  /* TODO?: make `last_result` non-const, as it's likely faster to update that,
   * instead of copying everything and updating the copy. */

  EvaluationResult blend = last_result;

  for (auto channel_result : current_result.items()) {
    const PropIdentifier &prop_ident = channel_result.key;
    AnimatedProperty *last_prop = blend.lookup_ptr(prop_ident);
    const AnimatedProperty &anim_prop = channel_result.value;

    if (!last_prop) {
      /* Nothing to blend with, so just take (influence * value). */
      blend.store(prop_ident.rna_path,
                  prop_ident.array_index,
                  anim_prop.value * current_layer.influence,
                  anim_prop.prop_rna);
      continue;
    }

    /* TODO: move this to a separate function. And write more smartness for rotations. */
    const eAnimationLayer_MixMode mix_mode = eAnimationLayer_MixMode(current_layer.mix_mode);
    switch (mix_mode) {
      case OVERRIDE:
        last_prop->value = anim_prop.value * current_layer.influence;
        break;
      case COMBINE:
        last_prop->value = lerp(current_layer.influence, last_prop->value, anim_prop.value);
        break;
      case ADD:
        last_prop->value += anim_prop.value * current_layer.influence;
        break;
      case SUBTRACT:
        last_prop->value -= anim_prop.value * current_layer.influence;
        break;
      case MULTIPLY:
        last_prop->value *= anim_prop.value * current_layer.influence;
        break;
    };
  }

  return blend;
}

}  // namespace blender::animrig
