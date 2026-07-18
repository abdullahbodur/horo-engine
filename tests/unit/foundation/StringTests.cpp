#include "Horo/Foundation/String.h"

#include <cassert>
#include <string>

namespace
{
void EmptyAndWhitespaceOnlyStringsAreBlank()
{
    assert(Horo::Text::IsBlank(""));
    assert(Horo::Text::IsBlank(" \t\n\r\f\v"));
}

void StringsContainingVisibleCharactersAreNotBlank()
{
    assert(!Horo::Text::IsBlank("horo"));
    assert(!Horo::Text::IsBlank("  horo  "));
}

void CharactersWithNegativePlainCharValuesAreHandledSafely()
{
    const std::string value(1, static_cast<char>(0xFF));
    assert(!Horo::Text::IsBlank(value));
}
}

int main()
{
    EmptyAndWhitespaceOnlyStringsAreBlank();
    StringsContainingVisibleCharactersAreNotBlank();
    CharactersWithNegativePlainCharValuesAreHandledSafely();
    return 0;
}
