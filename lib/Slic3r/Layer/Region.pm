package Slic3r::Layer::Region;
use Moo;

use List::Util qw(sum first);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(PI A B scale unscale chained_path points_coincide);
use Slic3r::Geometry::Clipper qw(union_ex diff_ex intersection_ex 
    offset offset_ex offset2 offset2_ex union_pt diff intersection
    union diff intersection_pl);
use Slic3r::Surface ':types';

has 'layer' => (
    is          => 'ro',
    weak_ref    => 1,
    required    => 1,
    handles     => [qw(id slice_z print_z height object print)],
);
has 'region'            => (is => 'ro', required => 1, handles => [qw(config)]);
has 'infill_area_threshold' => (is => 'lazy');
has 'overhang_width'    => (is => 'lazy');

# collection of surfaces generated by slicing the original geometry
# divided by type top/bottom/internal
has 'slices' => (is => 'rw', default => sub { Slic3r::Surface::Collection->new });

# collection of extrusion paths/loops filling gaps
has 'thin_fills' => (is => 'rw', default => sub { Slic3r::ExtrusionPath::Collection->new });

# collection of surfaces for infill generation
has 'fill_surfaces' => (is => 'rw', default => sub { Slic3r::Surface::Collection->new });

# ordered collection of extrusion paths/loops to build all perimeters
has 'perimeters' => (is => 'rw', default => sub { Slic3r::ExtrusionPath::Collection->new });

# ordered collection of extrusion paths to fill surfaces
has 'fills' => (is => 'rw', default => sub { Slic3r::ExtrusionPath::Collection->new });

sub _build_overhang_width {
    my $self = shift;
    my $threshold_rad = PI/2 - atan2($self->flow(FLOW_ROLE_PERIMETER)->width / $self->height / 2, 1);
    return scale($self->height * ((cos $threshold_rad) / (sin $threshold_rad)));
}

sub _build_infill_area_threshold {
    my $self = shift;
    return $self->flow(FLOW_ROLE_SOLID_INFILL)->scaled_spacing ** 2;
}

sub flow {
    my ($self, $role, $bridge, $width) = @_;
    return $self->region->flow(
        $role,
        $self->layer->height,
        $bridge // 0,
        $self->layer->id == 0,
        $width,
    );
}

