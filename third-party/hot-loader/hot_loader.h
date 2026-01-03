#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <climits>
#include <unordered_map>
#include <filesystem>
#include <functional>
#include <algorithm>
#include <vector>

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

        // Check if this exact task is already registered
        auto& task_list = _tasks[file];
        for (const auto& task_info : task_list) {
            if (task_info.task == task) {
                return -3; // Task already registered
            }
        }

        // Register the file with inotify if not already watching
        int wd = -1;
        if (!task_list.empty()) {
            // File is already being watched, reuse the watch descriptor
            wd = task_list[0].task->watch_descriptor();
        } else {
            // Need to create a new watch
            wd = inotify_add_watch(_inotify_fd, file.c_str(), kWatchEventMask);
            if (wd < 0) {
                return -4; // Failed to add watch
            }
        }

        task->set_watch_descriptor(wd); // Set the watch descriptor in the task

        // Add the task to the list
        task_list.emplace_back(task, ownership);
        _watch_descriptors[wd] = file;

        return 0; // Success
    }

    int unregister_task(HotLoadTask* task) {
        if (!task) {
            return -1; // Invalid task pointer
        }

        if (!_initialized.load()) {
            return -2; // HotLoader not initialized
        }

        std::lock_guard<std::mutex> lock(_mutex); // Ensure thread safety

        const std::string& file = task->watch_file();
        auto it = _tasks.find(file);
        if (it == _tasks.end() || it->second.empty()) {
            return -4; // Task not found
        }

        auto& task_list = it->second;

        // Find and remove the specific task
        auto task_it = std::find_if(task_list.begin(), task_list.end(),
            [task](const TaskInfo& info) { return info.task == task; });

        if (task_it == task_list.end()) {
            return -4; // Task not found
        }

        // Delete the task if HotLoader owns it
        if (task_it->ownership == OWN_TASK) {
            delete task;
        }

        // Remove this task from the list
        task_list.erase(task_it);

        // If no more tasks for this file, remove the inotify watch
        if (task_list.empty()) {
            if (task->watch_descriptor() >= 0) {
                inotify_rm_watch(_inotify_fd, task->watch_descriptor());
                _watch_descriptors.erase(task->watch_descriptor());
            }
            _tasks.erase(it);
        }

        return 0; // Success
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
        if (it == _tasks.end() || it->second.empty()) {
            return -4; // Task not found
        }

        auto& task_list = it->second;

        // Remove all tasks for this file
        for (const auto& task_info : task_list) {
            HotLoadTask* task = task_info.task;
            task->set_watch_descriptor(-1); // Reset the watch descriptor

            if (task_info.ownership == OWN_TASK) {
                delete task; // Delete the task if HotLoader owns it
            }
        }

        // Remove the inotify watch
        if (!task_list.empty() && task_list[0].task->watch_descriptor() >= 0) {
            int wd = task_list[0].task->watch_descriptor();
            inotify_rm_watch(_inotify_fd, wd);
            _watch_descriptors.erase(wd);
        }

        _tasks.erase(it);

        return 0; // Success
    }

    int unregister_all_tasks() {
        if (!_initialized.load()) {
            return -1; // HotLoader not initialized
        }

        std::lock_guard<std::mutex> lock(_mutex); // Ensure thread safety

        for (auto& [file, task_list] : _tasks) {
            for (const auto& task_info : task_list) {
                HotLoadTask* task = task_info.task;
                if (task->watch_descriptor() >= 0) {
                    inotify_rm_watch(_inotify_fd, task->watch_descriptor());
                    _watch_descriptors.erase(task->watch_descriptor());
                }

                if (task_info.ownership == OWN_TASK) {
                    delete task; // Delete the task if HotLoader owns it
                }
            }
        }

        _tasks.clear();
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
                    const std::string& file = it->second;
                    auto task_it = _tasks.find(file);
                    if (task_it != _tasks.end()) {
                        for (const auto& task_info : task_it->second) {
                            HotLoadTask* task = task_info.task;

                            if (mask & IN_IGNORED) {
                                rewatch_task(task);
                            } else {
                                task->on_reload();
                            }
                        }
                    }
                }
            }
        }
    }

    void restart_stopped_tasks() {
        std::lock_guard<std::mutex> lock(_mutex); // Ensure thread safety

        for (auto& [file, task_list] : _tasks) {
            if (task_list.empty()) {
                continue;
            }

            // Check if any task needs restart
            if (task_list[0].task->watch_descriptor() < 0 &&
                std::filesystem::exists(file)) {

                // Tasks are stopped, try to restart them
                int wd = inotify_add_watch(_inotify_fd, file.c_str(),
                                        kWatchEventMask);
                if (wd >= 0) {
                    for (const auto& task_info : task_list) {
                        task_info.task->on_reload();
                        task_info.task->set_watch_descriptor(wd);
                    }
                    _watch_descriptors[wd] = file;
                }
            }
        }
    }

    void rewatch_task(HotLoadTask* task) {
        if (!task) {
            return;
        }

        const std::string& file = task->watch_file();

        // Find all tasks for this file
        auto it = _tasks.find(file);
        if (it == _tasks.end() || it->second.empty()) {
            return;
        }

        auto& task_list = it->second;

        // Remove old watch
        if (task_list[0].task->watch_descriptor() >= 0) {
            inotify_rm_watch(_inotify_fd, task_list[0].task->watch_descriptor());
            _watch_descriptors.erase(task_list[0].task->watch_descriptor());
        }

        if (!std::filesystem::exists(file)) {
            // Reset all watch descriptors if file does not exist
            for (const auto& task_info : task_list) {
                task_info.task->set_watch_descriptor(-1);
            }
            return;
        }

        // Add new watch
        int wd = inotify_add_watch(_inotify_fd, file.c_str(),
                                kWatchEventMask);
        if (wd >= 0) {
            for (const auto& task_info : task_list) {
                task_info.task->on_reload();
                task_info.task->set_watch_descriptor(wd);
            }
            _watch_descriptors[wd] = file;
        } else {
            // Reset all watch descriptors if rewatch fails
            for (const auto& task_info : task_list) {
                task_info.task->set_watch_descriptor(-1);
            }
        }
    }

private:
    struct TaskInfo {
        HotLoadTask* task;
        OwnerShip ownership;

        TaskInfo(HotLoadTask* t, OwnerShip o) : task(t), ownership(o) {}
        TaskInfo() : task(nullptr), ownership(DOESNT_OWN_TASK) {}
    };

    std::mutex _mutex; // Mutex to protect access to shared resources
    std::unordered_map<std::string, std::vector<TaskInfo>> _tasks; // Maps file paths to multiple HotLoadTask pointers
    std::unordered_map<int, std::string> _watch_descriptors; // Maps inotify watch descriptors to file paths
    int _inotify_fd = -1; // File descriptor for inotify
    int _epoll_fd = -1;   // File descriptor for epoll
    std::atomic<bool> _initialized = false; // Flag to indicate if HotLoader is initialized
    std::atomic<bool> _running = false; // Flag to control the running state
    std::thread _worker_thread; // Worker thread for monitoring file changes
};