#ifndef slic3r_Polygon_hpp_
#define slic3r_Polygon_hpp_

#include <myinit.h>
#include <vector>
#include "Line.hpp"
#include "MultiPoint.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class Polygon : public MultiPoint {
    public:
    Point* last_point() const;
    SV* to_SV_ref();
    SV* to_SV_clone_ref() const;
    Lines lines() const;
    Polyline* split_at(const Point* point);
    Polyline* split_at_index(int index);
    Polyline* split_at_first_point();
    double area() const;
    bool is_counter_clockwise() const;
    bool is_clockwise() const;
    bool make_counter_clockwise();
    bool make_clockwise();
    bool is_valid() const;
};

typedef std::vector<Polygon> Polygons;

}

#endif
