/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __PR_CYCLES_SCENE_OBJECT_HPP__
#define __PR_CYCLES_SCENE_OBJECT_HPP__

#include "definitions.hpp"
#include <sharedutils/util_weak_handle.hpp>
#include <memory>

namespace raytracing
{
	class Scene;
	class DLLRTUTIL SceneObject
	{
	public:
		virtual ~SceneObject()=default;
		void Finalize();
		Scene &GetScene() const;
	protected:
		virtual void DoFinalize();
		SceneObject(Scene &scene);
	private:
		Scene &m_scene;
		bool m_bFinalized = false;
	};
};

#endif
