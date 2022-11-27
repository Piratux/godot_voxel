// Generated file

// clang-format off
const char *g_render_normalmap_shader_template_0 = 
"#version 450\n"
"\n"
"#define MAX_TRIANGLES_PER_CELL 5\n"
"\n"
"layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"\n"
"// TODO When should I use `restrict` or not?\n"
"\n"
"// Output image.\n"
"// For now we write only to RGB8, but according to\n"
"// https://www.khronos.org/opengl/wiki/Layout_Qualifier_(GLSL),\n"
"// there doesn't seem to be a format for RGB8.\n"
"// So maybe we could use A for something custom in the future.\n"
"layout (set = 0, binding = 0, rgba8ui) writeonly uniform uimage2D u_target_image;\n"
"\n"
"layout (set = 0, binding = 1, std430) restrict buffer MeshVertices {\n"
"	vec3 data[];\n"
"} u_vertices;\n"
"\n"
"layout (set = 0, binding = 2, std430) restrict buffer MeshIndices {\n"
"	int data[];\n"
"} u_indices;\n"
"\n"
"layout (set = 0, binding = 3, std430) restrict buffer CellTris {\n"
"	// List of triangle indices.\n"
"	// Grouped in chunks corresponding to triangles within a tile.\n"
"	// Each chunk can have up to 5 triangle indices.\n"
"	int data[];\n"
"} u_cell_tris;\n"
"\n"
"layout (set = 0, binding = 4, std430) restrict buffer AtlasInfo {\n"
"	// [tile index] => cell info\n"
"	// X:\n"
"	// Packed 8-bit coordinates of the cell.\n"
"	// Y:\n"
"	// aaaaaaaa aaaaaaaa aaaaaaaa 0bbb00cc\n"
"	// a: 24-bit index into `u_cell_tris.data` array.\n"
"	// b: 3-bit number of triangles.\n"
"	// c: 2-bit projection direction (0:X, 1:Y, 2:Z)\n"
"	// Global invocation X and Y tell which pixel we are in.\n"
"	ivec2 data[];\n"
"} u_tile_data;\n"
"\n"
"layout (set = 0, binding = 5, std430) restrict buffer Params {\n"
"	vec3 block_origin_world;\n"
"	// How big is a pixel of the atlas in world space\n"
"	float pixel_world_step;\n"
"	// cos(max_deviation_angle)\n"
"	float max_deviation_cosine;\n"
"	// sin(max_deviation_angle)\n"
"	float max_deviation_sine;\n"
"\n"
"	int tile_size_pixels;\n"
"	int tiles_x;\n"
"	int tiles_y;\n"
"} u_params;\n"
"\n"
"// This part contains functions we expect from the GLSL version of the voxel generator.\n";
// clang-format on

