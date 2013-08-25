package Slic3r::GUI::Plater::ObjectDialog;
use strict;
use warnings;
use utf8;

use Wx qw(:dialog :id :misc :sizer :systemsettings :notebook wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_BUTTON);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Object", wxDefaultPosition, [500,500], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    $self->{object} = $params{object};
    
    $self->{tabpanel} = Wx::Notebook->new($self, -1, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL);
    $self->{tabpanel}->AddPage($self->{preview} = Slic3r::GUI::Plater::ObjectDialog::PreviewTab->new($self->{tabpanel}, object => $self->{object}), "Preview")
        if $Slic3r::GUI::have_OpenGL;
    $self->{tabpanel}->AddPage($self->{info} = Slic3r::GUI::Plater::ObjectDialog::InfoTab->new($self->{tabpanel}, object => $self->{object}), "Info");
    $self->{tabpanel}->AddPage($self->{settings} = Slic3r::GUI::Plater::ObjectDialog::SettingsTab->new($self->{tabpanel}, object => $self->{object}), "Settings");
    $self->{tabpanel}->AddPage($self->{layers} = Slic3r::GUI::Plater::ObjectDialog::LayersTab->new($self->{tabpanel}, object => $self->{object}), "Layers");
    
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK);
    EVT_BUTTON($self, wxID_OK, sub {
        # validate user input
        return if !$self->{settings}->CanClose;
        return if !$self->{layers}->CanClose;
        
        # notify tabs
        $self->{layers}->Closing;
        
        $self->EndModal(wxID_OK);
    });
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($self->{tabpanel}, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    $sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    
    return $self;
}

package Slic3r::GUI::Plater::ObjectDialog::InfoTab;
use Wx qw(:dialog :id :misc :sizer :systemsettings);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    $self->{object} = $params{object};
    
    my $grid_sizer = Wx::FlexGridSizer->new(3, 2, 5, 5);
    $grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    $grid_sizer->AddGrowableCol(1);
    
    my $label_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $label_font->SetPointSize(10);
    
    my $properties = $self->get_properties;
    foreach my $property (@$properties) {
    	my $label = Wx::StaticText->new($self, -1, $property->[0] . ":");
    	my $value = Wx::StaticText->new($self, -1, $property->[1]);
    	$label->SetFont($label_font);
	    $grid_sizer->Add($label, 1, wxALIGN_BOTTOM);
	    $grid_sizer->Add($value, 0);
    }
    
    $self->SetSizer($grid_sizer);
    $grid_sizer->SetSizeHints($self);
    
    return $self;
}

sub get_properties {
	my $self = shift;
	
	my $object = $self->{object};
	my $properties = [
		['Name'			=> $object->name],
		['Size'			=> sprintf "%.2f x %.2f x %.2f", @{$object->transformed_size}],
		['Facets'		=> $object->facets],
		['Vertices'		=> $object->vertices],
		['Materials' 	=> $object->materials],
	];
	
	if (my $stats = $object->mesh_stats) {
	    push @$properties,
	        [ 'Shells'              => $stats->{number_of_parts} ],
	        [ 'Volume'              => sprintf('%.2f', $stats->{volume} * ($object->scale**3)) ],
	        [ 'Degenerate facets'   => $stats->{degenerate_facets} ],
	        [ 'Edges fixed'         => $stats->{edges_fixed} ],
	        [ 'Facets removed'      => $stats->{facets_removed} ],
	        [ 'Facets added'        => $stats->{facets_added} ],
	        [ 'Facets reversed'     => $stats->{facets_reversed} ],
	        [ 'Backwards edges'     => $stats->{backwards_edges} ],
	        # we don't show normals_fixed because we never provide normals
	        # to admesh, so it generates normals for all facets
	        ;
	} else {
	    push @$properties,
	        ['Two-Manifold' => $object->is_manifold ? 'Yes' : 'No'],
	        ;
	}
	
	return $properties;
}

