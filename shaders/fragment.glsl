#version 330 core

uniform sampler1D paletteTexture; // Your palette as a 1D texture
uniform sampler2D myTexture;
uniform vec3 baseColor;

in vec2 TexCoord; 
in vec3 FragPos;  // Position input from vertex shader

out vec4 FragColor;

void main()
{
    // vec4 color = texture(paletteTexture, colorIndex / 255.0);
    vec4 textureColor = texture(myTexture, TexCoord);

    vec4 color = textureColor;
    color.rgb *= baseColor;

    if(color.a < 0.99)
        discard;
    
    FragColor = color;
}