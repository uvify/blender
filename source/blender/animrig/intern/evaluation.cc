/* SPDX-FileCopyrightText: 2023 Blender Developers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "ANIM_evaluation.hh"

#include "RNA_access.hh"

#include "BKE_animsys.h"
#include "BKE_fcurve.h"

namespace blender::animrig {

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
                                        int array_index,
                                        float value)
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
static bool evaluate_keyframe_strip(PointerRNA *animated_id_ptr,
                                    KeyframeStrip &key_strip,
                                    const output_index_t output_index,
                                    const AnimationEvalContext *offset_eval_context,
                                    const bool flush_to_original)
{
  ChannelsForOutput *chans_for_out = key_strip.chans_for_out(output_index);
  if (!chans_for_out) {
    return false;
  }

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

    const float curval = calculate_fcurve(&anim_rna, fcu, offset_eval_context);
    BKE_animsys_write_to_rna_path(&anim_rna, curval);
    if (flush_to_original) {
      animsys_write_orig_anim_rna(animated_id_ptr, fcu->rna_path, fcu->array_index, curval);
    }
  }

  return true;
}

/* Returns whether the strip was evaluated. */
static bool evaluate_strip(PointerRNA *animated_id_ptr,
                           Strip &strip,
                           const output_index_t output_index,
                           const AnimationEvalContext *anim_eval_context,
                           const bool flush_to_original)
{
  AnimationEvalContext offset_eval_context = *anim_eval_context;
  /* Positive offset means the entire strip is pushed "to the right", so
   * evaluation needs to happen further "to the left". */
  offset_eval_context.eval_time -= strip.frame_offset;

  switch (strip.type) {
    case ANIM_STRIP_TYPE_KEYFRAME: {
      KeyframeStrip &key_strip = strip.as<KeyframeStrip>();
      return evaluate_keyframe_strip(
          animated_id_ptr, key_strip, output_index, &offset_eval_context, flush_to_original);
    }
  }

  return false;
}

void evaluate_animation(PointerRNA *animated_id_ptr,
                        Animation &animation,
                        const output_index_t output_index,
                        const AnimationEvalContext *anim_eval_context,
                        const bool flush_to_original)
{
  const float eval_time = anim_eval_context->eval_time;

  /* Evaluate each layer in order. */
  for (Layer *layer : animation.layers()) {
    for (Strip *strip : layer->strips()) {
      if (!strip->contains_frame(eval_time)) {
        continue;
      }

      const bool strip_was_evaluated = evaluate_strip(
          animated_id_ptr, *strip, output_index, anim_eval_context, flush_to_original);

      /* TODO: merge overlapping strips indepently, and mix the results. For now, just limit to the
       * first available strip. */
      if (strip_was_evaluated) {
        break;
      }
    }
  }
}

}  // namespace blender::animrig
