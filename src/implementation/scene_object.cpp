/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

module pragma.scenekit;

import :scene_object;

static pragma::scenekit::BaseObject *target = nullptr;
pragma::scenekit::BaseObject::BaseObject() {}
pragma::scenekit::BaseObject::~BaseObject() {}
pragma::scenekit::Scene &pragma::scenekit::SceneObject::GetScene() const { return m_scene; }
pragma::scenekit::SceneObject::SceneObject(Scene &scene) : m_scene {scene} {}

void pragma::scenekit::BaseObject::Finalize(Scene &scene, bool force)
{
	if(m_bFinalized && force == false)
		return;
	m_bFinalized = true;
	DoFinalize(scene);
}
void pragma::scenekit::BaseObject::DoFinalize(Scene &scene) {}
