/*
 * Copyright (C) 2015 Canonical Ltd.
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
 * Authored by: Michi Henning <michi.henning@canonical.com>
 */

#include <internal/thumbnailer.h>

#include <internal/file_io.h>
#include <internal/image.h>
#include <internal/raii.h>
#include <testsetup.h>
#include "thumbnailerinterface.h"
#include "utils/artserver.h"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <gtest/gtest.h>
#include <libqtdbustest/DBusTestRunner.h>
#include <libqtdbustest/QProcessDBusService.h>
#include <QCoreApplication>
#include <QDebug>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <unity/UnityExceptions.h>

#include <sys/types.h>
#include <fcntl.h>

#define TEST_IMAGE TESTDATADIR "/orientation-1.jpg"
#define BAD_IMAGE TESTDATADIR "/bad_image.jpg"
#define RGB_IMAGE TESTDATADIR "/RGB.png"
#define BIG_IMAGE TESTDATADIR "/big.jpg"
#define EMPTY_IMAGE TESTDATADIR "/empty"

#define TEST_VIDEO TESTDATADIR "/testvideo.ogg"
#define TEST_SONG TESTDATADIR "/testsong.ogg"

using namespace std;
using namespace unity::thumbnailer::internal;

// The thumbnailer uses g_get_user_cache_dir() to get the cache dir, and
// glib remembers that value, so changing XDG_CACHE_HOME later has no effect.

static auto set_tempdir = []()
{
    auto dir = new QTemporaryDir(TESTBINDIR "/test-dir.XXXXXX");
    setenv("XDG_CACHE_HOME", dir->path().toUtf8().data(), true);
    return dir;
};
static unique_ptr<QTemporaryDir> tempdir(set_tempdir());

class ThumbnailerTest : public ::testing::Test
{
public:
    static string tempdir_path()
    {
        return tempdir->path().toStdString();
    }

protected:
    virtual void SetUp() override
    {
        mkdir(tempdir_path().c_str(), 0700);
    }

    virtual void TearDown() override
    {
        boost::filesystem::remove_all(tempdir_path());
    }
};

TEST_F(ThumbnailerTest, basic)
{
    Thumbnailer tn;
    std::unique_ptr<ThumbnailRequest> request;
    string thumb;
    Image img;
    FdPtr fd(-1, do_close);

    fd.reset(open(EMPTY_IMAGE, O_RDONLY));
    thumb = tn.get_thumbnail(EMPTY_IMAGE, fd.get(), QSize())->thumbnail();
    EXPECT_EQ("", thumb);

    // Again, this time we get the answer from the failure cache.
    fd.reset(open(EMPTY_IMAGE, O_RDONLY));
    thumb = tn.get_thumbnail(EMPTY_IMAGE, fd.get(), QSize())->thumbnail();
    EXPECT_EQ("", thumb);

    fd.reset(open(TEST_IMAGE, O_RDONLY));
    request = tn.get_thumbnail(TEST_IMAGE, fd.get(), QSize());
    EXPECT_TRUE(boost::starts_with(request->key(), TEST_IMAGE)) << request->key();
    thumb = request->thumbnail();
    img = Image(thumb);
    EXPECT_EQ(640, img.width());
    EXPECT_EQ(480, img.height());

    // Again, for coverage. This time the thumbnail comes from the cache.
    thumb = tn.get_thumbnail(TEST_IMAGE, fd.get(), QSize())->thumbnail();
    img = Image(thumb);
    EXPECT_EQ(640, img.width());
    EXPECT_EQ(480, img.height());

    thumb = tn.get_thumbnail(TEST_IMAGE, fd.get(), QSize(160, 160))->thumbnail();
    img = Image(thumb);
    EXPECT_EQ(160, img.width());
    EXPECT_EQ(120, img.height());

    thumb = tn.get_thumbnail(TEST_IMAGE, fd.get(), QSize(1000, 1000))->thumbnail();  // Will not up-scale
    img = Image(thumb);
    EXPECT_EQ(640, img.width());
    EXPECT_EQ(480, img.height());

    thumb = tn.get_thumbnail(TEST_IMAGE, fd.get(), QSize(100, 100))->thumbnail();  // From EXIF data
    img = Image(thumb);
    EXPECT_EQ(100, img.width());
    EXPECT_EQ(75, img.height());

    fd.reset(open(BAD_IMAGE, O_RDONLY));
    try
    {
        tn.get_thumbnail(BAD_IMAGE, fd.get(), QSize())->thumbnail();
    }
    catch (std::exception const& e)
    {
        string msg = e.what();
        EXPECT_TRUE(boost::starts_with(msg, "unity::ResourceException: RequestBase::thumbnail(): key = ")) << msg;
    }

    fd.reset(open(RGB_IMAGE, O_RDONLY));
    thumb = tn.get_thumbnail(RGB_IMAGE, fd.get(), QSize(48, 48))->thumbnail();
    img = Image(thumb);
    EXPECT_EQ(48, img.width());
    EXPECT_EQ(48, img.height());

    fd.reset(open(BIG_IMAGE, O_RDONLY));
    thumb = tn.get_thumbnail(BIG_IMAGE, fd.get(), QSize())->thumbnail();  // > 1920, so will be trimmed down
    img = Image(thumb);
    EXPECT_EQ(1920, img.width());
    EXPECT_EQ(1439, img.height());

    thumb =
        tn.get_thumbnail(BIG_IMAGE, fd.get(), QSize(0, 0))->thumbnail();  // unconstrained, so will not be trimmed down
    img = Image(thumb);
    EXPECT_EQ(2731, img.width());
    EXPECT_EQ(2048, img.height());
}

