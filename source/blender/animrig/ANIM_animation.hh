/* SPDX-FileCopyrightText: 2023 Blender Developers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup animrig
 *
 * \brief Animation data-block functionality.
 */

#ifndef __cplusplus
#  error This is a C++ header.
#endif

struct Animation;
struct AnimationOutput;
struct ID;

namespace blender::animrig {

AnimationOutput *animation_add_output(Animation *anim, ID *animated_id);

}
