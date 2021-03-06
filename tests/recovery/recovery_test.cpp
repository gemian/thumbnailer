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

#include <internal/cachehelper.h>
#include "MockCache.h"
#include <testsetup.h>

#include <system_error>

using namespace std;
using namespace testing;
using namespace unity::thumbnailer::internal;
using namespace unity::thumbnailer::internal::testing;

#define CACHEDIR TESTBINDIR "/cachedir"

TEST(recovery, system_error_EBADF)
{
    CacheHelper<MockCache>::UPtr ch = CacheHelper<MockCache>::open(CACHEDIR,
                                                                   1024,
                                                                   core::CacheDiscardPolicy::lru_only);

    EXPECT_CALL(ch->cache(), get("foo"))
        .WillOnce(Return(core::Optional<string>("foo")))
        .WillOnce(Throw(system_error(error_code(EBADF, generic_category()))))
        .WillOnce(Return(core::Optional<string>()));

    EXPECT_EQ(core::Optional<string>("foo"), ch->get("foo"));
    try
    {
        ch->get("foo");
        FAIL();
    }
    catch (system_error const& e)
    {
        EXPECT_EQ(EBADF, e.code().value());
        EXPECT_EQ("Bad file descriptor", e.code().message());
    }
    EXPECT_FALSE(ch->get("foo"));
}

TEST(recovery, recover_from_666)
{
    CacheHelper<MockCache>::UPtr ch = CacheHelper<MockCache>::open(CACHEDIR,
                                                                   1024,
                                                                   core::CacheDiscardPolicy::lru_only);
    ch->compact();  // Throws 666 once, then succeeds.
}

TEST(recovery, retry_throws_runtime_error)
{
    CacheHelper<MockCache>::UPtr ch = CacheHelper<MockCache>::open(CACHEDIR,
                                                                   1024,
                                                                   core::CacheDiscardPolicy::lru_only);
    try
    {
        ch->put("foo", "foo");  // Throws 666 once, then runtime_error.
        FAIL();
    }
    catch (runtime_error const& e)
    {
        EXPECT_STREQ("bang", e.what()) << e.what();
    }
}

TEST(recovery, retry_throws_42)
{
    CacheHelper<MockCache>::UPtr ch = CacheHelper<MockCache>::open(CACHEDIR,
                                                                   1024,
                                                                   core::CacheDiscardPolicy::lru_only);
    try
    {
        ch->invalidate();  // Throws system_error(666), then 42.
        FAIL();
    }
    catch (int i)
    {
        EXPECT_EQ(42, i);
    }
}

TEST(recovery, recovery_throws_std_exception)
{
    CacheHelper<MockCache>::UPtr ch = CacheHelper<MockCache>::open("throw_std_exception",
                                                                   1024,
                                                                   core::CacheDiscardPolicy::lru_only);
    try
    {
        ch->invalidate();  // Throws system_error(666), then 42.
        FAIL();
    }
    catch (std::exception const& e)
    {
        EXPECT_STREQ("testing std exception", e.what()) << e.what();
    }
}

TEST(recovery, recovery_throws_unknown_exception)
{
    CacheHelper<MockCache>::UPtr ch = CacheHelper<MockCache>::open("throw_unknown_exception",
                                                                   1024,
                                                                   core::CacheDiscardPolicy::lru_only);
    try
    {
        try
        {
            ch->invalidate();  // Throws 42. (Depends on state established by previous test.)
            FAIL();
        }
        catch (int i)
        {
            EXPECT_EQ(42, i);
        }
        ch->invalidate();  // Throws system_error(666).
        FAIL();
    }
    catch (std::exception const& e)
    {
        FAIL();
    }
    catch (int i)
    {
        EXPECT_EQ(99, i);
    }
    catch (...)
    {
        FAIL();
    }
}

int main(int argc, char** argv)
{
    // So error string in exceptions come out in English.
    setenv("LC_ALL", "C", true);

    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
