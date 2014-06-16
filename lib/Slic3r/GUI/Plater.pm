package Slic3r::GUI::Plater;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename dirname);
use List::Util qw(sum first);
use Slic3r::Geometry qw(X Y Z MIN MAX scale unscale deg2rad);
use threads::shared qw(shared_clone);
use Thread::Semaphore;
use Wx qw(:button :cursor :dialog :filedialog :keycode :icon :font :id :listctrl :misc 
    :panel :sizer :toolbar :window wxTheApp);
use Wx::Event qw(EVT_BUTTON EVT_COMMAND EVT_KEY_DOWN EVT_LIST_ITEM_ACTIVATED 
    EVT_LIST_ITEM_DESELECTED EVT_LIST_ITEM_SELECTED EVT_MOUSE_EVENTS EVT_PAINT EVT_TOOL 
    EVT_CHOICE EVT_TIMER);
use base 'Wx::Panel';

use constant TB_ADD             => &Wx::NewId;
use constant TB_REMOVE          => &Wx::NewId;
use constant TB_RESET           => &Wx::NewId;
use constant TB_ARRANGE         => &Wx::NewId;
use constant TB_EXPORT_GCODE    => &Wx::NewId;
use constant TB_EXPORT_STL      => &Wx::NewId;
use constant TB_MORE    => &Wx::NewId;
use constant TB_FEWER   => &Wx::NewId;
use constant TB_45CW    => &Wx::NewId;
use constant TB_45CCW   => &Wx::NewId;
use constant TB_SCALE   => &Wx::NewId;
use constant TB_SPLIT   => &Wx::NewId;
use constant TB_VIEW    => &Wx::NewId;
use constant TB_SETTINGS => &Wx::NewId;
use constant apply_config_timer_EVENT => &Wx::NewId;

# package variables to avoid passing lexicals to threads
our $THUMBNAIL_DONE_EVENT    : shared = Wx::NewEventType;
our $PROGRESS_BAR_EVENT      : shared = Wx::NewEventType;
our $ERROR_EVENT             : shared = Wx::NewEventType;
our $EXPORT_COMPLETED_EVENT  : shared = Wx::NewEventType;
our $PROCESS_COMPLETED_EVENT : shared = Wx::NewEventType;

use constant FILAMENT_CHOOSERS_SPACING => 3;
use constant PROCESS_DELAY => 0.5 * 1000; # milliseconds

my $PreventListEvents = 0;

