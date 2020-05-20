// AABB tree built upon external data set, referencing the external data by integer indices.

#ifndef slic3r_AABBTreeIndirect_hpp_
#define slic3r_AABBTreeIndirect_hpp_

#include <algorithm>
#include <limits>
#include <vector>

#include "Utils.hpp" // for next_highest_power_of_2()

extern "C"
{
#include <igl/raytri.c>
}
#include <igl/Hit.h>
#include <igl/ray_box_intersect.h>

namespace Slic3r {
namespace AABBTreeIndirect {

// AABB tree for raycasting and closest triangle search.
template<int ANumDimensions, typename ACoordType>
class Tree
{
public:
    static constexpr int    NumDimensions = ANumDimensions;
	using					CoordType     = ACoordType;
    using 					Vec3crd 	  = Eigen::Matrix<CoordType, NumDimensions, 1, Eigen::DontAlign>;
    using  					BoundingBox   = Eigen::AlignedBox<CoordType, NumDimensions>;
    // Following could be static constexpr size_t, but that would not link in C++11
    enum : size_t {
    	// Node is not used.
        npos  = size_t(-1),
        // Inner node (not leaf).
        inner = size_t(-2)
    };

    struct Node {
    	// Index of the external source entity, for which this AABB tree was built, npos for internal nodes.
        size_t 			idx = npos;
    	// Bounding box around this entity, possibly with epsilons applied.
        BoundingBox 	bbox;

        bool 	is_valid() const { return this->idx != npos; }
        bool 	is_inner() const { return this->idx == inner; }
        bool 	is_leaf()  const { return ! this->is_inner(); }

    	template<typename SourceNode>
    	void set(const SourceNode &rhs) {
            this->idx  = rhs.idx();
            this->bbox = rhs.bbox();
		}
    };

	void clear() { m_nodes.clear(); }

	// SourceNode shall implement
	// size_t SourceNode::idx() const
	// 		- index to the outside triangle.
	// const Vec3crd& SourceNode::centroid() const
	// 		- centroid of this node, for splitting the triangles into left / right bounding box.
	// const BoundingBox& SourceNode::bbox() const
	// 		- bounding box of this node, likely expanded with epsilon to account for numeric rounding during tree traversal.
	template<typename SourceNode>
	void build(std::vector<SourceNode> &&input)
	{
        if (input.empty())
			clear();
		else {
			// Allocate enough memory for a full binary tree.
            //FIXME fianlize the tree size formula.
            m_nodes.assign(next_highest_power_of_2(input.size() * 2 + 1), Node());
            build_recursive(input, 0, 0, input.size() - 1);
		}
        input.clear();
	}

	const std::vector<Node>& 	nodes() const { return m_nodes; }
	const Node& 				node(size_t idx) const { return m_nodes[idx]; }
	bool 						empty() const { return m_nodes.empty(); }

	template<typename SourceNode>
    void build(const std::vector<SourceNode> &input)
	{
        std::vector<SourceNode> copy(input);
        this->build(std::move(copy));
	}

private:
	// Build a balanced tree by splitting the input sequence by an axis aligned plane at a dimension.
	template<typename SourceNode>
	void build_recursive(std::vector<SourceNode> &input, size_t node, const size_t left, const size_t right)
	{
        assert(node < m_nodes.size());
        assert(left <= right);

		if (left == right) {
			// Insert a node into the balanced tree.
			m_nodes[node].set(input[left]);
			return;
		}

		// Calculate bounding box of the input.
        BoundingBox bbox(input[left].bbox());
        for (size_t i = left + 1; i <= right; ++ i)
            bbox.extend(input[i].bbox());
        int dimension = -1;
        bbox.diagonal().maxCoeff(&dimension);

		// Partition the input to left / right pieces of the same length to produce a balanced tree.
		size_t center = (left + right) / 2;
		partition_input(input, size_t(dimension), left, right, center);
		// Insert a node into the tree.
		m_nodes[node].idx  = inner;
		m_nodes[node].bbox = bbox;
        build_recursive(input, node * 2 + 1, left, center);
		build_recursive(input, node * 2 + 2, center + 1, right);
	}

