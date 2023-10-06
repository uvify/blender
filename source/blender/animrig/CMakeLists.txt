# SPDX-FileCopyrightText: 2023 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(INC
  PUBLIC .
  intern

  ../blenkernel
  ../blentranslation
  ../editors/include

  # Just for being able to include 'ED_keyframing.hh', which includes 'RNA_types.hh'.
  #
  # The functionality in 'ED_keyframing.hh' that is actually used in this module
  # should be moved into this module instead, after which this path can be
  # removed.
  ../makesrna
)

set(INC_SYS
)

set(SRC
  intern/animation.cc
  intern/bone_collections.cc
  intern/bonecolor.cc

  ANIM_animation.hh
  ANIM_bone_collections.h
  ANIM_bone_collections.hh
  ANIM_bonecolor.hh
)

set(LIB
  bf_blenkernel
  bf::blenlib
  bf::dna
  PRIVATE bf_editor_interface
  PRIVATE bf::intern::guardedalloc
  PRIVATE bf::intern::atomic
)


blender_add_lib(bf_animrig "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
add_library(bf::animrig ALIAS bf_animrig)

if(WITH_GTESTS)
  set(TEST_INC
  )
  set(TEST_SRC
    intern/animation_test.cc
    intern/bone_collections_test.cc
  )
  set(TEST_LIB
    PRIVATE bf::animrig
  )
  include(GTestTesting)
  blender_add_test_lib(bf_animrig_tests "${TEST_SRC}" "${INC};${TEST_INC}" "${INC_SYS}" "${LIB};${TEST_LIB}")
endif()
