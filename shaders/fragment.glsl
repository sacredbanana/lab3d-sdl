#version 330 core

uniform sampler1D paletteTexture; // Your palette as a 1D texture
uniform sampler2D myTexture;
uniform vec3 baseColor;
uniform bool useTexture;

in vec2 TexCoord; 
in vec3 FragPos;  // Position input from vertex shader

out vec4 FragColor;

void main()
{
    // vec4 color = texture(paletteTexture, colorIndex / 255.0);
    vec4 color;

    if (useTexture) {
        vec4 textureColor = texture(myTexture, TexCoord);

        color = textureColor;
        color.rgb *= baseColor;

        if(color.a < 0.99)
            discard;
        }
    else {
        color = vec4(baseColor, 1.);
    }
    
    FragColor = color;
}