/* SPDX-FileCopyrightText: 2023 Blender Developers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_anim_defaults.h"
#include "DNA_anim_types.h"
#include "DNA_defaults.h"

#include "BLI_listbase.h"
#include "BLI_listbase_wrapper.hh"
#include "BLI_math_base.h"
#include "BLI_string.h"
#include "BLI_string_utf8.h"
#include "BLI_string_utils.hh"

#include "BKE_anim_data.h"
#include "BKE_animation.hh"
#include "BKE_fcurve.h"
#include "BKE_lib_id.hh"

#include "ED_keyframing.hh"

#include "MEM_guardedalloc.h"

#include "atomic_ops.h"

#include "ANIM_animation.hh"
#include "ANIM_fcurve.hh"

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
  T *new_array = reinterpret_cast<T *>(
      MEM_cnew_array<T *>(new_array_num, "animrig::animation/grow_array"));

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

template<typename T> static void shrink_array(T **array, int *num, const int shrink_num)
{
  BLI_assert(shrink_num > 0);
  const int new_array_num = *num - shrink_num;
  T *new_array = reinterpret_cast<T *>(MEM_cnew_array<T *>(new_array_num, __func__));

  blender::uninitialized_move_n(*array, new_array_num, new_array);
  MEM_freeN(*array);

  *array = new_array;
  *num = new_array_num;
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

Layer *Animation::layer_add(const StringRefNull name)
{
  using namespace blender::animrig;

  Layer *new_layer = animationlayer_alloc();
  STRNCPY_UTF8(new_layer->name, name.c_str());

  grow_array_and_append<::AnimationLayer *>(&this->layer_array, &this->layer_array_num, new_layer);
  this->layer_active_index = this->layer_array_num - 1;

  return new_layer;
}

bool Animation::layer_remove(Layer &layer_to_remove)
{
  const int64_t layer_index = this->find_layer_index(layer_to_remove);
  if (layer_index < 0) {
    return false;
  }

  BLI_assert(layer_index < this->layer_array_num);

  /* Move [layer_index+1:end] to [layer_index:end-1], but only if the `layer_to_remove` is not
   * already at the end. */
  if (layer_index < this->layer_array_num - 1) {
    ::AnimationLayer **start = this->layer_array + layer_index;
    const int64_t num_to_move = this->layer_array_num - layer_index - 1;
    memmove((void *)start, (void *)(start + 1), num_to_move * sizeof(::AnimationLayer *));
  }

  shrink_array<::AnimationLayer *>(&this->layer_array, &this->layer_array_num, 1);

  BKE_animation_layer_free_data(&layer_to_remove);
  MEM_delete(&layer_to_remove);

  return true;
}

