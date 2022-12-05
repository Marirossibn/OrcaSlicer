#include "WebGuideDialog.hpp"
#include "ConfigWizard.hpp"

#include <string.h>
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r_version.h"

#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

#include <wx/wx.h>
#include <wx/fileconf.h>
#include <wx/file.h>
#include <wx/wfstream.h>

#include <boost/cast.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

#include "MainFrame.hpp"
#include <boost/dll.hpp>
#include <slic3r/GUI/Widgets/WebView.hpp>
#include <slic3r/Utils/Http.hpp>
#include <libslic3r/miniz_extension.hpp>
#include <libslic3r/Utils.hpp>

using namespace nlohmann;

namespace Slic3r { namespace GUI {

json m_ProfileJson;

GuideFrame::GuideFrame(GUI_App *pGUI, long style)
    : DPIDialog((wxWindow *) (pGUI->mainframe), wxID_ANY, "BambuStudio", wxDefaultPosition, wxDefaultSize, style),
	m_appconfig_new()
{
    // INI
    m_SectionName = "firstguide";
    PrivacyUse    = true;
    InstallNetplugin = false;

    m_MainPtr = pGUI;

    // set the frame icon
    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);

    wxString TargetUrl = SetStartPage(BBL_WELCOME, false);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  set start page to welcome ");

    // Create the webview
    m_browser = WebView::CreateWebView(this, TargetUrl);
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    SetSizer(topsizer);

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    // Log backend information
    // wxLogMessage(wxWebView::GetBackendVersionInfo().ToString());
    // wxLogMessage("Backend: %s Version: %s",
    // m_browser->GetClassInfo()->GetClassName(),wxWebView::GetBackendVersionInfo().ToString());
    // wxLogMessage("User Agent: %s", m_browser->GetUserAgent());

    // Set a more sensible size for web browsing
    wxSize pSize = FromDIP(wxSize(820, 660));
    SetSize(pSize);

    CenterOnParent();
    //int screenheight = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y, NULL);
    //int screenwidth  = wxSystemSettings::GetMetric(wxSYS_SCREEN_X, NULL);
    //int MaxY         = (screenheight - pSize.y) > 0 ? (screenheight - pSize.y) / 2 : 0;
    //MoveWindow(this->m_hWnd, (screenwidth - pSize.x) / 2, MaxY, pSize.x, pSize.y, TRUE);
#ifdef __WXMSW__
    this->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        if ((m_page == BBL_FILAMENT_ONLY || m_page == BBL_MODELS_ONLY) && e.GetKeyCode() == WXK_ESCAPE) {
            if (this->IsModal())
                this->EndModal(wxID_CANCEL);
            else
                this->Close();
        }
        else
            e.Skip();
        });
#endif
    // Connect the webview events
    Bind(wxEVT_WEBVIEW_NAVIGATING, &GuideFrame::OnNavigationRequest, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATED, &GuideFrame::OnNavigationComplete, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &GuideFrame::OnDocumentLoaded, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR, &GuideFrame::OnError, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &GuideFrame::OnNewWindow, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &GuideFrame::OnTitleChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_FULLSCREEN_CHANGED, &GuideFrame::OnFullScreenChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &GuideFrame::OnScriptMessage, this, m_browser->GetId());

    // Connect the idle events
    // Bind(wxEVT_IDLE, &GuideFrame::OnIdle, this);
    // Bind(wxEVT_CLOSE_WINDOW, &GuideFrame::OnClose, this);

    LoadProfile();

    // UI
    SetStartPage(BBL_REGION);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  finished");
    wxGetApp().UpdateDlgDarkUI(this);
}

GuideFrame::~GuideFrame()
{
    if (m_browser) {
        delete m_browser;
        m_browser = nullptr;
    }
}

void GuideFrame::load_url(wxString &url)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< " enter, url=" << url.ToStdString();
    this->Show();
    WebView::LoadUrl(m_browser, url);
    m_browser->SetFocus();
    UpdateState();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< " exit";
}

wxString GuideFrame::SetStartPage(GuidePage startpage, bool load)
{
    m_page = startpage;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(" enter, load=%1%, start_page=%2%")%load%int(startpage);
    //wxLogMessage("GUIDE: webpage_1  %s", (boost::filesystem::path(resources_dir()) / "web\\guide\\1\\index.html").make_preferred().string().c_str() );
    wxString TargetUrl = from_u8( (boost::filesystem::path(resources_dir()) / "web/guide/1/index.html").make_preferred().string() );
    //wxLogMessage("GUIDE: webpage_2  %s", TargetUrl.mb_str());

    if (startpage == BBL_WELCOME){
        SetTitle(_L("Setup Wizard"));
        TargetUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/guide/1/index.html").make_preferred().string());
    } else if (startpage == BBL_REGION) {
        SetTitle(_L("Setup Wizard"));
        TargetUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/guide/11/index.html").make_preferred().string());
    } else if (startpage == BBL_MODELS) {
        SetTitle(_L("Setup Wizard"));
        TargetUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/guide/21/index.html").make_preferred().string());
    } else if (startpage == BBL_FILAMENTS) {
        SetTitle(_L("Setup Wizard"));

        int nSize = m_ProfileJson["model"].size();

        if (nSize>0)
            TargetUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/guide/22/index.html").make_preferred().string());
        else
            TargetUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/guide/21/index.html").make_preferred().string());
    } else if (startpage == BBL_FILAMENT_ONLY) {
        SetTitle("");
        TargetUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/guide/23/index.html").make_preferred().string());
    } else if (startpage == BBL_MODELS_ONLY) {
        SetTitle("");
        TargetUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/guide/24/index.html").make_preferred().string());
    }
    else {
        SetTitle(_L("Setup Wizard"));
        TargetUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/guide/21/index.html").make_preferred().string());
    }

    std::string strlang = wxGetApp().app_config->get("language");
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(", strlang=%1%")%strlang;
    if (strlang != "")
        TargetUrl = wxString::Format("%s?lang=%s", w2s(TargetUrl), strlang);

    TargetUrl = "file://" + TargetUrl;
    if (load)
        load_url(TargetUrl);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< " exit";
    return TargetUrl;
}

/**
 * Method that retrieves the current state from the web control and updates
 * the GUI the reflect this current state.
 */
void GuideFrame::UpdateState()
{
    // SetTitle(m_browser->GetCurrentTitle());
}

void GuideFrame::OnIdle(wxIdleEvent &WXUNUSED(evt))
{
    if (m_browser->IsBusy()) {
        wxSetCursor(wxCURSOR_ARROWWAIT);
    } else {
        wxSetCursor(wxNullCursor);
    }
}

// void GuideFrame::OnClose(wxCloseEvent& evt)
//{
//    this->Hide();
//}

/**
 * Callback invoked when there is a request to load a new page (for instance
 * when the user clicks a link)
 */
void GuideFrame::OnNavigationRequest(wxWebViewEvent &evt)
{
    // wxLogMessage("%s", "Navigation request to '" + evt.GetURL() + "'
    // (target='" + evt.GetTarget() + "')");

    UpdateState();
}

