/* SPDX-FileCopyrightText: 2023 Blender Developers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup animrig
 *
 * \brief Animation data-block functionality.
 */
#pragma once

#ifndef __cplusplus
#  error This is a C++ header.
#endif

#include "DNA_anim_types.h"

struct FCurve;
struct ID;

namespace blender::animrig {

class AnimationStrip : public ::AnimationStrip {
 public:
  AnimationStrip() = default;
  AnimationStrip(const AnimationStrip &other) = default;
  ~AnimationStrip() = default;

  // TODO: add? Maybe?
  // template<typename T> bool is() const;
  template<typename T> T *as();
};

class KeyframeAnimationStrip : public ::KeyframeAnimationStrip {
 public:
  KeyframeAnimationStrip() = default;
  KeyframeAnimationStrip(const KeyframeAnimationStrip &other) = default;
  ~KeyframeAnimationStrip() = default;

  /**
   * Find the animation channels for this output.
   *
   * Create an empty AnimationChannelsForOutput if there is none yet.
   */
  AnimationChannelsForOutput *chans_for_out(const AnimationOutput *out);

  /**
   * Find an FCurve for this output + RNA path + array index combination.
   *
   * If it cannot be found, a new one is created.
   */
  FCurve *fcurve_find_or_create(const AnimationOutput *out, const char *rna_path, int array_index);
};

template<> KeyframeAnimationStrip *AnimationStrip::as<KeyframeAnimationStrip>();

AnimationOutput *animation_add_output(Animation *anim, ID *animated_id);

FCurve *keyframe_insert(AnimationStrip *strip,
                        const AnimationOutput *out,
                        const char *rna_path,
                        int array_index,
                        float value,
                        float time,
                        eBezTriple_KeyframeType keytype);

}  // namespace blender::animrig