sub make_perimeters {
    my $self = shift;
    
    my $perimeter_flow      = $self->flow(FLOW_ROLE_PERIMETER);
    my $mm3_per_mm          = $perimeter_flow->mm3_per_mm($self->height);
    my $pwidth              = $perimeter_flow->scaled_width;
    my $pspacing            = $perimeter_flow->scaled_spacing;
    my $solid_infill_flow   = $self->flow(FLOW_ROLE_SOLID_INFILL);
    my $ispacing            = $solid_infill_flow->scaled_spacing;
    my $gap_area_threshold  = $pwidth ** 2;
    
    $self->perimeters->clear;
    $self->fill_surfaces->clear;
    $self->thin_fills->clear;
    
    my @contours    = ();    # array of Polygons with ccw orientation
    my @holes       = ();    # array of Polygons with cw orientation
    my @thin_walls  = ();    # array of ExPolygons
    my @gaps        = ();    # array of ExPolygons
    
    # we need to process each island separately because we might have different
    # extra perimeters for each one
    foreach my $surface (@{$self->slices}) {
        # detect how many perimeters must be generated for this island
        my $loop_number = $self->config->perimeters + ($surface->extra_perimeters || 0);
        
        my @last = @{$surface->expolygon};
        my @last_gaps = ();
        if ($loop_number > 0) {
            # we loop one time more than needed in order to find gaps after the last perimeter was applied
            for my $i (1 .. ($loop_number+1)) {  # outer loop is 1
                my @offsets = ();
                if ($i == 1) {
                    # the minimum thickness of a single loop is:
                    # width/2 + spacing/2 + spacing/2 + width/2
                    @offsets = @{offset2(\@last, -(0.5*$pwidth + 0.5*$pspacing - 1), +(0.5*$pspacing - 1))};
                    
                    # look for thin walls
                    if ($self->config->thin_walls) {
                        my $diff = diff_ex(
                            \@last,
                            offset(\@offsets, +0.5*$pwidth),
                            1,  # medial axis requires non-overlapping geometry
                        );
                        push @thin_walls, @$diff;
                    }
                } else {
                    @offsets = @{offset2(\@last, -(1.5*$pspacing - 1), +(0.5*$pspacing - 1))};
                
                    # look for gaps
                    if ($self->print->config->gap_fill_speed > 0 && $self->config->fill_density > 0) {
                        my $diff = diff_ex(
                            offset(\@last, -0.5*$pspacing),
                            offset(\@offsets, +0.5*$pspacing),
                        );
                        push @gaps, @last_gaps = grep abs($_->area) >= $gap_area_threshold, @$diff;
                    }
                }
            
                last if !@offsets;
                last if $i > $loop_number; # we were only looking for gaps this time
            
                # clone polygons because these ExPolygons will go out of scope very soon
                @last = @offsets;
                foreach my $polygon (@offsets) {
                    if ($polygon->is_counter_clockwise) {
                        push @contours, $polygon;
                    } else {
                        push @holes, $polygon;
                    }
                }
            }
        }
        
        # make sure we don't infill narrow parts that are already gap-filled
        # (we only consider this surface's gaps to reduce the diff() complexity)
        @last = @{diff(\@last, [ map @$_, @last_gaps ])};
        
        # create one more offset to be used as boundary for fill
        # we offset by half the perimeter spacing (to get to the actual infill boundary)
        # and then we offset back and forth by half the infill spacing to only consider the
        # non-collapsing regions
        $self->fill_surfaces->append(
            map Slic3r::Surface->new(expolygon => $_, surface_type => S_TYPE_INTERNAL),  # use a bogus surface type
            @{offset2_ex(
                [ map @{$_->simplify_p(&Slic3r::SCALED_RESOLUTION)}, @{union_ex(\@last)} ],
                -($pspacing/2 + $ispacing/2),
                +$ispacing/2,
            )}
        );
    }
    
    # process thin walls by collapsing slices to single passes
    my @thin_wall_polylines = ();
    if (@thin_walls) {
        # the following offset2 ensures almost nothing in @thin_walls is narrower than $min_width
        # (actually, something larger than that still may exist due to mitering or other causes)
        my $min_width = $pwidth / 4;
        @thin_walls = @{offset2_ex([ map @$_, @thin_walls ], -$min_width/2, +$min_width/2)};
        
        # the maximum thickness of our thin wall area is equal to the minimum thickness of a single loop
        @thin_wall_polylines = map @{$_->medial_axis($pwidth + $pspacing, $min_width)}, @thin_walls;
        Slic3r::debugf "  %d thin walls detected\n", scalar(@thin_wall_polylines) if $Slic3r::debug;
        
        if (0) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output(
                "medial_axis.svg",
                no_arrows => 1,
                expolygons      => \@thin_walls,
                green_polylines => [ map $_->polygon->split_at_first_point, @{$self->perimeters} ],
                red_polylines   => \@thin_wall_polylines,
            );
        }
    }
    
    # find nesting hierarchies separately for contours and holes
    my $contours_pt = union_pt(\@contours);
    my $holes_pt    = union_pt(\@holes);
    
    # prepare a coderef for traversing the PolyTree object
    # external contours are root items of $contours_pt
    # internal contours are the ones next to external
    my $traverse;
    $traverse = sub {
        my ($polynodes, $depth, $is_contour) = @_;
        
        # convert all polynodes to ExtrusionLoop objects
        my $collection = Slic3r::ExtrusionPath::Collection->new;
        my @children = ();
        foreach my $polynode (@$polynodes) {
            my $polygon = ($polynode->{outer} // $polynode->{hole})->clone;
            
            # return ccw contours and cw holes
            # GCode.pm will convert all of them to ccw, but it needs to know
            # what the holes are in order to compute the correct inwards move
            if ($is_contour) {
                $polygon->make_counter_clockwise;
            } else {
                $polygon->make_clockwise;
            }
            
            my $role = EXTR_ROLE_PERIMETER;
            if ($is_contour ? $depth == 0 : !@{ $polynode->{children} }) {
                # external perimeters are root level in case of contours
                # and items with no children in case of holes
                $role = EXTR_ROLE_EXTERNAL_PERIMETER;
            } elsif ($depth == 1 && $is_contour) {
                $role = EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER;
            }
            
            $collection->append(Slic3r::ExtrusionLoop->new(
                polygon         => $polygon,
                role            => $role,
                mm3_per_mm      => $mm3_per_mm,
            ));
            
            # save the children
            push @children, $polynode->{children};
        }

        # if we're handling the top-level contours, add thin walls as candidates too
        # in order to include them in the nearest-neighbor search
        if ($is_contour && $depth == 0) {
            foreach my $polyline (@thin_wall_polylines) {
                $collection->append(Slic3r::ExtrusionPath->new(
                    polyline        => $polyline,
                    role            => EXTR_ROLE_EXTERNAL_PERIMETER,
                    mm3_per_mm      => $mm3_per_mm,
                ));
            }
        }
        
        # use a nearest neighbor search to order these children
        # TODO: supply second argument to chained_path() too?
        my $sorted_collection = $collection->chained_path(0);
        my @orig_indices = @{$sorted_collection->orig_indices};
        
        my @loops = ();
        foreach my $loop (@$sorted_collection) {
            my $orig_index = shift @orig_indices;
            
            if ($loop->isa('Slic3r::ExtrusionPath')) {
                push @loops, $loop->clone;
            } else {
                # if this is an external contour find all holes belonging to this contour(s)
                # and prepend them
                if ($is_contour && $depth == 0) {
                    # $loop is the outermost loop of an island
                    my @holes = ();
                    for (my $i = 0; $i <= $#$holes_pt; $i++) {
                        if ($loop->contains_point($holes_pt->[$i]{outer}->first_point)) {
                            push @holes, splice @$holes_pt, $i, 1;  # remove from candidates to reduce complexity
                            $i--;
                        }
                    }
                    push @loops, reverse map $traverse->([$_], 0, 0), @holes;
                }
                
                # traverse children and prepend them to this loop
                push @loops, $traverse->($children[$orig_index], $depth+1, $is_contour);
                push @loops, $loop->clone;
            }
        }
        return @loops;
    };
    
    # order loops from inner to outer (in terms of object slices)
    my @loops = $traverse->($contours_pt, 0, 1);
    
    # if brim will be printed, reverse the order of perimeters so that
    # we continue inwards after having finished the brim
    # TODO: add test for perimeter order
    @loops = reverse @loops
        if $self->print->config->external_perimeters_first
            || ($self->layer->id == 0 && $self->print->config->brim_width > 0);
    
    # append perimeters
    $self->perimeters->append(@loops);
    
    # fill gaps
    {
        my $fill_gaps = sub {
            my ($min, $max, $w) = @_;
            
            my $this = diff_ex(
                offset2([ map @$_, @gaps ], -$min/2, +$min/2),
                offset2([ map @$_, @gaps ], -$max/2, +$max/2),
                1,
            );
            
            my $flow = $self->flow(FLOW_ROLE_SOLID_INFILL, 0, $w);
            my %path_args = (
                role        => EXTR_ROLE_GAPFILL,
                mm3_per_mm  => $flow->mm3_per_mm($self->height),
            );
            my @polylines = map @{$_->medial_axis($max, $min/2)}, @$this;
            $self->thin_fills->append(map {
                $_->isa('Slic3r::Polygon')
                    ? Slic3r::ExtrusionLoop->new(polygon => $_, %path_args)->split_at_first_point  # should we keep these as loops?
                    : Slic3r::ExtrusionPath->new(polyline => $_, %path_args),
            } @polylines);

            Slic3r::debugf "  %d gaps filled with extrusion width = %s\n", scalar @$this, $w
                if @$this;
        };
        
        # where $pwidth < thickness < 2*$pspacing, infill with width = 1.5*$pwidth
        # where 0.5*$pwidth < thickness < $pwidth, infill with width = 0.5*$pwidth
        $fill_gaps->($pwidth, 2*$pspacing, unscale 1.5*$pwidth);
        $fill_gaps->(0.5*$pwidth, $pwidth, unscale 0.5*$pwidth);
    }
}

