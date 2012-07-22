package Slic3r::Layer;
use Moo;

use Math::Clipper ':all';
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(scale unscale collinear X Y A B PI rad2deg_dir bounding_box_center shortest_path);
use Slic3r::Geometry::Clipper qw(safety_offset union_ex diff_ex intersection_ex xor_ex is_counter_clockwise);
use Slic3r::Surface ':types';

# a sequential number of layer, starting at 0
has 'id' => (
    is          => 'rw',
    #isa         => 'Int',
    required    => 1,
);

has 'slicing_errors' => (is => 'rw');

has 'slice_z' => (is => 'lazy');
has 'print_z' => (is => 'lazy');
has 'height'  => (is => 'lazy');
has 'flow'    => (is => 'lazy');
has 'perimeters_flow' => (is => 'lazy');
has 'infill_flow'     => (is => 'lazy');

# collection of spare segments generated by slicing the original geometry;
# these need to be merged in continuos (closed) polylines
has 'lines' => (is => 'rw', default => sub { [] });

# collection of surfaces generated by slicing the original geometry
has 'slices' => (is => 'rw');

# collection of polygons or polylines representing thin walls contained 
# in the original geometry
has 'thin_walls' => (is => 'rw');

# collection of polygons or polylines representing thin infill regions that
# need to be filled with a medial axis
has 'thin_fills' => (is => 'rw');

# collection of expolygons generated by offsetting the innermost perimeter(s)
# they represent boundaries of areas to fill, typed (top/bottom/internal)
has 'surfaces' => (is => 'rw');

# collection of surfaces for infill generation. the difference between surfaces
# fill_surfaces is that this one honors fill_density == 0 and turns small internal
# surfaces into solid ones
has 'fill_surfaces' => (is => 'rw');

# ordered collection of extrusion paths/loops to build all perimeters
has 'perimeters' => (is => 'rw');

# ordered collection of extrusion paths to fill surfaces for support material
has 'support_fills' => (is => 'rw');

# ordered collection of extrusion paths to fill surfaces
has 'fills' => (is => 'rw');

# Z used for slicing
sub _build_slice_z {
    my $self = shift;
    
    if ($self->id == 0) {
        return $Slic3r::_first_layer_height / 2 / $Slic3r::scaling_factor;
    }
    return ($Slic3r::_first_layer_height + (($self->id-1) * $Slic3r::layer_height) + ($Slic3r::layer_height/2))
        / $Slic3r::scaling_factor;  #/
}

# Z used for printing
sub _build_print_z {
    my $self = shift;
    return ($Slic3r::_first_layer_height + ($self->id * $Slic3r::layer_height)) / $Slic3r::scaling_factor;
}

sub _build_height {
    my $self = shift;
    return $self->id == 0 ? $Slic3r::_first_layer_height : $Slic3r::layer_height;
}

sub _build_flow {
    my $self = shift;
    return $self->id == 0 && $Slic3r::first_layer_flow
        ? $Slic3r::first_layer_flow
        : $Slic3r::flow;
}

sub _build_perimeters_flow {
    my $self = shift;
    return $self->id == 0 && $Slic3r::first_layer_flow
        ? $Slic3r::first_layer_flow
        : $Slic3r::perimeters_flow;
}

sub _build_infill_flow {
    my $self = shift;
    return $self->id == 0 && $Slic3r::first_layer_flow
        ? $Slic3r::first_layer_flow
        : $Slic3r::infill_flow;
}

# build polylines from lines
sub make_surfaces {
    my $self = shift;
    my ($loops) = @_;
    
    {
        my $safety_offset = scale 0.1;
        # merge everything
        my $expolygons = [ map $_->offset_ex(-$safety_offset), @{union_ex(safety_offset($loops, $safety_offset))} ];
        
        Slic3r::debugf "  %d surface(s) having %d holes detected from %d polylines\n",
            scalar(@$expolygons), scalar(map $_->holes, @$expolygons), scalar(@$loops);
        
        $self->slices([
            map Slic3r::Surface->new(expolygon => $_, surface_type => S_TYPE_INTERNAL),
                @$expolygons
        ]);
    }
    
    # the contours must be offsetted by half extrusion width inwards
    {
        my $distance = scale $self->perimeters_flow->width / 2;
        my @surfaces = @{$self->slices};
        @{$self->slices} = ();
        foreach my $surface (@surfaces) {
            push @{$self->slices}, map Slic3r::Surface->new
                (expolygon => $_, surface_type => S_TYPE_INTERNAL),
                map $_->offset_ex(+$distance),
                $surface->expolygon->offset_ex(-2*$distance);
        }
        
        # now detect thin walls by re-outgrowing offsetted surfaces and subtracting
        # them from the original slices
        my $outgrown = Math::Clipper::offset([ map $_->p, @{$self->slices} ], $distance);
        my $diff = diff_ex(
            [ map $_->p, @surfaces ],
            $outgrown,
            1,
        );
        
        $self->thin_walls([]);
        if (@$diff) {
            my $area_threshold = scale($self->perimeters_flow->spacing) ** 2;
            @$diff = grep $_->area > ($area_threshold), @$diff;
            
            @{$self->thin_walls} = map $_->medial_axis(scale $self->perimeters_flow->width), @$diff;
            
            Slic3r::debugf "  %d thin walls detected\n", scalar(@{$self->thin_walls}) if @{$self->thin_walls};
        }
    }
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output(undef, "surfaces.svg",
            polygons        => [ map $_->contour, @{$self->slices} ],
            red_polygons    => [ map $_->p, map @{$_->holes}, @{$self->slices} ],
        );
    }
}

