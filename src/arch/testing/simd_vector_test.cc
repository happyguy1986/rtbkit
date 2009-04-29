/* cpuid_test.cc
   Jeremy Barnes, 21 February 2007
   Copyright (c) 2007 Jeremy Barnes.  All rights reserved.

   Test of the CPUID detection code.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "arch/simd_vector.h"

#include <boost/test/unit_test.hpp>
#include <vector>
#include <set>
#include <iostream>


using namespace ML;
using namespace std;

using boost::unit_test::test_suite;

BOOST_AUTO_TEST_CASE( test1 )
{
    BOOST_CHECK(cpuid_flags() != 0);
}
