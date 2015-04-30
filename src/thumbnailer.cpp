/*
 * Copyright (C) 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Jussi Pakkanen <jussi.pakkanen@canonical.com>
 *              Michi Henning <michi.henning@canonical.com>
 */

#include <thumbnailer.h>

#include <internal/audioimageextractor.h>
#include <internal/file_io.h>
#include <internal/gobj_memory.h>
#include <internal/image.h>
#include <internal/lastfmdownloader.h>
#include <internal/make_directories.h>
#include <internal/raii.h>
#include <internal/safe_strerror.h>
#include <internal/ubuntuserverdownloader.h>
#include <internal/videoscreenshotter.h>

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <core/persistent_string_cache.h>

#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>

using namespace std;
using namespace unity::thumbnailer::internal;

class ThumbnailerPrivate
{
public:
    struct ImageData
    {
        string data;
        bool keep_in_cache;
    };

    AudioImageExtractor audio_;
    VideoScreenshotter video_;
    core::PersistentStringCache::UPtr full_size_cache_;  // Small cache of full (original) size images.
    core::PersistentStringCache::UPtr thumbnail_cache_;  // Large cache of scaled images.
    unique_ptr<ArtDownloader> downloader_;

    ThumbnailerPrivate();

    ImageData extract_image(string const& filename);
    string fetch_thumbnail(string const& key1,
                           string const& key2,
                           QSize const& requested_size,
                           function<ImageData(string const&, string const&)> fetch);

private:
    string extract_image_from_audio(string const& filename);
    string extract_image_from_video(string const& filename);
    string extract_image_from_other(string const& filename);
};

ThumbnailerPrivate::ThumbnailerPrivate()
{
    char const* artservice = getenv("THUMBNAILER_ART_PROVIDER");
    if (artservice && strcmp(artservice, "lastfm") == 0)
    {
        downloader_.reset(new LastFMDownloader());
    }
    else
    {
        downloader_.reset(new UbuntuServerDownloader());
    }

    string xdg_base = g_get_user_cache_dir();
    if (xdg_base == "")
    {
        string s("Thumbnailer(): Could not determine cache dir.");
        throw runtime_error(s);
    }

    string cache_dir = xdg_base + "/unity-thumbnailer";
    make_directories(cache_dir, 0700);

    try
    {
        // TODO: No good to hard-wire the cache size.
        full_size_cache_ = core::PersistentStringCache::open(cache_dir + "/images", 50 * 1024 * 1024,
                                                             core::CacheDiscardPolicy::lru_ttl);
        thumbnail_cache_ = core::PersistentStringCache::open(cache_dir + "/thumbnails", 100 * 1024 * 1024,
                                                             core::CacheDiscardPolicy::lru_ttl);
    }
    catch (std::exception const& e)
    {
        string s("Thumbnailer(): Cannot instantiate cache: ");
        s += e.what();
        throw runtime_error(s);
    }
}

string ThumbnailerPrivate::extract_image_from_audio(string const& filename)
{
    UnlinkPtr tmpname(create_tmp_filename(), do_unlink);

    if (audio_.extract(filename, tmpname.get()))
    {
        return read_file(tmpname.get());  // TODO: use a pipe instead of a temp file.
    }
    return "";
}

string ThumbnailerPrivate::extract_image_from_video(string const& filename)
{
    UnlinkPtr tmpname(create_tmp_filename(), do_unlink);

    if (video_.extract(filename, tmpname.get()))
    {
        return read_file(tmpname.get());  // TODO: use a pipe instead of a temp file.
    }
    return "";
}

string ThumbnailerPrivate::extract_image_from_other(string const& filename)
{
    return read_file(filename);
}

