/* SPDX-FileCopyrightText: 2023 Blender Foundation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file Animation data-block.
 * \ingroup bke
 */

#include "BLI_listbase.h"
#include "BLI_listbase_wrapper.hh"
#include "BLI_map.hh"
#include "BLI_string_utf8.h"

#include "BLO_read_write.hh"

#include "BKE_animation.hh"
#include "BKE_fcurve.h"
#include "BKE_idtype.hh"
#include "BKE_lib_id.hh"
#include "BKE_lib_query.hh"
#include "BKE_main.hh"

#include "ANIM_animation.hh"

#include "DNA_anim_types.h"
#include "DNA_defaults.h"

#include "BLT_translation.hh"

using namespace blender;

struct BlendWriter;
struct BlendDataReader;

static AnimationLayer *anim_layer_duplicate(const AnimationLayer *layer_src);
static AnimationOutput *anim_output_duplicate(const AnimationOutput *output_src);
static AnimationStrip *anim_strip_duplicate(const AnimationStrip *strip_src);
static AnimationStrip *anim_strip_duplicate_common(const AnimationStrip *strip_src);
static AnimationStrip *anim_strip_duplicate_keyframe(const AnimationStrip *strip_src);
static AnimationChannelsForOutput *anim_channels_for_output_duplicate(
    const AnimationChannelsForOutput *channels_src);

static void anim_strip_free_data_keyframe(AnimationStrip *strip);
static void anim_channels_for_output_free_data(AnimationChannelsForOutput *channels);

using anim_strip_duplicator = AnimationStrip *(*)(const AnimationStrip *strip_src);
using anim_strip_freeer = void (*)(AnimationStrip *strip);

/** Get a function that can duplicate a strip of the given type. */
static anim_strip_duplicator get_strip_duplicator(const eAnimationStrip_type strip_type)
{
  /* This could be a map lookup, but a `switch` will emit a compiler warning when a new strip type
   * was added to the enum and forgotten here. */
  switch (strip_type) {
    case ANIM_STRIP_TYPE_KEYFRAME:
      return anim_strip_duplicate_keyframe;
  }
  BLI_assert(!"unduplicatable strip type!");
  return nullptr;
}

/** Get a function that can free a strip of the given type. */
static anim_strip_freeer get_strip_freeer(const eAnimationStrip_type strip_type)
{
  /* This could be a map lookup, but a `switch` will emit a compiler warning when a new strip type
   * was added to the enum and forgotten here. */
  switch (strip_type) {
    case ANIM_STRIP_TYPE_KEYFRAME:
      return anim_strip_free_data_keyframe;
  }
  BLI_assert(!"unfreeable strip type!");
  return nullptr;
}

/** Deep copy an Animation data-block. */
static void animation_copy_data(Main * /*bmain*/, ID *id_dst, const ID *id_src, const int /*flag*/)
{
  Animation *anim_dst = (Animation *)id_dst;
  const Animation *anim_src = (const Animation *)id_src;

  /* Layers. */
  anim_dst->layer_array_num = anim_src->layer_array_num;
  anim_dst->layer_array = MEM_cnew_array<AnimationLayer *>(anim_src->layer_array_num, __func__);
  for (int i = 0; i < anim_src->layer_array_num; i++) {
    const AnimationLayer *layer_src = anim_src->layer_array[i];
    anim_dst->layer_array[i] = anim_layer_duplicate(layer_src);
  }

  /* Outputs. */
  anim_dst->output_array_num = anim_src->output_array_num;
  anim_dst->output_array = MEM_cnew_array<AnimationOutput *>(anim_src->output_array_num, __func__);
  for (int i = 0; i < anim_src->output_array_num; i++) {
    const AnimationOutput *output_src = anim_src->output_array[i];
    anim_dst->output_array[i] = anim_output_duplicate(output_src);
  }
}

/** Deep copy an AnimationLayer struct. */
static AnimationLayer *anim_layer_duplicate(const AnimationLayer *layer_src)
{
  AnimationLayer *layer_dst = static_cast<AnimationLayer *>(MEM_dupallocN(layer_src));

  /* Strips. */
  layer_dst->strip_array_num = layer_src->strip_array_num;
  layer_dst->strip_array = MEM_cnew_array<AnimationStrip *>(layer_src->strip_array_num, __func__);
  for (int i = 0; i < layer_src->strip_array_num; i++) {
    const AnimationStrip *strip_src = layer_src->strip_array[i];
    layer_dst->strip_array[i] = anim_strip_duplicate(strip_src);
  }

  return layer_dst;
}

