#include <functional>
#include <memory>
struct AnimalSound {
    ~AnimalSound() = default;
    AnimalSound(std::function<void(void)> snd) { sound = snd; }
    void makeSound() noexcept { sound(); }

  private:
    std::function<void(void)> sound;
};

struct AnimalType {
    virtual ~AnimalType() = default;
    AnimalType(std::shared_ptr<AnimalSound> snd) { sound = snd; }
    void install_new_sound(std::shared_ptr<AnimalSound> sound) { this->sound = sound; }
    void playSound() {
        if (sound) {
            sound->makeSound();
        }
    }

  private:
    std::shared_ptr<AnimalSound> sound;
};
