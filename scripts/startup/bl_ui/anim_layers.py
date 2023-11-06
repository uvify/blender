# SPDX-FileCopyrightText: 2023 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""NOTE: this is temporary UI code to show animation layers.

It is not meant for any particular use, just to have *something* in the UI.
"""

import bpy
from bpy.types import Context, Panel


class VIEW3D_PT_animation_layers(Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "Animation"
    bl_label = "Layers"

    @classmethod
    def poll(cls, context: Context) -> bool:
        return context.object and context.object.animation_data

    def draw(self, context: Context) -> None:
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        adt = context.object.animation_data
        if not adt:
            return

        col = layout.column()
        col.prop(adt, 'animation')
        col.prop(adt, 'animation_output_index', text="Output")

        layout.separator()

        anim = adt.animation
        if not anim:
            layout.label(text="No layers")
            return

        for layer_idx, layer in reversed(list(enumerate(anim.layers))):
            layerbox = layout.box()
            col = layerbox.column(align=True)
            col.prop(layer, "name", text=f"Layer {layer_idx+1}:")
            col.prop(layer, "influence")

            for strip_idx, strip in enumerate(layer.strips):
                stripcol = col.column(align=True)
                stripcol.label(text=f"Strip {strip_idx+1}:")
                stripcol.prop(strip, "frame_start")
                stripcol.prop(strip, "frame_end")
                stripcol.prop(strip, "frame_offset")


classes = (
    VIEW3D_PT_animation_layers,
)

if __name__ == "__main__":  # only for live edit.
    register, _ = bpy.utils.register_classes_factory(classes)
    register()
