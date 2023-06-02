/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef WITH_PYTHON

#  include "usd_umm.h"
#  include "usd.h"
#  include "usd_asset_utils.h"
#  include "usd_exporter_context.h"

#  include <pxr/usd/sdf/copyUtils.h>
#  include <pxr/usd/usdUtils/stageCache.h>

#  include <iostream>

#  include "DNA_material_types.h"
#  include "WM_api.h"

#  include "Python.h"

// The following is additional example code for invoking Python and
// a Blender Python operator from C++:

//#include "BPY_extern_python.h"
//#include "BPY_extern_run.h"

// const char *foo[] = { "bpy", 0 };
// BPY_run_string_eval(C, nullptr, "print('hi!!')");
// BPY_run_string_eval(C, foo, "bpy.ops.universalmaterialmap.instance_to_data_converter()");
// BPY_run_string_eval(C, nullptr, "print('test')");

static PyObject *g_umm_module = nullptr;
static const char *k_umm_module_name = "umm";
static const char *k_export_mtl_func_name = "export_material";
static const char *k_import_mtl_func_name = "import_material";

using namespace blender::io::usd;

static void print_obj(PyObject *obj)
{
  if (!obj) {
    return;
  }

  PyObject *str = PyObject_Str(obj);
  if (str && PyUnicode_Check(str)) {
    std::cout << PyUnicode_AsUTF8(str) << std::endl;
    Py_DECREF(str);
  }
}

/* A no-op callback used when impoting textures is turned off. */
static PyObject *import_texture_noop(PyObject *self, PyObject *args)
{
  /* Return the input path unchanged. */
  const char *asset_path;
  if (!PyArg_ParseTuple(args, "s", &asset_path)) {
    return NULL;
  }
  return PyUnicode_FromString(asset_path);
}

static PyMethodDef import_texture_noop_method = {
    "import_texture_noop_cb",
    import_texture_noop,
    METH_VARARGS,
    "A no-op function that returns the input path "
    "argument unchanged, used when texture importing "
    "is turned off."};

static PyObject *import_texture(PyObject *self, PyObject *args)
{
  const char *asset_path = "";
  if (!PyArg_ParseTuple(args, "s", &asset_path)) {
    return NULL;
  }

  if (!should_import_asset(asset_path)) {
    return PyUnicode_FromString(asset_path);
  }

  if (!self) {
    return NULL;
  }

  if (!PyTuple_Check(self)) {
    return NULL;
  }

  if (PyTuple_Size(self) < 2) {
    return NULL;
  }

  PyObject *tex_dir_item = PyTuple_GetItem(self, 0);
  if (!(tex_dir_item && PyUnicode_Check(tex_dir_item))) {
    return NULL;
  }
  const char *tex_dir = PyUnicode_AsUTF8(tex_dir_item);

  PyObject *name_collision_mode_item = PyTuple_GetItem(self, 1);
  if (!(name_collision_mode_item && PyLong_Check(name_collision_mode_item))) {
    return NULL;
  }

  eUSDTexNameCollisionMode name_collision_mode = static_cast<eUSDTexNameCollisionMode>(
      PyLong_AsLong(name_collision_mode_item));

  std::string import_path = import_asset(asset_path, tex_dir, name_collision_mode);

  if (!import_path.empty()) {
    return PyUnicode_FromString(import_path.c_str());
  }

  return PyUnicode_FromString(asset_path);
}

static PyMethodDef import_texture_method = {
    "import_texture",
    import_texture,
    METH_VARARGS,
    "If the given texture asset path is a URI or is "
    "relative to a USDZ arhive, attempt to copy the "
    "texture to the local file system and return the "
    "asset's local path. The source path will be "
    "returned unchanged if it's alreay a local "
    "file or if it could not be copied to a local "
    "destination. The function may return None if "
    "there was a Python error."};


