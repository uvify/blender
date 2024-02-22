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
struct AnimationLayer;
struct AnimationStrip;
struct Main;

Animation *BKE_animation_add(Main *bmain, const char name[]);

/** Free any data used by this animation layer (does not free the layer itself). */
void BKE_animation_layer_free_data(AnimationLayer *layer);

/** Free any data used by this animation strip (does not free the strip itself). */
void BKE_animation_strip_free_data(AnimationStrip *strip);