	// Partition the input m_nodes <left, right> at "k" and "dimension" using the QuickSelect method:
	// https://en.wikipedia.org/wiki/Quickselect
	// Items left of the k'th item are lower than the k'th item in the "dimension", 
	// items right of the k'th item are higher than the k'th item in the "dimension", 
	template<typename SourceNode>
	void partition_input(std::vector<SourceNode> &input, const size_t dimension, size_t left, size_t right, const size_t k) const
	{
		while (left < right) {
			size_t center = (left + right) / 2;
			CoordType pivot;
			{
				// Bubble sort the input[left], input[center], input[right], so that a median of the three values
				// will end up in input[center].
				CoordType left_value   = input[left  ].centroid()(dimension);
				CoordType center_value = input[center].centroid()(dimension);
				CoordType right_value  = input[right ].centroid()(dimension);
				if (left_value > center_value) {
					std::swap(input[left], input[center]);
					std::swap(left_value,  center_value);
				}
				if (left_value > right_value) {
					std::swap(input[left], input[right]);
					right_value = left_value;
				}
				if (center_value > right_value) {
					std::swap(input[center], input[right]);
					center_value = right_value;
				}
				pivot = center_value;
			}
			if (right <= left + 2)
				// The <left, right> interval is already sorted.
				break;
			size_t i = left;
			size_t j = right - 1;
			std::swap(input[center], input[j]);
			// Partition the set based on the pivot.
			for (;;) {
				// Skip left points that are already at correct positions.
				// Search will certainly stop at position (right - 1), which stores the pivot.
				while (input[++ i].centroid()(dimension) < pivot) ;
				// Skip right points that are already at correct positions.
				while (input[-- j].centroid()(dimension) > pivot && i < j) ;
				if (i >= j)
					break;
				std::swap(input[i], input[j]);
			}
			// Restore pivot to the center of the sequence.
			std::swap(input[i], input[right - 1]);
			// Which side the kth element is in?
			if (k < i)
				right = i - 1;
			else if (k == i)
				// Sequence is partitioned, kth element is at its place.
				break;
			else
				left = i + 1;
		}
	}

	std::vector<Node> m_nodes;
};

template<typename VertexType, typename IndexedFaceType>
inline Tree<3, typename VertexType::Scalar>
	build_aabb_tree(const std::vector<VertexType> &vertices, const std::vector<IndexedFaceType> &faces)
{
    using 				 TreeType 		= Tree<3, typename VertexType::Scalar>;
    using				 CoordType      = typename TreeType::CoordType;
    using 				 Vec3crd 	    = typename TreeType::Vec3crd;
    using 				 BoundingBox 	= typename TreeType::BoundingBox;
    static constexpr CoordType eps      = CoordType(1e-4);

	struct InputType {
        size_t 				idx()       const { return m_idx; }
        const BoundingBox& 	bbox()      const { return m_bbox; }
        const Vec3crd& 		centroid()  const { return m_centroid; }

		size_t 		m_idx;
		BoundingBox m_bbox;
        Vec3crd 	m_centroid;
	};

	std::vector<InputType> input;
	input.reserve(faces.size());
    Vec3crd veps(eps, eps, eps);
	for (size_t i = 0; i < faces.size(); ++ i) {
        const IndexedFaceType &face = faces[i];
		const VertexType &v1 = vertices[face(0)];
		const VertexType &v2 = vertices[face(1)];
		const VertexType &v3 = vertices[face(2)];
		InputType n;
        n.m_idx      = i;
        n.m_centroid = (1./3.) * (v1 + v2 + v3);
        n.m_bbox = BoundingBox(v1, v1);
        n.m_bbox.extend(v2);
        n.m_bbox.extend(v3);
        n.m_bbox.min() -= veps;
        n.m_bbox.max() += veps;
        input.emplace_back(n);
	}

	TreeType out;
	out.build(std::move(input));
	return out;
}

namespace detail {
	template<typename AVertexType, typename AIndexedFaceType, typename ATreeType, typename AVectorType>
	struct RayIntersector {
		using VertexType 		= AVertexType;
		using IndexedFaceType 	= AIndexedFaceType;
		using TreeType			= ATreeType;
		using VectorType 		= AVectorType;

		const std::vector<VertexType> 		&vertices;
		const std::vector<IndexedFaceType> 	&faces;
		const TreeType 						&tree;

		const VectorType					 origin;
		const VectorType 					 dir;
	};

    template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
    struct RayIntersectorHits : RayIntersector<VertexType, IndexedFaceType, TreeType, VectorType> {
		std::vector<igl::Hit>				 hits;
	};

