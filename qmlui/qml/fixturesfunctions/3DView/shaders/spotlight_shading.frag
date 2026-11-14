/*
  Q Light Controller Plus
  spotlight_shading.frag

  Copyright (c) Eric Arnebäck

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

FS_IN_ATTRIB vec3 fsPos;

DECLARE_FRAG_COLOR

uniform mat4 viewProjectionMatrix;
uniform mat4 inverseViewProjectionMatrix;

uniform sampler2D albedoTex;
uniform sampler2D normalTex;
uniform sampler2D depthTex;

uniform vec3 lightPos;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform float lightIntensity;
uniform int useShadows;

uniform vec4 goboRotation;

uniform float coneTopRadius;
uniform float coneBottomRadius;
uniform float coneDistCutoff;

uniform mat4 lightViewMatrix;
uniform mat4 lightViewProjectionMatrix;
uniform mat4 lightProjectionMatrix;
uniform mat4 lightViewProjectionScaleAndOffsetMatrix;

uniform sampler2D goboTex;
uniform sampler2D shadowTex;

uniform float headLength;

void main()
{

    vec3 albedo, normal, position;

    vec4 u =  viewProjectionMatrix * vec4(fsPos, 1.0);
    vec2 uv = (u.xy / u.w) * 0.5 + vec2(0.5);
    albedo = SAMPLE_TEX2D(albedoTex, uv).rgb;
    normal = normalize(SAMPLE_TEX2D(normalTex, uv).xyz);
    float z = SAMPLE_TEX2D(depthTex, uv).r;

    vec4 temp = inverseViewProjectionMatrix * vec4(u.x / u.w, u.y / u.w, -1.0 + 2.0 * z, 1.0);
    temp.xyz = temp.xyz / temp.w;
    position = temp.xyz;

    float shadowMask = 1.0;
    if (useShadows == 1) {
        // the shadow map may belong to several emitters: it has a matrix of its own
        vec4 p = lightViewProjectionScaleAndOffsetMatrix * vec4(position.xyz, 1.0);
        float curZ = p.z / p.w;
        float refZ = SAMPLE_TEX2D(shadowTex, p.xy / p.w).r;
        // The margin grows as the beam lands at a shallower angle: with a
        // fixed one, a surface seen almost edge on from the light shadows
        // itself in stripes
        float facing = max(0.0, dot(normal, -lightDir));
        float margin = mix(0.004, 0.0006, facing);
        shadowMask = (curZ < refZ + margin ? 1.0 : 0.0);
    }

    vec4 q = lightViewMatrix * vec4(position.xyz, 1.0);
    float r = coneTopRadius + (coneBottomRadius - coneTopRadius) * ((abs(q.z) - 0.5 * headLength) / coneDistCutoff);
    vec2 tc = (mat2x2(goboRotation.x, goboRotation.y, goboRotation.z, goboRotation.w) * ((-q.xy) * (1.0 / r))) * 0.5 + 0.5;

    vec4 gSample = SAMPLE_TEX2D(goboTex, tc.xy);
    float goboMask = gSample.a * gSample.r;

    // A beam fades out towards the rim of its cone, instead of ending in a
    // hard edged ellipse where it lands
    float rim = length(q.xy) / r;
    float edge = 1.0 - smoothstep(0.35, 1.0, rim);

    vec3 finalColor = shadowMask * goboMask * edge * lightColor * lightIntensity * max(0, dot(normal, -lightDir)) * albedo;

    MGL_FRAG_COLOR = vec4(finalColor, 1.0);
    //MGL_FRAG_COLOR = vec4(1.0, 0.0, 0.0, 1.0);
}