static AnimationOutput *anim_output_duplicate(const AnimationOutput *output_src)
{
  AnimationOutput *output_dup = static_cast<AnimationOutput *>(MEM_dupallocN(output_src));
  return output_dup;
}

static AnimationStrip *anim_strip_duplicate(const AnimationStrip *strip_src)
{
  anim_strip_duplicator duplicator = get_strip_duplicator(eAnimationStrip_type(strip_src->type));
  return duplicator(strip_src);
}

static AnimationStrip *anim_strip_duplicate_common(const AnimationStrip *strip_src)
{
  return static_cast<AnimationStrip *>(MEM_dupallocN(strip_src));
}

static AnimationStrip *anim_strip_duplicate_keyframe(const AnimationStrip *strip_src)
{
  BLI_assert_msg(strip_src->type == ANIM_STRIP_TYPE_KEYFRAME,
                 "wrong type of strip for this function");

  AnimationStrip *strip_dst = anim_strip_duplicate_common(strip_src);
  const KeyframeAnimationStrip *key_strip_src = reinterpret_cast<const KeyframeAnimationStrip *>(
      strip_src);
  KeyframeAnimationStrip *key_strip_dst = reinterpret_cast<KeyframeAnimationStrip *>(strip_dst);

  key_strip_dst->channels_for_output_array_num = key_strip_src->channels_for_output_array_num;
  key_strip_dst->channels_for_output_array = MEM_cnew_array<AnimationChannelsForOutput *>(
      key_strip_src->channels_for_output_array_num, __func__);
  for (int i = 0; i < key_strip_src->channels_for_output_array_num; i++) {
    const AnimationChannelsForOutput *channels_src = key_strip_src->channels_for_output_array[i];
    AnimationChannelsForOutput *channels_dup = anim_channels_for_output_duplicate(channels_src);
    key_strip_dst->channels_for_output_array[i] = channels_dup;
  }

  return &key_strip_dst->strip;
}

static AnimationChannelsForOutput *anim_channels_for_output_duplicate(
    const AnimationChannelsForOutput *channels_src)
{
  AnimationChannelsForOutput *channels_dup = static_cast<AnimationChannelsForOutput *>(
      MEM_dupallocN(channels_src));

  channels_dup->fcurve_array_num = channels_src->fcurve_array_num;
  channels_dup->fcurve_array = MEM_cnew_array<FCurve *>(channels_src->fcurve_array_num, __func__);
  for (int i = 0; i < channels_src->fcurve_array_num; i++) {
    const FCurve *fcu_src = channels_src->fcurve_array[i];
    channels_dup->fcurve_array[i] = BKE_fcurve_copy(fcu_src);
  }

  return channels_dup;
}

/** Free (or release) any data used by this animation (does not free the animation itself). */
static void animation_free_data(ID *id)
{
  ((Animation *)id)->wrap().free_data();
}

void BKE_animation_strip_free_data(AnimationStrip *strip)
{
  anim_strip_freeer freeer = get_strip_freeer(eAnimationStrip_type(strip->type));
  return freeer(strip);
}

static void anim_strip_free_data_keyframe(AnimationStrip *strip)
{
  animrig::KeyframeStrip &key_strip = strip->wrap().as<animrig::KeyframeStrip>();

  for (ChannelsForOutput *chans_for_out : key_strip.channels_for_output_span()) {
    anim_channels_for_output_free_data(chans_for_out);
    MEM_delete(chans_for_out);
  }
  MEM_SAFE_FREE(key_strip.channels_for_output_array);
  key_strip.channels_for_output_array_num = 0;
}

static void anim_channels_for_output_free_data(AnimationChannelsForOutput *channels)
{
  for (FCurve *fcu : channels->wrap().fcurves()) {
    BKE_fcurve_free(fcu);
  }
  MEM_SAFE_FREE(channels->fcurve_array);
  channels->fcurve_array_num = 0;
}

