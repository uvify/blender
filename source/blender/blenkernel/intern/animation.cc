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

#include "BKE_fcurve.h"
#include "BKE_idtype.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_main.h"

#include "ANIM_animation.hh"

#include "DNA_anim_types.h"
#include "DNA_defaults.h"

#include "BLT_translation.h"

using namespace blender;

struct BlendWriter;
struct BlendDataReader;

static AnimationLayer *anim_layer_duplicate(const AnimationLayer *layer_src);
static AnimationStrip *anim_strip_duplicate(const AnimationStrip *strip_src);
static AnimationStrip *anim_strip_duplicate_common(const AnimationStrip *strip_src);
static AnimationStrip *anim_strip_duplicate_keyframe(const AnimationStrip *strip_src);

static void anim_layer_free_data(AnimationLayer *layer);
static void anim_strip_free_data(AnimationStrip *strip);
static void anim_strip_free_data_keyframe(AnimationStrip *strip);

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

  /* Layers. **/
  anim_dst->layer_array_num = anim_src->layer_array_num;
  anim_dst->layer_array = MEM_cnew_array<AnimationLayer *>(anim_src->layer_array_num, __func__);
  for (int i = 0; i < anim_src->layer_array_num; i++) {
    const AnimationLayer *layer_src = anim_src->layer_array[i];
    anim_dst->layer_array[i] = anim_layer_duplicate(layer_src);
  }

  BLI_duplicatelist(&anim_dst->outputs, &anim_src->outputs);
}

/** Deep copy an AnimationLayer struct. */
static AnimationLayer *anim_layer_duplicate(const AnimationLayer *layer_src)
{
  printf("anim_layer_duplicate: duplicating layer %s\n", layer_src->name);
  AnimationLayer *layer_dst = static_cast<AnimationLayer *>(MEM_dupallocN(layer_src));

  /* Strips. */
  BLI_listbase_clear(&layer_dst->strips);
  for (const AnimationStrip *strip_src : ConstListBaseWrapper<AnimationStrip>(&layer_src->strips))
  {
    AnimationStrip *strip_dst = anim_strip_duplicate(strip_src);
    BLI_addtail(&layer_dst->strips, strip_dst);
  }

  return layer_dst;
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
  KeyframeAnimationStrip *key_strip_dst = reinterpret_cast<KeyframeAnimationStrip *>(strip_dst);

  BLI_duplicatelist(&key_strip_dst->channels_for_output, &key_strip_dst->channels_for_output);
  for (auto *channels :
       ListBaseWrapper<AnimationChannelsForOutput>(&key_strip_dst->channels_for_output))
  {
    /* Contrary to BLI_duplicatelist(), BKE_fcurves_copy() does not support having the same list as
     * 'src' and 'dest'. To avoid having to track the original source list (which would mean
     * iterating over key_strip_dst->channels_for_output and strip_src->channels_for_output in
     * parallel), just copy to a separate listbase and reassign. The original listbase items are
     * still owned by strip_src. */
    ListBase fcurves_copy = {0};
    BKE_fcurves_copy(&fcurves_copy, &channels->fcurves);
    channels->fcurves = fcurves_copy;
  }

  return &key_strip_dst->strip;
}

void BKE_animation_free_data(Animation *animation)
{
  /* TODO: move this entire function into the animrig::Animation class. */
  animrig::Animation &anim = animation->wrap();

  /* Free layers. */
  for (animrig::Layer *layer : anim.layers()) {
    anim_layer_free_data(layer);
    MEM_delete(layer);
  }
  MEM_SAFE_FREE(animation->layer_array);
  animation->layer_array_num = 0;

  for (AnimationOutput *output : ListBaseWrapper<AnimationOutput>(&animation->outputs)) {
    /* TODO: Move freeing of Output runtime data to another function. */
    MEM_freeN(output->runtime.id);
  }
  BLI_freelistN(&animation->outputs);
}

/** Free (or release) any data used by this animation (does not free the animation itself). */
static void animation_free_data(ID *id)
{
  BKE_animation_free_data((Animation *)id);
}

