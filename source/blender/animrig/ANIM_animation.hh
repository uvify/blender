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

#include "ANIM_fcurve.hh"

#include "DNA_anim_types.h"

#include "BLI_math_vector.hh"
#include "BLI_set.hh"
#include "BLI_string_ref.hh"

struct AnimationEvalContext;
struct FCurve;
struct ID;
struct PointerRNA;

namespace blender::animrig {

/* Forward declarations for the types defined later in this file. */
class Layer;
class Strip;
class Output;

/* Use an alias for the stable index type. */
using output_index_t = decltype(::AnimationOutput::stable_index);

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

  Layer *layer_add(StringRefNull name);
  /**
   * Remove the layer from this animation.
   *
   * After this call, the passed reference is no longer valid, as the memory
   * will have been freed. Any strips on the layer will be freed too.
   *
   * \return true when the layer was found & removed, false if it wasn't found. */
  bool layer_remove(Layer &layer_to_remove);

  /* Animation Output access. */
  blender::Span<const Output *> outputs() const;
  blender::MutableSpan<Output *> outputs();
  const Output *output(int64_t index) const;
  Output *output(int64_t index);

  Output *output_for_stable_index(output_index_t stable_index);
  const Output *output_for_stable_index(output_index_t stable_index) const;

  /**
   * Set the output name.
   *
   * This has to be done on the Animation level to ensure each output has a
   * unique name within the Animation.
   */
  void output_name_set(Output &out, StringRefNull new_name);
  Output *output_find_by_name(StringRefNull output_name);

  Output *output_for_id(const ID *animated_id);
  const Output *output_for_id(const ID *animated_id) const;

  Output *output_add();

  /** Assign this animation to the ID.
   *
   * \param output The output this ID should be animated by, may be nullptr if it is to be assigned
   * later. In that case, the ID will not actually receive any animation.
   * \param animated_id The ID that should be animated by this Animation data-block.
   */
  bool assign_id(Output *output, ID *animated_id);
  void unassign_id(ID *animated_id);

  /**
   * Find the output that best matches the animated ID.
   *
   * If the ID is already animated by this Animation, by matching this
   * Animation's outputs with (in order):
   *
   * - `animated_id->adt->output_stable_index`,
   * - `animated_id->adt->output_name`,
   * - `animated_id->name`.
   *
   * Note that this different from #output_for_id, which does not use the
   * output name, and only works when this Animation is already assigned. */
  Output *find_suitable_output_for(const ID *animated_id);

  /**
   * Free all data in the `Animation`.
   *
   * The `Animation` will effectively be like a freshly-created, empty `Animation`
   * after this is called.
   */
  void free_data();

 protected:
  /** Return the layer's index, or -1 if not found in this animation. */
  int64_t find_layer_index(const Layer &layer) const;

 private:
  Output &output_allocate_();
};
static_assert(sizeof(Animation) == sizeof(::Animation),
              "DNA struct and its C++ wrapper must have the same size");

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

  /**
   * Remove the strip from this layer.
   *
   * After this call, the passed reference is no longer valid, as the memory
   * will have been freed.
   *
   * \return true when the strip was found & removed, false if it wasn't found. */
  bool strip_remove(Strip &strip);

  /**
   * Free all data in the `Layer`.
   *
   * The `Layer` will effectively be like a freshly-created, empty `Layer`
   * after this is called.
   */
  void free_data();

 protected:
  /** Return the strip's index, or -1 if not found in this layer. */
  int64_t find_strip_index(const Strip &strip) const;
};
static_assert(sizeof(Layer) == sizeof(::AnimationLayer),
              "DNA struct and its C++ wrapper must have the same size");

class Output : public ::AnimationOutput {
 public:
  Output() = default;
  Output(const Output &other) = default;
  ~Output() = default;

  /**
   * Let the given ID receive animation from this output.
   *
   * This is a low-level function; for most purposes you want
   * #Animation::assign_id instead.
   *
   * \note This does _not_ set animated_id->adt->animation to the owner of this
   * Output. It's the caller's responsibility to do that.
   *
   * \return Whether this was possible. If the Output was already bound to a
   * specific ID type, and `animated_id` is of a different type, it will be
   * refused. If the ID type cannot be animated at all, false is also returned.
   *
   * \see assign_animation
   * \see Animation::assign_id
   */
  bool assign_id(ID *animated_id);

  bool is_suitable_for(const ID *animated_id) const;
};
static_assert(sizeof(Output) == sizeof(::AnimationOutput),
              "DNA struct and its C++ wrapper must have the same size");

class Strip : public ::AnimationStrip {
 public:
  Strip() = default;
  Strip(const Strip &other) = default;
  ~Strip() = default;

  // TODO: add? Maybe?
  // template<typename T> bool is() const;
  template<typename T> T &as();
  template<typename T> const T &as() const;

