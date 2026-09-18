#version 450

layout(location = 0) out vec2 uv;

void main()
{
	/* fullscreen triangle: 3 vertices, no vertex buffer needed */
	vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	/* flip V: our texture upload's row 0 is the top row, but this ends up
	 * meeting Vulkan's NDC (Y+ down) inverted otherwise -- shared by every
	 * fragment shader that samples the composited frame via this vertex
	 * shader, so they can all just use vTexCoord/uv directly */
	uv = vec2(pos.x, 1.0 - pos.y);
	gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
