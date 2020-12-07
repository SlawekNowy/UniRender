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
#include <sharedutils/util.h>
#include <memory>

namespace unirender
{
	class Scene;
	class DLLRTUTIL BaseObject
	{
	public:
		virtual ~BaseObject()=default;
		void Finalize(Scene &scene,bool force=false);

		void SetHash(const util::MurmurHash3 &hash) {m_hash = hash;}
		void SetHash(util::MurmurHash3 &&hash) {m_hash = std::move(hash);}
		const util::MurmurHash3 &GetHash() const {return m_hash;}
	protected:
		virtual void DoFinalize(Scene &scene);
	private:
		bool m_bFinalized = false;
		util::MurmurHash3 m_hash {};
	};
	class DLLRTUTIL SceneObject
		: public BaseObject
	{
	public:
		virtual ~SceneObject()=default;
		Scene &GetScene() const;
	protected:
		SceneObject(Scene &scene);
	private:
		Scene &m_scene;
	};
};

#endif
