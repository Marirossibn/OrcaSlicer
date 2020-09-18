#include "Point.hpp"
#include "Line.hpp"
#include "MultiPoint.hpp"
#include "Int128.hpp"
#include <algorithm>

namespace Slic3r {

std::vector<Vec3f> transform(const std::vector<Vec3f>& points, const Transform3f& t)
{
    unsigned int vertices_count = (unsigned int)points.size();
    if (vertices_count == 0)
        return std::vector<Vec3f>();

    unsigned int data_size = 3 * vertices_count * sizeof(float);

    Eigen::MatrixXf src(3, vertices_count);
    ::memcpy((void*)src.data(), (const void*)points.data(), data_size);

    Eigen::MatrixXf dst(3, vertices_count);
    dst = t * src.colwise().homogeneous();

    std::vector<Vec3f> ret_points(vertices_count, Vec3f::Zero());
    ::memcpy((void*)ret_points.data(), (const void*)dst.data(), data_size);
    return ret_points;
}

Pointf3s transform(const Pointf3s& points, const Transform3d& t)
{
    unsigned int vertices_count = (unsigned int)points.size();
    if (vertices_count == 0)
        return Pointf3s();

    unsigned int data_size = 3 * vertices_count * sizeof(double);

    Eigen::MatrixXd src(3, vertices_count);
    ::memcpy((void*)src.data(), (const void*)points.data(), data_size);

    Eigen::MatrixXd dst(3, vertices_count);
    dst = t * src.colwise().homogeneous();

    Pointf3s ret_points(vertices_count, Vec3d::Zero());
    ::memcpy((void*)ret_points.data(), (const void*)dst.data(), data_size);
    return ret_points;
}

void Point::rotate(double angle, const Point &center)
{
    double cur_x = (double)(*this)(0);
    double cur_y = (double)(*this)(1);
    double s     = ::sin(angle);
    double c     = ::cos(angle);
    double dx    = cur_x - (double)center(0);
    double dy    = cur_y - (double)center(1);
    (*this)(0) = (coord_t)round( (double)center(0) + c * dx - s * dy );
    (*this)(1) = (coord_t)round( (double)center(1) + c * dy + s * dx );
}

int Point::nearest_point_index(const Points &points) const
{
    PointConstPtrs p;
    p.reserve(points.size());
    for (Points::const_iterator it = points.begin(); it != points.end(); ++it)
        p.push_back(&*it);
    return this->nearest_point_index(p);
}

int Point::nearest_point_index(const PointConstPtrs &points) const
{
    int idx = -1;
    double distance = -1;  // double because long is limited to 2147483647 on some platforms and it's not enough
    
    for (PointConstPtrs::const_iterator it = points.begin(); it != points.end(); ++it) {
        /* If the X distance of the candidate is > than the total distance of the
           best previous candidate, we know we don't want it */
        double d = sqr<double>((*this)(0) - (*it)->x());
        if (distance != -1 && d > distance) continue;
        
        /* If the Y distance of the candidate is > than the total distance of the
           best previous candidate, we know we don't want it */
        d += sqr<double>((*this)(1) - (*it)->y());
        if (distance != -1 && d > distance) continue;
        
        idx = it - points.begin();
        distance = d;
        
        if (distance < EPSILON) break;
    }
    
    return idx;
}

int Point::nearest_point_index(const PointPtrs &points) const
{
    PointConstPtrs p;
    p.reserve(points.size());
    for (PointPtrs::const_iterator it = points.begin(); it != points.end(); ++it)
        p.push_back(*it);
    return this->nearest_point_index(p);
}

bool Point::nearest_point(const Points &points, Point* point) const
{
    int idx = this->nearest_point_index(points);
    if (idx == -1) return false;
    *point = points.at(idx);
    return true;
}

/* Three points are a counter-clockwise turn if ccw > 0, clockwise if
 * ccw < 0, and collinear if ccw = 0 because ccw is a determinant that
 * gives the signed area of the triangle formed by p1, p2 and this point.
 * In other words it is the 2D cross product of p1-p2 and p1-this, i.e.
 * z-component of their 3D cross product.
 * We return double because it must be big enough to hold 2*max(|coordinate|)^2
 */
double Point::ccw(const Point &p1, const Point &p2) const
{
    return (double)(p2(0) - p1(0))*(double)((*this)(1) - p1(1)) - (double)(p2(1) - p1(1))*(double)((*this)(0) - p1(0));
}

double Point::ccw(const Line &line) const
{
    return this->ccw(line.a, line.b);
}

// returns the CCW angle between this-p1 and this-p2
// i.e. this assumes a CCW rotation from p1 to p2 around this
double Point::ccw_angle(const Point &p1, const Point &p2) const
{
    double angle = atan2(p1(0) - (*this)(0), p1(1) - (*this)(1))
                 - atan2(p2(0) - (*this)(0), p2(1) - (*this)(1));
    
    // we only want to return only positive angles
    return angle <= 0 ? angle + 2*PI : angle;
}

Point Point::projection_onto(const MultiPoint &poly) const
{
    Point running_projection = poly.first_point();
    double running_min = (running_projection - *this).cast<double>().norm();
    
    Lines lines = poly.lines();
    for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
        Point point_temp = this->projection_onto(*line);
        if ((point_temp - *this).cast<double>().norm() < running_min) {
	        running_projection = point_temp;
	        running_min = (running_projection - *this).cast<double>().norm();
        }
    }
    return running_projection;
}

Point Point::projection_onto(const Line &line) const
{
    if (line.a == line.b) return line.a;
    
    /*
        (Ported from VisiLibity by Karl J. Obermeyer)
        The projection of point_temp onto the line determined by
        line_segment_temp can be represented as an affine combination
        expressed in the form projection of
        Point = theta*line_segment_temp.first + (1.0-theta)*line_segment_temp.second.
        If theta is outside the interval [0,1], then one of the Line_Segment's endpoints
        must be closest to calling Point.
    */
    double lx = (double)(line.b(0) - line.a(0));
    double ly = (double)(line.b(1) - line.a(1));
    double theta = ( (double)(line.b(0) - (*this)(0))*lx + (double)(line.b(1)- (*this)(1))*ly ) 
          / ( sqr<double>(lx) + sqr<double>(ly) );
    
    if (0.0 <= theta && theta <= 1.0)
        return (theta * line.a.cast<coordf_t>() + (1.0-theta) * line.b.cast<coordf_t>()).cast<coord_t>();
    
    // Else pick closest endpoint.
    return ((line.a - *this).cast<double>().squaredNorm() < (line.b - *this).cast<double>().squaredNorm()) ? line.a : line.b;
}

std::ostream& operator<<(std::ostream &stm, const Vec2d &pointf)
{
    return stm << pointf(0) << "," << pointf(1);
}

namespace int128 {

int orient(const Vec2crd &p1, const Vec2crd &p2, const Vec2crd &p3)
{
    Slic3r::Vector v1(p2 - p1);
    Slic3r::Vector v2(p3 - p1);
    return Int128::sign_determinant_2x2_filtered(v1(0), v1(1), v2(0), v2(1));
}

int cross(const Vec2crd &v1, const Vec2crd &v2)
{
    return Int128::sign_determinant_2x2_filtered(v1(0), v1(1), v2(0), v2(1));
}

}

}
