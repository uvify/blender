/* SPDX-FileCopyrightText: 2023 Blender Developers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_anim_types.h"

#include "BLI_string_utf8.h"

#include "MEM_guardedalloc.h"

#include "atomic_ops.h"

namespace blender::animrig {

AnimationOutput *animation_add_output(Animation *anim, ID *animated_id)
{
  AnimationOutput *output = MEM_new<AnimationOutput>(__func__);

  output->idtype = GS(animated_id->name);
  output->stable_index = atomic_add_and_fetch_int32(&anim->last_output_stable_index, 1);
  STRNCPY_UTF8(output->fallback, animated_id->name);

  // TODO: turn this into an actually nice function.
  output->runtime.id = MEM_new<ID *>(__func__);
  output->runtime.num_ids = 1;
  *(output->runtime.id) = animated_id;

  return output;
}

}  // namespace blender::animrig
