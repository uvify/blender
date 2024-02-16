/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup RNA
 */

#include <cstdlib>

#include "DNA_anim_types.h"

#include "BLI_utildefines.h"

#include "BLT_translation.hh"

#include "MEM_guardedalloc.h"

#include "RNA_access.hh"
#include "RNA_define.hh"
#include "RNA_enum_types.hh"

#include "rna_internal.hh"

#include "WM_api.hh"
#include "WM_types.hh"

const EnumPropertyItem rna_enum_layer_mix_mode_items[] = {
    {ANIM_LAYER_MIX_REPLACE,
     "REPLACE",
     0,
     "Replace",
     "Animation channels in this layer override the same channels from underlying layers"},
    {ANIM_LAYER_MIX_OFFSET,
     "OFFSET",
     0,
     "Offset",
     "Animation channels in this layer are added, as sequential operations, to the output of "
     "underlying layers"},
    {ANIM_LAYER_MIX_ADD,
     "ADD",
     0,
     "Add",
     "Animation channels in this layer are added, on a per-channel basis, to the output of "
     "underlying layers"},
    {ANIM_LAYER_MIX_SUBTRACT,
     "SUBTRACT",
     0,
     "Subtract",
     "Animation channels in this layer are subtracted, on a per-channel basis, from the output of "
     "underlying layers"},
    {ANIM_LAYER_MIX_MULTIPLY,
     "MULTIPLY",
     0,
     "Multiply",
     "Animation channels in this layer are multiplied, on a per-channel basis, with the output of "
     "underlying layers"},
    {0, nullptr, 0, nullptr, nullptr},
};

const EnumPropertyItem rna_enum_strip_type_items[] = {
    {ANIM_STRIP_TYPE_KEYFRAME,
     "KEYFRAME",
     0,
     "Keyframe",
     "Strip containing keyframes on F-Curves"},
    {0, nullptr, 0, nullptr, nullptr},
};

#ifdef RNA_RUNTIME

#  include "ANIM_animation.hh"

#  include "DEG_depsgraph.hh"

#  include <fmt/format.h>

using namespace blender;

static animrig::Animation &rna_animation(const PointerRNA *ptr)
{
  return reinterpret_cast<Animation *>(ptr->owner_id)->wrap();
}

static animrig::Output &rna_data_output(const PointerRNA *ptr)
{
  return reinterpret_cast<AnimationOutput *>(ptr->data)->wrap();
}

static animrig::Layer &rna_data_layer(const PointerRNA *ptr)
{
  return reinterpret_cast<AnimationLayer *>(ptr->data)->wrap();
}

static animrig::Strip &rna_data_strip(const PointerRNA *ptr)
{
  return reinterpret_cast<AnimationStrip *>(ptr->data)->wrap();
}

static void rna_Animation_tag_animupdate(Main *, Scene *, PointerRNA *ptr)
{
  animrig::Animation &anim = rna_animation(ptr);
  DEG_id_tag_update(&anim.id, ID_RECALC_ANIMATION);
}

static animrig::KeyframeStrip &rna_data_keyframe_strip(const PointerRNA *ptr)
{
#  ifndef NDEBUG
  animrig::Strip &base_strip = reinterpret_cast<AnimationStrip *>(ptr->data)->wrap();
  BLI_assert_msg(base_strip.type == ANIM_STRIP_TYPE_KEYFRAME,
                 "this strip is not a keyframe strip");
#  endif
  return reinterpret_cast<KeyframeAnimationStrip *>(ptr->data)->wrap();
}

static animrig::ChannelsForOutput &rna_data_chans_for_out(const PointerRNA *ptr)
{
  return reinterpret_cast<AnimationChannelsForOutput *>(ptr->data)->wrap();
}

template<typename T>
static void rna_iterator_array_begin(CollectionPropertyIterator *iter, Span<T *> items)
{
  rna_iterator_array_begin(iter, (void *)items.data(), sizeof(T *), items.size(), 0, nullptr);
}