static PyObject *create_import_texture_cb(const USDImportParams &import_params)
{
  if (import_params.import_textures_mode == USD_TEX_IMPORT_NONE) {
    /* Importing textures is turned off, so return a no-op function. */
    return PyCFunction_New(&import_texture_noop_method, NULL);
  }

  /* Create the first 'self' argument for the 'import_textures'
   * function, which is a tuple storing the texture import
   * parameters that will be needed to copy the texture. */

  const char *textures_dir = import_params.import_textures_mode == USD_TEX_IMPORT_PACK ?
                                 temp_textures_dir() :
                                 import_params.import_textures_dir;

  const eUSDTexNameCollisionMode name_collision_mode = import_params.import_textures_mode ==
                                                               USD_TEX_IMPORT_PACK ?
                                                           USD_TEX_NAME_COLLISION_OVERWRITE :
                                                           import_params.tex_name_collision_mode;

  PyObject *import_texture_self = PyTuple_New(2);
  PyObject *tex_dir_item = PyUnicode_FromString(textures_dir);
  PyTuple_SetItem(import_texture_self, 0, tex_dir_item);
  PyObject *collision_mode_item = PyLong_FromLong(static_cast<long>(name_collision_mode));
  PyTuple_SetItem(import_texture_self, 1, collision_mode_item);

  return PyCFunction_New(&import_texture_method, import_texture_self);
}

namespace {

enum eUMMNotification {
  UMM_NOTIFICATION_NONE = 0,
  UMM_NOTIFICATION_SUCCESS,
  UMM_NOTIFICATION_FAILURE
};

}  // anonymous namespace

/* Parse the dictionary returned by UMM for an error notification
 * and message.  Report the message in the Blender UI and return
 * the notification enum.  */
static eUMMNotification report_notification(PyObject *dict)
{
  if (!dict) {
    return UMM_NOTIFICATION_NONE;
  }

  if (!PyDict_Check(dict)) {
    return UMM_NOTIFICATION_NONE;
  }

  /* Display warnings first. */
  PyObject *warnings_list = PyDict_GetItemString(dict, "warnings");

  if (warnings_list && PyList_Check(warnings_list)) {
    Py_ssize_t len = PyList_Size(warnings_list);
    for (Py_ssize_t i = 0; i < len; ++i) {
      PyObject *warning_item = PyList_GetItem(warnings_list, i);
      if (!(warning_item && PyUnicode_Check(warning_item))) {
        continue;
      }
      const char *warning_str = PyUnicode_AsUTF8(warning_item);
      if (warning_str) {
        WM_reportf(RPT_WARNING, "%s", warning_str);
      }
    }
  }

  PyObject *notification_item = PyDict_GetItemString(dict, "umm_notification");

  if (!notification_item) {
    return UMM_NOTIFICATION_NONE;
  }

  if (!PyUnicode_Check(notification_item)) {
    WM_reportf(RPT_WARNING, "%s: 'umm_notification' value is not a string", __func__);
    return UMM_NOTIFICATION_NONE;
  }

  const char *notification_str = PyUnicode_AsUTF8(notification_item);

  if (!notification_str) {
    WM_reportf(RPT_WARNING, "%s: Couldn't get 'umm_notification' string value", __func__);
    return UMM_NOTIFICATION_NONE;
  }

  if (strcmp(notification_str, "success") == 0) {
    /* We don't report success, do nothing. */
    return UMM_NOTIFICATION_SUCCESS;
  }

  PyObject *message_item = PyDict_GetItemString(dict, "message");

  if (message_item && PyUnicode_Check(message_item)) {
    const char *message_str = PyUnicode_AsUTF8(message_item);
    if (!message_str) {
      WM_reportf(RPT_WARNING, "%s: Null message string value", __func__);
      return UMM_NOTIFICATION_NONE;
    }

    if (strcmp(notification_str, "unexpected_error") == 0) {
      WM_reportf(RPT_ERROR, "%s", message_str);
      return UMM_NOTIFICATION_FAILURE;
    }

    WM_reportf(RPT_WARNING, "%s: Unsupported notification type '%s'", __func__, notification_str);
  }

  return UMM_NOTIFICATION_NONE;
}

