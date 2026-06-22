#include "AnimalSound.h"
#include <print>
int main() {
    auto dogSound = std::make_shared<AnimalSound>([]() { std::println("Wang!Wang!Wang!"); });

    auto catSound = std::make_shared<AnimalSound>([]() { std::println("Mewo!Mewo!Mewo!"); });

    AnimalType dog(dogSound);
    dog.playSound();

    AnimalType cat(catSound);
    cat.playSound();

    dog.install_new_sound(catSound);
    std::println("What the fuck!");
    dog.playSound();
}
