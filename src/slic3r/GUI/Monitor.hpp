#ifndef slic3r_Monitor_hpp_
#define slic3r_Monitor_hpp_

#include "Tabbook.hpp"
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/grid.h>
#include <wx/dataview.h>
#include <wx/panel.h>
#include <wx/statline.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/gbsizer.h>
#include <wx/statbox.h>
#include <wx/tglbtn.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/webrequest.h>
#include <map>
#include <vector>
#include <memory>
#include "Event.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "wxExtensions.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/MonitorBasePanel.h"
#include "slic3r/GUI/StatusPanel.hpp"
#include "slic3r/GUI/UpgradePanel.hpp"
#include "slic3r/GUI/HMSPanel.hpp"
#include "slic3r/GUI/AmsWidgets.hpp"
#include "Widgets/SideTools.hpp"
#include "SelectMachine.hpp"

namespace Slic3r {
namespace GUI {

class MediaFilePanel;

class AddMachinePanel : public wxPanel
{
protected:
	Button* m_button_add_machine;
	wxStaticText* m_staticText_add_machine;
	wxStaticBitmap* m_bitmap_empty;

	void on_add_machine(wxCommandEvent& event);

public:

	AddMachinePanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);
	~AddMachinePanel();

	void msw_rescale();
};

class MonitorPanel : public wxPanel
{
private:
    Tabbook*		m_tabpanel{ nullptr };
    wxSizer*        m_main_sizer{ nullptr };
    
    AddMachinePanel*    m_status_add_machine_panel;
    StatusPanel*        m_status_info_panel;
    MediaFilePanel*     m_media_file_panel;
    UpgradePanel*       m_upgrade_panel;
    HMSPanel*           m_hms_panel;
    Button *            m_connection_info{nullptr};

	/* side tools */
    SideTools*      m_side_tools{nullptr};
    wxStaticBitmap* m_bitmap_printer_type;
    wxStaticBitmap* m_bitmap_arrow;
    wxStaticText*   m_staticText_printer_name;
    wxStaticBitmap* m_bitmap_wifi_signal;
    wxBoxSizer *    m_side_tools_sizer;


    SelectMachinePopup m_select_machine;

	/* images */
    wxBitmap m_signal_strong_img;
    wxBitmap m_signal_middle_img;
    wxBitmap m_signal_weak_img;
    wxBitmap m_signal_no_img;
    wxBitmap m_printer_img;
    wxBitmap m_arrow_img;

    int last_wifi_signal = -1;
    wxTimer* m_refresh_timer = nullptr;
    int last_status;
    bool m_initialized { false };

public:
    MonitorPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~MonitorPanel();
    
	void init_bitmap();
    void init_timer();
    void init_tabpanel();
    void set_default();
    wxWindow* create_side_tools();

	void msw_rescale();

	void select_machine(std::string machine_sn);
    void on_update_all(wxMouseEvent &event);
    void on_timer(wxTimerEvent& event);
    void on_select_printer(wxCommandEvent& event);
    void on_printer_clicked(wxMouseEvent &event);
    void on_size(wxSizeEvent &event);

    /* update apis */
    void update_status(MachineObject* obj);
    //void update_ams(MachineObject* obj);
    void update_all();

    bool Show(bool show);

	void update_side_panel();
    void show_status(int status);

    MachineObject *obj { nullptr };
    std::string last_conn_type = "undedefined";
};

} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
