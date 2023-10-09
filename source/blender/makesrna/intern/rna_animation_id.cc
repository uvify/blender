/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup RNA
 */

#include <cstdlib>

#include "DNA_anim_types.h"

#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.hh"
#include "RNA_define.hh"
#include "RNA_enum_types.hh"

#include "rna_internal.h"

#include "WM_types.hh"

#ifdef RNA_RUNTIME

// #  include "BKE_anim_data.h"
// #  include "BKE_animsys.h"
// #  include "BKE_fcurve.h"

// #  include "DEG_depsgraph.hh"
// #  include "DEG_depsgraph_build.hh"

// #  include "DNA_object_types.h"

// #  include "ED_anim_api.hh"

// #  include "WM_api.hh"

#  include "ANIM_animation.hh"

using namespace blender;

static animrig::Animation &rna_animation(const PointerRNA *ptr)
{
  return reinterpret_cast<Animation *>(ptr->owner_id)->wrap();
}

static animrig::Layer &rna_data_layer(const PointerRNA *ptr)
{
  return reinterpret_cast<AnimationLayer *>(ptr->data)->wrap();
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
  animrig::Output *output = anim.output_add(animated_id);
  // TODO: notifiers.
  return output;
}

static void rna_iterator_animation_layers_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  animrig::Animation &anim = rna_animation(ptr);
  Span<animrig::Layer *> layers = anim.layers();

  rna_iterator_array_begin(
      iter, (void *)layers.data(), sizeof(AnimationLayer *), layers.size(), 0, nullptr);
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

static void rna_iterator_animation_outputs_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  animrig::Animation &anim = rna_animation(ptr);
  Span<animrig::Output *> outputs = anim.outputs();

  rna_iterator_array_begin(
      iter, (void *)outputs.data(), sizeof(AnimationOutput *), outputs.size(), 0, nullptr);
}

static int rna_iterator_animation_outputs_length(PointerRNA *ptr)
{
  animrig::Animation &anim = rna_animation(ptr);
  return anim.outputs().size();
}

static void rna_iterator_animationlayer_strips_begin(CollectionPropertyIterator *iter,
                                                     PointerRNA *ptr)
{
  animrig::Layer &layer = rna_data_layer(ptr);
  Span<animrig::Strip *> strips = layer.strips();

  rna_iterator_array_begin(
      iter, (void *)strips.data(), sizeof(AnimationStrip *), strips.size(), 0, nullptr);
}

static int rna_iterator_animationlayer_strips_length(PointerRNA *ptr)
{
  animrig::Layer &layer = rna_data_layer(ptr);
  return layer.strips().size();
}

static FCurve *rna_AnimationStrip_keyframe_insert(AnimationStrip *strip,
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

  FCurve *fcurve = animrig::keyframe_insert(
      &strip->wrap(), output, rna_path, array_index, value, time, BEZT_KEYTYPE_KEYFRAME);
  return fcurve;
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
  RNA_def_property_int_sdna(prop, nullptr, "last_output_stable_index");
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
  RNA_def_struct_ui_text(srna,
                         "Animation Output",
                         "Reference to a data-block that will be animated by this Animation");

  prop = RNA_def_property(srna, "stable_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, nullptr, "stable_index");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
}

static void rna_def_animationlayer_strips(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  // FunctionRNA *func;
  // PropertyRNA *parm;

  RNA_def_property_srna(cprop, "AnimationStrips");
  srna = RNA_def_struct(brna, "AnimationStrips", nullptr);
  RNA_def_struct_sdna(srna, "AnimationLayer");
  RNA_def_struct_ui_text(srna, "Animation Strips", "Collection of animation strips");
}

static void rna_def_animation_layer(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "AnimationLayer", nullptr);
  RNA_def_struct_ui_text(srna, "Animation Layer", "");

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_struct_name_property(srna, prop);

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

static void rna_def_animation_strip(BlenderRNA *brna)
{
  StructRNA *srna;
  // PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  srna = RNA_def_struct(brna, "AnimationStrip", nullptr);
  RNA_def_struct_ui_text(srna, "Animation Strip", "");

  /* Strip.keyframe_insert(...). */
  func = RNA_def_function(srna, "keyframe_insert", "rna_AnimationStrip_keyframe_insert");
  // RNA_def_function_ui_description(func, "");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func,
                         "output",
                         "AnimationOutput",
                         "Output",
                         "The output that identifies which 'thing' should be keyed");
  RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);

  parm = RNA_def_string(func, "data_path", nullptr, 0, "Data Path", "F-Curve data path");
  RNA_def_parameter_flags(parm, PropertyFlag(0), PARM_REQUIRED);

  parm = RNA_def_int(func,
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

void RNA_def_animation_id(BlenderRNA *brna)
{
  rna_def_animation(brna);
  rna_def_animation_output(brna);
  rna_def_animation_layer(brna);
  rna_def_animation_strip(brna);
}

#endif
