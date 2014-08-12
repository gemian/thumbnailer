/*
 * Copyright (C) 2012-2014 Canonical Ltd.
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
 */

#include<internal/mediaartcache.h>

#include<dirent.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<errno.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<assert.h>
#include<utime.h>

#include<glib.h>

#include<vector>
#include<algorithm>
#include<stdexcept>

using namespace std;

static string md5(const string &str) {
    const unsigned char *buf = (const unsigned char *)str.c_str();
    char *normalized = g_utf8_normalize((const gchar*)buf, str.size(), G_NORMALIZE_ALL);
    string final;
    gchar *result;

    if(normalized) {
        buf = (const unsigned char*)normalized;
    }

    result = g_compute_checksum_for_data(G_CHECKSUM_MD5, buf, strlen((const char*)buf));
    final = result;
    g_free((gpointer)normalized);
    g_free(result);
    return final;
}

MediaArtCache::MediaArtCache() {
    string xdg_base = g_get_user_cache_dir();

    if (xdg_base == "") {
        string s("Could not determine cache dir.");
        throw runtime_error(s);
    }
    int ec = mkdir(xdg_base.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
    if (ec < 0 && errno != EEXIST) {
        string s("Could not create base dir.");
        throw runtime_error(s);
    }
    root_dir = xdg_base + "/media-art";
    ec = mkdir(root_dir.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
    if (ec < 0 && errno != EEXIST) {
        string s("Could not create cache dir.");
        throw runtime_error(s);
    }
}

bool MediaArtCache::has_album_art(const std::string &artist, const std::string &album) const {
    string fname = get_album_art_file(artist, album);
    return access(fname.c_str(), R_OK) == 0;
}

bool MediaArtCache::has_artist_art(const std::string &artist, const std::string &album) const {
    string fname = get_artist_art_file(artist, album);
    return access(fname.c_str(), R_OK) == 0;
}

void MediaArtCache::add_art(const std::string& abs_fname, const char *data, unsigned int datalen)
{
    GError *err = nullptr;
    if(!g_file_set_contents(abs_fname.c_str(), data, datalen, &err)) {
        string e("Could not write file ");
        e += abs_fname;
        e += ": ";
        e += err->message;
        g_error_free(err);
        throw runtime_error(e);
    }
}

void MediaArtCache::add_album_art(const std::string &artist, const std::string &album,
        const char *data, unsigned int datalen) {
    add_art(get_full_album_filename(artist, album), data, datalen);
}

void MediaArtCache::add_artist_art(const std::string &artist, const std::string &album,
        const char *data, unsigned int datalen)
{
    add_art(get_full_artist_filename(artist, album), data, datalen);
}

string MediaArtCache::get_art_file(const std::string& abs_fname) const
{
    if (access(abs_fname.c_str(), R_OK) == 0) {
        utime(abs_fname.c_str(), nullptr); // update access times to current time
        return abs_fname;
    }
    return "";
}

string MediaArtCache::get_album_art_file(const std::string &artist, const std::string &album) const {
    return get_art_file(get_full_album_filename(artist, album));
}

string MediaArtCache::get_artist_art_file(const std::string &artist, const std::string &album) const {
    return get_art_file(get_full_artist_filename(artist, album));
}

string MediaArtCache::get_art_uri(const string &artist, const string &album) const
{
    std::string filename = get_album_art_file(artist, album);

    if (filename == "") {
        return "";
    }

    GError *err = nullptr;
    char *c_uri = g_filename_to_uri(filename.c_str(), "", &err);
    if (!c_uri) {
        string e("Could not convert file name ");
        e += filename;
        e += " to URI: ";
        e += err->message;
        g_error_free(err);
        throw runtime_error(e);
    }

    std::string uri = c_uri;
    g_free(c_uri);
    return uri;
}

std::string MediaArtCache::get_full_album_filename(const std::string &artist, const std::string & album) const {
    return root_dir + "/" + compute_base_name("album", artist, album);
}

std::string MediaArtCache::get_full_artist_filename(const std::string &artist, const std::string & album) const {
    return root_dir + "/" + compute_base_name("artist", artist, album);
}

std::string MediaArtCache::compute_base_name(const std::string &prefix, const std::string &artist, const std::string &album) const {
    string h1 = md5(artist);
    string h2 = md5(album);
    return prefix + "-" + h1 + "-" + h2 + ".jpg";
}


void MediaArtCache::clear() const {
    DIR *d = opendir(root_dir.c_str());
    if(!d) {
        string s = "Something went wrong.";
        throw runtime_error(s);
    }
    struct dirent *entry, *de;
    entry = (dirent*)malloc(sizeof(dirent) + NAME_MAX + 1);
    while(readdir_r(d, entry, &de) == 0 && de) {
        string basename = entry->d_name;
        if (basename == "." || basename == "..")
            continue;
        string fname = root_dir + "/" + basename;
        if(remove(fname.c_str()) < 0) {
            // This is not really an error worth
            // halting everything for.
            fprintf(stderr, "Could not delete file %s: %s.\n", fname.c_str(),
                    strerror(errno));
        }
    }
    free(entry);
    closedir(d);
}

void MediaArtCache::prune() {
    vector<pair<double, string>> mtimes;
    DIR *d = opendir(root_dir.c_str());
    if(!d) {
        string s = "Something went wrong.";
        throw runtime_error(s);
    }
    struct dirent *entry, *de;
    entry = (dirent*)malloc(sizeof(dirent) + NAME_MAX + 1);
    while(readdir_r(d, entry, &de) == 0 && de) {
        string basename = entry->d_name;
        if (basename == "." || basename == "..")
            continue;
        string fname = root_dir + "/" + basename;
        struct stat sbuf;
        if(stat(fname.c_str(), &sbuf) != 0) {
            continue;
        }
        // Use mtime because atime is not guaranteed to work if, for example
        // the filesystem is mounted with noatime or relatime.
        mtimes.push_back(make_pair(sbuf.st_mtim.tv_sec + sbuf.st_mtim.tv_nsec/1000000000.0, fname));
    }
    free(entry);
    closedir(d);
    if (mtimes.size() <= MAX_SIZE)
        return;
    sort(mtimes.begin(), mtimes.end());
    for(size_t i=0; i < mtimes.size()-MAX_SIZE; i++) {
        if(remove(mtimes[i].second.c_str()) < 0) {
            fprintf(stderr, "Could not remove file %s: %s.\n",
                    mtimes[i].second.c_str(), strerror(errno));
        }
    }
}
