#include "WeatherForecast.h"

int main() {
    WeatherForecast forcast;

    forcast.registerNewObserver(std::make_shared<DefaultSender>());
    forcast.registerNewObserver(std::make_shared<PhoneSender>());
    forcast.registerNewObserver(std::make_shared<TVSender>());

    forcast.notifies_once();
}
