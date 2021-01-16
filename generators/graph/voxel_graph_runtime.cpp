#include "voxel_graph_runtime.h"
#include "../../util/macros.h"
#include "../../util/noise/fast_noise_lite.h"
#include "../../util/profiling.h"
#include "image_range_grid.h"
#include "range_utility.h"
#include "voxel_generator_graph.h"
#include "voxel_graph_node_db.h"

//#include <core/image.h>
#include <core/math/math_funcs.h>
#include <modules/opensimplex/open_simplex_noise.h>
#include <scene/resources/curve.h>
#include <unordered_set>

//#ifdef DEBUG_ENABLED
//#define VOXEL_DEBUG_GRAPH_PROG_SENTINEL uint16_t(12345) // 48, 57 (base 10)
//#endif

template <typename T>
inline const T &read(const ArraySlice<const uint8_t> &mem, uint32_t &p) {
#ifdef DEBUG_ENABLED
	CRASH_COND(p + sizeof(T) > mem.size());
#endif
	const T *v = reinterpret_cast<const T *>(&mem[p]);
	p += sizeof(T);
	return *v;
}

template <typename T>
inline void append(std::vector<uint8_t> &mem, const T &v) {
	size_t p = mem.size();
	mem.resize(p + sizeof(T));
	*(T *)(&mem[p]) = v;
}

VoxelGraphRuntime::VoxelGraphRuntime() {
	clear();
}

VoxelGraphRuntime::~VoxelGraphRuntime() {
	clear();
}

void VoxelGraphRuntime::clear() {
	_program.clear();
}

VoxelGraphRuntime::CompilationResult VoxelGraphRuntime::compile(const ProgramGraph &graph, bool debug) {
	VoxelGraphRuntime::CompilationResult result = _compile(graph, debug);
	if (!result.success) {
		clear();
	}
	return result;
}

