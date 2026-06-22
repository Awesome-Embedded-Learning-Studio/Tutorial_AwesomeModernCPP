#pragma once
#include <memory>
#include <print>
#include <vector>
struct HomeTheaterBaseComponents {
  public:
    virtual ~HomeTheaterBaseComponents() = default;
    virtual void on() noexcept = 0;
    virtual void off() noexcept = 0;
};

struct DVDPlayer : public HomeTheaterBaseComponents {
  public:
    DVDPlayer() { std::print("DVDPlayer created\n"); }
    ~DVDPlayer() { std::print("DVDPlayer destroyed\n"); }
    void on() noexcept override { std::print("DVDPlayer is now ON\n"); }
    void off() noexcept override { std::print("DVDPlayer is now OFF\n"); }
};

struct Projector : public HomeTheaterBaseComponents {
  public:
    Projector() { std::print("Projector created\n"); }
    ~Projector() { std::print("Projector destroyed\n"); }
    void on() noexcept override { std::print("Projector is now ON\n"); }
    void off() noexcept override { std::print("Projector is now OFF\n"); };
};
struct LightController : public HomeTheaterBaseComponents {
  public:
    LightController() { std::print("LightController created\n"); }
    ~LightController() { std::print("LightController destroyed\n"); }
    void on() noexcept override { std::print("LightController is now ON\n"); }
    void off() noexcept override { std::print("LightController is now OFF\n"); }
};

struct SoundSystem : public HomeTheaterBaseComponents {
  public:
    SoundSystem() { std::print("SoundSystem created\n"); }
    ~SoundSystem() { std::print("SoundSystem destroyed\n"); }
    void on() noexcept override { std::print("SoundSystem is now ON\n"); }
    void off() noexcept override { std::print("SoundSystem is now OFF\n"); }
};

class HomeTheater {
  public:
    HomeTheater() {
        components.push_back(std::make_shared<LightController>());
        components.push_back(std::make_shared<DVDPlayer>());
        components.push_back(std::make_shared<SoundSystem>());
        components.push_back(std::make_shared<Projector>());
    }

    void watchMovie() {
        for (auto& each : components) {
            each->on();
        }
    }

    void closeMovie() {
        for (auto& each : components) {
            each->off();
        }
    }

  private:
    std::vector<std::shared_ptr<HomeTheaterBaseComponents>> components;
};
