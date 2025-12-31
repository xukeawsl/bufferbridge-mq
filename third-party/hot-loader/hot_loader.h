#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <climits>
#include <unordered_map>
#include <filesystem>
#include <functional>

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/inotify.h>

class HotLoadTask {
    friend class HotLoader; // Allow HotLoader to access private members
public:

    HotLoadTask(const std::string& file)
        : _file(normalize_path(file)), _watch_descriptor(-1) {}

    virtual ~HotLoadTask() = default;

    const std::string& watch_file() const {
        return _file;
    }

    virtual void on_reload() {}

    static std::string normalize_path(const std::string& input_path) {
        try {
            if (!std::filesystem::exists(input_path) || !std::filesystem::is_regular_file(input_path)) {
                return {}; // Return empty string if path does not exist
            }

            // Convert to absolute path (if input is relative)
            std::filesystem::path absolute_path = std::filesystem::absolute(input_path);
            
            // Resolve symbolic links and eliminate redundancies
            std::filesystem::path canonical_path = std::filesystem::weakly_canonical(absolute_path);
            
            // Normalize the path format
            canonical_path = canonical_path.lexically_normal();
            canonical_path.make_preferred(); // Convert slashes to preferred format

            return canonical_path.string();
        } catch (...) {
            return {}; // Return empty string on error
        }
    }

private:
    int watch_descriptor() const {
        return _watch_descriptor;
    }

    void set_watch_descriptor(int wd) {
        _watch_descriptor = wd;
    }

private:
    std::string _file;
    int _watch_descriptor; // Inotify watch descriptor
};

class HotLoader final {
public:
    constexpr static int kMaxEventCount = 1024; // Maximum number of events to handle at once
    constexpr static int kEventBufferSize = 1024 * (sizeof(struct inotify_event) + NAME_MAX + 1); // Buffer size for inotify events
    constexpr static int kEpollTimeout = 1000; // Timeout for epoll_wait, -1 means wait indefinitely

    constexpr static int kWatchEventMask = IN_CLOSE_WRITE | IN_IGNORED;

    enum OwnerShip {
        OWN_TASK, // HotLoader owns the task and will delete it
        DOESNT_OWN_TASK // HotLoader does not own the task, caller is responsible for deletion
    };

    static HotLoader& instance() {
        static HotLoader instance;
        return instance;
    }

    int init() {
        if (_initialized.load()) {
            return 0; // Already initialized
        }

        _inotify_fd = inotify_init1(IN_NONBLOCK);
        if (_inotify_fd < 0) {
            return -1; // Failed to initialize inotify
        }

        _epoll_fd = epoll_create1(0);
        if (_epoll_fd < 0) {
            close_file_descriptors();
            return -2; // Failed to create epoll instance
        }

        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET; // Edge-triggered mode
        event.data.fd = _inotify_fd; // Associate the inotify fd with the event
        if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _inotify_fd, &event) < 0) {
            close_file_descriptors();
            perror("epoll_ctl failed");
            return -3; // Failed to add inotify fd to epoll
        }

        _initialized.store(true); // Mark HotLoader as initialized

        return 0;
    }

    int register_task(HotLoadTask* task, OwnerShip ownership) {
        if (!task) {
            return -1; // Invalid task pointer
        }

        if (!_initialized.load()) {
            return -2; // HotLoader not initialized
        }

        std::lock_guard<std::mutex> lock(_mutex); // Ensure thread safety
        
        const std::string& file = task->watch_file();
        if (_tasks.find(file) != _tasks.end()) {
            return -3; // Task already registered
        }

        // Register the file with inotify
        int wd = inotify_add_watch(_inotify_fd, file.c_str(), kWatchEventMask);
        if (wd < 0) {
            return -4; // Failed to add watch
        }

        task->set_watch_descriptor(wd); // Set the watch descriptor in the task

        _tasks[file] = task;
        _ownerships[file] = ownership;
        _watch_descriptors[wd] = task;

        return 0; // Success
    }

    int unregister_task(HotLoadTask* task) {
        if (!task) {
            return -1; // Invalid task pointer
        }
        return unregister_task(task->watch_file());
    }

    int unregister_task(const std::string& file) {
        if (!_initialized.load()) {
            return -2; // HotLoader not initialized
        }

        std::string normalize_file = HotLoadTask::normalize_path(file);
        if (normalize_file.empty()) {
            return -3; // Invalid file path
        }

        std::lock_guard<std::mutex> lock(_mutex); // Ensure thread safety

        auto it = _tasks.find(normalize_file);
        if (it == _tasks.end()) {
            return -4; // Task not found
        }

        HotLoadTask* task = it->second;
        _tasks.erase(it);

        if (task->watch_descriptor() >= 0) {
            // Remove the watch from inotify
            inotify_rm_watch(_inotify_fd, task->watch_descriptor());
            _watch_descriptors.erase(task->watch_descriptor());
        }

        task->set_watch_descriptor(-1); // Reset the watch descriptor

        if (_ownerships[normalize_file] == OWN_TASK) {
            delete task; // Delete the task if HotLoader owns it
        }

        _ownerships.erase(normalize_file); // Remove ownership information

        return 0; // Success
    }

    int unregister_all_tasks() {
        if (!_initialized.load()) {
            return -1; // HotLoader not initialized
        }

        std::lock_guard<std::mutex> lock(_mutex); // Ensure thread safety

        for (auto& [file, task] : _tasks) {
            if (task->watch_descriptor() >= 0) {
                inotify_rm_watch(_inotify_fd, task->watch_descriptor());
                _watch_descriptors.erase(task->watch_descriptor());
            }

            if (_ownerships[file] == OWN_TASK) {
                delete task; // Delete the task if HotLoader owns it
            }
        }

        _tasks.clear();
        _ownerships.clear();
        _watch_descriptors.clear();

        return 0; // Success
    }

    int run() {
        if (_running.load()) {
            return -1; // HotLoader already running
        }

        if (!_initialized.load()) {
            return -2; // HotLoader not initialized
        }

        _running.store(true); // Set the running flag to true

        _worker_thread = std::thread(std::bind(&HotLoader::work_loop, this));

        return 0; // Success
    }

    void stop() {
        _running.store(false); // Set the running flag to false
        
        if (_worker_thread.joinable()) {
            _worker_thread.join(); // Wait for the worker thread to finish
        }

        unregister_all_tasks(); // Unregister all tasks
    }

