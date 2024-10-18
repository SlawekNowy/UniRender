/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#include "definitions.hpp"
#include <exception>
#include <string>

export module pragma.scenekit:exception;

export namespace pragma::scenekit {
	class DLLRTUTIL Exception : public std::exception {
	  public:
		Exception(const std::string &msg) : m_message {msg} {}
		virtual char const *what() const noexcept override { return m_message.c_str(); }
	  private:
		std::string m_message {};
	};
};
