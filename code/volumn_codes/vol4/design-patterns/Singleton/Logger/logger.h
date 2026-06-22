/**
 * @file logger.h
 * @author Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief logger defines here
 * @version 0.1
 * @date 2025-05-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#pragma once
#include <mutex>
#include <string>
class SimpleLogger {
  public:
    /**
     * @brief instance make the query of getting instances
     *
     * @return SimpleLogger& instance ref itself
     */
    static SimpleLogger& instance();
    /**
     * @brief log messages
     *
     * @param message message to log
     */
    void log_messages(const std::string& message);

  private:
    SimpleLogger();
    ~SimpleLogger() = default;
    /* Logger is nether copiable and movable */
    SimpleLogger& operator=(const SimpleLogger&) = delete;
    SimpleLogger(const SimpleLogger&) = delete;
    SimpleLogger(SimpleLogger&&) = delete;
    SimpleLogger& operator=(SimpleLogger&&) = delete;
};
