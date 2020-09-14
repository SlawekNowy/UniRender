/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "util_raytracing/scene_object.hpp"

#pragma optimize("",off)
raytracing::Scene &raytracing::SceneObject::GetScene() const {return m_scene;}
raytracing::SceneObject::SceneObject(Scene &scene)
	: m_scene{scene}
{}

void raytracing::BaseObject::Finalize(Scene &scene,bool force)
{
	if(m_bFinalized && force == false)
		return;
	m_bFinalized = true;
	DoFinalize(scene);
}
void raytracing::BaseObject::DoFinalize(Scene &scene) {}
#pragma optimize("",on)
