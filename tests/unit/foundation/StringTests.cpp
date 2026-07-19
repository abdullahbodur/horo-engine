#include <catch2/catch_test_macros.hpp>

#include "Horo/Foundation/String.h"

#include <string>

namespace
{
TEST_CASE("Empty And Whitespace Only Strings Are Blank", "[unit][foundation]")
{
    REQUIRE((Horo::Text::IsBlank("")));
    REQUIRE((Horo::Text::IsBlank(" \t\n\r\f\v")));
}

TEST_CASE("Strings Containing Visible Characters Are Not Blank", "[unit][foundation]")
{
    REQUIRE((!Horo::Text::IsBlank("horo")));
    REQUIRE((!Horo::Text::IsBlank("  horo  ")));
}

TEST_CASE("Characters With Negative Plain Char Values Are Handled Safely", "[unit][foundation]")
{
    constexpr std::string value(1, static_cast<char>(0xFF));
    REQUIRE((!Horo::Text::IsBlank(value)));
}
} // namespace