    template<typename RayIntersectorType, typename Scalar>
	static inline bool intersect_ray_recursive_first_hit(
        RayIntersectorType 	   &ray_intersector,
        size_t 				    node_idx,
        Scalar                  min_t,
        igl::Hit 			   &hit)
	{
		const auto &nodes = ray_intersector.tree.nodes();
		if (node_idx >= nodes.size())
            return false;

        const auto &node = nodes[node_idx];
		if (! node.is_valid())
            return false;

		{
	    	Scalar t_start, t_end;
            if (! igl::ray_box_intersect(ray_intersector.origin, ray_intersector.dir, node.bbox.template cast<Scalar>(), Scalar(0), min_t, t_start, t_end))
				return false;
		}

	  	if (node.is_leaf()) {
            using Vector = Eigen::Matrix<Scalar, 3, 1>;
            Vector origin_d	= ray_intersector.origin.template cast<double>();
            Vector dir_d 	= ray_intersector.dir   .template cast<double>();
            auto   face     = ray_intersector.faces[node.idx];
            Vector v0 		= ray_intersector.vertices[face(0)].template cast<double>();
            Vector v1 		= ray_intersector.vertices[face(1)].template cast<double>();
            Vector v2 		= ray_intersector.vertices[face(2)].template cast<double>();
		    // shoot ray, record hit
		    double t, u, v;
		    if (intersect_triangle1(origin_d.data(), dir_d.data(), v0.data(), v1.data(), v2.data(), &t, &u, &v) && t > 0.) {
                hit = igl::Hit { int(node.idx), -1, float(u), float(v), float(t) };
				return true;
		    }
		    return false;
	  	}

		// Left / right child node index.
		size_t left  = node_idx * 2 + 1;
		size_t right = left + 1;
		igl::Hit left_hit;
		igl::Hit right_hit;
        bool left_ret = intersect_ray_recursive_first_hit(ray_intersector, left,  min_t, left_hit);
		if (left_ret && left_hit.t < min_t) {
    		min_t = left_hit.t;
    		hit   = left_hit;
  		} else
		    left_ret = false;
        bool right_ret = intersect_ray_recursive_first_hit(ray_intersector, right, min_t, right_hit);
		if (right_ret && right_hit.t < min_t)
			hit = right_hit;
		else
			right_ret = false;
		return left_ret || right_ret;
	}

    template<typename RayIntersectorType>
	static inline void intersect_ray_recursive_all_hits(RayIntersectorType &ray_intersector, size_t node_idx)
	{
        using Vector = typename RayIntersectorType::VectorType;
        using Scalar = typename Vector::Scalar;

		const auto &node = ray_intersector.tree.node(node_idx);
		if (! node.is_valid())
			return;

	  	{
	    	Scalar t_start, t_end;
            if (! igl::ray_box_intersect(ray_intersector.origin, ray_intersector.dir, node.bbox.template cast<Scalar>(),
	    			Scalar(0), std::numeric_limits<Scalar>::infinity(), t_start, t_end))
				return;
	    }

	  	if (node.is_leaf()) {
            using Vector = Eigen::Matrix<Scalar, 3, 1>;
            Vector origin_d = ray_intersector.origin.template cast<double>();
            Vector dir_d    = ray_intersector.dir   .template cast<double>();
            auto   face     = ray_intersector.faces[node.idx];
            Vector v0 		= ray_intersector.vertices[face(0)].template cast<double>();
            Vector v1 		= ray_intersector.vertices[face(1)].template cast<double>();
            Vector v2 		= ray_intersector.vertices[face(2)].template cast<double>();
		    // shoot ray, record hit
		    double t, u, v;
		    if (intersect_triangle1(origin_d.data(), dir_d.data(), v0.data(), v1.data(), v2.data(), &t, &u, &v) && t > 0.)
                ray_intersector.hits.emplace_back(igl::Hit{ int(node.idx), -1, float(u), float(v), float(t) });
			return;
	  	}

		// Left / right child node index.
		size_t left  = node_idx * 2 + 1;
		size_t right = left + 1;
	  	intersect_ray_recursive_all_hits(ray_intersector, left);
	  	intersect_ray_recursive_all_hits(ray_intersector, right);
	}

	template<typename AVertexType, typename AIndexedFaceType, typename ATreeType, typename AVectorType>
	struct IndexedTriangleSetDistancer {
		using VertexType 		= AVertexType;
		using IndexedFaceType 	= AIndexedFaceType;
		using TreeType			= ATreeType;
		using VectorType 		= AVectorType;

