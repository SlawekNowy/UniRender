/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __UNIRENDER_CYCLES_OBJECT_HPP__
#define __UNIRENDER_CYCLES_OBJECT_HPP__

#include "../object.hpp"

namespace ccl {class Object;};
namespace unirender::cycles
{
	/*class DLLRTUTIL Object
		: public raytracing::Object
	{
	public:
	private:
	};*/
};
REGISTER_BASIC_BITWISE_OPERATORS(raytracing::Object::Flags)

#endif
