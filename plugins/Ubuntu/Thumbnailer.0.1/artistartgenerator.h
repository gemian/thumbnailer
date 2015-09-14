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
 * Authors: Pawel Stolowski <pawel.stolowski@canonical.com>
 */

#pragma once

#include <ratelimiter.h>
#include <unity/thumbnailer/qt/thumbnailer-qt.h>

#include <QQuickImageProvider>

#include <memory>

namespace unity
{
namespace thumbnailer
{
namespace qml
{

class ArtistArtGenerator : public QQuickAsyncImageProvider
{
private:
    std::shared_ptr<unity::thumbnailer::qt::Thumbnailer> thumbnailer;
    std::shared_ptr<unity::thumbnailer::RateLimiter> backlog_limiter;

public:
    ArtistArtGenerator(std::shared_ptr<unity::thumbnailer::qt::Thumbnailer> thumbnailer,
                       std::shared_ptr<unity::thumbnailer::RateLimiter> backlog_limiter);
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;
};
}
}
}