		const std::vector<VertexType> 		&vertices;
		const std::vector<IndexedFaceType> 	&faces;
		const TreeType 						&tree;

		const VectorType					 origin;
	};

	// Real-time collision detection, Ericson, Chapter 5
	template<typename Vector>
	static inline Vector closest_point_to_triangle(const Vector &p, const Vector &a, const Vector &b, const Vector &c)
	{
		using Scalar = typename Vector::Scalar;
		// Check if P in vertex region outside A
		Vector ab = b - a;
		Vector ac = c - a;
		Vector ap = p - a;
		Scalar d1 = ab.dot(ap);
		Scalar d2 = ac.dot(ap);
		if (d1 <= Scalar(0) && d2 <= Scalar(0))
		  return a;
		// Check if P in vertex region outside B
		Vector bp = p - b;
		Scalar d3 = ab.dot(bp);
		Scalar d4 = ac.dot(bp);
		if (d3 >= Scalar(0) && d4 <= d3)
		  return b;
		// Check if P in edge region of AB, if so return projection of P onto AB
		Scalar vc = d1*d4 - d3*d2;
		if (a != b && vc <= Scalar(0) && d1 >= Scalar(0) && d3 <= Scalar(0)) {
		    Scalar v = d1 / (d1 - d3);
		    return a + v * ab;
		}
		// Check if P in vertex region outside C
		Vector cp = p - c;
		Scalar d5 = ab.dot(cp);
		Scalar d6 = ac.dot(cp);
		if (d6 >= Scalar(0) && d5 <= d6)
		  return c;
		// Check if P in edge region of AC, if so return projection of P onto AC
		Scalar vb = d5*d2 - d1*d6;
		if (vb <= Scalar(0) && d2 >= Scalar(0) && d6 <= Scalar(0)) {
		  Scalar w = d2 / (d2 - d6);
		  return a + w * ac;
		}
		// Check if P in edge region of BC, if so return projection of P onto BC
		Scalar va = d3*d6 - d5*d4;
		if (va <= Scalar(0) && (d4 - d3) >= Scalar(0) && (d5 - d6) >= Scalar(0)) {
		  Scalar w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		  return b + w * (c - b);
		}
		// P inside face region. Compute Q through its barycentric coordinates (u,v,w)
		Scalar denom = Scalar(1.0) / (va + vb + vc);
		Scalar v = vb * denom;
		Scalar w = vc * denom;
		return a + ab * v + ac * w; // = u*a + v*b + w*c, u = va * denom = 1.0-v-w
	};

