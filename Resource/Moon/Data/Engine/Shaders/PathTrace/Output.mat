#shader vertex
#version 450 core

layout (location = 0) in vec2 position;
layout (location = 1) in vec2 texCoords;

out vec2 TexCoords;

void main()
{
    gl_Position = vec4(position.x,position.y,0.0,1.0);
    TexCoords = texCoords;
}


#shader fragment
#version 450 core

out vec4 color;
in vec2 TexCoords;
uniform sampler2D imgTex;
void main()
{
    color = texture(imgTex, TexCoords);
}