VoxelGraphRuntime::CompilationResult VoxelGraphRuntime::_compile(const ProgramGraph &graph, bool debug) {
	clear();

	std::vector<uint32_t> order;
	std::vector<uint32_t> terminal_nodes;

	graph.find_terminal_nodes(terminal_nodes);

	if (!debug) {
		// Exclude debug nodes
		unordered_remove_if(terminal_nodes, [&graph](uint32_t node_id) {
			const ProgramGraph::Node *node = graph.get_node(node_id);
			const VoxelGraphNodeDB::NodeType &type = VoxelGraphNodeDB::get_singleton()->get_type(node->type_id);
			return type.debug_only;
		});
	}

	graph.find_dependencies(terminal_nodes, order);

	uint32_t xzy_start_index = 0;

	// Optimize parts of the graph that only depend on X and Z,
	// so they can be moved in the outer loop when blocks are generated, running less times.
	// Moves them all at the beginning.
	{
		std::vector<uint32_t> immediate_deps;
		std::unordered_set<uint32_t> nodes_depending_on_y;
		std::vector<uint32_t> order_xz;
		std::vector<uint32_t> order_xzy;

		for (size_t i = 0; i < order.size(); ++i) {
			const uint32_t node_id = order[i];
			const ProgramGraph::Node *node = graph.get_node(node_id);

			bool depends_on_y = false;

			if (node->type_id == VoxelGeneratorGraph::NODE_INPUT_Y) {
				nodes_depending_on_y.insert(node_id);
				depends_on_y = true;
			}

			if (!depends_on_y) {
				immediate_deps.clear();
				graph.find_immediate_dependencies(node_id, immediate_deps);

				for (size_t j = 0; j < immediate_deps.size(); ++j) {
					const uint32_t dep_node_id = immediate_deps[j];

					if (nodes_depending_on_y.find(dep_node_id) != nodes_depending_on_y.end()) {
						depends_on_y = true;
						nodes_depending_on_y.insert(node_id);
						break;
					}
				}
			}

			if (depends_on_y) {
				order_xzy.push_back(node_id);
			} else {
				order_xz.push_back(node_id);
			}
		}

		xzy_start_index = order_xz.size();

		//#ifdef DEBUG_ENABLED
		//		const uint32_t order_xz_raw_size = order_xz.size();
		//		const uint32_t *order_xz_raw = order_xz.data();
		//		const uint32_t order_xzy_raw_size = order_xzy.size();
		//		const uint32_t *order_xzy_raw = order_xzy.data();
		//#endif

		size_t i = 0;
		for (size_t j = 0; j < order_xz.size(); ++j) {
			order[i++] = order_xz[j];
		}
		for (size_t j = 0; j < order_xzy.size(); ++j) {
			order[i++] = order_xzy[j];
		}
	}

	//#ifdef DEBUG_ENABLED
	//	const uint32_t order_raw_size = order.size();
	//	const uint32_t *order_raw = order.data();
	//#endif

	struct MemoryHelper {
		std::vector<uint16_t> &bindings;
		std::vector<Constant> &constants;
		unsigned int next_address = 0;

		uint16_t add_binding() {
			const unsigned int a = next_address;
			++next_address;
			bindings.push_back(a);
			return a;
		}

		uint16_t add_var() {
			const unsigned int a = next_address;
			++next_address;
			return a;
		}

		uint16_t add_constant(float v) {
			const unsigned int a = next_address;
			++next_address;
			Constant c;
			c.address = a;
			c.value = v;
			constants.push_back(c);
			return a;
		}
	};

	MemoryHelper mem{ _program.bindings, _program.constants };

	// Main inputs X, Y, Z
	_program.x_input_address = mem.add_binding();
	_program.y_input_address = mem.add_binding();
	_program.z_input_address = mem.add_binding();

	std::vector<uint8_t> &operations = _program.operations;
	const VoxelGraphNodeDB &type_db = *VoxelGraphNodeDB::get_singleton();

	// Run through each node in order, and turn them into program instructions
	for (size_t i = 0; i < order.size(); ++i) {
		const uint32_t node_id = order[i];
		const ProgramGraph::Node *node = graph.get_node(node_id);
		const VoxelGraphNodeDB::NodeType &type = type_db.get_type(node->type_id);

		CRASH_COND(node == nullptr);
		CRASH_COND(node->inputs.size() != type.inputs.size());
		CRASH_COND(node->outputs.size() != type.outputs.size());

		if (i == xzy_start_index) {
			_program.xzy_start = operations.size();
		}

		// We still hardcode some of the nodes. Maybe we can abstract them too one day.
		switch (node->type_id) {
			case VoxelGeneratorGraph::NODE_CONSTANT: {
				CRASH_COND(type.outputs.size() != 1);
				CRASH_COND(type.params.size() != 1);
				const uint16_t a = mem.add_constant(node->params[0].operator float());
				_program.output_port_addresses[ProgramGraph::PortLocation{ node_id, 0 }] = a;
				continue;
			}

			case VoxelGeneratorGraph::NODE_INPUT_X:
				_program.output_port_addresses[ProgramGraph::PortLocation{ node_id, 0 }] = _program.x_input_address;
				continue;

			case VoxelGeneratorGraph::NODE_INPUT_Y:
				_program.output_port_addresses[ProgramGraph::PortLocation{ node_id, 0 }] = _program.y_input_address;
				continue;

			case VoxelGeneratorGraph::NODE_INPUT_Z:
				_program.output_port_addresses[ProgramGraph::PortLocation{ node_id, 0 }] = _program.z_input_address;
				continue;

			case VoxelGeneratorGraph::NODE_OUTPUT_SDF:
				if (_program.sdf_output_address != -1) {
					CompilationResult result;
					result.success = false;
					result.message = "Multiple SDF outputs are not supported";
					result.node_id = node_id;
					return result;
				}
				CRASH_COND(node->inputs.size() != 1);
				if (node->inputs[0].connections.size() > 0) {
					ProgramGraph::PortLocation src_port = node->inputs[0].connections[0];
					const uint16_t *aptr = _program.output_port_addresses.getptr(src_port);
					// Previous node ports must have been registered
					CRASH_COND(aptr == nullptr);
					_program.sdf_output_address = *aptr;
				}
				continue;

			case VoxelGeneratorGraph::NODE_SDF_PREVIEW:
				continue;
		};

		// Add actual operation
		CRASH_COND(node->type_id > 0xff);
		append(operations, static_cast<uint8_t>(node->type_id));

		// Inputs and outputs use a convention so we can have generic code for them.
		// Parameters are more specific, and may be affected by alignment so better just do them by hand

		// Add inputs
		for (size_t j = 0; j < type.inputs.size(); ++j) {
			uint16_t a;

			if (node->inputs[j].connections.size() == 0) {
				// No input, default it
				CRASH_COND(j >= node->default_inputs.size());
				float defval = node->default_inputs[j];
				a = mem.add_constant(defval);

			} else {
				ProgramGraph::PortLocation src_port = node->inputs[j].connections[0];
				const uint16_t *aptr = _program.output_port_addresses.getptr(src_port);
				// Previous node ports must have been registered
				CRASH_COND(aptr == nullptr);
				a = *aptr;
			}

			append(operations, a);
		}

		// Add outputs
		for (size_t j = 0; j < type.outputs.size(); ++j) {
			const uint16_t a = mem.add_var();

			// This will be used by next nodes
			const ProgramGraph::PortLocation op{ node_id, static_cast<uint32_t>(j) };
			_program.output_port_addresses[op] = a;

			append(operations, a);
		}

		// Add space for params size, default is no params so size is 0
		size_t params_size_index = operations.size();
		append<uint16_t>(operations, 0);

		// Get params, copy resources when used, and hold a reference to them
		std::vector<Variant> params_copy;
		params_copy.resize(node->params.size());
		for (size_t i = 0; i < node->params.size(); ++i) {
			Variant v = node->params[i];
			if (v.get_type() == Variant::OBJECT) {
				Ref<Resource> res = v;
				if (res.is_null()) {
					// duplicate() is only available in Resource,
					// so we have to limit to this instead of Reference or Object
					CompilationResult result;
					result.success = false;
					result.message = "A parameter is an object but does not inherit Resource";
					result.node_id = node_id;
					return result;
				}
				res = res->duplicate();
				_program.ref_resources.push_back(res);
				v = res;
			}
			params_copy[i] = v;
		}

		if (type.compile_func != nullptr) {
			const size_t size_before = operations.size();
			CompileContext ctx(*node, operations, _program.heap_resources, params_copy);
			type.compile_func(ctx);
			if (ctx.has_error()) {
				CompilationResult result;
				result.success = false;
				result.message = ctx.get_error_message();
				result.node_id = node_id;
				return result;
			}
			const size_t params_size = operations.size() - size_before;
			CRASH_COND(params_size > std::numeric_limits<uint16_t>::max());
			*reinterpret_cast<uint16_t *>(&operations[params_size_index]) = params_size;
		}

#ifdef VOXEL_DEBUG_GRAPH_PROG_SENTINEL
		// Append a special value after each operation
		append(operations, VOXEL_DEBUG_GRAPH_PROG_SENTINEL);
#endif
	}

	_program.buffer_count = mem.next_address;

	PRINT_VERBOSE(String("Compiled voxel graph. Program size: {0}b, buffers: {1}")
						  .format(varray(
								  SIZE_T_TO_VARIANT(_program.operations.size() * sizeof(float)),
								  SIZE_T_TO_VARIANT(_program.buffer_count))));

	CompilationResult result;
	result.success = true;
	return result;
}

