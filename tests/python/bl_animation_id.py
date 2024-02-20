# SPDX-FileCopyrightText: 2020-2023 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

import unittest
import sys

import bpy

"""
blender -b --factory-startup --python tests/python/bl_animation_id.py --
"""


class AnimationIDAssignmentTest(unittest.TestCase):
    """Test assigning animations & check reference counts."""

    def test_animation_id_assignment(self):
        # Create new animation datablock.
        anim = bpy.data.animations.new('TestAnim')
        self.assertEqual(0, anim.users)

        # Assign the animation to the cube,
        cube = bpy.data.objects['Cube']
        cube_adt = cube.animation_data_create()
        cube_adt.animation = anim
        self.assertEqual(1, anim.users)

        # Assign the animation to the camera as well.
        camera = bpy.data.objects['Camera']
        camera_adt = camera.animation_data_create()
        camera_adt.animation = anim
        self.assertEqual(2, anim.users)

        # Unassigning should decrement the user count.
        cube_adt.animation = None
        self.assertEqual(1, anim.users)

        # Deleting the camera should also decrement the user count.
        bpy.data.objects.remove(camera)
        self.assertEqual(0, anim.users)


class DataPathTest(unittest.TestCase):
    def setUp(self):
        anims = bpy.data.animations
        while anims:
            anims.remove(anims[0])

    def test_repr(self):
        anim = bpy.data.animations.new('TestAnim')

        layer = anim.layers.new(name="Layer")
        self.assertEqual("bpy.data.animations['TestAnim'].layers[\"Layer\"]", repr(layer))

        strip = layer.strips.new(type='KEYFRAME')
        self.assertEqual("bpy.data.animations['TestAnim'].layers[\"Layer\"].strips[0]", repr(strip))


def main():
    global args
    import argparse

    if '--' in sys.argv:
        argv = [sys.argv[0]] + sys.argv[sys.argv.index('--') + 1:]
    else:
        argv = sys.argv

    parser = argparse.ArgumentParser()
    args, remaining = parser.parse_known_args(argv)

    unittest.main(argv=remaining)


if __name__ == "__main__":
    main()