template<typename T>
static void rna_iterator_array_begin(CollectionPropertyIterator *iter, MutableSpan<T *> items)
{
  rna_iterator_array_begin(iter, (void *)items.data(), sizeof(T *), items.size(), 0, nullptr);
}

static AnimationOutput *rna_Animation_outputs_new(Animation *anim_id,
                                                  ReportList *reports,
                                                  ID *animated_id)
{
  if (animated_id == nullptr) {
    BKE_report(reports,
               RPT_ERROR,
               "An output without animated ID cannot be created at the moment; if you need it, "
               "please file a bug report");
    return nullptr;
  }

  animrig::Animation &anim = anim_id->wrap();
  animrig::Output *output = anim.output_add();
  output->assign_id(animated_id);
  // TODO: notifiers.
  return output;
}

static void rna_iterator_animation_layers_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  animrig::Animation &anim = rna_animation(ptr);
  rna_iterator_array_begin(iter, anim.layers());
}

static int rna_iterator_animation_layers_length(PointerRNA *ptr)
{
  animrig::Animation anim = rna_animation(ptr);
  return anim.layers().size();
}

static AnimationLayer *rna_Animation_layers_new(Animation *anim, const char *name)
{
  AnimationLayer *layer = anim->wrap().layer_add(name);
  // TODO: notifiers.
  return layer;
}

void rna_Animation_layers_remove(Animation *dna_animation,
                                 bContext *C,
                                 ReportList *reports,
                                 AnimationLayer *dna_layer)
{
  animrig::Animation &anim = dna_animation->wrap();
  animrig::Layer &layer = dna_layer->wrap();
  if (!anim.layer_remove(layer)) {
    BKE_report(reports, RPT_ERROR, "this layer does not belong to this animation");
    return;
  }

  WM_event_add_notifier(C, NC_ANIMATION | ND_ANIMCHAN, nullptr);
  DEG_id_tag_update(&anim.id, ID_RECALC_ANIMATION);
}

static void rna_iterator_animation_outputs_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  animrig::Animation &anim = rna_animation(ptr);
  rna_iterator_array_begin(iter, anim.outputs());
}

static int rna_iterator_animation_outputs_length(PointerRNA *ptr)
{
  animrig::Animation &anim = rna_animation(ptr);
  return anim.outputs().size();
}

static std::optional<std::string> rna_AnimationOutput_path(const PointerRNA *ptr)
{
  animrig::Animation &anim = rna_animation(ptr);
  animrig::Output &output_to_find = rna_data_output(ptr);

  Span<animrig::Output *> outputs = anim.outputs();
  for (int i = 0; i < outputs.size(); ++i) {
    animrig::Output &output = *outputs[i];
    if (&output != &output_to_find) {
      continue;
    }

    return fmt::format("outputs[{}]", i);
  }
  return std::nullopt;
}

static void rna_AnimationOutput_name_set(PointerRNA *ptr, const char *name)
{
  animrig::Animation &anim = rna_animation(ptr);
  animrig::Output &output = rna_data_output(ptr);

  anim.output_name_set(output, name);
}

static std::optional<std::string> rna_AnimationLayer_path(const PointerRNA *ptr)
{
  animrig::Layer &layer = rna_data_layer(ptr);

  char name_esc[sizeof(layer.name) * 2];
  BLI_str_escape(name_esc, layer.name, sizeof(name_esc));
  return fmt::format("layers[\"%s\"]", name_esc);
}

static void rna_iterator_animationlayer_strips_begin(CollectionPropertyIterator *iter,
                                                     PointerRNA *ptr)
{
  animrig::Layer &layer = rna_data_layer(ptr);
  rna_iterator_array_begin(iter, layer.strips());
}

static int rna_iterator_animationlayer_strips_length(PointerRNA *ptr)
{
  animrig::Layer &layer = rna_data_layer(ptr);
  return layer.strips().size();
}

