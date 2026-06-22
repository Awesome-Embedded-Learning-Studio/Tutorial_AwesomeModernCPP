#pragma once
#include <memory>
#include <print>
#include <vector>
struct MessagePackage {
    double temperature;
    double humidity;
};

struct Sender {
    virtual ~Sender() = default;
    virtual void receivingMessage(const MessagePackage& message) = 0;
};

struct DefaultSender : Sender {
    void receivingMessage(const MessagePackage& message) override {
        std::println("Receiving Message: Temperature: {}, Humidity: {}", message.temperature,
                     message.humidity);
        pack = message;
    }
    // you can process other sessions like the historical sessions
    void print_current_cached() { receivingMessage(pack); }

  private:
    MessagePackage pack;
};

struct PhoneSender : Sender {
    void receivingMessage(const MessagePackage& message) override {
        std::println("Hello from Message! Temperature: {}, Humidity: {}", message.temperature,
                     message.humidity);
        pack = message;
    }
    // you can process other sessions like the historical sessions
    void print_current_cached() { receivingMessage(pack); }

  private:
    MessagePackage pack;
};

struct TVSender : Sender {
    void receivingMessage(const MessagePackage& message) override {
        std::println("Hello from TV! Temperature: {}, Humidity: {}", message.temperature,
                     message.humidity);
        pack = message;
    }
    // you can process other sessions like the historical sessions
    void print_current_cached() { receivingMessage(pack); }

  private:
    MessagePackage pack;
};

struct WeatherForecast {

    struct WeatherSensor {
        static MessagePackage get_message_pack() {
            return {std::rand() % 20 + 10.0, std::rand() % 50 + 10.0};
        }
    };

    void registerNewObserver(std::shared_ptr<Sender> sender) {
        observers.push_back(std::move(sender));
    }

    void detachedObserver(std::shared_ptr<Sender> sender) {
        observers.erase(std::remove(observers.begin(), observers.end(), sender));
    }

    void notifies_once() {
        for (auto& each : observers) {
            each->receivingMessage(WeatherSensor::get_message_pack());
        }
    }

  private:
    std::vector<std::shared_ptr<Sender>> observers;
};
