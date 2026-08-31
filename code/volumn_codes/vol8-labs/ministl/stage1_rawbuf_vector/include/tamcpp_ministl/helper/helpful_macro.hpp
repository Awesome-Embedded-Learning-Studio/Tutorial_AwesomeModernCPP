#pragma once

#ifdef DISABLE_COPY
#    error "Oh no, DISABLE_COPY has been defined in your project!"
#endif

/**
 * @brief 这个地方故意最后不留一个分号，因为这样强调这是一个语句，就这么简单~
 *
 */
#define DISABLE_COPY(ClassName)           \
    ClassName(const ClassName&) = delete; \
    ClassName& operator=(const ClassName&) = delete
