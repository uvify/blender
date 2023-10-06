/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 */

#pragma once

struct Animation;
struct Main;

struct Animation *BKE_animation_add(struct Main *bmain, const char name[]);

/** Free (or release) any data used by this animation (does not free the animation itself). */
void BKE_animation_free_data(struct Animation *animation);