sub prepare_fill_surfaces {
    my $self = shift;
    
    # if no solid layers are requested, turn top/bottom surfaces to internal
    if ($self->config->top_solid_layers == 0) {
        $_->surface_type(S_TYPE_INTERNAL) for @{$self->fill_surfaces->filter_by_type(S_TYPE_TOP)};
    }
    if ($self->config->bottom_solid_layers == 0) {
        $_->surface_type(S_TYPE_INTERNAL) for @{$self->fill_surfaces->filter_by_type(S_TYPE_BOTTOM)};
    }
        
    # turn too small internal regions into solid regions according to the user setting
    if ($self->config->fill_density > 0) {
        my $min_area = scale scale $self->config->solid_infill_below_area; # scaling an area requires two calls!
        $_->surface_type(S_TYPE_INTERNALSOLID)
            for grep { $_->area <= $min_area } @{$self->fill_surfaces->filter_by_type(S_TYPE_INTERNAL)};
    }
}

sub process_external_surfaces {
    my ($self, $lower_layer) = @_;
    
    my @surfaces = @{$self->fill_surfaces};
    my $margin = scale &Slic3r::EXTERNAL_INFILL_MARGIN;
    
    my @bottom = ();
    foreach my $surface (grep $_->surface_type == S_TYPE_BOTTOM, @surfaces) {
        my $grown = $surface->expolygon->offset_ex(+$margin);
        
        # detect bridge direction before merging grown surfaces otherwise adjacent bridges
        # would get merged into a single one while they need different directions
        # also, supply the original expolygon instead of the grown one, because in case
        # of very thin (but still working) anchors, the grown expolygon would go beyond them
        my $angle = $lower_layer
            ? $self->_detect_bridge_direction($surface->expolygon, $lower_layer)
            : undef;
        
        push @bottom, map $surface->clone(expolygon => $_, bridge_angle => $angle), @$grown;
    }
    
    my @top = ();
    foreach my $surface (grep $_->surface_type == S_TYPE_TOP, @surfaces) {
        # give priority to bottom surfaces
        my $grown = diff_ex(
            $surface->expolygon->offset(+$margin),
            [ map $_->p, @bottom ],
        );
        push @top, map $surface->clone(expolygon => $_), @$grown;
    }
    
    # if we're slicing with no infill, we can't extend external surfaces
    # over non-existent infill
    my @fill_boundaries = $self->config->fill_density > 0
        ? @surfaces
        : grep $_->surface_type != S_TYPE_INTERNAL, @surfaces;
    
    # intersect the grown surfaces with the actual fill boundaries
    my @new_surfaces = ();
    foreach my $group (@{Slic3r::Surface::Collection->new(@top, @bottom)->group}) {
        push @new_surfaces,
            map $group->[0]->clone(expolygon => $_),
            @{intersection_ex(
                [ map $_->p, @$group ],
                [ map $_->p, @fill_boundaries ],
                1,  # to ensure adjacent expolygons are unified
            )};
    }
    
    # subtract the new top surfaces from the other non-top surfaces and re-add them
    my @other = grep $_->surface_type != S_TYPE_TOP && $_->surface_type != S_TYPE_BOTTOM, @surfaces;
    foreach my $group (@{Slic3r::Surface::Collection->new(@other)->group}) {
        push @new_surfaces, map $group->[0]->clone(expolygon => $_), @{diff_ex(
            [ map $_->p, @$group ],
            [ map $_->p, @new_surfaces ],
        )};
    }
    $self->fill_surfaces->clear;
    $self->fill_surfaces->append(@new_surfaces);
}