float VoxelGraphRuntime::generate_single(State &state, Vector3 position) const {
	float output;
	generate_set(state,
			ArraySlice<float>(&position.x, 1),
			ArraySlice<float>(&position.y, 1),
			ArraySlice<float>(&position.z, 1),
			ArraySlice<float>(&output, 1), false);
	return output;
}

void VoxelGraphRuntime::prepare_state(State &state, unsigned int buffer_size) const {
	const unsigned int old_buffer_count = state.buffers.size();
	if (_program.buffer_count > state.buffers.size()) {
		state.buffers.resize(_program.buffer_count);
	}

	// Note: this must be after we resize the vector
	ArraySlice<Buffer> buffers(state.buffers, 0, state.buffers.size());
	state.buffer_size = buffer_size;

	for (auto it = _program.bindings.begin(); it != _program.bindings.end(); ++it) {
		const uint16_t a = *it;
		Buffer &b = buffers[a];
		if (b.is_binding) {
			// Forgot to unbind?
			CRASH_COND(b.data != nullptr);
		} else if (b.data != nullptr) {
			// Deallocate this buffer if it wasnt a binding and contained something
			memdelete(b.data);
		}
		b.is_binding = true;
	}

	// Allocate more buffers if needed
	if (old_buffer_count < state.buffers.size()) {
		for (size_t buffer_index = old_buffer_count; buffer_index < buffers.size(); ++buffer_index) {
			Buffer &buffer = buffers[buffer_index];
			// TODO Put all bindings at the beginning. This would avoid the branch.
			if (buffer.is_binding) {
				// These are supposed to be setup already
				continue;
			}
			// We don't expect previous stuff in those buffers since we just created their slots
			CRASH_COND(buffer.data != nullptr);
			// TODO Use pool?
			// New buffers get an up-to-date size, but must also comply with common capacity
			const unsigned int bs = max(state.buffer_capacity, buffer_size);
			buffer.data = reinterpret_cast<float *>(memalloc(bs * sizeof(float)));
			buffer.capacity = bs;
		}
	}

	// Make old buffers larger if needed
	if (state.buffer_capacity < buffer_size) {
		for (size_t buffer_index = 0; buffer_index < old_buffer_count; ++buffer_index) {
			Buffer &buffer = buffers[buffer_index];
			if (buffer.is_binding) {
				continue;
			}
			if (buffer.data == nullptr) {
				buffer.data = reinterpret_cast<float *>(memalloc(buffer_size * sizeof(float)));
			} else {
				buffer.data = reinterpret_cast<float *>(memrealloc(buffer.data, buffer_size * sizeof(float)));
			}
			buffer.capacity = buffer_size;
		}
		state.buffer_capacity = buffer_size;
	}
	for (auto it = state.buffers.begin(); it != state.buffers.end(); ++it) {
		it->size = buffer_size;
	}

	state.ranges.resize(_program.buffer_count);

	// Always fill constants because we don't know if we'll run the same program as before...
	for (auto it = _program.constants.begin(); it != _program.constants.end(); ++it) {
		const Constant &c = *it;
		Buffer &buffer = buffers[c.address];
		buffer.is_constant = true;
		buffer.constant_value = c.value;
		CRASH_COND(buffer.size > buffer.capacity);
		for (unsigned int j = 0; j < buffer_size; ++j) {
			buffer.data[j] = c.value;
		}
		state.ranges[c.address] = Interval::from_single_value(c.value);
	}

	/*if (use_range_analysis) {
		// TODO To be really worth it, we may need a runtime graph traversal pass,
		// where we build an execution map of nodes that are worthy 🔨

		const float ra_min = _memory[i];
		const float ra_max = _memory[i + _memory.size() / 2];

		buffer.is_constant = (ra_min == ra_max);
		if (buffer.is_constant) {
			buffer.constant_value = ra_min;
		}
	}*/
}