  bool contains_frame(float frame_time) const;
  bool is_last_frame(float frame_time) const;

  /**
   * Set the start and end frame.
   *
   * Note that this does not do anything else. There is no check whether the
   * frame numbers are valid (i.e. frame_start <= frame_end). Infinite values
   * (negative for frame_start, positive for frame_end) are supported.
   */
  void resize(float frame_start, float frame_end);
};
static_assert(sizeof(Strip) == sizeof(::AnimationStrip),
              "DNA struct and its C++ wrapper must have the same size");

class KeyframeStrip : public ::KeyframeAnimationStrip {
 public:
  KeyframeStrip() = default;
  KeyframeStrip(const KeyframeStrip &other) = default;
  ~KeyframeStrip() = default;

  /* ChannelsForOutput array access. Note that the 'array_index' is the index
   * into channels_for_output_array in the DNA base struct. */
  blender::Span<const ChannelsForOutput *> channels_for_output_span() const;
  blender::MutableSpan<ChannelsForOutput *> channels_for_output_span();
  const ChannelsForOutput *channels_for_output_at(int64_t array_index) const;
  ChannelsForOutput *channels_for_output_at(int64_t array_index);

  /**
   * Find the animation channels for this output.
   *
   * \return nullptr if there is none yet for this output.
   */
  const ChannelsForOutput *chans_for_out(const Output &out) const;
  ChannelsForOutput *chans_for_out(const Output &out);
  const ChannelsForOutput *chans_for_out(output_index_t output_stable_index) const;
  ChannelsForOutput *chans_for_out(output_index_t output_stable_index);

  /**
   * Add the animation channels for this output.
   *
   * Should only be called when there is no `ChannelsForOutput` for this output yet.
   */
  ChannelsForOutput *chans_for_out_add(const Output &out);

  /**
   * Find an FCurve for this output + RNA path + array index combination.
   *
   * If it cannot be found, `nullptr` is returned.
   */
  FCurve *fcurve_find(const Output &out, StringRefNull rna_path, int array_index);

  /**
   * Find an FCurve for this output + RNA path + array index combination.
   *
   * If it cannot be found, a new one is created.
   */
  FCurve *fcurve_find_or_create(const Output &out, StringRefNull rna_path, int array_index);

  FCurve *keyframe_insert(const Output &out,
                          StringRefNull rna_path,
                          int array_index,
                          float2 time_value,
                          const KeyframeSettings &settings);
};
static_assert(sizeof(KeyframeStrip) == sizeof(::KeyframeAnimationStrip),
              "DNA struct and its C++ wrapper must have the same size");

template<> KeyframeStrip &Strip::as<KeyframeStrip>();
template<> const KeyframeStrip &Strip::as<KeyframeStrip>() const;

class ChannelsForOutput : public ::AnimationChannelsForOutput {
 public:
  ChannelsForOutput() = default;
  ChannelsForOutput(const ChannelsForOutput &other) = default;
  ~ChannelsForOutput() = default;

  /* FCurves access. */
  blender::Span<const FCurve *> fcurves() const;
  blender::MutableSpan<FCurve *> fcurves();
  const FCurve *fcurve(int64_t index) const;
  FCurve *fcurve(int64_t index);

  const FCurve *fcurve_find(StringRefNull rna_path, int array_index) const;
};
static_assert(sizeof(ChannelsForOutput) == sizeof(::AnimationChannelsForOutput),
              "DNA struct and its C++ wrapper must have the same size");

/**
 * Assign the animation to the ID.
 *
 * This will will make a best-effort guess as to which output to use, in this
 * order;
 *
 * - By stable index.
 * - By fallback string.
 * - By the ID's name (matching agains the output name).
 * - If the above do not find a suitable output, the animated ID will not
 *   receive any animation and the calller is responsible for creating an output
 *   and assigning it.
 *
 * \return `false` if the assignment was not possible (for example the ID is of a type that cannot
 * be animated). If the above fall-through case of "no output found" is reached, this function will
 * still return `true` as the Animation was succesfully assigned.
 */
bool assign_animation(Animation &anim, ID *animated_id);

/**
 * Ensure that this ID is no longer animated.
 */
void unassign_animation(ID *animated_id);

/**
 * Return the Animation of this ID, or nullptr if it has none.
 */
Animation *get_animation(ID *animated_id);

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

inline blender::animrig::ChannelsForOutput &AnimationChannelsForOutput::wrap()
{
  return *reinterpret_cast<blender::animrig::ChannelsForOutput *>(this);
}
inline const blender::animrig::ChannelsForOutput &AnimationChannelsForOutput::wrap() const
{
  return *reinterpret_cast<const blender::animrig::ChannelsForOutput *>(this);
}