static bool is_none_value(PyObject *tup)
{
  if (!(tup && PyTuple_Check(tup) && PyTuple_Size(tup) > 1)) {
    return false;
  }

  PyObject *second = PyTuple_GetItem(tup, 1);

  return second == Py_None;
}

/* Be sure to call PyGILState_Ensure() before calling this function. */
static bool ensure_module_loaded(bool warn = true)
{

  if (!g_umm_module) {
    g_umm_module = PyImport_ImportModule(k_umm_module_name);
    if (!g_umm_module) {
      if (warn) {
        std::cout << "WARNING: couldn't load Python module " << k_umm_module_name << std::endl;
        if (PyErr_Occurred()) {
          PyErr_Print();
        }
      }
      PyErr_Clear();
    }
  }

  return g_umm_module != nullptr;
}

static bool copy_material_to_stage(PyObject *dict, const pxr::UsdShadeMaterial &usd_material)
{
  if (!(dict && usd_material)) {
    return false;
  }

  if (!PyDict_Check(dict)) {
    WM_reportf(RPT_ERROR, "%s:  Result is not a dictionary", __func__);
    return false;
  }

  PyObject *usda_item = PyDict_GetItemString(dict, "usda");

  if (!usda_item) {
    WM_reportf(RPT_ERROR, "%s:  Result dictionary is missing expected 'usda' item", __func__);
    return false;
  }

  if (!PyUnicode_Check(usda_item)) {
    WM_reportf(RPT_ERROR, "%s:  Result 'usda' item is not a string", __func__);
    return false;
  }

  const char *usda_str = PyUnicode_AsUTF8(usda_item);

  std::cout << "****** usda_str " << usda_str << std::endl;

  pxr::UsdStageWeakPtr stage = usd_material.GetPrim().GetStage();

  if (!stage) {
    WM_reportf(RPT_ERROR,
               "%s: Couldn't get stage from material %s",
               __func__,
               usd_material.GetPath().GetAsString().c_str());
    return false;
  }

  pxr::UsdStageRefPtr anon_stage = pxr::UsdStage::CreateInMemory();
  if (!anon_stage) {
    WM_reportf(RPT_ERROR, "%s: Couldn't create anonymous stage", __func__);
    return false;
  }

  pxr::SdfLayerHandle src_layer = anon_stage->GetRootLayer();
  if (!src_layer->ImportFromString(usda_str)) {
    WM_reportf(RPT_ERROR, "%s: Couldn't read usda into anonymous layer", __func__);
    return false;
  }

  pxr::SdfPath mtl_path = usd_material.GetPath();

  pxr::SdfLayerHandle dst_layer = stage->GetRootLayer();

  if (!pxr::SdfCopySpec(src_layer, mtl_path, dst_layer, mtl_path)) {
    WM_reportf(RPT_ERROR,
               "%s: Couldn't copy %s from usda into the stage",
               __func__,
               mtl_path.GetAsString().c_str());
    return false;
  }

  return true;
}

static PyObject *get_material_usda_obj(const pxr::UsdShadeMaterial &usd_material)
{
  if (!usd_material) {
    return nullptr;
  }

  pxr::UsdStageWeakPtr stage = usd_material.GetPrim().GetStage();

  if (!stage) {
    WM_reportf(RPT_ERROR,
               "%s: Couldn't get stage from material %s",
               __func__,
               usd_material.GetPath().GetAsString().c_str());
    return nullptr;
  }

  pxr::UsdStageRefPtr anon_stage = pxr::UsdStage::CreateInMemory();
  if (!anon_stage) {
    WM_reportf(RPT_ERROR, "%s: Couldn't create anonymous stage", __func__);
    return nullptr;
  }

  pxr::SdfLayerHandle src_layer = stage->GetRootLayer();

  pxr::SdfPath mtl_path = usd_material.GetPath();

  pxr::SdfLayerHandle dst_layer = anon_stage->GetRootLayer();
  pxr::SdfCreatePrimInLayer(dst_layer, mtl_path);

  if (!pxr::SdfCopySpec(src_layer, mtl_path, dst_layer, mtl_path)) {
    WM_reportf(RPT_ERROR,
               "%s: Couldn't copy %s from usda into the anonymous stage",
               __func__,
               mtl_path.GetAsString().c_str());
    return nullptr;
  }

  std::string usda;
  if (!dst_layer->ExportToString(&usda)) {
    WM_reportf(RPT_ERROR, "%s: Couldn't export anonymous stage ot string", __func__);
    return nullptr;
  }

  if (usda.empty()) {
    WM_reportf(RPT_ERROR, "%s: USDA string is empty", __func__);
    return nullptr;
  }

  std::cout << "**** usda: " << usda << std::endl;
  return PyUnicode_FromString(usda.c_str());
}