	template<typename IndexedTriangleSetDistancerType, typename Scalar>
    static inline Scalar squared_distance_recursive(
        IndexedTriangleSetDistancerType	&distancer,
		size_t 							 node_idx,
		Scalar 							 low_sqr_d,
  		Scalar 							 up_sqr_d,
		size_t 							&i,
  		Eigen::PlainObjectBase<typename IndexedTriangleSetDistancerType::VectorType> &c)
	{
		using Vector = typename IndexedTriangleSetDistancerType::VectorType;

  		if (low_sqr_d > up_sqr_d)
			return low_sqr_d;
	  
        auto set_min = [&i, &c, &up_sqr_d](const Scalar sqr_d_candidate, const size_t i_candidate, const Vector &c_candidate) {
			if (sqr_d_candidate < up_sqr_d) {
				i     	 = i_candidate;
				c     	 = c_candidate;
				up_sqr_d = sqr_d_candidate;
			}
        };

		const auto &node = distancer.tree.node(node_idx);
		assert(node.is_valid());
  		if (node.is_leaf()) 
  		{
            const auto &triangle = distancer.faces[node.idx];
            Vector c_candidate = closest_point_to_triangle<Vector>(
				distancer.origin, 
                distancer.vertices[triangle(0)].template cast<Scalar>(),
                distancer.vertices[triangle(1)].template cast<Scalar>(),
                distancer.vertices[triangle(2)].template cast<Scalar>());
            set_min((c_candidate - distancer.origin).squaredNorm(), node.idx, c_candidate);
  		} 
  		else
  		{
			size_t left_node_idx  = node_idx * 2 + 1;
            size_t right_node_idx = left_node_idx + 1;
			const auto &node_left  = distancer.tree.node(left_node_idx);
			const auto &node_right = distancer.tree.node(right_node_idx);
			assert(node_left.is_valid());
			assert(node_right.is_valid());

			bool   looked_left    = false;
			bool   looked_right   = false;
			const auto &look_left = [&]()
			{
                size_t	i_left;
                Vector 	c_left = c;
                Scalar	sqr_d_left = squared_distance_recursive(distancer, left_node_idx, low_sqr_d, up_sqr_d, i_left, c_left);
				set_min(sqr_d_left, i_left, c_left);
				looked_left = true;
			};
			const auto &look_right = [&]()
			{
                size_t	i_right;
                Vector	c_right = c;
                Scalar	sqr_d_right = squared_distance_recursive(distancer, right_node_idx, low_sqr_d, up_sqr_d, i_right, c_right);
				set_min(sqr_d_right, i_right, c_right);
				looked_right = true;
			};

			// must look left or right if in box
            using BBoxScalar = typename IndexedTriangleSetDistancerType::TreeType::BoundingBox::Scalar;
            if (node_left.bbox.contains(distancer.origin.template cast<BBoxScalar>()))
			  	look_left();
            if (node_right.bbox.contains(distancer.origin.template cast<BBoxScalar>()))
			  	look_right();
			// if haven't looked left and could be less than current min, then look
			Scalar left_up_sqr_d  = node_left.bbox.squaredExteriorDistance(distancer.origin);
			Scalar right_up_sqr_d = node_right.bbox.squaredExteriorDistance(distancer.origin);
			if (left_up_sqr_d < right_up_sqr_d) {
			  	if (! looked_left && left_up_sqr_d < up_sqr_d)
			    	look_left();
			  	if (! looked_right && right_up_sqr_d < up_sqr_d)
			    	look_right();
			} else {
			  	if (! looked_right && right_up_sqr_d < up_sqr_d)
			    	look_right();
			  	if (! looked_left && left_up_sqr_d < up_sqr_d)
			    	look_left();
			}
		}
		return up_sqr_d;
	}

} // namespace detail

template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline bool intersect_ray_first_hit(
	const std::vector<VertexType> 		&vertices,
	const std::vector<IndexedFaceType> 	&faces,
	const TreeType 						&tree,
	const VectorType					&origin,
	const VectorType 					&dir,
	igl::Hit 							&hit)
{
    using Scalar = typename VectorType::Scalar;
    auto ray_intersector = detail::RayIntersector<VertexType, IndexedFaceType, TreeType, VectorType> {
		vertices, faces, tree,
		origin, dir
	};
	return ! tree.empty() && detail::intersect_ray_recursive_first_hit(
        ray_intersector, size_t(0), std::numeric_limits<Scalar>::infinity(), hit);
}

template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline bool intersect_ray_all_hits(
	const std::vector<VertexType> 		&vertices,
	const std::vector<IndexedFaceType> 	&faces,
	const TreeType 						&tree,
	const VectorType					&origin,
	const VectorType 					&dir,
	std::vector<igl::Hit> 				&hits)
{
    auto ray_intersector = detail::RayIntersectorHits<VertexType, IndexedFaceType, TreeType, VectorType> {
		vertices, faces, tree,
		origin, dir 
	};
	if (! tree.empty()) {
        ray_intersector.hits.reserve(8);
		detail::intersect_ray_recursive_all_hits(ray_intersector, 0);
		std::swap(hits, ray_intersector.hits);
	    std::sort(hits.begin(), hits.end(), [](const auto &l, const auto &r) { return l.t < r.t; });
	}
	return ! hits.empty();
}

// Closest point to triangle test will be performed with the accuracy of VectorType::Scalar.
template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline typename VectorType::Scalar squared_distance(
	const std::vector<VertexType> 		&vertices,
	const std::vector<IndexedFaceType> 	&faces,
	const TreeType 						&tree,
	const VectorType					&point,
	size_t 								&hit_idx_out,
	Eigen::PlainObjectBase<VectorType>	&hit_point_out)
{
    using Scalar = typename VectorType::Scalar;
    auto distancer = detail::IndexedTriangleSetDistancer<VertexType, IndexedFaceType, TreeType, VectorType>
        { vertices, faces, tree, point };
    return detail::squared_distance_recursive(distancer, size_t(0), Scalar(0), std::numeric_limits<Scalar>::infinity(), hit_idx_out, hit_point_out);
}

} // namespace AABBTreeIndirect
} // namespace Slic3r

#endif /* slic3r_AABBTreeIndirect_hpp_ */