ThumbnailerPrivate::ImageData ThumbnailerPrivate::extract_image(string const& filename)
{
    // Work out content type.

    unique_gobj<GFile> file(g_file_new_for_path(filename.c_str()));
    if (!file)
    {
        return {"", false};
    }

    unique_gobj<GFileInfo> info(g_file_query_info(file.get(), G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE,
                                                  G_FILE_QUERY_INFO_NONE,
                                                  /* cancellable */ NULL,
                                                  /* error */ NULL));
    if (!info)
    {
        return {"", false};
    }

    string content_type = g_file_info_get_attribute_string(info.get(), G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
    if (content_type.empty())
    {
        return {"", false};
    }

    // Call the appropriate image extractor and return the image data as JPEG (not scaled).

    if (content_type.find("audio/") == 0)
    {
        return ImageData{extract_image_from_audio(filename), true};
    }
    if (content_type.find("video/") == 0)
    {
        return ImageData{extract_image_from_video(filename), true};
    }
    return ImageData{extract_image_from_other(filename), false};
}

Thumbnailer::Thumbnailer()
    : p_(new ThumbnailerPrivate())
{
}

Thumbnailer::~Thumbnailer() = default;

// Main look-up logic for thumbnails.
// key1 and key2 are set by the caller. They are artist and album or, for files,
// the pathname in key1 and the device, inode, and mtime in key2.
//
// We first look in the cache to see if we have a thumbnail already for the provided keys and size.
// If not, we check whether a full-size image was downloaded previously and is still hanging
// around. If so, we scale the full-size image to what was asked for, add it to the thumbnail
// cache, and return the scaled image. Otherwise, we fetch the image (by downloading it
// or extracting it from a file), add the full-size image to the full-size cache, scale
// the image and add the scaled version to the thumbnail cache, and return the scaled image.
//
// If an image contains an EXIF thumbnail and the thumbnail is >= desired size, we generate
// the thumbnail from the EXIF thumbnail.

string ThumbnailerPrivate::fetch_thumbnail(string const& key1,
                                           string const& key2,
                                           QSize const& requested_size,
                                           function<ImageData(string const&, string const&)> fetch)
{
    int MAX_SIZE = 1920;  // TODO: Make limit configurable?
    QSize target_size = requested_size.isValid() ? requested_size : QSize(MAX_SIZE, MAX_SIZE);

    string key = key1;
    key += '\0';
    key += key2;

    // desired_size is 0 if the caller wants original size.
    string sized_key = key;
    sized_key += '\0';
    sized_key += to_string(target_size.width());
    sized_key += '\0';
    sized_key += to_string(target_size.height());

    // Check if we have the thumbnail in the cache already.
    auto thumbnail = thumbnail_cache_->get(sized_key);
    if (thumbnail)
    {
        return *thumbnail;
    }

    // Don't have the thumbnail yet, see if we have the original image around.
    auto full_size = full_size_cache_->get(key);
    if (!full_size)
    {
        // Try and download or read the artwork.
        auto image_data = fetch(key1, key2);
        if (image_data.data.empty())
        {
            // TODO: If download failed, need to disable re-try for some time.
            //       Might need to do this in the calling code, because timeouts
            //       will be different depending on why it failed, and whether
            //       the fetch was from a local or remote source.
            return "";
        }

        // We keep the full-size version around for a while because it
        // is likely that the caller will ask for small thumbnail
        // first (for initial search results), followed by a larger
        // thumbnail (for a preview). If so, we don't download the
        // artwork a second time. For local files, we keep the full-size
        // version if it was generated from a video or audio file (which is
        // expensive), but not if it was generated from an image file (which is cheap).
        if (image_data.keep_in_cache)
        {
            Image full_size_image(image_data.data, target_size);
            auto w = full_size_image.width();
            auto h = full_size_image.height();
            if (max(w, h) > MAX_SIZE)
            {
                // Don't put ridiculously large images into the full-size cache.
                full_size_image = Image(image_data.data, QSize(MAX_SIZE, MAX_SIZE));
            }
            full_size_cache_->put(key, full_size_image.to_jpeg());
        }
        full_size = move(image_data.data);
    }

    Image scaled_image(*full_size, target_size);
    string jpeg = scaled_image.to_jpeg();
    thumbnail_cache_->put(sized_key, jpeg);
    return jpeg;
}

string Thumbnailer::get_thumbnail(string const& filename, QSize const &requested_size)
{
    assert(!filename.empty());

    auto path = boost::filesystem::canonical(filename);

    struct stat st;
    if (stat(path.native().c_str(), &st) == -1)
    {
        throw runtime_error("get_thumbnail(): cannot stat " + path.native() + ", errno = " + to_string(errno));
    }
    auto dev = st.st_dev;
    auto ino = st.st_ino;
    auto mtim = st.st_mtim;

    // The full cache key for the file is the concatenation of path name, device, inode, and modification time.
    // If the file exists with the same path on different removable media, or the file was modified since
    // we last cached it, the key will be different. There is no point in trying to remove such stale entries
    // from the cache. Instead, we just let the normal eviction mechanism take care of them (because stale
    // thumbnails due to file removal or file update are rare).
    string key1 = path.native();
    string key2 = to_string(dev);
    key2 += '\0';
    key2 += to_string(ino);
    key2 += '\0';
    key2 += to_string(mtim.tv_sec) + "." + to_string(mtim.tv_nsec);

    auto fetch = [this](string const& key1, string const& /* key2 */)
    {
        return p_->extract_image(key1);
    };
    return p_->fetch_thumbnail(key1, key2, requested_size, fetch);
}

string Thumbnailer::get_album_art(string const& artist, string const& album, QSize const &requested_size)
{
    assert(artist.empty() || !album.empty());
    assert(album.empty() || !artist.empty());

    auto fetch = [this](string const& artist, string const& album)
    {
        return ThumbnailerPrivate::ImageData{p_->downloader_->download(artist, album), true};
    };
    // Append "\0album" to key2, so we don't mix up album art and artist art.
    string key2 = album;
    key2 += '\0';
    key2 += "album";
    return p_->fetch_thumbnail(artist, key2, requested_size, fetch);
}

string Thumbnailer::get_artist_art(string const& artist, string const& album, QSize const &requested_size)
{
    assert(artist.empty() || !album.empty());
    assert(album.empty() || !artist.empty());

    auto fetch = [this](string const& artist, string const& album)
    {
        return ThumbnailerPrivate::ImageData{p_->downloader_->download_artist(artist, album), true};
    };
    // Append "\0artist" to key2, so we don't mix up album art and artist art.
    string key2 = album;
    key2 += '\0';
    key2 += "artist";
    return p_->fetch_thumbnail(artist, key2, requested_size, fetch);
}
