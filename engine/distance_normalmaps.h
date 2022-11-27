#ifndef VOXEL_DISTANCE_NORMALMAPS_H
#define VOXEL_DISTANCE_NORMALMAPS_H

#include "../util/fixed_array.h"
#include "../util/godot/ref_counted.h"
#include "../util/macros.h"
#include "../util/math/vector3f.h"
#include "../util/span.h"

//#define VOXEL_VIRTUAL_TEXTURE_USE_TEXTURE_ARRAY
// Texture arrays are handy but the maximum amount of layers is often too low (2048 on an nVidia 1060), which happens
// too frequently with block size 32. So instead we have to keep using 2D atlases with padding.
#ifdef VOXEL_VIRTUAL_TEXTURE_USE_TEXTURE_ARRAY
#include "../util/godot/texture_array.h"
#endif
#include "../util/godot/image.h"
#include "../util/godot/texture_2d.h"

#include <vector>

namespace zylann::voxel {

class VoxelGenerator;
class VoxelData;

// TODO This system could be extended to more than just normals
// - Texturing data
// - Color
// - Some kind of depth (could be useful to fake water from far away)

// UV-mapping a voxel mesh is not trivial, but if mapping is required, an alternative is to subdivide the mesh into a
// grid of cells (we can use Transvoxel cells). In each cell, pick an axis-aligned projection working best with
// triangles of the cell using the average of their normals. A tile can then be generated by projecting its pixels on
// triangles, and be stored in an atlas. A shader can then read the atlas using a lookup texture to find the tile.

struct NormalMapSettings {
	// If enabled, an atlas of normalmaps will be generated for each cell of the voxel mesh, in order to add
	// more visual details using a shader.
	bool enabled = false;
	// LOD index from which normalmaps will start being generated.
	uint8_t begin_lod_index = 2;
	// Tile resolution that will be used starting from the beginning LOD. Resolution will double at each following
	// LOD index.
	uint8_t tile_resolution_min = 4;
	uint8_t tile_resolution_max = 8;
	// If the angle between geometry normals and computed normals exceeds this angle, their direction will be clamped.
	uint8_t max_deviation_degrees = 60;
	// If enabled, encodes normalmaps using octahedral compression, which trades a bit of quality for
	// significantly reduced memory usage (using 2 bytes per pixel instead of 3).
	bool octahedral_encoding_enabled = false;

	static constexpr uint8_t MIN_DEVIATION_DEGREES = 1;
	static constexpr uint8_t MAX_DEVIATION_DEGREES = 179;
};

unsigned int get_virtual_texture_tile_resolution_for_lod(const NormalMapSettings &settings, unsigned int lod_index);

struct NormalMapData {
	// Encoded normals
	std::vector<uint8_t> normals;
	struct Tile {
		uint8_t x;
		uint8_t y;
		uint8_t z;
		uint8_t axis;
	};
	std::vector<Tile> tiles;

	inline void clear() {
		normals.clear();
		tiles.clear();
	}
};

// To hold the current cell only. Not optimized for space. May use a more efficient structure per implementation of
// `ICellIterator`.
struct CurrentCellInfo {
	static const unsigned int MAX_TRIANGLES = 5;
	FixedArray<uint32_t, MAX_TRIANGLES> triangle_begin_indices;
	uint32_t triangle_count = 0;
	Vector3i position;
};

class ICellIterator {
public:
	virtual ~ICellIterator() {}
	virtual unsigned int get_count() const = 0;
	virtual bool next(CurrentCellInfo &info) = 0;
	virtual void rewind() = 0;
};

// For each non-empty cell of the mesh, choose an axis-aligned projection based on triangle normals in the cell.
// Sample voxels inside the cell to compute a tile of world space normals from the SDF.
// If the angle between the triangle and the computed normal is larger tham `max_deviation_radians`,
// the normal's direction will be clamped.
void compute_normalmap(ICellIterator &cell_iterator, Span<const Vector3f> mesh_vertices,
		Span<const Vector3f> mesh_normals, Span<const int> mesh_indices, NormalMapData &normal_map_data,
		unsigned int tile_resolution, VoxelGenerator &generator, const VoxelData *voxel_data, Vector3i origin_in_voxels,
		unsigned int lod_index, bool octahedral_encoding, float max_deviation_radians);

struct NormalMapImages {
#ifdef VOXEL_VIRTUAL_TEXTURE_USE_TEXTURE_ARRAY
	Vector<Ref<Image>> atlas;
#else
	Ref<Image> atlas;
#endif
	Ref<Image> lookup;
};

struct NormalMapTextures {
#ifdef VOXEL_VIRTUAL_TEXTURE_USE_TEXTURE_ARRAY
	Ref<Texture2DArray> atlas;
#else
	Ref<Texture2D> atlas;
#endif
	Ref<Texture2D> lookup;
};

Ref<Image> store_lookup_to_image(const std::vector<NormalMapData::Tile> &tiles, Vector3i block_size);

NormalMapImages store_normalmap_data_to_images(
		const NormalMapData &data, unsigned int tile_resolution, Vector3i block_size, bool octahedral_encoding);

// Converts normalmap data into textures. They can be used in a shader to apply normals and obtain extra visual details.
// This may not be allowed to run in a different thread than the main thread if the renderer is not using Vulkan.
NormalMapTextures store_normalmap_data_to_textures(const NormalMapImages &data);

struct VirtualTextureOutput {
	// Normalmap atlas used for smooth voxels.
	// If textures can't be created from threads, images are returned instead.
	NormalMapImages normalmap_images;
	NormalMapTextures normalmap_textures;
	// Can be false if textures are computed asynchronously. Will become true when it's done (and not change after).
	std::atomic_bool valid;
};

// Given a number of items, tells which size a 2D square grid should be in order to contain them
inline unsigned int get_square_grid_size_from_item_count(unsigned int item_count) {
	return int(Math::ceil(Math::sqrt(double(item_count))));
}

} // namespace zylann::voxel

#endif // VOXEL_DISTANCE_NORMALMAPS_H
