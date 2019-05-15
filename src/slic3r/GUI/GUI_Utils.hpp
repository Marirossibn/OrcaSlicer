#ifndef slic3r_GUI_Utils_hpp_
#define slic3r_GUI_Utils_hpp_

#include <memory>
#include <string>
#include <ostream>
#include <functional>

#include <boost/optional.hpp>

#include <wx/frame.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/dcclient.h>
#include <wx/debug.h>
#include <wx/settings.h>

class wxCheckBox;
class wxTopLevelWindow;
class wxRect;


namespace Slic3r {
namespace GUI {


wxTopLevelWindow* find_toplevel_parent(wxWindow *window);

void on_window_geometry(wxTopLevelWindow *tlw, std::function<void()> callback);

enum { DPI_DEFAULT = 96 };

int get_dpi_for_window(wxWindow *window);

struct DpiChangedEvent : public wxEvent {
    int dpi;
    wxRect rect;

    DpiChangedEvent(wxEventType eventType, int dpi, wxRect rect)
        : wxEvent(0, eventType), dpi(dpi), rect(rect)
    {}

    virtual wxEvent *Clone() const
    {
        return new DpiChangedEvent(*this);
    }
};

wxDECLARE_EVENT(EVT_DPI_CHANGED, DpiChangedEvent);

template<class P> class DPIAware : public P
{
public:
    DPIAware(wxWindow *parent, wxWindowID id, const wxString &title, const wxPoint &pos=wxDefaultPosition,
        const wxSize &size=wxDefaultSize, long style=wxDEFAULT_FRAME_STYLE, const wxString &name=wxFrameNameStr)
        : P(parent, id, title, pos, size, style, name)
    {
        m_scale_factor = (float)get_dpi_for_window(this) / (float)DPI_DEFAULT;
        m_prev_scale_factor = m_scale_factor;
		float scale_primary_display = (float)get_dpi_for_window(nullptr) / (float)DPI_DEFAULT;
		m_normal_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
		if (std::abs(m_scale_factor - scale_primary_display) > 1e-6)
			m_normal_font = m_normal_font.Scale(m_scale_factor / scale_primary_display);

        // initialize default width_unit according to the width of the one symbol ("m") of the currently active font of this window.
        m_em_unit = std::max<size_t>(10, this->GetTextExtent("m").x - 1/*10 * m_scale_factor*/);

//        recalc_font();

        this->Bind(EVT_DPI_CHANGED, [this](const DpiChangedEvent &evt) {
            m_scale_factor = (float)evt.dpi / (float)DPI_DEFAULT;

            if (!m_can_rescale)
                return;

            if (is_new_scale_factor())
                rescale(evt.rect);
        });

        this->Bind(wxEVT_MOVE_START, [this](wxMoveEvent& event)
        {
            event.Skip();

            // Suppress application rescaling, when a MainFrame moving is not ended
            m_can_rescale = false;
        });

        this->Bind(wxEVT_MOVE_END, [this](wxMoveEvent& event)
        {
            event.Skip();

            m_can_rescale = is_new_scale_factor();

            // If scale factor is different after moving of MainFrame ...
            if (m_can_rescale)
                // ... rescale application
                rescale(event.GetRect());
            else
            // set value to _true_ in purpose of possibility of a display dpi changing from System Settings
                m_can_rescale = true;
        });
    }

    virtual ~DPIAware() {}

    float   scale_factor() const        { return m_scale_factor; }
    float   prev_scale_factor() const   { return m_prev_scale_factor; }

    int     em_unit() const             { return m_em_unit; }
//    int     font_size() const           { return m_font_size; }
    const wxFont& normal_font() const   { return m_normal_font; }

protected:
    virtual void on_dpi_changed(const wxRect &suggested_rect) = 0;

private:
    float m_scale_factor;
    int m_em_unit;
//    int m_font_size;

    wxFont m_normal_font;
    float m_prev_scale_factor;
    bool  m_can_rescale{ true };

//    void recalc_font()
//    {
//        wxClientDC dc(this);
//        const auto metrics = dc.GetFontMetrics();
//        m_font_size = metrics.height;
//         m_em_unit = metrics.averageWidth;
//    }

    // check if new scale is differ from previous
    bool    is_new_scale_factor() const { return fabs(m_scale_factor - m_prev_scale_factor) > 0.001; }

    // recursive function for scaling fonts for all controls in Window
    void    scale_controls_fonts(wxWindow *window, const float scale_f)
    {
        auto children = window->GetChildren();

        for (auto child : children) {
            scale_controls_fonts(child, scale_f);
            child->SetFont(child->GetFont().Scaled(scale_f));
        }

        window->Layout();
    }

    void    rescale(const wxRect &suggested_rect)
    {
        this->Freeze();
        const float relative_scale_factor = m_scale_factor / m_prev_scale_factor;

        // rescale fonts of all controls
        scale_controls_fonts(this, relative_scale_factor);
        this->SetFont(this->GetFont().Scaled(relative_scale_factor));


        // rescale normal_font value
        m_normal_font = m_normal_font.Scaled(relative_scale_factor);

        // An analog of em_unit value from GUI_App.
        m_em_unit = std::max<size_t>(10, 10 * m_scale_factor);

        // rescale missed controls sizes and images
        on_dpi_changed(suggested_rect);

        this->Layout();
        this->Thaw();

        // reset previous scale factor from current scale factor value
        m_prev_scale_factor = m_scale_factor;
    }

};

typedef DPIAware<wxFrame> DPIFrame;
typedef DPIAware<wxDialog> DPIDialog;


class EventGuard
{
    // This is a RAII-style smart-ptr-like guard that will bind any event to any event handler
    // and unbind it as soon as it goes out of scope or unbind() is called.
    // This can be used to solve the annoying problem of wx events being delivered to freed objects.

private:
    // This is a way to type-erase both the event type as well as the handler:

