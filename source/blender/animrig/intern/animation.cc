/* SPDX-FileCopyrightText: 2023 Blender Developers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_anim_defaults.h"
#include "DNA_anim_types.h"
#include "DNA_defaults.h"

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

static animrig::Layer *animationlayer_alloc()
{
  AnimationLayer *layer = DNA_struct_default_alloc(AnimationLayer);
  return &layer->wrap();
}
static animrig::Strip *animationstrip_alloc_infinite(const eAnimationStrip_type type)
{
  AnimationStrip *strip;
  switch (type) {
    case ANIM_STRIP_TYPE_KEYFRAME: {
      KeyframeAnimationStrip *key_strip = MEM_new<KeyframeAnimationStrip>(__func__);
      strip = &key_strip->strip;
      break;
    }
  }

  BLI_assert_msg(strip, "unsupported strip type");

  /* Copy the default AnimationStrip fields into the allocated data-block. */
  memcpy(strip, DNA_struct_default_get(AnimationStrip), sizeof(*strip));
  return &strip->wrap();
}

/* Copied from source/blender/blenkernel/intern/grease_pencil.cc. It also has a shrink_array()
 * function, if we ever need one (we will). */
template<typename T> static void grow_array(T **array, int *num, const int add_num)
{
  BLI_assert(add_num > 0);
  const int new_array_num = *num + add_num;
  T *new_array = reinterpret_cast<T *>(MEM_cnew_array<T *>(new_array_num, __func__));

  blender::uninitialized_relocate_n(*array, *num, new_array);
  if (*array != nullptr) {
    MEM_freeN(*array);
  }

  *array = new_array;
  *num = new_array_num;
}

template<typename T> static void grow_array_and_append(T **array, int *num, T item)
{
  grow_array(array, num, 1);
  (*array)[*num - 1] = item;
}

/* ----- Animation C++ implementation ----------- */

blender::Span<const Layer *> Animation::layers() const
{
  return blender::Span<Layer *>{reinterpret_cast<Layer **>(this->layer_array),
                                this->layer_array_num};
}
blender::MutableSpan<Layer *> Animation::layers()
{
  return blender::MutableSpan<Layer *>{reinterpret_cast<Layer **>(this->layer_array),
                                       this->layer_array_num};
}
const Layer *Animation::layer(const int64_t index) const
{
  return &this->layer_array[index]->wrap();
}
Layer *Animation::layer(const int64_t index)
{
  return &this->layer_array[index]->wrap();
}

Layer *Animation::layer_add(const char *name)
{
  using namespace blender::animrig;

  Layer *new_layer = animationlayer_alloc();
  STRNCPY_UTF8(new_layer->name, name);

  /* FIXME: For now, just add a keyframe strip. This may not be the right choice
   * going forward, and maybe it's better to allocate the strip at the first
   * use. */
  new_layer->strip_add(ANIM_STRIP_TYPE_KEYFRAME);

  grow_array_and_append<::AnimationLayer *>(&this->layer_array, &this->layer_array_num, new_layer);
  this->layer_active_index = this->layer_array_num - 1;

  return new_layer;
}

blender::Span<const Output *> Animation::outputs() const
{
  return blender::Span<Output *>{reinterpret_cast<Output **>(this->output_array),
                                 this->output_array_num};
}
blender::MutableSpan<Output *> Animation::outputs()
{
  return blender::MutableSpan<Output *>{reinterpret_cast<Output **>(this->output_array),
                                        this->output_array_num};
}
const Output *Animation::output(const int64_t index) const
{
  return &this->output_array[index]->wrap();
}
Output *Animation::output(const int64_t index)
{
  return &this->output_array[index]->wrap();
}

Output *Animation::output_add(ID *animated_id)
{
  Output &output = MEM_new<AnimationOutput>(__func__)->wrap();

  output.idtype = GS(animated_id->name);
  output.stable_index = atomic_add_and_fetch_int32(&this->last_output_stable_index, 1);

  /* The ID type bytes can be stripped from the name, as that information is
   * already stored in output.idtype. This also makes it easier to combine
   * names when multiple IDs share the same output. */
  STRNCPY_UTF8(output.fallback, animated_id->name + 2);

  /* Allocate the runtime struct. */
  output.runtime = MEM_new<Output_runtime>(__func__);
  output.runtime->ids.append(animated_id);

  /* Append the Output to the animation data-block. */
  grow_array_and_append<::AnimationOutput *>(
      &this->output_array, &this->output_array_num, &output);

  return &output;
}

/* ----- AnimationLayer C++ implementation ----------- */

blender::Span<const Strip *> Layer::strips() const
{
  return blender::Span<Strip *>{reinterpret_cast<Strip **>(this->strip_array),
                                this->strip_array_num};
}
blender::MutableSpan<Strip *> Layer::strips()
{
  return blender::MutableSpan<Strip *>{reinterpret_cast<Strip **>(this->strip_array),
                                       this->strip_array_num};
}
const Strip *Layer::strip(const int64_t index) const
{
  return &this->strip_array[index]->wrap();
}
Strip *Layer::strip(const int64_t index)
{
  return &this->strip_array[index]->wrap();
}

Strip *Layer::strip_add(const eAnimationStrip_type strip_type)
{
  ::AnimationStrip *dna_strip = animationstrip_alloc_infinite(strip_type);
  Strip &strip = dna_strip->wrap();

  /* Add the new strip to the strip array. */
  grow_array_and_append<::AnimationStrip *>(&this->strip_array, &this->strip_array_num, &strip);

  return &strip;
}

/* ----- KeyframeAnimationStrip C++ implementation ----------- */