TEST_F(ThumbnailerTest, bad_fd)
{
    Thumbnailer tn;

    // Invalid file descriptor
    try
    {
        tn.get_thumbnail(TEST_IMAGE, -1, QSize());
    }
    catch (std::exception const& e)
    {
        string msg = e.what();
        EXPECT_TRUE(boost::contains(msg, ": Could not stat file descriptor:")) << msg;
    }

    // File descriptor for wrong file
    FdPtr fd(open(TEST_VIDEO, O_RDONLY), do_close);
    try
    {
        tn.get_thumbnail(TEST_IMAGE, fd.get(), QSize());
    }
    catch (std::exception const& e)
    {
        string msg = e.what();
        EXPECT_TRUE(boost::contains(msg, ": file descriptor does not refer to file ")) << msg;
    }
}

TEST_F(ThumbnailerTest, replace_photo)
{
    string testfile = tempdir_path() + "/foo.jpg";
    ASSERT_EQ(0, link(TEST_IMAGE, testfile.c_str()));

    Thumbnailer tn;
    FdPtr fd(open(testfile.c_str(), O_RDONLY), do_close);
    auto request = tn.get_thumbnail(testfile, fd.get(), QSize());
    // The client FD isn't needed any more, so close it.
    fd.reset(-1);

    // Replace test image with a different file with different
    // dimensions so we can tell which one is thumbnailed.
    ASSERT_EQ(0, unlink(testfile.c_str()));
    ASSERT_EQ(0, link(BIG_IMAGE, testfile.c_str()));

    string data = request->thumbnail();
    Image img(data);
    EXPECT_EQ(640, img.width());
    EXPECT_EQ(480, img.height());
}

TEST_F(ThumbnailerTest, thumbnail_video)
{
    Thumbnailer tn;
    FdPtr fd(open(TEST_VIDEO, O_RDONLY), do_close);
    auto request = tn.get_thumbnail(TEST_VIDEO, fd.get(), QSize());
    ASSERT_NE(nullptr, request.get());
    // Video thumbnails cannot be produced immediately
    ASSERT_EQ("", request->thumbnail());

    QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
    request->download(chrono::milliseconds(15000));
    ASSERT_TRUE(spy.wait(20000));
    {
        string thumb = request->thumbnail();
        ASSERT_NE("", thumb);
        Image img(thumb);
        EXPECT_EQ(1920, img.width());
        EXPECT_EQ(1080, img.height());
    }

    {
        // Fetch the thumbnail again with the same size.
        // That causes it to come from the thumbnail cache.
        auto request = tn.get_thumbnail(TEST_VIDEO, fd.get(), QSize());
        string thumb = request->thumbnail();
        ASSERT_NE("", thumb);
        Image img(thumb);
        EXPECT_EQ(1920, img.width());
        EXPECT_EQ(1080, img.height());
    }

    {
        // Fetch the thumbnail again with a different size.
        // That causes it to be scaled from the thumbnail cache.
        auto request = tn.get_thumbnail(TEST_VIDEO, fd.get(), QSize(500, 500));
        string thumb = request->thumbnail();
        ASSERT_NE("", thumb);
        Image img(thumb);
        EXPECT_EQ(500, img.width());
        EXPECT_EQ(281, img.height());
    }
}

