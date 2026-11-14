/*
  Q Light Controller Plus
  gamma_correct.frag

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

FS_IN_ATTRIB vec2 fsUv;

DECLARE_FRAG_COLOR

uniform sampler2D hdrTex;
uniform sampler2D bloomTex;
uniform sampler2D scatterTex;

void main()
{
    vec3 hdrColor = SAMPLE_TEX2D(hdrTex, fsUv).rgb;
    hdrColor += 0.5 * SAMPLE_TEX2D(bloomTex, fsUv).rgb;
    // The beams in the air: half size, stretched over the frame. Five taps
    // spread the grain that the ray marching starts each ray with, which
    // shows more now that every sample covers four pixels.
    vec2 st = vec2(1.0 / 512.0);
    vec3 beams = SAMPLE_TEX2D(scatterTex, fsUv).rgb * 0.4;
    beams += SAMPLE_TEX2D(scatterTex, fsUv + vec2( st.x,  st.y)).rgb * 0.15;
    beams += SAMPLE_TEX2D(scatterTex, fsUv + vec2(-st.x,  st.y)).rgb * 0.15;
    beams += SAMPLE_TEX2D(scatterTex, fsUv + vec2( st.x, -st.y)).rgb * 0.15;
    beams += SAMPLE_TEX2D(scatterTex, fsUv + vec2(-st.x, -st.y)).rgb * 0.15;
    hdrColor += beams;
    vec3 finalColor = vec3(1.0) - exp(-hdrColor * 1.0);

    MGL_FRAG_COLOR = vec4(pow(finalColor, vec3(1.0 / 2.2)), 1.0);
}
