/*
 * Example anybind binding for md4c (Markdown -> HTML).
 * SPDX-License-Identifier: MIT
 */
#include <anybind/anybind.h>
#include <md4c-html.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace {

void append_chunk(const MD_CHAR* text, MD_SIZE size, void* userdata) {
  static_cast<std::string*>(userdata)->append(text, size);
}

std::string to_html(const std::string& markdown, bool gfm, bool xhtml) {
  if (markdown.size() > std::numeric_limits<MD_SIZE>::max()) {
    throw std::runtime_error("markdown input exceeds md4c size limit");
  }

  const unsigned parser_flags = gfm ? MD_DIALECT_GITHUB : MD_DIALECT_COMMONMARK;
  const unsigned renderer_flags = xhtml ? MD_HTML_FLAG_XHTML : 0;

  std::string out;
  out.reserve(markdown.size());
  const int rc = md_html(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &append_chunk,
                         &out, parser_flags, renderer_flags);
  if (rc != 0) {
    throw std::runtime_error("md_html failed with status " + std::to_string(rc));
  }
  return out;
}

}  // namespace

ANYBIND_MODULE(md4c, m) {
  using namespace anybind::literals;

  m.def("toHtml", &to_html, "markdown"_a, "gfm"_a = true, "xhtml"_a = false);
}