static void animation_foreach_id(ID *id, LibraryForeachIDData *data)
{
  animrig::Animation &anim = reinterpret_cast<Animation *>(id)->wrap();

  for (animrig::Layer *layer : anim.layers()) {
    for (animrig::Strip *strip : layer->strips()) {
      switch (strip->type) {
        case ANIM_STRIP_TYPE_KEYFRAME: {
          auto &key_strip = strip->as<animrig::KeyframeStrip>();
          for (animrig::ChannelsForOutput *chans_for_out : key_strip.channels_for_output_span()) {
            for (FCurve *fcurve : chans_for_out->fcurves()) {
              BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(data, BKE_fcurve_foreach_id(fcurve, data));
            }
          }
        }
      }
    }
  }
}

static void write_channels_for_output(BlendWriter *writer,
                                      animrig::ChannelsForOutput &chans_for_out)
{
  Span<FCurve *> fcurves = chans_for_out.fcurves();

  /* Construct a listbase to pass to the BKE function. This is for historical
   * purposes, as Actions also store the FCurves as ListBase. */
  for (int i = 0; i < fcurves.size() - 1; i++) {
    fcurves[i]->next = fcurves[i + 1];
  }

  BLI_assert(BLI_listbase_is_empty(&chans_for_out.fcurve_listbase));
  animrig::ChannelsForOutput backup = chans_for_out;

  /* Prepare for writing. */
  chans_for_out.fcurve_array = nullptr;
  chans_for_out.fcurve_array_num = 0;
  chans_for_out.fcurve_listbase.first = fcurves[0];
  chans_for_out.fcurve_listbase.last = fcurves[fcurves.size() - 1];

  BLO_write_struct(writer, AnimationChannelsForOutput, &chans_for_out);
  BKE_fcurve_blend_write(writer, &chans_for_out.fcurve_listbase);

  /* Reset the pointers to nullptr again to clean up. */
  for (FCurve *fcu : fcurves) {
    fcu->next = nullptr;
  }

  chans_for_out = backup;
  BLI_assert(BLI_listbase_is_empty(&chans_for_out.fcurve_listbase));
}

static void write_keyframe_strip(BlendWriter *writer, animrig::KeyframeStrip &key_strip)
{
  BLO_write_struct(writer, KeyframeAnimationStrip, &key_strip);

  auto channels_for_output = key_strip.channels_for_output_span();
  BLO_write_pointer_array(writer, channels_for_output.size(), channels_for_output.data());

  for (animrig::ChannelsForOutput *chans_for_out : channels_for_output) {
    write_channels_for_output(writer, *chans_for_out);
  }
}

static void write_strips(BlendWriter *writer, Span<animrig::Strip *> strips)
{
  BLO_write_pointer_array(writer, strips.size(), strips.data());

  for (animrig::Strip *strip : strips) {
    switch (strip->type) {
      case ANIM_STRIP_TYPE_KEYFRAME: {
        auto &key_strip = strip->as<animrig::KeyframeStrip>();
        write_keyframe_strip(writer, key_strip);
      }
    }
  }
}

static void write_layers(BlendWriter *writer, Span<animrig::Layer *> layers)
{
  BLO_write_pointer_array(writer, layers.size(), layers.data());

  for (animrig::Layer *layer : layers) {
    BLO_write_struct(writer, AnimationLayer, layer);
    write_strips(writer, layer->strips());
  }
}

static void write_outputs(BlendWriter *writer, Span<animrig::Output *> outputs)
{
  BLO_write_pointer_array(writer, outputs.size(), outputs.data());
  for (animrig::Output *output : outputs) {
    BLO_write_struct(writer, AnimationOutput, output);
  }
}

static void animation_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  animrig::Animation &anim = reinterpret_cast<Animation *>(id)->wrap();

  BLO_write_id_struct(writer, Animation, id_address, &anim.id);
  BKE_id_blend_write(writer, &anim.id);

  write_layers(writer, anim.layers());
  write_outputs(writer, anim.outputs());
}