AnimationStrip *rna_AnimationStrips_new(AnimationLayer *dna_layer, const int type)
{
  const eAnimationStrip_type strip_type = eAnimationStrip_type(type);

  animrig::Layer &layer = dna_layer->wrap();
  animrig::Strip *strip = layer.strip_add(strip_type);

  // TODO: notifiers.
  return strip;
}

void rna_AnimationStrips_remove(ID *animation_id,
                                AnimationLayer *dna_layer,
                                bContext *C,
                                ReportList *reports,
                                AnimationStrip *dna_strip)
{
  animrig::Layer &layer = dna_layer->wrap();
  animrig::Strip &strip = dna_strip->wrap();
  if (!layer.strip_remove(strip)) {
    BKE_report(reports, RPT_ERROR, "this strip does not belong to this layer");
    return;
  }

  WM_event_add_notifier(C, NC_ANIMATION | ND_ANIMCHAN, nullptr);
  DEG_id_tag_update(animation_id, ID_RECALC_ANIMATION);
}

static StructRNA *rna_AnimationStrip_refine(PointerRNA *ptr)
{
  animrig::Strip &strip = rna_data_strip(ptr);
  switch (strip.type) {
    case ANIM_STRIP_TYPE_KEYFRAME:
      return &RNA_KeyframeAnimationStrip;
  }
  return &RNA_UnknownType;
}

static std::optional<std::string> rna_AnimationStrip_path(const PointerRNA *ptr)
{
  animrig::Animation &anim = rna_animation(ptr);
  animrig::Strip &strip_to_find = rna_data_strip(ptr);

  for (animrig::Layer *layer : anim.layers()) {
    Span<animrig::Strip *> strips = layer->strips();
    for (int i = 0; i < strips.size(); ++i) {
      animrig::Strip &strip = *strips[i];
      if (&strip != &strip_to_find) {
        continue;
      }

      PointerRNA layer_ptr = RNA_pointer_create(&anim.id, &RNA_AnimationLayer, layer);
      const std::optional<std::string> layer_path = rna_AnimationLayer_path(&layer_ptr);
      BLI_assert_msg(layer_path, "Every animation layer should have a valid RNA path.");
      const std::string strip_path = fmt::format("{}.strips[{}]", *layer_path, i);
      return strip_path;
    }
  }

  return std::nullopt;
}

static void rna_iterator_keyframestrip_chans_for_out_begin(CollectionPropertyIterator *iter,
                                                           PointerRNA *ptr)
{
  animrig::KeyframeStrip &key_strip = rna_data_keyframe_strip(ptr);
  rna_iterator_array_begin(iter, key_strip.channels_for_output());
}

static int rna_iterator_keyframestrip_chans_for_out_length(PointerRNA *ptr)
{
  animrig::KeyframeStrip &key_strip = rna_data_keyframe_strip(ptr);
  return key_strip.channels_for_output().size();
}

static FCurve *rna_KeyframeAnimationStrip_key_insert(KeyframeAnimationStrip *strip,
                                                     ReportList *reports,
                                                     AnimationOutput *output,
                                                     const char *rna_path,
                                                     const int array_index,
                                                     const float value,
                                                     const float time)
{
  if (output == nullptr) {
    BKE_report(reports, RPT_ERROR, "output cannot be None");
    return nullptr;
  }

  animrig::KeyframeStrip &key_strip = strip->wrap();
  const animrig::Output &out = output->wrap();
  const animrig::KeyframeSettings settings = animrig::get_keyframe_settings(true);

  FCurve *fcurve = key_strip.keyframe_insert(out, rna_path, array_index, {time, value}, settings);
  return fcurve;
}

static void rna_iterator_ChansForOut_fcurves_begin(CollectionPropertyIterator *iter,
                                                   PointerRNA *ptr)
{
  animrig::ChannelsForOutput &chans_for_out = rna_data_chans_for_out(ptr);
  rna_iterator_array_begin(iter, chans_for_out.fcurves());
}

