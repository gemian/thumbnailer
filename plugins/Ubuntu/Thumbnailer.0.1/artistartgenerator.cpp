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
 * Authors: Jussi Pakkanen <jussi.pakkanen@canonical.com>
 *          James Henstridge <james.henstridge@canonical.com>
 *          Pawel Stolowski <pawel.stolowski@canonical.com>
 */

#include "artistartgenerator.h"

#include "artgeneratorcommon.h"
#include <service/dbus_names.h>
#include "thumbnailerimageresponse.h"

namespace
{

const char DEFAULT_ARTIST_ART[] = "/usr/share/thumbnailer/icons/album_missing.png";

}  // namespace

namespace unity
{

namespace thumbnailer
{

namespace qml
{

ArtistArtGenerator::ArtistArtGenerator()
    : QQuickAsyncImageProvider()
{
}

QQuickImageResponse* ArtistArtGenerator::requestImageResponse(const QString& id, const QSize& requestedSize)
{
    QSize size = requestedSize;
    // TODO: Turn this into an error soonish.
    if (!requestedSize.isValid())
    {
        qWarning().nospace() << "ArtistArtGenerator::requestImageResponse(): deprecated invalid QSize: "
                             << requestedSize << ". This feature will be removed soon. Pass the desired size instead.";
        size.setWidth(128);
        size.setHeight(128);
    }

    QUrlQuery query(id);
    if (!query.hasQueryItem("artist") || !query.hasQueryItem("album"))
    {
        qWarning() << "ArtistArtGenerator::requestImageResponse(): Invalid artistart uri:" << id;
        return new ThumbnailerImageResponse(requestedSize, DEFAULT_ARTIST_ART);
    }

    if (!connection)
    {
        // Create connection here and not on the constructor, so it belongs to the proper thread.
        connection.reset(new QDBusConnection(
            QDBusConnection::connectToBus(QDBusConnection::SessionBus, "album_art_generator_dbus_connection")));
        iface.reset(new ThumbnailerInterface(service::BUS_NAME, service::THUMBNAILER_BUS_PATH, *connection));
    }

    const QString artist = query.queryItemValue("artist", QUrl::FullyDecoded);
    const QString album = query.queryItemValue("album", QUrl::FullyDecoded);

    // perform dbus call
    auto reply = iface->GetArtistArt(artist, album, size);
    std::unique_ptr<QDBusPendingCallWatcher> watcher(
        new QDBusPendingCallWatcher(reply));
    return new ThumbnailerImageResponse(size, DEFAULT_ARTIST_ART, std::move(watcher));
}

}  // namespace qml

}  // namespace thumbnailer

}  // namespace unity