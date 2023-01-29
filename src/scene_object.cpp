/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#include "util_raytracing/scene_object.hpp"
#include "util_raytracing/object.hpp"

#pragma optimize("",off)
static unirender::BaseObject *target = nullptr;
unirender::BaseObject::BaseObject()
{}
unirender::BaseObject::~BaseObject() {}
unirender::Scene &unirender::SceneObject::GetScene() const {return m_scene;}
unirender::SceneObject::SceneObject(Scene &scene)
	: m_scene{scene}
{}

void unirender::BaseObject::Finalize(Scene &scene,bool force)
{
	if(m_bFinalized && force == false)
		return;
	m_bFinalized = true;
	DoFinalize(scene);
}
void unirender::BaseObject::DoFinalize(Scene &scene) {}
#pragma optimize("",on)
