#pragma once
#include <memory>
#include <print>
/**
 * @brief   AbstractBurger means the base type of burger,
 *          which base interfaces should be matched
 *
 */
class AbstractBurger {
  public:
    virtual void grill() = 0;
    virtual void prepare() = 0;
    virtual void wrap() = 0;
    virtual ~AbstractBurger() = default; /* this is required to the parent calss */
};

class McBurger : public AbstractBurger {
  public:
    void grill() override { std::print("Grilling McBurger...\n"); }

    void prepare() override {
        std::print("Preparing McBurger with lettuce, tomato, and special sauce...\n");
        std::print("McBurger is ready to be wrapped!\n");
    }

    void wrap() override {
        std::print("Wrapping McBurger in a paper wrapper...\n");
        std::print("McBurger is ready to be served!\n");
    }
};

class BurgerKingBurger : public AbstractBurger {
  public:
    void grill() override { std::print("Grilling Burger King Burger...\n"); }

    void prepare() override {
        std::print("Preparing Burger King Burger with lettuce, tomato, and special sauce...\n");
        std::print("Burger King Burger is ready to be wrapped!\n");
    }

    void wrap() override {
        std::print("Wrapping Burger King Burger in a paper wrapper...\n");
        std::print("Burger King Burger is ready to be served!\n");
    }
};

class BurgerKingCheeseBurger : public AbstractBurger {
  public:
    void grill() override { std::print("Grilling Burger King Cheese Burger...\n"); }

    void prepare() override {
        std::print("Preparing Burger King Cheese Burger with lettuce, tomato, cheese, and special "
                   "sauce...\n");
        std::print("Burger King Cheese Burger is ready to be wrapped!\n");
    }

    void wrap() override {
        std::print("Wrapping Burger King Cheese Burger in a paper wrapper...\n");
        std::print("Burger King Cheese Burger is ready to be served!\n");
    }
};

class McCheeseBurger : public AbstractBurger {
  public:
    void grill() override { std::print("Grilling McCheeseBurger...\n"); }

    void prepare() override {
        std::print("Preparing McCheeseBurger with lettuce, tomato, cheese, and special sauce...\n");
        std::print("McCheeseBurger is ready to be wrapped!\n");
    }

    void wrap() override {
        std::print("Wrapping McCheeseBurger in a paper wrapper...\n");
        std::print("McCheeseBurger is ready to be served!\n");
    }
};

class BurgerProvider {
  public:
    virtual std::unique_ptr<AbstractBurger>
    create_specifiedBurger(const std::string& specified_type) = 0;
    virtual ~BurgerProvider() = default;
};

class McBurgerProvider : public BurgerProvider {
  public:
    std::unique_ptr<AbstractBurger>
    create_specifiedBurger(const std::string& specified_type) override {
        if (specified_type == "normal") {
            return std::make_unique<McBurger>();
        } else {
            return std::make_unique<McCheeseBurger>();
        }
    }
};

class BurgerKingProvider : public BurgerProvider {
  public:
    std::unique_ptr<AbstractBurger>
    create_specifiedBurger(const std::string& specified_type) override {
        if (specified_type == "normal") {
            return std::make_unique<BurgerKingBurger>();
        } else {
            return std::make_unique<BurgerKingCheeseBurger>();
        }
    }
};
