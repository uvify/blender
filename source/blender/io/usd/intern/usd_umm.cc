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

  PyObject *notification_item = PyDict_GetItemString(dict, "umm_notification");

  if (!notification_item) {
    return UMM_NOTIFICATION_NONE;
  }

  if (!PyUnicode_Check(notification_item)) {
    std::cerr << "WARNING: 'umm_notification' value is not a string" << std::endl;
    return UMM_NOTIFICATION_NONE;
  }

  const char *notification_str = PyUnicode_AsUTF8(notification_item);

  if (!notification_str) {
    std::cerr << "WARNING: couldn't get 'umm_notification' string value" << std::endl;
    return UMM_NOTIFICATION_NONE;
  }

  if (strcmp(notification_str, "success") == 0) {
    /* We don't report success, do nothing. */
    return UMM_NOTIFICATION_SUCCESS;
  }

  const char *message_str = nullptr;

  PyObject *message_item = PyDict_GetItemString(dict, "message");

  if (message_item && PyUnicode_Check(message_item)) {
    message_str = PyUnicode_AsUTF8(message_item);
  }

  if (strcmp(notification_str, "incomplete_process") == 0) {
    WM_reportf(RPT_WARNING, "%s", message_str);
    return UMM_NOTIFICATION_FAILURE;
  }

  if (strcmp(notification_str, "unexpected_error") == 0) {
    WM_reportf(RPT_ERROR, "%s", message_str);
    return UMM_NOTIFICATION_FAILURE;
  }

  std::cout << "WARNING: unknown notification type: " << notification_str << std::endl;

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

  PyObject *args = PyTuple_New(1);
  PyTuple_SetItem(args, 0, args_dict);

  std::cout << func_name << " arguments:\n";
  print_obj(args);

  PyObject *ret = PyObject_Call(func, args, nullptr);
  Py_DECREF(func);

  bool success = false;

  if (ret && !is_none_value(ret)) {
    std::cout << "result:\n";
    print_obj(ret);
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

  PyObject *args = PyTuple_New(1);
  PyTuple_SetItem(args, 0, args_dict);

  std::cout << func_name << " arguments:\n";
  print_obj(args);

  PyObject *ret = PyObject_Call(func, args, nullptr);
  Py_DECREF(func);

  bool success = false;

  if (ret && !is_none_value(ret)) {
    std::cout << "result:\n";
    print_obj(ret);
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
