#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
uniform mat4 model;
uniform mat4 projection;
// layout (location = 1) in float aColorIndex; // This should correspond to the attribute in your VAO

// out float colorIndex;  // Output variable to fragment shader

out vec2 TexCoord;
out vec3 FragPos;

void main()
{
    gl_Position = projection * model * vec4(aPos, 1.0);
    // colorIndex = aColorIndex; // Set the value that will be passed to the fragment shader

    TexCoord = aTexCoord;
    FragPos = aPos;
}