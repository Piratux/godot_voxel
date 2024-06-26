#ifndef ZN_GODOT_PHYSICS_BODY_3D_H
#define ZN_GODOT_PHYSICS_BODY_3D_H

#if defined(ZN_GODOT)
#include <core/version.h>

#if VERSION_MAJOR == 4 && VERSION_MINOR <= 2
#include <scene/3d/physics_body_3d.h>
#else
#include <scene/3d/physics/physics_body_3d.h>
#endif

#elif defined(ZN_GODOT_EXTENSION)
#include <godot_cpp/classes/physics_body3d.hpp>
using namespace godot;
#endif

#endif // ZN_GODOT_PHYSICS_BODY_3D_H