void VoxelGraphRuntime::generate_set(State &state,
		ArraySlice<float> in_x, ArraySlice<float> in_y, ArraySlice<float> in_z,
		ArraySlice<float> out_sdf, bool skip_xz) const {

	// I don't like putting private helper functions in headers.
	struct L {
		static inline void bind_buffer(ArraySlice<Buffer> buffers, int a, ArraySlice<float> d) {
			Buffer &buffer = buffers[a];
			CRASH_COND(!buffer.is_binding);
			buffer.data = d.data();
			buffer.size = d.size();
		}

		static inline void unbind_buffer(ArraySlice<Buffer> buffers, int a) {
			Buffer &buffer = buffers[a];
			CRASH_COND(!buffer.is_binding);
			buffer.data = nullptr;
		}
	};

	VOXEL_PROFILE_SCOPE();

#ifdef DEBUG_ENABLED
	// Each array must have the same size
	CRASH_COND(!(in_x.size() == in_y.size() && in_y.size() == in_z.size() && in_z.size() == out_sdf.size()));
#endif

	const unsigned int buffer_size = in_x.size();

#ifdef TOOLS_ENABLED
	ERR_FAIL_COND_MSG(!has_output(), "The graph has no SDF output");
	ERR_FAIL_COND(state.buffers.size() < _program.buffer_count);
	ERR_FAIL_COND(state.buffers.size() == 0);
	ERR_FAIL_COND(state.buffer_size < buffer_size);
	ERR_FAIL_COND(state.buffers[0].size < buffer_size);
#ifdef DEBUG_ENABLED
	for (size_t i = 0; i < state.buffers.size(); ++i) {
		const Buffer &b = state.buffers[i];
		CRASH_COND(b.size < buffer_size);
		CRASH_COND(b.size > state.buffer_capacity);
		CRASH_COND(b.size != state.buffer_size);
		if (!b.is_binding) {
			CRASH_COND(b.size > b.capacity);
		}
	}
#endif
#endif

	ArraySlice<Buffer> buffers(state.buffers, 0, state.buffers.size());

	// Bind inputs
	if (_program.x_input_address != -1) {
		L::bind_buffer(buffers, _program.x_input_address, in_x);
	}
	if (_program.y_input_address != -1) {
		L::bind_buffer(buffers, _program.y_input_address, in_y);
	}
	if (_program.z_input_address != -1) {
		L::bind_buffer(buffers, _program.z_input_address, in_z);
	}

	uint32_t pc = skip_xz ? _program.xzy_start : 0;

	// STL is unreadable on debug builds of Godot, because _DEBUG isn't defined
	//#ifdef DEBUG_ENABLED
	//	const size_t memory_size = memory.size();
	//	const size_t program_size = _program.size();
	//	const float *memory_raw = memory.data();
	//	const uint8_t *program_raw = (const uint8_t *)_program.data();
	//#endif

	const ArraySlice<const uint8_t> operations(_program.operations.data(), 0, _program.operations.size());

	while (pc < operations.size()) {
		const uint8_t opid = operations[pc++];
		const VoxelGraphNodeDB::NodeType &node_type = VoxelGraphNodeDB::get_singleton()->get_type(opid);

		const uint32_t inputs_size = node_type.inputs.size() * sizeof(uint16_t);
		const uint32_t outputs_size = node_type.outputs.size() * sizeof(uint16_t);

		const ArraySlice<const uint16_t> inputs =
				operations.sub(pc, inputs_size).reinterpret_cast_to<const uint16_t>();
		pc += inputs_size;
		const ArraySlice<const uint16_t> outputs =
				operations.sub(pc, outputs_size).reinterpret_cast_to<const uint16_t>();
		pc += outputs_size;

		const uint16_t params_size = read<uint16_t>(operations, pc);
		ArraySlice<const uint8_t> params;
		if (params_size > 0) {
			params = operations.sub(pc, params_size);
			pc += params_size;
		}

		// Skip node if all its outputs are constant
		bool all_outputs_constant = true;
		for (uint32_t i = 0; i < outputs.size(); ++i) {
			const Buffer &buffer = buffers[outputs[i]];
			all_outputs_constant &= buffer.is_constant;
		}
		if (all_outputs_constant) {
			continue;
		}

		ERR_FAIL_COND(node_type.process_buffer_func == nullptr);
		ProcessBufferContext ctx(inputs, outputs, params, buffers);
		node_type.process_buffer_func(ctx);
	}

	// Populate output buffers
	Buffer &sdf_output_buffer = buffers[_program.sdf_output_address];
	if (sdf_output_buffer.is_constant) {
		out_sdf.fill(sdf_output_buffer.constant_value);
	} else {
		memcpy(out_sdf.data(), sdf_output_buffer.data, buffer_size * sizeof(float));
	}

	// Unbind buffers
	if (_program.x_input_address != -1) {
		L::unbind_buffer(buffers, _program.x_input_address);
	}
	if (_program.y_input_address != -1) {
		L::unbind_buffer(buffers, _program.y_input_address);
	}
	if (_program.z_input_address != -1) {
		L::unbind_buffer(buffers, _program.z_input_address);
	}
}

