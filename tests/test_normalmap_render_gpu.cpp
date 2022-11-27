#include "../engine/generate_distance_normalmap_gpu_task.h"
#include "../engine/generate_distance_normalmap_task.h"
#include "../engine/mesh_block_task.h"
#include "../engine/voxel_engine.h"
#include "../generators/graph/voxel_generator_graph.h"
#include "../meshers/transvoxel/transvoxel_cell_iterator.h"
#include "../meshers/transvoxel/voxel_mesher_transvoxel.h"
#include "testing.h"

namespace zylann::voxel::tests {

void test_normalmap_render_gpu() {
	Ref<VoxelGeneratorGraph> generator;
	generator.instantiate();
	{
		pg::VoxelGraphFunction &g = **generator->get_main_function();

		// Flat plane
		// const uint32_t n_y = g.create_node(pg::VoxelGraphFunction::NODE_INPUT_Y, Vector2());
		// const uint32_t n_add = g.create_node(pg::VoxelGraphFunction::NODE_ADD, Vector2());
		// const uint32_t n_out_sd = g.create_node(pg::VoxelGraphFunction::NODE_OUTPUT_SDF, Vector2());
		// g.add_connection(n_y, 0, n_add, 0);
		// g.set_node_default_input(n_add, 1, -1.5f);
		// g.add_connection(n_add, 0, n_out_sd, 0);

		// Wavy plane
		// X --- Sin1 --- Add1 --- Add2 --- Add3 --- OutSDF
		//               /        /       /
		//     Z --- Sin2        Y     -3.5
		//
		const uint32_t n_x = g.create_node(pg::VoxelGraphFunction::NODE_INPUT_X, Vector2());
		const uint32_t n_y = g.create_node(pg::VoxelGraphFunction::NODE_INPUT_Y, Vector2());
		const uint32_t n_z = g.create_node(pg::VoxelGraphFunction::NODE_INPUT_Z, Vector2());
		const uint32_t n_add1 = g.create_node(pg::VoxelGraphFunction::NODE_ADD, Vector2());
		const uint32_t n_add2 = g.create_node(pg::VoxelGraphFunction::NODE_ADD, Vector2());
		const uint32_t n_add3 = g.create_node(pg::VoxelGraphFunction::NODE_ADD, Vector2());
		const uint32_t n_sin1 = g.create_node(pg::VoxelGraphFunction::NODE_SIN, Vector2());
		const uint32_t n_sin2 = g.create_node(pg::VoxelGraphFunction::NODE_SIN, Vector2());
		const uint32_t n_out_sd = g.create_node(pg::VoxelGraphFunction::NODE_OUTPUT_SDF, Vector2());
		g.add_connection(n_x, 0, n_sin1, 0);
		g.add_connection(n_z, 0, n_sin2, 0);
		g.add_connection(n_sin1, 0, n_add1, 0);
		g.add_connection(n_sin2, 0, n_add1, 1);
		g.add_connection(n_add1, 0, n_add2, 0);
		g.add_connection(n_y, 0, n_add2, 1);
		g.add_connection(n_add2, 0, n_add3, 0);
		g.set_node_default_input(n_add3, 1, -1.5f);
		g.add_connection(n_add3, 0, n_out_sd, 0);

		pg::CompilationResult result = generator->compile(false);
		ZN_TEST_ASSERT(result.success);
	}

	generator->compile_shaders();

	Ref<VoxelMesherTransvoxel> mesher;
	mesher.instantiate();

	const int block_size = 16;
	VoxelBufferInternal voxels;
	voxels.create(Vector3iUtil::create(block_size + mesher->get_minimum_padding() + mesher->get_maximum_padding()));

	const Vector3i origin_in_voxels;
	const uint8_t lod_index = 0;
	generator->generate_block(VoxelGenerator::VoxelQueryData{ voxels, origin_in_voxels, 0 });

	const VoxelMesher::Input mesher_input = { voxels, generator.ptr(), nullptr, origin_in_voxels, lod_index, false,
		false, true };
	VoxelMesher::Output mesher_output;
	mesher->build(mesher_output, mesher_input);

	const bool mesh_is_empty = VoxelMesher::is_mesh_empty(mesher_output.surfaces);
	ZN_TEST_ASSERT(!mesh_is_empty);

	const transvoxel::MeshArrays &mesh_arrays = VoxelMesherTransvoxel::get_mesh_cache_from_current_thread();
	Span<const transvoxel::CellInfo> cell_infos = VoxelMesherTransvoxel::get_cell_info_from_current_thread();
	ZN_ASSERT(cell_infos.size() > 0 && mesh_arrays.vertices.size() > 0);

	UniquePtr<TransvoxelCellIterator> cell_iterator = make_unique_instance<TransvoxelCellIterator>(cell_infos);

	std::shared_ptr<VirtualTextureOutput> virtual_textures = make_shared_instance<VirtualTextureOutput>();
	virtual_textures->valid = false;

	NormalMapSettings virtual_texture_settings;
	virtual_texture_settings.enabled = true;
	virtual_texture_settings.begin_lod_index = 0;
	virtual_texture_settings.max_deviation_degrees = 60;
	virtual_texture_settings.tile_resolution_min = 32;
	virtual_texture_settings.tile_resolution_max = 32;
	virtual_texture_settings.octahedral_encoding_enabled = false;

	GenerateDistanceNormalmapTask nm_task;
	nm_task.cell_iterator = std::move(cell_iterator);
	nm_task.mesh_vertices = mesh_arrays.vertices;
	nm_task.mesh_normals = mesh_arrays.normals;
	nm_task.mesh_indices = mesh_arrays.indices;
	nm_task.generator = generator;
	nm_task.voxel_data = nullptr;
	nm_task.mesh_block_size = Vector3iUtil::create(block_size);
	nm_task.lod_index = lod_index;
	nm_task.mesh_block_position = Vector3i();
	nm_task.volume_id = 0;
	nm_task.virtual_textures = virtual_textures;
	nm_task.virtual_texture_settings = virtual_texture_settings;
	nm_task.priority_dependency;
	nm_task.use_gpu = true;
	GenerateDistanceNormalMapGPUTask *gpu_task = nm_task.make_gpu_task();

	RenderingDevice *rd = VoxelEngine::get_singleton().get_rendering_device();
	ZN_TEST_ASSERT(rd != nullptr);

	GPUTaskContext gpu_task_context{ *rd };
	gpu_task->prepare(gpu_task_context);

	rd->submit();
	rd->sync();

	const PackedByteArray atlas_texture_data = gpu_task->collect_texture_and_cleanup(*rd);

	Ref<Image> atlas_image = Image::create_from_data(
			gpu_task->texture_width, gpu_task->texture_height, false, Image::FORMAT_RGBA8, atlas_texture_data);
	ERR_FAIL_COND(atlas_image.is_null());
	atlas_image->convert(Image::FORMAT_RGB8);

	atlas_image->save_png("test_gpu_normalmap.png");

	ZN_DELETE(gpu_task);

	// Make a comparison with the CPU version

	nm_task.cell_iterator->rewind();
	NormalMapData normalmap_data;

	compute_normalmap(*nm_task.cell_iterator, to_span(nm_task.mesh_vertices), to_span(nm_task.mesh_normals),
			to_span(nm_task.mesh_indices), normalmap_data, virtual_texture_settings.tile_resolution_min, **generator,
			nm_task.voxel_data.get(), origin_in_voxels, lod_index, virtual_texture_settings.octahedral_encoding_enabled,
			math::deg_to_rad(float(virtual_texture_settings.max_deviation_degrees)));

	NormalMapImages images =
			store_normalmap_data_to_images(normalmap_data, virtual_texture_settings.tile_resolution_min,
					nm_task.mesh_block_size, virtual_texture_settings.octahedral_encoding_enabled);
	ZN_ASSERT(images.atlas.is_valid());
	images.atlas->save_png("test_cpu_normalmap.png");
}

} // namespace zylann::voxel::tests