TEST_F(ThumbnailerTest, replace_video)
{
    string testfile = tempdir_path() + "/foo.ogv";
    ASSERT_EQ(0, link(TEST_VIDEO, testfile.c_str())) << strerror(errno);

    Thumbnailer tn;
    FdPtr fd(open(testfile.c_str(), O_RDONLY | O_CLOEXEC), do_close);
    auto request = tn.get_thumbnail(testfile, fd.get(), QSize());
    // The client FD isn't needed any more, so close it.
    fd.reset(-1);

    // Replace test image with a different file with different
    // dimensions so we can tell which one is thumbnailed.
    ASSERT_EQ(0, unlink(testfile.c_str()));
    ASSERT_EQ(0, link(BIG_IMAGE, testfile.c_str()));

    ASSERT_EQ("", request->thumbnail());
    QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
    request->download(chrono::milliseconds(15000));
    ASSERT_TRUE(spy.wait(20000));

    string data = request->thumbnail();
    Image img(data);
    EXPECT_EQ(1920, img.width());
    EXPECT_EQ(1080, img.height());
}

TEST_F(ThumbnailerTest, thumbnail_song)
{
    Thumbnailer tn;
    FdPtr fd(open(TEST_SONG, O_RDONLY), do_close);
    auto request = tn.get_thumbnail(TEST_SONG, fd.get(), QSize());
    ASSERT_NE(nullptr, request.get());
    // Audio thumbnails cannot be produced immediately
    ASSERT_EQ("", request->thumbnail());

    QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
    request->download(chrono::milliseconds(15000));
    ASSERT_TRUE(spy.wait(20000));
    string thumb = request->thumbnail();
    ASSERT_NE("", thumb);
    Image img(thumb);
    EXPECT_EQ(200, img.width());
    EXPECT_EQ(200, img.height());
}

TEST_F(ThumbnailerTest, exceptions)
{
    string const cache_dir = tempdir_path();
    ASSERT_EQ(0, chmod(cache_dir.c_str(), 0000));
    try
    {
        Thumbnailer tn;
        FAIL();
    }
    catch (runtime_error const& e)
    {
        ASSERT_EQ(0, chmod(cache_dir.c_str(), 0700));
        string msg = e.what();
        string exp = "Thumbnailer(): Cannot instantiate cache: PersistentStringCache: cannot open or create cache: ";
        EXPECT_EQ(0, msg.compare(0, exp.length(), exp)) << msg;
    }
    ASSERT_EQ(0, chmod(cache_dir.c_str(), 0700));
}

TEST_F(ThumbnailerTest, vs_thumb_exec_failure)
{
    Thumbnailer tn;
    {
        // Cause vs-thumb exec failure.
        char const* tn_util = getenv("TN_UTILDIR");
        ASSERT_TRUE(tn_util && *tn_util != '\0');
        string old_env = tn_util;

        setenv("TN_UTILDIR", "no_such_directory", true);

        FdPtr fd(open(TEST_SONG, O_RDONLY), do_close);
        auto request = tn.get_thumbnail(TEST_SONG, fd.get(), QSize());
        EXPECT_EQ("", request->thumbnail());

        QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
        request->download();
        ASSERT_TRUE(spy.wait(15000));

        try
        {
            request->thumbnail();
            FAIL();
        }
        catch (unity::ResourceException const& e)
        {
            string msg = e.to_string();
            string exp = "VideoScreenshotter::data(): Error starting vs-thumb. QProcess::ProcessError";
            EXPECT_TRUE(msg.find(exp) != string::npos) << msg;
        }
        setenv("TN_UTILDIR", old_env.c_str(), true);
    }
}

class RemoteServer : public ::testing::Test
{
protected:
    static void SetUpTestCase()
    {
        // start fake server
        art_server_.reset(new ArtServer());

        // start dbus service
        tempdir.reset(new QTemporaryDir(TESTBINDIR "/dbus-test.XXXXXX"));
        setenv("XDG_CACHE_HOME", (tempdir->path() + "/cache").toUtf8().data(), true);
    }

    static void TearDownTestCase()
    {
        art_server_.reset();

        boost::filesystem::remove_all(ThumbnailerTest::tempdir_path());

        tempdir.reset();
        unsetenv("THUMBNAILER_MAX_IDLE");
        unsetenv("XDG_CACHE_HOME");
    }

    static unique_ptr<ArtServer> art_server_;
};

unique_ptr<ArtServer> RemoteServer::art_server_;

