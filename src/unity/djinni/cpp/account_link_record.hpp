// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#pragma once

#include <string>
#include <utility>

/**Data record representing a link between an account and an external service (e.g. Holdin.com) */
struct AccountLinkRecord final {
    /** What service is it (each service should use a unique string to identify itself)  */
    std::string serviceName;
    /** Any data unique to the service, e.g. a key used for secure communication or similar */
    std::string serviceData;

    AccountLinkRecord(std::string serviceName_,
                      std::string serviceData_)
    : serviceName(std::move(serviceName_))
    , serviceData(std::move(serviceData_))
    {}
};