static int rna_iterator_ChansForOut_fcurves_length(PointerRNA *ptr)
{
  animrig::ChannelsForOutput &chans_for_out = rna_data_chans_for_out(ptr);
  return chans_for_out.fcurves().size();
}

static AnimationChannelsForOutput *rna_KeyframeAnimationStrip_channels(
    KeyframeAnimationStrip *self, const int output_index)
{
  animrig::KeyframeStrip &key_strip = self->wrap();
  return key_strip.chans_for_out(output_index);
}

#else

static void rna_def_animation_outputs(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "AnimationOutputs");
  srna = RNA_def_struct(brna, "AnimationOutputs", nullptr);
  RNA_def_struct_sdna(srna, "Animation");
  RNA_def_struct_ui_text(srna, "Animation Outputs", "Collection of animation outputs");

  /* Animation.outputs.new(...) */
  func = RNA_def_function(srna, "new", "rna_Animation_outputs_new");
  RNA_def_function_ui_description(func, "Add an output to the animation");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(
      func, "animated_id", "ID", "Data-Block", "Data-block that will be animated by this output");

  RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);
  parm = RNA_def_pointer(func, "output", "AnimationOutput", "", "Newly created animation output");
  RNA_def_function_return(func, parm);
}

static void rna_def_animation_layers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "AnimationLayers");
  srna = RNA_def_struct(brna, "AnimationLayers", nullptr);
  RNA_def_struct_sdna(srna, "Animation");
  RNA_def_struct_ui_text(srna, "Animation Layers", "Collection of animation layers");

  /* Animation.layers.new(...) */
  func = RNA_def_function(srna, "new", "rna_Animation_layers_new");
  RNA_def_function_ui_description(func, "Add a layer to the animation");
  parm = RNA_def_string(func,
                        "name",
                        nullptr,
                        sizeof(AnimationLayer::name) - 1,
                        "Name",
                        "Name of the layer, unique within the Animation data-block");
  RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);
  parm = RNA_def_pointer(func, "layer", "AnimationLayer", "", "Newly created animation layer");
  RNA_def_function_return(func, parm);

  /* Animation.layers.remove(layer) */
  func = RNA_def_function(srna, "remove", "rna_Animation_layers_remove");
  RNA_def_function_flag(func, FUNC_USE_CONTEXT | FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove the layer from the animation");
  parm = RNA_def_pointer(
      func, "anim_layer", "AnimationLayer", "Animation Layer", "The layer to remove");
  RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);
}

static void rna_def_animation(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "Animation", "ID");
  RNA_def_struct_sdna(srna, "Animation");
  RNA_def_struct_ui_text(srna, "Animation", "A collection of animation layers");
  RNA_def_struct_ui_icon(srna, ICON_ACTION);

  prop = RNA_def_property(srna, "last_output_stable_index", PROP_INT, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  /* Collection properties .*/
  prop = RNA_def_property(srna, "outputs", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "AnimationOutput");
  RNA_def_property_collection_funcs(prop,
                                    "rna_iterator_animation_outputs_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_dereference_get",
                                    "rna_iterator_animation_outputs_length",
                                    nullptr, /* TODO */
                                    nullptr, /* TODO */
                                    nullptr);
  RNA_def_property_ui_text(prop, "Outputs", "The list of data-blocks animated by this Animation");
  rna_def_animation_outputs(brna, prop);

  prop = RNA_def_property(srna, "layers", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "AnimationLayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_iterator_animation_layers_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_dereference_get",
                                    "rna_iterator_animation_layers_length",
                                    nullptr, /* TODO */
                                    nullptr, /* TODO */
                                    nullptr);
  RNA_def_property_ui_text(prop, "Layers", "The list of layers that make up this Animation");
  rna_def_animation_layers(brna, prop);
}

