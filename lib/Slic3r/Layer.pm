package Slic3r::Layer;
use Moo;

use Math::Clipper ':all';
use Slic3r::Geometry qw(scale collinear X Y A B PI rad2deg_dir bounding_box_center);
use Slic3r::Geometry::Clipper qw(union_ex diff_ex intersection_ex xor_ex is_counter_clockwise);
use XXX;

# a sequential number of layer, starting at 0
has 'id' => (
    is          => 'rw',
    #isa         => 'Int',
    required    => 1,
);

has 'slicing_errors' => (is => 'rw');

# collection of spare segments generated by slicing the original geometry;
# these need to be merged in continuos (closed) polylines
has 'lines' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::TriangleMesh::IntersectionLine]',
    default => sub { [] },
);

# collection of surfaces generated by slicing the original geometry
has 'slices' => (is => 'ro', default => sub { [] });

# collection of polygons or polylines representing thin walls contained 
# in the original geometry
has 'thin_walls' => (is => 'ro', default => sub { [] });

# collection of expolygons generated by offsetting the innermost perimeter(s)
# they represent boundaries of areas to fill
has 'fill_boundaries' => (is => 'ro', default => sub { [] });

# collection of polygons or polylines representing thin infill regions that
# need to be filled with a medial axis
has 'thin_fills' => (is => 'ro', default => sub { [] });

# collection of surfaces generated by clipping the slices to the fill boundaries
has 'surfaces' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::Surface]',
    default => sub { [] },
);

# collection of surfaces for infill
has 'fill_surfaces' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::Surface]',
    default => sub { [] },
);

# ordered collection of extrusion paths to build all perimeters
has 'perimeters' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::ExtrusionLoop]',
    default => sub { [] },
);

# ordered collection of extrusion paths to build skirt loops
has 'skirts' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::ExtrusionLoop]',
    default => sub { [] },
);

# ordered collection of extrusion paths to fill surfaces
has 'fills' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::ExtrusionPath]',
    default => sub { [] },
);

# Z used for slicing
sub slice_z {
    my $self = shift;
    if ($self->id == 0) {
        return ($Slic3r::layer_height * $Slic3r::first_layer_height_ratio) / 2 / $Slic3r::resolution;
    }
    return (($Slic3r::layer_height * $Slic3r::first_layer_height_ratio)
        + (($self->id-1) * $Slic3r::layer_height)
        + ($Slic3r::layer_height/2)) / $Slic3r::resolution;
}

# Z used for printing
sub print_z {
    my $self = shift;
    return (($Slic3r::layer_height * $Slic3r::first_layer_height_ratio)
        + ($self->id * $Slic3r::layer_height)) / $Slic3r::resolution;
}

sub add_line {
    my $self = shift;
    my ($line) = @_;
    
    push @{ $self->lines }, $line;
    return $line;
}

# build polylines from lines
sub make_surfaces {
    my $self = shift;
    my ($loops) = @_;
    
    {
        # TODO: fix self-intersecting polygons in $loops (GH #160)
        
        # merge everything
        my $expolygons = union_ex($loops);
        
        # sometimes the magic of floating point values produces holes outside of any contour;
        # we need to ignore such holes, but Clipper will convert them to contours.
        # so we identify them and remove them manually.
        
        my $area_sum = sub {
            my $area = 0;
            $area += $_->area for @_;
            return $area;
        };
        
        # get expolygons without holes (candidate for reverse holes detection)
        my @expolygons_without_holes = grep { @$_ == 1 } @$expolygons;
        
        # prepare holes as contours to allow for safe xor'ing
        my @reversed_holes = map [ reverse @$_ ], grep !is_counter_clockwise($_), @$loops;
        
        # compare each expolygon without holes with each original hole; if their XOR
        # is empty then they're the same and we can remove the hole from our layer
        my %bogus_holes = ();
        foreach my $contour (map $_->contour, @expolygons_without_holes) {
            foreach my $hole (grep !exists $bogus_holes{$_}, @reversed_holes) {
                my $xor = xor_ex([$contour], [$hole]);
                if ($area_sum->(@$xor) < scale 1) {  # TODO: define this threshold better
                    $bogus_holes{$hole} = $hole;
                }
            }
        }
        
        # remove identified holes
        $expolygons = diff_ex(
            [ map @$_, @$expolygons ],
            [ values %bogus_holes ],
        ) if %bogus_holes;
        
        Slic3r::debugf "  %d bogus hole(s) removed\n", scalar(values %bogus_holes) if %bogus_holes;
        Slic3r::debugf "  %d surface(s) having %d holes detected from %d polylines\n",
            scalar(@$expolygons), scalar(map $_->holes, @$expolygons), scalar(@$loops);
        
        push @{$self->slices},
            map Slic3r::Surface->new(expolygon => $_, surface_type => 'internal'),
                @$expolygons;
    }
    
    # the contours must be offsetted by half extrusion width inwards
    {
        my $distance = scale $Slic3r::flow_width / 2;
        my @surfaces = @{$self->slices};
        @{$self->slices} = ();
        foreach my $surface (@surfaces) {
            push @{$self->slices}, map Slic3r::Surface->new
                (expolygon => $_, surface_type => 'internal'),
                $surface->expolygon->offset_ex(-$distance);
        }
        
        # now detect thin walls by re-outgrowing offsetted surfaces and subtracting
        # them from the original slices
        my $outgrown = Math::Clipper::offset([ map $_->p, @{$self->slices} ], $distance);
        my $diff = diff_ex(
            [ map $_->p, @surfaces ],
            $outgrown,
            1,
        );
        
        # TODO: remove very small expolygons from diff before attempting to do medial axis
        # (benchmark first)
        push @{$self->thin_walls},
            grep $_,
            map $_->medial_axis(scale $Slic3r::flow_width),
            @$diff;
        
        Slic3r::debugf "  %d thin walls detected\n", scalar(@{$self->thin_walls}) if @{$self->thin_walls};
    }
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output(undef, "surfaces.svg",
            polygons        => [ map $_->contour, @{$self->slices} ],
            red_polygons    => [ map $_->p, map @{$_->holes}, @{$self->slices} ],
        );
    }
}

