//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// <string>

// basic_string<charT,traits,Allocator>&
//   assign(size_type n, charT c);

#include <string>
#include <cassert>

#include "min_allocator.h"
#include <crossbow/string.hpp>
using namespace crossbow;

template <class S>
void
test(S s, typename S::size_type n, typename S::value_type c, S expected) {
    s.assign(n, c);
    assert(s.__invariants());
    assert(s == expected);
}

int main() {
    {
        typedef string S;
        test(S(), 0, 'a', S());
        test(S(), 1, 'a', S(1, 'a'));
        test(S(), 10, 'a', S(10, 'a'));
        test(S(), 100, 'a', S(100, 'a'));

        test(S("12345"), 0, 'a', S());
        test(S("12345"), 1, 'a', S(1, 'a'));
        test(S("12345"), 10, 'a', S(10, 'a'));

        test(S("12345678901234567890"), 0, 'a', S());
        test(S("12345678901234567890"), 1, 'a', S(1, 'a'));
        test(S("12345678901234567890"), 10, 'a', S(10, 'a'));
    }
#if __cplusplus >= 201103L
    {
        typedef basic_string<char, std::char_traits<char>, min_allocator<char>> S;
        test(S(), 0, 'a', S());
        test(S(), 1, 'a', S(1, 'a'));
        test(S(), 10, 'a', S(10, 'a'));
        test(S(), 100, 'a', S(100, 'a'));

        test(S("12345"), 0, 'a', S());
        test(S("12345"), 1, 'a', S(1, 'a'));
        test(S("12345"), 10, 'a', S(10, 'a'));

        test(S("12345678901234567890"), 0, 'a', S());
        test(S("12345678901234567890"), 1, 'a', S(1, 'a'));
        test(S("12345678901234567890"), 10, 'a', S(10, 'a'));
    }
#endif
}
