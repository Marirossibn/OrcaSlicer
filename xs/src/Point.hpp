#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

class Point
{
    public:
    unsigned long x;
    unsigned long y;
    Point(unsigned long _x = 0, unsigned long _y = 0): x(_x), y(_y) {};
    ~Point();
    SV* _toPerl();
};

#endif