static void anim_layer_free_data(AnimationLayer *layer)
{
  for (AnimationStrip *strip : ListBaseWrapper<AnimationStrip>(&layer->strips)) {
    anim_strip_free_data(strip);
  }
  BLI_freelistN(&layer->strips);
}

static void anim_strip_free_data(AnimationStrip *strip)
{
  anim_strip_freeer freeer = get_strip_freeer(eAnimationStrip_type(strip->type));
  return freeer(strip);
}

static void anim_strip_free_data_keyframe(AnimationStrip *strip)
{
  BLI_assert_msg(strip->type == ANIM_STRIP_TYPE_KEYFRAME, "wrong type of strip for this function");
  KeyframeAnimationStrip *key_strip = reinterpret_cast<KeyframeAnimationStrip *>(strip);

  for (AnimationChannelsForOutput *chans_for_out :
       ListBaseWrapper<AnimationChannelsForOutput>(&key_strip->channels_for_output))
  {
    BKE_fcurves_free(&chans_for_out->fcurves);
  }
  BLI_freelistN(&key_strip->channels_for_output);
}

static void animation_foreach_id(ID *id, LibraryForeachIDData *data)
{
  Animation *anim = reinterpret_cast<Animation *>(id);
  const int flag = BKE_lib_query_foreachid_process_flags_get(data);

  // LISTBASE_FOREACH (FCurve *, fcu, &anim->curves) {
  //   BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(data, BKE_fcurve_foreach_id(fcu, data));
  // }

  // LISTBASE_FOREACH (TimeMarker *, marker, &anim->markers) {
  //   BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, marker->camera, IDWALK_CB_NOP);
  // }

  // if (flag & IDWALK_DO_DEPRECATED_POINTERS) {
  //   LISTBASE_FOREACH (banimionChannel *, chan, &anim->chanbase) {
  //     BKE_LIB_FOREACHID_PROCESS_ID_NOCHECK(data, chan->ipo, IDWALK_CB_USER);
  //     LISTBASE_FOREACH (bConstraintChannel *, chan_constraint, &chan->constraintChannels) {
  //       BKE_LIB_FOREACHID_PROCESS_ID_NOCHECK(data, chan_constraint->ipo, IDWALK_CB_USER);
  //     }
  //   }
  // }
}

static void animation_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  Animation *animation = (Animation *)id;
  int the_id = SDNA_TYPE_FROM_STRUCT(Animation);

  // BLO_write_id_struct(writer, Animation, id_address, &animation->id);
  BKE_id_blend_write(writer, &animation->id);

  // BKE_fcurve_blend_write(writer, &animation->curves);

  // LISTBASE_FOREACH (banimationGroup *, grp, &animation->groups) {
  //   BLO_write_struct(writer, banimationGroup, grp);
  // }

  // LISTBASE_FOREACH (TimeMarker *, marker, &animation->markers) {
  //   BLO_write_struct(writer, TimeMarker, marker);
  // }

  // BKE_previewimg_blend_write(writer, animation->preview);
}

static void animation_blend_read_data(BlendDataReader *reader, ID *id)
{
  Animation *animation = (Animation *)id;

  // BLO_read_list(reader, &animation->curves);
  // BLO_read_list(reader, &animation->chanbase); /* XXX deprecated - old animation system */
  // BLO_read_list(reader, &animation->groups);
  // BLO_read_list(reader, &animation->markers);

  // /* XXX deprecated - old animation system <<< */
  // LISTBASE_FOREACH (banimationChannel *, achan, &animation->chanbase) {
  //   BLO_read_data_address(reader, &achan->grp);

  //   BLO_read_list(reader, &achan->constraintChannels);
  // }
  // /* >>> XXX deprecated - old animation system */

  // BKE_fcurve_blend_read_data(reader, &animation->curves);

  // LISTBASE_FOREACH (banimationGroup *, agrp, &animation->groups) {
  //   BLO_read_data_address(reader, &agrp->channels.first);
  //   BLO_read_data_address(reader, &agrp->channels.last);
  // }

  // BLO_read_data_address(reader, &animation->preview);
  // BKE_previewimg_blend_read(reader, animation->preview);
}

IDTypeInfo IDType_ID_AN = {
    /*id_code*/ ID_AC,
    /*id_filter*/ FILTER_ID_AN,
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
