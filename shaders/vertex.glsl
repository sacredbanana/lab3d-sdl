#version 330 core

layout (location = 0) in vec3 aPos;
// layout (location = 1) in float aColorIndex; // This should correspond to the attribute in your VAO
// layout (location = 2) in float tx1;
// layout (location = 3) in float ty1;
// layout (location = 4) in float tx2;
// layout (location = 5) in float ty2;
// layout (location = 6) in vec4 projection;

// out float colorIndex;  // Output variable to fragment shader
// out float txx1;
// out float tyy1;
// out float txx2;
// out float tyy2;
out vec3 FragPos;                    // Output position to fragment shader

void main()
{
    gl_Position = vec4(aPos, 1.0);
    // colorIndex = aColorIndex; // Set the value that will be passed to the fragment shader
    // txx1 = tx1;
    // tyy1 = ty1;
    // txx2 = tx2;
    // tyy2 = ty2;
    FragPos = aPos;
}