/*
 * Copyright (C) 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Michi Henning <michi.henning@canonical.com>
 */

#pragma once

#include "action.h"

namespace unity
{

namespace thumbnailer
{

namespace tools
{

class Shutdown : public Action
{
public:
    UNITY_DEFINES_PTRS(Shutdown);

    Shutdown(QCommandLineParser& parser);
    virtual ~Shutdown();

    virtual void run(DBusConnection& conn) override;
};

}  // namespace tools

}  // namespace thumbnailer

}  // namespace unity