blender::Span<const ChannelsForOutput *> KeyframeStrip::channels_for_output() const
{
  return blender::Span<ChannelsForOutput *>{
      reinterpret_cast<ChannelsForOutput **>(this->channels_for_output_array),
      this->channels_for_output_array_num};
}
blender::MutableSpan<ChannelsForOutput *> KeyframeStrip::channels_for_output()
{
  return blender::MutableSpan<ChannelsForOutput *>{
      reinterpret_cast<ChannelsForOutput **>(this->channels_for_output_array),
      this->channels_for_output_array_num};
}
const ChannelsForOutput *KeyframeStrip::channel_for_output(const int64_t index) const
{
  return &this->channels_for_output_array[index]->wrap();
}
ChannelsForOutput *KeyframeStrip::channel_for_output(const int64_t index)
{
  return &this->channels_for_output_array[index]->wrap();
}

template<> KeyframeStrip &Strip::as<KeyframeStrip>()
{
  BLI_assert_msg(type == ANIM_STRIP_TYPE_KEYFRAME,
                 "Strip is not of type ANIM_STRIP_TYPE_KEYFRAME");
  return *reinterpret_cast<KeyframeStrip *>(this);
}

const ChannelsForOutput *KeyframeStrip::chans_for_out(const Output &out) const
{
  /* FIXME: use a hash map lookup for this. */
  for (const ChannelsForOutput *channels : this->channels_for_output()) {
    if (channels->output_stable_index == out.stable_index) {
      return channels;
    }
  }
  return nullptr;
}

ChannelsForOutput *KeyframeStrip::chans_for_out(const Output &out)
{
  const auto *const_this = const_cast<const KeyframeStrip *>(this);
  const auto *const_channels = const_this->chans_for_out(out);
  return const_cast<ChannelsForOutput *>(const_channels);
}

ChannelsForOutput *KeyframeStrip::chans_for_out_add(const Output &out)
{
#ifndef NDEBUG
  BLI_assert_msg(chans_for_out(out) == nullptr,
                 "Cannot add chans-for-out for already-registered output");
#endif

  ChannelsForOutput &channels = MEM_new<AnimationChannelsForOutput>(__func__)->wrap();
  channels.output_stable_index = out.stable_index;

  grow_array_and_append<AnimationChannelsForOutput *>(
      &this->channels_for_output_array, &this->channels_for_output_array_num, &channels);

  return &channels;
}

FCurve *KeyframeStrip::fcurve_find(const Output &out, const char *rna_path, const int array_index)
{
  ChannelsForOutput *channels = this->chans_for_out(out);
  if (channels == nullptr) {
    return nullptr;
  }

  /* Copy of the logic in BKE_fcurve_find(), but then compatible with our array-of-FCurves instead
   * of ListBase. */

  for (FCurve *fcu : channels->fcurves()) {
    /* Check indices first, much cheaper than a string comparison. */
    /* Simple string-compare (this assumes that they have the same root...) */
    if (UNLIKELY(fcu->array_index == array_index && fcu->rna_path &&
                 fcu->rna_path[0] == rna_path[0] && STREQ(fcu->rna_path, rna_path)))
    {
      return fcu;
    }
  }
  return nullptr;
}

FCurve *KeyframeStrip::fcurve_find_or_create(const Output &out,
                                             const char *rna_path,
                                             const int array_index)
{
  FCurve *fcurve = this->fcurve_find(out, rna_path, array_index);
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

  ChannelsForOutput *channels = this->chans_for_out(out);
  if (channels == nullptr) {
    channels = this->chans_for_out_add(out);
  }

  if (channels->fcurve_array_num == 0) {
    fcurve->flag |= FCURVE_ACTIVE; /* First curve is added active. */
  }

  grow_array_and_append(&channels->fcurve_array, &channels->fcurve_array_num, fcurve);
  return fcurve;
}

/* KeyframeAnimationStrip C++ implementation. */

blender::Span<const FCurve *> ChannelsForOutput::fcurves() const
{
  return blender::Span<FCurve *>{reinterpret_cast<FCurve **>(this->fcurve_array),
                                 this->fcurve_array_num};
}
blender::MutableSpan<FCurve *> ChannelsForOutput::fcurves()
{
  return blender::MutableSpan<FCurve *>{reinterpret_cast<FCurve **>(this->fcurve_array),
                                        this->fcurve_array_num};
}
const FCurve *ChannelsForOutput::fcurve(const int64_t index) const
{
  return this->fcurve_array[index];
}
FCurve *ChannelsForOutput::fcurve(const int64_t index)
{
  return this->fcurve_array[index];
}

FCurve *keyframe_insert(Strip *strip,
                        const Output &out,
                        const char *rna_path,
                        const int array_index,
                        const float value,
                        const float time,
                        const eBezTriple_KeyframeType keytype)
{
  if (strip->type != ANIM_STRIP_TYPE_KEYFRAME) {
    /* TODO: handle this properly, in a way that can be communicated to the user. */
    std::fprintf(stderr,
                 "Strip is not of type ANIM_STRIP_TYPE_KEYFRAME, unable to insert keys here\n");
    return nullptr;
  }

  KeyframeStrip &key_strip = strip->wrap().as<KeyframeStrip>();
  FCurve *fcurve = key_strip.fcurve_find_or_create(out, rna_path, array_index);

  if (!BKE_fcurve_is_keyframable(fcurve)) {
    /* TODO: handle this properly, in a way that can be communicated to the user. */
    std::fprintf(stderr,
                 "FCurve %s[%d] for output %s doesn't allow inserting keys.\n",
                 rna_path,
                 array_index,
                 out.fallback);
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
                 out.fallback);
    return nullptr;
  }

  return fcurve;
}

}  // namespace blender::animrig
