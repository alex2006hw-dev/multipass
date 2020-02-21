/*
 * Copyright (C) 2017-2019 Canonical, Ltd.
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

#include <multipass/sshfs_mount/sshfs_mount.h>

#include <multipass/exceptions/sshfs_missing_error.h>
#include <multipass/logging/log.h>
#include <multipass/ssh/ssh_session.h>
#include <multipass/sshfs_mount/sftp_server.h>
#include <multipass/utils.h>

#include <multipass/format.h>

#include <iostream>

namespace mp = multipass;
namespace mpl = multipass::logging;

namespace
{
constexpr auto category = "sshfs mount";
template <typename Callable>
auto run_cmd(mp::SSHSession& session, std::string&& cmd, Callable&& error_handler)
{
    auto ssh_process = session.exec(cmd);
    if (ssh_process.exit_code() != 0)
        error_handler(ssh_process);
    return ssh_process.read_std_output();
}

// Run a command on a given SSH session.
auto run_cmd(mp::SSHSession& session, std::string&& cmd)
{
    auto error_handler = [](mp::SSHProcess& proc) { throw std::runtime_error(proc.read_std_error()); };
    return run_cmd(session, std::forward<std::string>(cmd), error_handler);
}

// Check if sshfs exists on a given SSH session.
void check_sshfs_exists(mp::SSHSession& session)
{
    auto error_handler = [](mp::SSHProcess& proc) {
        mpl::log(mpl::Level::warning, category,
                 fmt::format("Unable to determine if 'sshfs' is installed: {}", proc.read_std_error()));
        throw mp::SSHFSMissingError();
    };

    run_cmd(session, "which sshfs", error_handler);
}

// Return the home directory of the current user.
std::string get_home_dir(mp::SSHSession& session)
{
    std::string home = run_cmd(session, "bash -c 'echo ${HOME}'");

    home.pop_back(); // Remove the trailing newline.
    home += "/";

    return home;
}

// Given any path, return it expressed as an absolute path.
std::string get_absolute_path(mp::SSHSession& session, const std::string& path)
{
    return *(path.begin()) == '/' ? path : get_home_dir(session) + '/' + path;
}

// Given a path, returns the part of the path which exists.
std::string get_existing_dirs(mp::SSHSession& session, const std::string& path)
{
    std::string existing = run_cmd(
        session, fmt::format("/bin/bash -c 'P=\"{}\"; while [ ! -d \"$P/\" ]; do P=${{P%/*}}; done; echo $P/'", path));

    // Strip the trailing newline from the string.
    existing.pop_back();

    return existing;
}

// Create a directory on a given root folder.
void make_target_dir(mp::SSHSession& session, const std::string& root, const std::string& target)
{
    if (!target.empty())
        run_cmd(session, fmt::format("cd \"{}\" && sudo mkdir -p \"{}\"", root, target));
}

// Set ownership of all directories on a path starting on a given root.
// Assume it is already created.
void set_owner_for(mp::SSHSession& session, const std::string& root, const std::string& target)
{
    auto vm_user = run_cmd(session, "id -nu");
    auto vm_group = run_cmd(session, "id -ng");
    mp::utils::trim_end(vm_user);
    mp::utils::trim_end(vm_group);

    run_cmd(session, fmt::format("/bin/bash -c 'cd \"{}\" && P=\"{}\"; Q=$P; while [ ! -z \"$Q\" ]; do sudo chown "
                                 "{}:{} $P; Q=`echo $P | cut -f 1 -d / -s`; P=${{P%/*}}; done'",
                                 root, target, vm_user, vm_group));
}

auto make_sftp_server(mp::SSHSession&& session, const std::string& source, const std::string& target,
                      const std::unordered_map<int, int>& gid_map, const std::unordered_map<int, int>& uid_map)
{
    mpl::log(mpl::Level::debug, category,
             fmt::format("{}:{} {}(source = {}, target = {}, …): ", __FILE__, __LINE__, __FUNCTION__, source, target));

    check_sshfs_exists(session);

    // If the path is not absolute, we assume it is relative to users home.
    std::string absolute_path = get_absolute_path(session, target);

    // Split the path in existing and missing parts.
    std::string leading(get_existing_dirs(session, absolute_path));
    std::string missing = leading.size() >= absolute_path.size()
                              ? ""
                              : absolute_path.substr(leading.size(), absolute_path.size() - leading.size());

    // We need to create the part of the path which does not still exist,
    // and set then the correct ownership.
    make_target_dir(session, leading, missing);
    set_owner_for(session, leading, missing);

    auto output = run_cmd(session, "id -u");
    mpl::log(mpl::Level::debug, category,
             fmt::format("{}:{} {}(): `id -u` = {}", __FILE__, __LINE__, __FUNCTION__, output));
    auto default_uid = std::stoi(output);
    output = run_cmd(session, "id -g");
    mpl::log(mpl::Level::debug, category,
             fmt::format("{}:{} {}(): `id -g` = {}", __FILE__, __LINE__, __FUNCTION__, output));
    auto default_gid = std::stoi(output);

    return std::make_unique<mp::SftpServer>(std::move(session), source, absolute_path, gid_map, uid_map, default_uid,
                                            default_gid);
}

} // namespace

mp::SshfsMount::SshfsMount(SSHSession&& session, const std::string& source, const std::string& target,
                           const std::unordered_map<int, int>& gid_map, const std::unordered_map<int, int>& uid_map)
    : sftp_server{make_sftp_server(std::move(session), source, get_absolute_path(session, target), gid_map, uid_map)},
      sftp_thread{[this] {
          std::cout << "Connected" << std::endl;
          sftp_server->run();
          std::cout << "Stopped" << std::endl;
      }}
{
}

mp::SshfsMount::~SshfsMount()
{
    stop();
}

void mp::SshfsMount::stop()
{
    sftp_server->stop();
    if (sftp_thread.joinable())
        sftp_thread.join();
}
