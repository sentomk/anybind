/*
 * anybind: Windows delay-load hook, compiled into every addon on Windows.
 * SPDX-License-Identifier: MIT
 *
 * Addons link against an import library that attributes every Node-API symbol to
 * node.exe, and node.exe is delay-loaded. When the first Node-API function is
 * called, this hook hands back the module that actually exports Node-API instead
 * of loading node.exe, so the addon also works in electron.exe, bun.exe and any
 * other host. Same approach as node-gyp's win_delay_load_hook.cc.
 */
#ifdef _MSC_VER

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <delayimp.h>
#include <string.h>

static FARPROC WINAPI anybind_delay_load_hook(unsigned int event, DelayLoadInfo* info) {
  if (event != dliNotePreLoadLibrary) return nullptr;
  if (_stricmp(info->szDll, "node.exe") != 0) return nullptr;
  // Node.js built as a shared library exports Node-API from libnode.dll.
  HMODULE module = GetModuleHandleA("libnode.dll");
  if (module == nullptr) module = GetModuleHandleA(nullptr);
  return reinterpret_cast<FARPROC>(module);
}

decltype(__pfnDliNotifyHook2) __pfnDliNotifyHook2 = anybind_delay_load_hook;

#endif  // _MSC_VER