    struct EventStorageBase {
        virtual ~EventStorageBase() {}
    };

    template<class EvTag, class Fun>
    struct EventStorageFun : EventStorageBase {
        wxEvtHandler *emitter;
        EvTag tag;
        Fun fun;

        EventStorageFun(wxEvtHandler *emitter, const EvTag &tag, Fun fun)
            : emitter(emitter)
            , tag(tag)
            , fun(std::move(fun))
        {
            emitter->Bind(this->tag, this->fun);
        }

        virtual ~EventStorageFun() { emitter->Unbind(tag, fun); }
    };

    template<typename EvTag, typename Class, typename EvArg, typename EvHandler>
    struct EventStorageMethod : EventStorageBase {
        typedef void(Class::* MethodPtr)(EvArg &);

        wxEvtHandler *emitter;
        EvTag tag;
        MethodPtr method;
        EvHandler *handler;

        EventStorageMethod(wxEvtHandler *emitter, const EvTag &tag, MethodPtr method, EvHandler *handler)
            : emitter(emitter)
            , tag(tag)
            , method(method)
            , handler(handler)
        {
            emitter->Bind(tag, method, handler);
        }

        virtual ~EventStorageMethod() { emitter->Unbind(tag, method, handler); }
    };

    std::unique_ptr<EventStorageBase> event_storage;
public:
    EventGuard() {}
    EventGuard(const EventGuard&) = delete;
    EventGuard(EventGuard &&other) : event_storage(std::move(other.event_storage)) {}

    template<class EvTag, class Fun>
    EventGuard(wxEvtHandler *emitter, const EvTag &tag, Fun fun)
        :event_storage(new EventStorageFun<EvTag, Fun>(emitter, tag, std::move(fun)))
    {}

    template<typename EvTag, typename Class, typename EvArg, typename EvHandler>
    EventGuard(wxEvtHandler *emitter, const EvTag &tag, void(Class::* method)(EvArg &), EvHandler *handler)
        :event_storage(new EventStorageMethod<EvTag, Class, EvArg, EvHandler>(emitter, tag, method, handler))
    {}

    EventGuard& operator=(const EventGuard&) = delete;
    EventGuard& operator=(EventGuard &&other)
    {
        event_storage = std::move(other.event_storage);
        return *this;
    }

    void unbind() { event_storage.reset(nullptr); }
    explicit operator bool() const { return !!event_storage; }
};


class CheckboxFileDialog : public wxFileDialog
{
public:
    CheckboxFileDialog(wxWindow *parent,
        const wxString &checkbox_label,
        bool checkbox_value,
        const wxString &message = wxFileSelectorPromptStr,
        const wxString &default_dir = wxEmptyString,
        const wxString &default_file = wxEmptyString,
        const wxString &wildcard = wxFileSelectorDefaultWildcardStr,
        long style = wxFD_DEFAULT_STYLE,
        const wxPoint &pos = wxDefaultPosition,
        const wxSize &size = wxDefaultSize,
        const wxString &name = wxFileDialogNameStr
    );

    bool get_checkbox_value() const;

private:
    struct ExtraPanel : public wxPanel
    {
        wxCheckBox *cbox;

        ExtraPanel(wxWindow *parent);
        static wxWindow* ctor(wxWindow *parent);
    };

    wxString checkbox_label;
};


class WindowMetrics
{
private:
    wxRect rect;
    bool maximized;

    WindowMetrics() : maximized(false) {}
public:
    static WindowMetrics from_window(wxTopLevelWindow *window);
    static boost::optional<WindowMetrics> deserialize(const std::string &str);

    wxRect get_rect() const { return rect; }
    bool get_maximized() const { return maximized; }

    void sanitize_for_display(const wxRect &screen_rect);
    std::string serialize() const;
};

std::ostream& operator<<(std::ostream &os, const WindowMetrics& metrics);


}}

#endif
