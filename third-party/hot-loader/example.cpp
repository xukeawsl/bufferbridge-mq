#include <iostream>
#include <thread>

#include "hot_loader.h"

class MyHotLoadTask : public HotLoadTask {
public:
    MyHotLoadTask(const std::string& file) : HotLoadTask(file) {}

    void on_reload() override {
        // Custom reload logic
        std::cout << "Reloading task for file: " << watch_file() << std::endl;
    }
};

// g++ example.cpp -o example -lpthread -std=c++17
int main() {
    MyHotLoadTask task("config1.json");

    if (HotLoader::instance().init() != 0) {
        std::cerr << "Failed to initialize HotLoader" << std::endl;
        return 1;
    }

    int ret = HotLoader::instance().register_task(&task, HotLoader::DOESNT_OWN_TASK);
    if (ret != 0) {
        std::cerr << "Failed to register task: " << ret << std::endl;
        return 1;
    }

    if (HotLoader::instance().run() != 0) {
        std::cerr << "Failed to start HotLoader" << std::endl;
        return 1;
    }
    std::cout << "HotLoader is running. Press Enter to stop..." << std::endl;

    std::thread([]() {
        int ret;
        ret = HotLoader::instance().register_task(new MyHotLoadTask("config2.json"), HotLoader::OWN_TASK);
        if (ret != 0) {
            std::cerr << "Failed to register task: " << ret << std::endl;
        } else {
            std::cout << "Task registered successfully." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(10));
        ret = HotLoader::instance().unregister_task("config2.json");
        if (ret != 0) {
            std::cerr << "Failed to unregister task: " << ret << std::endl;
        } else {
            std::cout << "Task unregistered successfully." << std::endl;
        }
    }).detach();

    std::cin.get(); // Wait for user input to stop
    HotLoader::instance().unregister_task(&task);
    std::cout << "Task unregistered." << std::endl;
    HotLoader::instance().stop();
    std::cout << "HotLoader stopped." << std::endl;

    return 0;
}