/**
 * Callback invoked when a navigation request was accepted
 */
void GuideFrame::OnNavigationComplete(wxWebViewEvent &evt)
{
    //wxLogMessage("%s", "Navigation complete; url='" + evt.GetURL() + "'");

    wxString NewUrl = evt.GetURL();

    UpdateState();
}

/**
 * Callback invoked when a page is finished loading
 */
void GuideFrame::OnDocumentLoaded(wxWebViewEvent &evt)
{
    // Only notify if the document is the main frame, not a subframe
    wxString tmpUrl = evt.GetURL();
    wxString NowUrl = m_browser->GetCurrentURL();

    if (evt.GetURL() == m_browser->GetCurrentURL()) {
        // wxLogMessage("%s", "Document loaded; url='" + evt.GetURL() + "'");
    }
    UpdateState();

    // wxCommandEvent *event = new
    // wxCommandEvent(EVT_WEB_RESPONSE_MESSAGE,this->GetId()); wxQueueEvent(this,
    // event);
}

/**
 * On new window, we veto to stop extra windows appearing
 */
void GuideFrame::OnNewWindow(wxWebViewEvent &evt)
{
    wxString flag = " (other)";

    wxString NewUrl= evt.GetURL();
    wxLaunchDefaultBrowser(NewUrl);
    //if (evt.GetNavigationAction() == wxWEBVIEW_NAV_ACTION_USER) { flag = " (user)"; }
    // wxLogMessage("%s", "New window; url='" + evt.GetURL() + "'" + flag);

    // If we handle new window events then just load them in this window as we
    // are a single window browser
    // if (m_tools_handle_new_window->IsChecked())
    //    m_browser->LoadURL(evt.GetURL());

    UpdateState();
}

void GuideFrame::OnTitleChanged(wxWebViewEvent &evt)
{
    // SetTitle(evt.GetString());
    // wxLogMessage("%s", "Title changed; title='" + evt.GetString() + "'");
}

void GuideFrame::OnFullScreenChanged(wxWebViewEvent &evt)
{
    // wxLogMessage("Full screen changed; status = %d", evt.GetInt());
    ShowFullScreen(evt.GetInt() != 0);
}

void GuideFrame::OnScriptMessage(wxWebViewEvent &evt)
{
    try {
        wxString strInput = evt.GetString();
        json     j        = json::parse(strInput);

        wxString strCmd = j["command"];
        if (strCmd == "close_page") {
            this->EndModal(wxID_CANCEL);
        }
        if (strCmd == "user_clause") {
            wxString strAction = j["data"]["action"];

            if (strAction == "refuse") {
                // CloseTheApp
                this->EndModal(wxID_OK);

                m_MainPtr->mainframe->Close(); // Refuse Clause, App quit immediately
            }
        } else if (strCmd == "user_private_choice") {
            wxString strAction = j["data"]["action"];

            if (strAction == "agree") {
                PrivacyUse = true;
            } else {
                PrivacyUse = false;
            }
        }
        else if (strCmd == "request_userguide_profile") {
            json m_Res = json::object();
            m_Res["command"] = "response_userguide_profile";
            m_Res["sequence_id"] = "10001";
            m_Res["response"]        = m_ProfileJson;

            wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump(-1, ' ', false, json::error_handler_t::ignore));

            wxGetApp().CallAfter([this,strJS] { RunScript(strJS); });
        }
        else if (strCmd == "save_userguide_models")
        {
            json MSelected = j["data"];

            int nModel = m_ProfileJson["model"].size();
            for (int m = 0; m < nModel; m++) {
                json TmpModel = m_ProfileJson["model"][m];
                m_ProfileJson["model"][m]["nozzle_selected"] = "";

                for (auto it = MSelected.begin(); it != MSelected.end(); ++it) {
                    json OneSelect = it.value();

                    wxString s1 = TmpModel["model"];
                    wxString s2 = OneSelect["model"];
                    if (s1.compare(s2) == 0) {
                        m_ProfileJson["model"][m]["nozzle_selected"] = OneSelect["nozzle_diameter"];
                        break;
                    }
                }
            }
        }
        else if (strCmd == "save_userguide_filaments") {
            //reset
            for (auto it = m_ProfileJson["filament"].begin(); it != m_ProfileJson["filament"].end(); ++it)
            {
                m_ProfileJson["filament"][it.key()]["selected"] = 0;
            }

            json fSelected = j["data"]["filament"];
            int nF = fSelected.size();
            for (int m = 0; m < nF; m++)
            {
                std::string fName = fSelected[m];

                m_ProfileJson["filament"][fName]["selected"] = 1;
            }
        }
        else if (strCmd == "user_guide_finish") {
            SaveProfile();

            std::string oldregion = m_ProfileJson["region"];
            bool        bLogin    = false;
            if (m_Region != oldregion) {
                AppConfig* config = GUI::wxGetApp().app_config;
                std::string country_code = config->get_country_code();
                NetworkAgent* agent = wxGetApp().getAgent();
                if (agent) {
                    agent->set_country_code(country_code);
                    if (wxGetApp().is_user_login()) {
                        bLogin = true;
                        agent->user_logout();
                    }
                }
            }

            this->EndModal(wxID_OK);

            if (InstallNetplugin)
                GUI::wxGetApp().CallAfter([this] { GUI::wxGetApp().ShowDownNetPluginDlg(); });

            if (bLogin)
                GUI::wxGetApp().CallAfter([this] { login(); });
        }
        else if (strCmd == "user_guide_cancel") {
            this->EndModal(wxID_CANCEL);
            this->Close();
        } else if (strCmd == "save_region") {
            m_Region = j["region"];
        }
        else if (strCmd == "network_plugin_install") {
            std::string sAction = j["data"]["action"];

            if (sAction == "yes")
                InstallNetplugin = true;
            else
                InstallNetplugin = false;
        }
    } catch (std::exception &e) {
        // wxMessageBox(e.what(), "json Exception", MB_OK);
    }

    wxString strAll = m_ProfileJson.dump(-1,' ',false, json::error_handler_t::ignore);
}

void GuideFrame::RunScript(const wxString &javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    //m_javascript = javascript;

    // wxLogMessage("Running JavaScript:\n%s\n", javascript);

    if (!m_browser) return;

    WebView::RunScript(m_browser, javascript);
}

