#include "GUI.hpp"

#include <assert.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

#if __APPLE__
#import <IOKit/pwr_mgt/IOPMLib.h>
#elif _WIN32
#include <Windows.h>
// Undefine min/max macros incompatible with the standard library
// For example, std::numeric_limits<std::streamsize>::max()
// produces some weird errors
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "boost/nowide/convert.hpp"
#pragma comment(lib, "user32.lib")
#endif

#include <wx/app.h>
#include <wx/button.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/window.h>

#include "Tab.h"
#include "AppConfig.hpp"

namespace Slic3r { namespace GUI {

#if __APPLE__
IOPMAssertionID assertionID;
#endif

void disable_screensaver()
{
    #if __APPLE__
    CFStringRef reasonForActivity = CFSTR("Slic3r");
    IOReturn success = IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep, 
        kIOPMAssertionLevelOn, reasonForActivity, &assertionID); 
    // ignore result: success == kIOReturnSuccess
    #elif _WIN32
    SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_CONTINUOUS);
    #endif
}

void enable_screensaver()
{
    #if __APPLE__
    IOReturn success = IOPMAssertionRelease(assertionID);
    #elif _WIN32
    SetThreadExecutionState(ES_CONTINUOUS);
    #endif
}

std::vector<std::string> scan_serial_ports()
{
    std::vector<std::string> out;
#ifdef _WIN32
    // 1) Open the registry key SERIALCOM.
    HKEY hKey;
    LONG lRes = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey);
    assert(lRes == ERROR_SUCCESS);
    if (lRes == ERROR_SUCCESS) {
        // 2) Get number of values of SERIALCOM key.
        DWORD        cValues;                   // number of values for key 
        {
            TCHAR    achKey[255];               // buffer for subkey name
            DWORD    cbName;                    // size of name string 
            TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
            DWORD    cchClassName = MAX_PATH;   // size of class string 
            DWORD    cSubKeys=0;                // number of subkeys 
            DWORD    cbMaxSubKey;               // longest subkey size 
            DWORD    cchMaxClass;               // longest class string 
            DWORD    cchMaxValue;               // longest value name 
            DWORD    cbMaxValueData;            // longest value data 
            DWORD    cbSecurityDescriptor;      // size of security descriptor 
            FILETIME ftLastWriteTime;           // last write time 
            // Get the class name and the value count.
            lRes = RegQueryInfoKey(
                hKey,                    // key handle 
                achClass,                // buffer for class name 
                &cchClassName,           // size of class string 
                NULL,                    // reserved 
                &cSubKeys,               // number of subkeys 
                &cbMaxSubKey,            // longest subkey size 
                &cchMaxClass,            // longest class string 
                &cValues,                // number of values for this key 
                &cchMaxValue,            // longest value name 
                &cbMaxValueData,         // longest value data 
                &cbSecurityDescriptor,   // security descriptor 
                &ftLastWriteTime);       // last write time
            assert(lRes == ERROR_SUCCESS);
        }
        // 3) Read the SERIALCOM values.
        {
            DWORD dwIndex = 0;
            for (int i = 0; i < cValues; ++ i, ++ dwIndex) {
                wchar_t valueName[2048];
                DWORD	valNameLen = 2048;
                DWORD	dataType;
				wchar_t data[2048];
				DWORD	dataSize = 4096;
				lRes = ::RegEnumValueW(hKey, dwIndex, valueName, &valNameLen, nullptr, &dataType, (BYTE*)&data, &dataSize);
                if (lRes == ERROR_SUCCESS && dataType == REG_SZ && valueName[0] != 0)
					out.emplace_back(boost::nowide::narrow(data));
            }
        }
        ::RegCloseKey(hKey);
    }
#else
    // UNIX and OS X
    std::initializer_list<const char*> prefixes { "ttyUSB" , "ttyACM", "tty.", "cu.", "rfcomm" };
    for (auto &dir_entry : boost::filesystem::directory_iterator(boost::filesystem::path("/dev"))) {
        std::string name = dir_entry.path().filename().string();
        for (const char *prefix : prefixes) {
            if (boost::starts_with(name, prefix)) {
                out.emplace_back(dir_entry.path().string());
                break;
            }
        }
    }
#endif

    out.erase(std::remove_if(out.begin(), out.end(), 
        [](const std::string &key){ 
            return boost::starts_with(key, "Bluetooth") || boost::starts_with(key, "FireFly"); 
        }),
        out.end());
    return out;
}