sub make_perimeters {
    my $self = shift;
    Slic3r::debugf "Making perimeters for layer %d\n", $self->id;
    
    my $gap_area_threshold = scale($self->perimeters_flow->width)** 2;
    
    # this array will hold one arrayref per original surface (island);
    # each item of this arrayref is an arrayref representing a depth (from outer
    # perimeters to inner); each item of this arrayref is an ExPolygon:
    # @perimeters = (
    #    [ # first island
    #        [ Slic3r::ExPolygon, Slic3r::ExPolygon... ],  #depth 0: outer loop
    #        [ Slic3r::ExPolygon, Slic3r::ExPolygon... ],  #depth 1: inner loop
    #    ],
    #    [ # second island
    #        ...
    #    ]
    # )
    my @perimeters = ();  # one item per depth; each item
    
    # organize islands using a shortest path search
    my @surfaces = @{shortest_path([
        map [ $_->contour->[0], $_ ], @{$self->slices},
    ])};
    
    $self->perimeters([]);
    $self->surfaces([]);
    $self->thin_fills([]);
    
    # for each island:
    foreach my $surface (@surfaces) {
        my @last_offsets = ($surface->expolygon);
        my $distance = 0;
        
        # experimental hole compensation (see ArcCompensation in the RepRap wiki)
        if (0) {
            foreach my $hole ($last_offsets[0]->holes) {
                my $circumference = abs($hole->length);
                next unless $circumference <= $Slic3r::small_perimeter_length;
                # this compensation only works for circular holes, while it would 
                # overcompensate for hexagons and other shapes having straight edges.
                # so we require a minimum number of vertices.
                next unless $circumference / @$hole >= scale 3 * $Slic3r::flow->width;
                
                # revert the compensation done in make_surfaces() and get the actual radius
                # of the hole
                my $radius = ($circumference / PI / 2) - scale $self->perimeters_flow->spacing/2;
                my $new_radius = (scale($self->perimeters_flow->width) + sqrt((scale($self->perimeters_flow->width)**2) + (4*($radius**2)))) / 2;
                # holes are always turned to contours, so reverse point order before and after
                $hole->reverse;
                my @offsetted = $hole->offset(+ ($new_radius - $radius));
                # skip arc compensation when hole is not round (thus leads to multiple offsets)
                @$hole = map Slic3r::Point->new($_), @{ $offsetted[0] } if @offsetted == 1;
                $hole->reverse;
            }
        }
        
        my @gaps = ();
        
        # generate perimeters inwards
        my $loop_number = $Slic3r::perimeters + ($surface->additional_inner_perimeters || 0);
        push @perimeters, [];
        for (my $loop = 0; $loop < $loop_number; $loop++) {
            # offsetting a polygon can result in one or many offset polygons
            if ($distance) {
                my @new_offsets = ();
                foreach my $expolygon (@last_offsets) {
                    my @offsets = map $_->offset_ex(+0.5*$distance), $expolygon->offset_ex(-1.5*$distance);
                    push @new_offsets, @offsets;
                    
                    my $diff = diff_ex(
                        [ map @$_, $expolygon->offset_ex(-$distance) ],
                        [ map @$_, @offsets ],
                    );
                    push @gaps, grep $_->area >= $gap_area_threshold, @$diff;
                }
                @last_offsets = @new_offsets;
            }
            last if !@last_offsets;
            push @{ $perimeters[-1] }, [@last_offsets];
            
            # offset distance for inner loops
            $distance = scale $self->perimeters_flow->spacing;
        }
        
        # create one more offset to be used as boundary for fill
        {
            my @fill_boundaries = map $_->offset_ex(-$distance), @last_offsets;
            $_->simplify(scale $Slic3r::resolution) for @fill_boundaries;
            push @{ $self->surfaces }, @fill_boundaries;
            
            # detect the small gaps that we need to treat like thin polygons,
            # thus generating the skeleton and using it to fill them
            push @{ $self->thin_fills },
                map $_->medial_axis(scale $self->perimeters_flow->width),
                @gaps;
            Slic3r::debugf "  %d gaps filled\n", scalar @{ $self->thin_fills }
                if @{ $self->thin_fills };
        }
    }
    
    # process one island (original surface) at time
    foreach my $island (@perimeters) {
        # do holes starting from innermost one
        my @holes = ();
        my %is_external = ();
        my @hole_depths = map [ map $_->holes, @$_ ], @$island;
        
        # organize the outermost hole loops using a shortest path search
        @{$hole_depths[0]} = @{shortest_path([
            map [ $_->[0], $_ ], @{$hole_depths[0]},
        ])};
        
        CYCLE: while (map @$_, @hole_depths) {
            shift @hole_depths while !@{$hole_depths[0]};
            
            # take first available hole
            push @holes, shift @{$hole_depths[0]};
            $is_external{$#holes} = 1;
            
            my $current_depth = 0;
            while (1) {
                $current_depth++;
                
                # look for the hole containing this one if any
                next CYCLE if !$hole_depths[$current_depth];
                my $parent_hole;
                for (@{$hole_depths[$current_depth]}) {
                    if ($_->encloses_point($holes[-1]->[0])) {
                        $parent_hole = $_;
                        last;
                    }
                }
                next CYCLE if !$parent_hole;
                
                # look for other holes contained in such parent
                for (@{$hole_depths[$current_depth-1]}) {
                    if ($parent_hole->encloses_point($_->[0])) {
                        # we have a sibling, so let's move onto next iteration
                        next CYCLE;
                    }
                }
                
                push @holes, $parent_hole;
                @{$hole_depths[$current_depth]} = grep $_ ne $parent_hole, @{$hole_depths[$current_depth]};
            }
        }
        
        # do holes, then contours starting from innermost one
        $self->_add_perimeter($holes[$_], $is_external{$_} ? EXTR_ROLE_EXTERNAL_PERIMETER : undef)
            for reverse 0 .. $#holes;
        for my $depth (reverse 0 .. $#$island) {
            my $role = $depth == $#$island ? EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER
                : $depth == 0 ? EXTR_ROLE_EXTERNAL_PERIMETER
                : EXTR_ROLE_PERIMETER;
            $self->_add_perimeter($_, $role) for map $_->contour, @{$island->[$depth]};
        }
    }
    
    # add thin walls as perimeters
    {
        my @thin_paths = ();
        my %properties = (
            role            => EXTR_ROLE_PERIMETER,
            flow_spacing    => $self->perimeters_flow->spacing,
        );
        for (@{ $self->thin_walls }) {
            push @thin_paths, $_->isa('Slic3r::Polygon')
                ? Slic3r::ExtrusionLoop->pack(polygon => $_, %properties)
                : Slic3r::ExtrusionPath->pack(polyline => $_, %properties);
        }
        my $collection = Slic3r::ExtrusionPath::Collection->new(paths => \@thin_paths);
        push @{ $self->perimeters }, $collection->shortest_path;
    }
}

sub _add_perimeter {
    my $self = shift;
    my ($polygon, $role) = @_;
    
    return unless $polygon->is_printable($self->perimeters_flow->width);
    push @{ $self->perimeters }, Slic3r::ExtrusionLoop->pack(
        polygon         => $polygon,
        role            => (abs($polygon->length) <= $Slic3r::small_perimeter_length) ? EXTR_ROLE_SMALLPERIMETER : ($role // EXTR_ROLE_PERIMETER),  #/
        flow_spacing    => $self->perimeters_flow->spacing,
    );
}

sub prepare_fill_surfaces {
    my $self = shift;
    
    my @surfaces = @{$self->surfaces};
    
    # if no solid layers are requested, turn top/bottom surfaces to internal
    # note that this modifies $self->surfaces in place
    if ($Slic3r::solid_layers == 0) {
        $_->surface_type(S_TYPE_INTERNAL) for grep $_->surface_type != S_TYPE_INTERNAL, @surfaces;
    }
    
    # if hollow object is requested, remove internal surfaces
    if ($Slic3r::fill_density == 0) {
        @surfaces = grep $_->surface_type != S_TYPE_INTERNAL, @surfaces;
    }
        
    # merge too small internal surfaces with their surrounding tops
    # (if they're too small, they can be treated as solid)
    {
        my $min_area = ((7 * $self->infill_flow->spacing / $Slic3r::scaling_factor)**2) * PI;
        my $small_internal = [
            grep { $_->expolygon->contour->area <= $min_area }
            grep { $_->surface_type == S_TYPE_INTERNAL }
            @surfaces
        ];
        foreach my $s (@$small_internal) {
            @surfaces = grep $_ ne $s, @surfaces;
        }
        my $union = union_ex([
            (map $_->p, grep $_->surface_type == S_TYPE_TOP, @surfaces),
            (map @$_, map $_->expolygon->safety_offset, @$small_internal),
        ]);
        my @top = map Slic3r::Surface->new(expolygon => $_, surface_type => S_TYPE_TOP), @$union;
        @surfaces = (grep($_->surface_type != S_TYPE_TOP, @surfaces), @top);
    }
    
    # this will remove unprintable surfaces
    # (those that are too tight for extrusion)
    {
        my $distance = scale $self->infill_flow->spacing / 2;
        my @fill_surfaces = ();
        
        foreach my $surface (@surfaces) {
            # offset inwards
            my @offsets = $surface->expolygon->offset_ex(-$distance);
            
            # offset the results outwards again and merge the results
            @offsets = map $_->offset_ex($distance), @offsets;
            @offsets = @{ union_ex([ map @$_, @offsets ], undef, 1) };
            
            push @fill_surfaces, map Slic3r::Surface->new(
                expolygon => $_,
                surface_type => $surface->surface_type), @offsets;
        }
        
        Slic3r::debugf "identified %d small surfaces at layer %d\n",
            (@surfaces - @fill_surfaces), $self->id 
            if @fill_surfaces != @surfaces;
        
        # the difference between @surfaces and $self->fill_surfaces
        # is what's too small; we add it back as solid infill
        if ($Slic3r::fill_density > 0) {
            my $diff = diff_ex(
                [ map $_->p, @surfaces ],
                [ map $_->p, @fill_surfaces ],
            );
            push @surfaces, map Slic3r::Surface->new(
                expolygon => $_,
                surface_type => S_TYPE_INTERNALSOLID
            ), @$diff;
        }
    }
    
    $self->fill_surfaces([@surfaces]);
}

# make bridges printable
sub process_bridges {
    my $self = shift;
    
    # no bridges are possible if we have no internal surfaces
    return if $Slic3r::fill_density == 0;
    
    my @bridges = ();
    
    # a bottom surface on a layer > 0 is either a bridge or a overhang 
    # or a combination of both; any top surface is a candidate for
    # reverse bridge processing
    
    my @solid_surfaces = grep {
        ($_->surface_type == S_TYPE_BOTTOM && $self->id > 0) || $_->surface_type == S_TYPE_TOP
    } @{$self->fill_surfaces} or return;
    
    my @internal_surfaces = grep { $_->surface_type == S_TYPE_INTERNAL || $_->surface_type == S_TYPE_INTERNALSOLID } @{$self->slices};
    
    SURFACE: foreach my $surface (@solid_surfaces) {
        my $expolygon = $surface->expolygon->safety_offset;
        my $description = $surface->surface_type == S_TYPE_BOTTOM ? 'bridge/overhang' : 'reverse bridge';
        
        # offset the contour and intersect it with the internal surfaces to discover 
        # which of them has contact with our bridge
        my @supporting_surfaces = ();
        my ($contour_offset) = $expolygon->contour->offset(scale $self->flow->spacing * sqrt(2));
        foreach my $internal_surface (@internal_surfaces) {
            my $intersection = intersection_ex([$contour_offset], [$internal_surface->p]);
            if (@$intersection) {
                push @supporting_surfaces, $internal_surface;
            }
        }
        
        if (0) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output(undef, "bridge_surfaces.svg",
                green_polygons  => [ map $_->p, @supporting_surfaces ],
                red_polygons    => [ @$expolygon ],
            );
        }
        
        Slic3r::debugf "Found $description on layer %d with %d support(s)\n", 
            $self->id, scalar(@supporting_surfaces);
        
        next SURFACE unless @supporting_surfaces;
        
        my $bridge_angle = undef;
        if ($surface->surface_type == S_TYPE_BOTTOM) {
            # detect optimal bridge angle
            
            my $bridge_over_hole = 0;
            my @edges = ();  # edges are POLYLINES
            foreach my $supporting_surface (@supporting_surfaces) {
                my @surface_edges = map $_->clip_with_polygon($contour_offset),
                    ($supporting_surface->contour, $supporting_surface->holes);
                
                if (@supporting_surfaces == 1 && @surface_edges == 1
                    && @{$supporting_surface->contour} == @{$surface_edges[0]}) {
                    $bridge_over_hole = 1;
                }
                push @edges, grep { @$_ } @surface_edges;
            }
            Slic3r::debugf "  Bridge is supported on %d edge(s)\n", scalar(@edges);
            Slic3r::debugf "  and covers a hole\n" if $bridge_over_hole;
            
            if (0) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output(undef, "bridge_edges.svg",
                    polylines       => [ map $_->p, @edges ],
                );
            }
            
            if (@edges == 2) {
                my @chords = map Slic3r::Line->new($_->[0], $_->[-1]), @edges;
                my @midpoints = map $_->midpoint, @chords;
                my $line_between_midpoints = Slic3r::Line->new(@midpoints);
                $bridge_angle = rad2deg_dir($line_between_midpoints->direction);
            } elsif (@edges == 1) {
                # TODO: this case includes both U-shaped bridges and plain overhangs;
                # we need a trapezoidation algorithm to detect the actual bridged area
                # and separate it from the overhang area.
                # in the mean time, we're treating as overhangs all cases where
                # our supporting edge is a straight line
                if (@{$edges[0]} > 2) {
                    my $line = Slic3r::Line->new($edges[0]->[0], $edges[0]->[-1]);
                    $bridge_angle = rad2deg_dir($line->direction);
                }
            } elsif (@edges) {
                my $center = bounding_box_center([ map @$_, @edges ]);
                my $x = my $y = 0;
                foreach my $point (map @$, @edges) {
                    my $line = Slic3r::Line->new($center, $point);
                    my $dir = $line->direction;
                    my $len = $line->length;
                    $x += cos($dir) * $len;
                    $y += sin($dir) * $len;
                }
                $bridge_angle = rad2deg_dir(atan2($y, $x));
            }
            
            Slic3r::debugf "  Optimal infill angle of bridge on layer %d is %d degrees\n",
                $self->id, $bridge_angle if defined $bridge_angle;
        }
        
        # now, extend our bridge by taking a portion of supporting surfaces
        {
            # offset the bridge by the specified amount of mm (minimum 3)
            my $bridge_overlap = scale 3;
            my ($bridge_offset) = $expolygon->contour->offset($bridge_overlap);
            
            # calculate the new bridge
            my $intersection = intersection_ex(
                [ @$expolygon, map $_->p, @supporting_surfaces ],
                [ $bridge_offset ],
            );
            
            push @bridges, map Slic3r::Surface->new(
                expolygon => $_,
                surface_type => $surface->surface_type,
                bridge_angle => $bridge_angle,
            ), @$intersection;
        }
    }
    
    # now we need to merge bridges to avoid overlapping
    {
        # build a list of unique bridge types
        my @surface_groups = Slic3r::Surface->group(@bridges);
        
        # merge bridges of the same type, removing any of the bridges already merged;
        # the order of @surface_groups determines the priority between bridges having 
        # different surface_type or bridge_angle
        @bridges = ();
        foreach my $surfaces (@surface_groups) {
            my $union = union_ex([ map $_->p, @$surfaces ]);
            my $diff = diff_ex(
                [ map @$_, @$union ],
                [ map $_->p, @bridges ],
            );
            
            push @bridges, map Slic3r::Surface->new(
                expolygon => $_,
                surface_type => $surfaces->[0]->surface_type,
                bridge_angle => $surfaces->[0]->bridge_angle,
            ), @$union;
        }
    }
    
    # apply bridges to layer
    {
        my @surfaces = @{$self->fill_surfaces};
        @{$self->fill_surfaces} = ();
        
        # intersect layer surfaces with bridges to get actual bridges
        foreach my $bridge (@bridges) {
            my $actual_bridge = intersection_ex(
                [ map $_->p, @surfaces ],
                [ $bridge->p ],
            );
            
            push @{$self->fill_surfaces}, map Slic3r::Surface->new(
                expolygon => $_,
                surface_type => $bridge->surface_type,
                bridge_angle => $bridge->bridge_angle,
            ), @$actual_bridge;
        }
        
        # difference between layer surfaces and bridges are the other surfaces
        foreach my $group (Slic3r::Surface->group(@surfaces)) {
            my $difference = diff_ex(
                [ map $_->p, @$group ],
                [ map $_->p, @bridges ],
            );
            push @{$self->fill_surfaces}, map Slic3r::Surface->new(
                expolygon => $_,
                surface_type => $group->[0]->surface_type), @$difference;
        }
    }
}

1;
