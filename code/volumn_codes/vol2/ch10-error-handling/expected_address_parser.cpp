#include <string>
#include <string_view>
#include <expected>
#include <iostream>
#include <charconv>

struct AddressError {
    enum Code {
        kEmptyInput,
        kMissingPort,
        kInvalidHost,
        kInvalidPort,
        kPortOutOfRange,
    } code;
    std::string detail;
};

struct NetworkAddress {
    std::string host;
    int port;
};

std::expected<std::string, AddressError> validate_input(
    std::string_view input) {
    if (input.empty()) {
        return std::unexpected(AddressError{
            AddressError::kEmptyInput, "Input is empty"});
    }
    return std::string(input);
}

std::expected<NetworkAddress, AddressError> split_address(
    std::string input) {
    auto colon = input.rfind(':');
    if (colon == std::string::npos) {
        return std::unexpected(AddressError{
            AddressError::kMissingPort,
            "No port specified: " + input});
    }

    NetworkAddress addr;
    addr.host = input.substr(0, colon);
    if (addr.host.empty()) {
        return std::unexpected(AddressError{
            AddressError::kInvalidHost, "Host is empty"});
    }

    auto port_str = input.substr(colon + 1);
    int port = 0;
    auto [ptr, ec] = std::from_chars(
        port_str.data(), port_str.data() + port_str.size(), port);
    if (ec != std::errc{} || ptr != port_str.data() + port_str.size()) {
        return std::unexpected(AddressError{
            AddressError::kInvalidPort,
            "Port is not a number: " + std::string(port_str)});
    }
    if (port < 1 || port > 65535) {
        return std::unexpected(AddressError{
            AddressError::kPortOutOfRange,
            "Port out of range: " + std::to_string(port)});
    }
    addr.port = port;
    return addr;
}

int main() {
    auto result = validate_input("192.168.1.1:8080")
        .and_then(split_address)
        .transform([](const NetworkAddress& a) -> std::string {
            return a.host + ":" + std::to_string(a.port);
        })
        .or_else([](const AddressError& e) -> std::expected<std::string, AddressError> {
            std::cerr << "Error: " << e.detail << "\n";
            return std::unexpected(e);
        });

    if (result) {
        std::cout << "Address: " << *result << "\n";
    }
}
