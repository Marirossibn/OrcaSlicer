package Slic3r::Layer::Region;
use Moo;

use List::Util qw(sum first);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(PI X1 X2 Y1 Y2 A B scale chained_path_items points_coincide);
use Slic3r::Geometry::Clipper qw(safety_offset union_ex diff_ex intersection_ex);
use Slic3r::Surface ':types';

has 'layer' => (
    is          => 'ro',
    weak_ref    => 1,
    required    => 1,
    trigger     => 1,
    handles     => [qw(id slice_z print_z height flow)],
);
has 'region'            => (is => 'ro', required => 1, handles => [qw(extruders)]);
has 'perimeter_flow'    => (is => 'rw');
has 'infill_flow'       => (is => 'rw');
has 'solid_infill_flow' => (is => 'rw');
has 'top_infill_flow'   => (is => 'rw');
has 'infill_area_threshold' => (is => 'lazy');
has 'overhang_width'    => (is => 'lazy');

# collection of spare segments generated by slicing the original geometry;
# these need to be merged in continuos (closed) polylines
has 'lines' => (is => 'rw', default => sub { [] });

# collection of surfaces generated by slicing the original geometry
has 'slices' => (is => 'rw', default => sub { [] });

# collection of polygons or polylines representing thin walls contained 
# in the original geometry
has 'thin_walls' => (is => 'rw', default => sub { [] });

# collection of polygons or polylines representing thin infill regions that
# need to be filled with a medial axis
has 'thin_fills' => (is => 'rw', default => sub { [] });

# collection of surfaces for infill generation
has 'fill_surfaces' => (is => 'rw', default => sub { [] });

# ordered collection of extrusion paths/loops to build all perimeters
has 'perimeters' => (is => 'rw', default => sub { [] });

# ordered collection of extrusion paths to fill surfaces
has 'fills' => (is => 'rw', default => sub { [] });

sub BUILD {
    my $self = shift;
    $self->_update_flows;
}

sub _trigger_layer {
    my $self = shift;
    $self->_update_flows;
}

sub _update_flows {
    my $self = shift;
    return if !$self->region;
    
    if ($self->id == 0) {
        for (qw(perimeter infill solid_infill top_infill)) {
            my $method = "${_}_flow";
            $self->$method
                ($self->region->first_layer_flows->{$_} || $self->region->flows->{$_});
        } 
    } else {
        $self->perimeter_flow($self->region->flows->{perimeter});
        $self->infill_flow($self->region->flows->{infill});
        $self->solid_infill_flow($self->region->flows->{solid_infill});
        $self->top_infill_flow($self->region->flows->{top_infill});
    }
}

sub _build_overhang_width {
    my $self = shift;
    my $threshold_rad = PI/2 - atan2($self->perimeter_flow->width / $self->height / 2, 1);
    return scale($self->height * ((cos $threshold_rad) / (sin $threshold_rad)));
}

sub _build_infill_area_threshold {
    my $self = shift;
    return $self->solid_infill_flow->scaled_spacing ** 2;
}

# build polylines from lines
sub make_surfaces {
    my $self = shift;
    my ($loops) = @_;
    
    return if !@$loops;
    $self->slices([ _merge_loops($loops) ]);
    
    # detect thin walls by offsetting slices by half extrusion inwards
    {
        my $width = $self->perimeter_flow->scaled_width;
        my $outgrown = [
            Slic3r::Geometry::Clipper::ex_int_offset2([ map @$_, map $_->expolygon, @{$self->slices} ], -$width, +$width),
        ];
        my $diff = diff_ex(
            [ map $_->p, @{$self->slices} ],
            [ map @$_, @$outgrown ],
            1,
        );
        
        $self->thin_walls([]);
        if (@$diff) {
            my $area_threshold = $self->perimeter_flow->scaled_spacing ** 2;
            @$diff = grep $_->area > ($area_threshold), @$diff;
            
            @{$self->thin_walls} = map $_->medial_axis($self->perimeter_flow->scaled_width), @$diff;
            
            Slic3r::debugf "  %d thin walls detected\n", scalar(@{$self->thin_walls}) if @{$self->thin_walls};
        }
    }
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("surfaces.svg",
            polygons        => [ map $_->contour, @{$self->slices} ],
            red_polygons    => [ map $_->p, map @{$_->holes}, @{$self->slices} ],
        );
    }
}