static void rna_def_animation_output(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "AnimationOutput", nullptr);
  RNA_def_struct_path_func(srna, "rna_AnimationOutput_path");
  RNA_def_struct_ui_text(srna,
                         "Animation Output",
                         "Reference to a data-block that will be animated by this Animation");

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_struct_name_property(srna, prop);
  RNA_def_property_string_funcs(prop, nullptr, nullptr, "rna_AnimationOutput_name_set");
  RNA_def_property_update(prop, NC_ANIMATION | ND_ANIMCHAN, nullptr);

  prop = RNA_def_property(srna, "stable_index", PROP_INT, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
}

static void rna_def_animationlayer_strips(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "AnimationStrips");
  srna = RNA_def_struct(brna, "AnimationStrips", nullptr);
  RNA_def_struct_sdna(srna, "AnimationLayer");
  RNA_def_struct_ui_text(srna, "Animation Strips", "Collection of animation strips");

  /* Layer.strips.new(type='...') */
  func = RNA_def_function(srna, "new", "rna_AnimationStrips_new");
  RNA_def_function_ui_description(func, "Add a new infinite strip to the layer");
  parm = RNA_def_enum(func,
                      "type",
                      rna_enum_strip_type_items,
                      ANIM_STRIP_TYPE_KEYFRAME,
                      "Type",
                      "The type of strip to create");
  /* Return value. */
  parm = RNA_def_pointer(func, "strip", "AnimationStrip", "", "Newly created animation strip");
  RNA_def_function_return(func, parm);

  /* Layer.strips.remove(strip) */
  func = RNA_def_function(srna, "remove", "rna_AnimationStrips_remove");
  RNA_def_function_flag(func, FUNC_USE_SELF_ID | FUNC_USE_CONTEXT | FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove the strip from the animation layer");
  parm = RNA_def_pointer(
      func, "anim_strip", "AnimationStrip", "Animation Strip", "The strip to remove");
  RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);
}

static void rna_def_animation_layer(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "AnimationLayer", nullptr);
  RNA_def_struct_ui_text(srna, "Animation Layer", "");
  RNA_def_struct_path_func(srna, "rna_AnimationLayer_path");

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_struct_name_property(srna, prop);

  prop = RNA_def_property(srna, "influence", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_ui_text(
      prop,
      "Influence",
      "How much of this layer is used when blending into the output of lower layers");
  RNA_def_property_ui_range(prop, 0.0, 1.0, 3, 2);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_update(prop, NC_ANIMATION | ND_ANIMCHAN, "rna_Animation_tag_animupdate");

  prop = RNA_def_property(srna, "mix_mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_ui_text(
      prop, "Mix Mode", "How animation of this layer is blended into the output of lower layers");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_enum_items(prop, rna_enum_layer_mix_mode_items);
  RNA_def_property_update(prop, NC_ANIMATION | ND_ANIMCHAN, "rna_Animation_tag_animupdate");

  /* Collection properties .*/
  prop = RNA_def_property(srna, "strips", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "AnimationStrip");
  RNA_def_property_collection_funcs(prop,
                                    "rna_iterator_animationlayer_strips_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_dereference_get",
                                    "rna_iterator_animationlayer_strips_length",
                                    nullptr, /* TODO */
                                    nullptr, /* TODO */
                                    nullptr);
  RNA_def_property_ui_text(prop, "Strips", "The list of strips that are on this animation layer");

  rna_def_animationlayer_strips(brna, prop);
}

static void rna_def_keyframestrip_channels_for_outputs(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  RNA_def_property_srna(cprop, "AnimationChannelsForOutputs");
  srna = RNA_def_struct(brna, "AnimationChannelsForOutputs", nullptr);
  RNA_def_struct_sdna(srna, "KeyframeAnimationStrip");
  RNA_def_struct_ui_text(srna,
                         "Animation Channels for Outputs",
                         "For each animation output, a list of animation channels");
}

