#include "BurgerCreator.h"

void process_burger_session(std::unique_ptr<AbstractBurger>& burger) {
    burger->grill();
    burger->prepare();
    burger->wrap();
}

int main() {
    std::unique_ptr<BurgerProvider> mcBurgerCreator = std::make_unique<McBurgerProvider>();
    std::unique_ptr<AbstractBurger> mcBurger = mcBurgerCreator->create_specifiedBurger("normal");

    process_burger_session(mcBurger);

    std::unique_ptr<AbstractBurger> mcCheessBurger =
        mcBurgerCreator->create_specifiedBurger("cheese");
    process_burger_session(mcCheessBurger);

    std::unique_ptr<BurgerProvider> bkBurgerCreator = std::make_unique<BurgerKingProvider>();
    std::unique_ptr<AbstractBurger> bkBurger = bkBurgerCreator->create_specifiedBurger("cheese");
    process_burger_session(bkBurger);

    std::unique_ptr<AbstractBurger> bkNormalBurger =
        bkBurgerCreator->create_specifiedBurger("normal");
    process_burger_session(bkNormalBurger);

    return 0;
}
