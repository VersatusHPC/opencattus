#pragma once

#include <ranges>
#include <utility>
#include <version>

namespace opencattus::utils::ranges {

#if defined(__cpp_lib_ranges_to_container)
#if __cpp_lib_ranges_to_container >= 202202L
#define OPENCATTUS_HAS_STD_RANGES_TO 1
#endif
#endif

#if !defined(OPENCATTUS_HAS_STD_RANGES_TO)
#define OPENCATTUS_HAS_STD_RANGES_TO 0
#endif

template <typename Container, std::ranges::input_range Range>
[[nodiscard]] auto to(Range&& range) -> Container
{
#if OPENCATTUS_HAS_STD_RANGES_TO
    return std::forward<Range>(range) | std::ranges::to<Container>();
#else
    Container output;
    if constexpr (std::ranges::sized_range<Range>
        && requires(Container& container, typename Container::size_type size) {
               container.reserve(size);
           }) {
        const auto count = static_cast<typename Container::size_type>(
            std::ranges::size(range));
        output.reserve(count);
    }

    for (auto&& value : range) {
        output.emplace_back(std::forward<decltype(value)>(value));
    }
    return output;
#endif
}

template <template <typename...> typename Container,
    std::ranges::input_range Range>
[[nodiscard]] auto to(Range&& range)
{
#if OPENCATTUS_HAS_STD_RANGES_TO
    return std::forward<Range>(range) | std::ranges::to<Container>();
#else
    return to<Container<std::ranges::range_value_t<Range>>>(
        std::forward<Range>(range));
#endif
}

template <typename Container> struct ToClosure {
    template <std::ranges::input_range Range>
    friend auto operator|(Range&& range, ToClosure) -> Container
    {
        return to<Container>(std::forward<Range>(range));
    }
};

template <typename Container> [[nodiscard]] auto to() -> ToClosure<Container>
{
    return { };
}

template <template <typename...> typename Container> struct ToTemplateClosure {
    template <std::ranges::input_range Range>
    friend auto operator|(Range&& range, ToTemplateClosure)
    {
        return to<Container>(std::forward<Range>(range));
    }
};

template <template <typename...> typename Container>
[[nodiscard]] auto to() -> ToTemplateClosure<Container>
{
    return { };
}

#undef OPENCATTUS_HAS_STD_RANGES_TO

} // namespace opencattus::utils::ranges