TEST_F(RemoteServer, basic)
{
    Thumbnailer tn;

    {
        auto request = tn.get_album_art("metallica", "load", QSize());
        EXPECT_EQ("", request->thumbnail());

        QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
        request->download();
        ASSERT_TRUE(spy.wait(15000));

        auto thumb = request->thumbnail();
        Image img(thumb);
        EXPECT_EQ(48, img.width());
        EXPECT_EQ(48, img.height());
    }

    {
        auto request = tn.get_artist_art("metallica", "load", QSize());
        EXPECT_EQ("", request->thumbnail());

        QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
        request->download();
        ASSERT_TRUE(spy.wait(15000));

        auto thumb = request->thumbnail();
        Image img(thumb);
        EXPECT_EQ(48, img.width());
        EXPECT_EQ(48, img.height());
    }

    {
        // For coverage, big images are down-sized for the full-size cache.
        auto request = tn.get_artist_art("big", "image", QSize());
        EXPECT_EQ("", request->thumbnail());

        QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
        request->download();
        ASSERT_TRUE(spy.wait(15000));

        auto thumb = request->thumbnail();
        Image img(thumb);
        EXPECT_EQ(1920, img.width());
        EXPECT_EQ(1439, img.height());
    }
}

TEST_F(RemoteServer, no_such_album)
{
    Thumbnailer tn;

    auto request = tn.get_album_art("no_such_artist", "no_such_album", QSize());
    EXPECT_EQ("", request->thumbnail());

    QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
    request->download();
    ASSERT_TRUE(spy.wait(15000));
    EXPECT_EQ("", request->thumbnail());
}

TEST_F(RemoteServer, decode_fails)
{
    Thumbnailer tn;

    auto request = tn.get_album_art("empty", "empty", QSize());
    EXPECT_EQ("", request->thumbnail());
    request->download();

    QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
    ASSERT_TRUE(spy.wait(15000));

    try
    {
        EXPECT_EQ("", request->thumbnail());
        FAIL();
    }
    catch (unity::ResourceException const& e)
    {
        EXPECT_EQ("unity::ResourceException: RequestBase::thumbnail(): key = empty\\0empty\\0album:\n"
                  "    load_image(): cannot close pixbuf loader: Unrecognized image file format",
                  e.to_string());
    }
}

TEST_F(RemoteServer, no_such_local_image)
{
    Thumbnailer tn;

    try
    {
        auto request = tn.get_thumbnail("no_such_file", -1, QSize());
        FAIL();
    }
    catch (unity::ResourceException const& e)
    {
        string msg = e.to_string();
        EXPECT_TRUE(boost::starts_with(msg,
                                       "unity::ResourceException: Thumbnailer::get_thumbnail():\n"
                                       "    boost::filesystem::canonical: No such file or directory: ")) << msg;
    }
}

TEST_F(RemoteServer, timeout)
{
    Thumbnailer tn;

    auto request = tn.get_album_art("sleep", "3", QSize());
    EXPECT_EQ("", request->thumbnail());
    request->download(chrono::seconds(1));

    QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
    ASSERT_TRUE(spy.wait(15000));

    EXPECT_EQ("", request->thumbnail());
}

TEST_F(RemoteServer, server_error)
{
    Thumbnailer tn;

    auto request = tn.get_album_art("error", "403", QSize());
    EXPECT_EQ("", request->thumbnail());

    QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
    request->download();
    ASSERT_TRUE(spy.wait(15000));

    try
    {
        request->thumbnail();
        FAIL();
    }
    catch (unity::ResourceException const& e)
    {
        string msg = e.to_string();
        EXPECT_TRUE(boost::starts_with(
                        msg,
                        "unity::ResourceException: RequestBase::thumbnail(): key = error")) << msg;
    }
}

TEST_F(RemoteServer, album_and_artist_have_distinct_keys)
{
    Thumbnailer tn;

    auto album_request = tn.get_album_art("metallica", "load", QSize());
    auto artist_request = tn.get_artist_art("metallica", "load", QSize());
    EXPECT_NE(album_request->key(), artist_request->key());
}

class DeadServer : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto apiroot = QString("http://deadserver.invalid:80");
        setenv("THUMBNAILER_UBUNTU_APIROOT", apiroot.toUtf8().constData(), true);
    }

    void TearDown() override
    {
        unsetenv("THUMBNAILER_UBUNTU_APIROOT");
    }
};

TEST_F(DeadServer, errors)
{
    Thumbnailer tn;

    // DeadServer won't reply
    auto request = tn.get_album_art("some_artist", "some_album", QSize());
    EXPECT_EQ("", request->thumbnail());

    request->download();

    QSignalSpy spy(request.get(), &ThumbnailRequest::downloadFinished);
    ASSERT_TRUE(spy.wait(15000));

    EXPECT_EQ("", request->thumbnail());
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    setenv("TN_UTILDIR", TESTBINDIR "/../src/vs-thumb", true);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
