/*
 * anybind stub generator: interface between the recording Node-API
 * implementation (recorder.cpp) and the generator's entry point (main.cpp).
 * SPDX-License-Identifier: MIT
 */
#pragma once

#ifndef NAPI_VERSION
#define NAPI_VERSION 8
#endif
#include <node_api.h>

#include <string>

namespace anybind_stubgen {

/* Creates the environment that every Node-API call in this process uses. */
napi_env create_env();

/* Runs finalizers and cleanup hooks like an environment teardown, then frees `env`. */
void destroy_env(napi_env env);

/* A fresh, empty `exports` object. */
napi_value create_object(napi_env env);

/* Reads own string property `name` of `object`; false if it is missing or not a string. */
bool get_own_string(napi_env env, napi_value object, const char* name, std::string* out);

/* Clears a pending JS exception and returns its message; false if none is pending. */
bool take_exception(napi_env env, std::string* message);

}  // namespace anybind_stubgen
