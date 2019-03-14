/*
 * Copyright (C) 2019 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <multipass/process.h>
#include <multipass/process_factory.h>
#include <multipass/process_spec.h>

namespace mp = multipass;

namespace
{
class UnsecuredProcess : public mp::Process
{
public:
    UnsecuredProcess(std::unique_ptr<mp::ProcessSpec>&& process_spec) : mp::Process(process_spec->error_log_level())
    {
        setProgram(process_spec->program());
        setArguments(process_spec->arguments());
        setProcessEnvironment(process_spec->environment());
        if (!process_spec->working_directory().isNull())
            setWorkingDirectory(process_spec->working_directory());
    }
};
} // namespace

// This is the default ProcessFactory that creates a Process with no security mechanisms enabled
std::unique_ptr<mp::Process> mp::ProcessFactory::create_process(std::unique_ptr<mp::ProcessSpec>&& process_spec) const
{
    return std::make_unique<::UnsecuredProcess>(std::move(process_spec));
}