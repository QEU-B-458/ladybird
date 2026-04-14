/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/OwnPtr.h>
#include <AK/String.h>
#include <AK/StringView.h>

namespace MyceliumVR {

enum class MountPermissions {
    ReadOnly,
    ReadWrite,
};

class Mount {
public:
    virtual ~Mount() = default;

    virtual ErrorOr<ByteBuffer> read_file(StringView relative_path) const = 0;
    virtual ErrorOr<void> write_file(StringView relative_path, ReadonlyBytes) = 0;
    virtual bool exists(StringView relative_path) const = 0;
};

class DirectoryMount final : public Mount {
public:
    explicit DirectoryMount(ByteString root_path);

    virtual ErrorOr<ByteBuffer> read_file(StringView relative_path) const override;
    virtual ErrorOr<void> write_file(StringView relative_path, ReadonlyBytes) override;
    virtual bool exists(StringView relative_path) const override;

private:
    ErrorOr<ByteString> resolve(StringView relative_path) const;

    ByteString m_root_path;
};

class VirtualFileSystem {
public:
    ErrorOr<void> mount(StringView prefix, NonnullOwnPtr<Mount>, MountPermissions = MountPermissions::ReadOnly);
    ErrorOr<void> mount_directory(StringView prefix, ByteString root_path, MountPermissions = MountPermissions::ReadOnly);

    ErrorOr<ByteBuffer> read_file(StringView virtual_path) const;
    ErrorOr<void> write_file(StringView virtual_path, ReadonlyBytes);
    bool exists(StringView virtual_path) const;

private:
    struct ResolvedPath {
        Mount const* mount { nullptr };
        Mount* mutable_mount { nullptr };
        MountPermissions permissions { MountPermissions::ReadOnly };
        StringView relative_path;
    };

    ErrorOr<ResolvedPath> resolve(StringView virtual_path) const;
    static ErrorOr<String> normalize_prefix(StringView prefix);

    HashMap<String, NonnullOwnPtr<Mount>> m_mounts;
    HashMap<String, MountPermissions> m_permissions;
};

}