#if wxUSE_WEBVIEW_IE
void GuideFrame::OnRunScriptObjectWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var person = new Object();person.name = 'Foo'; \
    person.lastName = 'Bar';return person;}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void GuideFrame::OnRunScriptDateWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var d = new Date('10/08/2017 21:30:40'); \
    var tzoffset = d.getTimezoneOffset() * 60000; return \
    new Date(d.getTime() - tzoffset);}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void GuideFrame::OnRunScriptArrayWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){ return [\"foo\", \"bar\"]; }f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}
#endif

/**
 * Callback invoked when a loading error occurs
 */
void GuideFrame::OnError(wxWebViewEvent &evt)
{
#define WX_ERROR_CASE(type) \
    case type: category = #type; break;

    wxString category;
    switch (evt.GetInt()) {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    // wxLogMessage("%s", "Error; url='" + evt.GetURL() + "', error='" +
    // category + " (" + evt.GetString() + ")'");

    // Show the info bar with an error
    // m_info->ShowMessage(_L("An error occurred loading ") + evt.GetURL() +
    // "\n" + "'" + category + "'", wxICON_ERROR);
    BOOST_LOG_TRIVIAL(info) << "GuideFrame::OnError: An error occurred loading " << evt.GetURL() << category;

    UpdateState();
}

void GuideFrame::OnScriptResponseMessage(wxCommandEvent &WXUNUSED(evt))
{
    // if (!m_response_js.empty())
    //{
    //    RunScript(m_response_js);
    //}

    // RunScript("This is a message to Web!");
    // RunScript("postMessage(\"AABBCCDD\");");
}

bool GuideFrame::IsFirstUse()
{
    wxString    strUse;
    std::string strVal = wxGetApp().app_config->get(std::string(m_SectionName.mb_str()), "finish");
    if (strVal == "1")
        return false;

    if (bbl_bundle_rsrc == true)
        return true;

    return true;
}

/*int GuideFrame::CopyDir(const boost::filesystem::path &from_dir, const boost::filesystem::path &to_dir)
{
    if (!boost::filesystem::is_directory(from_dir)) return -1;
    // i assume to_dir.parent surely exists
    if (!boost::filesystem::is_directory(to_dir)) boost::filesystem::create_directory(to_dir);
    for (auto &dir_entry : boost::filesystem::directory_iterator(from_dir)) {
        if (!boost::filesystem::is_directory(dir_entry.path())) {
            std::string    em;
            CopyFileResult cfr = copy_file(dir_entry.path().string(), (to_dir / dir_entry.path().filename()).string(), em, false);
            if (cfr != SUCCESS) { BOOST_LOG_TRIVIAL(error) << "Error when copying files from " << from_dir << " to " << to_dir << ": " << em; }
        } else {
            CopyDir(dir_entry.path(), to_dir / dir_entry.path().filename());
        }
    }

    return 0;
}*/

int GuideFrame::SaveProfile()
{
    //privacy
    if (PrivacyUse == true) {
        m_MainPtr->app_config->set(std::string(m_SectionName.mb_str()), "privacyuse", "1");
    } else
        m_MainPtr->app_config->set(std::string(m_SectionName.mb_str()), "privacyuse", "0");

    m_MainPtr->app_config->set("region", m_Region);

    //finish
    m_MainPtr->app_config->set(std::string(m_SectionName.mb_str()), "finish", "1");

    m_MainPtr->app_config->save();

    //Load BBS Conf
    /*wxString strConfPath = wxGetApp().app_config->config_path();
    json     jCfg;
    std::ifstream(w2s(strConfPath)) >> jCfg;

    //model
    jCfg["models"] = json::array();
    int nM         = m_ProfileJson["model"].size();
    int nModelChoose    = 0;
    for (int m = 0; m < nM; m++)
    {
        json amodel = m_ProfileJson["model"][m];

        amodel["nozzle_diameter"] = amodel["nozzle_selected"];
        amodel.erase("nozzle_selected");
        amodel.erase("preview");
        amodel.erase("sub_path");
        amodel.erase("cover");
        amodel.erase("materials");

        std::string ss = amodel["nozzle_diameter"];
        if (ss.compare("") != 0) {
            nModelChoose++;
            jCfg["models"].push_back(amodel);
        }
    }
    if (nModelChoose == 0)
        jCfg.erase("models");

    if (nModelChoose > 0) {
        // filament
        jCfg["filaments"] = json::array();
        for (auto it = m_ProfileJson["filament"].begin(); it != m_ProfileJson["filament"].end(); ++it) {
            if (it.value()["selected"] == 1) { jCfg["filaments"].push_back(it.key()); }
        }

        // Preset
        jCfg["presets"]["filaments"] = json::array();
        jCfg["presets"]["filaments"].push_back(jCfg["filaments"][0]);

        std::string PresetMachine  = m_ProfileJson["machine"][0]["name"];
        jCfg["presets"]["machine"] = PresetMachine;

        int nTotal = m_ProfileJson["process"].size();
        int nSet   = nTotal / 2;
        if (nSet > 0) nSet--;

        std::string sMode          = m_ProfileJson["process"][nSet]["name"];
        jCfg["presets"]["process"] = sMode;

    } else {
        jCfg["presets"]["filaments"] = json::array();
        jCfg["presets"]["filaments"].push_back("Default Filament");

        jCfg["presets"]["machine"] = "Default Printer";

        jCfg["presets"]["process"] = "Default Setting";
    }

    std::string sOut = jCfg.dump(4, ' ', false);

    std::ofstream output_file(w2s(strConfPath));
    output_file << sOut;
    output_file.close();

    //Copy Profiles
    if (bbl_bundle_rsrc)
    {
        CopyDir(rsrc_vendor_dir,vendor_dir);
    }*/

    std::string strAll = m_ProfileJson.dump(-1, ' ', false, json::error_handler_t::ignore);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "before save to app_config: "<< std::endl<<strAll;

    //set filaments to app_config
    const std::string &section_name = AppConfig::SECTION_FILAMENTS;
    std::map<std::string, std::string> section_new;
    m_appconfig_new.clear_section(section_name);
    for (auto it = m_ProfileJson["filament"].begin(); it != m_ProfileJson["filament"].end(); ++it) {
        if (it.value()["selected"] == 1){
            section_new[it.key()] = "true";
        }
    }
    m_appconfig_new.set_section(section_name, section_new);

    //set vendors to app_config
    Slic3r::AppConfig::VendorMap empty_vendor_map;
    m_appconfig_new.set_vendors(empty_vendor_map);
    for (auto it = m_ProfileJson["model"].begin(); it != m_ProfileJson["model"].end(); ++it)
    {
        if (it.value().is_object()) {
            json temp_model = it.value();
            std::string model_name = temp_model["model"];
            std::string vendor_name = temp_model["vendor"];
            std::string selected = temp_model["nozzle_selected"];
            boost::trim(selected);
            std::string nozzle;
            while (selected.size() > 0) {
                auto pos = selected.find(';');
                if (pos != std::string::npos) {
                    nozzle   = selected.substr(0, pos);
                    m_appconfig_new.set_variant(vendor_name, model_name, nozzle, "true");
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("vendor_name %1%, model_name %2%, nozzle %3% selected")%vendor_name %model_name %nozzle;
                    selected = selected.substr(pos + 1);
                    boost::trim(selected);
                }
                else {
                    m_appconfig_new.set_variant(vendor_name, model_name, selected, "true");
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("vendor_name %1%, model_name %2%, nozzle %3% selected")%vendor_name %model_name %selected;
                    break;
                }
            }
        }
    }

    //m_appconfig_new

    return 0;
}

static std::set<std::string> get_new_added_presets(const std::map<std::string, std::string>& old_data, const std::map<std::string, std::string>& new_data)
{
    auto get_aliases = [](const std::map<std::string, std::string>& data) {
        std::set<std::string> old_aliases;
        for (auto item : data) {
            const std::string& name = item.first;
            size_t pos = name.find("@");
            old_aliases.emplace(pos == std::string::npos ? name : name.substr(0, pos-1));
        }
        return old_aliases;
    };

    std::set<std::string> old_aliases = get_aliases(old_data);
    std::set<std::string> new_aliases = get_aliases(new_data);
    std::set<std::string> diff;
    std::set_difference(new_aliases.begin(), new_aliases.end(), old_aliases.begin(), old_aliases.end(), std::inserter(diff, diff.begin()));

    return diff;
}

static std::string get_first_added_preset(const std::map<std::string, std::string>& old_data, const std::map<std::string, std::string>& new_data)
{
    std::set<std::string> diff = get_new_added_presets(old_data, new_data);
    if (diff.empty())
        return std::string();
    return *diff.begin();
}

bool GuideFrame::apply_config(AppConfig *app_config, PresetBundle *preset_bundle, const PresetUpdater *updater, bool& apply_keeped_changes)
{
    const auto enabled_vendors = m_appconfig_new.vendors();
    const auto old_enabled_vendors = app_config->vendors();

    const auto enabled_filaments = m_appconfig_new.has_section(AppConfig::SECTION_FILAMENTS) ? m_appconfig_new.get_section(AppConfig::SECTION_FILAMENTS) : std::map<std::string, std::string>();
    const auto old_enabled_filaments = app_config->has_section(AppConfig::SECTION_FILAMENTS) ? app_config->get_section(AppConfig::SECTION_FILAMENTS) : std::map<std::string, std::string>();

    bool check_unsaved_preset_changes = false;
    std::vector<std::string> install_bundles;
    std::vector<std::string> remove_bundles;
    const auto vendor_dir = (boost::filesystem::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR).make_preferred();
    for (const auto &it : enabled_vendors) {
        if (it.second.size() > 0) {
            auto vendor_file = vendor_dir/(it.first + ".json");
            if (!fs::exists(vendor_file)) {
                install_bundles.emplace_back(it.first);
            }
        }
    }

    //add the removed vendor bundles
    for (const auto &it : old_enabled_vendors) {
        if (it.second.size() > 0) {
            if (enabled_vendors.find(it.first) != enabled_vendors.end())
                continue;
            auto vendor_file = vendor_dir/(it.first + ".json");
            if (fs::exists(vendor_file)) {
                remove_bundles.emplace_back(it.first);
            }
        }
    }

    check_unsaved_preset_changes = (enabled_vendors != old_enabled_vendors) || (enabled_filaments != old_enabled_filaments);
    wxString header = _L("The configuration package is changed in previous Config Guide");
    wxString caption = _L("Configuration package changed");
    int act_btns = UnsavedChangesDialog::ActionButtons::KEEP|UnsavedChangesDialog::ActionButtons::SAVE;

    if (check_unsaved_preset_changes &&
        !wxGetApp().check_and_keep_current_preset_changes(caption, header, act_btns, &apply_keeped_changes))
        return false;

    if (install_bundles.size() > 0) {
        // Install bundles from resources.
        // Don't create snapshot - we've already done that above if applicable.
        if (! updater->install_bundles_rsrc(std::move(install_bundles), false))
            return false;
    } else {
        BOOST_LOG_TRIVIAL(info) << "No bundles need to be installed from resource directory";
    }

    if (remove_bundles.size() > 0) {
        //remove unused bundles
        for (const auto &it : remove_bundles) {
            auto vendor_file = vendor_dir/(it + ".json");
            auto sub_dir = vendor_dir/(it);
            if (fs::exists(vendor_file))
                fs::remove(vendor_file);
            if (fs::exists(sub_dir))
                fs::remove_all(sub_dir);
        }
    } else {
        BOOST_LOG_TRIVIAL(info) << "No bundles need to be removed";
    }

    std::string preferred_model;
    std::string preferred_variant;
    PrinterTechnology preferred_pt = ptFFF;
    auto get_preferred_printer_model = [preset_bundle, enabled_vendors, old_enabled_vendors, preferred_pt](const std::string& bundle_name, std::string& variant) {
        const auto config = enabled_vendors.find(bundle_name);
        if (config == enabled_vendors.end())
            return std::string();

        const std::map<std::string, std::set<std::string>>& model_maps = config->second;
        //for (const auto& vendor_profile : preset_bundle->vendors) {
        for (const auto model_it: model_maps) {
            if (model_it.second.size() > 0) {
                variant = *model_it.second.begin();
                const auto config_old = old_enabled_vendors.find(bundle_name);
                if (config_old == old_enabled_vendors.end())
                    return model_it.first;
                const auto model_it_old = config_old->second.find(model_it.first);
                if (model_it_old == config_old->second.end())
                    return model_it.first;
                else if (model_it_old->second != model_it.second) {
                    for (const auto& var : model_it.second)
                        if (model_it_old->second.find(var) == model_it_old->second.end()) {
                            variant = var;
                            return model_it.first;
                        }
                }
            }
        }
        //}
        if (!variant.empty())
            variant.clear();
        return std::string();
    };
    // Prusa printers are considered first, then 3rd party.
    if (preferred_model = get_preferred_printer_model(PresetBundle::BBL_BUNDLE, preferred_variant);
        preferred_model.empty()) {
        for (const auto& bundle : enabled_vendors) {
            if (bundle.first == PresetBundle::BBL_BUNDLE) { continue; }
            if (preferred_model = get_preferred_printer_model(bundle.first, preferred_variant);
                !preferred_model.empty())
                    break;
        }
    }

    std::string first_added_filament;
    auto get_first_added_material_preset = [this, app_config](const std::string& section_name, std::string& first_added_preset) {
        if (m_appconfig_new.has_section(section_name)) {
            // get first of new added preset names
            const std::map<std::string, std::string>& old_presets = app_config->has_section(section_name) ? app_config->get_section(section_name) : std::map<std::string, std::string>();
            first_added_preset = get_first_added_preset(old_presets, m_appconfig_new.get_section(section_name));
        }
    };
    // Not switch filament
    //get_first_added_material_preset(AppConfig::SECTION_FILAMENTS, first_added_filament);

    //update the app_config
    app_config->set_section(AppConfig::SECTION_FILAMENTS, enabled_filaments);
    app_config->set_vendors(m_appconfig_new);

    if (check_unsaved_preset_changes)
        preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSilentDisableSystem,
                                    {preferred_model, preferred_variant, first_added_filament, std::string()});

    // Update the selections from the compatibilty.
    preset_bundle->export_selections(*app_config);

    return true;
}

bool GuideFrame::run()
{
    //BOOST_LOG_TRIVIAL(info) << boost::format("Running ConfigWizard, reason: %1%, start_page: %2%") % reason % start_page;

    GUI_App &app = wxGetApp();

    //p->set_run_reason(reason);
    //p->set_start_page(start_page);
    app.preset_bundle->export_selections(*app.app_config);

    BOOST_LOG_TRIVIAL(info) << "GuideFrame before ShowModal";
    if (this->ShowModal() == wxID_OK) {
        bool apply_keeped_changes = false;
        BOOST_LOG_TRIVIAL(info) << "GuideFrame returned ok";
        if (! this->apply_config(app.app_config, app.preset_bundle, app.preset_updater, apply_keeped_changes))
            return false;

        if (apply_keeped_changes)
            app.apply_keeped_preset_modifications();

        app.app_config->set_legacy_datadir(false);
        app.update_mode();
        // BBS
        //app.obj_manipul()->update_ui_from_settings();
        BOOST_LOG_TRIVIAL(info) << "GuideFrame applied";
        this->Close();
        return true;
    } else {
        BOOST_LOG_TRIVIAL(info) << "GuideFrame cancelled";
        if (app.preset_bundle->printers.only_default_printers()) {
            //we install the default here
            bool apply_keeped_changes = false;
            //clear filament section and use default materials
            app.app_config->set_variant(PresetBundle::BBL_BUNDLE,
                PresetBundle::BBL_DEFAULT_PRINTER_MODEL, PresetBundle::BBL_DEFAULT_PRINTER_VARIANT, "true");
            app.app_config->clear_section(AppConfig::SECTION_FILAMENTS);
            app.preset_bundle->load_selections(*app.app_config, {PresetBundle::BBL_DEFAULT_PRINTER_MODEL, PresetBundle::BBL_DEFAULT_PRINTER_VARIANT, PresetBundle::BBL_DEFAULT_FILAMENT, std::string()});

            app.app_config->set_legacy_datadir(false);
            app.update_mode();
            return true;
        }
        else
            return false;
    }
}

int GuideFrame::GetFilamentInfo(std::string filepath, std::string &sVendor, std::string &sType)
{
    //GetStardardFilePath(filepath);

    try {
        std::string contents;
        LoadFile(filepath, contents);
        json jLocal = json::parse(contents);

        if (sVendor == "") {
            if (jLocal.contains("filament_vendor"))
                sVendor = jLocal["filament_vendor"][0];
        }

        if (sType == "") {
            if (jLocal.contains("filament_type"))
                sType = jLocal["filament_type"][0];
        }

        if (sVendor == "" || sType == "")
        {
            if (jLocal.contains("inherits")) {
                boost::filesystem::path sf(filepath.c_str());
                filepath = sf.string();

                std::string strFile   = filepath;
                //wxString strFolder = strFile.BeforeLast(boost::filesystem::path::preferred_separator);
                boost::filesystem::path file_path(filepath);

                std::string FName = jLocal["inherits"];
                FName += ".json";
                //wxString strNewFile = wxString::Format("%s%c%s.json", strFolder.mb_str(), boost::filesystem::path::preferred_separator, FName.c_str());
                boost::filesystem::path inherits_path = boost::filesystem::absolute(file_path.parent_path() / FName).make_preferred();

                //boost::filesystem::path nf(strNewFile.c_str());
                if (boost::filesystem::exists(inherits_path))
                    return GetFilamentInfo(inherits_path.string(), sVendor, sType);
                else
                    return -1;
            } else {
                if (sType == "")
                    return -1;
                else
                    sVendor = "Generic";
                    return 0;
            }
        }
        else
            return 0;
    }
    catch(nlohmann::detail::parse_error &err) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": parse "<<filepath <<" got a nlohmann::detail::parse_error, reason = " << err.what();
        return -1;
    }
    catch (std::exception &e)
    {
        // wxLogMessage("GUIDE: load_profile_error  %s ", e.what());
        // wxMessageBox(e.what(), "", MB_OK);
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": parse " << filepath <<" got exception: "<<e.what();
        return -1;
    }

    return 0;
}


int GuideFrame::LoadProfile()
{
    try {
        //wxString ExePath            = boost::dll::program_location().parent_path().string();
        //wxString TargetFolder       = ExePath + "\\resources\\profiles\\";
        //wxString TargetFolderSearch = ExePath + "\\resources\\profiles\\*.json";

        //intptr_t    handle;
        //_finddata_t findData;

        //handle = _findfirst(TargetFolderSearch.mb_str(), &findData); // 查找目录中的第一个文件
        //if (handle == -1) { return -1; }

        //do {
        //    if (findData.attrib & _A_SUBDIR && strcmp(findData.name, ".") == 0 && strcmp(findData.name, "..") == 0) // 是否是子目录并且不为"."或".."
        //    {
        //        // cout << findData.name << "\t<dir>\n";
        //    } else {
        //        wxString strVendor = wxString(findData.name).BeforeLast('.');
        //        LoadProfileFamily(strVendor, TargetFolder + findData.name);
        //    }

        //} while (_findnext(handle, &findData) == 0); // 查找目录中的下一个文件

        // BBS: change directories by design
        //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  will load config from %1%.") % bbl_bundle_path;
        m_ProfileJson             = json::parse("{}");
        m_ProfileJson["model"]    = json::array();
        m_ProfileJson["machine"]  = json::object();
        m_ProfileJson["filament"] = json::object();
        m_ProfileJson["process"]  = json::array();

        vendor_dir      = (boost::filesystem::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR ).make_preferred();
        rsrc_vendor_dir = (boost::filesystem::path(resources_dir()) / "profiles").make_preferred();

        // BBS: add BBL as default
        // BBS: add json logic for vendor bundle
        auto bbl_bundle_path = vendor_dir;
        bbl_bundle_rsrc = false;
        if (!boost::filesystem::exists((vendor_dir / PresetBundle::BBL_BUNDLE).replace_extension(".json"))) {
            bbl_bundle_path = rsrc_vendor_dir;
            bbl_bundle_rsrc = true;
        }

        // intptr_t    handle;
        //_finddata_t findData;

        //handle = _findfirst((bbl_bundle_path / "*.json").make_preferred().string().c_str(), &findData); // 查找目录中的第一个文件
        // if (handle == -1) { return -1; }

        // do {
        //    if (findData.attrib & _A_SUBDIR && strcmp(findData.name, ".") == 0 && strcmp(findData.name, "..") == 0) // 是否是子目录并且不为"."或".."
        //    {
        //        // cout << findData.name << "\t<dir>\n";
        //    } else {
        //        wxString strVendor = wxString(findData.name).BeforeLast('.');
        //        LoadProfileFamily(w2s(strVendor), vendor_dir.make_preferred().string() + "\\"+ findData.name);
        //    }

        //} while (_findnext(handle, &findData) == 0); // 查找目录中的下一个文件


        //load BBL bundle from user data path
        string                                targetPath = bbl_bundle_path.make_preferred().string();
        boost::filesystem::path               myPath(targetPath);
        boost::filesystem::directory_iterator endIter;
        for (boost::filesystem::directory_iterator iter(myPath); iter != endIter; iter++) {
            if (boost::filesystem::is_directory(*iter)) {
                //cout << "is dir" << endl;
                //cout << iter->path().string() << endl;
            } else {
                //cout << "is a file" << endl;
                //cout << iter->path().string() << endl;
                wxString strVendor = from_u8(iter->path().string()).BeforeLast('.');
                strVendor          = strVendor.AfterLast( '\\');
                strVendor          = strVendor.AfterLast('\/');

                if (w2s(strVendor) == PresetBundle::BBL_BUNDLE)
                    LoadProfileFamily(w2s(strVendor), iter->path().string());
            }
        }

        //string                                others_targetPath = rsrc_vendor_dir.string();
        boost::filesystem::directory_iterator others_endIter;
        for (boost::filesystem::directory_iterator iter(rsrc_vendor_dir); iter != others_endIter; iter++) {
            if (boost::filesystem::is_directory(*iter)) {
                //cout << "is dir" << endl;
                //cout << iter->path().string() << endl;
            } else {
                //cout << "is a file" << endl;
                //cout << iter->path().string() << endl;
                wxString strVendor = from_u8(iter->path().string()).BeforeLast('.');
                strVendor          = strVendor.AfterLast( '\\');
                strVendor          = strVendor.AfterLast('\/');

                if (w2s(strVendor) != PresetBundle::BBL_BUNDLE)
                    LoadProfileFamily(w2s(strVendor), iter->path().string());
            }
        }


        //LoadProfileFamily(PresetBundle::BBL_BUNDLE, bbl_bundle_path.string());

        const auto enabled_filaments = wxGetApp().app_config->has_section(AppConfig::SECTION_FILAMENTS) ? wxGetApp().app_config->get_section(AppConfig::SECTION_FILAMENTS) : std::map<std::string, std::string>();
        m_appconfig_new.set_vendors(*wxGetApp().app_config);
        m_appconfig_new.set_section(AppConfig::SECTION_FILAMENTS, enabled_filaments);

        for (auto it = m_ProfileJson["model"].begin(); it != m_ProfileJson["model"].end(); ++it)
        {
            if (it.value().is_object()) {
                json& temp_model = it.value();
                std::string model_name = temp_model["model"];
                std::string vendor_name = temp_model["vendor"];
                std::string nozzle_diameter = temp_model["nozzle_diameter"];
                std::string selected;
                boost::trim(nozzle_diameter);
                std::string nozzle;
                bool enabled = false, first=true;
                while (nozzle_diameter.size() > 0) {
                    auto pos = nozzle_diameter.find(';');
                    if (pos != std::string::npos) {
                        nozzle   = nozzle_diameter.substr(0, pos);
                        enabled = m_appconfig_new.get_variant(vendor_name, model_name, nozzle);
                        if (enabled) {
                            if (!first)
                                selected += ";";
                            selected += nozzle;
                            first = false;
                        }
                        nozzle_diameter = nozzle_diameter.substr(pos + 1);
                        boost::trim(nozzle_diameter);
                    }
                    else {
                        enabled = m_appconfig_new.get_variant(vendor_name, model_name, nozzle_diameter);
                        if (enabled) {
                            if (!first)
                                selected += ";";
                            selected += nozzle_diameter;
                        }
                        break;
                    }
                }
                temp_model["nozzle_selected"] = selected;
                //m_ProfileJson["model"][a]["nozzle_selected"]
            }
        }

        if (m_ProfileJson["model"].size() == 1) {
            std::string strNozzle = m_ProfileJson["model"][0]["nozzle_diameter"];
            m_ProfileJson["model"][0]["nozzle_selected"]=strNozzle;
        }


        for (auto it = m_ProfileJson["filament"].begin(); it != m_ProfileJson["filament"].end(); ++it) {
            //json temp_filament = it.value();
            std::string filament_name = it.key();
            if (enabled_filaments.find(filament_name) != enabled_filaments.end())
                m_ProfileJson["filament"][filament_name]["selected"] = 1;
        }

        //----region
        m_Region = wxGetApp().app_config->get("region");
        m_ProfileJson["region"] = m_Region;

        m_ProfileJson["network_plugin_install"] = wxGetApp().app_config->get("app","installed_networking");
        m_ProfileJson["network_plugin_compability"] = wxGetApp().is_compatibility_version() ? "1" : "0";
    }
    catch (std::exception &e) {
        //wxLogMessage("GUIDE: load_profile_error  %s ", e.what());
        // wxMessageBox(e.what(), "", MB_OK);
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", error: "<< e.what() <<std::endl;
    }

    std::string strAll = m_ProfileJson.dump(-1, ' ', false, json::error_handler_t::ignore);
    //wxLogMessage("GUIDE: profile_json_s2  %s ", m_ProfileJson.dump(-1, ' ', false, json::error_handler_t::ignore));

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished, json contents: "<< std::endl<<strAll;
    return 0;
}

void StringReplace(string &strBase, string strSrc, string strDes)
{
    string::size_type pos    = 0;
    string::size_type srcLen = strSrc.size();
    string::size_type desLen = strDes.size();
    pos                      = strBase.find(strSrc, pos);
    while ((pos != string::npos)) {
        strBase.replace(pos, srcLen, strDes);
        pos = strBase.find(strSrc, (pos + desLen));
    }
}


//int GuideFrame::LoadProfileFamily(std::string strVendor, std::string strFilePath)
//{
//    //wxString strFolder = strFilePath.BeforeLast(boost::filesystem::path::preferred_separator);
//    boost::filesystem::path file_path(strFilePath);
//    boost::filesystem::path vendor_dir = boost::filesystem::absolute(file_path.parent_path()/ strVendor).make_preferred();
//    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  vendor path %1%.")% vendor_dir.string();
//    try {
//
//        //wxLogMessage("GUIDE: json_path1  %s", w2s(strFilePath));
//
//        std::string contents;
//        LoadFile(strFilePath, contents);
//        //wxLogMessage("GUIDE: json_path1 content: %s", contents);
//        json jLocal=json::parse(contents);
//        //wxLogMessage("GUIDE: json_path1 Loaded");
//
//        // BBS:models
//        json pmodels = jLocal["machine_model_list"];
//        int  nsize   = pmodels.size();
//
//        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  got %1% machine models")%nsize;
//
//        for (int n = 0; n < nsize; n++) {
//            json OneModel = pmodels.at(n);
//
//            OneModel["model"] = OneModel["name"];
//            OneModel.erase("name");
//
//            std::string s1 = OneModel["model"];
//            std::string s2 = OneModel["sub_path"];
//
//            boost::filesystem::path sub_path = boost::filesystem::absolute(vendor_dir / s2).make_preferred();
//            std::string sub_file = sub_path.string();
//
//            //wxLogMessage("GUIDE: json_path2  %s", w2s(ModelFilePath));
//            LoadFile(sub_file, contents);
//            //wxLogMessage("GUIDE: json_path2 content: %s", contents);
//            json     pm=json::parse(contents);
//            //wxLogMessage("GUIDE: json_path2  loaded");
//
//            OneModel["vendor"]          = strVendor;
//            std::string NozzleOpt = pm["nozzle_diameter"];
//            StringReplace(NozzleOpt, " ", "");
//            OneModel["nozzle_diameter"] = NozzleOpt;
//            OneModel["materials"]       = pm["default_materials"];
//
//            //wxString strCoverPath = wxString::Format("%s\\%s\\%s_cover.png", strFolder, strVendor, std::string(s1.mb_str()));
//            std::string cover_file = s1+"_cover.png";
//            boost::filesystem::path cover_path = boost::filesystem::absolute(vendor_dir / cover_file).make_preferred();
//            OneModel["cover"]   = cover_path.string();
//
//            OneModel["nozzle_selected"] = "";
//
//            m_ProfileJson["model"].push_back(OneModel);
//        }
//
//        // BBS:Machine
//        json pmachine = jLocal["machine_list"];
//        nsize         = pmachine.size();
//        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  got %1% machines")%nsize;
//        for (int n = 0; n < nsize; n++) {
//            json OneMachine = pmachine.at(n);
//
//            std::string s1 = OneMachine["name"];
//            std::string s2 = OneMachine["sub_path"];
//
//            //wxString ModelFilePath = wxString::Format("%s\\%s\\%s", strFolder, strVendor, s2);
//            boost::filesystem::path sub_path = boost::filesystem::absolute(vendor_dir / s2).make_preferred();
//            std::string sub_file = sub_path.string();
//            LoadFile(sub_file, contents);
//            json pm = json::parse(contents);
//
//            std::string strInstant = pm["instantiation"];
//            if (strInstant.compare("true") == 0) {
//                OneMachine["model"] = pm["printer_model"];
//
//                m_ProfileJson["machine"].push_back(OneMachine);
//            }
//        }
//
//        // BBS:Filament
//        json pFilament = jLocal["filament_list"];
//        nsize          = pFilament.size();
//
//        int nFalse = 0;
//        int nModel = 0;
//        int nFinish = 0;
//        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  got %1% filaments")%nsize;
//        for (int n = 0; n < nsize; n++) {
//            json OneFF = pFilament.at(n);
//
//            std::string s1 = OneFF["name"];
//            std::string s2 = OneFF["sub_path"];
//
//            if (!m_ProfileJson["filament"].contains(s1))
//            {
//                //wxString ModelFilePath = wxString::Format("%s\\%s\\%s", strFolder, strVendor, s2);
//                boost::filesystem::path sub_path = boost::filesystem::absolute(vendor_dir / s2).make_preferred();
//                std::string sub_file = sub_path.string();
//                LoadFile(sub_file, contents);
//                json pm = json::parse(contents);
//
//                std::string strInstant = pm["instantiation"];
//                if (strInstant == "true") {
//                    std::string sV;
//                    std::string sT;
//
//                    int nRet = GetFilamentInfo(sub_file, sV, sT);
//                    if (nRet != 0) continue;
//
//                    OneFF["vendor"] = sV;
//                    OneFF["type"] = sT;
//
//                    OneFF["models"] = "";
//                    OneFF["selected"] = 0;
//                }
//                else
//                    continue;
//
//            } else {
//                OneFF = m_ProfileJson["filament"][s1];
//            }
//
//            std::string vModel = "";
//            int nm    = m_ProfileJson["model"].size();
//            int bFind = 0;
//            for (int m = 0; m < nm; m++) {
//                std::string strFF = m_ProfileJson["model"][m]["materials"];
//                strFF          = (boost::format(";%1%;")%strFF).str();
//                std::string strTT = (boost::format(";%1%;")%s1).str();
//                if (strFF.find(strTT) != std::string::npos) {
//                    std::string sModel = m_ProfileJson["model"][m]["model"];
//
//                    vModel = (boost::format("%1%[%2%]")%vModel %sModel).str();
//                    bFind           = 1;
//                }
//            }
//
//            OneFF["models"]                    = vModel;
//
//            m_ProfileJson["filament"][s1] = OneFF;
//        }
//
//        //process
//        json pProcess = jLocal["process_list"];
//        nsize    = pProcess.size();
//        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  got %1% processes")%nsize;
//        for (int n = 0; n < nsize; n++) {
//            json OneProcess = pProcess.at(n);
//
//            std::string s2            = OneProcess["sub_path"];
//            //wxString ModelFilePath = wxString::Format("%s\\%s\\%s", strFolder, strVendor, s2);
//            boost::filesystem::path sub_path = boost::filesystem::absolute(vendor_dir / s2).make_preferred();
//            std::string sub_file = sub_path.string();
//            LoadFile(sub_file, contents);
//            json pm = json::parse(contents);
//
//            std::string bInstall = pm["instantiation"];
//            if (bInstall == "true")
//            {
//                m_ProfileJson["process"].push_back(OneProcess);
//            }
//        }
//
//    }
//    catch(nlohmann::detail::parse_error &err) {
//        BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": parse "<<strFilePath <<" got a nlohmann::detail::parse_error, reason = " << err.what();
//        return -1;
//    }
//    catch (std::exception &e) {
//        // wxMessageBox(e.what(), "", MB_OK);
//        //wxLogMessage("GUIDE: LoadFamily Error: %s", e.what());
//        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << strFilePath << " got exception: " << e.what();
//        return -1;
//    }
//
//    return 0;
//}

int GuideFrame::LoadProfileFamily(std::string strVendor, std::string strFilePath)
{
    // wxString strFolder = strFilePath.BeforeLast(boost::filesystem::path::preferred_separator);
    boost::filesystem::path file_path(strFilePath);
    boost::filesystem::path vendor_dir = boost::filesystem::absolute(file_path.parent_path() / strVendor).make_preferred();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  vendor path %1%.") % vendor_dir.string();
    try {
        // wxLogMessage("GUIDE: json_path1  %s", w2s(strFilePath));

        std::string contents;
        LoadFile(strFilePath, contents);
        // wxLogMessage("GUIDE: json_path1 content: %s", contents);
        json jLocal = json::parse(contents);
        // wxLogMessage("GUIDE: json_path1 Loaded");

        // BBS:models
        json pmodels = jLocal["machine_model_list"];
        int  nsize   = pmodels.size();

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  got %1% machine models") % nsize;

        for (int n = 0; n < nsize; n++) {
            json OneModel = pmodels.at(n);

            OneModel["model"] = OneModel["name"];
            OneModel.erase("name");

            std::string s1 = OneModel["model"];
            std::string s2 = OneModel["sub_path"];

            boost::filesystem::path sub_path = boost::filesystem::absolute(vendor_dir / s2).make_preferred();
            std::string             sub_file = sub_path.string();

            // wxLogMessage("GUIDE: json_path2  %s", w2s(ModelFilePath));
            LoadFile(sub_file, contents);
            // wxLogMessage("GUIDE: json_path2 content: %s", contents);
            json pm = json::parse(contents);
            // wxLogMessage("GUIDE: json_path2  loaded");

            OneModel["vendor"]    = strVendor;
            std::string NozzleOpt = pm["nozzle_diameter"];
            StringReplace(NozzleOpt, " ", "");
            OneModel["nozzle_diameter"] = NozzleOpt;
            OneModel["materials"]       = pm["default_materials"];

            // wxString strCoverPath = wxString::Format("%s\\%s\\%s_cover.png", strFolder, strVendor, std::string(s1.mb_str()));
            std::string             cover_file = s1 + "_cover.png";
            boost::filesystem::path cover_path = boost::filesystem::absolute(vendor_dir / cover_file).make_preferred();
            OneModel["cover"]                  = cover_path.string();

            OneModel["nozzle_selected"] = "";

            m_ProfileJson["model"].push_back(OneModel);
        }

        // BBS:Machine
        json pmachine = jLocal["machine_list"];
        nsize         = pmachine.size();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  got %1% machines") % nsize;
        for (int n = 0; n < nsize; n++) {
            json OneMachine = pmachine.at(n);

            std::string s1 = OneMachine["name"];
            std::string s2 = OneMachine["sub_path"];

            // wxString ModelFilePath = wxString::Format("%s\\%s\\%s", strFolder, strVendor, s2);
            boost::filesystem::path sub_path = boost::filesystem::absolute(vendor_dir / s2).make_preferred();
            std::string             sub_file = sub_path.string();
            LoadFile(sub_file, contents);
            json pm = json::parse(contents);

            std::string strInstant = pm["instantiation"];
            if (strInstant.compare("true") == 0) {
                OneMachine["model"] = pm["printer_model"];
                OneMachine["nozzle"] = pm["nozzle_diameter"][0];

                m_ProfileJson["machine"][s1]=OneMachine;
            }
        }

        // BBS:Filament
        json pFilament = jLocal["filament_list"];
        nsize          = pFilament.size();

        int nFalse  = 0;
        int nModel  = 0;
        int nFinish = 0;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  got %1% filaments") % nsize;
        for (int n = 0; n < nsize; n++) {
            json OneFF = pFilament.at(n);

            std::string s1 = OneFF["name"];
            std::string s2 = OneFF["sub_path"];

            if (!m_ProfileJson["filament"].contains(s1)) {
                // wxString ModelFilePath = wxString::Format("%s\\%s\\%s", strFolder, strVendor, s2);
                boost::filesystem::path sub_path = boost::filesystem::absolute(vendor_dir / s2).make_preferred();
                std::string             sub_file = sub_path.string();
                LoadFile(sub_file, contents);
                json pm = json::parse(contents);

                std::string strInstant = pm["instantiation"];
                if (strInstant == "true") {
                    std::string sV;
                    std::string sT;

                    int nRet = GetFilamentInfo(sub_file, sV, sT);
                    if (nRet != 0) continue;

                    OneFF["vendor"] = sV;
                    OneFF["type"]   = sT;

                    OneFF["models"]   = "";

                    json pPrinters = pm["compatible_printers"];
                    int nPrinter   = pPrinters.size();
                    std::string ModelList = "";
                    for (int i = 0; i < nPrinter; i++)
                    {
                        std::string sP = pPrinters.at(i);
                        if (m_ProfileJson["machine"].contains(sP))
                        {
                            std::string mModel = m_ProfileJson["machine"][sP]["model"];
                            std::string mNozzle = m_ProfileJson["machine"][sP]["nozzle"];
                            std::string NewModel = mModel + "++" + mNozzle;

                            ModelList = (boost::format("%1%[%2%]") % ModelList % NewModel).str();
                        }
                    }

                    OneFF["models"]    = ModelList;
                    OneFF["selected"] = 0;

                    m_ProfileJson["filament"][s1] = OneFF;
                } else
                    continue;

            }
        }

        // process
        json pProcess = jLocal["process_list"];
        nsize         = pProcess.size();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  got %1% processes") % nsize;
        for (int n = 0; n < nsize; n++) {
            json OneProcess = pProcess.at(n);

            std::string s2 = OneProcess["sub_path"];
            // wxString ModelFilePath = wxString::Format("%s\\%s\\%s", strFolder, strVendor, s2);
            boost::filesystem::path sub_path = boost::filesystem::absolute(vendor_dir / s2).make_preferred();
            std::string             sub_file = sub_path.string();
            LoadFile(sub_file, contents);
            json pm = json::parse(contents);

            std::string bInstall = pm["instantiation"];
            if (bInstall == "true") { m_ProfileJson["process"].push_back(OneProcess); }
        }

    } catch (nlohmann::detail::parse_error &err) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << strFilePath << " got a nlohmann::detail::parse_error, reason = " << err.what();
        return -1;
    } catch (std::exception &e) {
        // wxMessageBox(e.what(), "", MB_OK);
        // wxLogMessage("GUIDE: LoadFamily Error: %s", e.what());
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << strFilePath << " got exception: " << e.what();
        return -1;
    }

    return 0;
}


