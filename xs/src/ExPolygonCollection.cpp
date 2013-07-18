#include "ExPolygonCollection.hpp"

namespace Slic3r {

void
ExPolygonCollection::scale(double factor)
{
    for (ExPolygonsPtr::iterator it = expolygons.begin(); it != expolygons.end(); ++it) {
        (**it).scale(factor);
    }
}

void
ExPolygonCollection::translate(double x, double y)
{
   for (ExPolygonsPtr::iterator it = expolygons.begin(); it != expolygons.end(); ++it) {
        (**it).translate(x, y);
    }
}

void
ExPolygonCollection::rotate(double angle, Point* center)
{
    for (ExPolygonsPtr::iterator it = expolygons.begin(); it != expolygons.end(); ++it) {
        (**it).rotate(angle, center);
    }
}

}
