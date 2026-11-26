layout(location = 0) in vec3 aPos;
layout(location = 1) in float aAlpha;
out vec3 vPos;
out float vAlpha;
void main() { vPos = aPos; vAlpha = aAlpha; gl_Position = vec4(aPos, 1.0); }