static void read_chans_for_out(BlendDataReader *reader, animrig::ChannelsForOutput &chans_for_out)
{
  // BLO_read_pointer_array(reader, reinterpret_cast<void **>(&chans_for_out.fcurve_array));

  /* Read the FCurves ListBase. */
  BLO_read_list(reader, &chans_for_out.fcurve_listbase);
  BKE_fcurve_blend_read_data(reader, &chans_for_out.fcurve_listbase);

  /* Allocate an appropriately-sized array. */
  chans_for_out.fcurve_array_num = BLI_listbase_count(&chans_for_out.fcurve_listbase);
  chans_for_out.fcurve_array = MEM_cnew_array<FCurve *>(chans_for_out.fcurve_array_num, __func__);

  /* Convert the ListBase to the array. */
  int fcu_index = 0;
  LISTBASE_FOREACH_MUTABLE (FCurve *, fcu, &chans_for_out.fcurve_listbase) {
    chans_for_out.fcurve_array[fcu_index++] = fcu;
    fcu->prev = nullptr;
    fcu->next = nullptr;
  }
  BLI_listbase_clear(&chans_for_out.fcurve_listbase);
}

static void read_keyframe_strip(BlendDataReader *reader, animrig::KeyframeStrip &strip)
{
  BLO_read_pointer_array(reader, reinterpret_cast<void **>(&strip.channels_for_output_array));

  for (int i = 0; i < strip.channels_for_output_array_num; i++) {
    BLO_read_data_address(reader, &strip.channels_for_output_array[i]);
    AnimationChannelsForOutput *chans_for_out = strip.channels_for_output_array[i];

    read_chans_for_out(reader, chans_for_out->wrap());
  }
}

static void read_animation_layers(BlendDataReader *reader, animrig::Animation &anim)
{
  BLO_read_pointer_array(reader, reinterpret_cast<void **>(&anim.layer_array));

  for (int layer_idx = 0; layer_idx < anim.layer_array_num; layer_idx++) {
    BLO_read_data_address(reader, &anim.layer_array[layer_idx]);
    AnimationLayer *layer = anim.layer_array[layer_idx];

    BLO_read_pointer_array(reader, reinterpret_cast<void **>(&layer->strip_array));
    for (int strip_idx = 0; strip_idx < layer->strip_array_num; strip_idx++) {
      BLO_read_data_address(reader, &layer->strip_array[strip_idx]);
      AnimationStrip *strip = layer->strip_array[strip_idx];

      switch (strip->type) {
        case ANIM_STRIP_TYPE_KEYFRAME: {
          auto &key_strip = strip->wrap().as<animrig::KeyframeStrip>();
          read_keyframe_strip(reader, key_strip);
        }
      }
    }
  }
}

static void read_animation_outputs(BlendDataReader *reader, animrig::Animation &anim)
{
  BLO_read_pointer_array(reader, reinterpret_cast<void **>(&anim.output_array));

  for (int i = 0; i < anim.output_array_num; i++) {
    BLO_read_data_address(reader, &anim.output_array[i]);
  }
}

static void animation_blend_read_data(BlendDataReader *reader, ID *id)
{
  animrig::Animation &animation = reinterpret_cast<Animation *>(id)->wrap();
  read_animation_layers(reader, animation);
  read_animation_outputs(reader, animation);
}

IDTypeInfo IDType_ID_AN = {
    /*id_code*/ ID_AN,
    /*id_filter*/ FILTER_ID_AN,
    /*dependencies_id_types*/ 0,
    /*main_listbase_index*/ INDEX_ID_AN,
    /*struct_size*/ sizeof(Animation),
    /*name*/ "Animation",
    /*name_plural*/ "animations",
    /*translation_context*/ BLT_I18NCONTEXT_ID_ANIMATION,
    /*flags*/ IDTYPE_FLAGS_NO_ANIMDATA,
    /*asset_type_info*/ nullptr,

    /*init_data*/ nullptr,
    /*copy_data*/ animation_copy_data,
    /*free_data*/ animation_free_data,
    /*make_local*/ nullptr,
    /*foreach_id*/ animation_foreach_id,
    /*foreach_cache*/ nullptr,
    /*foreach_path*/ nullptr,
    /*owner_pointer_get*/ nullptr,

    /*blend_write*/ animation_blend_write,
    /*blend_read_data*/ animation_blend_read_data,
    /*blend_read_after_liblink*/ nullptr,

    /*blend_read_undo_preserve*/ nullptr,

    /*lib_override_apply_post*/ nullptr,
};

Animation *BKE_animation_add(Main *bmain, const char name[])
{
  Animation *anim = static_cast<Animation *>(BKE_id_new(bmain, ID_AN, name));
  return anim;
}