sub _detect_bridge_direction {
    my ($self, $expolygon, $lower_layer) = @_;
    
    my $perimeter_flow  = $self->flow(FLOW_ROLE_PERIMETER);
    my $infill_flow     = $self->flow(FLOW_ROLE_INFILL);
    
    my $grown = $expolygon->offset(+$perimeter_flow->scaled_width);
    my @lower = @{$lower_layer->slices};       # expolygons
    
    # detect what edges lie on lower slices
    my @edges = (); # polylines
    foreach my $lower (@lower) {
        # turn bridge contour and holes into polylines and then clip them
        # with each lower slice's contour
        my @clipped = @{intersection_pl([ map $_->split_at_first_point, @$grown ], [$lower->contour])};
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
    return undef if !@edges;
    
    my $bridge_angle = undef;
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("bridge_$expolygon.svg",
            expolygons      => [ $expolygon ],
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
        my $inset = $expolygon->offset_ex($infill_flow->scaled_width);
        
        # detect anchors as intersection between our bridge expolygon and the lower slices
        my $anchors = intersection_ex(
            $grown,
            [ map @$_, @lower ],
            1,  # safety offset required to avoid Clipper from detecting empty intersection while Boost actually found some @edges
        );
        
        if (@$anchors) {
            # we'll now try several directions using a rudimentary visibility check:
            # bridge in several directions and then sum the length of lines having both
            # endpoints within anchors
            my %directions = ();  # angle => score
            my $angle_increment = PI/36; # 5°
            my $line_increment = $infill_flow->scaled_width;
            for (my $angle = 0; $angle <= PI; $angle += $angle_increment) {
                # rotate everything - the center point doesn't matter
                $_->rotate($angle, [0,0]) for @$inset, @$anchors;
            
                # generate lines in this direction
                my $bounding_box = Slic3r::Geometry::BoundingBox->new_from_points([ map @$_, map @$_, @$anchors ]);
            
                my @lines = ();
                for (my $x = $bounding_box->x_min; $x <= $bounding_box->x_max; $x += $line_increment) {
                    push @lines, Slic3r::Polyline->new([$x, $bounding_box->y_min], [$x, $bounding_box->y_max]);
                }
            
                my @clipped_lines = map Slic3r::Line->new(@$_), @{ intersection_pl(\@lines, [ map @$_, @$inset ]) };
            
                # remove any line not having both endpoints within anchors
                # NOTE: these calls to contains_point() probably need to check whether the point 
                # is on the anchor boundaries too
                @clipped_lines = grep {
                    my $line = $_;
                    !(first { $_->contains_point($line->a) } @$anchors)
                        && !(first { $_->contains_point($line->b) } @$anchors);
                } @clipped_lines;
            
                # sum length of bridged lines
                $directions{-$angle} = sum(map $_->length, @clipped_lines) // 0;
            }
        
            # this could be slightly optimized with a max search instead of the sort
            my @sorted_directions = sort { $directions{$a} <=> $directions{$b} } keys %directions;
    
            # the best direction is the one causing most lines to be bridged
            $bridge_angle = Slic3r::Geometry::rad2deg_dir($sorted_directions[-1]);
        }
    }
    
    Slic3r::debugf "  Optimal infill angle of bridge on layer %d is %d degrees\n",
        $self->id, $bridge_angle if defined $bridge_angle;
    
    return $bridge_angle;
}

1;
