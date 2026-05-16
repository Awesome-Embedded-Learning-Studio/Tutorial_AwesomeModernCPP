cmake_minimum_required(VERSION 3.20)
project(cppcon2025_01 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Article 01: Narrowing and type safety
add_executable(01-01-narrowing-demo 01-01-narrowing-demo.cpp)
add_executable(01-02-number-concept 01-02-number-concept.cpp)
add_executable(01-03-narrowing-concept 01-03-narrowing-concept.cpp)
add_executable(01-04-narrow-convert 01-04-narrow-convert.cpp)
add_executable(01-05-number-arithmetic 01-05-number-arithmetic.cpp)
add_executable(01-06-overflow-not-caught 01-06-overflow-not-caught.cpp)

# Article 02: Operators and span check
add_executable(02-01-safe-int-operators 02-01-safe-int-operators.cpp)
add_executable(02-02-comparison-trap 02-02-comparison-trap.cpp)
add_executable(02-03-safe-comparison 02-03-safe-comparison.cpp)
add_executable(02-04-checked-span 02-04-checked-span.cpp)
add_executable(02-05-span-helpers 02-05-span-helpers.cpp)

# Article 03: Generic programming and range
add_executable(03-01-ctad-number 03-01-ctad-number.cpp)
add_executable(03-02-safe-sort-concepts 03-02-safe-sort-concepts.cpp)
add_executable(03-03-range-sort 03-03-range-sort.cpp)
add_executable(03-04-sortable-iterator-requirement 03-04-sortable-iterator-requirement.cpp)
add_executable(03-05-can-add-concept 03-05-can-add-concept.cpp)

# Article 04: From template to concept
add_executable(04-01-forward-sort-overload 04-01-forward-sort-overload.cpp)
add_executable(04-02-use-patterns 04-02-use-patterns.cpp)

# Article 05: Syntax unification and detection
add_executable(05-01-smart-ptr-concept 05-01-smart-ptr-concept.cpp)
add_executable(05-02-safe-pair 05-02-safe-pair.cpp)

# Article 06: Concept beyond param and OOP
add_executable(06-01-power-of-two-concept 06-01-power-of-two-concept.cpp)
add_executable(06-02-oop-vs-generic-draw 06-02-oop-vs-generic-draw.cpp)

# Article 07: Concept iteration and beyond
add_executable(07-01-partial-concept-number 07-01-partial-concept-number.cpp)
add_executable(07-02-concept-function-param 07-02-concept-function-param.cpp)

# Article 08: Template instantiation and type system
add_executable(08-01-advance-if-constexpr 08-01-advance-if-constexpr.cpp)
add_executable(08-02-concept-type-system 08-02-concept-type-system.cpp)
add_executable(08-03-concept-param-demo 08-03-concept-param-demo.cpp)
add_executable(08-04-safe-numeric-convert 08-04-safe-numeric-convert.cpp)

# Article 09: Value params, inheritance and future
add_executable(09-01-concept-value-param 09-01-concept-value-param.cpp)