sub _merge_loops {
    my ($loops, $safety_offset) = @_;
    
    # Input loops are not suitable for evenodd nor nonzero fill types, as we might get
    # two consecutive concentric loops having the same winding order - and we have to 
    # respect such order. In that case, evenodd would create wrong inversions, and nonzero
    # would ignore holes inside two concentric contours.
    # So we're ordering loops and collapse consecutive concentric loops having the same 
    # winding order.
    # TODO: find a faster algorithm for this.
    my @loops = sort { $a->encloses_point($b->[0]) ? 0 : 1 } @$loops;  # outer first
    $safety_offset //= scale 0.0499;
    @loops = @{ safety_offset(\@loops, $safety_offset) };
    my $expolygons = [];
    while (my $loop = shift @loops) {
        bless $loop, 'Slic3r::Polygon';
        if ($loop->is_counter_clockwise) {
            $expolygons = union_ex([ $loop, map @$_, @$expolygons ]);
        } else {
            $expolygons = diff_ex([ map @$_, @$expolygons ], [$loop]);
        }
    }
    $expolygons = [ map $_->offset_ex(-$safety_offset), @$expolygons ];
    
    Slic3r::debugf "  %d surface(s) having %d holes detected from %d polylines\n",
        scalar(@$expolygons), scalar(map $_->holes, @$expolygons), scalar(@$loops);
    
    return map Slic3r::Surface->new(expolygon => $_, surface_type => S_TYPE_INTERNAL), @$expolygons;
}

