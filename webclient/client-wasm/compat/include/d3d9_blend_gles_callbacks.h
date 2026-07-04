#pragma once

#include <GLES2/gl2.h>

#include "d3d9_blend_state.h"

namespace wyd::d3d9_compat {

inline void GLESBlendEnable() {
  glEnable(GL_BLEND);
}

inline void GLESBlendDisable() {
  glDisable(GL_BLEND);
}

inline void GLESBlendColor(float r, float g, float b, float a) {
  glBlendColor(r, g, b, a);
}

inline void GLESBlendFunc(DWORD src, DWORD dst) {
  glBlendFunc(static_cast<GLenum>(src), static_cast<GLenum>(dst));
}

inline void GLESBlendFuncSeparate(DWORD src_rgb,
                                  DWORD dst_rgb,
                                  DWORD src_alpha,
                                  DWORD dst_alpha) {
  glBlendFuncSeparate(
      static_cast<GLenum>(src_rgb),
      static_cast<GLenum>(dst_rgb),
      static_cast<GLenum>(src_alpha),
      static_cast<GLenum>(dst_alpha));
}

inline void GLESBlendEquation(DWORD op) {
  glBlendEquation(static_cast<GLenum>(op));
}

inline void GLESBlendEquationSeparate(DWORD rgb_op, DWORD alpha_op) {
  glBlendEquationSeparate(static_cast<GLenum>(rgb_op), static_cast<GLenum>(alpha_op));
}

inline WebGLLegacyBlendFuncRuntimeFns MakeGLESBlendLegacyRuntimeFns() {
  WebGLLegacyBlendFuncRuntimeFns runtime;
  runtime.enable_blend = GLESBlendEnable;
  runtime.disable_blend = GLESBlendDisable;
  runtime.blend_func = GLESBlendFunc;
  runtime.blend_equation = GLESBlendEquation;
  return runtime;
}

inline WebGLBlendRuntimeFns MakeGLESBlendRuntimeFns(bool supports_min_max_equations) {
  WebGLBlendRuntimeFns runtime;
  runtime.enable_blend = GLESBlendEnable;
  runtime.disable_blend = GLESBlendDisable;
  runtime.apply.blend_color = GLESBlendColor;
  runtime.apply.blend_func = GLESBlendFunc;
  runtime.apply.blend_func_separate = GLESBlendFuncSeparate;
  runtime.apply.blend_equation = GLESBlendEquation;
  runtime.apply.blend_equation_separate = GLESBlendEquationSeparate;
  runtime.apply.supports_min_max_equations = supports_min_max_equations;
  return runtime;
}

}  // namespace wyd::d3d9_compat