// clang-format off
const char *g_render_normalmap_shader_template_1 = 
"\n"
"float get_sd(vec3 pos) {\n"
"	return generate_sdf(pos);\n"
"}\n"
"\n"
"vec3 get_sd_normal(vec3 pos, float s) {\n"
"	float sd000 = get_sd(pos);\n"
"	float sd100 = get_sd(pos + vec3(s, 0.0, 0.0));\n"
"	float sd010 = get_sd(pos + vec3(0.0, s, 0.0));\n"
"	float sd001 = get_sd(pos + vec3(0.0, 0.0, s));\n"
"	vec3 normal = normalize(vec3(sd100 - sd000, sd010 - sd000, sd001 - sd000));\n"
"	return normal;\n"
"}\n"
"\n"
"const int TRI_NO_INTERSECTION = 0;\n"
"const int TRI_PARALLEL = 1;\n"
"const int TRI_INTERSECTION = 2;\n"
"\n"
"int ray_intersects_triangle(vec3 p_from, vec3 p_dir, vec3 p_v0, vec3 p_v1, vec3 p_v2, out float out_distance) {\n"
"	const vec3 e1 = p_v1 - p_v0;\n"
"	const vec3 e2 = p_v2 - p_v0;\n"
"	const vec3 h = cross(p_dir, e2);\n"
"	const float a = dot(e1, h);\n"
"\n"
"	if (abs(a) < 0.00001) {\n"
"		out_distance = -1.0;\n"
"		return TRI_PARALLEL;\n"
"	}\n"
"\n"
"	const float f = 1.0f / a;\n"
"\n"
"	const vec3 s = p_from - p_v0;\n"
"	const float u = f * dot(s, h);\n"
"\n"
"	if ((u < 0.0) || (u > 1.0)) {\n"
"		out_distance = -1.0;\n"
"		return TRI_NO_INTERSECTION;\n"
"	}\n"
"\n"
"	const vec3 q = cross(s, e1);\n"
"\n"
"	const float v = f * dot(p_dir, q);\n"
"\n"
"	if ((v < 0.0) || (u + v > 1.0)) {\n"
"		out_distance = -1.0;\n"
"		return TRI_NO_INTERSECTION;\n"
"	}\n"
"\n"
"	// At this stage we can compute t to find out where\n"
"	// the intersection point is on the line.\n"
"	const float t = f * dot(e2, q);\n"
"\n"
"	if (t > 0.00001) { // ray intersection\n"
"		//r_res = p_from + p_dir * t;\n"
"		out_distance = t;\n"
"		return TRI_INTERSECTION;\n"
"\n"
"	} else { // This means that there is a line intersection but not a ray intersection.\n"
"		out_distance = -1.0;\n"
"		return TRI_NO_INTERSECTION;\n"
"	}\n"
"}\n"
"\n"
"vec3 get_triangle_normal(vec3 v0, vec3 v1, vec3 v2) {\n"
"	vec3 e1 = v1 - v0;\n"
"	vec3 e2 = v2 - v0;\n"
"	return normalize(cross(e2, e1));\n"
"}\n"
"\n"
"mat3 basis_from_axis_angle_cs(vec3 p_axis, float cosine, float sine) {\n"
"	// Rotation matrix from axis and angle, see\n"
"	// https://en.wikipedia.org/wiki/Rotation_matrix#Rotation_matrix_from_axis_angle\n"
"\n"
"	mat3 cols;\n"
"\n"
"	const vec3 axis_sq = vec3(p_axis.x * p_axis.x, p_axis.y * p_axis.y, p_axis.z * p_axis.z);\n"
"	cols[0][0] = axis_sq.x + cosine * (1.0f - axis_sq.x);\n"
"	cols[1][1] = axis_sq.y + cosine * (1.0f - axis_sq.y);\n"
"	cols[2][2] = axis_sq.z + cosine * (1.0f - axis_sq.z);\n"
"\n"
"	const float t = 1 - cosine;\n"
"\n"
"	float xyzt = p_axis.x * p_axis.y * t;\n"
"	float zyxs = p_axis.z * sine;\n"
"	cols[1][0] = xyzt - zyxs;\n"
"	cols[0][1] = xyzt + zyxs;\n"
"\n"
"	xyzt = p_axis.x * p_axis.z * t;\n"
"	zyxs = p_axis.y * sine;\n"
"	cols[2][0] = xyzt + zyxs;\n"
"	cols[0][2] = xyzt - zyxs;\n"
"\n"
"	xyzt = p_axis.y * p_axis.z * t;\n"
"	zyxs = p_axis.x * sine;\n"
"	cols[2][1] = xyzt - zyxs;\n"
"	cols[1][2] = xyzt + zyxs;\n"
"\n"
"	return cols;\n"
"}\n"
"\n"
"vec3 rotate_vec3_cs(const vec3 v, const vec3 axis, float cosine, float sine) {\n"
"	return basis_from_axis_angle_cs(axis, cosine, sine) * v;\n"
"}\n"
"\n"
"void main() {\n"
"	const ivec2 pixel_pos = ivec2(gl_GlobalInvocationID.xy);\n"
"\n"
"	const ivec2 tile_pos = pixel_pos / u_params.tile_size_pixels;\n"
"	const int tile_index = tile_pos.x + tile_pos.y * u_params.tiles_x;\n"
"\n"
"	const ivec2 tile_data = u_tile_data.data[tile_index];\n"
"	const int tri_count = (tile_data.y >> 4) & 0x7;\n"
"\n"
"	if (tri_count == 0) {\n"
"		//imageStore(u_target_image, pixel_pos, ivec4(255,0,255,255));\n"
"		return;\n"
"	}\n"
"\n"
"	const int tri_info_begin = tile_data.y >> 8;\n"
"	const int projection = tile_data.y & 0x3;\n"
"\n"
"	const int packed_cell_pos = tile_data.x;//u_tile_cell_positions.data[tile_index];\n"
"	const vec3 cell_pos_cells = vec3(\n"
"		packed_cell_pos & 0xff,\n"
"		(packed_cell_pos >> 8) & 0xff,\n"
"		(packed_cell_pos >> 16) & 0xff\n"
"	);\n"
"	const float cell_size_world = u_params.pixel_world_step * float(u_params.tile_size_pixels);\n"
"	const vec3 cell_origin_mesh = cell_size_world * cell_pos_cells;\n"
"\n"
"	// Choose a basis where Z is the axis we cast the ray. X and Y are lateral axes of the tile.\n"
"	const vec3 ray_dir = vec3(float(projection == 0), float(projection == 1), float(projection == 2));\n"
"	const vec3 dx = vec3(float(projection == 1 || projection == 2), 0.0, float(projection == 0));\n"
"	const vec3 dy = vec3(0.0, float(projection == 0 || projection == 2), float(projection == 1));\n"
"\n"
"	const ivec2 pixel_pos_in_tile = pixel_pos - tile_pos * u_params.tile_size_pixels;\n"
"	const vec2 pos_in_tile = u_params.pixel_world_step * vec2(pixel_pos_in_tile);\n"
"	const vec3 ray_origin_mesh = cell_origin_mesh\n"
"		 - 1.01 * ray_dir * cell_size_world\n"
"		 + pos_in_tile.x * dx + pos_in_tile.y * dy;\n"
"\n"
"	// Find closest hit triangle\n"
"	const float no_hit_distance = 999999.0;\n"
"	float nearest_hit_distance = no_hit_distance;\n"
"	vec3 tri_v0;\n"
"	vec3 tri_v1;\n"
"	vec3 tri_v2;\n"
"	for (int i = 0; i < tri_count; ++i) {\n"
"		const int tri_index = u_cell_tris.data[tri_info_begin + i];\n"
"		const int ii0 = tri_index * 3;\n"
"		\n"
"		const int i0 = u_indices.data[ii0];\n"
"		const int i1 = u_indices.data[ii0 + 1];\n"
"		const int i2 = u_indices.data[ii0 + 2];\n"
"\n"
"		const vec3 v0 = u_vertices.data[i0];\n"
"		const vec3 v1 = u_vertices.data[i1];\n"
"		const vec3 v2 = u_vertices.data[i2];\n"
"\n"
"		float hit_distance;\n"
"		const int intersection_result = ray_intersects_triangle(ray_origin_mesh, ray_dir, v0, v1, v2, hit_distance);\n"
"\n"
"		if (intersection_result == TRI_INTERSECTION && hit_distance < nearest_hit_distance) {\n"
"			nearest_hit_distance = hit_distance;\n"
"			tri_v0 = v0;\n"
"			tri_v1 = v1;\n"
"			tri_v2 = v2;\n"
"		}\n"
"	}\n"
"\n"
"	ivec4 col = ivec4(127, 127, 127, 255);\n"
"\n"
"	if (nearest_hit_distance != no_hit_distance) {\n"
"		const vec3 hit_pos_world = ray_origin_mesh + ray_dir * nearest_hit_distance + u_params.block_origin_world;\n"
"\n"
"		const vec3 sd_normal = get_sd_normal(hit_pos_world, u_params.pixel_world_step);\n"
"		const vec3 tri_normal = get_triangle_normal(tri_v0, tri_v1, tri_v2);\n"
"\n"
"		vec3 normal = sd_normal;\n"
"\n"
"		// Clamp normal if it deviates too much\n"
"		const float tdot = dot(sd_normal, tri_normal);\n"
"		if (tdot < u_params.max_deviation_cosine) {\n"
"			if (tdot < -0.999) {\n"
"				normal = tri_normal;\n"
"			} else {\n"
"				const vec3 axis = normalize(cross(tri_normal, normal));\n"
"				normal = rotate_vec3_cs(tri_normal, axis, \n"
"					u_params.max_deviation_cosine, \n"
"					u_params.max_deviation_sine);\n"
"			}\n"
"		}\n"
"\n"
"		col = ivec4(255.0 * (vec3(0.5) + 0.5 * normal), 255);\n"
"	}\n"
"\n"
"	imageStore(u_target_image, pixel_pos, col);\n"
"}\n"
"\n";
// clang-format on