sub prepare_fill_surfaces {
    my $self = shift;
    
    my @surfaces = @{$self->surfaces};
        
    # merge too small internal surfaces with their surrounding tops
    # (if they're too small, they can be treated as solid)
    {
        my $min_area = ((7 * $Slic3r::flow_spacing / $Slic3r::resolution)**2) * PI;
        my $small_internal = [
            grep { $_->expolygon->contour->area <= $min_area }
            grep { $_->surface_type eq 'internal' }
            @surfaces
        ];
        foreach my $s (@$small_internal) {
            @surfaces = grep $_ ne $s, @surfaces;
        }
        my $union = union_ex([
            (map $_->p, grep $_->surface_type eq 'top', @surfaces),
            (map @$_, map $_->expolygon->safety_offset, @$small_internal),
        ]);
        my @top = map Slic3r::Surface->new(expolygon => $_, surface_type => 'top'), @$union;
        @surfaces = (grep($_->surface_type ne 'top', @surfaces), @top);
    }
    
    # remove top/bottom surfaces
    if ($Slic3r::solid_layers == 0) {
        @surfaces = grep $_->surface_type eq 'internal', @surfaces;
    }
    
    # remove internal surfaces
    if ($Slic3r::fill_density == 0) {
        @surfaces = grep $_->surface_type ne 'internal', @surfaces;
    }
    
    $self->fill_surfaces([@surfaces]);
}

sub remove_small_surfaces {
    my $self = shift;
    
    my $distance = scale $Slic3r::flow_spacing / 2;
    
    my @surfaces = @{$self->fill_surfaces};
    @{$self->fill_surfaces} = ();
    foreach my $surface (@surfaces) {
        # offset inwards
        my @offsets = $surface->expolygon->offset_ex(-$distance);
        
        # offset the results outwards again and merge the results
        @offsets = map $_->offset_ex($distance), @offsets;
        @offsets = @{ union_ex([ map @$_, @offsets ], undef, 1) };
        
        push @{$self->fill_surfaces}, map Slic3r::Surface->new(
            expolygon => $_,
            surface_type => $surface->surface_type), @offsets;
    }
    
    Slic3r::debugf "identified %d small surfaces at layer %d\n",
        (@surfaces - @{$self->fill_surfaces}), $self->id 
        if @{$self->fill_surfaces} != @surfaces;
    
    # the difference between @surfaces and $self->fill_surfaces
    # is what's too small; we add it back as solid infill
    if ($Slic3r::fill_density > 0) {
        my $diff = diff_ex(
            [ map $_->p, @surfaces ],
            [ map $_->p, @{$self->fill_surfaces} ],
        );
        push @{$self->fill_surfaces}, map Slic3r::Surface->new(
            expolygon => $_,
            surface_type => 'internal-solid'), @$diff;
    }
}

sub remove_small_perimeters {
    my $self = shift;
    my @good_perimeters = grep $_->is_printable, @{$self->perimeters};
    Slic3r::debugf "removed %d unprintable perimeters at layer %d\n",
        (@{$self->perimeters} - @good_perimeters), $self->id
        if @good_perimeters != @{$self->perimeters};
    
    @{$self->perimeters} = @good_perimeters;
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
        ($_->surface_type eq 'bottom' && $self->id > 0) || $_->surface_type eq 'top'
    } @{$self->fill_surfaces} or return;
    
    my @internal_surfaces = grep $_->surface_type =~ /internal/, @{$self->slices};
    
    SURFACE: foreach my $surface (@solid_surfaces) {
        my $expolygon = $surface->expolygon->safety_offset;
        my $description = $surface->surface_type eq 'bottom' ? 'bridge/overhang' : 'reverse bridge';
        
        # offset the contour and intersect it with the internal surfaces to discover 
        # which of them has contact with our bridge
        my @supporting_surfaces = ();
        my ($contour_offset) = $expolygon->contour->offset(scale $Slic3r::flow_spacing * sqrt(2));
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
        if ($surface->surface_type eq 'bottom') {
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
                my $line = Slic3r::Line->new($edges[0]->[0], $edges[0]->[-1]);
                $bridge_angle = rad2deg_dir($line->direction);
            } else {
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
