/*
 * Copyright (C) 2013 Canonical Ltd.
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
 * Authored by: Jussi Pakkanen <jussi.pakkanen@canonical.com>
 */

#include<thumbnailer.h>
#include<gst/gst.h>

class GstInitializer {
public:
    GstInitializer() { gst_init(nullptr, nullptr); };
};

class ThumbnailerPrivate {
public:
    ThumbnailerPrivate() {};
};

Thumbnailer::Thumbnailer() {
    p = new ThumbnailerPrivate();
}

Thumbnailer::~Thumbnailer() {
    delete p;
}