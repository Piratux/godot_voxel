#ifndef VOXEL_STREAM_MEMORY_H
#define VOXEL_STREAM_MEMORY_H

#include "../constants/voxel_constants.h"
#include "../util/containers/fixed_array.h"
#include "../util/containers/span.h"
#include "../util/memory.h"
#include "../util/thread/mutex.h"

#include "voxel_stream.h"
#include <unordered_map>

namespace zylann::voxel {

class VoxelBufferInternal;
struct InstanceBlockData;

// "fake" stream that just stores copies of the data in memory instead of saving them to the filesystem. May be used for
// testing.
class VoxelStreamMemory : public VoxelStream {
	GDCLASS(VoxelStreamMemory, VoxelStream)
public:
	void load_voxel_blocks(Span<VoxelQueryData> p_blocks) override;
	void save_voxel_blocks(Span<VoxelQueryData> p_blocks) override;
	void load_voxel_block(VoxelQueryData &query_data) override;
	void save_voxel_block(VoxelQueryData &query_data) override;

	bool supports_instance_blocks() const override;
	void load_instance_blocks(Span<InstancesQueryData> out_blocks) override;
	void save_instance_blocks(Span<InstancesQueryData> p_blocks) override;

	bool supports_loading_all_blocks() const override;
	void load_all_blocks(FullLoadingResult &result) override;

	int get_used_channels_mask() const override;

	int get_lod_count() const override;

private:
	static void _bind_methods();

	struct Lod {
		std::unordered_map<Vector3i, VoxelBufferInternal> voxel_blocks;
		std::unordered_map<Vector3i, InstanceBlockData> instance_blocks;
		Mutex mutex;
	};

	FixedArray<Lod, constants::MAX_LOD> _lods;
};

} // namespace zylann::voxel

#endif // VOXEL_STREAM_MEMORY_H