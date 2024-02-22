/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 *
 * This file only contains the memory management functions for the Animation
 * data-block. For all other functionality, see `source/blender/animrig`.
 */

#pragma once

struct Animation;
struct AnimationChannelsForOutput;
struct Main;

Animation *BKE_animation_add(Main *bmain, const char name[]);

/** Free any data used by this channels-for-output (does not free the channels-for-output itself).
 */
void BKE_anim_channels_for_output_free_data(AnimationChannelsForOutput *channels);
