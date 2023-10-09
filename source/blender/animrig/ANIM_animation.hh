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

class Layer;
class Strip;
class Output;

class Animation : public ::Animation {
 public:
  Animation() = default;
  Animation(const Animation &other) = default;
  ~Animation() = default;

  /* Animation Layers access. */
  blender::Span<const Layer *> layers() const;
  blender::MutableSpan<Layer *> layers();
  const Layer *layer(int64_t index) const;
  Layer *layer(int64_t index);

  Layer *layer_add(const char *name);

  /* Animation Output access. */
  blender::Span<const Output *> outputs() const;
  blender::MutableSpan<Output *> outputs();
  const Output *output(int64_t index) const;
  Output *output(int64_t index);

  Output *output_add(ID *animated_id);
};
static_assert(sizeof(Animation) == sizeof(::Animation));

class Layer : public ::AnimationLayer {
 public:
  Layer() = default;
  Layer(const Layer &other) = default;
  ~Layer() = default;

  /* Strip access. */
  blender::Span<const Strip *> strips() const;
  blender::MutableSpan<Strip *> strips();
  const Strip *strip(int64_t index) const;
  Strip *strip(int64_t index);

  Strip *strip_add(eAnimationStrip_type strip_type);
};

class Output : public ::AnimationOutput {
 public:
  Output() = default;
  Output(const Output &other) = default;
  ~Output() = default;
};

class Strip : public ::AnimationStrip {
 public:
  Strip() = default;
  Strip(const Strip &other) = default;
  ~Strip() = default;

  // TODO: add? Maybe?
  // template<typename T> bool is() const;
  template<typename T> T &as();
};

class KeyframeStrip : public ::KeyframeAnimationStrip {
 public:
  KeyframeStrip() = default;
  KeyframeStrip(const KeyframeStrip &other) = default;
  ~KeyframeStrip() = default;

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

template<> KeyframeStrip &Strip::as<KeyframeStrip>();

FCurve *keyframe_insert(Strip *strip,
                        const AnimationOutput *out,
                        const char *rna_path,
                        int array_index,
                        float value,
                        float time,
                        eBezTriple_KeyframeType keytype);

}  // namespace blender::animrig

/* Wrap functions for the DNA structs. */

inline blender::animrig::Animation &Animation::wrap()
{
  return *reinterpret_cast<blender::animrig::Animation *>(this);
}
inline const blender::animrig::Animation &Animation::wrap() const
{
  return *reinterpret_cast<const blender::animrig::Animation *>(this);
}

inline blender::animrig::Layer &AnimationLayer::wrap()
{
  return *reinterpret_cast<blender::animrig::Layer *>(this);
}
inline const blender::animrig::Layer &AnimationLayer::wrap() const
{
  return *reinterpret_cast<const blender::animrig::Layer *>(this);
}

inline blender::animrig::Output &AnimationOutput::wrap()
{
  return *reinterpret_cast<blender::animrig::Output *>(this);
}
inline const blender::animrig::Output &AnimationOutput::wrap() const
{
  return *reinterpret_cast<const blender::animrig::Output *>(this);
}

inline blender::animrig::Strip &AnimationStrip::wrap()
{
  return *reinterpret_cast<blender::animrig::Strip *>(this);
}
inline const blender::animrig::Strip &AnimationStrip::wrap() const
{
  return *reinterpret_cast<const blender::animrig::Strip *>(this);
}

inline blender::animrig::KeyframeStrip &KeyframeAnimationStrip::wrap()
{
  return *reinterpret_cast<blender::animrig::KeyframeStrip *>(this);
}
inline const blender::animrig::KeyframeStrip &KeyframeAnimationStrip::wrap() const
{
  return *reinterpret_cast<const blender::animrig::KeyframeStrip *>(this);
}