my $sema = Thread::Semaphore->new;

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    $self->{config} = Slic3r::Config->new_from_defaults(qw(
        bed_size bed_shape print_center complete_objects extruder_clearance_radius skirts skirt_distance
    ));
    $self->{model} = Slic3r::Model->new;
    $self->{print} = Slic3r::Print->new;
    $self->{objects} = [];
    $self->{apply_config_timer} = Wx::Timer->new($self, apply_config_timer_EVENT)
        if $Slic3r::have_threads;
    
    $self->{print}->set_status_cb(sub {
        my ($percent, $message) = @_;
        
        if ($Slic3r::have_threads) {
            Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $PROGRESS_BAR_EVENT, shared_clone([$percent, $message])));
        } else {
            $self->on_progress_event($percent, $message);
        }
    });
    
    $self->{canvas} = Slic3r::GUI::Plater::2D->new($self, [335,335], $self->{objects}, $self->{model}, $self->{config});
    $self->{canvas}->on_select_object(sub {
        my ($obj_idx) = @_;
        $self->select_object($obj_idx);
    });
    $self->{canvas}->on_double_click(sub {
        $self->object_cut_dialog if $self->selected_object;
    });
    $self->{canvas}->on_right_click(sub {
        my ($click_pos) = @_;
        
        my ($obj_idx, $object) = $self->selected_object;
        return if !defined $obj_idx;
        
        my $menu = $self->object_menu;
        $self->{canvas}->PopupMenu($menu, $click_pos);
        $menu->Destroy;
    });
    $self->{canvas}->on_instance_moved(sub {
        $self->update;
    });
    
    # toolbar for object manipulation
    if (!&Wx::wxMSW) {
        Wx::ToolTip::Enable(1);
        $self->{htoolbar} = Wx::ToolBar->new($self, -1, wxDefaultPosition, wxDefaultSize, wxTB_HORIZONTAL | wxTB_TEXT | wxBORDER_SIMPLE | wxTAB_TRAVERSAL);
        $self->{htoolbar}->AddTool(TB_ADD, "Add…", Wx::Bitmap->new("$Slic3r::var/brick_add.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_REMOVE, "Delete", Wx::Bitmap->new("$Slic3r::var/brick_delete.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_RESET, "Delete All", Wx::Bitmap->new("$Slic3r::var/cross.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_ARRANGE, "Arrange", Wx::Bitmap->new("$Slic3r::var/bricks.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_MORE, "More", Wx::Bitmap->new("$Slic3r::var/add.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_FEWER, "Fewer", Wx::Bitmap->new("$Slic3r::var/delete.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_45CCW, "45° ccw", Wx::Bitmap->new("$Slic3r::var/arrow_rotate_anticlockwise.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_45CW, "45° cw", Wx::Bitmap->new("$Slic3r::var/arrow_rotate_clockwise.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_SCALE, "Scale…", Wx::Bitmap->new("$Slic3r::var/arrow_out.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_SPLIT, "Split", Wx::Bitmap->new("$Slic3r::var/shape_ungroup.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_VIEW, "View/Cut…", Wx::Bitmap->new("$Slic3r::var/package.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_SETTINGS, "Settings…", Wx::Bitmap->new("$Slic3r::var/cog.png", wxBITMAP_TYPE_PNG), '');
    } else {
        my %tbar_buttons = (
            add             => "Add…",
            remove          => "Delete",
            reset           => "Delete All",
            arrange         => "Arrange",
            increase        => "",
            decrease        => "",
            rotate45ccw     => "",
            rotate45cw      => "",
            rotate          => "Rotate…",
            changescale     => "Scale…",
            split           => "Split",
            view            => "View/Cut…",
            settings        => "Settings…",
        );
        $self->{btoolbar} = Wx::BoxSizer->new(wxHORIZONTAL);
        for (qw(add remove reset arrange increase decrease rotate45ccw rotate45cw rotate changescale split view settings)) {
            $self->{"btn_$_"} = Wx::Button->new($self, -1, $tbar_buttons{$_}, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            $self->{btoolbar}->Add($self->{"btn_$_"});
        }
    }

    $self->{list} = Wx::ListView->new($self, -1, wxDefaultPosition, wxDefaultSize, wxLC_SINGLE_SEL | wxLC_REPORT | wxBORDER_SUNKEN | wxTAB_TRAVERSAL | wxWANTS_CHARS);
    $self->{list}->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 145);
    $self->{list}->InsertColumn(1, "Copies", wxLIST_FORMAT_CENTER, 45);
    $self->{list}->InsertColumn(2, "Scale", wxLIST_FORMAT_CENTER, wxLIST_AUTOSIZE_USEHEADER);
    EVT_LIST_ITEM_SELECTED($self, $self->{list}, \&list_item_selected);
    EVT_LIST_ITEM_DESELECTED($self, $self->{list}, \&list_item_deselected);
    EVT_LIST_ITEM_ACTIVATED($self, $self->{list}, \&list_item_activated);
    EVT_KEY_DOWN($self->{list}, sub {
        my ($list, $event) = @_;
        if ($event->GetKeyCode == WXK_TAB) {
            $list->Navigate($event->ShiftDown ? &Wx::wxNavigateBackward : &Wx::wxNavigateForward);
        } else {
            $event->Skip;
        }
    });
    
    # right pane buttons
    $self->{btn_export_gcode} = Wx::Button->new($self, -1, "Export G-code…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_export_stl} = Wx::Button->new($self, -1, "Export STL…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_export_gcode}->SetFont($Slic3r::GUI::small_font);
    $self->{btn_export_stl}->SetFont($Slic3r::GUI::small_font);
    
    if ($Slic3r::GUI::have_button_icons) {
        my %icons = qw(
            add             brick_add.png
            remove          brick_delete.png
            reset           cross.png
            arrange         bricks.png
            export_gcode    cog_go.png
            export_stl      brick_go.png
            
            increase        add.png
            decrease        delete.png
            rotate45cw      arrow_rotate_clockwise.png
            rotate45ccw     arrow_rotate_anticlockwise.png
            changescale     arrow_out.png
            split           shape_ungroup.png
            view            package.png
            settings        cog.png
        );
        for (grep $self->{"btn_$_"}, keys %icons) {
            $self->{"btn_$_"}->SetBitmap(Wx::Bitmap->new("$Slic3r::var/$icons{$_}", wxBITMAP_TYPE_PNG));
        }
    }
    $self->selection_changed(0);
    $self->object_list_changed;
    EVT_BUTTON($self, $self->{btn_export_gcode}, sub {
        $self->export_gcode;
        Slic3r::thread_cleanup();
    });
    EVT_BUTTON($self, $self->{btn_export_stl}, \&export_stl);
    
    if ($self->{htoolbar}) {
        EVT_TOOL($self, TB_ADD, \&add);
        EVT_TOOL($self, TB_REMOVE, sub { $self->remove() }); # explicitly pass no argument to remove
        EVT_TOOL($self, TB_RESET, \&reset);
        EVT_TOOL($self, TB_ARRANGE, \&arrange);
        EVT_TOOL($self, TB_MORE, \&increase);
        EVT_TOOL($self, TB_FEWER, \&decrease);
        EVT_TOOL($self, TB_45CW, sub { $_[0]->rotate(-45) });
        EVT_TOOL($self, TB_45CCW, sub { $_[0]->rotate(45) });
        EVT_TOOL($self, TB_SCALE, \&changescale);
        EVT_TOOL($self, TB_SPLIT, \&split_object);
        EVT_TOOL($self, TB_VIEW, sub { $_[0]->object_cut_dialog });
        EVT_TOOL($self, TB_SETTINGS, sub { $_[0]->object_settings_dialog });
    } else {
        EVT_BUTTON($self, $self->{btn_add}, \&add);
        EVT_BUTTON($self, $self->{btn_remove}, sub { $self->remove() }); # explicitly pass no argument to remove
        EVT_BUTTON($self, $self->{btn_reset}, \&reset);
        EVT_BUTTON($self, $self->{btn_arrange}, \&arrange);
        EVT_BUTTON($self, $self->{btn_increase}, \&increase);
        EVT_BUTTON($self, $self->{btn_decrease}, \&decrease);
        EVT_BUTTON($self, $self->{btn_rotate45cw}, sub { $_[0]->rotate(-45) });
        EVT_BUTTON($self, $self->{btn_rotate45ccw}, sub { $_[0]->rotate(45) });
        EVT_BUTTON($self, $self->{btn_changescale}, \&changescale);
        EVT_BUTTON($self, $self->{btn_split}, \&split_object);
        EVT_BUTTON($self, $self->{btn_view}, sub { $_[0]->object_cut_dialog });
        EVT_BUTTON($self, $self->{btn_settings}, sub { $_[0]->object_settings_dialog });
    }
    
    $_->SetDropTarget(Slic3r::GUI::Plater::DropTarget->new($self))
        for $self, $self->{canvas}, $self->{list};
    
    EVT_COMMAND($self, -1, $THUMBNAIL_DONE_EVENT, sub {
        my ($self, $event) = @_;
        my ($obj_idx) = @{$event->GetData};
        return if !$self->{objects}[$obj_idx];  # object was deleted before thumbnail generation completed
        
        $self->on_thumbnail_made($obj_idx);
    });
    
    EVT_COMMAND($self, -1, $PROGRESS_BAR_EVENT, sub {
        my ($self, $event) = @_;
        my ($percent, $message) = @{$event->GetData};
        $self->on_progress_event($percent, $message);
    });
    
    EVT_COMMAND($self, -1, $ERROR_EVENT, sub {
        my ($self, $event) = @_;
        Slic3r::GUI::show_error($self, @{$event->GetData});
    });
    
    EVT_COMMAND($self, -1, $EXPORT_COMPLETED_EVENT, sub {
        my ($self, $event) = @_;
        $self->on_export_completed($event->GetData);
    });
    
    EVT_COMMAND($self, -1, $PROCESS_COMPLETED_EVENT, sub {
        my ($self, $event) = @_;
        $self->on_process_completed($event->GetData);
        Slic3r::thread_cleanup();
    });
    
    EVT_TIMER($self, apply_config_timer_EVENT, sub {
        my ($self, $event) = @_;
        $self->async_apply_config;
    });
    
    $self->{canvas}->update_bed_size;
    $self->update;
    
    {
        my $presets;
        if ($self->GetFrame->{mode} eq 'expert') {
            $presets = Wx::BoxSizer->new(wxVERTICAL);
            my %group_labels = (
                print       => 'Print settings',
                filament    => 'Filament',
                printer     => 'Printer',
            );
            $self->{preset_choosers} = {};
            $self->{preset_choosers_sizers} = {};
            for my $group (qw(print filament printer)) {
                my $text = Wx::StaticText->new($self, -1, "$group_labels{$group}:", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
                $text->SetFont($Slic3r::GUI::small_font);
                my $choice = Wx::Choice->new($self, -1, wxDefaultPosition, [140, -1], []);
                $choice->SetFont($Slic3r::GUI::small_font);
                $self->{preset_choosers}{$group} = [$choice];
                EVT_CHOICE($choice, $choice, sub { $self->on_select_preset($group, @_) });
                
                $self->{preset_choosers_sizers}{$group} = Wx::BoxSizer->new(wxVERTICAL);
                $self->{preset_choosers_sizers}{$group}->Add($choice, 0, wxEXPAND | wxBOTTOM, FILAMENT_CHOOSERS_SPACING);
                
                $presets->Add($text, 0, wxALIGN_LEFT | wxRIGHT, 4);
                $presets->Add($self->{preset_choosers_sizers}{$group}, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, 8);
            }
        }
        
        my $object_info_sizer;
        {
            my $box = Wx::StaticBox->new($self, -1, "Info");
            $object_info_sizer = Wx::StaticBoxSizer->new($box, wxVERTICAL);
            my $grid_sizer = Wx::FlexGridSizer->new(3, 4, 5, 5);
            $grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
            $grid_sizer->AddGrowableCol(1, 1);
            $grid_sizer->AddGrowableCol(3, 1);
            $object_info_sizer->Add($grid_sizer, 0, wxEXPAND);
            
            my @info = (
                size        => "Size",
                volume      => "Volume",
                facets      => "Facets",
                materials   => "Materials",
                manifold    => "Manifold",
            );
            while (my $field = shift @info) {
                my $label = shift @info;
                my $text = Wx::StaticText->new($self, -1, "$label:", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
                $text->SetFont($Slic3r::GUI::small_font);
                $grid_sizer->Add($text, 0);
                
                $self->{"object_info_$field"} = Wx::StaticText->new($self, -1, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
                $self->{"object_info_$field"}->SetFont($Slic3r::GUI::small_font);
                if ($field eq 'manifold') {
                    $self->{object_info_manifold_warning_icon} = Wx::StaticBitmap->new($self, -1, Wx::Bitmap->new("$Slic3r::var/error.png", wxBITMAP_TYPE_PNG));
                    $self->{object_info_manifold_warning_icon}->Hide;
                    
                    my $h_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
                    $h_sizer->Add($self->{object_info_manifold_warning_icon}, 0);
                    $h_sizer->Add($self->{"object_info_$field"}, 0);
                    $grid_sizer->Add($h_sizer, 0, wxEXPAND);
                } else {
                    $grid_sizer->Add($self->{"object_info_$field"}, 0);
                }
            }
        }
        
        my $right_buttons_sizer = Wx::BoxSizer->new(wxVERTICAL);
        $right_buttons_sizer->Add($presets, 0, wxEXPAND, 0) if defined $presets;
        $right_buttons_sizer->Add($self->{btn_export_gcode}, 0, wxEXPAND | wxTOP, 8);
        $right_buttons_sizer->Add($self->{btn_export_stl}, 0, wxEXPAND | wxTOP, 2);
        
        my $right_top_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $right_top_sizer->Add($self->{list}, 1, wxEXPAND | wxLEFT, 5);
        $right_top_sizer->Add($right_buttons_sizer, 0, wxEXPAND | wxALL, 10);
        
        my $right_sizer = Wx::BoxSizer->new(wxVERTICAL);
        $right_sizer->Add($right_top_sizer, 1, wxEXPAND | wxBOTTOM, 10);
        $right_sizer->Add($object_info_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);
        
        my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $hsizer->Add($self->{canvas}, 1, wxEXPAND | wxTOP, 1);
        $hsizer->Add($right_sizer, 0, wxEXPAND | wxBOTTOM, 0);
        
        my $sizer = Wx::BoxSizer->new(wxVERTICAL);
        $sizer->Add($self->{htoolbar}, 0, wxEXPAND, 0) if $self->{htoolbar};
        $sizer->Add($self->{btoolbar}, 0, wxEXPAND, 0) if $self->{btoolbar};
        $sizer->Add($hsizer, 1, wxEXPAND, 0);
        
        $sizer->SetSizeHints($self);
        $self->SetSizer($sizer);
    }
    return $self;
}

sub on_select_preset {
	my $self = shift;
	my ($group, $choice) = @_;
	
	if ($group eq 'filament' && @{$self->{preset_choosers}{filament}} > 1) {
		my @filament_presets = $self->filament_presets;
		$Slic3r::GUI::Settings->{presets}{filament} = $choice->GetString($filament_presets[0]) . ".ini";
		$Slic3r::GUI::Settings->{presets}{"filament_${_}"} = $choice->GetString($filament_presets[$_])
			for 1 .. $#filament_presets;
		wxTheApp->save_settings;
		return;
	}
	$self->GetFrame->{options_tabs}{$group}->select_preset($choice->GetSelection);
}

sub GetFrame {
    my ($self) = @_;
    return &Wx::GetTopLevelParent($self);
}

sub update_presets {
    my $self = shift;
    my ($group, $items, $selected) = @_;
    
    foreach my $choice (@{ $self->{preset_choosers}{$group} }) {
        my $sel = $choice->GetSelection;
        $choice->Clear;
        $choice->Append($_) for @$items;
        $choice->SetSelection($sel) if $sel <= $#$items;
    }
    $self->{preset_choosers}{$group}[0]->SetSelection($selected);
}

sub filament_presets {
    my $self = shift;
    
    return map $_->GetSelection, @{ $self->{preset_choosers}{filament} };
}

sub add {
    my $self = shift;
    
    my @input_files = wxTheApp->open_model($self);
    $self->load_file($_) for @input_files;
}

sub load_file {
    my $self = shift;
    my ($input_file) = @_;
    
    $Slic3r::GUI::Settings->{recent}{skein_directory} = dirname($input_file);
    wxTheApp->save_settings;
    
    my $process_dialog = Wx::ProgressDialog->new('Loading…', "Processing input file…", 100, $self, 0);
    $process_dialog->Pulse;
    
    local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
    
    my $model = eval { Slic3r::Model->read_from_file($input_file) };
    Slic3r::GUI::show_error($self, $@) if $@;
    
    if (defined $model) {
        $self->load_model_objects(@{$model->objects});
        $self->statusbar->SetStatusText("Loaded " . basename($input_file));
    }
    
    $process_dialog->Destroy;
}

sub load_model_objects {
    my ($self, @model_objects) = @_;
    
    my $need_arrange = 0;
    my @obj_idx = ();
    foreach my $model_object (@model_objects) {
        my $o = $self->{model}->add_object($model_object);
        
        push @{ $self->{objects} }, Slic3r::GUI::Plater::Object->new(
            name => basename($model_object->input_file),
        );
        push @obj_idx, $#{ $self->{objects} };
    
        if ($model_object->instances_count == 0) {
            # if object has no defined position(s) we need to rearrange everything after loading
            $need_arrange = 1;
        
            # add a default instance and center object around origin
            $o->center_around_origin;
            $o->add_instance(offset => Slic3r::Pointf->new(@{$self->{config}->print_center}));
        }
    
        $self->{print}->auto_assign_extruders($o);
        $self->{print}->add_model_object($o);
    }
    
    # if user turned autocentering off, automatic arranging would disappoint them
    if (!$Slic3r::GUI::Settings->{_}{autocenter}) {
        $need_arrange = 0;
    }
    
    $self->objects_loaded(\@obj_idx, no_arrange => !$need_arrange);
}

sub objects_loaded {
    my $self = shift;
    my ($obj_idxs, %params) = @_;
    
    foreach my $obj_idx (@$obj_idxs) {
        my $object = $self->{objects}[$obj_idx];
        my $model_object = $self->{model}->objects->[$obj_idx];
        $self->{list}->InsertStringItem($obj_idx, $object->name);
        $self->{list}->SetItemFont($obj_idx, Wx::Font->new(10, wxDEFAULT, wxNORMAL, wxNORMAL))
            if $self->{list}->can('SetItemFont');  # legacy code for wxPerl < 0.9918 not supporting SetItemFont()
    
        $self->{list}->SetItem($obj_idx, 1, $model_object->instances_count);
        $self->{list}->SetItem($obj_idx, 2, ($model_object->instances->[0]->scaling_factor * 100) . "%");
    
        $self->make_thumbnail($obj_idx);
    }
    $self->arrange unless $params{no_arrange};
    $self->update;
    $self->{list}->Update;
    $self->{list}->Select($obj_idxs->[-1], 1);
    $self->object_list_changed;
    
    $self->schedule_background_process;
}

sub remove {
    my $self = shift;
    my ($obj_idx) = @_;
    
    $self->stop_background_process;
    
    # if no object index is supplied, remove the selected one
    if (!defined $obj_idx) {
        ($obj_idx, undef) = $self->selected_object;
    }
    
    splice @{$self->{objects}}, $obj_idx, 1;
    $self->{model}->delete_object($obj_idx);
    $self->{print}->delete_object($obj_idx);
    $self->{list}->DeleteItem($obj_idx);
    $self->object_list_changed;
    
    $self->select_object(undef);
    $self->update;
    $self->{canvas}->Refresh;
    
    $self->schedule_background_process;
}

sub reset {
    my $self = shift;
    
    $self->stop_background_process;
    
    @{$self->{objects}} = ();
    $self->{model}->clear_objects;
    $self->{print}->clear_objects;
    $self->{list}->DeleteAllItems;
    $self->object_list_changed;
    
    $self->select_object(undef);
    $self->{canvas}->Refresh;
}

sub increase {
    my $self = shift;
    
    my ($obj_idx, $object) = $self->selected_object;
    my $model_object = $self->{model}->objects->[$obj_idx];
    my $last_instance = $model_object->instances->[-1];
    my $i = $model_object->add_instance(
        offset          => Slic3r::Pointf->new(map 10+$_, @{$last_instance->offset}),
        scaling_factor  => $last_instance->scaling_factor,
        rotation        => $last_instance->rotation,
    );
    $self->{print}->objects->[$obj_idx]->add_copy(@{$i->offset});
    $self->{list}->SetItem($obj_idx, 1, $model_object->instances_count);
    
    # only autoarrange if user has autocentering enabled
    if ($Slic3r::GUI::Settings->{_}{autocenter}) {
        $self->arrange;
    } else {
        $self->{canvas}->Refresh;
    }
}

sub decrease {
    my $self = shift;
    
    my ($obj_idx, $object) = $self->selected_object;
    my $model_object = $self->{model}->objects->[$obj_idx];
    if ($model_object->instances_count >= 2) {
        $model_object->delete_last_instance;
        $self->{print}->objects->[$obj_idx]->delete_last_copy;
        $self->{list}->SetItem($obj_idx, 1, $model_object->instances_count);
    } else {
        $self->remove;
    }
    
    if ($self->{objects}[$obj_idx]) {
        $self->{list}->Select($obj_idx, 0);
        $self->{list}->Select($obj_idx, 1);
    }
    $self->update;
    $self->{canvas}->Refresh;
}

sub rotate {
    my $self = shift;
    my ($angle, $axis) = @_;
    
    $axis //= Z;
    
    my ($obj_idx, $object) = $self->selected_object;
    return if !defined $obj_idx;
    
    my $model_object = $self->{model}->objects->[$obj_idx];
    my $model_instance = $model_object->instances->[0];
    
    # we need thumbnail to be computed before allowing rotation
    return if !$object->thumbnail;
    
    if (!defined $angle) {
        my $axis_name = $axis == X ? 'X' : $axis == Y ? 'Y' : 'Z';
        $angle = Wx::GetNumberFromUser("", "Enter the rotation angle:", "Rotate around $axis_name axis", $model_instance->rotation, -364, 364, $self);
        return if !$angle || $angle == -1;
        $angle = 0 - $angle;  # rotate clockwise (be consistent with button icon)
    }
    
    {
        if ($axis == Z) {
            my $new_angle = $model_instance->rotation + $angle;
            $_->set_rotation($new_angle) for @{ $model_object->instances };
            $object->transform_thumbnail($self->{model}, $obj_idx);
        } else {
            # rotation around X and Y needs to be performed on mesh
            # so we first apply any Z rotation
            if ($model_instance->rotation != 0) {
                $model_object->rotate(deg2rad($model_instance->rotation), Z);
                $_->set_rotation(0) for @{ $model_object->instances };
            }
            $model_object->rotate(deg2rad($angle), $axis);
            $self->make_thumbnail($obj_idx);
        }
        $model_object->update_bounding_box;
        
        # update print and start background processing
        $self->stop_background_process;
        $self->{print}->add_model_object($model_object, $obj_idx);
        $self->schedule_background_process;
        
    }
    $self->selection_changed;  # refresh info (size etc.)
    $self->update;
    $self->{canvas}->Refresh;
}

sub flip {
    my ($self, $axis) = @_;
    
    my ($obj_idx, $object) = $self->selected_object;
    return if !defined $obj_idx;
    
    my $model_object = $self->{model}->objects->[$obj_idx];
    my $model_instance = $model_object->instances->[0];
    
    # apply Z rotation before flipping
    if ($model_instance->rotation != 0) {
        $model_object->rotate(deg2rad($model_instance->rotation), Z);
        $_->set_rotation(0) for @{ $model_object->instances };
    }
    
    $model_object->flip($axis);
    $model_object->update_bounding_box;
    $self->make_thumbnail($obj_idx);
        
    # update print and start background processing
    $self->stop_background_process;
    $self->{print}->add_model_object($model_object, $obj_idx);
    $self->schedule_background_process;
    
    $self->selection_changed;  # refresh info (size etc.)
    $self->update;
    $self->{canvas}->Refresh;
}

sub changescale {
    my $self = shift;
    
    my ($obj_idx, $object) = $self->selected_object;
    my $model_object = $self->{model}->objects->[$obj_idx];
    my $model_instance = $model_object->instances->[0];
    
    # we need thumbnail to be computed before allowing scaling
    return if !$object->thumbnail;
    
    # max scale factor should be above 2540 to allow importing files exported in inches
    my $scale = Wx::GetNumberFromUser("", "Enter the scale % for the selected object:", "Scale", $model_instance->scaling_factor*100, 0, 100000, $self);
    return if !$scale || $scale == -1;
    
    $self->{list}->SetItem($obj_idx, 2, "$scale%");
    $scale /= 100;  # turn percent into factor
    {
        my $variation = $scale / $model_instance->scaling_factor;
        foreach my $range (@{ $model_object->layer_height_ranges }) {
            $range->[0] *= $variation;
            $range->[1] *= $variation;
        }
        $_->set_scaling_factor($scale) for @{ $model_object->instances };
        $model_object->update_bounding_box;
        
        # update print and start background processing
        $self->stop_background_process;
        $self->{print}->add_model_object($model_object, $obj_idx);
        $self->schedule_background_process;
        
        $object->transform_thumbnail($self->{model}, $obj_idx);
    }
    $self->selection_changed(1);  # refresh info (size, volume etc.)
    $self->update;
    $self->{canvas}->Refresh;
}

sub arrange {
    my $self = shift;
    
    my $bb = Slic3r::Polygon->new_scale(@{$self->{config}->bed_shape})->bounding_box;
    eval {
        $self->{model}->arrange_objects($self->GetFrame->config->min_object_distance, $bb);
    };
    # ignore arrange failures on purpose: user has visual feedback and we don't need to warn him
    # when parts don't fit in print bed
    
    $self->update(1);
    $self->{canvas}->Refresh;
}

sub split_object {
    my $self = shift;
    
    my ($obj_idx, $current_object)  = $self->selected_object;
    my $current_model_object        = $self->{model}->objects->[$obj_idx];
    
    if (@{$current_model_object->volumes} > 1) {
        Slic3r::GUI::warning_catcher($self)->("The selected object couldn't be split because it contains more than one volume/material.");
        return;
    }
    
    my @new_meshes = @{$current_model_object->volumes->[0]->mesh->split};
    if (@new_meshes == 1) {
        Slic3r::GUI::warning_catcher($self)->("The selected object couldn't be split because it already contains a single part.");
        return;
    }
    
    $self->stop_background_process;
    
    # create a bogus Model object, we only need to instantiate the new Model::Object objects
    my $new_model = Slic3r::Model->new;
    
    my @model_objects = ();
    foreach my $mesh (@new_meshes) {
        $mesh->repair;
        
        my $model_object = $new_model->add_object(
            input_file              => $current_model_object->input_file,
            config                  => $current_model_object->config->clone,
            layer_height_ranges     => $current_model_object->layer_height_ranges,  # TODO: clone this
        );
        $model_object->add_volume(
            mesh        => $mesh,
            material_id => $current_model_object->volumes->[0]->material_id,
        );
        
        for my $instance_idx (0..$#{ $current_model_object->instances }) {
            my $current_instance = $current_model_object->instances->[$instance_idx];
            $model_object->add_instance(
                offset          => Slic3r::Pointf->new(
                    $current_instance->offset->[X] + ($instance_idx * 10),
                    $current_instance->offset->[Y] + ($instance_idx * 10),
                ),
                rotation        => $current_instance->rotation,
                scaling_factor  => $current_instance->scaling_factor,
            );
        }
        # we need to center this single object around origin
        $model_object->center_around_origin;
        push @model_objects, $model_object;
    }

    # remove the original object before spawning the object_loaded event, otherwise 
    # we'll pass the wrong $obj_idx to it (which won't be recognized after the
    # thumbnail thread returns)
    $self->remove($obj_idx);
    $current_object = $obj_idx = undef;
    
    # load all model objects at once, otherwise the plate would be rearranged after each one
    # causing original positions not to be kept
    $self->load_model_objects(@model_objects);
}

sub schedule_background_process {
    my ($self) = @_;
    $self->{apply_config_timer}->Start(PROCESS_DELAY, 1);  # 1 = one shot
}

sub async_apply_config {
    my ($self) = @_;
    
    # pause process thread before applying new config
    # since we don't want to touch data that is being used by the threads
    $self->suspend_background_process;
    
    # apply new config
    my $invalidated = $self->{print}->apply_config($self->GetFrame->config);
    
    return if !$Slic3r::GUI::Settings->{_}{background_processing};
    
    if ($invalidated) {
        # kill current thread if any
        $self->stop_background_process;
    } else {
        # TODO: restore process thread
    }
    
    # schedule a new process thread in case it wasn't running
    $self->start_background_process;
}

sub start_background_process {
    my ($self) = @_;
    
    $self->resume_background_process;
    
    return if !$Slic3r::have_threads;
    return if !@{$self->{objects}};
    return if $self->{process_thread};
    
    # It looks like declaring a local $SIG{__WARN__} prevents the ugly
    # "Attempt to free unreferenced scalar" warning...
    local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
    
    # don't start process thread if config is not valid
    eval {
        # this will throw errors if config is not valid
        $self->GetFrame->config->validate;
        $self->{print}->validate;
    };
    return if $@;
    
    # apply extra variables
    {
        my $extra = $self->GetFrame->extra_variables;
        $self->{print}->placeholder_parser->set($_, $extra->{$_}) for keys %$extra;
    }
    
    # start thread
    @_ = ();
    $self->{process_thread} = threads->create(sub {
        local $SIG{'KILL'} = sub {
            Slic3r::debugf "Background process cancelled; exiting thread...\n";
            Slic3r::thread_cleanup();
            threads->exit();
        };
        local $SIG{'STOP'} = sub {
            $sema->down;
            $sema->up;
        };
        
        eval {
            $self->{print}->process;
        };
        if ($@) {
            Slic3r::debugf "Discarding background process error: $@\n";
            Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $PROCESS_COMPLETED_EVENT, 0));
        } else {
            Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $PROCESS_COMPLETED_EVENT, 1));
        }
        Slic3r::thread_cleanup();
    });
    Slic3r::debugf "Background processing started.\n";
}

sub stop_background_process {
    my ($self) = @_;
    
    $self->{apply_config_timer}->Stop;
    $self->statusbar->SetCancelCallback(undef);
    $self->statusbar->StopBusy;
    $self->statusbar->SetStatusText("");
    
    if ($self->{process_thread}) {
        Slic3r::debugf "Killing background process.\n";
        $self->{process_thread}->kill('KILL')->join;
        $self->{process_thread} = undef;
    } else {
        Slic3r::debugf "No background process running.\n";
    }
    
    # if there's an export process, kill that one as well
    if ($self->{export_thread}) {
        Slic3r::debugf "Killing background export process.\n";
        $self->{export_thread}->kill('KILL')->join;
        $self->{export_thread} = undef;
    }
}

sub suspend_background_process {
    my ($self) = @_;
    
    $sema->down;
    $_->kill('STOP') for grep $_, $self->{process_thread}, $self->{export_thread};
}

sub resume_background_process {
    my ($self) = @_;
    $sema->up;
}

sub export_gcode {
    my $self = shift;
    
    return if !@{$self->{objects}};
    
    if ($self->{export_gcode_output_file}) {
        Wx::MessageDialog->new($self, "Another export job is currently running.", 'Error', wxOK | wxICON_ERROR)->ShowModal;
        return;
    }
    
    # if process is not running, validate config
    # (we assume that if it is running, config is valid)
    eval {
        # this will throw errors if config is not valid
        $self->GetFrame->config->validate;
        $self->{print}->validate;
    };
    Slic3r::GUI::catch_error($self) and return;
    
    
    # apply config and validate print
    my $config = $self->GetFrame->config;
    eval {
        # this will throw errors if config is not valid
        $config->validate;
        $self->{print}->apply_config($config);
        $self->{print}->validate;
    };
    if (!$Slic3r::have_threads) {
        Slic3r::GUI::catch_error($self) and return;
    }
    
    # select output file
    $self->{export_gcode_output_file} = $main::opt{output};
    {
        my $default_output_file = $self->{print}->expanded_output_filepath($self->{export_gcode_output_file});
        my $dlg = Wx::FileDialog->new($self, 'Save G-code file as:', wxTheApp->output_path(dirname($default_output_file)),
            basename($default_output_file), &Slic3r::GUI::FILE_WILDCARDS->{gcode}, wxFD_SAVE);
        if ($dlg->ShowModal != wxID_OK) {
            $dlg->Destroy;
            $self->{export_gcode_output_file} = undef;
            return;
        }
        $Slic3r::GUI::Settings->{_}{last_output_path} = dirname($dlg->GetPath);
        wxTheApp->save_settings;
        $self->{export_gcode_output_file} = $Slic3r::GUI::MainFrame::last_output_file = $dlg->GetPath;
        $dlg->Destroy;
    }
    
    $self->statusbar->StartBusy;
    
    if ($Slic3r::have_threads) {
        $self->statusbar->SetCancelCallback(sub {
            $self->stop_background_process;
            $self->statusbar->SetStatusText("Export cancelled");
        });
        
        # start background process, whose completion event handler
        # will detect $self->{export_gcode_output_file} and proceed with export
        $self->start_background_process;
    } else {
        eval {
            $self->{print}->process;
            $self->{print}->export_gcode(output_file => $self->{export_gcode_output_file});
        };
        my $result = !Slic3r::GUI::catch_error($self);
        $self->on_export_completed($result);
    }
}

# This gets called only if we have threads.
sub on_process_completed {
    my ($self, $result) = @_;
    
    $self->statusbar->SetCancelCallback(undef);
    $self->statusbar->StopBusy;
    $self->statusbar->SetStatusText("");
    
    Slic3r::debugf "Background processing completed.\n";
    $self->{process_thread}->detach if $self->{process_thread};
    $self->{process_thread} = undef;
    
    return if !$result;
    
    # if we have an export filename, start a new thread for exporting G-code
    if ($self->{export_gcode_output_file}) {
        @_ = ();
        
        # workaround for "Attempt to free un referenced scalar..."
        our $_thread_self = $self;
        
        $self->{export_thread} = threads->create(sub {
            local $SIG{'KILL'} = sub {
                Slic3r::debugf "Export process cancelled; exiting thread...\n";
                Slic3r::thread_cleanup();
                threads->exit();
            };
            local $SIG{'STOP'} = sub {
                $sema->down;
                $sema->up;
            };
        
            eval {
                $_thread_self->{print}->export_gcode(output_file => $_thread_self->{export_gcode_output_file});
            };
            if ($@) {
                Wx::PostEvent($_thread_self, Wx::PlThreadEvent->new(-1, $ERROR_EVENT, shared_clone([ $@ ])));
                Wx::PostEvent($_thread_self, Wx::PlThreadEvent->new(-1, $EXPORT_COMPLETED_EVENT, 0));
            } else {
                Wx::PostEvent($_thread_self, Wx::PlThreadEvent->new(-1, $EXPORT_COMPLETED_EVENT, 1));
            }
            Slic3r::thread_cleanup();
        });
        Slic3r::debugf "Background G-code export started.\n";
    }
}

# This gets called also if we have no threads.
sub on_progress_event {
    my ($self, $percent, $message) = @_;
    
    $self->statusbar->SetProgress($percent);
    $self->statusbar->SetStatusText("$message…");
}

# This gets called also if we don't have threads.
sub on_export_completed {
    my ($self, $result) = @_;
    
    $self->statusbar->SetCancelCallback(undef);
    $self->statusbar->StopBusy;
    $self->statusbar->SetStatusText("");
    
    Slic3r::debugf "Background export process completed.\n";
    $self->{export_thread}->detach if $self->{export_thread};
    $self->{export_thread} = undef;
    
    my $message;
    if ($result) {
        $message = "G-code file exported to " . $self->{export_gcode_output_file};
    } else {
        $message = "Export failed";
    }
    $self->{export_gcode_output_file} = undef;
    $self->statusbar->SetStatusText($message);
    wxTheApp->notify($message);
}

sub export_stl {
    my $self = shift;
    
    return if !@{$self->{objects}};
        
    my $output_file = $self->_get_export_file('STL') or return;
    Slic3r::Format::STL->write_file($output_file, $self->{model}, binary => 1);
    $self->statusbar->SetStatusText("STL file exported to $output_file");
    
    # this method gets executed in a separate thread by wxWidgets since it's a button handler
    Slic3r::thread_cleanup() if $Slic3r::have_threads;
}

sub export_amf {
    my $self = shift;
    
    return if !@{$self->{objects}};
        
    my $output_file = $self->_get_export_file('AMF') or return;
    Slic3r::Format::AMF->write_file($output_file, $self->{model});
    $self->statusbar->SetStatusText("AMF file exported to $output_file");
    
    # this method gets executed in a separate thread by wxWidgets since it's a menu handler
    Slic3r::thread_cleanup() if $Slic3r::have_threads;
}

sub _get_export_file {
    my $self = shift;
    my ($format) = @_;
    
    my $suffix = $format eq 'STL' ? '.stl' : '.amf.xml';
    
    my $output_file = $main::opt{output};
    {
        $output_file = $self->{print}->expanded_output_filepath($output_file);
        $output_file =~ s/\.gcode$/$suffix/i;
        my $dlg = Wx::FileDialog->new($self, "Save $format file as:", dirname($output_file),
            basename($output_file), &Slic3r::GUI::MODEL_WILDCARD, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if ($dlg->ShowModal != wxID_OK) {
            $dlg->Destroy;
            return undef;
        }
        $output_file = $Slic3r::GUI::MainFrame::last_output_file = $dlg->GetPath;
        $dlg->Destroy;
    }
    return $output_file;
}

sub make_thumbnail {
    my $self = shift;
    my ($obj_idx) = @_;
    
    my $plater_object = $self->{objects}[$obj_idx];
    $plater_object->thumbnail(Slic3r::ExPolygon::Collection->new);
    my $cb = sub {
        $plater_object->make_thumbnail($self->{model}, $obj_idx);
        
        if ($Slic3r::have_threads) {
            Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $THUMBNAIL_DONE_EVENT, shared_clone([ $obj_idx ])));
            Slic3r::thread_cleanup();
            threads->exit;
        } else {
            $self->on_thumbnail_made($obj_idx);
        }
    };
    
    @_ = ();
    $Slic3r::have_threads
        ? threads->create(sub { $cb->(); Slic3r::thread_cleanup(); })->detach
        : $cb->();
}

sub on_thumbnail_made {
    my $self = shift;
    my ($obj_idx) = @_;
    
    $self->{objects}[$obj_idx]->transform_thumbnail($self->{model}, $obj_idx);
    $self->update;
    $self->{canvas}->Refresh;
}

# this method gets called whenever print center is changed or the objects' bounding box changes
# (i.e. when an object is added/removed/moved/rotated/scaled)
sub update {
    my ($self, $force_autocenter) = @_;
    
    if ($Slic3r::GUI::Settings->{_}{autocenter} || $force_autocenter) {
        $self->{model}->center_instances_around_point($self->{config}->print_center);
    }
    
    # sync model and print object instances
    for my $obj_idx (0..$#{$self->{objects}}) {
        my $model_object = $self->{model}->objects->[$obj_idx];
        my $print_object = $self->{print}->objects->[$obj_idx];
        
        $print_object->delete_all_copies;
        $print_object->add_copy(@{$_->offset}) for @{$model_object->instances};
    }
    
    $self->{canvas}->Refresh;
}

sub on_config_change {
    my $self = shift;
    my ($opt_key, $value) = @_;
    
    if ($opt_key eq 'extruders_count' && defined $value) {
        my $choices = $self->{preset_choosers}{filament};
        while (@$choices < $value) {
        	my @presets = $choices->[0]->GetStrings;
            push @$choices, Wx::Choice->new($self, -1, wxDefaultPosition, [150, -1], [@presets]);
            $choices->[-1]->SetFont($Slic3r::GUI::small_font);
            $self->{preset_choosers_sizers}{filament}->Add($choices->[-1], 0, wxEXPAND | wxBOTTOM, FILAMENT_CHOOSERS_SPACING);
            EVT_CHOICE($choices->[-1], $choices->[-1], sub { $self->on_select_preset('filament', @_) });
            my $i = first { $choices->[-1]->GetString($_) eq ($Slic3r::GUI::Settings->{presets}{"filament_" . $#$choices} || '') } 0 .. $#presets;
        	$choices->[-1]->SetSelection($i || 0);
        }
        while (@$choices > $value) {
            $self->{preset_choosers_sizers}{filament}->Remove(-1);
            $choices->[-1]->Destroy;
            pop @$choices;
        }
        $self->Layout;
    } elsif ($self->{config}->has($opt_key)) {
        $self->{config}->set($opt_key, $value);
        if ($opt_key eq 'bed_size') {
            $self->{canvas}->update_bed_size;
            $self->update;
        }
        $self->update if $opt_key eq 'print_center';
    }
    
    return if !$self->GetFrame->is_loaded;
    
    # (re)start timer
    $self->schedule_background_process;
}

sub list_item_deselected {
    my ($self, $event) = @_;
    return if $PreventListEvents;
    
    if ($self->{list}->GetFirstSelected == -1) {
        $self->select_object(undef);
        $self->{canvas}->Refresh;
    }
}

sub list_item_selected {
    my ($self, $event) = @_;
    return if $PreventListEvents;
    
    my $obj_idx = $event->GetIndex;
    $self->select_object($obj_idx);
    $self->{canvas}->Refresh;
}

sub list_item_activated {
    my ($self, $event, $obj_idx) = @_;
    
    $obj_idx //= $event->GetIndex;
	$self->object_cut_dialog($obj_idx);
}

sub object_cut_dialog {
    my $self = shift;
    my ($obj_idx) = @_;
    
    if (!defined $obj_idx) {
        ($obj_idx, undef) = $self->selected_object;
    }
    
    if (!$Slic3r::GUI::have_OpenGL) {
        Slic3r::GUI::show_error($self, "Please install the OpenGL modules to use this feature (see build instructions).");
        return;
    }
    
    my $dlg = Slic3r::GUI::Plater::ObjectCutDialog->new($self,
		object              => $self->{objects}[$obj_idx],
		model_object        => $self->{model}->objects->[$obj_idx],
	);
	$dlg->ShowModal;
	
	if (my @new_objects = $dlg->NewModelObjects) {
	    $self->remove($obj_idx);
	    $self->load_model_objects(grep defined($_), @new_objects);
	    $self->arrange;
	}
}

sub object_settings_dialog {
    my $self = shift;
    my ($obj_idx) = @_;
    
    if (!defined $obj_idx) {
        ($obj_idx, undef) = $self->selected_object;
    }
    my $model_object = $self->{model}->objects->[$obj_idx];
    
    # validate config before opening the settings dialog because
    # that dialog can't be closed if validation fails, but user
    # can't fix any error which is outside that dialog
    return unless $self->validate_config;
    
    my $dlg = Slic3r::GUI::Plater::ObjectSettingsDialog->new($self,
		object          => $self->{objects}[$obj_idx],
		model_object    => $model_object,
	);
	$dlg->ShowModal;
	
	# update thumbnail since parts may have changed
	if ($dlg->PartsChanged) {
    	$self->make_thumbnail($obj_idx);
	}
	
	# update print
	if ($dlg->PartsChanged || $dlg->PartSettingsChanged) {
        $self->{print}->reload_object($obj_idx);
        $self->schedule_background_process;
    }
}

sub object_list_changed {
    my $self = shift;
    
    my $have_objects = @{$self->{objects}} ? 1 : 0;
    my $method = $have_objects ? 'Enable' : 'Disable';
    $self->{"btn_$_"}->$method
        for grep $self->{"btn_$_"}, qw(reset arrange export_gcode export_stl);
    
    if ($self->{htoolbar}) {
        $self->{htoolbar}->EnableTool($_, $have_objects)
            for (TB_RESET, TB_ARRANGE);
    }
}

sub selection_changed {
    my $self = shift;
    
    my ($obj_idx, $object) = $self->selected_object;
    my $have_sel = defined $obj_idx;
    
    my $method = $have_sel ? 'Enable' : 'Disable';
    $self->{"btn_$_"}->$method
        for grep $self->{"btn_$_"}, qw(remove increase decrease rotate45cw rotate45ccw changescale split view settings);
    
    if ($self->{htoolbar}) {
        $self->{htoolbar}->EnableTool($_, $have_sel)
            for (TB_REMOVE, TB_MORE, TB_FEWER, TB_45CW, TB_45CCW, TB_SCALE, TB_SPLIT, TB_VIEW, TB_SETTINGS);
    }
    
    if ($self->{object_info_size}) { # have we already loaded the info pane?
        if ($have_sel) {
            my $model_object = $self->{model}->objects->[$obj_idx];
            my $model_instance = $model_object->instances->[0];
            $self->{object_info_size}->SetLabel(sprintf("%.2f x %.2f x %.2f", @{$model_object->instance_bounding_box(0)->size}));
            $self->{object_info_materials}->SetLabel($model_object->materials_count);
            
            if (my $stats = $model_object->mesh_stats) {
                $self->{object_info_volume}->SetLabel(sprintf('%.2f', $stats->{volume} * ($model_instance->scaling_factor**3)));
                $self->{object_info_facets}->SetLabel(sprintf('%d (%d shells)', $model_object->facets_count, $stats->{number_of_parts}));
                if (my $errors = sum(@$stats{qw(degenerate_facets edges_fixed facets_removed facets_added facets_reversed backwards_edges)})) {
                    $self->{object_info_manifold}->SetLabel(sprintf("Auto-repaired (%d errors)", $errors));
                    $self->{object_info_manifold_warning_icon}->Show;
                    
                    # we don't show normals_fixed because we never provide normals
	                # to admesh, so it generates normals for all facets
                    my $message = sprintf '%d degenerate facets, %d edges fixed, %d facets removed, %d facets added, %d facets reversed, %d backwards edges',
                        @$stats{qw(degenerate_facets edges_fixed facets_removed facets_added facets_reversed backwards_edges)};
                    $self->{object_info_manifold}->SetToolTipString($message);
                    $self->{object_info_manifold_warning_icon}->SetToolTipString($message);
                } else {
                    $self->{object_info_manifold}->SetLabel("Yes");
                }
            } else {
                $self->{object_info_facets}->SetLabel($object->facets);
            }
        } else {
            $self->{"object_info_$_"}->SetLabel("") for qw(size volume facets materials manifold);
            $self->{object_info_manifold_warning_icon}->Hide;
            $self->{object_info_manifold}->SetToolTipString("");
        }
        $self->Layout;
    }
    
    # prepagate the event to the frame (a custom Wx event would be cleaner)
    $self->GetFrame->on_plater_selection_changed($have_sel);
}

sub select_object {
    my ($self, $obj_idx) = @_;
    
    $_->selected(0) for @{ $self->{objects} };
    if (defined $obj_idx) {
        $self->{objects}->[$obj_idx]->selected(1);
        
        # We use this flag to avoid circular event handling
        # Select() happens to fire a wxEVT_LIST_ITEM_SELECTED on Windows, 
        # whose event handler calls this method again and again and again
        $PreventListEvents = 1;
        $self->{list}->Select($obj_idx, 1);
        $PreventListEvents = 0;
    } else {
        # TODO: deselect all in list
    }
    $self->selection_changed(1);
}

sub selected_object {
    my $self = shift;
    
    my $obj_idx = first { $self->{objects}[$_]->selected } 0..$#{ $self->{objects} };
    return undef if !defined $obj_idx;
    return ($obj_idx, $self->{objects}[$obj_idx]),
}

sub validate_config {
    my $self = shift;
    
    eval {
        $self->GetFrame->config->validate;
    };
    return 0 if Slic3r::GUI::catch_error($self);    
    return 1;
}

sub statusbar {
    my $self = shift;
    return $self->GetFrame->{statusbar};
}

sub object_menu {
    my ($self) = @_;
    
    my $frame = $self->GetFrame;
    my $menu = Wx::Menu->new;
    $frame->_append_menu_item($menu, "Delete\tCtrl+Del", 'Remove the selected object', sub {
        $self->remove;
    });
    $frame->_append_menu_item($menu, "Increase copies\tCtrl++", 'Place one more copy of the selected object', sub {
        $self->increase;
    });
    $frame->_append_menu_item($menu, "Decrease copies\tCtrl+-", 'Remove one copy of the selected object', sub {
        $self->decrease;
    });
    $menu->AppendSeparator();
    $frame->_append_menu_item($menu, "Rotate 45° clockwise", 'Rotate the selected object by 45° clockwise', sub {
        $self->rotate(-45);
    });
    $frame->_append_menu_item($menu, "Rotate 45° counter-clockwise", 'Rotate the selected object by 45° counter-clockwise', sub {
        $self->rotate(+45);
    });
    
    my $rotateMenu = Wx::Menu->new;
    $menu->AppendSubMenu($rotateMenu, "Rotate…", 'Rotate the selected object by an arbitrary angle');
    $frame->_append_menu_item($rotateMenu, "Around X axis…", 'Rotate the selected object by an arbitrary angle around X axis', sub {
        $self->rotate(undef, X);
    });
    $frame->_append_menu_item($rotateMenu, "Around Y axis…", 'Rotate the selected object by an arbitrary angle around Y axis', sub {
        $self->rotate(undef, Y);
    });
    $frame->_append_menu_item($rotateMenu, "Around Z axis…", 'Rotate the selected object by an arbitrary angle around Z axis', sub {
        $self->rotate(undef, Z);
    });
    
    my $flipMenu = Wx::Menu->new;
    $menu->AppendSubMenu($flipMenu, "Flip…", 'Mirror the selected object');
    $frame->_append_menu_item($flipMenu, "Along X axis…", 'Mirror the selected object along the X axis', sub {
        $self->flip(X);
    });
    $frame->_append_menu_item($flipMenu, "Along Y axis…", 'Mirror the selected object along the Y axis', sub {
        $self->flip(Y);
    });
    $frame->_append_menu_item($flipMenu, "Along Z axis…", 'Mirror the selected object along the Z axis', sub {
        $self->flip(Z);
    });
    
    $frame->_append_menu_item($menu, "Scale…", 'Scale the selected object by an arbitrary factor', sub {
        $self->changescale;
    });
    $frame->_append_menu_item($menu, "Split", 'Split the selected object into individual parts', sub {
        $self->split_object;
    });
    $frame->_append_menu_item($menu, "View/Cut…", 'Open the 3D cutting tool', sub {
        $self->object_cut_dialog;
    });
    $menu->AppendSeparator();
    $frame->_append_menu_item($menu, "Settings…", 'Open the object editor dialog', sub {
        $self->object_settings_dialog;
    });
    
    return $menu;
}

package Slic3r::GUI::Plater::DropTarget;
use Wx::DND;
use base 'Wx::FileDropTarget';

sub new {
    my $class = shift;
    my ($window) = @_;
    my $self = $class->SUPER::new;
    $self->{window} = $window;
    return $self;
}

sub OnDropFiles {
    my $self = shift;
    my ($x, $y, $filenames) = @_;
    
    # stop scalars leaking on older perl
    # https://rt.perl.org/rt3/Public/Bug/Display.html?id=70602
    @_ = ();
    
    # only accept STL, OBJ and AMF files
    return 0 if grep !/\.(?:stl|obj|amf(?:\.xml)?)$/i, @$filenames;
    
    $self->{window}->load_file($_) for @$filenames;
}

package Slic3r::GUI::Plater::Object;
use Moo;

use List::Util qw(first);
use Slic3r::Geometry qw(X Y Z MIN MAX deg2rad);

has 'name'                  => (is => 'rw', required => 1);
has 'thumbnail'             => (is => 'rw'); # ExPolygon::Collection in scaled model units with no transforms
has 'transformed_thumbnail' => (is => 'rw');
has 'instance_thumbnails'   => (is => 'ro', default => sub { [] });  # array of ExPolygon::Collection objects, each one representing the actual placed thumbnail of each instance in pixel units
has 'selected'              => (is => 'rw', default => sub { 0 });

sub make_thumbnail {
    my ($self, $model, $obj_idx) = @_;
    
    # make method idempotent
    $self->thumbnail->clear;
    
    my $mesh = $model->objects->[$obj_idx]->raw_mesh;
    if ($mesh->facets_count <= 5000) {
        # remove polygons with area <= 1mm
        my $area_threshold = Slic3r::Geometry::scale 1;
        $self->thumbnail->append(
            grep $_->area >= $area_threshold,
            @{ $mesh->horizontal_projection },   # horizontal_projection returns scaled expolygons
        );
        $self->thumbnail->simplify(0.5);
    } else {
        my $convex_hull = Slic3r::ExPolygon->new($mesh->convex_hull);
        $self->thumbnail->append($convex_hull);
    }
    
    return $self->thumbnail;
}

sub transform_thumbnail {
    my ($self, $model, $obj_idx) = @_;
    
    return unless defined $self->thumbnail;
    
    my $model_object = $model->objects->[$obj_idx];
    my $model_instance = $model_object->instances->[0];
    
    # the order of these transformations MUST be the same everywhere, including
    # in Slic3r::Print->add_model_object()
    my $t = $self->thumbnail->clone;
    $t->rotate(deg2rad($model_instance->rotation), Slic3r::Point->new(0,0));
    $t->scale($model_instance->scaling_factor);
    
    $self->transformed_thumbnail($t);
}

1;