void GuideFrame::StrReplace(std::string &strBase, std::string strSrc, std::string strDes)
{
    int pos    = 0;
    int srcLen = strSrc.size();
    int desLen = strDes.size();
    pos = strBase.find(strSrc, pos);
    while ((pos != std::string::npos)) {
        strBase.replace(pos, srcLen, strDes);
        pos = strBase.find(strSrc, (pos + desLen));
    }
}

std::string GuideFrame::w2s(wxString sSrc)
{
    return std::string(sSrc.mb_str());
}

void GuideFrame::GetStardardFilePath(std::string &FilePath) {
    StrReplace(FilePath, "\\", w2s(wxString::Format("%c", boost::filesystem::path::preferred_separator)));
    StrReplace(FilePath, "\/", w2s(wxString::Format("%c", boost::filesystem::path::preferred_separator)));
}

bool GuideFrame::LoadFile(std::string jPath, std::string &sContent)
{
    try {
        boost::nowide::ifstream t(jPath);
        std::stringstream buffer;
        buffer << t.rdbuf();
        sContent=buffer.str();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", load %1% into buffer")% jPath;
    }
    catch (std::exception &e)
    {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",  got exception: "<<e.what();
        return false;
    }

    return true;
}

int GuideFrame::DownloadPlugin()
{
    return wxGetApp().download_plugin(
        "plugins", "network_plugin.zip", 
        [this](int status, int percent, bool& cancel) {
            return ShowPluginStatus(status, percent, cancel);
        }
    , nullptr);
}

int GuideFrame::InstallPlugin()
{
    return wxGetApp().install_plugin("plugins", "network_plugin.zip", 
        [this](int status, int percent, bool &cancel) {
            return ShowPluginStatus(status, percent, cancel);
        }
    );
}

int GuideFrame::ShowPluginStatus(int status, int percent, bool& cancel)
{
    //TODO
    return 0;
}



}} // namespace Slic3r::GUI
