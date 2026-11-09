/*
 * Copyright (C) 2013-2020 Graeme Gott <graeme@gottcode.org>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef VIBRISMENU_RUN_ACTION_H
#define VIBRISMENU_RUN_ACTION_H

#include "element.h"

#include <string>

namespace VibrisMenu
{

class RunAction : public Element
{
public:
	RunAction();

	void run(GdkScreen* screen) const override;
	unsigned int search(const Query& query) override;

private:
	std::string m_command_line;
};

}

#endif // VIBRISMENU_RUN_ACTION_H