private:
    HotLoader() = default;
    HotLoader(const HotLoader&) = delete;
    HotLoader& operator=(const HotLoader&) = delete;
    HotLoader(HotLoader&&) = delete;
    HotLoader& operator=(HotLoader&&) = delete;

    ~HotLoader() {
        stop();
        close_file_descriptors();
    }

    void close_file_descriptors() {
        if (_inotify_fd >= 0) {
            close(_inotify_fd);
            _inotify_fd = -1;
        }
        if (_epoll_fd >= 0) {
            close(_epoll_fd);
            _epoll_fd = -1;
        }
    }

    void work_loop() {
        static struct epoll_event events[kMaxEventCount];
        static char event_buf[kEventBufferSize];

        while (_running.load()) {
            restart_stopped_tasks();

            int n_ready = epoll_wait(_epoll_fd, events, kMaxEventCount, kEpollTimeout);
            if (n_ready < 0) {
                if (errno == EINTR) {
                    continue; // Interrupted, retry
                }

                perror("epoll_wait failed");
                stop();
                return; // Error occurred
            }

            if (n_ready == 0) {
                continue; // No events ready, continue the loop
            }

            std::unordered_map<int, uint32_t> event_masks;

            for (int i = 0; i < n_ready; ++i) {
                if (events[i].data.fd == _inotify_fd) {
                    while (true) {
                        ssize_t len = read(_inotify_fd, event_buf, kEventBufferSize);
                        if (len < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                break; // No more events or interrupted, break the loop
                            } else if (errno == EINTR) {
                                continue; // Interrupted, retry reading
                            }

                            perror("read inotify events");
                            stop();
                            return; // Error occurred
                        }

                        for (char* ptr = event_buf; ptr < event_buf + len;) {
                            struct inotify_event* event = reinterpret_cast<struct inotify_event*>(ptr);
                            ptr += sizeof(struct inotify_event) + event->len;

                            event_masks[event->wd] |= event->mask; // Aggregate event masks
                        }
                    }
                }
            }

            // Process the aggregated events
            std::lock_guard<std::mutex> lock(_mutex); // Lock to ensure thread safety

            for (const auto& [wd, mask] : event_masks) {
                auto it = _watch_descriptors.find(wd);
                if (it != _watch_descriptors.end()) {
                    HotLoadTask* task = it->second;

                    if (mask & IN_IGNORED) {
                        rewatch_task(task);
                    } else {
                        task->on_reload();
                    }
                }
            }
        }
    }

    void restart_stopped_tasks() {
        std::lock_guard<std::mutex> lock(_mutex); // Ensure thread safety

        for (auto& [_, task] : _tasks) {
            if (task->watch_descriptor() < 0 &&
                std::filesystem::exists(task->watch_file())) {
                
                // Task is stopped, try to restart it
                int wd = inotify_add_watch(_inotify_fd, task->watch_file().c_str(), 
                                        kWatchEventMask);
                if (wd >= 0) {
                    task->on_reload();

                    task->set_watch_descriptor(wd);
                    _watch_descriptors[wd] = task;
                }
            }
        }
    }

    void rewatch_task(HotLoadTask* task) {
        if (!task) {
            return;
        }

        if (task->watch_descriptor() >= 0) {
            inotify_rm_watch(_inotify_fd, task->watch_descriptor());
        }

        if (!std::filesystem::exists(task->watch_file())) {
            task->set_watch_descriptor(-1); // Reset if file does not exist
            return;
        }

        int wd = inotify_add_watch(_inotify_fd, task->watch_file().c_str(), 
                                kWatchEventMask);
        if (wd >= 0) {
            task->on_reload();

            task->set_watch_descriptor(wd);
            _watch_descriptors[wd] = task;
        } else {
            task->set_watch_descriptor(-1); // Reset if rewatch fails
        }
    }

private:
    std::mutex _mutex; // Mutex to protect access to shared resources
    std::unordered_map<std::string, HotLoadTask*> _tasks; // Maps file paths to HotLoadTask pointers
    std::unordered_map<std::string, OwnerShip> _ownerships; // Maps file paths to ownership status
    std::unordered_map<int, HotLoadTask*> _watch_descriptors; // Maps inotify watch descriptors to HotLoadTask pointers
    int _inotify_fd = -1; // File descriptor for inotify
    int _epoll_fd = -1;   // File descriptor for epoll
    std::atomic<bool> _initialized = false; // Flag to indicate if HotLoader is initialized
    std::atomic<bool> _running = false; // Flag to control the running state
    std::thread _worker_thread; // Worker thread for monitoring file changes
};