/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2021 Silverlan
*/

#ifndef __RT_EXCEPTION_HPP__
#define __RT_EXCEPTION_HPP__

#include "definitions.hpp"
#include <exception>
#include <string>

namespace unirender
{
	class DLLRTUTIL Exception
		: public std::exception
	{
	public:
		Exception(const std::string &msg)
			: m_message{msg}
		{}
		virtual char const *what() const override
		{
			return m_message.c_str();
		}
	private:
		std::string m_message {};
	};
};

#endif
