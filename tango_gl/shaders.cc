/*
 * Copyright 2014 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "tango-gl/shaders.h"

namespace tango_gl {
namespace shaders {

// Fallback vertex shader.  This shader will be used if a valid shader
// program is not set on a material.
std::string kFallbackVS() {
    return  "precision mediump float;\n"
            "precision mediump int;\n"
            "attribute vec4 vertex;\n"
            "uniform mat4 mvp;\n"
            "void main() {\n"
            "  gl_Position = mvp * vertex;\n"
            "}\n";
}

// Fallback pixel shader.  This shader will be used if a valid shader
// program is not set on a material.
std::string kFallbackPS() {
    return  "precision mediump float;\n"
            "void main() {\n"
            "  gl_FragColor = vec4(1, 0, 1, 1);\n"
            "}\n";
}

std::string GetBasicVertexShader() {
  return "precision highp float;\n"
         "precision mediump int;\n"
         "attribute vec4 vertex;\n"
         "uniform mat4 mvp;\n"
         "uniform vec4 color;\n"
         "varying vec4 v_color;\n"
         "void main() {\n"
         "  gl_Position = mvp*vertex;\n"
         "  v_color = color;\n"
         "}\n";
}

std::string GetBasicFragmentShader() {
  return "precision highp float;\n"
         "varying vec4 v_color;\n"
         "void main() {\n"
         "  gl_FragColor = v_color;\n"
         "}\n";
}

std::string GetTexturedVertexShader() {
  return "precision highp float;\n"
         "precision highp int;\n"
         "attribute vec4 vertex;\n"
         "attribute vec2 uv;\n"
         "varying vec2 f_textureCoords;\n"
         "uniform mat4 mvp;\n"
         "void main() {\n"
         "  f_textureCoords = uv;\n"
         "  gl_Position = mvp * vertex;\n"
         "}\n";
}

std::string GetTexturedFragmentShader() {


  return "precision highp float;\n"
         "precision highp int;\n"
         "uniform sampler2D texture;\n"
         "varying vec2 f_textureCoords;\n"
         "void main() {\n"
         "  gl_FragColor = texture2D(texture, f_textureCoords);\n"
         "}\n";
}

std::string GetDiffuseTexturedVertexShader() {
  return
      "precision highp float;\n"
      "precision highp int;\n"
      "attribute vec4 vertex;\n"
      "attribute vec3 normal;\n"
      "attribute vec2 uv;\n"
      " \n"
      "uniform mat4 mvp;\n"
      "uniform mat3 mv;\n"
      " \n"
      "varying vec2 f_textureCoords;\n"
      " \n"
      "void main() {\n"
      "  f_textureCoords = uv;\n"
      "  gl_Position = mvp * vertex;\n"
      "}\n";
}

  std::string GetDiffuseTexturedFragmentShader() {
    return "precision highp float;\n"
        "precision highp int;\n"
        "uniform sampler2D texture;\n"
        "uniform float light_dir;\n"
        " \n"
        "varying vec2 f_textureCoords;\n"
        " \n"
        "void main() {\n"
        "  \n"
        "  gl_FragColor = light_dir * texture2D(texture, f_textureCoords);\n"
        "}\n";
  }
//     "  Intensity = clamp(dot(f_normal, -light_dir), 0.0, 1.0);\n"
//"  gl_FragColor = ( clamp( Intensity * light_color, 0.0, 1.0 ) ) * texture2D(texture, f_textureCoords);\n"

std::string GetColorVertexShader() {
  return "precision mediump float;\n"
         "precision mediump int;\n"
         "attribute vec4 vertex;\n"
         "attribute vec4 color;\n"
         "uniform mat4 mvp;\n"
         "varying vec4 v_color;\n"
         "void main() {\n"
         "  gl_Position = mvp*vertex;\n"
         "  v_color = color;\n"
         "}\n";
}

std::string GetVideoOverlayVertexShader() {
  return "precision highp float;\n"
         "precision highp int;\n"
         "attribute vec4 vertex;\n"
         "attribute vec2 textureCoords;\n"
         "varying vec2 f_textureCoords;\n"
         "uniform mat4 mvp;\n"
         "void main() {\n"
         "  f_textureCoords = textureCoords;\n"
         "  gl_Position = mvp * vertex;\n"
         "}\n";
}

std::string GetVideoOverlayFragmentShader() {
  return "#extension GL_OES_EGL_image_external : require\n"
         "precision highp float;\n"
         "precision highp int;\n"
         "uniform samplerExternalOES texture;\n"
         "varying vec2 f_textureCoords;\n"
         "void main() {\n"
         "  gl_FragColor = texture2D(texture, f_textureCoords);\n"
         "}\n";
}

std::string GetVideoOverlayTexture2DFragmentShader() {
  return "precision highp float;\n"
         "precision highp int;\n"
         "uniform sampler2D texture;\n"
         "varying vec2 f_textureCoords;\n"
         "void main() {\n"
         "  gl_FragColor = texture2D(texture, f_textureCoords);\n"
         "}\n";
}

std::string GetShadedVertexShader() {
  return "attribute vec4 vertex;\n"
         "attribute vec3 normal;\n"
         "uniform mat4 mvp;\n"
         "uniform mat4 mv;\n"
         "uniform vec4 color;\n"
         "uniform vec3 lightVec;\n"
         "varying vec4 v_color;\n"
         "void main() {\n"
         "  vec3 mvNormal = vec3(mv * vec4(normal, 0.0));\n"
         "  float diffuse = max(-dot(mvNormal, lightVec), 0.0);\n"
         "  v_color.a = color.a;\n"
         "  v_color.xyz = color.xyz * diffuse + color.xyz * 0.3;\n"
         "  gl_Position = mvp*vertex;\n"
         "}\n";
}

std::string GetPhongVertexShader() {
  return "#version 300 es\n"
      "uniform mat4 u_projectionMatrix;\n"
      "uniform mat4 u_modelViewMatrix;\n"
      "uniform mat3 u_normalMatrix;\n"
      "in vec4 a_vertex;\n"
      "in vec3 a_normal;\n"
      "out vec3 v_normal;\n"
      "out vec3 v_eye;\n"
      "void main() {\n"
      "	vec4 vertex = u_modelViewMatrix * a_vertex;\n"
      "	v_eye = -vec3(vertex);\n"
      "	v_normal = u_normalMatrix * a_normal;\n"
      "	gl_Position = u_projectionMatrix * vertex;\n"
      "}\n";
}

std::string GetPhongFragmentShader() {
  return  "#version 300 es\n"
      "precision lowp float;\n"
      "struct LightProperties\n"
      "{\n"
      "	vec3 direction;\n"
      "	vec4 ambientColor;\n"
      "	vec4 diffuseColor;\n"
      "	vec4 specularColor;\n"
      "};\n"
      "struct MaterialProperties\n"
      "{\n"
      "	vec4 ambientColor;\n"
      "	vec4 diffuseColor;\n"
      "	vec4 specularColor;\n"
      "	float specularExponent;\n"
      "};\n"
      "uniform	LightProperties u_light;\n"
      "uniform	MaterialProperties u_material;\n"
      "in vec3 v_normal;\n"
      "in vec3 v_eye;\n"
      "out vec4 fragColor;\n"
      "\n"
      "void main()\n"
      "{	// Note: All calculations are in camera space.\n"
      "	vec4 color = u_light.ambientColor * u_material.ambientColor;\n"
      "	vec3 normal = normalize(v_normal);\n"
      "	float nDotL = max(dot(u_light.direction, normal), 0.0);\n"
      "	if (nDotL > 0.0)\n"
      "	{\n"
      "		vec3 eye = normalize(v_eye);\n"
      "		// Incident vector is opposite light direction vector.\n"
      "		vec3 reflection = reflect(-u_light.direction, normal);\n"
      "		float eDotR = max(dot(eye, reflection), 0.0);\n"
      "		color += u_light.diffuseColor * u_material.diffuseColor * nDotL;\n"
      "		float specularIntensity = 0.0;\n"
      "		if (eDotR > 0.0) { \n"
      "			specularIntensity = pow(eDotR, u_material.specularExponent);\n"
      "		}\n"
      "		color += u_light.specularColor * u_material.specularColor * specularIntensity;\n"
      "	}\n"
      "	fragColor = color;\n"
      "}\n";
}
}  // namespace shaders
}  // namespace tango_gl