sub make_perimeters {
    my $self = shift;
    
    my $perimeter_spacing   = $self->perimeter_flow->scaled_spacing;
    my $infill_spacing      = $self->solid_infill_flow->scaled_spacing;
    my $gap_area_threshold = $self->perimeter_flow->scaled_width ** 2;
    
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
    
    # organize islands using a nearest-neighbor search
    my @surfaces = @{chained_path_items([
        map [ $_->contour->[0], $_ ], @{$self->slices},
    ])};
    
    $self->perimeters([]);
    $self->fill_surfaces([]);
    $self->thin_fills([]);
    
    # for each island:
    foreach my $surface (@surfaces) {
        my @last_offsets = ($surface->expolygon);
        
        # experimental hole compensation (see ArcCompensation in the RepRap wiki)
        if (0) {
            foreach my $hole ($last_offsets[0]->holes) {
                my $circumference = abs($hole->length);
                next unless $circumference <= &Slic3r::SMALL_PERIMETER_LENGTH;
                # this compensation only works for circular holes, while it would 
                # overcompensate for hexagons and other shapes having straight edges.
                # so we require a minimum number of vertices.
                next unless $circumference / @$hole >= 3 * $self->perimeter_flow->scaled_width;
                
                # revert the compensation done in make_surfaces() and get the actual radius
                # of the hole
                my $radius = ($circumference / PI / 2) - $self->perimeter_flow->scaled_spacing/2;
                my $new_radius = ($self->perimeter_flow->scaled_width + sqrt(($self->perimeter_flow->scaled_width ** 2) + (4*($radius**2)))) / 2;
                # holes are always turned to contours, so reverse point order before and after
                $hole->reverse;
                my @offsetted = $hole->offset(+ ($new_radius - $radius));
                # skip arc compensation when hole is not round (thus leads to multiple offsets)
                @$hole = map Slic3r::Point->new($_), @{ $offsetted[0] } if @offsetted == 1;
                $hole->reverse;
            }
        }
        
        my @gaps = ();
        
        # generate perimeters inwards (loop 0 is the external one)
        my $loop_number = $Slic3r::Config->perimeters + ($surface->extra_perimeters || 0);
        push @perimeters, [] if $loop_number > 0;
        
        # do one more loop (<= instead of <) so that we can detect gaps even after the desired
        # number of perimeters has been generated
        for (my $loop = 0; $loop <= $loop_number; $loop++) {
            my $spacing = $perimeter_spacing;
            $spacing /= 2 if $loop == 0;
            
            # offsetting a polygon can result in one or many offset polygons
            my @new_offsets = ();
            foreach my $expolygon (@last_offsets) {
                my @offsets = Slic3r::Geometry::Clipper::ex_int_offset2($expolygon, -1.5*$spacing,  +0.5*$spacing);
                push @new_offsets, @offsets;
                
                # where the above check collapses the expolygon, then there's no room for an inner loop
                # and we can extract the gap for later processing
                my $diff = diff_ex(
                    [ map @$_, $expolygon->offset_ex(-0.5*$spacing) ],
                    # +2 on the offset here makes sure that Clipper float truncation 
                    # won't shrink the clip polygon to be smaller than intended.
                    [ Slic3r::Geometry::Clipper::offset([map @$_, @offsets], +0.5*$spacing + 2) ],
                );
                push @gaps, grep $_->area >= $gap_area_threshold, @$diff;
            }
            
            last if !@new_offsets || $loop == $loop_number;
            @last_offsets = @new_offsets;
            
            # sort loops before storing them
            @last_offsets = @{chained_path_items([
                map [ $_->contour->[0], $_ ], @last_offsets,
            ])};
            
            push @{ $perimeters[-1] }, [@last_offsets];
        }
        
        # create one more offset to be used as boundary for fill
        {
            # we offset by half the perimeter spacing (to get to the actual infill boundary)
            # and then we offset back and forth by the infill spacing to only consider the
            # non-collapsing regions
            push @{ $self->fill_surfaces },
                map $_->simplify(&Slic3r::SCALED_RESOLUTION),
                @{union_ex([
                    Slic3r::Geometry::Clipper::offset(
                        [Slic3r::Geometry::Clipper::offset([ map @$_, @last_offsets ], -($perimeter_spacing/2 + $infill_spacing))], 
                        +$infill_spacing,
                    ),
                ])};
        }
        
        # fill gaps
        if ($Slic3r::Config->gap_fill_speed > 0 && $Slic3r::Config->fill_density > 0 && @gaps) {
            my $filler = $self->layer->object->print->fill_maker->filler('rectilinear');
            $filler->layer_id($self->layer->id);
            
            # we should probably use this code to handle thin walls and remove that logic from
            # make_surfaces(), but we need to enable dynamic extrusion width before as we can't
            # use zigzag for thin walls.
            # in the mean time we subtract thin walls from the detected gaps so that we don't
            # reprocess them, causing overlapping thin walls and zigzag.
            @gaps = @{diff_ex(
                [ map @$_, @gaps ],
                [ map $_->grow($self->perimeter_flow->scaled_width), @{$self->{thin_walls}} ],
                1,
            )};
            
            my $w = $self->perimeter_flow->width;
            my @widths = (1.5 * $w, $w, 0.4 * $w);  # worth trying 0.2 too?
            foreach my $width (@widths) {
                my $flow = $self->perimeter_flow->clone(width => $width);
                
                # extract the gaps having this width
                my @this_width = map $_->offset_ex(+0.5*$flow->scaled_width),
                    map $_->noncollapsing_offset_ex(-0.5*$flow->scaled_width),
                    @gaps;
                
                if (0) {  # remember to re-enable t/dynamic.t
                    # fill gaps using dynamic extrusion width, by treating them like thin polygons,
                    # thus generating the skeleton and using it to fill them
                    my %path_args = (
                        role            => EXTR_ROLE_SOLIDFILL,
                        flow_spacing    => $flow->spacing,
                    );
                    push @{ $self->thin_fills }, map {
                        $_->isa('Slic3r::Polygon')
                            ? (map $_->pack, Slic3r::ExtrusionLoop->new(polygon => $_, %path_args)->split_at_first_point)  # we should keep these as loops
                            : Slic3r::ExtrusionPath->pack(polyline => $_, %path_args),
                    } map $_->medial_axis($flow->scaled_width), @this_width;
                
                    Slic3r::debugf "  %d gaps filled with extrusion width = %s\n", scalar @this_width, $width
                        if @{ $self->thin_fills };
                    
                } else {
                    # fill gaps using zigzag infill
                    
                    # since this is infill, we have to offset by half-extrusion width inwards
                    my @infill = map $_->offset_ex(-0.5*$flow->scaled_width), @this_width;
                    
                    foreach my $expolygon (@infill) {
                        my @paths = $filler->fill_surface(
                            Slic3r::Surface->new(expolygon => $expolygon),
                            density         => 1,
                            flow_spacing    => $flow->spacing,
                        );
                        my $params = shift @paths;
                        
                        push @{ $self->thin_fills },
                            map {
                                $_->polyline->simplify($flow->scaled_width / 3);
                                $_->pack;
                            }
                            map Slic3r::ExtrusionPath->new(
                                polyline        => Slic3r::Polyline->new(@$_),
                                role            => EXTR_ROLE_GAPFILL,
                                height          => $self->height,
                                flow_spacing    => $params->{flow_spacing},
                            ), @paths;
                    }
                }
                
                # check what's left
                @gaps = @{diff_ex(
                    [ map @$_, @gaps ],
                    [ map @$_, @this_width ],
                )};
            }
        }
    }
    
    # process one island (original surface) at time
    # islands are already sorted with a nearest-neighbor search
    foreach my $island (@perimeters) {
        # do holes starting from innermost one
        my @holes = ();
        my %is_external = ();
        
        # each item of @$island contains the expolygons having the same depth;
        # for each depth we build an arrayref containing all the holes
        my @hole_depths = map [ map $_->holes, @$_ ], @$island;
        
        # organize the outermost hole loops using a nearest-neighbor search
        @{$hole_depths[0]} = @{chained_path_items([
            map [ $_->[0], $_ ], @{$hole_depths[0]},
        ])};
        
        # loop while we have spare holes
        CYCLE: while (map @$_, @hole_depths) {
            # remove first depth container if it contains no holes anymore
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
        
        # first do holes
        $self->_add_perimeter($holes[$_], $is_external{$_} ? EXTR_ROLE_EXTERNAL_PERIMETER : undef)
            for reverse 0 .. $#holes;
        
        # then do contours according to the user settings
        my @contour_order = 0 .. $#$island;
        @contour_order = reverse @contour_order if !$Slic3r::Config->external_perimeters_first;
        for my $depth (@contour_order) {
            my $role = $depth == $#$island ? EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER
                : $depth == 0 ? EXTR_ROLE_EXTERNAL_PERIMETER
                : EXTR_ROLE_PERIMETER;
            $self->_add_perimeter($_, $role) for map $_->contour, @{$island->[$depth]};
        }
    }
    
    # if brim will be printed, reverse the order of perimeters so that
    # we continue inwards after having finished the brim
    if ($self->layer->id == 0 && $Slic3r::Config->brim_width > 0) {
        @{$self->perimeters} = reverse @{$self->perimeters};
    }
    
    # add thin walls as perimeters
    push @{ $self->perimeters }, Slic3r::ExtrusionPath::Collection->new(paths => [
        map {
            Slic3r::ExtrusionPath->pack(
                polyline        => ($_->isa('Slic3r::Polygon') ? $_->split_at_first_point : $_),
                role            => EXTR_ROLE_EXTERNAL_PERIMETER,
                flow_spacing    => $self->perimeter_flow->spacing,
            );
        } @{ $self->thin_walls }
    ])->chained_path;
}

sub _add_perimeter {
    my $self = shift;
    my ($polygon, $role) = @_;
    
    return unless $polygon->is_printable($self->perimeter_flow->scaled_width);
    push @{ $self->perimeters }, Slic3r::ExtrusionLoop->pack(
        polygon         => $polygon,
        role            => ($role // EXTR_ROLE_PERIMETER),
        flow_spacing    => $self->perimeter_flow->spacing,
    );
}

sub prepare_fill_surfaces {
    my $self = shift;
    
    # if no solid layers are requested, turn top/bottom surfaces to internal
    if ($Slic3r::Config->top_solid_layers == 0) {
        $_->surface_type(S_TYPE_INTERNAL) for grep $_->surface_type == S_TYPE_TOP, @{$self->fill_surfaces};
    }
    if ($Slic3r::Config->bottom_solid_layers == 0) {
        $_->surface_type(S_TYPE_INTERNAL) for grep $_->surface_type == S_TYPE_BOTTOM, @{$self->fill_surfaces};
    }
        
    # turn too small internal regions into solid regions according to the user setting
    if ($Slic3r::Config->fill_density > 0) {
        my $min_area = scale scale $Slic3r::Config->solid_infill_below_area; # scaling an area requires two calls!
        my @small = grep $_->surface_type == S_TYPE_INTERNAL && $_->expolygon->contour->area <= $min_area, @{$self->fill_surfaces};
        $_->surface_type(S_TYPE_INTERNALSOLID) for @small;
        Slic3r::debugf "identified %d small solid surfaces at layer %d\n", scalar(@small), $self->id if @small > 0;
    }
}

sub process_external_surfaces {
    my $self = shift;
    
    # enlarge top and bottom surfaces
    {
        # get all external surfaces
        my @top     = grep $_->surface_type == S_TYPE_TOP, @{$self->fill_surfaces};
        my @bottom  = grep $_->surface_type == S_TYPE_BOTTOM, @{$self->fill_surfaces};
        
        # offset them and intersect the results with the actual fill boundaries
        my $margin = scale 3;  # TODO: ensure this is greater than the total thickness of the perimeters
        @top = @{intersection_ex(
            [ Slic3r::Geometry::Clipper::offset([ map $_->p, @top ], +$margin) ],
            [ map $_->p, @{$self->fill_surfaces} ],
            undef,
            1,  # to ensure adjacent expolygons are unified
        )};
        @bottom = @{intersection_ex(
            [ Slic3r::Geometry::Clipper::offset([ map $_->p, @bottom ], +$margin) ],
            [ map $_->p, @{$self->fill_surfaces} ],
            undef,
            1,  # to ensure adjacent expolygons are unified
        )};
        
        # give priority to bottom surfaces
        @top = @{diff_ex(
            [ map @$_, @top ],
            [ map @$_, @bottom ],
        )};
        
        # generate new surfaces
        my @new_surfaces = ();
        push @new_surfaces, map Slic3r::Surface->new(
                expolygon       => $_,
                surface_type    => S_TYPE_TOP,
            ), @top;
        push @new_surfaces, map Slic3r::Surface->new(
                expolygon       => $_,
                surface_type    => S_TYPE_BOTTOM,
            ), @bottom;
        
        # subtract the new top surfaces from the other non-top surfaces and re-add them
        my @other = grep $_->surface_type != S_TYPE_TOP && $_->surface_type != S_TYPE_BOTTOM, @{$self->fill_surfaces};
        foreach my $group (Slic3r::Surface->group(@other)) {
            push @new_surfaces, map $group->[0]->clone(expolygon => $_), @{diff_ex(
                [ map $_->p, @$group ],
                [ map $_->p, @new_surfaces ],
            )};
        }
        @{$self->fill_surfaces} = @new_surfaces;
    }
    
    # detect bridge direction (skip bottom layer)
    if ($self->id > 0) {
        my @bottom  = grep $_->surface_type == S_TYPE_BOTTOM, @{$self->fill_surfaces};  # surfaces
        my @lower   = @{$self->layer->object->layers->[ $self->id - 1 ]->slices};       # expolygons
        
        foreach my $surface (@bottom) {
            # detect what edges lie on lower slices
            my @edges = (); # polylines
            foreach my $lower (@lower) {
                # turn bridge contour and holes into polylines and then clip them
                # with each lower slice's contour
                my @clipped = map $_->split_at_first_point->clip_with_polygon($lower->contour), @{$surface->expolygon};
                if (@clipped == 2) {
                    # If the split_at_first_point() call above happens to split the polygon inside the clipping area
                    # we would get two consecutive polylines instead of a single one, so we use this ugly hack to 
                    # recombine them back into a single one in order to trigger the @edges == 2 logic below.
                    # This needs to be replaced with something way better.
                    if (points_coincide($clipped[0][0], $clipped[-1][-1])) {
                        @clipped = (Slic3r::Polyline->new(@{$clipped[-1]}, @{$clipped[0]}));
                    }
                    if (points_coincide($clipped[-1][0], $clipped[0][-1])) {
                        @clipped = (Slic3r::Polyline->new(@{$clipped[0]}, @{$clipped[1]}));
                    }
                }
                push @edges, @clipped;
            }
            
            Slic3r::debugf "Found bridge on layer %d with %d support(s)\n", $self->id, scalar(@edges);
            next if !@edges;
            
            my $bridge_angle = undef;
            
            if (0) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output("bridge_$surface.svg",
                    expolygons      => [ $surface->expolygon ],
                    red_expolygons  => [ @lower ],
                    polylines       => [ @edges ],
                );
            }
            
            if (@edges == 2) {
                my @chords = map Slic3r::Line->new($_->[0], $_->[-1]), @edges;
                my @midpoints = map $_->midpoint, @chords;
                my $line_between_midpoints = Slic3r::Line->new(@midpoints);
                $bridge_angle = Slic3r::Geometry::rad2deg_dir($line_between_midpoints->direction);
            } elsif (@edges == 1) {
                # TODO: this case includes both U-shaped bridges and plain overhangs;
                # we need a trapezoidation algorithm to detect the actual bridged area
                # and separate it from the overhang area.
                # in the mean time, we're treating as overhangs all cases where
                # our supporting edge is a straight line
                if (@{$edges[0]} > 2) {
                    my $line = Slic3r::Line->new($edges[0]->[0], $edges[0]->[-1]);
                    $bridge_angle = Slic3r::Geometry::rad2deg_dir($line->direction);
                }
            } elsif (@edges) {
                # inset the bridge expolygon; we'll use this one to clip our test lines
                my $inset = [ $surface->expolygon->offset_ex($self->infill_flow->scaled_width) ];
                
                # detect anchors as intersection between our bridge expolygon and the lower slices
                my $anchors = intersection_ex(
                    [ $surface->p ],
                    [ map @$_, @lower ],
                );
                
                # we'll now try several directions using a rudimentary visibility check:
                # bridge in several directions and then sum the length of lines having both
                # endpoints within anchors
                my %directions = ();  # angle => score
                my $angle_increment = PI/36; # 5°
                my $line_increment = $self->infill_flow->scaled_width;
                for (my $angle = 0; $angle <= PI; $angle += $angle_increment) {
                    # rotate everything - the center point doesn't matter
                    $_->rotate($angle, [0,0]) for @$inset, @$anchors;
                    
                    # generate lines in this direction
                    my $bounding_box = [ Slic3r::Geometry::bounding_box([ map @$_, map @$_, @$anchors ]) ];
                    my @lines = ();
                    for (my $x = $bounding_box->[X1]; $x <= $bounding_box->[X2]; $x += $line_increment) {
                        push @lines, [ [$x, $bounding_box->[Y1]], [$x, $bounding_box->[Y2]] ];
                    }
                    
                    # TODO: use a multi_polygon_multi_linestring_intersection() call
                    my @clipped_lines = map @{ Boost::Geometry::Utils::polygon_multi_linestring_intersection($_, \@lines) }, @$inset;
                    
                    # remove any line not having both endpoints within anchors
                    @clipped_lines = grep {
                        my $line = $_;
                        !(first { $_->encloses_point_quick($line->[A]) } @$anchors)
                            && !(first { $_->encloses_point_quick($line->[B]) } @$anchors);
                    } @clipped_lines;
                    
                    # sum length of bridged lines
                    $directions{-$angle} = sum(map Slic3r::Geometry::line_length($_), @clipped_lines) // 0;
                }
                
                # this could be slightly optimized with a max search instead of the sort
                my @sorted_directions = sort { $directions{$a} <=> $directions{$b} } keys %directions;
                
                # the best direction is the one causing most lines to be bridged
                $bridge_angle = Slic3r::Geometry::rad2deg_dir($sorted_directions[-1]);
            }
            
            Slic3r::debugf "  Optimal infill angle of bridge on layer %d is %d degrees\n",
                $self->id, $bridge_angle if defined $bridge_angle;
            
            $surface->bridge_angle($bridge_angle);
        }
    }
}

1;
