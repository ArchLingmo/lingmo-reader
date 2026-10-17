// SPDX-FileCopyrightText: 2026 LingmoOS Team
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading modes for a rendered page: 1 = night (lightness inverted, hues
// kept, white paper becomes dark grey), 2 = sepia (warm paper, brown ink).
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float mode;
    vec4 paper;
    vec4 ink;
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    vec4 c = texture(source, qt_TexCoord0);
    vec3 rgb = c.a > 0.0 ? c.rgb / c.a : c.rgb;
    float luma = dot(rgb, vec3(0.299, 0.587, 0.114));

    if (mode > 1.5) {
        // Sepia: paper colour where the page is white, ink where it is black
        vec3 tinted = mix(ink.rgb, paper.rgb, luma);
        rgb = mix(tinted, rgb * paper.rgb, 0.35 * (1.0 - abs(luma * 2.0 - 1.0)));
    } else if (mode > 0.5) {
        // Night: flip lightness, keep the colour, then squeeze into paper..ink
        vec3 flipped = clamp(rgb + vec3(1.0 - 2.0 * luma), 0.0, 1.0);
        rgb = mix(paper.rgb, ink.rgb, dot(flipped, vec3(0.299, 0.587, 0.114))) + (flipped - vec3(dot(flipped, vec3(0.299, 0.587, 0.114)))) * 0.85;
    }
    fragColor = vec4(rgb * c.a, c.a) * qt_Opacity;
}