int64_t Animation::find_layer_index(const Layer &layer) const
{
  for (const int64_t layer_index : this->layers().index_range()) {
    const Layer *visit_layer = this->layer(layer_index);
    if (visit_layer == &layer) {
      return layer_index;
    }
  }
  return -1;
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

Output *Animation::output_for_stable_index(const output_index_t stable_index)
{
  const Output *out = const_cast<const Animation *>(this)->output_for_stable_index(stable_index);
  return const_cast<Output *>(out);
}

const Output *Animation::output_for_stable_index(const output_index_t stable_index) const
{
  /* TODO: implement hashmap lookup. */
  for (const Output *out : outputs()) {
    if (out->stable_index == stable_index) {
      return out;
    }
  }
  return nullptr;
}

static void anim_output_name_ensure_unique(Animation &animation, Output &out)
{
  /* Cannot capture parameters by reference in the lambda, as that would change its signature
   * and no longer be compatible with BLI_uniquename_cb(). That's why this struct is necessary. */
  struct DupNameCheckData {
    Animation &anim;
    Output &out;
  };
  DupNameCheckData check_data = {animation, out};

  auto check_name_is_used = [](void *arg, const char *name) -> bool {
    DupNameCheckData *data = static_cast<DupNameCheckData *>(arg);
    for (const Output *output : data->anim.outputs()) {
      if (output == &data->out) {
        /* Don't compare against the output that's being renamed. */
        continue;
      }
      if (STREQ(output->name, name)) {
        return true;
      }
    }
    return false;
  };

  BLI_uniquename_cb(check_name_is_used, &check_data, "", '.', out.name, sizeof(out.name));
}

void Animation::output_name_set(Output &out, const StringRefNull new_name)
{
  STRNCPY_UTF8(out.name, new_name.c_str());
  anim_output_name_ensure_unique(*this, out);

  /* TODO: update `AnimData::animation_output_name` field of any ID that is animated by this.
   *  When this gets added, reconsider the code in Animation::unassign_id((). */
}

Output *Animation::output_find_by_name(const StringRefNull output_name)
{
  for (Output *out : outputs()) {
    if (STREQ(out->name, output_name.c_str())) {
      return out;
    }
  }
  return nullptr;
}

Output *Animation::output_for_id(const ID *animated_id)
{
  const Output *out = const_cast<const Animation *>(this)->output_for_id(animated_id);
  return const_cast<Output *>(out);
}

const Output *Animation::output_for_id(const ID *animated_id) const

{
  const AnimData *adt = BKE_animdata_from_id(animated_id);

  /* Note that there is no check that `adt->animation` is actually `this`. */

  const Output *out = this->output_for_stable_index(adt->output_stable_index);
  if (!out) {
    return nullptr;
  }
  if (!out->is_suitable_for(animated_id)) {
    return nullptr;
  }
  return out;
}

Output &Animation::output_allocate_()
{
  Output &output = MEM_new<AnimationOutput>(__func__)->wrap();
  this->last_output_stable_index++;
  BLI_assert_msg(this->last_output_stable_index > 0,
                 "Animation Output stable index 32-bit overflow");
  output.stable_index = this->last_output_stable_index;
  return output;
}

Output *Animation::output_add()
{
  Output &output = this->output_allocate_();

  /* Append the Output to the animation data-block. */
  grow_array_and_append<::AnimationOutput *>(
      &this->output_array, &this->output_array_num, &output);

  return &output;
}

Output *Animation::find_suitable_output_for(const ID *animated_id)
{
  AnimData *adt = BKE_animdata_from_id(animated_id);

  /* The stable index is only valid when this animation has already been
   * assigned. Otherwise it's meaningless. */
  if (adt && adt->animation == this) {
    Output *out = this->output_for_stable_index(adt->output_stable_index);
    if (out && out->is_suitable_for(animated_id)) {
      return out;
    }
  }

  /* Try the output name from the AnimData, if it is set,*/
  if (adt && adt->output_name[0]) {
    Output *out = this->output_find_by_name(adt->output_name);
    if (out && out->is_suitable_for(animated_id)) {
      return out;
    }
  }

  /* As a last resort, search for the ID name. */
  Output *out = this->output_find_by_name(animated_id->name);
  if (out && out->is_suitable_for(animated_id)) {
    return out;
  }

  return nullptr;
}

bool Animation::assign_id(Output *output, ID *animated_id)
{
  AnimData *adt = BKE_animdata_ensure_id(animated_id);
  if (!adt) {
    return false;
  }

  if (adt->animation) {
    /* Unassign the ID from its existing animation first, or use the top-level
     * function `assign_animation(anim, ID)`. */
    return false;
  }

  if (output) {
    if (!output->assign_id(animated_id)) {
      return false;
    }

    /* If the output is not yet named, use the ID name. */
    if (output->name[0] == '\0') {
      this->output_name_set(*output, animated_id->name);
    }
    /* Always make sure the ID's output name matches the assigned output. */
    STRNCPY_UTF8(adt->output_name, output->name);
  }
  else {
    adt->output_stable_index = 0;
    /* Keep adt->output_name untouched, as A) it's not necessary to erase it
     * because `adt->output_stable_index = 0` already indicates "no output yet",
     * and B) it would erase information that can later be used when trying to
     * identify which output this was once attached to.  */
  }

  adt->animation = this;
  id_us_plus(&this->id);

  return true;
}

void Animation::unassign_id(ID *animated_id)
{
  AnimData *adt = BKE_animdata_from_id(animated_id);
  BLI_assert_msg(adt->animation == this, "ID is not assigned to this Animation");

  /* Before unassigning, make sure that the stored Output name is up to date.
   * This can be removed when Animation::output_name_set() actually updates this whenever the
   * output name changes. */
  const Output *out = this->output_for_stable_index(adt->output_stable_index);
  if (out) {
    STRNCPY_UTF8(adt->output_name, out->name);
  }

  id_us_min(&this->id);
  adt->animation = nullptr;
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

bool Layer::strip_remove(Strip &strip_to_remove)
{
  const int64_t strip_index = this->find_strip_index(strip_to_remove);
  if (strip_index < 0) {
    return false;
  }

  BLI_assert(strip_index < this->strip_array_num);

  /* Move [strip_index+1:end] to [strip_index:end-1], but only if the `strip_to_remove` is not
   * already at the end. */
  if (strip_index < this->strip_array_num - 1) {
    ::AnimationStrip **start = this->strip_array + strip_index;
    const int64_t num_to_move = this->strip_array_num - strip_index - 1;
    memmove((void *)start, (void *)(start + 1), num_to_move * sizeof(::AnimationStrip *));
  }

  shrink_array<::AnimationStrip *>(&this->strip_array, &this->strip_array_num, 1);

  BKE_animation_strip_free_data(&strip_to_remove);
  MEM_delete(&strip_to_remove);

  return true;
}

int64_t Layer::find_strip_index(const Strip &strip) const
{
  for (const int64_t strip_index : this->strips().index_range()) {
    const Strip *visit_strip = this->strip(strip_index);
    if (visit_strip == &strip) {
      return strip_index;
    }
  }
  return -1;
}

/* ----- AnimationOutput C++ implementation ----------- */

bool Output::assign_id(ID *animated_id)
{
  if (!this->is_suitable_for(animated_id)) {
    return false;
  }

  AnimData *adt = BKE_animdata_ensure_id(animated_id);
  if (!adt) {
    return false;
  }

  if (this->idtype == 0) {
    this->idtype = GS(animated_id->name);
  }

  adt->output_stable_index = this->stable_index;
  return true;
}

bool Output::is_suitable_for(const ID *animated_id) const
{
  /* Check that the ID type is compatible with this output. */
  const int animated_idtype = GS(animated_id->name);
  return this->idtype == 0 || this->idtype == animated_idtype;
}

bool assign_animation(Animation &anim, ID *animated_id)
{
  unassign_animation(animated_id);

  Output *out = anim.find_suitable_output_for(animated_id);
  return anim.assign_id(out, animated_id);
}

void unassign_animation(ID *animated_id)
{
  Animation *anim = get_animation(animated_id);
  if (!anim) {
    return;
  }
  anim->unassign_id(animated_id);
}

Animation *get_animation(ID *animated_id)
{
  AnimData *adt = BKE_animdata_from_id(animated_id);
  if (!adt) {
    return nullptr;
  }
  if (!adt->animation) {
    return nullptr;
  }
  return &adt->animation->wrap();
}

/* ----- AnimationStrip C++ implementation ----------- */

bool Strip::contains_frame(const float frame_time) const
{
  return frame_start <= frame_time && frame_time <= frame_end;
}

bool Strip::is_last_frame(const float frame_time) const
{
  /* Maybe this needs a more advanced equality check. Implement that when
   * we have an actual example case that breaks. */
  return this->frame_end == frame_time;
}

void Strip::resize(const float frame_start, const float frame_end)
{
  BLI_assert(frame_start <= frame_end);
  BLI_assert_msg(frame_start < std::numeric_limits<float>::infinity(),
                 "only the end frame can be at positive infinity");
  BLI_assert_msg(frame_end > -std::numeric_limits<float>::infinity(),
                 "only the start frame can be at negative infinity");
  this->frame_start = frame_start;
  this->frame_end = frame_end;
}

/* ----- KeyframeAnimationStrip C++ implementation ----------- */

blender::Span<const ChannelsForOutput *> KeyframeStrip::channels_for_output_span() const
{
  return blender::Span<ChannelsForOutput *>{
      reinterpret_cast<ChannelsForOutput **>(this->channels_for_output_array),
      this->channels_for_output_array_num};
}
blender::MutableSpan<ChannelsForOutput *> KeyframeStrip::channels_for_output_span()
{
  return blender::MutableSpan<ChannelsForOutput *>{
      reinterpret_cast<ChannelsForOutput **>(this->channels_for_output_array),
      this->channels_for_output_array_num};
}
const ChannelsForOutput *KeyframeStrip::channels_for_output_at(const int64_t array_index) const
{
  return &this->channels_for_output_array[array_index]->wrap();
}
ChannelsForOutput *KeyframeStrip::channels_for_output_at(const int64_t array_index)
{
  return &this->channels_for_output_array[array_index]->wrap();
}

template<> KeyframeStrip &Strip::as<KeyframeStrip>()
{
  BLI_assert_msg(type == ANIM_STRIP_TYPE_KEYFRAME,
                 "Strip is not of type ANIM_STRIP_TYPE_KEYFRAME");
  return *reinterpret_cast<KeyframeStrip *>(this);
}

template<> const KeyframeStrip &Strip::as<KeyframeStrip>() const
{
  BLI_assert_msg(type == ANIM_STRIP_TYPE_KEYFRAME,
                 "Strip is not of type ANIM_STRIP_TYPE_KEYFRAME");
  return *reinterpret_cast<const KeyframeStrip *>(this);
}

const ChannelsForOutput *KeyframeStrip::chans_for_out(
    const output_index_t output_stable_index) const
{
  /* FIXME: use a hash map lookup for this. */
  for (const ChannelsForOutput *channels : this->channels_for_output_span()) {
    if (channels->output_stable_index == output_stable_index) {
      return channels;
    }
  }
  return nullptr;
}
ChannelsForOutput *KeyframeStrip::chans_for_out(const output_index_t output_stable_index)
{
  const auto *const_this = const_cast<const KeyframeStrip *>(this);
  const auto *const_channels = const_this->chans_for_out(output_stable_index);
  return const_cast<ChannelsForOutput *>(const_channels);
}
const ChannelsForOutput *KeyframeStrip::chans_for_out(const Output &out) const
{
  return this->chans_for_out(out.stable_index);
}
ChannelsForOutput *KeyframeStrip::chans_for_out(const Output &out)
{
  return this->chans_for_out(out.stable_index);
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

FCurve *KeyframeStrip::fcurve_find(const Output &out,
                                   const StringRefNull rna_path,
                                   const int array_index)
{
  ChannelsForOutput *channels = this->chans_for_out(out);
  if (channels == nullptr) {
    return nullptr;
  }

  /* Copy of the logic in BKE_fcurve_find(), but then compatible with our array-of-FCurves
   * instead of ListBase. */

  for (FCurve *fcu : channels->fcurves()) {
    /* Check indices first, much cheaper than a string comparison. */
    /* Simple string-compare (this assumes that they have the same root...) */
    if (fcu->array_index == array_index && fcu->rna_path && StringRef(fcu->rna_path) == rna_path) {
      return fcu;
    }
  }
  return nullptr;
}

FCurve *KeyframeStrip::fcurve_find_or_create(const Output &out,
                                             const StringRefNull rna_path,
                                             const int array_index)
{
  FCurve *fcurve = this->fcurve_find(out, rna_path, array_index);
  if (fcurve) {
    return fcurve;
  }

  /* Copied from ED_action_fcurve_ensure(). */
  /* TODO: move to separate function, call that from both places. */
  fcurve = BKE_fcurve_create();
  fcurve->rna_path = BLI_strdup(rna_path.c_str());
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

FCurve *KeyframeStrip::keyframe_insert(const Output &out,
                                       const StringRefNull rna_path,
                                       const int array_index,
                                       const float2 time_value,
                                       const KeyframeSettings &settings)
{
  FCurve *fcurve = this->fcurve_find_or_create(out, rna_path, array_index);

  if (!BKE_fcurve_is_keyframable(fcurve)) {
    /* TODO: handle this properly, in a way that can be communicated to the user. */
    std::fprintf(stderr,
                 "FCurve %s[%d] for output %s doesn't allow inserting keys.\n",
                 rna_path.c_str(),
                 array_index,
                 out.name);
    return nullptr;
  }

  /* TODO: Handle the eInsertKeyFlags. */
  const int index = insert_vert_fcurve(fcurve, time_value, settings, eInsertKeyFlags(0));
  if (index < 0) {
    std::fprintf(stderr,
                 "Could not insert key into FCurve %s[%d] for output %s.\n",
                 rna_path.c_str(),
                 array_index,
                 out.name);
    return nullptr;
  }

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

const FCurve *ChannelsForOutput::fcurve_find(const StringRefNull rna_path,
                                             const int array_index) const
{
  for (const FCurve *fcu : this->fcurves()) {
    /* Check indices first, much cheaper than a string comparison. */
    if (fcu->array_index == array_index && fcu->rna_path && StringRef(fcu->rna_path) == rna_path) {
      return fcu;
    }
  }
  return nullptr;
}

}  // namespace blender::animrig
