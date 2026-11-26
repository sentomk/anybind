"use strict";

const path = require("node:path");

module.exports = {
  /** Directory to add to the C++ include path (contains `anybind/anybind.h`). */
  include: path.join(__dirname, "include"),
  /** CMake module providing `anybind::anybind` and `anybind_add_module()`. */
  cmakeModule: path.join(__dirname, "cmake", "anybind.cmake"),
  version: require("./package.json").version,
};