namespace blender::io::usd {

bool umm_module_loaded()
{
  PyGILState_STATE gilstate = PyGILState_Ensure();

  bool loaded = ensure_module_loaded(false /* warn */);

  PyGILState_Release(gilstate);

  return loaded;
}

bool umm_import_material(const USDImportParams &import_params,
                         Material *mtl,
                         const pxr::UsdShadeMaterial &usd_material,
                         const std::string &render_context)
{
  if (!(mtl && usd_material)) {
    return false;
  }

  PyGILState_STATE gilstate = PyGILState_Ensure();

  if (!ensure_module_loaded()) {
    PyGILState_Release(gilstate);
    return false;
  }

  const char *func_name = k_import_mtl_func_name;

  print_obj(g_umm_module);

  if (!PyObject_HasAttrString(g_umm_module, func_name)) {
    WM_reportf(
        RPT_ERROR, "%s: module %s has no attribute %s", __func__, k_umm_module_name, func_name);
    return false;
  }

  PyObject *func = PyObject_GetAttrString(g_umm_module, func_name);

  if (!func) {
    WM_reportf(RPT_ERROR,
               "%s: couldn't get %s module attribute %s",
               __func__,
               k_umm_module_name,
               func_name);
    PyGILState_Release(gilstate);
    return false;
  }

  // Create the args dictionary.
  PyObject *args_dict = PyDict_New();

  if (!args_dict) {
    WM_reportf(RPT_ERROR, "%s:  Couldn't create args_dict dictionary", __func__);
    PyGILState_Release(gilstate);
    return false;
  }

  PyObject *instance_name = PyUnicode_FromString(mtl->id.name + 2);
  PyDict_SetItemString(args_dict, "instance_name", instance_name);
  Py_DECREF(instance_name);

  PyObject *render_context_arg = PyUnicode_FromString(render_context.c_str());
  PyDict_SetItemString(args_dict, "render_context", render_context_arg);
  Py_DECREF(render_context_arg);

  PyObject *mtl_path_arg = PyUnicode_FromString(usd_material.GetPath().GetAsString().c_str());
  PyDict_SetItemString(args_dict, "mtl_path", mtl_path_arg);
  Py_DECREF(mtl_path_arg);

  pxr::UsdStageCache::Id id = pxr::UsdUtilsStageCache::Get().Insert(
      usd_material.GetPrim().GetStage());

  if (!id.IsValid()) {
    WM_reportf(RPT_ERROR, "%s:  Couldn't create stage cache", __func__);
    PyGILState_Release(gilstate);
    return false;
  }

  PyObject *stage_id_arg = PyLong_FromLong(id.ToLongInt());
  PyDict_SetItemString(args_dict, "stage_id", stage_id_arg);
  Py_DECREF(stage_id_arg);

  PyObject *import_tex_cb_arg = create_import_texture_cb(import_params);
  PyDict_SetItemString(args_dict, "import_texture_cb", import_tex_cb_arg);
  Py_DECREF(import_tex_cb_arg);

  PyObject *args = PyTuple_New(1);
  PyTuple_SetItem(args, 0, args_dict);

  //std::cout << func_name << " arguments:\n";
  //print_obj(args);

  PyObject *ret = PyObject_Call(func, args, nullptr);
  Py_DECREF(func);

  bool success = false;

  if (ret && !is_none_value(ret)) {
    std::cout << "result:\n";
    //print_obj(ret);
    success = report_notification(ret) == UMM_NOTIFICATION_SUCCESS;
  }

  if (ret) {
    Py_DECREF(ret);
  }

  Py_DECREF(args);

  PyGILState_Release(gilstate);

  return success;
}

bool umm_export_material(const USDExporterContext &usd_export_context,
                         const Material *mtl,
                         const pxr::UsdShadeMaterial &usd_material,
                         const std::string &render_context)
{
  if (!(usd_material && mtl)) {
    return false;
  }

  PyGILState_STATE gilstate = PyGILState_Ensure();

  if (!ensure_module_loaded()) {
    PyGILState_Release(gilstate);
    return false;
  }

  const char *func_name = k_export_mtl_func_name;

  print_obj(g_umm_module);

  if (!PyObject_HasAttrString(g_umm_module, func_name)) {
    WM_reportf(
        RPT_ERROR, "%s: module %s has no attribute %s", __func__, k_umm_module_name, func_name);
    PyGILState_Release(gilstate);
    return false;
  }

  PyObject *func = PyObject_GetAttrString(g_umm_module, func_name);

  if (!func) {
    WM_reportf(RPT_ERROR,
               "%s: couldn't get %s module attribute %s",
               __func__,
               k_umm_module_name,
               func_name);
    PyGILState_Release(gilstate);
    return false;
  }

  // Create the args dictionary.
  PyObject *args_dict = PyDict_New();

  if (!args_dict) {
    WM_reportf(RPT_ERROR, "%s:  Couldn't create args_dict dictionary", __func__);
    PyGILState_Release(gilstate);
    return false;
  }

  PyObject *instance_name = PyUnicode_FromString(mtl->id.name + 2);
  PyDict_SetItemString(args_dict, "instance_name", instance_name);
  Py_DECREF(instance_name);

  PyObject *render_context_arg = PyUnicode_FromString(render_context.c_str());
  PyDict_SetItemString(args_dict, "render_context", render_context_arg);
  Py_DECREF(render_context_arg);

  PyObject *mtl_path_arg = PyUnicode_FromString(usd_material.GetPath().GetAsString().c_str());
  PyDict_SetItemString(args_dict, "mtl_path", mtl_path_arg);
  Py_DECREF(mtl_path_arg);

  pxr::UsdStageWeakPtr stage = usd_material.GetPrim().GetStage();

  if (!stage) {
    WM_reportf(RPT_ERROR, "%s:  Couldn't get stage pointer from material", __func__);
    PyGILState_Release(gilstate);
    return false;
  }

  pxr::UsdStageCache::Id id = pxr::UsdUtilsStageCache::Get().Insert(stage);

  if (!id.IsValid()) {
    WM_reportf(RPT_ERROR, "%s:  Couldn't create stage cache", __func__);
    PyGILState_Release(gilstate);
    return false;
  }

  PyObject *stage_id_arg = PyLong_FromLong(id.ToLongInt());
  PyDict_SetItemString(args_dict, "stage_id", stage_id_arg);
  Py_DECREF(stage_id_arg);

  std::string usd_path = stage->GetRootLayer()->GetRealPath();
  PyObject *usd_path_arg = PyUnicode_FromString(usd_path.c_str());
  PyDict_SetItemString(args_dict, "usd_path", usd_path_arg);
  Py_DECREF(usd_path_arg);

  PyObject *args = PyTuple_New(1);
  PyTuple_SetItem(args, 0, args_dict);

  //std::cout << func_name << " arguments:\n";
  //print_obj(args);

  PyObject *ret = PyObject_Call(func, args, nullptr);
  Py_DECREF(func);

  bool success = false;

  if (ret && !is_none_value(ret)) {
    std::cout << "result:\n";
    //print_obj(ret);
    success = report_notification(ret) == UMM_NOTIFICATION_SUCCESS;
  }

  if (ret) {
    Py_DECREF(ret);
  }

  Py_DECREF(args);

  PyGILState_Release(gilstate);

  return success;
}

}  // Namespace blender::io::usd

#endif  // ifdef WITH_PYTHON