package Slic3r::GUI::Plater::ObjectDialog::PreviewTab;
use Wx qw(:dialog :id :misc :sizer :systemsettings);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    $self->{object} = $params{object};
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add(Slic3r::GUI::PreviewCanvas->new($self, $self->{object}->get_model_object), 1, wxEXPAND, 0);
    $self->SetSizer($sizer);
    $sizer->SetSizeHints($self);
    
    return $self;
}

package Slic3r::GUI::Plater::ObjectDialog::SettingsTab;
use Wx qw(:dialog :id :misc :sizer :systemsettings :button :icon);
use Wx::Grid;
use Wx::Event qw(EVT_BUTTON);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    $self->{object} = $params{object};
    
    $self->{sizer} = Wx::BoxSizer->new(wxVERTICAL);
    
    # descriptive text
    {
        my $label = Wx::StaticText->new($self, -1, "You can use this section to override some settings just for this object.",
            wxDefaultPosition, [-1, 25]);
        $label->SetFont(Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        $self->{sizer}->Add($label, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    }
    
    # option selector
    {
        # get all options with object scope and sort them by category+label
        my %settings = map { $_ => sprintf('%s > %s', $Slic3r::Config::Options->{$_}{category}, $Slic3r::Config::Options->{$_}{full_label} // $Slic3r::Config::Options->{$_}{label}) }
            grep { ($Slic3r::Config::Options->{$_}{scope} // '') eq 'object' }
            keys %$Slic3r::Config::Options;
        $self->{options} = [ sort { $settings{$a} cmp $settings{$b} } keys %settings ];
        my $choice = Wx::Choice->new($self, -1, wxDefaultPosition, [150, -1], [ map $settings{$_}, @{$self->{options}} ]);
        
        # create the button
        my $btn = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/add.png", wxBITMAP_TYPE_PNG));
        EVT_BUTTON($self, $btn, sub {
            my $opt_key = $self->{options}[$choice->GetSelection];
            $self->{object}->config->apply(Slic3r::Config->new_from_defaults($opt_key));
            $self->update_optgroup;
        });
        
        my $h_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $h_sizer->Add($choice, 1, wxEXPAND | wxALL, 0);
        $h_sizer->Add($btn, 0, wxEXPAND | wxLEFT, 10);
        $self->{sizer}->Add($h_sizer, 0, wxEXPAND | wxALL, 10);
    }
    
    $self->{options_sizer} = Wx::BoxSizer->new(wxVERTICAL);
    $self->{sizer}->Add($self->{options_sizer}, 0, wxEXPAND | wxALL, 10);
    
    $self->update_optgroup;
    
    $self->SetSizer($self->{sizer});
    $self->{sizer}->SetSizeHints($self);
    
    return $self;
}

sub update_optgroup {
    my $self = shift;
    
    $self->{options_sizer}->Clear(1);
    
    my $config = $self->{object}->config;
    my %categories = ();
    foreach my $opt_key (keys %$config) {
        my $category = $Slic3r::Config::Options->{$opt_key}{category};
        $categories{$category} ||= [];
        push @{$categories{$category}}, $opt_key;
    }
    foreach my $category (sort keys %categories) {
        my $optgroup = Slic3r::GUI::ConfigOptionsGroup->new(
            parent      => $self,
            title       => $category,
            config      => $config,
            options     => [ sort @{$categories{$category}} ],
            full_labels => 1,
            extra_column => sub {
                my ($line) = @_;
                my ($opt_key) = @{$line->{options}};  # we assume that we have one option per line
                my $btn = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/delete.png", wxBITMAP_TYPE_PNG));
                EVT_BUTTON($self, $btn, sub {
                    delete $self->{object}->config->{$opt_key};
                    Slic3r::GUI->CallAfter(sub { $self->update_optgroup });
                });
                return $btn;
            },
        );
        $self->{options_sizer}->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM, 10);
    }
    $self->Layout;
}

sub CanClose {
    my $self = shift;
    
    # validate options before allowing user to dismiss the dialog
    # the validate method only works on full configs so we have
    # to merge our settings with the default ones
    my $config = Slic3r::Config->merge($self->GetParent->GetParent->GetParent->GetParent->GetParent->config, $self->{object}->config);
    eval {
        $config->validate;
    };
    return 0 if Slic3r::GUI::catch_error($self);    
    return 1;
}

package Slic3r::GUI::Plater::ObjectDialog::LayersTab;
use Wx qw(:dialog :id :misc :sizer :systemsettings);
use Wx::Grid;
use Wx::Event qw(EVT_GRID_CELL_CHANGED);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    $self->{object} = $params{object};
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    
    {
        my $label = Wx::StaticText->new($self, -1, "You can use this section to override the default layer height for parts of this object. Set layer height to zero to skip portions of the input file.",
            wxDefaultPosition, [-1, 25]);
        $label->SetFont(Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        $sizer->Add($label, 0, wxEXPAND | wxALL, 10);
    }
    
    my $grid = $self->{grid} = Wx::Grid->new($self, -1, wxDefaultPosition, wxDefaultSize);
    $sizer->Add($grid, 1, wxEXPAND | wxALL, 10);
    $grid->CreateGrid(0, 3);
    $grid->DisableDragRowSize;
    $grid->HideRowLabels if &Wx::wxVERSION_STRING !~ / 2\.8\./;
    $grid->SetColLabelValue(0, "Min Z (mm)");
    $grid->SetColLabelValue(1, "Max Z (mm)");
    $grid->SetColLabelValue(2, "Layer height (mm)");
    $grid->SetColSize($_, 135) for 0..2;
    $grid->SetDefaultCellAlignment(wxALIGN_CENTRE, wxALIGN_CENTRE);
    
    # load data
    foreach my $range (@{ $self->{object}->layer_height_ranges }) {
        $grid->AppendRows(1);
        my $i = $grid->GetNumberRows-1;
        $grid->SetCellValue($i, $_, $range->[$_]) for 0..2;
    }
    $grid->AppendRows(1); # append one empty row
    
    EVT_GRID_CELL_CHANGED($grid, sub {
        my ($grid, $event) = @_;
        
        # remove any non-numeric character
        my $value = $grid->GetCellValue($event->GetRow, $event->GetCol);
        $value =~ s/,/./g;
        $value =~ s/[^0-9.]//g;
        $grid->SetCellValue($event->GetRow, $event->GetCol, $value);
        
        # if there's no empty row, let's append one
        for my $i (0 .. $grid->GetNumberRows-1) {
            if (!grep $grid->GetCellValue($i, $_), 0..2) {
                return;
            }
        }
        $grid->AppendRows(1);
    });
    
    $self->SetSizer($sizer);
    $sizer->SetSizeHints($self);
    
    return $self;
}

sub CanClose {
    my $self = shift;
    
    # validate ranges before allowing user to dismiss the dialog
    
    foreach my $range ($self->_get_ranges) {
        my ($min, $max, $height) = @$range;
        if ($max <= $min) {
            Slic3r::GUI::show_error($self, "Invalid Z range $min-$max.");
            return 0;
        }
        if ($min < 0 || $max < 0) {
            Slic3r::GUI::show_error($self, "Invalid Z range $min-$max.");
            return 0;
        }
        if ($height < 0) {
            Slic3r::GUI::show_error($self, "Invalid layer height $height.");
            return 0;
        }
        # TODO: check for overlapping ranges
    }
    
    return 1;
}

sub Closing {
    my $self = shift;
    
    # save ranges into the plater object
    $self->{object}->layer_height_ranges([ $self->_get_ranges ]);
}

sub _get_ranges {
    my $self = shift;
    
    my @ranges = ();
    for my $i (0 .. $self->{grid}->GetNumberRows-1) {
        my ($min, $max, $height) = map $self->{grid}->GetCellValue($i, $_), 0..2;
        next if $min eq '' || $max eq '' || $height eq '';
        push @ranges, [ $min, $max, $height ];
    }
    return sort { $a->[0] <=> $b->[0] } @ranges;
}

1;