static void rna_def_animation_keyframe_strip(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "KeyframeAnimationStrip", "AnimationStrip");
  RNA_def_struct_ui_text(
      srna, "Keyframe Animation Strip", "Strip with a set of FCurves for each animation output");

  prop = RNA_def_property(srna, "channels_for_output", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "AnimationChannelsForOutput");
  RNA_def_property_collection_funcs(prop,
                                    "rna_iterator_keyframestrip_chans_for_out_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_dereference_get",
                                    "rna_iterator_keyframestrip_chans_for_out_length",
                                    nullptr, /* TODO */
                                    nullptr, /* TODO */
                                    nullptr);
  rna_def_keyframestrip_channels_for_outputs(brna, prop);

  {
    FunctionRNA *func;
    PropertyRNA *parm;

    /* KeyframeStrip.channels(...). */
    func = RNA_def_function(srna, "channels", "rna_KeyframeAnimationStrip_channels");
    parm = RNA_def_int(func,
                       "output_index",
                       0,
                       0,
                       INT_MAX,
                       "Output Index",
                       "Number that identifies a specific animation output",
                       0,
                       INT_MAX);
    RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);
    parm = RNA_def_pointer(func, "channels", "AnimationChannelsForOutput", "Channels", "");
    RNA_def_function_return(func, parm);

    /* KeyframeStrip.key_insert(...). */

    func = RNA_def_function(srna, "key_insert", "rna_KeyframeAnimationStrip_key_insert");
    RNA_def_function_flag(func, FUNC_USE_REPORTS);
    parm = RNA_def_pointer(func,
                           "output",
                           "AnimationOutput",
                           "Output",
                           "The output that identifies which 'thing' should be keyed");
    RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);

    parm = RNA_def_string(func, "data_path", nullptr, 0, "Data Path", "F-Curve data path");
    RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);

    parm = RNA_def_int(
        func,
        "array_index",
        -1,
        -INT_MAX,
        INT_MAX,
        "Array Index",
        "Index of the animated array element, or -1 if the property is not an array",
        -1,
        4);
    RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);

    parm = RNA_def_float(func,
                         "value",
                         0.0,
                         -FLT_MAX,
                         FLT_MAX,
                         "Value to key",
                         "Value of the animated property",
                         -FLT_MAX,
                         FLT_MAX);
    RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);

    parm = RNA_def_float(func,
                         "time",
                         0.0,
                         -FLT_MAX,
                         FLT_MAX,
                         "Time of the key",
                         "Time, in frames, of the key",
                         -FLT_MAX,
                         FLT_MAX);
    RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);

    parm = RNA_def_pointer(func, "fcurve", "FCurve", "", "The FCurve this key was inserted on");
    RNA_def_function_return(func, parm);
  }
}

static void rna_def_animation_strip(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "AnimationStrip", nullptr);
  RNA_def_struct_ui_text(srna, "Animation Strip", "");
  RNA_def_struct_path_func(srna, "rna_AnimationStrip_path");
  RNA_def_struct_refine_func(srna, "rna_AnimationStrip_refine");

  static const EnumPropertyItem prop_type_items[] = {
      {ANIM_STRIP_TYPE_KEYFRAME,
       "KEYFRAME",
       0,
       "Keyframe",
       "Strip with a set of FCurves for each animation output"},
      {0, nullptr, 0, nullptr, nullptr},
  };

  prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, prop_type_items);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "frame_start", PROP_FLOAT, PROP_NONE);
  RNA_def_property_ui_text(prop, "Frame Start", "");
  RNA_def_property_update(prop, NC_ANIMATION | ND_ANIMCHAN, "rna_Animation_tag_animupdate");

  prop = RNA_def_property(srna, "frame_end", PROP_FLOAT, PROP_NONE);
  RNA_def_property_ui_text(prop, "End", "");
  RNA_def_property_update(prop, NC_ANIMATION | ND_ANIMCHAN, "rna_Animation_tag_animupdate");

  prop = RNA_def_property(srna, "frame_offset", PROP_FLOAT, PROP_NONE);
  RNA_def_property_ui_text(prop, "Offset", "");
  RNA_def_property_update(prop, NC_ANIMATION | ND_ANIMCHAN, "rna_Animation_tag_animupdate");

  /* Define Strip subclasses. */
  rna_def_animation_keyframe_strip(brna);
}

