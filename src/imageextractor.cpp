/*
 * Copyright (C) 2013-2015 Canonical Ltd.
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
 *              James Henstridge <james.henstridge@canonical.com>
 *              Michi Henning <michi.henning@canonical.com>
 */

#include <internal/imageextractor.h>

#include <internal/config.h>
#include <internal/file_io.h>
#include <internal/safe_strerror.h>
#include <internal/trace.h>

using namespace std;
using namespace unity::thumbnailer::internal;

ImageExtractor::ImageExtractor(std::string const& filename, chrono::milliseconds timeout)
    : filename_(filename)
    , timeout_ms_(timeout.count())
{
    process_.setStandardInputFile(QProcess::nullDevice());
    process_.setProcessChannelMode(QProcess::ForwardedChannels);
    connect(&process_, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            &ImageExtractor::processFinished);
    connect(&process_, static_cast<void (QProcess::*)(QProcess::ProcessError)>(&QProcess::error), this,
            &ImageExtractor::error);
    connect(&timer_, &QTimer::timeout, this, &ImageExtractor::timeout);
}

ImageExtractor::~ImageExtractor()
{
    if (process_.state() != QProcess::NotRunning)
    {
        // LCOV_EXCL_START
        process_.kill();
        if (!process_.waitForFinished(timeout_ms_))
        {
            qWarning().nospace() << "~ImageExtractor(): " << exe_path_ << " (pid" << process_.pid()
                       << ") did not exit after " << timeout_ms_ << " milliseconds";
        }
        // LCOV_EXCL_STOP
    }
}

void ImageExtractor::extract()
{
    // Gstreamer video pipelines are unstable so we need to run an
    // external helper library.
    char* utildir = getenv("TN_UTILDIR");
    exe_path_ = utildir ? utildir : SHARE_PRIV_ABS;
    exe_path_ += "/vs-thumb";

    if (!tmpfile_.open())
    {
        throw runtime_error("ImageExtractor::extract(): cannot open " + tmpfile_.fileTemplate().toStdString());  // LCOV_EXCL_LINE
    }
    process_.start(exe_path_, {QString::fromStdString(filename_), tmpfile_.fileName()});
    // Set a watchdog timer in case vs-thumb doesn't finish in time.
    timer_.setSingleShot(true);
    timer_.start(timeout_ms_);
}

string ImageExtractor::data()
{
    if (!error_.empty())
    {
        throw runtime_error(string("ImageExtractor::data(): ") + error_);
    }
    return read_file(tmpfile_.fileName().toStdString());
}

void ImageExtractor::processFinished()
{
    timer_.stop();
    switch (process_.exitStatus())
    {
        case QProcess::NormalExit:
            switch (process_.exitCode())
            {
                case 0:
                    error_ = "";
                    break;
                case 1:
                    error_ = "could not extract screenshot";
                    break;
                case 2:
                    error_ = "extractor pipeline failed";
                    break;
                default:
                    error_ = string("unknown exit status ") + to_string(process_.exitCode()) +
                             " from " + exe_path_.toStdString();
                    break;
            }
            break;
        case QProcess::CrashExit:
            if (error_.empty())
            {
                // Conditional because, if get a timeout and send a kill,
                // we don't want to overwrite the message set by timeout().
                error_ = exe_path_.toStdString() + " crashed";
            }
            break;
        default:
            abort();  // LCOV_EXCL_LINE  // Impossible
    }
    Q_EMIT finished();
}

void ImageExtractor::timeout()
{
    if (process_.state() != QProcess::NotRunning)
    {
        process_.kill();
    }
    error_ = exe_path_.toStdString() + " (pid " + to_string(process_.pid()) + ") did not return after " +
             to_string(timeout_ms_) + " milliseconds";
}

void ImageExtractor::error()
{
    if (process_.error() == QProcess::ProcessError::FailedToStart)
    {
        timer_.stop();
        error_ = string("failed to start ") + exe_path_.toStdString();
        Q_EMIT finished();
    }
}