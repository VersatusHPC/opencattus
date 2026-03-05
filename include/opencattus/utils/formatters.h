#ifndef OPENCATTUS_FORMATTERS_H_
#define OPENCATTUS_FORMATTERS_H_

#include <filesystem>
#include <fmt/format.h>

#include <boost/asio.hpp>

#include <opencattus/models/os.h>
#include <opencattus/patterns/wrapper.h>
#include <opencattus/utils/enums.h>

// Custom formatters for 3rd party types
template <>
struct fmt::formatter<std::filesystem::path> : formatter<string_view> {
    template <typename FormatContext>
    auto format(const std::filesystem::path& path, FormatContext& ctx) const
        -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "{}", path.string());
    }
};

template <>
struct fmt::formatter<boost::asio::ip::address> : formatter<string_view> {
    template <typename FormatContext>
    auto format(const boost::asio::ip::address& address,
        FormatContext& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "{}", address.to_string());
    }
};

// Custom formatters for our types
template <>
struct fmt::formatter<opencattus::models::OS> : formatter<string_view> {
    template <typename FormatContext>
    auto format(const opencattus::models::OS& osinfo, FormatContext& ctx) const
        -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "OS(distro={}, kernel={})",
            osinfo.getDistroString(), osinfo.getKernel().value_or(""));
    }
};

// Wrapper<T, Tag> formatter (delegates to T formatter)
template <typename T, typename Tag>
struct fmt::formatter<opencattus::Wrapper<T, Tag>> : formatter<string_view> {
    template <typename FormatContext>
    auto format(const opencattus::Wrapper<T, Tag>& wrapper,
        FormatContext& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "{}", wrapper.get());
    }
};

// Enum types formatter
template <typename E>
    requires std::is_enum_v<E>
struct fmt::formatter<E> : formatter<string_view> {
    template <typename FormatContext>
    auto format(const E& enumVal, FormatContext& ctx) const
        -> decltype(ctx.out())
    {
        return fmt::format_to(
            ctx.out(), "{}", opencattus::utils::enums::toString<E>(enumVal));
    }
};

#endif