// TODO Accept float bounds
Interval VoxelGraphRuntime::analyze_range(State &state, Vector3i min_pos, Vector3i max_pos) const {
#ifdef TOOLS_ENABLED
	ERR_FAIL_COND_V_MSG(!has_output(), Interval(), "The graph has no SDF output");
	ERR_FAIL_COND_V(state.ranges.size() != _program.buffer_count, Interval());
#endif

	ArraySlice<Interval> ranges(state.ranges, 0, state.ranges.size());

	ranges[0] = Interval(min_pos.x, max_pos.x);
	ranges[1] = Interval(min_pos.y, max_pos.y);
	ranges[2] = Interval(min_pos.z, max_pos.z);

	const ArraySlice<const uint8_t> operations(_program.operations.data(), 0, _program.operations.size());

	uint32_t pc = 0;
	while (pc < operations.size()) {
		const uint8_t opid = operations[pc++];
		const VoxelGraphNodeDB::NodeType &node_type = VoxelGraphNodeDB::get_singleton()->get_type(opid);

		const uint32_t inputs_size = node_type.inputs.size() * sizeof(uint16_t);
		const uint32_t outputs_size = node_type.outputs.size() * sizeof(uint16_t);

		const ArraySlice<const uint16_t> inputs =
				operations.sub(pc, inputs_size).reinterpret_cast_to<const uint16_t>();
		pc += inputs_size;
		const ArraySlice<const uint16_t> outputs =
				operations.sub(pc, outputs_size).reinterpret_cast_to<const uint16_t>();
		pc += outputs_size;

		const uint16_t params_size = read<uint16_t>(operations, pc);
		ArraySlice<const uint8_t> params;
		if (params_size > 0) {
			params = operations.sub(pc, params_size);
			pc += params_size;
		}

		ERR_FAIL_COND_V(node_type.range_analysis_func == nullptr, Interval());
		RangeAnalysisContext ctx(inputs, outputs, params, ranges);
		node_type.range_analysis_func(ctx);

#ifdef VOXEL_DEBUG_GRAPH_PROG_SENTINEL
		// If this fails, the program is ill-formed
		CRASH_COND(read<uint16_t>(_program, pc) != VOXEL_DEBUG_GRAPH_PROG_SENTINEL);
#endif
	}

	return ranges[_program.sdf_output_address];
}

uint16_t VoxelGraphRuntime::get_output_port_address(ProgramGraph::PortLocation port) const {
	const uint16_t *aptr = _program.output_port_addresses.getptr(port);
	ERR_FAIL_COND_V(aptr == nullptr, 0);
	return *aptr;
}
