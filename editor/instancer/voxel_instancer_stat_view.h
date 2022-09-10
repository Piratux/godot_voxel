#ifndef VOXEL_INSTANCER_STAT_VIEW_H
#define VOXEL_INSTANCER_STAT_VIEW_H

#include "../../terrain/instancing/voxel_instancer.h"

#include <scene/gui/box_container.h>
#include <scene/gui/tree.h>

namespace zylann::voxel {

class VoxelInstancerStatView : public VBoxContainer {
	GDCLASS(VoxelInstancerStatView, VBoxContainer)
public:
	VoxelInstancerStatView() {
		_tree = memnew(Tree);
		_tree->set_columns(2);
		_tree->set_select_mode(Tree::SELECT_ROW);
		_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		_tree->set_column_expand_ratio(0, 1);
		_tree->set_column_expand_ratio(1, 3);
		add_child(_tree);
	}

	void set_instancer(const VoxelInstancer *instancer) {
		_instancer = instancer;
		set_process(_instancer != nullptr);
	}

private:
	void _notification(int p_what) {
		switch (p_what) {
			case NOTIFICATION_PROCESS:
				_process();
				break;
		}
	}

	void _process() {
		ERR_FAIL_COND(_instancer == nullptr);
		const VoxelInstancer &instancer = *_instancer;

		Ref<VoxelInstanceLibrary> library = instancer.get_library();

		instancer.debug_get_instance_counts(_count_per_layer);

		_tree->clear();

		if (library.is_null()) {
			return;
		}

		TreeItem *root = _tree->create_item();
		_tree->set_hide_root(true);

		for (auto it = _count_per_layer.begin(); it != _count_per_layer.end(); ++it) {
			TreeItem *tree_item = _tree->create_item(root);
			const VoxelInstanceLibraryItem *lib_item = library->get_item_const(it->first);
			ERR_CONTINUE(lib_item == nullptr);

			String name = lib_item->get_item_name();
			if (name == "") {
				name = "[" + String::num_int64(it->first) + "]";
			}

			tree_item->set_text(0, name);
			tree_item->set_text(1, String::num_int64(it->second));
		}
	}

	Tree *_tree = nullptr;
	const VoxelInstancer *_instancer = nullptr;
	std::unordered_map<uint32_t, uint32_t> _count_per_layer;
};

} // namespace zylann::voxel

#endif // VOXEL_INSTANCER_STAT_VIEW_H
