# SPDX-FileCopyrightText: 2023 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""NOTE: this is temporary UI code to show animation layers.

It is not meant for any particular use, just to have *something* in the UI.
"""

import contextlib
import threading

import bpy
from bpy.types import Context, Panel, Animation, WindowManager
from bpy.props import PointerProperty


class VIEW3D_PT_animation_layers(Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "Animation"
    bl_label = "Baklava"

    @classmethod
    def poll(cls, context: Context) -> bool:
        return context.object

    def draw(self, context: Context) -> None:
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        # FIXME: this should be done in response to a messagebus callback, notifier, whatnot.
        adt = context.object.animation_data
        with _wm_selected_animation_lock:
            if adt:
                context.window_manager.selected_animation = adt.animation
            else:
                context.window_manager.selected_animation = None

        col = layout.column()
        # This has to go via an auxillary property, as assigning an Animation
        # data-block should be possible even when `context.object.animation_data`
        # is `None`, and thus its `animation` property does not exist.
        col.template_ID(context.window_manager, 'selected_animation')

        col = layout.column(align=True)
        anim = adt and adt.animation
        if anim:
            col.prop(adt, 'animation_output_index', text="Output")
            out = [o for o in anim.outputs if o.stable_index == adt.animation_output_index]
            if out:
                col.prop(out[0], 'name', text="Anim Output Name")
            else:
                col.label(text="AN Output Name: -")
        if adt:
            col.prop(adt, 'animation_output_name', text="ADT Output Name")
        else:
            col.label(text="ADT Output Name: -")

        layout.separator()

        if not anim:
            layout.label(text="No layers")
            return

        for layer_idx, layer in reversed(list(enumerate(anim.layers))):
            layerbox = layout.box()
            col = layerbox.column(align=True)
            col.prop(layer, "name", text=f"Layer {layer_idx+1}:")
            col.prop(layer, "influence")
            col.prop(layer, "mix_mode")

            # for strip_idx, strip in enumerate(layer.strips):
            #     stripcol = col.column(align=True)
            #     stripcol.label(text=f"Strip {strip_idx+1}:")
            #     stripcol.prop(strip, "frame_start")
            #     stripcol.prop(strip, "frame_end")
            #     stripcol.prop(strip, "frame_offset")


classes = (
    VIEW3D_PT_animation_layers,
)

_wm_selected_animation_lock = threading.Lock()


def _wm_selected_animation_update(self: WindowManager, context: Context) -> None:
    # Avoid responding to changes written by the panel above.
    lock_ok = _wm_selected_animation_lock.acquire(blocking=False)
    if not lock_ok:
        return
    try:
        if self.selected_animation is None and context.object.animation_data is None:
            return

        adt = context.object.animation_data_create()
        if adt.animation == self.selected_animation:
            # Avoid writing to the property when the new value hasn't changed.
            return
        adt.animation = self.selected_animation
    finally:
        _wm_selected_animation_lock.release()


def register_props() -> None:
    # Due to this hackyness, the WindowManager will increase the user count of
    # the pointed-to Animation data-block.
    WindowManager.selected_animation = PointerProperty(
        type=Animation,
        name="Animation",
        description="Animation assigned to the active Object",
        update=_wm_selected_animation_update,
    )


if __name__ == "__main__":  # only for live edit.
    register_, _ = bpy.utils.register_classes_factory(classes)
    register_()
    register_props()
