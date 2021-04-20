#pragma once

#include <atomic>
#include "Disks/DiskFactory.h"
#include "Disks/Executor.h"
#include <Poco/DirectoryIterator.h>


namespace DB
{


/// Base Disk class for remote FS's, which are not posix-compatible (DiskS3 and DiskHDFS)
class IDiskRemote : public IDisk
{

friend class DiskRemoteReservation;

public:
    IDiskRemote(
        const String & disk_name_,
        const String & remote_fs_root_path_,
        const String & metadata_path_,
        const String & log_name_,
        std::unique_ptr<Executor> executor_ = std::make_unique<SyncExecutor>());

    struct Metadata;

    const String & getName() const override { return disk_name; }

    const String & getPath() const override { return metadata_path; }

    Metadata readMeta(const String & path) const;

    Metadata createMeta(const String & path) const;

    UInt64 getTotalSpace() const override { return std::numeric_limits<UInt64>::max(); }

    UInt64 getAvailableSpace() const override { return std::numeric_limits<UInt64>::max(); }

    UInt64 getUnreservedSpace() const override { return std::numeric_limits<UInt64>::max(); }

    UInt64 getKeepingFreeSpace() const override { return 0; }

    bool exists(const String & path) const override;

    bool isFile(const String & path) const override;

    void createFile(const String & path) override;

    void removeFile(const String & path) override { removeSharedFile(path, false); }

    void removeSharedFile(const String & path, bool keep_in_remote_fs) override;

    void removeSharedRecursive(const String & path, bool) override;

    void removeFileIfExists(const String & path) override;

    void removeRecursive(const String & path) override;

    size_t getFileSize(const String & path) const override;

    void moveFile(const String & from_path, const String & to_path) override;

    void replaceFile(const String & from_path, const String & to_path) override;

    void setReadOnly(const String & path) override;

    void listFiles(const String & path, std::vector<String> & file_names) override;

    bool isDirectory(const String & path) const override;

    void createDirectory(const String & path) override;

    void createDirectories(const String & path) override;

    void clearDirectory(const String & path) override;

    void moveDirectory(const String & from_path, const String & to_path) override { moveFile(from_path, to_path); }

    void removeDirectory(const String & path) override;

    DiskDirectoryIteratorPtr iterateDirectory(const String & path) override;

    void setLastModified(const String & path, const Poco::Timestamp & timestamp) override;

    Poco::Timestamp getLastModified(const String & path) override;

    void createHardLink(const String & src_path, const String & dst_path) override;

    ReservationPtr reserve(UInt64 bytes) override;

    virtual void removeFromRemoteFS(const Metadata & /* metadata */) {}

protected:
    void removeMetaRecursive(const String & path, bool keep_in_remote_fs);

    void removeMeta(const String & path, bool keep_in_remote_fs);

    const String disk_name;
    const String remote_fs_root_path;
    const String metadata_path;
    Poco::Logger * log;

private:
    bool tryReserve(UInt64 bytes);

    UInt64 reserved_bytes = 0;
    UInt64 reservation_count = 0;
    std::mutex reservation_mutex;
};

using RemoteDiskPtr = std::shared_ptr<IDiskRemote>;

/// Remote FS (S3, HDFS) metadata file layout:
/// Number of FS objects, Total size of all FS objects.
/// Each FS object represents path where object located in FS and size of object.

struct IDiskRemote::Metadata
{
    /// Metadata file version.
    static constexpr UInt32 VERSION_ABSOLUTE_PATHS = 1;
    static constexpr UInt32 VERSION_RELATIVE_PATHS = 2;
    static constexpr UInt32 VERSION_READ_ONLY_FLAG = 3;

    using PathAndSize = std::pair<String, size_t>;

    /// Remote FS (S3, HDFS) root path.
    const String & remote_fs_root_path;

    /// Disk path.
    const String & disk_path;

    /// Relative path to metadata file on local FS.
    String metadata_file_path;

    /// Total size of all remote FS (S3, HDFS) objects.
    size_t total_size;

    /// Remote FS (S3, HDFS) objects paths and their sizes.
    std::vector<PathAndSize> remote_fs_objects;

    /// Number of references (hardlinks) to this metadata file.
    UInt32 ref_count;

    /// Flag indicates that file is read only.
    bool read_only = false;

    /// Load metadata by path or create empty if `create` flag is set.
    Metadata(const String & remote_fs_root_path_,
            const String & disk_path_,
            const String & metadata_file_path_,
            bool create = false);

    void addObject(const String & path, size_t size);

    /// Fsync metadata file if 'sync' flag is set.
    void save(bool sync = false);

};


class RemoteDiskDirectoryIterator final : public IDiskDirectoryIterator
{
public:
    RemoteDiskDirectoryIterator(const String & full_path, const String & folder_path_) : iter(full_path), folder_path(folder_path_) {}

    void next() override { ++iter; }

    bool isValid() const override { return iter != Poco::DirectoryIterator(); }

    String path() const override
    {
        if (iter->isDirectory())
            return folder_path + iter.name() + '/';
        else
            return folder_path + iter.name();
    }

    String name() const override { return iter.name(); }

private:
    Poco::DirectoryIterator iter;
    String folder_path;
};


class DiskRemoteReservation final : public IReservation
{
public:
    DiskRemoteReservation(const RemoteDiskPtr & disk_, UInt64 size_)
        : disk(disk_), size(size_), metric_increment(CurrentMetrics::DiskSpaceReservedForMerge, size_)
    {
    }

    UInt64 getSize() const override { return size; }

    DiskPtr getDisk(size_t i) const override;

    Disks getDisks() const override { return {disk}; }

    void update(UInt64 new_size) override;

    ~DiskRemoteReservation() override;

private:
    RemoteDiskPtr disk;
    UInt64 size;
    CurrentMetrics::Increment metric_increment;
};

}
