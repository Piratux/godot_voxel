#ifndef PRIORITY_DEPENDENCY_H
#define PRIORITY_DEPENDENCY_H

#include "../util/math/vector3f.h"
#include "../util/tasks/task_priority.h"
#include <atomic>
#include <memory>
#include <vector>

namespace zylann::voxel {

// Information to calculate the priority of a voxel task having a specific location
struct PriorityDependency {
	struct ViewersData {
		// These positions are written by the main thread and read by block processing threads.
		// Order doesn't matter.
		// It's only used to adjust task priority so using a lock isn't worth it. In worst case scenario,
		// a task will run much sooner or later than expected, but it will run in any case.
		// This vector is never resized after the instance is created. It is just big enough to have room for all
		// viewers.
		std::vector<Vector3f> viewers;
		// Use this count instead of `viewers.size()`. Can change, but will always be <= `viewers.size()`
		std::atomic_uint32_t viewers_count;
		float highest_view_distance = 999999;
	};

	std::shared_ptr<ViewersData> shared;
	// Position relative to the same space as viewers.
	// TODO Won't update while in queue. Can it be bad?
	Vector3f world_position;

	// If the closest viewer is further away than this distance, the request can be cancelled as not worth it
	float drop_distance_squared;

	TaskPriority evaluate(uint8_t lod_index, uint8_t band2_priority, float *out_closest_distance_sq);
};

} // namespace zylann::voxel

#endif // PRIORITY_DEPENDENCY_H
