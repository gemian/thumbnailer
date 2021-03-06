/*
 * Copyright 2014 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: James Henstridge <james.henstridge@canonical.com>
 */

#pragma once

#include <QtQml>

namespace unity
{
namespace thumbnailer
{
namespace qml
{

class ThumbnailerPlugin : public QQmlExtensionPlugin
{
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#endif
    Q_OBJECT
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QQmlExtensionInterface")

public:
    virtual void registerTypes(const char* uri) override;
    virtual void initializeEngine(QQmlEngine* engine, const char* uri) override;
};
}
}
}
