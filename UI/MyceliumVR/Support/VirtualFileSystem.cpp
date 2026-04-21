/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VirtualFileSystem.h"

#include <AK/LexicalPath.h>
#include <LibCore/File.h>
#include <LibCore/System.h>

namespace MyceliumVR {

DirectoryMount::DirectoryMount(ByteString root_path)
    : m_root_path(LexicalPath::canonicalized_path(move(root_path)))
{
}

ErrorOr<ByteBuffer> DirectoryMount::read_file(StringView relative_path) const
{
    auto host_path = TRY(resolve(relative_path));
    auto file = TRY(Core::File::open(host_path, Core::File::OpenMode::Read));
    return TRY(file->read_until_eof());
}

ErrorOr<void> DirectoryMount::write_file(StringView relative_path, ReadonlyBytes bytes)
{
    auto host_path = TRY(resolve(relative_path));
    auto file = TRY(Core::File::open(host_path, Core::File::OpenMode::Write | Core::File::OpenMode::Truncate));
    TRY(file->write_until_depleted(bytes));
    return {};
}

bool DirectoryMount::exists(StringView relative_path) const
{
    auto host_path = resolve(relative_path);
    if (host_path.is_error())
        return false;
    return !Core::System::stat(host_path.value()).is_error();
}

ErrorOr<ByteString> DirectoryMount::resolve(StringView relative_path) const
{
    if (relative_path.starts_with('/'))
        return Error::from_string_literal("Virtual path escaped mount root");
    if (relative_path == ".."sv || relative_path.starts_with("../"sv) || relative_path.contains("/../"sv) || relative_path.ends_with("/.."sv))
        return Error::from_string_literal("Virtual path escaped mount root");

    return LexicalPath::join(m_root_path, relative_path).string();
}

ErrorOr<void> VirtualFileSystem::mount(StringView prefix, NonnullOwnPtr<Mount> mount, MountPermissions permissions)
{
    auto normalized_prefix = TRY(normalize_prefix(prefix));
    m_permissions.set(normalized_prefix, permissions);
    m_mounts.set(move(normalized_prefix), move(mount));
    return {};
}

ErrorOr<void> VirtualFileSystem::mount_directory(StringView prefix, ByteString root_path, MountPermissions permissions)
{
    return mount(prefix, make<DirectoryMount>(move(root_path)), permissions);
}

ErrorOr<void> VirtualFileSystem::unmount(StringView prefix)
{
    auto normalized_prefix = TRY(normalize_prefix(prefix));
    if (!m_mounts.remove(normalized_prefix))
        return Error::from_string_literal("Virtual filesystem mount prefix was not mounted");
    m_permissions.remove(normalized_prefix);
    return {};
}

ErrorOr<ByteBuffer> VirtualFileSystem::read_file(StringView virtual_path) const
{
    auto resolved = TRY(resolve(virtual_path));
    return TRY(resolved.mount->read_file(resolved.relative_path));
}

ErrorOr<void> VirtualFileSystem::write_file(StringView virtual_path, ReadonlyBytes bytes)
{
    auto resolved = TRY(resolve(virtual_path));
    if (resolved.permissions != MountPermissions::ReadWrite)
        return Error::from_string_literal("Virtual filesystem mount is read-only");
    VERIFY(resolved.mutable_mount);
    return TRY(resolved.mutable_mount->write_file(resolved.relative_path, bytes));
}

bool VirtualFileSystem::exists(StringView virtual_path) const
{
    auto resolved = resolve(virtual_path);
    if (resolved.is_error())
        return false;
    return resolved.value().mount->exists(resolved.value().relative_path);
}

ErrorOr<VirtualFileSystem::ResolvedPath> VirtualFileSystem::resolve(StringView virtual_path) const
{
    Optional<ResolvedPath> best_match;
    size_t best_prefix_length = 0;

    for (auto const& entry : m_mounts) {
        auto prefix = entry.key.bytes_as_string_view();
        if (!virtual_path.starts_with(prefix))
            continue;
        if (prefix.length() <= best_prefix_length)
            continue;

        best_prefix_length = prefix.length();
        best_match = ResolvedPath {
            .mount = entry.value.ptr(),
            .mutable_mount = entry.value.ptr(),
            .permissions = m_permissions.get(prefix).value_or(MountPermissions::ReadOnly),
            .relative_path = virtual_path.substring_view(prefix.length()),
        };
    }

    if (!best_match.has_value())
        return Error::from_string_literal("No virtual filesystem mount matched path");

    return best_match.release_value();
}

Vector<String> VirtualFileSystem::mount_prefixes() const
{
    Vector<String> prefixes;
    for (auto const& [prefix, _] : m_mounts)
        prefixes.append(prefix);
    return prefixes;
}

ErrorOr<String> VirtualFileSystem::normalize_prefix(StringView prefix)
{
    if (!prefix.contains("://"sv))
        return Error::from_string_literal("Virtual filesystem mount prefix must include a scheme");
    if (!prefix.ends_with('/'))
        return MUST(String::formatted("{}/", prefix));
    return String::from_utf8(prefix.bytes());
}

}