bool debugged()
{
    #ifdef _WIN32
    return IsDebuggerPresent();
	#else
	return false;
    #endif /* _WIN32 */
}

void break_to_debugger()
{
    #ifdef _WIN32
    if (IsDebuggerPresent())
        DebugBreak();
    #endif /* _WIN32 */
}

// Passing the wxWidgets GUI classes instantiated by the Perl part to C++.
wxApp       *g_wxApp        = nullptr;
wxFrame     *g_wxMainFrame  = nullptr;
wxNotebook  *g_wxTabPanel   = nullptr;

void set_wxapp(wxApp *app)
{
    g_wxApp = app;
}

void set_main_frame(wxFrame *main_frame)
{
    g_wxMainFrame = main_frame;
}

void set_tab_panel(wxNotebook *tab_panel)
{
    g_wxTabPanel = tab_panel;
}

void add_debug_menu(wxMenuBar *menu)
{
#if 0
    auto debug_menu = new wxMenu();
    debug_menu->Append(wxWindow::NewControlId(1), "Some debug");
    menu->Append(debug_menu, _T("&Debug"));
#endif
}

void create_preset_tabs(PresetBundle *preset_bundle, AppConfig *app_config)
{	
	add_created_tab(new TabPrint   (g_wxTabPanel, "Print"),    preset_bundle, app_config);
	add_created_tab(new TabFilament(g_wxTabPanel, "Filament"), preset_bundle, app_config);
	add_created_tab(new TabPrinter (g_wxTabPanel, "Printer"),  preset_bundle, app_config);
}

void add_created_tab(Tab* panel, PresetBundle *preset_bundle, AppConfig *app_config)
{
	panel->m_no_controller = app_config->get("no_controller").empty();
	panel->create_preset_tab(preset_bundle);
	// Callback to be executed after any of the configuration fields(Perl class Slic3r::GUI::OptionsGroup::Field) change their value.
	panel->m_on_value_change = [/*this*/](std::string opt_key, boost::any value){
	//! plater & loaded - variables of MainFrame
// 		if (plater) {
// 			plater->on_config_change(m_config); //# propagate config change events to the plater
// 			if (opt_key.compare("extruders_count")	plater->on_extruders_change(value);
// 		}
		// don't save while loading for the first time
//		if (loaded && Slic3r::GUI::autosave) m_config->save(Slic3r::GUI::autosave) ;
	};

// 	# Install a callback for the tab to update the platter and print controller presets, when
// 	# a preset changes at Slic3r::GUI::Tab.
// 	$tab->on_presets_changed(sub{
// 		if ($self->{plater}) {
// 			# Update preset combo boxes(Print settings, Filament, Printer) from their respective tabs.
// 			$self->{plater}->update_presets($tab_name, @_);
// 			if ($tab_name eq 'printer') {
// 				# Printer selected at the Printer tab, update "compatible" marks at the print and filament selectors.
// 				my($presets, $reload_dependent_tabs) = @_;
// 				for my $tab_name_other(qw(print filament)) {
// 					# If the printer tells us that the print or filament preset has been switched or invalidated,
// 					# refresh the print or filament tab page.Otherwise just refresh the combo box.
// 					my $update_action = ($reload_dependent_tabs && (first{ $_ eq $tab_name_other } (@{$reload_dependent_tabs})))
// 						? 'load_current_preset' : 'update_tab_ui';
// 					$self->{options_tabs}{$tab_name_other}->$update_action;
// 				}
// 				# Update the controller printers.
// 				$self->{controller}->update_presets(@_) if $self->{controller};
// 			}
// 			$self->{plater}->on_config_change($tab->{presets}->get_current_preset->config);
// 		}
// 	});

	//# Load the currently selected preset into the GUI, update the preset selection box.
//	panel->load_current_preset;

	g_wxTabPanel->AddPage(panel, panel->title());
}

void show_error(wxWindow* parent, std::string message){
	auto msg_wingow = new wxMessageDialog(parent, message, "Error", wxOK | wxICON_ERROR);
	msg_wingow->ShowModal();
}

void show_info(wxWindow* parent, std::string message, std::string title){
	auto msg_wingow = new wxMessageDialog(parent, message, title.empty() ? "Notise" : title, wxOK | wxICON_INFORMATION);
	msg_wingow->ShowModal();
}

} }