static void rna_def_chans_for_out_fcurves(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  RNA_def_property_srna(cprop, "AnimationChannelsForOutputFCurves");
  srna = RNA_def_struct(brna, "AnimationChannelsForOutputFCurves", nullptr);
  RNA_def_struct_sdna(srna, "bAnimationChannelsForOutput");
  RNA_def_struct_ui_text(
      srna, "F-Curves", "Collection of F-Curves for a specific animation output");

  // /* AnimationChannelsForOutput.fcurves.new(...) */
  // func = RNA_def_function(srna, "new", "rna_AnimationChannelsForOutput_fcurve_new");
  // RNA_def_function_ui_description(func, "Add an F-Curve to the action");
  // RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_MAIN);
  // parm = RNA_def_string(func, "data_path", nullptr, 0, "Data Path", "F-Curve data path to use");
  // RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);
  // RNA_def_int(func, "index", 0, 0, INT_MAX, "Index", "Array index", 0, INT_MAX);
  // RNA_def_string(
  //     func, "action_group", nullptr, 0, "AnimationChannelsForOutput Group", "Acton group to add
  //     this F-Curve into");

  // parm = RNA_def_pointer(func, "fcurve", "FCurve", "", "Newly created F-Curve");
  // RNA_def_function_return(func, parm);

  /* AnimationChannelsForOutput.fcurves.find(...) */
  // func = RNA_def_function(srna, "find", "rna_AnimationChannelsForOutput_fcurve_find");
  // RNA_def_function_ui_description(
  //     func,
  //     "Find an F-Curve. Note that this function performs a linear scan "
  //     "of all F-Curves in the action.");
  // RNA_def_function_flag(func, FUNC_USE_REPORTS);
  // parm = RNA_def_string(func, "data_path", nullptr, 0, "Data Path", "F-Curve data path");
  // RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);
  // RNA_def_int(func, "index", 0, 0, INT_MAX, "Index", "Array index", 0, INT_MAX);
  // parm = RNA_def_pointer(
  //     func, "fcurve", "FCurve", "", "The found F-Curve, or None if it doesn't exist");
  // RNA_def_function_return(func, parm);

  /* AnimationChannelsForOutput.fcurves.remove(...) */
  // func = RNA_def_function(srna, "remove", "rna_AnimationChannelsForOutput_fcurve_remove");
  // RNA_def_function_ui_description(func, "Remove F-Curve");
  // RNA_def_function_flag(func, FUNC_USE_REPORTS);
  // parm = RNA_def_pointer(func, "fcurve", "FCurve", "", "F-Curve to remove");
  // RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  // RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, ParameterFlag(0));

  /* AnimationChannelsForOutput.fcurves.clear() */
  // func = RNA_def_function(srna, "clear", "rna_AnimationChannelsForOutput_fcurve_clear");
  // RNA_def_function_ui_description(func, "Remove all F-Curves");
}

static void rna_def_animation_channels_for_output(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "AnimationChannelsForOutput", nullptr);
  RNA_def_struct_ui_text(srna, "Animation Channels for Output", "");

  prop = RNA_def_property(srna, "output_stable_index", PROP_INT, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "fcurves", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_funcs(prop,
                                    "rna_iterator_ChansForOut_fcurves_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_dereference_get",
                                    "rna_iterator_ChansForOut_fcurves_length",
                                    nullptr, /* TODO */
                                    nullptr, /* TODO */
                                    nullptr);
  RNA_def_property_struct_type(prop, "FCurve");
  RNA_def_property_ui_text(prop, "F-Curves", "The individual F-Curves that animate the output");
  rna_def_chans_for_out_fcurves(brna, prop);
}

void RNA_def_animation_id(BlenderRNA *brna)
{
  rna_def_animation(brna);
  rna_def_animation_output(brna);
  rna_def_animation_layer(brna);
  rna_def_animation_strip(brna);
  rna_def_animation_channels_for_output(brna);
}

#endif
