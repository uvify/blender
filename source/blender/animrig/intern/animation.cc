/* SPDX-FileCopyrightText: 2023 Blender Developers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_anim_defaults.h"
#include "DNA_anim_types.h"

#include "BLI_listbase.h"
#include "BLI_listbase_wrapper.hh"
#include "BLI_string.h"
#include "BLI_string_utf8.h"

#include "BKE_fcurve.h"

#include "ED_keyframing.hh"

#include "MEM_guardedalloc.h"

#include "atomic_ops.h"

#include "ANIM_animation.hh"

#include <cstdio>
#include <cstring>

namespace blender::animrig {

AnimationOutput *animation_add_output(Animation *anim, ID *animated_id)
{
  AnimationOutput *output = MEM_new<AnimationOutput>(__func__);

  output->idtype = GS(animated_id->name);
  output->stable_index = atomic_add_and_fetch_int32(&anim->last_output_stable_index, 1);

  /* The ID type bytes can be stripped from the name, as that information is
   * already stored in output->idtype. This also makes it easier to combine
   * names when multiple IDs share the same output. */
  STRNCPY_UTF8(output->fallback, animated_id->name + 2);

  // TODO: turn this into an actually nice function.
  output->runtime.id = MEM_new<ID *>(__func__);
  output->runtime.num_ids = 1;
  *(output->runtime.id) = animated_id;

  BLI_addtail(&anim->outputs, output);

  return output;
}

template<> KeyframeAnimationStrip *AnimationStrip::as<KeyframeAnimationStrip>()
{
  BLI_assert_msg(type == ANIM_STRIP_TYPE_KEYFRAME,
                 "Strip is not of type ANIM_STRIP_TYPE_KEYFRAME");
  return reinterpret_cast<KeyframeAnimationStrip *>(this);
}

AnimationChannelsForOutput *KeyframeAnimationStrip::chans_for_out(const AnimationOutput *out)
{
  /* FIXME: use a hash map lookup for this. */
  for (AnimationChannelsForOutput *channels :
       ListBaseWrapper<AnimationChannelsForOutput>(&this->channels_for_output))
  {
    if (channels->output_stable_index == out->stable_index) {
      return channels;
    }
  }

  AnimationChannelsForOutput *channels = MEM_new<AnimationChannelsForOutput>(__func__);
  channels->output_stable_index = out->stable_index;
  BLI_addtail(&this->channels_for_output, channels);

  return channels;
}

FCurve *KeyframeAnimationStrip::fcurve_find_or_create(const AnimationOutput *out,
                                                      const char *rna_path,
                                                      const int array_index)
{
  AnimationChannelsForOutput *channels = this->chans_for_out(out);

  FCurve *fcurve = BKE_fcurve_find(&channels->fcurves, rna_path, array_index);
  if (fcurve) {
    return fcurve;
  }

  /* Copied from ED_action_fcurve_ensure(). */
  /* TODO: move to separate function, call that from both places. */
  fcurve = BKE_fcurve_create();
  fcurve->rna_path = BLI_strdup(rna_path);
  fcurve->array_index = array_index;

  fcurve->flag = (FCURVE_VISIBLE | FCURVE_SELECTED);
  fcurve->auto_smoothing = U.auto_smoothing_new;

  if (BLI_listbase_is_empty(&channels->fcurves)) {
    fcurve->flag |= FCURVE_ACTIVE; /* First curve is added active. */
  }
  BLI_addhead(&channels->fcurves, fcurve);
  return fcurve;
}

FCurve *keyframe_insert(AnimationStrip *strip,
                        const AnimationOutput *out,
                        const char *rna_path,
                        int array_index,
                        float value,
                        float time,
                        eBezTriple_KeyframeType keytype)
{
  if (strip->type != ANIM_STRIP_TYPE_KEYFRAME) {
    /* TODO: handle this properly, in a way that can be communicated to the user. */
    std::fprintf(stderr,
                 "Strip is not of type ANIM_STRIP_TYPE_KEYFRAME, unable to insert keys here\n");
    return nullptr;
  }

  KeyframeAnimationStrip *key_strip = strip->wrap().as<KeyframeAnimationStrip>();
  FCurve *fcurve = key_strip->fcurve_find_or_create(out, rna_path, array_index);

  if (!BKE_fcurve_is_keyframable(fcurve)) {
    /* TODO: handle this properly, in a way that can be communicated to the user. */
    std::fprintf(stderr,
                 "FCurve %s[%d] for output %s doesn't allow inserting keys.\n",
                 rna_path,
                 array_index,
                 out->fallback);
    return nullptr;
  }

  /* TODO: Move this function from the editors module to the animrig module. */
  /* TODO: Handle the eInsertKeyFlags. */
  const int index = insert_vert_fcurve(fcurve, time, value, keytype, eInsertKeyFlags(0));
  if (index < 0) {
    std::fprintf(stderr,
                 "Could not insert key into FCurve %s[%d] for output %s.\n",
                 rna_path,
                 array_index,
                 out->fallback);
    return nullptr;
  }

  return fcurve;
}

}  // namespace blender::animrig
