#version 330 core

uniform sampler1D paletteTexture; // Your palette as a 1D texture
uniform sampler2D texture;
in float colorIndex; // The index to lookup in your palette
in float txx1;
in float tyy1;
in float txx2;
in float tyy2;
in vec3 FragPos;  // Position input from vertex shader


out vec4 FragColor;

void main()
{
    vec4 color = texture(paletteTexture, colorIndex / 255.0);
    color = texture(texture, FragPos.xy);

    if(color.a < 0.99)
        discard;
    
    FragColor = color;
}